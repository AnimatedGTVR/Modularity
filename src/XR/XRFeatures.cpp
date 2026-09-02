#include "XRFeatures.h"

#include <algorithm>

namespace Modularity::XR {

namespace {

// One row per XRFeature, in enum order.
//
// `implemented` reflects what actually exists in src/XR/ today. When a feature
// gains an implementation, flip its flag here and the settings UI stops greying
// it out; nothing else changes. Deliberately conservative: a false here is much
// cheaper than a toggle that silently does nothing on a headset.
const std::vector<XRFeatureInfo>& featureTable() {
    static const std::vector<XRFeatureInfo> table = {
        { XRFeature::OpenGLESGraphics,        "XR_KHR_opengl_es_enable",
          "OpenGL ES Graphics Binding",       true,  true,  false },
        { XRFeature::OpenGLGraphics,          "XR_KHR_opengl_enable",
          "OpenGL Graphics Binding",          false, false, false },
        { XRFeature::CompositionLayerDepth,   "XR_KHR_composition_layer_depth",
          "Depth Submission",                 true,  false, false },
        { XRFeature::AndroidLoaderInit,       "XR_KHR_loader_init_android",
          "Android Loader Init",              true,  true,  false },
        { XRFeature::AndroidCreateInstance,   "XR_KHR_android_create_instance",
          "Android Instance Create Info",     true,  true,  false },

        { XRFeature::QuestDisplayRefreshRate, "XR_FB_display_refresh_rate",
          "Display Refresh Rate",             true,  false, true  },
        { XRFeature::QuestHandTracking,       "XR_EXT_hand_tracking",
          "Hand Tracking",                    false, false, true  },
        { XRFeature::QuestPassthrough,        "XR_FB_passthrough",
          "Passthrough",                      false, false, true  },
        { XRFeature::QuestFoveatedRendering,  "XR_FB_foveation",
          "Foveated Rendering",               false, false, true  },
        { XRFeature::QuestDisplayUtilities,   "XR_META_performance_metrics",
          "Display Utilities",                false, false, true  },
        { XRFeature::QuestPerformanceSettings,"XR_EXT_performance_settings",
          "Performance Settings",             false, false, true  },
    };
    return table;
}

bool contains(const std::vector<std::string>& list, const char* name) {
    return std::find(list.begin(), list.end(), std::string(name)) != list.end();
}

} // namespace

const std::vector<XRFeatureInfo>& AllFeatures() { return featureTable(); }

const XRFeatureInfo& GetFeatureInfo(XRFeature feature) {
    const std::vector<XRFeatureInfo>& table = featureTable();
    const size_t index = static_cast<size_t>(feature);
    if (index >= table.size()) return table[0];
    return table[index];
}

const char* FeatureExtensionName(XRFeature feature) { return GetFeatureInfo(feature).extensionName; }
const char* FeatureDisplayName(XRFeature feature) { return GetFeatureInfo(feature).displayName; }
bool IsFeatureImplemented(XRFeature feature) { return GetFeatureInfo(feature).implemented; }

bool IsFeatureRequested(const ProjectOpenXRSettings& settings, XRFeature feature) {
    switch (feature) {
        // Graphics binding and the Android platform extensions are not user
        // toggles: they follow from the target and the chosen backend.
        case XRFeature::OpenGLESGraphics:
            return settings.graphicsBackend == XRGraphicsBackend::OpenGLES;
        case XRFeature::OpenGLGraphics:
            return false; // desktop GL binding is not implemented yet
        case XRFeature::AndroidLoaderInit:
        case XRFeature::AndroidCreateInstance:
#if defined(__ANDROID__)
            return true;
#else
            return false;
#endif
        case XRFeature::CompositionLayerDepth:
            return settings.depthSubmission == XRDepthSubmission::Depth;

        // Meta Quest group: the group toggle gates every member.
        case XRFeature::QuestDisplayRefreshRate:
            return settings.quest.enabled && settings.quest.displayRefreshRate;
        case XRFeature::QuestHandTracking:
            return settings.quest.enabled && settings.quest.handTracking;
        case XRFeature::QuestPassthrough:
            return settings.quest.enabled && settings.quest.passthrough;
        case XRFeature::QuestFoveatedRendering:
            return settings.quest.enabled && settings.quest.foveatedRendering;
        case XRFeature::QuestDisplayUtilities:
            return settings.quest.enabled && settings.quest.displayUtilities;
        case XRFeature::QuestPerformanceSettings:
            return settings.quest.enabled && settings.quest.performanceSettings;

        case XRFeature::Count:
        default:
            return false;
    }
}

std::vector<const char*> BuildRequestedExtensions(const ProjectOpenXRSettings& settings,
                                                  const std::vector<std::string>& available,
                                                  std::vector<XRSkippedFeature>& outSkipped) {
    outSkipped.clear();
    std::vector<const char*> requested;
    requested.reserve(featureTable().size());

    for (const XRFeatureInfo& info : featureTable()) {
        if (!IsFeatureRequested(settings, info.feature)) continue;

        if (!info.implemented) {
            outSkipped.push_back({ info.feature,
                                   std::string(info.displayName) +
                                       " is not implemented in this build yet" });
            continue;
        }
        if (!contains(available, info.extensionName)) {
            outSkipped.push_back({ info.feature,
                                   std::string(info.extensionName) +
                                       " is not offered by this OpenXR runtime" });
            continue;
        }
        requested.push_back(info.extensionName);
    }
    return requested;
}

} // namespace Modularity::XR
