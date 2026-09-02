#pragma once

// The OpenXR action system, and the per-frame input snapshot everything else
// reads.
//
// Section 14's rule is that nothing above this layer touches raw OpenXR: game
// scripts, XR components and the interaction system all read XRInputState, which
// is plain data with no OpenXR types in it. This class is the only place that
// knows XrAction, XrPath or XrSpace exist.
//
// Design notes worth stating, because they are what keeps this cheap enough for
// a Quest 2 frame budget (section 42):
//
//   * Actions are created once at session setup and never per frame. XrAction
//     and XrPath handles are cached in members; nothing here allocates or parses
//     a string during syncFrame().
//   * One action per concept, using OpenXR subaction paths for left/right,
//     rather than two parallel sets of actions.
//   * Trigger and grip are float actions. Their boolean forms are derived here
//     with hysteresis rather than bound as separate boolean actions, so the
//     press point is identical no matter which interaction profile is active and
//     does not depend on a runtime's own conversion threshold.
//   * Binding tables are per-profile data (XRInputBindings), so adding Valve
//     Index or a Vive wand is a new table, not a change to this file.

#include "../../include/XR/XRInputTypes.h"
#include "XRMath.h"
#include "XRSession.h"
#include "XRSettings.h"

#include <array>
#include <string>
#include <vector>

#if MODULARITY_HAS_OPENXR
#include "XRPlatform.h"
#endif

namespace Modularity::XR {

// Everything one controller reported this frame. Pure data: copied out of
// OpenXR once per frame and then read many times by scripts and components,
// which is far cheaper than each reader calling xrGetActionState itself.
struct XRControllerState {
    // True when the runtime has this controller bound to an interaction profile.
    // A controller that is switched off or out of battery reports false here and
    // leaves every other field at its last value.
    bool active = false;

    XRPose gripPose;
    XRPose aimPose;

    // Linear and angular velocity of the grip pose, in tracking space. Zero when
    // the runtime does not report velocities.
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 angularVelocity = glm::vec3(0.0f);
    bool hasVelocity = false;

    std::array<float, static_cast<size_t>(XRAxisKind::Count)> axes{};
    std::array<glm::vec2, static_cast<size_t>(XRAxis2DKind::Count)> axes2D{};

    // Current and previous frame, so Down/Up edges are a comparison rather than
    // per-caller bookkeeping.
    std::array<bool, static_cast<size_t>(XRButton::Count)> buttons{};
    std::array<bool, static_cast<size_t>(XRButton::Count)> previousButtons{};

    // Which of this controller's controls the active interaction profile
    // actually binds. Quest Touch has no Menu button on the right hand, and
    // reporting "not bound" is more useful than reporting a button that is
    // always false.
    std::array<bool, static_cast<size_t>(XRButton::Count)> buttonBound{};

    std::string interactionProfile;

    XRTrackingState trackingState() const {
        if (!gripPose.isValid()) return XRTrackingState::NotTracked;
        return gripPose.isTracked() ? XRTrackingState::Tracked : XRTrackingState::Extrapolated;
    }

    const XRPose& pose(XRPoseKind kind) const {
        return (kind == XRPoseKind::Aim) ? aimPose : gripPose;
    }
};

// The whole XR input snapshot for a frame. Head is a pose only; it has no
// buttons of its own.
struct XRInputState {
    bool sessionActive = false;  // a session exists and is running
    bool hasFocus = false;       // FOCUSED, so actions are actually delivered

    XRPose headPose;
    XRControllerState controllers[2];  // indexed by XRDevice::Left/Right minus 1

