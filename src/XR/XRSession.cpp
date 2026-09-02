#include "XRSession.h"

#include "XRLoader.h"

#include <algorithm>
#include <vector>

namespace Modularity::XR {

const char* ToString(XRSessionStatus status) {
    switch (status) {
        case XRSessionStatus::Idle:         return "Idle";
        case XRSessionStatus::Ready:        return "Ready";
        case XRSessionStatus::Synchronized: return "Synchronized";
        case XRSessionStatus::Visible:      return "Visible";
        case XRSessionStatus::Focused:      return "Focused";
        case XRSessionStatus::Stopping:     return "Stopping";
        case XRSessionStatus::LossPending:  return "Loss Pending";
        case XRSessionStatus::Exiting:      return "Exiting";
        case XRSessionStatus::None:
        default:                            return "None";
    }
}

XRSession::~XRSession() { destroy(); }

#if !MODULARITY_HAS_OPENXR

bool XRSession::create(const XRRuntime&, const ProjectOpenXRSettings&,
                       const XRGraphicsBindingGLES&, std::string& outError) {
    outError = "This build of Modularity was compiled without OpenXR support "
               "(MODULARITY_ENABLE_OPENXR=OFF).";
    return false;
}

void XRSession::destroy() {
    created_ = false;
    running_ = false;
    status_ = XRSessionStatus::None;
}

bool XRSession::pollEvents() { return false; }

void XRSession::requestExit() { exitRequested_ = true; }

#else // MODULARITY_HAS_OPENXR

namespace {

XRSessionStatus fromXrState(XrSessionState state) {
    switch (state) {
        case XR_SESSION_STATE_IDLE:         return XRSessionStatus::Idle;
        case XR_SESSION_STATE_READY:        return XRSessionStatus::Ready;
        case XR_SESSION_STATE_SYNCHRONIZED: return XRSessionStatus::Synchronized;
        case XR_SESSION_STATE_VISIBLE:      return XRSessionStatus::Visible;
        case XR_SESSION_STATE_FOCUSED:      return XRSessionStatus::Focused;
        case XR_SESSION_STATE_STOPPING:     return XRSessionStatus::Stopping;
        case XR_SESSION_STATE_LOSS_PENDING: return XRSessionStatus::LossPending;
        case XR_SESSION_STATE_EXITING:      return XRSessionStatus::Exiting;
        case XR_SESSION_STATE_UNKNOWN:
        default:                            return XRSessionStatus::None;
    }
}

} // namespace

bool XRSession::create(const XRRuntime& runtime,
                       const ProjectOpenXRSettings& settings,
                       const XRGraphicsBindingGLES& binding,
                       std::string& outError) {
    outError.clear();
    destroy();

    if (!runtime.isInitialized() || runtime.instance() == XR_NULL_HANDLE) {
        outError = "Cannot create an OpenXR session before the OpenXR instance exists.";
        return false;
    }
    if (!runtime.hasSystem()) {
        outError = "Cannot create an OpenXR session: no headset has been acquired yet.";
        return false;
    }

    instance_ = runtime.instance();
    systemId_ = runtime.systemId();
    viewConfigurationType_ = runtime.viewConfigurationType();

    // Prefer OPAQUE (a normal VR app). Passthrough would want ALPHA_BLEND, but
    // that extension is not implemented, so asking for it here would be exactly
    // the kind of fake support section 34 rules out.
    blendMode_ = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    const std::vector<XrEnvironmentBlendMode>& modes = runtime.blendModes();
    if (!modes.empty() &&
        std::find(modes.begin(), modes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE) == modes.end()) {
        // The runtime does not do opaque at all (rare, AR-only hardware). Take
        // whatever it offers first rather than failing outright.
        blendMode_ = modes.front();
        XRLogWarn("Runtime does not support an opaque blend mode; using the first "
                  "advertised mode instead.");
    }

    if (!checkGraphicsRequirements(outError) ||
        !createSession(binding, outError) ||
        !createReferenceSpaces(settings, outError)) {
        destroy();
        MutableDiagnostics().lastError = outError;
        return false;
    }

    created_ = true;
    status_ = XRSessionStatus::Idle;
    MutableDiagnostics().sessionCreated = true;
    XRLogInfo("Session created");
    return true;
}

bool XRSession::checkGraphicsRequirements(std::string& outError) {
#if MODULARITY_XR_HAS_GLES_TYPES
    const XRExtensionFunctions& ext = XRLoaderExtensionFunctions();
    if (!ext.GetOpenGLESGraphicsRequirementsKHR) {
        outError = "The OpenXR runtime did not provide xrGetOpenGLESGraphicsRequirementsKHR. "
                   "XR_KHR_opengl_es_enable is required for Modularity's OpenGL ES rendering path.";
        return false;
    }

    // Mandatory per the spec: xrCreateSession returns
    // XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING if this has not been called for
    // the system, even though the values themselves are only advisory here.
    XrGraphicsRequirementsOpenGLESKHR requirements{};
    requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR;
    const XrResult result =
        ext.GetOpenGLESGraphicsRequirementsKHR(instance_, systemId_, &requirements);
    if (XR_FAILED(result)) {
        XRLogResultFailure("xrGetOpenGLESGraphicsRequirementsKHR", static_cast<int32_t>(result));
        outError = "xrGetOpenGLESGraphicsRequirementsKHR failed: " +
                   XRResultString(static_cast<int32_t>(result));
        return false;
    }

    const auto versionText = [](XrVersion v) {
        return std::to_string(XR_VERSION_MAJOR(v)) + "." + std::to_string(XR_VERSION_MINOR(v));
    };
    XRLogInfo("OpenGL ES graphics requirements: " +
              versionText(requirements.minApiVersionSupported) + " to " +
              versionText(requirements.maxApiVersionSupported));
    return true;
#else
    outError = "This build has no OpenXR OpenGL ES support compiled in. Modularity's OpenXR "
               "rendering path targets OpenGL ES; the desktop OpenGL binding is not "
               "implemented yet, and a Vulkan binding is intentionally absent.";
    return false;
#endif
}

bool XRSession::createSession(const XRGraphicsBindingGLES& binding, std::string& outError) {
#if MODULARITY_XR_HAS_GRAPHICS_BINDING
    if (!binding.hasContext()) {
        outError = "Cannot create an OpenXR session without a live EGL display and context. "
                   "The renderer must be up before XR starts.";
        return false;
    }

    // Binds the EGL context the engine already owns. OpenXR does not create a
    // context of its own; it hands back swapchain images that are textures in
    // *this* context, which is what lets the existing renderer draw into them.
    XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding{};
    graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR;
    graphicsBinding.next = nullptr;
    graphicsBinding.display = static_cast<EGLDisplay>(binding.eglDisplay);
    graphicsBinding.config = static_cast<EGLConfig>(binding.eglConfig);
    graphicsBinding.context = static_cast<EGLContext>(binding.eglContext);

    XrSessionCreateInfo createInfo{};
    createInfo.type = XR_TYPE_SESSION_CREATE_INFO;
    createInfo.next = &graphicsBinding;
    createInfo.createFlags = 0;
    createInfo.systemId = systemId_;

    const XrResult result =
        XRLoaderSessionFunctions().CreateSession(instance_, &createInfo, &session_);
    if (XR_FAILED(result) || session_ == XR_NULL_HANDLE) {
        session_ = XR_NULL_HANDLE;
        XRLogResultFailure("xrCreateSession", static_cast<int32_t>(result));
        outError = "xrCreateSession failed: " + XRResultString(static_cast<int32_t>(result));
        return false;
    }

    XRDiagnosticsSnapshot& diag = MutableDiagnostics();
    diag.graphicsApi = "OpenGL ES";
    XRLogInfo("EGL graphics binding initialized");
    return true;
#else
    (void)binding;
    outError = "No OpenXR graphics binding is implemented for this platform. The OpenGL ES "
               "binding requires Android (XrGraphicsBindingOpenGLESAndroidKHR); a desktop "
               "GLES build can compile the XR rendering path but cannot bind a session yet.";
    return false;
#endif
}

bool XRSession::createReferenceSpaces(const ProjectOpenXRSettings& settings,
                                      std::string& outError) {
    const XRSessionFunctions& fns = XRLoaderSessionFunctions();

    // What the runtime actually offers. STAGE is optional in OpenXR, and a
    // headset without a configured guardian genuinely may not have it, so a
    // roomscale project degrades to seated rather than refusing to start.
    std::vector<XrReferenceSpaceType> available;
    uint32_t spaceCount = 0;
    if (XR_SUCCEEDED(fns.EnumerateReferenceSpaces(session_, 0, &spaceCount, nullptr)) &&
        spaceCount > 0) {
        available.resize(spaceCount);
        if (XR_FAILED(fns.EnumerateReferenceSpaces(session_, spaceCount, &spaceCount,
                                                   available.data()))) {
            available.clear();
        }
    }
    const auto offers = [&available](XrReferenceSpaceType type) {
        // An empty list means enumeration failed; assume the space exists and let
        // xrCreateReferenceSpace be the authority rather than pre-failing.
        return available.empty() ||
               std::find(available.begin(), available.end(), type) != available.end();
    };

    XrReferenceSpaceType wanted = (settings.trackingOrigin == XRTrackingOrigin::Floor)
                                      ? XR_REFERENCE_SPACE_TYPE_STAGE
                                      : XR_REFERENCE_SPACE_TYPE_LOCAL;
    effectiveTrackingOrigin_ = settings.trackingOrigin;
    referenceSpaceFellBack_ = false;
    if (wanted == XR_REFERENCE_SPACE_TYPE_STAGE && !offers(XR_REFERENCE_SPACE_TYPE_STAGE)) {
        XRLogWarn("Runtime does not offer a STAGE reference space; Floor tracking origin "
                  "falls back to Eye Level (LOCAL).");
        wanted = XR_REFERENCE_SPACE_TYPE_LOCAL;
        effectiveTrackingOrigin_ = XRTrackingOrigin::Eye;
        referenceSpaceFellBack_ = true;
    }

    XrReferenceSpaceCreateInfo spaceInfo{};
    spaceInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    spaceInfo.referenceSpaceType = wanted;
    // Identity: the XR Origin scene object supplies the world placement, so the
    // reference space itself stays unmodified. Baking an offset in here would
    // fight the XR Origin transform instead of composing with it.
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;

    XrResult result = fns.CreateReferenceSpace(session_, &spaceInfo, &appSpace_);
    if (XR_FAILED(result) || appSpace_ == XR_NULL_HANDLE) {
        appSpace_ = XR_NULL_HANDLE;
        XRLogResultFailure("xrCreateReferenceSpace", static_cast<int32_t>(result));
        outError = "xrCreateReferenceSpace failed for the tracking space: " +
                   XRResultString(static_cast<int32_t>(result));
        return false;
    }

    // VIEW space is mandatory in OpenXR, so a failure here is a real runtime bug
    // rather than a configuration problem.
    XrReferenceSpaceCreateInfo viewInfo{};
    viewInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    viewInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    viewInfo.poseInReferenceSpace.orientation.w = 1.0f;
    result = fns.CreateReferenceSpace(session_, &viewInfo, &viewSpace_);
    if (XR_FAILED(result) || viewSpace_ == XR_NULL_HANDLE) {
        viewSpace_ = XR_NULL_HANDLE;
        XRLogResultFailure("xrCreateReferenceSpace(VIEW)", static_cast<int32_t>(result));
        outError = "xrCreateReferenceSpace failed for the VIEW space: " +
                   XRResultString(static_cast<int32_t>(result));
        return false;
    }

    MutableDiagnostics().activeTrackingOrigin = ToString(effectiveTrackingOrigin_);
    XRLogInfo(std::string("Reference space: ") +
              (wanted == XR_REFERENCE_SPACE_TYPE_STAGE ? "STAGE (Floor)" : "LOCAL (Eye Level)"));
    return true;
}

bool XRSession::beginSession(std::string& outError) {
    if (running_) return true;

    XrSessionBeginInfo beginInfo{};
    beginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
    beginInfo.primaryViewConfigurationType = viewConfigurationType_;

    const XrResult result = XRLoaderSessionFunctions().BeginSession(session_, &beginInfo);
    if (XR_FAILED(result)) {
        XRLogResultFailure("xrBeginSession", static_cast<int32_t>(result));
        outError = "xrBeginSession failed: " + XRResultString(static_cast<int32_t>(result));
        return false;
    }
    running_ = true;
    XRLogInfo("Session begun");
    return true;
}

void XRSession::endSession() {
    if (!running_ || session_ == XR_NULL_HANDLE) return;
    const XrResult result = XRLoaderSessionFunctions().EndSession(session_);
    if (XR_FAILED(result)) {
        // Logged, not propagated: this runs during teardown, where the only useful
        // response is to carry on destroying things.
        XRLogResultFailure("xrEndSession", static_cast<int32_t>(result));
    }
    running_ = false;
    XRLogInfo("Session ended");
}

void XRSession::handleStateChange(const XrEventDataSessionStateChanged& event) {
    // Events can arrive for a session that has already been destroyed (the runtime
    // queues them). Anything not addressed to the live session is not ours.
    if (event.session != XR_NULL_HANDLE && event.session != session_) return;

    const XRSessionStatus previous = status_;
    status_ = fromXrState(event.state);
    if (previous != status_) {
        XRLogInfo(std::string("Session state: ") + ToString(previous) + " -> " + ToString(status_));
    }

    switch (event.state) {
        case XR_SESSION_STATE_READY: {
            std::string error;
            if (!beginSession(error)) {
                XRLogError(error);
                MutableDiagnostics().lastError = error;
                // Could not start frames; treat it as a loss so the caller tears
                // down rather than spinning on a session that will never run.
                status_ = XRSessionStatus::LossPending;
            }
            break;
        }
        case XR_SESSION_STATE_STOPPING:
            // The runtime wants frames to stop (headset off, app backgrounded).
            // xrEndSession is mandatory here; skipping it is the classic "app hangs
            // when you take the headset off" bug.
            endSession();
            break;
        case XR_SESSION_STATE_EXITING:
        case XR_SESSION_STATE_LOSS_PENDING:
            running_ = false;
            break;
        default:
            break;
    }
}

bool XRSession::pollEvents() {
    if (!created_ || instance_ == XR_NULL_HANDLE) return false;

    const XRInstanceFunctions& fns = XRLoaderInstanceFunctions();
    bool keepRunning = true;

    for (;;) {
        // Re-zeroed every iteration: xrPollEvent only writes the union member the
        // event actually uses, and reading a stale one from the previous event is
        // a genuinely nasty class of bug.
        XrEventDataBuffer event{};
        event.type = XR_TYPE_EVENT_DATA_BUFFER;

        const XrResult result = fns.PollEvent(instance_, &event);
        if (result == XR_EVENT_UNAVAILABLE) break;
        if (XR_FAILED(result)) {
            XRLogResultFailure("xrPollEvent", static_cast<int32_t>(result));
            return false;
        }

        switch (event.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
                handleStateChange(
                    *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event));
                break;

            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                // The whole runtime is going away (update, crash, restart). Nothing
                // survives this; the engine drops back to its non-XR path.
                XRLogWarn("Runtime reported instance loss pending; shutting XR down.");
                status_ = XRSessionStatus::LossPending;
                keepRunning = false;
                break;

            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                // Controllers connected, disconnected or changed type. The input
                // layer re-reads its bindings on the next sync rather than polling
                // xrGetCurrentInteractionProfile every frame.
                interactionProfileChanged_ = true;
                XRLogInfo("Interaction profile changed");
                break;

            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                // The user recentred or the guardian moved. Poses stay valid; this
                // is informational until a recentre-aware feature needs it.
                XRLogInfo("Reference space change pending (recentre)");
                break;

            case XR_TYPE_EVENT_DATA_EVENTS_LOST: {
                const auto& lost = *reinterpret_cast<const XrEventDataEventsLost*>(&event);
                XRLogWarn("Runtime dropped " + std::to_string(lost.lostEventCount) +
                          " OpenXR event(s); the app was not draining them fast enough.");
                break;
            }

            default:
                break;
        }
    }

