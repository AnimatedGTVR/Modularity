#pragma once

// Conversions between OpenXR's math types and Modularity's (glm).
//
// Kept in one header because these are the only places a sign or handedness
// convention can be got wrong, and a mistake in any of them looks like "VR is
// subtly wrong" rather than like a crash. Both APIs are right-handed with +Y up
// and -Z forward, so positions and orientations map across directly; the only
// real work is the projection matrix, which OpenXR expresses as four half-angles
// rather than a symmetric field of view.

#include "../Common.h"

#include <cmath>

#if MODULARITY_HAS_OPENXR
#include "XRPlatform.h"
#endif

namespace Modularity::XR {

// A tracked pose in Modularity's coordinate system, plus whether the runtime
// actually vouched for it this frame. `positionValid`/`orientationValid` are
// tracked separately because OpenXR reports them separately: a controller that
// has been set down still reports a valid orientation from its IMU while its
// position goes stale, and treating that as fully valid makes objects drift.
struct XRPose {
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    bool positionValid = false;
    bool orientationValid = false;
    // True when the runtime is actively tracking, as opposed to extrapolating
    // from the last known pose.
    bool positionTracked = false;
    bool orientationTracked = false;

    bool isTracked() const { return positionTracked && orientationTracked; }
    bool isValid() const { return positionValid && orientationValid; }

    glm::mat4 toMatrix() const {
        return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation);
    }
};

// One eye's view for the current frame.
struct XRView {
    XRPose pose;
    // OpenXR field-of-view half-angles in radians, signed: left/down are normally
    // negative. These are asymmetric on every real headset, which is exactly why
    // Modularity's symmetric BuildCameraProjection cannot be reused here.
    float angleLeft = 0.0f;
    float angleRight = 0.0f;
    float angleUp = 0.0f;
    float angleDown = 0.0f;
};

#if MODULARITY_HAS_OPENXR

inline glm::vec3 ToGlm(const XrVector3f& v) { return glm::vec3(v.x, v.y, v.z); }

inline glm::quat ToGlm(const XrQuaternionf& q) {
    // glm::quat's constructor takes (w, x, y, z); XrQuaternionf stores x,y,z,w.
    // Getting this order wrong is the single most common OpenXR integration bug.
    return glm::quat(q.w, q.x, q.y, q.z);
}

inline XrVector3f ToXr(const glm::vec3& v) { return XrVector3f{ v.x, v.y, v.z }; }

inline XrQuaternionf ToXr(const glm::quat& q) { return XrQuaternionf{ q.x, q.y, q.z, q.w }; }

// Fills an XRPose from a located space, honouring the four location flags
// independently so a partially-tracked pose is reported as such.
inline XRPose ToXRPose(const XrPosef& pose, XrSpaceLocationFlags flags) {
    XRPose out;
    out.positionValid = (flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
    out.orientationValid = (flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
    out.positionTracked = (flags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
    out.orientationTracked = (flags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
    if (out.positionValid) out.position = ToGlm(pose.position);
    if (out.orientationValid) out.rotation = ToGlm(pose.orientation);
    return out;
}

inline XRView ToXRView(const XrView& view, XrViewStateFlags stateFlags) {
    XRView out;
    // XrViewState uses the same two "valid" bits as XrSpaceLocation, and a view is
    // never reported as merely extrapolated, so tracked mirrors valid here.
    out.pose.positionValid = (stateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;
    out.pose.orientationValid = (stateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
    out.pose.positionTracked = (stateFlags & XR_VIEW_STATE_POSITION_TRACKED_BIT) != 0;
    out.pose.orientationTracked = (stateFlags & XR_VIEW_STATE_ORIENTATION_TRACKED_BIT) != 0;
    if (out.pose.positionValid) out.pose.position = ToGlm(view.pose.position);
    if (out.pose.orientationValid) out.pose.rotation = ToGlm(view.pose.orientation);
    out.angleLeft = view.fov.angleLeft;
    out.angleRight = view.fov.angleRight;
    out.angleUp = view.fov.angleUp;
    out.angleDown = view.fov.angleDown;
    return out;
}

#endif // MODULARITY_HAS_OPENXR

// Asymmetric perspective projection from OpenXR's four half-angles, in the OpenGL
// convention (right-handed, clip depth -1..1) that Modularity's shaders expect.
//
// This is the one piece BuildCameraProjection cannot express: it builds a
// symmetric frustum from a single vertical FOV, whereas every HMD's per-eye
// frustum is off-centre. Feeding a symmetric matrix to a headset gives a picture
// that looks almost right and makes people motion sick, so the XR path always
// builds its own.
inline glm::mat4 XRProjectionMatrix(const XRView& view, float nearPlane, float farPlane) {
    const float tanLeft = std::tan(view.angleLeft);
    const float tanRight = std::tan(view.angleRight);
    const float tanDown = std::tan(view.angleDown);
    const float tanUp = std::tan(view.angleUp);

    const float tanWidth = tanRight - tanLeft;
    const float tanHeight = tanUp - tanDown;
    // A zero extent would divide by zero; it only happens with a garbage FOV, and
    // an identity matrix renders nothing visible rather than producing NaNs that
    // poison every downstream matrix.
    if (std::abs(tanWidth) < 1e-6f || std::abs(tanHeight) < 1e-6f) {
        return glm::mat4(1.0f);
    }

    const float safeNear = std::max(0.001f, nearPlane);
    const float safeFar = std::max(safeNear + 0.001f, farPlane);

    glm::mat4 proj(0.0f);
    proj[0][0] = 2.0f / tanWidth;
    proj[1][1] = 2.0f / tanHeight;
    proj[2][0] = (tanRight + tanLeft) / tanWidth;
    proj[2][1] = (tanUp + tanDown) / tanHeight;
    proj[2][2] = -(safeFar + safeNear) / (safeFar - safeNear);
    proj[2][3] = -1.0f;
    proj[3][2] = -(2.0f * safeFar * safeNear) / (safeFar - safeNear);
    return proj;
}

// View matrix for an eye, given the XR Origin's world transform.
//
// The eye pose is in tracking space, so the full chain is
// origin-to-world * tracking-pose, inverted. Composing rather than overwriting is
// what lets a game move the XR Origin to move the player without fighting the
// headset's own tracking (section 11).
inline glm::mat4 XRViewMatrix(const XRPose& eyePose, const glm::mat4& originToWorld) {
    return glm::inverse(originToWorld * eyePose.toMatrix());
}

} // namespace Modularity::XR
