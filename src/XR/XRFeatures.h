#pragma once

// OpenXR extension / feature-group registry.
//
// Section 35's requirement is that the core runtime must not fill up with
// `if (quest) ... if (handTracking) ...`. So every OpenXR extension Modularity
// can ask for is one row in a table here, and the runtime only ever asks the
// registry two things: "what extensions should I request?" and "did I get this
// one?". Adding a feature is a row plus its implementation - no core edits.
//
// The `implemented` column is load-bearing. An extension the runtime advertises
// but Modularity has no code for is reported as unavailable and is never
// requested, because section 34 asks for no faked feature support. That is why
// the settings UI can grey those toggles out honestly rather than accepting a
// setting that does nothing.

#include "XRSettings.h"

#include <string>
#include <vector>

namespace Modularity::XR {

enum class XRFeature {
    // --- core graphics / platform -----------------------------------------
    OpenGLESGraphics,        // XR_KHR_opengl_es_enable        (required on Android)
    OpenGLGraphics,          // XR_KHR_opengl_enable           (desktop, future)
    CompositionLayerDepth,   // XR_KHR_composition_layer_depth (Depth Submission)
    AndroidLoaderInit,       // XR_KHR_loader_init_android
    AndroidCreateInstance,   // XR_KHR_android_create_instance

    // --- Meta Quest feature group -----------------------------------------
    QuestDisplayRefreshRate, // XR_FB_display_refresh_rate
    QuestHandTracking,       // XR_EXT_hand_tracking
    QuestPassthrough,        // XR_FB_passthrough
    QuestFoveatedRendering,  // XR_FB_foveation
    QuestDisplayUtilities,   // XR_META_performance_metrics
    QuestPerformanceSettings,// XR_EXT_performance_settings

    Count
};

struct XRFeatureInfo {
    XRFeature feature;
    const char* extensionName;
    const char* displayName;
    // False = declared so the UI and roadmap can show it, but there is no
    // implementation behind it yet. Never requested at xrCreateInstance.
    bool implemented;
    // True = the session cannot come up without it on this platform.
    bool requiredOnAndroid;
    // True = belongs to the Meta Quest feature group in the settings UI.
    bool questGroup;
};

const XRFeatureInfo& GetFeatureInfo(XRFeature feature);
const char* FeatureExtensionName(XRFeature feature);
const char* FeatureDisplayName(XRFeature feature);
bool IsFeatureImplemented(XRFeature feature);

// Every feature, in declaration order. For building the settings UI.
const std::vector<XRFeatureInfo>& AllFeatures();

// Does this project's configuration ask for the feature at all? Purely a read of
// ProjectOpenXRSettings; says nothing about runtime availability.
bool IsFeatureRequested(const ProjectOpenXRSettings& settings, XRFeature feature);

// The extension name list to hand to xrCreateInstance: features that are
// requested by the project AND implemented by Modularity AND advertised by the
// runtime. `available` is the runtime's advertised extension list.
//
// Anything requested but missing lands in `outSkipped` with a reason, so the
// caller can log exactly why (say) depth submission fell back to None instead of
// silently disabling it.
struct XRSkippedFeature {
    XRFeature feature;
    std::string reason;
};

std::vector<const char*> BuildRequestedExtensions(const ProjectOpenXRSettings& settings,
                                                  const std::vector<std::string>& available,
                                                  std::vector<XRSkippedFeature>& outSkipped);

} // namespace Modularity::XR
