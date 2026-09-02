#pragma once

// OpenXR instance + system discovery.
//
// Owns everything that exists before a session: the loader, XrInstance, the
// extension set actually enabled, XrSystemId, and the view configuration. It
// deliberately stops short of session/swapchain/rendering, which need a live GL
// context and belong to the (later) XRSession + XRSwapchainGL layers.
//
// Splitting here has a practical payoff: the editor's Project Settings page can
// bring this much up on demand to populate Runtime Diagnostics, without creating
// a session or touching the renderer.
//
// Failure is always a controlled "unavailable" with a readable reason - never a
// crash, never a half-initialized instance (section 5).

#include "XRDiagnostics.h"
#include "XRFeatures.h"
#include "XRSettings.h"

#include <cstdint>
#include <string>
#include <vector>

#if MODULARITY_HAS_OPENXR
#include "XRPlatform.h"
#endif

namespace Modularity::XR {

// Per-eye resolution and sample count the runtime recommends.
struct XRViewInfo {
    uint32_t recommendedWidth = 0;
    uint32_t recommendedHeight = 0;
    uint32_t maxWidth = 0;
    uint32_t maxHeight = 0;
    uint32_t recommendedSampleCount = 1;
    uint32_t maxSampleCount = 1;
};

class XRRuntime {
public:
    XRRuntime() = default;
    ~XRRuntime();

    XRRuntime(const XRRuntime&) = delete;
    XRRuntime& operator=(const XRRuntime&) = delete;

    // Brings up loader -> instance -> system and fills XRDiagnostics.
    //
    // On Android `applicationVM` is the JavaVM* and `applicationActivity` the
    // activity's jobject; both are required by XR_KHR_loader_init_android and
    // XR_KHR_android_create_instance. Off Android they are ignored and may be null.
    //
    // Returns false with `outError` set on any failure, leaving the object in a
    // clean shut-down state. A false return is a normal outcome (no headset, no
    // runtime installed) and callers should treat it as "run without XR".
    bool initialize(const ProjectOpenXRSettings& settings,
                    void* applicationVM,
                    void* applicationActivity,
                    std::string& outError);

    void shutdown();

    bool isInitialized() const { return initialized_; }

    // True once xrGetSystem has returned a system id, meaning a headset is
    // actually present. An instance can exist without one (headset unplugged).
    bool hasSystem() const { return hasSystem_; }

    const std::string& runtimeName() const { return runtimeName_; }
    const std::string& systemName() const { return systemName_; }
    const std::vector<std::string>& availableExtensions() const { return availableExtensions_; }
    const std::vector<std::string>& enabledExtensions() const { return enabledExtensions_; }

    bool isExtensionEnabled(const char* name) const;
    // Requested + implemented + granted by the runtime.
    bool isFeatureActive(XRFeature feature) const;

    // Per-eye view info from the stereo view configuration. Empty until a system
    // has been acquired.
    const std::vector<XRViewInfo>& views() const { return views_; }
    uint32_t viewCount() const { return static_cast<uint32_t>(views_.size()); }

#if MODULARITY_HAS_OPENXR
    XrInstance instance() const { return instance_; }
    XrSystemId systemId() const { return systemId_; }
    XrViewConfigurationType viewConfigurationType() const { return viewConfigurationType_; }
    const std::vector<XrEnvironmentBlendMode>& blendModes() const { return blendModes_; }
#endif

private:
#if MODULARITY_HAS_OPENXR
    // Android's loader refuses every other call until this one succeeds.
    bool initializeAndroidLoader(void* applicationVM, void* applicationActivity,
                                 std::string& outError);
    bool enumerateExtensions(std::string& outError);
    bool createInstance(const ProjectOpenXRSettings& settings, void* applicationVM,
                        void* applicationActivity, std::string& outError);
    bool queryInstanceProperties(std::string& outError);
    bool acquireSystem(std::string& outError);
    bool queryViewConfiguration(std::string& outError);
    void publishDiagnostics(const ProjectOpenXRSettings& settings);

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId systemId_ = XR_NULL_SYSTEM_ID;
    XrViewConfigurationType viewConfigurationType_ = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    std::vector<XrEnvironmentBlendMode> blendModes_;
#endif

    bool initialized_ = false;
    bool hasSystem_ = false;
    std::string runtimeName_;
    std::string systemName_;
    std::vector<std::string> availableExtensions_;
    std::vector<std::string> enabledExtensions_;
    std::vector<XRViewInfo> views_;
};

} // namespace Modularity::XR
