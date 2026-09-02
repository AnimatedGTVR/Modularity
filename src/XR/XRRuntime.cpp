#include "XRRuntime.h"

#include "XRLoader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Modularity::XR {

XRRuntime::~XRRuntime() { shutdown(); }

bool XRRuntime::isExtensionEnabled(const char* name) const {
    if (!name) return false;
    return std::find(enabledExtensions_.begin(), enabledExtensions_.end(), std::string(name)) !=
           enabledExtensions_.end();
}

bool XRRuntime::isFeatureActive(XRFeature feature) const {
    if (!IsFeatureImplemented(feature)) return false;
    return isExtensionEnabled(FeatureExtensionName(feature));
}

#if !MODULARITY_HAS_OPENXR

// OpenXR compiled out. Everything reports unavailable; no code path in the engine
// needs a second spelling for "no XR here".
bool XRRuntime::initialize(const ProjectOpenXRSettings&, void*, void*, std::string& outError) {
    outError = "This build of Modularity was compiled without OpenXR support "
               "(MODULARITY_ENABLE_OPENXR=OFF).";
    ResetDiagnostics();
    MutableDiagnostics().lastError = outError;
    return false;
}

void XRRuntime::shutdown() {
    initialized_ = false;
    hasSystem_ = false;
}

#else // MODULARITY_HAS_OPENXR

namespace {

// Copies a fixed-size OpenXR char buffer into a std::string without trusting it
// to be terminated.
std::string fromXrBuffer(const char* buffer, size_t capacity) {
    if (!buffer) return {};
    const size_t length = ::strnlen(buffer, capacity);
    return std::string(buffer, length);
}

std::string versionString(XrVersion version) {
    return std::to_string(XR_VERSION_MAJOR(version)) + "." +
           std::to_string(XR_VERSION_MINOR(version)) + "." +
           std::to_string(XR_VERSION_PATCH(version));
}

const char* blendModeName(XrEnvironmentBlendMode mode) {
    switch (mode) {
        case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:      return "Opaque";
        case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:    return "Additive";
        case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: return "Alpha Blend";
        default:                                    return "Unknown";
    }
}

} // namespace

bool XRRuntime::initialize(const ProjectOpenXRSettings& settings,
                           void* applicationVM,
                           void* applicationActivity,
                           std::string& outError) {
    outError.clear();
    shutdown();
    ResetDiagnostics();

    if (!settings.enabled) {
        outError = "OpenXR is disabled in Project Settings.";
        return false;
    }

    std::string loaderError;
    if (!XRLoaderInit(loaderError)) {
        outError = loaderError;
        XRLogWarn(outError);
        MutableDiagnostics().lastError = outError;
        return false;
    }
    MutableDiagnostics().loaderAvailable = true;
    MutableDiagnostics().loaderPath = XRLoaderPath();

    // Android's loader must be handed the JavaVM + Context before anything else,
    // including extension enumeration. Getting this wrong shows up as a confusing
    // XR_ERROR_INITIALIZATION_FAILED much later, so it goes first.
    if (!initializeAndroidLoader(applicationVM, applicationActivity, outError) ||
        !enumerateExtensions(outError) ||
        !createInstance(settings, applicationVM, applicationActivity, outError) ||
        !queryInstanceProperties(outError)) {
        shutdown();
        MutableDiagnostics().lastError = outError;
        return false;
    }

    initialized_ = true;

    // A missing system is NOT a failure: the runtime is up, the headset just is
    // not ready (asleep, unplugged, proximity sensor open). Callers retry later.
    std::string systemError;
    if (acquireSystem(systemError)) {
        std::string viewError;
        if (!queryViewConfiguration(viewError)) {
            XRLogWarn(viewError);
        }
    } else {
        XRLogWarn(systemError);
    }

    publishDiagnostics(settings);
    return true;
}

