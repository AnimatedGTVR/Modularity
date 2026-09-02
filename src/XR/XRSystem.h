#pragma once

// The engine-facing face of OpenXR.
//
// Engine owns exactly one of these and never touches XRRuntime, XRSession,
// XRSwapchainGL or XRInput directly. That is the whole point of the layering in
// section 48: everything above this line talks about views, poses and render
// targets, and nothing above it mentions an XrSession.
//
// Per-frame contract, which mirrors OpenXR's own and must be followed in order:
//
//     if (!xr.update())            -> XR asked to stop; shut it down
//     if (xr.beginFrame()) {
//         for each view:
//             xr.beginView(i, fb)  -> acquire + wait, bind this FBO
//             ...engine renders... -> Modularity's own renderer, unchanged
//             xr.endView(i)        -> release
//     }
//     xr.endFrame()                -> always, even when nothing was rendered
//
// endFrame() is called unconditionally on purpose: OpenXR requires every
// xrBeginFrame to be answered by an xrEndFrame, including frames the runtime
// told us not to render. Skipping it is how an app ends up deadlocked against
// xrWaitFrame.

#include "XRInput.h"
#include "XRMath.h"
#include "XRRuntime.h"
#include "XRSession.h"
#include "XRSettings.h"
#include "XRSwapchainGL.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Modularity::XR {

// What the engine needs to know to draw one eye.
struct XRRenderView {
    XRView view;              // pose + asymmetric FOV, from the runtime
    uint32_t framebuffer = 0; // bound target for this eye
    uint32_t width = 0;
    uint32_t height = 0;
    // Layer inside the array texture when multiview is in use; always 0 for
    // Multi Pass. The renderer uses this to select the array slice to draw into.
    uint32_t arrayLayer = 0;
};

class XRSystem {
public:
    XRSystem() = default;
    ~XRSystem();

    XRSystem(const XRSystem&) = delete;
    XRSystem& operator=(const XRSystem&) = delete;

    // Everything the platform has to hand over. On Android these come from
    // AndroidRuntime; off Android they are empty and startup reports that no
    // graphics binding is available.
    struct PlatformHandles {
        void* applicationVM = nullptr;
        void* applicationActivity = nullptr;
        XRGraphicsBindingGLES graphics;
    };

    // Brings up loader -> instance -> system -> session -> swapchains -> input.
    //
    // Returns false with a readable reason for every failure mode, including the
    // routine ones (OpenXR disabled in settings, no runtime installed, no headset
    // connected). A false return always leaves XR fully shut down, so the caller
    // simply carries on without VR.
    bool startup(const ProjectOpenXRSettings& settings, const PlatformHandles& platform,
                 std::string& outError);

    void shutdown();

    // True once a session exists. Not the same as running: a created session that
    // has not reached READY yet still reports true here.
    bool isCreated() const { return session_.isCreated(); }

    // True when frames should be submitted. This is the flag the engine's render
    // path keys off.
    bool isRunning() const { return session_.isRunning(); }

    // Drains OpenXR events and keeps the session state machine moving. Call once
    // per frame before anything else. Returns false when XR must be shut down.
    bool update();

    // xrWaitFrame + xrBeginFrame + xrLocateViews.
    //
    // Returns true when the runtime wants this frame rendered. A false return is
    // normal (headset off the head, app backgrounded) and the caller must still
    // call endFrame().
    bool beginFrame();

    // Acquires and waits on the swapchain image for one view, and fills `outView`
    // with everything needed to render it. Must be paired with endView.
    bool beginView(uint32_t viewIndex, XRRenderView& outView);
    void endView(uint32_t viewIndex);

    // xrEndFrame. Submits the projection layer for whatever was rendered. Always
    // call this once per successful beginFrame, rendered or not.
    void endFrame();

    // Number of eyes the runtime is asking for (2 for stereo).
    uint32_t viewCount() const { return static_cast<uint32_t>(views_.size()); }

    // The render mode actually in use after runtime fallbacks, which may differ
    // from the project setting. Without a GLES build there are no swapchains to
    // ask, so this reports what was configured rather than a resolved value.
    XRRenderMode activeRenderMode() const {
#if MODULARITY_HAS_OPENXR && MODULARITY_XR_HAS_GLES_TYPES
        return swapchains_.resolvedRenderMode();
#else
        return settings_.renderMode;
#endif
    }

    // Where the player's tracking space sits in the world. Set by the XR Origin
    // component each frame; identity until then, which puts tracking space at the
    // world origin rather than leaving poses undefined.
    void setOriginToWorld(const glm::mat4& matrix) { originToWorld_ = matrix; }
    const glm::mat4& originToWorld() const { return originToWorld_; }

    // Near/far the XR projection is built with. Sourced from the active XR camera
    // so an XR project's clip planes are configured the same way as a flat one.
    void setClipPlanes(float nearPlane, float farPlane);
    float nearPlane() const { return nearPlane_; }
    float farPlane() const { return farPlane_; }

    const XRRuntime& runtime() const { return runtime_; }
    const XRSession& session() const { return session_; }

    // View and projection for one eye, composed with the XR Origin transform.
    // This is what feeds Modularity's existing camera matrices (section 8).
    glm::mat4 viewMatrix(uint32_t viewIndex) const;
    glm::mat4 projectionMatrix(uint32_t viewIndex) const;

private:
    bool createSwapchains(std::string& outError);

    XRRuntime runtime_;
    XRSession session_;
    ProjectOpenXRSettings settings_;

#if MODULARITY_HAS_OPENXR && MODULARITY_XR_HAS_GLES_TYPES
    XRSwapchainSetGL swapchains_;
#endif
#if MODULARITY_HAS_OPENXR
    XRInput input_;
    XrFrameState frameState_{};
    std::vector<XrCompositionLayerProjectionView> projectionViews_;
    XrCompositionLayerProjection projectionLayer_{};
#endif

    std::vector<XRRenderView> views_;
    glm::mat4 originToWorld_ = glm::mat4(1.0f);
    float nearPlane_ = 0.05f;
    float farPlane_ = 1000.0f;

    // Set between a successful beginFrame and endFrame. Guards against an
    // xrEndFrame with no matching xrBeginFrame, which is a spec violation some
    // runtimes answer by hanging.
    bool frameStarted_ = false;
    bool shouldRender_ = false;
};

} // namespace Modularity::XR
