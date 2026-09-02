// Engine's side of the OpenXR integration.
//
// Kept out of Engine.cpp on purpose: this is the only place the engine talks to
// XRSystem, and keeping it in its own translation unit means the XR frame loop
// can be read (and reviewed) without wading through the 1000-line main loop it
// hooks into. Engine.cpp calls exactly three functions from here.
//
// The rule this file exists to enforce: with OpenXR disabled, none of it does
// anything. isXRRequestedByProject() is false, updateXRSystem() returns
// immediately, renderXRFrame() returns false, and the engine's normal render and
// present path runs untouched.

#include "Engine.h"

#include "XR/XRDiagnostics.h"
#include "XR/XRInput.h"
#include "XR/XRSystem.h"

#ifdef __ANDROID__
#include "AndroidRuntime/AndroidRuntime.h"
#endif

#include <string>

namespace {

// The XR camera's clip planes come from the project's runtime camera so an XR
// scene is configured the same way a flat one is. These are only the fallbacks
// for a project with no camera object yet.
constexpr float kDefaultXRNearPlane = 0.05f;
constexpr float kDefaultXRFarPlane = 1000.0f;

} // namespace

bool Engine::isXRRequestedByProject() const {
    if (!projectManager.currentProject.isLoaded) return false;
    if (!projectManager.currentProject.openXRSettings.enabled) return false;
#if !MODULARITY_HAS_OPENXR
    return false;
#else
    // Vulkan sessions are not supported and never will be by this path. Rather
    // than fail obscurely at xrCreateSession, refuse up front: OpenXR being on
    // does not imply Vulkan, and Vulkan being on does not imply XR.
    if (graphicsBackend == Modularity::GraphicsBackend::Vulkan) return false;
    return true;
#endif
}

void Engine::updateXRSystem() {
    xrFrameSubmitted = false;

    if (!isXRRequestedByProject()) {
        // The project turned XR off (or was closed) while a session was live.
        if (xrSystem.isCreated()) shutdownXRSystem();
        xrStartupAttempted = false;
        return;
    }

    if (!xrSystem.isCreated()) {
        // One attempt per project load. A machine with no OpenXR runtime would
        // otherwise pay a failed dlopen every single frame.
        if (xrStartupAttempted) return;
        xrStartupAttempted = true;

        Modularity::XR::XRSystem::PlatformHandles platform;
#ifdef __ANDROID__
        platform.applicationVM = Modularity::AndroidRuntime::GetJavaVM();
        platform.applicationActivity = Modularity::AndroidRuntime::GetActivityObject();
        platform.graphics.eglDisplay = Modularity::AndroidRuntime::GetEglDisplay();
        platform.graphics.eglConfig = Modularity::AndroidRuntime::GetEglConfig();
        platform.graphics.eglContext = Modularity::AndroidRuntime::GetEglContext();
#endif

        xrStartupError.clear();
        if (!xrSystem.startup(projectManager.currentProject.openXRSettings, platform,
                              xrStartupError)) {
            // Not fatal, by design (section 5): the engine carries on flat. The
            // reason is surfaced in the console and in Project Settings rather
            // than being reduced to "VR failed".
            addConsoleMessage("OpenXR unavailable: " + xrStartupError,
                              ConsoleMessageType::Warning);
            return;
        }
        addConsoleMessage("OpenXR session started (" + xrSystem.runtime().runtimeName() + ")",
                          ConsoleMessageType::Success);
        return;
    }

    // Drains OpenXR events and drives the session state machine. A false return
    // means the runtime asked us to stop - headset removed, app exiting, runtime
    // lost - and the only correct response is a clean teardown.
    if (!xrSystem.update()) {
        addConsoleMessage("OpenXR session ended.", ConsoleMessageType::Info);
        shutdownXRSystem();
        // Deliberately not resetting xrStartupAttempted: an exiting session
        // should not be restarted on the next frame in a loop. Reloading the
        // project (or toggling the setting) is what starts XR again.
    }
}