    const XRControllerState& controller(XRDevice device) const {
        const int index = (device == XRDevice::Right) ? 1 : 0;
        return controllers[index];
    }
    XRControllerState& controller(XRDevice device) {
        const int index = (device == XRDevice::Right) ? 1 : 0;
        return controllers[index];
    }
};

// Process-wide snapshot, written by XRInput::syncFrame and read by ModuInput.XR,
// the XR components and the interaction system. Single-threaded by contract:
// only the render thread touches it, the same thread that owns the session.
//
// It exists even when OpenXR is compiled out, reporting an inactive session, so
// every reader has exactly one code path instead of an #if at each call site.
XRInputState& MutableInputState();
const XRInputState& InputState();
void ResetInputState();

// A haptic pulse a script or component asked for. Queued rather than applied
// immediately because scripts run outside the XR frame boundary, and applying
// haptics is only legal while the session is running.
struct XRHapticRequest {
    XRDevice device = XRDevice::Right;
    float amplitude = 0.5f;   // 0..1
    float duration = 0.08f;   // seconds
    float frequency = 0.0f;   // Hz, 0 = let the runtime choose
};

// Queues a pulse for the next sync. Safe to call with no session (drops it), so
// scripts never need to check whether XR is running before asking for feedback.
void RequestHapticPulse(const XRHapticRequest& request);

#if MODULARITY_HAS_OPENXR

class XRInput {
public:
    XRInput() = default;
    ~XRInput();

    XRInput(const XRInput&) = delete;
    XRInput& operator=(const XRInput&) = delete;

    // Creates the action set, actions, pose spaces and suggested bindings, then
    // attaches the set to the session. Must run after the session exists and
    // before the first frame: OpenXR forbids attaching action sets to a running
    // session more than once.
    bool initialize(const XRSession& session, const ProjectOpenXRSettings& settings,
                    std::string& outError);

    void shutdown();

    bool isInitialized() const { return initialized_; }

    // xrSyncActions plus reading every action into XRInputState. Call once per
    // frame after xrWaitFrame, with the frame's predicted display time so poses
    // are located for the moment they will actually be shown.
    void syncFrame(const XRSession& session, XrTime predictedDisplayTime);

    // Re-reads which interaction profile each hand is bound to. Called when the
    // session reports an interaction-profile-changed event rather than polled.
    void refreshInteractionProfiles(const XRSession& session);

private:
    bool createActions(std::string& outError);
    bool suggestBindings(const ProjectOpenXRSettings& settings, std::string& outError);
    bool createActionSpaces(const XRSession& session, std::string& outError);
    void readController(const XRSession& session, XRDevice device, XrTime predictedDisplayTime);
    void applyPendingHaptics();

    XrPath stringToPath(const char* path) const;

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSession session_ = XR_NULL_HANDLE;
    XrActionSet actionSet_ = XR_NULL_HANDLE;

    // One action per concept; left/right are selected with subaction paths.
    XrAction gripPoseAction_ = XR_NULL_HANDLE;
    XrAction aimPoseAction_ = XR_NULL_HANDLE;
    XrAction triggerValueAction_ = XR_NULL_HANDLE;
    XrAction gripValueAction_ = XR_NULL_HANDLE;
    XrAction thumbstickAction_ = XR_NULL_HANDLE;
    XrAction thumbstickClickAction_ = XR_NULL_HANDLE;
    XrAction primaryButtonAction_ = XR_NULL_HANDLE;
    XrAction secondaryButtonAction_ = XR_NULL_HANDLE;
    XrAction menuButtonAction_ = XR_NULL_HANDLE;
    XrAction hapticAction_ = XR_NULL_HANDLE;

    // Per-hand subaction paths and the spaces the pose actions resolve to.
    std::array<XrPath, 2> handPaths_{ XR_NULL_PATH, XR_NULL_PATH };
    std::array<XrSpace, 2> gripSpaces_{ XR_NULL_HANDLE, XR_NULL_HANDLE };
    std::array<XrSpace, 2> aimSpaces_{ XR_NULL_HANDLE, XR_NULL_HANDLE };

    bool initialized_ = false;
};

#endif // MODULARITY_HAS_OPENXR

} // namespace Modularity::XR