bool XRRuntime::initializeAndroidLoader(void* applicationVM, void* applicationActivity,
                                        std::string& outError) {
// Keyed on the extension macro, not __ANDROID__. XR_KHR_loader_init_android is
// defined by openxr_platform.h exactly when XR_USE_PLATFORM_ANDROID is on, which
// is the real precondition for this code: it needs XrLoaderInitInfoAndroidKHR to
// exist. Using __ANDROID__ instead meant this whole block silently compiled away
// on any host without the NDK, so the most important part of Quest bring-up was
// the one part no build ever type-checked.
#if defined(XR_KHR_loader_init_android)
    const XRGlobalFunctions& globals = XRLoaderGlobals();
    if (!globals.InitializeLoaderKHR) {
        outError = "OpenXR loader does not export xrInitializeLoaderKHR, which is required "
                   "on Android. The packaged libopenxr_loader.so is too old or not an "
                   "Android loader.";
        return false;
    }
    if (!applicationVM || !applicationActivity) {
        outError = "Android OpenXR loader init needs the JavaVM and activity Context, "
                   "but one of them was null.";
        return false;
    }

    XrLoaderInitInfoAndroidKHR initInfo{};
    initInfo.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
    initInfo.next = nullptr;
    initInfo.applicationVM = applicationVM;
    initInfo.applicationContext = applicationActivity;

    const XrResult result = globals.InitializeLoaderKHR(
        reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&initInfo));
    if (XR_FAILED(result)) {
        XRLogResultFailure("xrInitializeLoaderKHR", static_cast<int32_t>(result));
        outError = "xrInitializeLoaderKHR failed: " + XRResultString(static_cast<int32_t>(result));
        return false;
    }
    XRLogInfo("Android loader initialized");
    return true;
#else
    (void)applicationVM;
    (void)applicationActivity;
    (void)outError;
    return true; // nothing to do off Android
#endif
}

bool XRRuntime::enumerateExtensions(std::string& outError) {
    const XRGlobalFunctions& globals = XRLoaderGlobals();
    uint32_t count = 0;
    XrResult result = globals.EnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
    if (XR_FAILED(result)) {
        XRLogResultFailure("xrEnumerateInstanceExtensionProperties", static_cast<int32_t>(result));
        outError = "xrEnumerateInstanceExtensionProperties failed: " +
                   XRResultString(static_cast<int32_t>(result));
        return false;
    }

    availableExtensions_.clear();
    if (count == 0) {
        XRLogWarn("Runtime advertises no OpenXR extensions");
        return true;
    }

    std::vector<XrExtensionProperties> properties(
        count, XrExtensionProperties{ XR_TYPE_EXTENSION_PROPERTIES, nullptr, {}, 0 });
    result = globals.EnumerateInstanceExtensionProperties(nullptr, count, &count, properties.data());
    if (XR_FAILED(result)) {
        XRLogResultFailure("xrEnumerateInstanceExtensionProperties", static_cast<int32_t>(result));
        outError = "xrEnumerateInstanceExtensionProperties failed: " +
                   XRResultString(static_cast<int32_t>(result));
        return false;
    }

    availableExtensions_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        availableExtensions_.push_back(
            fromXrBuffer(properties[i].extensionName, XR_MAX_EXTENSION_NAME_SIZE));
    }
    std::sort(availableExtensions_.begin(), availableExtensions_.end());
    XRLogInfo("Available extensions: " + std::to_string(availableExtensions_.size()));
    return true;
}