bool Engine::renderXRFrame() {
#if !MODULARITY_HAS_OPENXR
    return false;
#else
    if (!xrSystem.isRunning() || !rendererInitialized) return false;

    // The XR camera. Reuses the project's runtime camera for everything that is
    // not tracking - clip planes, culling mask, background, post FX volumes - so
    // an XR project is configured exactly like a flat one and there is no second
    // camera pipeline (section 8).
    Camera xrCamera = camera;
    float nearPlane = kDefaultXRNearPlane;
    float farPlane = kDefaultXRFarPlane;
    // An object carrying an XR Camera component wins over the plain player
    // camera, so a scene can keep a flat-screen camera around without it
    // hijacking the headset. Falls back to the player camera when the rig has
    // not been set up yet.
    const SceneObject* runtimeCam = findXRCameraObject();
    if (!runtimeCam) runtimeCam = findPlayerCameraObject();
    if (runtimeCam) {
        xrCamera = makeCameraFromObject(*runtimeCam);
        nearPlane = std::max(0.01f, runtimeCam->camera.nearClip);
        farPlane = std::max(nearPlane + 0.01f, runtimeCam->camera.farClip);
    }
    xrSystem.setClipPlanes(nearPlane, farPlane);

    // The XR Origin transform was already pushed by updateXRComponents earlier
    // this frame, so nothing to do here - deliberately not overwritten, which is
    // what it used to do while the component did not exist yet.

    // xrWaitFrame paces us here. A false return is normal (headset off the head,
    // app backgrounded): the frame still has to be ended, which is why endFrame
    // is unconditional below.
    const bool render = xrSystem.beginFrame();
    if (render) {
        const uint32_t viewCount = xrSystem.viewCount();
        for (uint32_t viewIndex = 0; viewIndex < viewCount; ++viewIndex) {
            Modularity::XR::XRRenderView view;
            if (!xrSystem.beginView(viewIndex, view)) continue;

            // The eye's own view matrix, composed with the XR Origin transform.
            // Written into the Camera the renderer already understands, so
            // renderSceneToTarget needs no XR-specific knowledge at all.
            const glm::mat4 viewMatrix = xrSystem.viewMatrix(viewIndex);
            const glm::mat4 projection = xrSystem.projectionMatrix(viewIndex);

            // Camera::getViewMatrix builds its matrix from position/front/up, so
            // the tracked pose is decomposed back into those rather than the view
            // matrix being injected directly. Keeping the Camera authoritative is
            // what lets every existing renderer path (culling, shadows, post FX,
            // mirrors) keep working unchanged.
            const glm::mat4 eyeToWorld = glm::inverse(viewMatrix);
            xrCamera.position = glm::vec3(eyeToWorld[3]);
            xrCamera.front = glm::normalize(-glm::vec3(eyeToWorld[2]));
            xrCamera.up = glm::normalize(glm::vec3(eyeToWorld[1]));

            renderer.beginRender(viewMatrix, projection, xrCamera.position);
            renderer.renderSceneToTarget(xrCamera, sceneObjects, view.framebuffer,
                                         static_cast<int>(view.width),
                                         static_cast<int>(view.height), projection, nearPlane,
                                         farPlane);
            renderer.endRender();

            xrSystem.endView(viewIndex);
        }
    }

    // Always: every xrBeginFrame must be answered by an xrEndFrame, including
    // frames the runtime told us not to render. Skipping it deadlocks xrWaitFrame.
    xrSystem.endFrame();
    return render;
#endif
}

void Engine::shutdownXRSystem() {
    if (!xrSystem.isCreated()) {
        Modularity::XR::ResetInputState();
        return;
    }
    xrSystem.shutdown();
    xrFrameSubmitted = false;
    Modularity::XR::ResetInputState();
}
