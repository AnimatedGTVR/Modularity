#include "XRSettings.h"

#include <algorithm>
#include <cctype>

namespace Modularity::XR {

namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

struct InteractionProfileInfo {
    const char* id;          // serialization id, never change
    const char* displayName;
    const char* path;        // OpenXR interaction profile path
    bool implemented;        // has a binding table in XRInputBindings
};

// Indexed by XRInteractionProfile. Adding a profile means adding a row here and a
// binding table in XRInputBindings; ModuInput.XR itself does not change.
constexpr InteractionProfileInfo kProfiles[] = {
    { "metaQuestTouch", "Meta Quest Touch Controller",
      "/interaction_profiles/oculus/touch_controller", true },
    { "valveIndex", "Valve Index Controller",
      "/interaction_profiles/valve/index_controller", false },
    { "htcVive", "HTC Vive Controller",
      "/interaction_profiles/htc/vive_controller", false },
    { "microsoftMotionController", "Microsoft Motion Controller",
      "/interaction_profiles/microsoft/motion_controller", false },
    { "khronosSimple", "Khronos Simple Controller",
      "/interaction_profiles/khr/simple_controller", false },
};

static_assert(sizeof(kProfiles) / sizeof(kProfiles[0]) ==
                  static_cast<size_t>(XRInteractionProfile::Count),
              "kProfiles must have one row per XRInteractionProfile");

const InteractionProfileInfo& info(XRInteractionProfile profile) {
    const size_t index = static_cast<size_t>(profile);
    if (index >= static_cast<size_t>(XRInteractionProfile::Count)) {
        return kProfiles[0];
    }
    return kProfiles[index];
}

} // namespace

const char* ToString(XRGraphicsBackend backend) {
    switch (backend) {
        case XRGraphicsBackend::OpenGLES:
        default: return "OpenGL ES";
    }
}

const char* ToString(XRRenderMode mode) {
    switch (mode) {
        case XRRenderMode::MultiPass: return "Multi Pass";
        case XRRenderMode::SinglePassMultiview:
        default: return "Single Pass / Multiview";
    }
}

const char* ToString(XRDepthSubmission depth) {
    switch (depth) {
        case XRDepthSubmission::Depth: return "Depth";
        case XRDepthSubmission::None:
        default: return "None";
    }
}

const char* ToString(XRTrackingOrigin origin) {
    switch (origin) {
        case XRTrackingOrigin::Eye: return "Eye Level";
        case XRTrackingOrigin::Floor:
        default: return "Floor";
    }
}

const char* SerializationId(XRInteractionProfile profile) { return info(profile).id; }
const char* DisplayName(XRInteractionProfile profile) { return info(profile).displayName; }
const char* InteractionProfilePath(XRInteractionProfile profile) { return info(profile).path; }
bool IsInteractionProfileImplemented(XRInteractionProfile profile) { return info(profile).implemented; }

XRRenderMode XRRenderModeFromString(const std::string& value) {
    const std::string v = toLower(value);
    if (v == "multipass" || v == "multi pass" || v == "1") return XRRenderMode::MultiPass;
    return XRRenderMode::SinglePassMultiview;
}

XRDepthSubmission XRDepthSubmissionFromString(const std::string& value) {
    const std::string v = toLower(value);
    if (v == "depth" || v == "1") return XRDepthSubmission::Depth;
    return XRDepthSubmission::None;
}

XRTrackingOrigin XRTrackingOriginFromString(const std::string& value) {
    const std::string v = toLower(value);
    if (v == "eye" || v == "eyelevel" || v == "eye level" || v == "local" || v == "1") {
        return XRTrackingOrigin::Eye;
    }
    return XRTrackingOrigin::Floor;
}

bool XRInteractionProfileFromString(const std::string& value, XRInteractionProfile& out) {
    const std::string v = toLower(value);
    for (int i = 0; i < static_cast<int>(XRInteractionProfile::Count); ++i) {
        if (toLower(kProfiles[i].id) == v) {
            out = static_cast<XRInteractionProfile>(i);
            return true;
        }
    }
    return false;
}

bool ProjectOpenXRSettings::hasInteractionProfile(XRInteractionProfile profile) const {
    return std::find(interactionProfiles.begin(), interactionProfiles.end(), profile) !=
           interactionProfiles.end();
}

void ProjectOpenXRSettings::setInteractionProfile(XRInteractionProfile profile, bool on) {
    const auto it = std::find(interactionProfiles.begin(), interactionProfiles.end(), profile);
    if (on && it == interactionProfiles.end()) {
        interactionProfiles.push_back(profile);
    } else if (!on && it != interactionProfiles.end()) {
        interactionProfiles.erase(it);
    }
}

std::vector<XRInteractionProfile> ProjectOpenXRSettings::effectiveInteractionProfiles() const {
    // Drop anything with no binding table so session setup never suggests bindings
    // for a profile it cannot describe. An empty result is returned as-is: the
    // user really did turn everything off, and quietly substituting a default here
    // would make the settings UI lie about what is active.
    std::vector<XRInteractionProfile> result;
    result.reserve(interactionProfiles.size());
    for (XRInteractionProfile profile : interactionProfiles) {
        if (IsInteractionProfileImplemented(profile)) result.push_back(profile);
    }
    return result;
}

std::vector<XRInteractionProfile> DefaultInteractionProfiles() {
    return { XRInteractionProfile::MetaQuestTouch };
}

} // namespace Modularity::XR
