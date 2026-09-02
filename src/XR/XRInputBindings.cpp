#include "XRInputBindings.h"

namespace Modularity::XR {

namespace {

// Meta Quest Touch (/interaction_profiles/oculus/touch_controller).
//
// Two things about this profile are worth knowing before changing anything here:
//
//   * Trigger and squeeze are analog only - there is no boolean click path. They
//     are bound as float actions and thresholded in XRInput, which is why the
//     press point is identical on every profile rather than whatever a given
//     runtime happens to use.
//   * Only the left controller has a menu button. The right controller's
//     equivalent is reserved by the system for the Oculus/Meta button and is not
//     available to applications, so the right entry is deliberately null rather
//     than bound to something approximate.
constexpr XRBindingEntry kMetaQuestTouchEntries[] = {
    { XRActionSlot::GripPose,        "/user/hand/left/input/grip/pose",
                                     "/user/hand/right/input/grip/pose" },
    { XRActionSlot::AimPose,         "/user/hand/left/input/aim/pose",
                                     "/user/hand/right/input/aim/pose" },
    { XRActionSlot::TriggerValue,    "/user/hand/left/input/trigger/value",
                                     "/user/hand/right/input/trigger/value" },
    { XRActionSlot::GripValue,       "/user/hand/left/input/squeeze/value",
                                     "/user/hand/right/input/squeeze/value" },
    { XRActionSlot::Thumbstick,      "/user/hand/left/input/thumbstick",
                                     "/user/hand/right/input/thumbstick" },
    { XRActionSlot::ThumbstickClick, "/user/hand/left/input/thumbstick/click",
                                     "/user/hand/right/input/thumbstick/click" },
    // X/Y on the left controller, A/B on the right.
    { XRActionSlot::PrimaryButton,   "/user/hand/left/input/x/click",
                                     "/user/hand/right/input/a/click" },
    { XRActionSlot::SecondaryButton, "/user/hand/left/input/y/click",
                                     "/user/hand/right/input/b/click" },
    { XRActionSlot::MenuButton,      "/user/hand/left/input/menu/click",
                                     nullptr },
    { XRActionSlot::Haptic,          "/user/hand/left/output/haptic",
                                     "/user/hand/right/output/haptic" },
};

constexpr XRProfileBindings kProfileTables[] = {
    { XRInteractionProfile::MetaQuestTouch,
      "/interaction_profiles/oculus/touch_controller",
      kMetaQuestTouchEntries,
      sizeof(kMetaQuestTouchEntries) / sizeof(kMetaQuestTouchEntries[0]) },
};

} // namespace

const XRProfileBindings* GetProfileBindings(XRInteractionProfile profile) {
    for (const XRProfileBindings& table : kProfileTables) {
        if (table.profile == profile) return &table;
    }
    return nullptr;
}

} // namespace Modularity::XR
