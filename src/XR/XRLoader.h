#pragma once

// Runtime discovery of the OpenXR loader.
//
// Modularity never links libopenxr_loader. This opens it with dlopen (LoadLibrary
// on Windows), pulls out xrGetInstanceProcAddr, and builds a dispatch table from
// there. Three consequences worth stating plainly, because they are the whole
// reason for this file:
//
//   * A build with OpenXR compiled in adds nothing to the link line, so it still
//     runs on a machine with no OpenXR runtime installed. That is what makes
//     "OpenXR enabled" independent of "Vulkan enabled" - neither implies the other.
//   * On Quest the loader is whatever libopenxr_loader.so the APK packages, which
//     makes loader choice a packaging decision instead of a compile-time one.
//   * Everything degrades to "OpenXR unavailable" instead of a link error or a
//     crash, which is what section 5's controlled-error-state requirement needs.
//
// Call order: XRLoaderInit() -> XRLoaderInstanceFunctions() after xrCreateInstance
// -> XRLoaderShutdown(). Not thread-safe; the render thread owns all of it.

#include <cstdint>
#include <string>
#include <vector>

#if MODULARITY_HAS_OPENXR

// Not <openxr/openxr.h> directly: XRPlatform.h sets the XR_USE_* macros first so
// the Android and OpenGL ES types actually get declared.
#include "XRPlatform.h"

namespace Modularity::XR {

// Global-level entry points: everything callable before an XrInstance exists.
// xrGetInstanceProcAddr is the only symbol actually looked up in the shared
// object; the rest come through it, which is what the OpenXR spec requires.
struct XRGlobalFunctions {
    PFN_xrGetInstanceProcAddr                     GetInstanceProcAddr = nullptr;
    PFN_xrEnumerateApiLayerProperties             EnumerateApiLayerProperties = nullptr;
    PFN_xrEnumerateInstanceExtensionProperties    EnumerateInstanceExtensionProperties = nullptr;
    PFN_xrCreateInstance                          CreateInstance = nullptr;
#ifdef XR_KHR_loader_init
    // Android only, and mandatory there: the loader needs the JavaVM and the
    // activity's Context before any other call. Resolved optionally so desktop
    // loaders that do not export it are not treated as broken.
    PFN_xrInitializeLoaderKHR                     InitializeLoaderKHR = nullptr;
#endif
};

// Instance-level entry points, resolved once an XrInstance exists.
struct XRInstanceFunctions {
    PFN_xrDestroyInstance                         DestroyInstance = nullptr;
    PFN_xrGetInstanceProperties                   GetInstanceProperties = nullptr;
    PFN_xrResultToString                          ResultToString = nullptr;
    PFN_xrStructureTypeToString                   StructureTypeToString = nullptr;
    PFN_xrGetSystem                               GetSystem = nullptr;
    PFN_xrGetSystemProperties                     GetSystemProperties = nullptr;
    PFN_xrEnumerateViewConfigurations             EnumerateViewConfigurations = nullptr;
    PFN_xrGetViewConfigurationProperties          GetViewConfigurationProperties = nullptr;
    PFN_xrEnumerateViewConfigurationViews         EnumerateViewConfigurationViews = nullptr;
    PFN_xrEnumerateEnvironmentBlendModes          EnumerateEnvironmentBlendModes = nullptr;
    PFN_xrStringToPath                            StringToPath = nullptr;
    PFN_xrPathToString                            PathToString = nullptr;
    PFN_xrPollEvent                               PollEvent = nullptr;
};

// Session, space, swapchain and frame entry points. Split out from the instance
// table only for readability: OpenXR resolves every one of these through
// xrGetInstanceProcAddr with the same XrInstance, so all three tables are filled
// by the same call.
struct XRSessionFunctions {
    PFN_xrCreateSession                           CreateSession = nullptr;
    PFN_xrDestroySession                          DestroySession = nullptr;
    PFN_xrBeginSession                            BeginSession = nullptr;
    PFN_xrEndSession                              EndSession = nullptr;
    PFN_xrRequestExitSession                      RequestExitSession = nullptr;

    PFN_xrEnumerateReferenceSpaces                EnumerateReferenceSpaces = nullptr;
    PFN_xrCreateReferenceSpace                    CreateReferenceSpace = nullptr;
    PFN_xrDestroySpace                            DestroySpace = nullptr;
    PFN_xrLocateSpace                             LocateSpace = nullptr;

    PFN_xrEnumerateSwapchainFormats               EnumerateSwapchainFormats = nullptr;
    PFN_xrCreateSwapchain                         CreateSwapchain = nullptr;
    PFN_xrDestroySwapchain                        DestroySwapchain = nullptr;
    PFN_xrEnumerateSwapchainImages                EnumerateSwapchainImages = nullptr;
    PFN_xrAcquireSwapchainImage                   AcquireSwapchainImage = nullptr;
    PFN_xrWaitSwapchainImage                      WaitSwapchainImage = nullptr;
    PFN_xrReleaseSwapchainImage                   ReleaseSwapchainImage = nullptr;