    if (status_ == XRSessionStatus::Exiting || status_ == XRSessionStatus::LossPending) {
        keepRunning = false;
    }
    return keepRunning;
}

void XRSession::requestExit() {
    if (exitRequested_ || !created_ || session_ == XR_NULL_HANDLE) return;
    exitRequested_ = true;
    const XrResult result = XRLoaderSessionFunctions().RequestExitSession(session_);
    if (XR_FAILED(result)) {
        // XR_ERROR_SESSION_NOT_RUNNING just means the session already stopped, so
        // the exit the caller wanted has effectively happened.
        XRLogResultFailure("xrRequestExitSession", static_cast<int32_t>(result));
    }
}

bool XRSession::consumeInteractionProfileChanged() {
    const bool changed = interactionProfileChanged_;
    interactionProfileChanged_ = false;
    return changed;
}

void XRSession::destroy() {
    if (session_ != XR_NULL_HANDLE) {
        endSession();
        const XRSessionFunctions& fns = XRLoaderSessionFunctions();
        // Spaces must go before the session that owns them.
        if (appSpace_ != XR_NULL_HANDLE && fns.DestroySpace) fns.DestroySpace(appSpace_);
        if (viewSpace_ != XR_NULL_HANDLE && fns.DestroySpace) fns.DestroySpace(viewSpace_);
        if (fns.DestroySession) fns.DestroySession(session_);
        XRLogInfo("Session destroyed");
    }
    appSpace_ = XR_NULL_HANDLE;
    viewSpace_ = XR_NULL_HANDLE;
    session_ = XR_NULL_HANDLE;
    instance_ = XR_NULL_HANDLE;
    systemId_ = XR_NULL_SYSTEM_ID;
    interactionProfileChanged_ = false;

    created_ = false;
    running_ = false;
    exitRequested_ = false;
    referenceSpaceFellBack_ = false;
    status_ = XRSessionStatus::None;
    MutableDiagnostics().sessionCreated = false;
}

#endif // MODULARITY_HAS_OPENXR

} // namespace Modularity::XR