bool XRRuntime::createInstance(const ProjectOpenXRSettings& settings,
                               void* applicationVM,
                               void* applicationActivity,
                               std::string& outError) {
    std::vector<XRSkippedFeature> skipped;
    std::vector<const char*> extensions =
        BuildRequestedExtensions(settings, availableExtensions_, skipped);

    for (const XRSkippedFeature& skip : skipped) {
        XRLogWarn(std::string("Skipping ") + FeatureDisplayName(skip.feature) + ": " + skip.reason);
    }

    // The graphics binding extension is the one thing that is not optional: with
    // no way to bind a GL ES context there is no point creating a session.
// Keyed on "this build has a graphics binding" rather than on __ANDROID__, for
// the same reason as the two blocks above: what makes the check meaningful is
// that we intend to bind GL ES, not which OS we happen to be compiling for.
#if MODULARITY_XR_HAS_GRAPHICS_BINDING
    const char* graphicsExtension = FeatureExtensionName(XRFeature::OpenGLESGraphics);
    const bool hasGraphics =
        std::find_if(extensions.begin(), extensions.end(), [&](const char* name) {
            return std::strcmp(name, graphicsExtension) == 0;
        }) != extensions.end();
    if (!hasGraphics) {
        outError = std::string("This OpenXR runtime does not support ") + graphicsExtension +
                   ", which Modularity requires for its OpenGL ES rendering path. "
                   "(Vulkan XR bindings are intentionally not implemented.)";
        return false;
    }
#endif

    XrInstanceCreateInfo createInfo{};
    createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
    createInfo.next = nullptr;
    createInfo.createFlags = 0;
    createInfo.enabledApiLayerCount = 0;
    createInfo.enabledApiLayerNames = nullptr;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.enabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

    std::snprintf(createInfo.applicationInfo.applicationName,
                  XR_MAX_APPLICATION_NAME_SIZE, "%s", "Modularity");
    createInfo.applicationInfo.applicationVersion = 1;
    std::snprintf(createInfo.applicationInfo.engineName,
                  XR_MAX_ENGINE_NAME_SIZE, "%s", "Modularity");
    createInfo.applicationInfo.engineVersion = 1;
    // 1.0 rather than XR_CURRENT_API_VERSION: asking for the newest version the
    // headers know about gets XR_ERROR_API_VERSION_UNSUPPORTED from runtimes that
    // only implement 1.0, and Modularity uses no 1.1-only entry points.
    createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;

// Same reasoning as initializeAndroidLoader: the precondition is that
// XrInstanceCreateInfoAndroidKHR exists, which is what this macro means.
#if defined(XR_KHR_android_create_instance)
    XrInstanceCreateInfoAndroidKHR androidInfo{};
    if (std::find(availableExtensions_.begin(), availableExtensions_.end(),
                  std::string(FeatureExtensionName(XRFeature::AndroidCreateInstance))) !=
        availableExtensions_.end()) {
        androidInfo.type = XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR;
        androidInfo.next = nullptr;
        androidInfo.applicationVM = applicationVM;
        androidInfo.applicationActivity = applicationActivity;
        createInfo.next = &androidInfo;
    }
#else
    (void)applicationVM;
    (void)applicationActivity;
#endif

    const XrResult result = XRLoaderGlobals().CreateInstance(&createInfo, &instance_);
    if (XR_FAILED(result) || instance_ == XR_NULL_HANDLE) {
        instance_ = XR_NULL_HANDLE;
        XRLogResultFailure("xrCreateInstance", static_cast<int32_t>(result));
        outError = "xrCreateInstance failed: " + XRResultString(static_cast<int32_t>(result));
        return false;
    }

    enabledExtensions_.clear();
    enabledExtensions_.reserve(extensions.size());
    for (const char* name : extensions) enabledExtensions_.emplace_back(name);

    // Resolved after the enabled list is built: extension entry points may only
    // be looked up for extensions that actually made it into the instance.
    std::string resolveError;
    if (!XRLoaderResolveInstanceFunctions(instance_, enabledExtensions_, resolveError)) {
        outError = resolveError;
        XRLogError(outError);
        return false;
    }

    MutableDiagnostics().instanceCreated = true;
    XRLogInfo("Instance created with " + std::to_string(enabledExtensions_.size()) +
              " extension(s)");
    for (const std::string& name : enabledExtensions_) {
        XRLogInfo("  enabled: " + name);
    }
    return true;
}

bool XRRuntime::queryInstanceProperties(std::string& outError) {
    XrInstanceProperties properties{};
    properties.type = XR_TYPE_INSTANCE_PROPERTIES;
    const XrResult result = XRLoaderInstanceFunctions().GetInstanceProperties(instance_, &properties);
    if (XR_FAILED(result)) {
        XRLogResultFailure("xrGetInstanceProperties", static_cast<int32_t>(result));
        outError = "xrGetInstanceProperties failed: " + XRResultString(static_cast<int32_t>(result));
        return false;
    }

    runtimeName_ = fromXrBuffer(properties.runtimeName, XR_MAX_RUNTIME_NAME_SIZE);
    XRDiagnosticsSnapshot& diag = MutableDiagnostics();
    diag.runtimeName = runtimeName_;
    diag.runtimeVersion = versionString(properties.runtimeVersion);
    diag.openXRVersion = versionString(XR_API_VERSION_1_0);

    XRLogInfo("Runtime: " + (runtimeName_.empty() ? std::string("(unnamed)") : runtimeName_));
    XRLogInfo("Runtime version: " + diag.runtimeVersion);
    return true;
}

