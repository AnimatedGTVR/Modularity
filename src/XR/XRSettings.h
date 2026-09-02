#pragma once

// Project-level OpenXR configuration.
//
// Deliberately free of any OpenXR header: this is included by ProjectManager.h
// and the Project Settings UI, which must keep compiling in builds where
// MODULARITY_HAS_OPENXR is 0. The types here describe *intent* (what the project
// asked for); what the runtime actually managed to provide is reported separately
// by XRDiagnostics.
//
// Everything in here is serialized into project.modu by Project::saveProjectFile
// under "openXR*" keys. Older projects have none of them and load with these
// defaults, which is why every default is "off".

#include <string>
#include <vector>

namespace Modularity::XR {

// Graphics binding used for the XR session. OpenGL ES is the only implemented
// binding; Vulkan is intentionally absent rather than present-but-broken.
enum class XRGraphicsBackend {
    OpenGLES = 0,
};

// How the two eye views are produced.
//   SinglePassMultiview - one scene pass into a 2-layer array texture using
//                         GL_OVR_multiview2. Falls back to MultiPass at runtime
//                         when the extension is missing.
//   MultiPass           - one scene pass per eye. Always available.
enum class XRRenderMode {
    SinglePassMultiview = 0,
    MultiPass = 1,
};

// Whether Modularity submits a depth layer alongside the projection layer, which
// lets the runtime do better reprojection. Requires XR_KHR_composition_layer_depth.
enum class XRDepthSubmission {
    None = 0,
    Depth = 1,
};

// Which OpenXR reference space the XR Origin maps onto.
//   Floor - XR_REFERENCE_SPACE_TYPE_STAGE (roomscale, origin on the floor)
//   Eye   - XR_REFERENCE_SPACE_TYPE_LOCAL (seated, origin at the recentre pose)
enum class XRTrackingOrigin {
    Floor = 0,
    Eye = 1,
};

// OpenXR interaction profiles the project wants bindings suggested for. Only
// MetaQuestTouch is implemented; the rest exist so the enum, the settings UI and
// the binding table can grow without a format change. Anything not implemented is
// shown disabled in the UI rather than silently accepted.
enum class XRInteractionProfile {
    MetaQuestTouch = 0,
    ValveIndex = 1,
    HtcVive = 2,
    MicrosoftMotionController = 3,
    KhronosSimpleController = 4,
    Count
};

const char* ToString(XRGraphicsBackend backend);
const char* ToString(XRRenderMode mode);
const char* ToString(XRDepthSubmission depth);
const char* ToString(XRTrackingOrigin origin);

// Stable serialization ids. Never reuse or renumber these; project files store them.
const char* SerializationId(XRInteractionProfile profile);
// Human-readable name for the settings UI.
const char* DisplayName(XRInteractionProfile profile);
// The OpenXR interaction profile path, e.g. "/interaction_profiles/oculus/touch_controller".
const char* InteractionProfilePath(XRInteractionProfile profile);
// False for profiles that are declared but have no binding table yet, so the UI
// can grey them out instead of pretending they work.
bool IsInteractionProfileImplemented(XRInteractionProfile profile);

// Quest Touch only. A function rather than a constant so the default can depend
// on build configuration later without touching call sites.
std::vector<XRInteractionProfile> DefaultInteractionProfiles();

XRRenderMode XRRenderModeFromString(const std::string& value);
XRDepthSubmission XRDepthSubmissionFromString(const std::string& value);
XRTrackingOrigin XRTrackingOriginFromString(const std::string& value);
bool XRInteractionProfileFromString(const std::string& value, XRInteractionProfile& out);

// Meta Quest feature group. Mirrors Unity's OpenXR feature-group idea: base Quest
// support must not drag in every Meta extension, so each one is an independent
// opt-in that maps to a specific OpenXR extension request.
//
// `enabled` here means "ask for it"; whether the runtime actually granted it is
// reported by XRDiagnostics. Features whose implementation does not exist yet are
// marked unimplemented in XRFeatureRegistry and cannot be turned on.
struct XRQuestFeatureSettings {
    bool enabled = true;            // Meta Quest Support (the group itself)
    bool handTracking = false;      // XR_EXT_hand_tracking          - not implemented yet
    bool passthrough = false;       // XR_FB_passthrough             - not implemented yet
    bool displayRefreshRate = false;// XR_FB_display_refresh_rate
    bool displayUtilities = false;  // XR_META_performance_metrics   - not implemented yet
    bool foveatedRendering = false; // XR_FB_foveation               - not implemented yet
    bool performanceSettings = false; // XR_EXT_performance_settings - not implemented yet
};

// XR Interaction system (interactors/interactables/manager). Independent of the
// OpenXR runtime itself: a project can use raw ModuInput.XR without it.
struct XRInteractionSettings {
    bool enabled = true;
    // Name of the action set asset driving the components. Empty = the built-in
    // "Default XR Actions" set.
    std::string defaultActionSet;
};

struct ProjectOpenXRSettings {
    // Master switch. Off means the engine never even tries to load an OpenXR
    // loader, and every Android/desktop path behaves exactly as it did before
    // OpenXR existed.
    bool enabled = false;

    XRGraphicsBackend graphicsBackend = XRGraphicsBackend::OpenGLES;
    XRRenderMode renderMode = XRRenderMode::SinglePassMultiview;
    XRDepthSubmission depthSubmission = XRDepthSubmission::None;
    XRTrackingOrigin trackingOrigin = XRTrackingOrigin::Floor;

    // Defaults to DefaultInteractionProfiles() so a project that predates these
    // settings still gets Quest Touch bindings. Empty means genuinely empty (the
    // user turned every profile off), which is why project.modu writes an
    // explicit openXRInteractionProfileCount: without it an empty list would be
    // indistinguishable from "never configured" and silently spring back to the
    // default on reload.
    std::vector<XRInteractionProfile> interactionProfiles = DefaultInteractionProfiles();

    XRQuestFeatureSettings quest;
    XRInteractionSettings interaction;

    bool hasInteractionProfile(XRInteractionProfile profile) const;
    void setInteractionProfile(XRInteractionProfile profile, bool on);

    // The profiles session setup actually suggests bindings for: the configured
    // list minus anything with no binding table, so we never describe a profile
    // we cannot bind.
    std::vector<XRInteractionProfile> effectiveInteractionProfiles() const;
};

} // namespace Modularity::XR
