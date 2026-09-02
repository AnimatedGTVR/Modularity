#pragma once

// OpenXR session lifecycle.
//
// Owns everything that exists between xrCreateSession and xrDestroySession: the
// XrSession itself, the reference spaces the XR Origin maps onto, and the event
// loop that drives OpenXR's session state machine. It deliberately stops short of
// swapchains and frame submission, which belong to XRSwapchainGL / XRFrameLoop.
//
// The state machine is the reason this is its own file. OpenXR requires the app
// to react to xrPollEvent - xrBeginSession only after READY, xrEndSession only
// after STOPPING - and getting that wrong on Quest shows up as an app that hangs
// on a black screen when you take the headset off. Section 5's requirement is
// that every failure lands in a controlled state, so LOSS_PENDING, EXITING,
// instance loss and a runtime that simply vanishes all route to the same clean
// teardown rather than to an assert.
//
// Threading: single-threaded by contract. The engine's render thread owns the
// session, the same thread that owns the GL context OpenXR was bound to.

#include "XRDiagnostics.h"
#include "XRRuntime.h"
#include "XRSettings.h"

#include <string>

#if MODULARITY_HAS_OPENXR
#include "XRPlatform.h"
#endif

namespace Modularity::XR {

// The graphics handles OpenXR binds its session to. Passed as void* so callers
// outside src/XR/ (AndroidRuntime, Engine) never need the EGL headers, and so a
// platform without a binding can pass an empty one without conditional code.
//
// These are Modularity's *existing* EGL objects, not new ones: OpenXR renders
// into swapchain images owned by the runtime using the context the engine
// already created, which is what keeps this from becoming a second renderer.
struct XRGraphicsBindingGLES {
    void* eglDisplay = nullptr;  // EGLDisplay
    void* eglConfig = nullptr;   // EGLConfig
    void* eglContext = nullptr;  // EGLContext

    // eglConfig is allowed to be null: some runtimes accept it, and a null here
    // is reported by the runtime rather than pre-rejected, so the error the user
    // sees is the runtime's own.
    bool hasContext() const { return eglDisplay != nullptr && eglContext != nullptr; }
};

// Modularity's view of XrSessionState. Same values, but named for what the engine
// does about them, and with a None for "no session at all" which OpenXR has no
// enumerant for.
enum class XRSessionStatus {
    None = 0,      // no session exists
    Idle,          // created, runtime not ready for frames yet
    Ready,         // READY seen; xrBeginSession has run, frames are expected
    Synchronized,  // frames are being submitted but nothing is displayed
    Visible,       // displayed, but not receiving input focus
    Focused,       // displayed and focused - the normal running state
    Stopping,      // runtime asked us to stop; xrEndSession has run
    LossPending,   // session is being lost; tear down
    Exiting        // runtime asked the app to exit
};

const char* ToString(XRSessionStatus status);

class XRSession {
public:
    XRSession() = default;
    ~XRSession();

    XRSession(const XRSession&) = delete;
    XRSession& operator=(const XRSession&) = delete;

    // Creates the session and its reference spaces against an already-initialized
    // XRRuntime that has a system. `binding` must carry a live GL ES context.
    //
    // Returns false with a readable `outError` on any failure, leaving nothing
    // half-created. A false return means "run without XR", never "crash".
    bool create(const XRRuntime& runtime,
                const ProjectOpenXRSettings& settings,
                const XRGraphicsBindingGLES& binding,
                std::string& outError);

    void destroy();

    bool isCreated() const { return created_; }

    // True between xrBeginSession and xrEndSession. Frame submission is only
    // legal in this window.
    bool isRunning() const { return running_; }

    // True when submitted frames actually reach the display (VISIBLE or FOCUSED).
    // When false the frame loop still runs - OpenXR requires frames to keep being
    // submitted while synchronized - but the scene render can be skipped.
    bool shouldRender() const {
        return status_ == XRSessionStatus::Visible || status_ == XRSessionStatus::Focused;
    }

    // True only in FOCUSED. Input actions are not delivered otherwise, so gameplay
    // that reads controllers should pause rather than see every button read false.
    bool hasFocus() const { return status_ == XRSessionStatus::Focused; }

    XRSessionStatus status() const { return status_; }

    // Set once the runtime has asked the app to exit or the session has been lost.
    // The caller should tear the session down; it will not recover on its own.
    bool wantsTeardown() const {
        return status_ == XRSessionStatus::Exiting || status_ == XRSessionStatus::LossPending;
    }

    // Drains xrPollEvent and drives xrBeginSession / xrEndSession. Call exactly
    // once per frame before any frame work.
    //
    // Returns false when XR must be shut down (exit requested, session or instance
    // lost, or an unrecoverable poll error). A false return is a normal outcome:
    // taking the headset off and ending the app both produce it.
    bool pollEvents();

    // Asks the runtime to end the session cleanly. The actual teardown still comes
    // back through pollEvents as STOPPING -> IDLE -> EXITING, which is what the
    // spec requires; this only starts it.
    void requestExit();

    // The reference space the XR Origin maps onto: STAGE for Floor, LOCAL for Eye
    // Level. Falls back to LOCAL (with a log line) when the runtime does not offer
    // STAGE, so a roomscale project still runs seated instead of failing.
    bool usingFallbackReferenceSpace() const { return referenceSpaceFellBack_; }
    XRTrackingOrigin effectiveTrackingOrigin() const { return effectiveTrackingOrigin_; }

#if MODULARITY_HAS_OPENXR
    XrSession handle() const { return session_; }
    // Tracking space for world-anchored poses (controllers, head position).
    XrSpace appSpace() const { return appSpace_; }
    // VIEW space: poses relative to the head, for head-locked content.
    XrSpace viewSpace() const { return viewSpace_; }
    XrInstance instance() const { return instance_; }
    XrSystemId systemId() const { return systemId_; }
    XrViewConfigurationType viewConfigurationType() const { return viewConfigurationType_; }
    XrEnvironmentBlendMode blendMode() const { return blendMode_; }

    // Set by the frame loop when an interaction profile change event arrives, so
    // the input layer can re-read which controllers are bound without polling.
    bool consumeInteractionProfileChanged();
#endif

private:
#if MODULARITY_HAS_OPENXR
    bool checkGraphicsRequirements(std::string& outError);
    bool createSession(const XRGraphicsBindingGLES& binding, std::string& outError);
    bool createReferenceSpaces(const ProjectOpenXRSettings& settings, std::string& outError);
    bool beginSession(std::string& outError);
    void endSession();
    void handleStateChange(const XrEventDataSessionStateChanged& event);

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId systemId_ = XR_NULL_SYSTEM_ID;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace appSpace_ = XR_NULL_HANDLE;
    XrSpace viewSpace_ = XR_NULL_HANDLE;
    XrViewConfigurationType viewConfigurationType_ = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    XrEnvironmentBlendMode blendMode_ = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    bool interactionProfileChanged_ = false;
#endif

    bool created_ = false;
    bool running_ = false;
    bool exitRequested_ = false;
    bool referenceSpaceFellBack_ = false;
    XRSessionStatus status_ = XRSessionStatus::None;
    XRTrackingOrigin effectiveTrackingOrigin_ = XRTrackingOrigin::Floor;
};

} // namespace Modularity::XR