bool XRRuntime::acquireSystem(std::string& outError) {
    XrSystemGetInfo systemInfo{};
    systemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    const XrResult result =
        XRLoaderInstanceFunctions().GetSystem(instance_, &systemInfo, &systemId_);
    if (XR_FAILED(result) || systemId_ == XR_NULL_SYSTEM_ID) {
        systemId_ = XR_NULL_SYSTEM_ID;
        hasSystem_ = false;
        // XR_ERROR_FORM_FACTOR_UNAVAILABLE just means "headset not ready yet",
        // which is routine on Quest during boot, so it is not logged as an error.
        outError = "xrGetSystem: no head-mounted display available yet (" +
                   XRResultString(static_cast<int32_t>(result)) + ")";
        return false;
    }

    XrSystemProperties systemProperties{};
    systemProperties.type = XR_TYPE_SYSTEM_PROPERTIES;
    const XrResult propsResult = XRLoaderInstanceFunctions().GetSystemProperties(
        instance_, systemId_, &systemProperties);
    if (XR_SUCCEEDED(propsResult)) {
        systemName_ = fromXrBuffer(systemProperties.systemName, XR_MAX_SYSTEM_NAME_SIZE);
    }

    hasSystem_ = true;
    MutableDiagnostics().systemAcquired = true;
    MutableDiagnostics().systemName = systemName_;
    XRLogInfo("System acquired: " + (systemName_.empty() ? std::string("(unnamed)") : systemName_));
    return true;
}

bool XRRuntime::queryViewConfiguration(std::string& outError) {
    const XRInstanceFunctions& fns = XRLoaderInstanceFunctions();
    views_.clear();
    blendModes_.clear();

    uint32_t viewCount = 0;
    XrResult result = fns.EnumerateViewConfigurationViews(
        instance_, systemId_, viewConfigurationType_, 0, &viewCount, nullptr);
    if (XR_FAILED(result)) {
        XRLogResultFailure("xrEnumerateViewConfigurationViews", static_cast<int32_t>(result));
        outError = "xrEnumerateViewConfigurationViews failed: " +
                   XRResultString(static_cast<int32_t>(result));
        return false;
    }
    if (viewCount == 0) {
        outError = "Runtime reported zero views for the stereo view configuration.";
        return false;
    }

    std::vector<XrViewConfigurationView> configViews(
        viewCount, XrViewConfigurationView{ XR_TYPE_VIEW_CONFIGURATION_VIEW, nullptr, 0, 0, 0, 0, 0, 0 });
    result = fns.EnumerateViewConfigurationViews(instance_, systemId_, viewConfigurationType_,
                                                 viewCount, &viewCount, configViews.data());
    if (XR_FAILED(result)) {
        XRLogResultFailure("xrEnumerateViewConfigurationViews", static_cast<int32_t>(result));
        outError = "xrEnumerateViewConfigurationViews failed: " +
                   XRResultString(static_cast<int32_t>(result));
        return false;
    }

    views_.reserve(viewCount);
    for (const XrViewConfigurationView& view : configViews) {
        XRViewInfo info;
        info.recommendedWidth = view.recommendedImageRectWidth;
        info.recommendedHeight = view.recommendedImageRectHeight;
        info.maxWidth = view.maxImageRectWidth;
        info.maxHeight = view.maxImageRectHeight;
        info.recommendedSampleCount = view.recommendedSwapchainSampleCount;
        info.maxSampleCount = view.maxSwapchainSampleCount;
        views_.push_back(info);
    }

    XRLogInfo("Stereo view count: " + std::to_string(views_.size()));
    if (!views_.empty()) {
        XRLogInfo("Recommended eye resolution: " + std::to_string(views_[0].recommendedWidth) +
                  "x" + std::to_string(views_[0].recommendedHeight) + " (" +
                  std::to_string(views_[0].recommendedSampleCount) + "x MSAA)");
    }

    // Environment blend modes, for passthrough-capable runtimes later.
    uint32_t blendCount = 0;
    if (XR_SUCCEEDED(fns.EnumerateEnvironmentBlendModes(instance_, systemId_,
                                                        viewConfigurationType_, 0, &blendCount,
                                                        nullptr)) &&
        blendCount > 0) {
        blendModes_.resize(blendCount);
        if (XR_FAILED(fns.EnumerateEnvironmentBlendModes(instance_, systemId_,
                                                         viewConfigurationType_, blendCount,
                                                         &blendCount, blendModes_.data()))) {
            blendModes_.clear();
        }
    }
    return true;
}