    PFN_xrWaitFrame                               WaitFrame = nullptr;
    PFN_xrBeginFrame                              BeginFrame = nullptr;
    PFN_xrEndFrame                                EndFrame = nullptr;
    PFN_xrLocateViews                             LocateViews = nullptr;
};

// Action-system entry points. Everything ModuInput.XR and the XR components are
// built on. Resolved with the rest; a runtime missing any of these is not usable
// for input, which is reported rather than crashed on.
struct XRInputFunctions {
    PFN_xrCreateActionSet                         CreateActionSet = nullptr;
    PFN_xrDestroyActionSet                        DestroyActionSet = nullptr;
    PFN_xrCreateAction                            CreateAction = nullptr;
    PFN_xrDestroyAction                           DestroyAction = nullptr;
    PFN_xrSuggestInteractionProfileBindings       SuggestInteractionProfileBindings = nullptr;
    PFN_xrAttachSessionActionSets                 AttachSessionActionSets = nullptr;
    PFN_xrGetCurrentInteractionProfile            GetCurrentInteractionProfile = nullptr;
    PFN_xrSyncActions                             SyncActions = nullptr;
    PFN_xrGetActionStateBoolean                   GetActionStateBoolean = nullptr;
    PFN_xrGetActionStateFloat                     GetActionStateFloat = nullptr;
    PFN_xrGetActionStateVector2f                  GetActionStateVector2f = nullptr;
    PFN_xrGetActionStatePose                      GetActionStatePose = nullptr;
    PFN_xrCreateActionSpace                       CreateActionSpace = nullptr;
    PFN_xrApplyHapticFeedback                     ApplyHapticFeedback = nullptr;
    PFN_xrStopHapticFeedback                      StopHapticFeedback = nullptr;
};

// Entry points that only exist when their extension was enabled. Each is null
// unless the corresponding extension made it into xrCreateInstance, so a null
// here means "not available", never "resolution failed".
struct XRExtensionFunctions {
#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
    // Mandatory before xrCreateSession on the GL ES binding: skipping it is
    // XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING at session creation.
    PFN_xrGetOpenGLESGraphicsRequirementsKHR      GetOpenGLESGraphicsRequirementsKHR = nullptr;
#endif
};

// Opens the loader and resolves the global functions. Idempotent: a second call
// while already loaded is a no-op returning true. On failure `outError` explains
// which stage failed and the loader stays unloaded.
bool XRLoaderInit(std::string& outError);

// True once the loader is open and xrGetInstanceProcAddr resolved.
bool XRLoaderAvailable();

// Path of the shared object that was opened, for diagnostics. Empty when unloaded.
const std::string& XRLoaderPath();

const XRGlobalFunctions& XRLoaderGlobals();

// Resolves the instance, session, input and extension tables. Must be called
// immediately after a successful xrCreateInstance and before any instance-level
// call. `enabledExtensions` is the list actually passed to xrCreateInstance;
// extension entry points are only looked up for extensions in it, so a null in
// XRExtensionFunctions always means "the extension was not enabled", never
// "resolution failed".
bool XRLoaderResolveInstanceFunctions(XrInstance instance,
                                      const std::vector<std::string>& enabledExtensions,
                                      std::string& outError);

const XRInstanceFunctions& XRLoaderInstanceFunctions();
const XRSessionFunctions& XRLoaderSessionFunctions();
const XRInputFunctions& XRLoaderInputFunctions();
const XRExtensionFunctions& XRLoaderExtensionFunctions();

// Forgets the instance table. Call before destroying the instance; the loader
// itself stays open (dlclose is deliberately avoided, see XRLoaderShutdown).
void XRLoaderReleaseInstanceFunctions();

// Drops the global table. Does NOT dlclose the loader: OpenXR runtimes commonly
// spawn threads and register atexit handlers, and unmapping the library out from
// under those is the same class of dangling-pointer bug documented for
// ScriptRuntime::unloadAll. The process is exiting anyway.
void XRLoaderShutdown();

// Convenience wrapper used by XRDiagnostics, which must not depend on whether an
// instance currently exists. Returns false when no runtime stringifier is
// reachable, leaving the caller to use its own table.
bool XRLoaderResultToString(int32_t result, std::string& out);

} // namespace Modularity::XR

#else // !MODULARITY_HAS_OPENXR

namespace Modularity::XR {
inline bool XRLoaderResultToString(int32_t, std::string&) { return false; }
} // namespace Modularity::XR

#endif // MODULARITY_HAS_OPENXR
