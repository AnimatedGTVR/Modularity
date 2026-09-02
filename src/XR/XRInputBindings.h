#pragma once

// Per-interaction-profile binding tables.
//
// This is the file you add to when Modularity should support another controller.
// Everything above it - XRInput, ModuInput.XR, the XR components, game scripts -
// is written against the abstract slots below and does not change (section 14).
//
// A binding table is pure data: for each action slot, the OpenXR input path on
// the left and right controller, or null where that profile has no such control.
// Nulls are meaningful and get reported through XRControllerState::buttonBound,
// so a script can tell "this controller has no Menu button" apart from "the Menu
// button is not pressed".

#include "XRSettings.h"

#include <vector>

namespace Modularity::XR {

// The actions Modularity creates. One per concept; left/right come from OpenXR
// subaction paths rather than from separate actions.
enum class XRActionSlot {
    GripPose = 0,
    AimPose,
    TriggerValue,
    GripValue,
    Thumbstick,
    ThumbstickClick,
    PrimaryButton,
    SecondaryButton,
    MenuButton,
    Haptic,
    Count
};

struct XRBindingEntry {
    XRActionSlot slot;
    // Full OpenXR paths. Null means this profile does not expose that control on
    // that hand - which is normal, not an error.
    const char* leftPath;
    const char* rightPath;
};

struct XRProfileBindings {
    XRInteractionProfile profile;
    const char* profilePath;
    const XRBindingEntry* entries;
    size_t entryCount;
};

// Returns the binding table for a profile, or nullptr when that profile has no
// table yet. A nullptr here is exactly what makes
// IsInteractionProfileImplemented(profile) false, so the two can never disagree.
const XRProfileBindings* GetProfileBindings(XRInteractionProfile profile);

} // namespace Modularity::XR