void XRRuntime::publishDiagnostics(const ProjectOpenXRSettings& settings) {
    XRDiagnosticsSnapshot& diag = MutableDiagnostics();
    diag.compiledIn = true;
    diag.graphicsApi = ToString(settings.graphicsBackend);
    diag.availableExtensions = availableExtensions_;
    diag.enabledExtensions = enabledExtensions_;

    diag.viewCount = static_cast<uint32_t>(views_.size());
    diag.viewConfiguration = (views_.size() == 2) ? "Stereo"
                            : views_.empty()      ? std::string()
                                                  : std::to_string(views_.size()) + " views";
    if (!views_.empty()) {
        diag.recommendedWidth = views_[0].recommendedWidth;
        diag.recommendedHeight = views_[0].recommendedHeight;
        diag.maxWidth = views_[0].maxWidth;
        diag.maxHeight = views_[0].maxHeight;
        diag.recommendedSwapchainSampleCount = views_[0].recommendedSampleCount;
    }

    diag.blendModes.clear();
    for (XrEnvironmentBlendMode mode : blendModes_) {
        diag.blendModes.emplace_back(blendModeName(mode));
    }

    // Capability reporting is "advertised AND implemented here", never just
    // advertised - a true in this struct is a promise the feature works.
    diag.depthSubmissionSupported = isFeatureActive(XRFeature::CompositionLayerDepth);
    diag.handTrackingSupported = isFeatureActive(XRFeature::QuestHandTracking);
    diag.passthroughSupported = isFeatureActive(XRFeature::QuestPassthrough);
    diag.displayRefreshRateSupported = isFeatureActive(XRFeature::QuestDisplayRefreshRate);
    diag.foveatedRenderingSupported = isFeatureActive(XRFeature::QuestFoveatedRendering);
    // Multiview is a GL extension, not an OpenXR one, so it can only be answered
    // once a GL context is current. XRSwapchainGL fills this in later.
    diag.multiviewSupported = false;

    diag.activeTrackingOrigin = ToString(settings.trackingOrigin);
    // Render mode and depth submission report as "requested" until the session is
    // up and the runtime fallbacks are known.
    diag.activeRenderMode = ToString(settings.renderMode);
    diag.activeDepthSubmission =
        diag.depthSubmissionSupported ? ToString(settings.depthSubmission)
                                      : ToString(XRDepthSubmission::None);
}

void XRRuntime::shutdown() {
    if (instance_ != XR_NULL_HANDLE) {
        const XRInstanceFunctions& fns = XRLoaderInstanceFunctions();
        if (fns.DestroyInstance) fns.DestroyInstance(instance_);
        instance_ = XR_NULL_HANDLE;
        XRLoaderReleaseInstanceFunctions();
        XRLogInfo("Instance destroyed");
    }
    systemId_ = XR_NULL_SYSTEM_ID;
    blendModes_.clear();
    initialized_ = false;
    hasSystem_ = false;
    runtimeName_.clear();
    systemName_.clear();
    availableExtensions_.clear();
    enabledExtensions_.clear();
    views_.clear();
}

#endif // MODULARITY_HAS_OPENXR

} // namespace Modularity::XR
