#pragma once

// OpenXR logging + the "what did we actually get" snapshot.
//
// Two jobs, deliberately in one place:
//
//  1. Logging. Every OpenXR line goes through XRLog so the prefixes stay
//     consistent ("[OpenXR]", "[OpenXR Input]", "[ModuInput.XR]") and errors
//     always carry the real XrResult name instead of a generic failure message.
//
//  2. XRDiagnosticsSnapshot: what the runtime reported. The Project Settings
//     Runtime Diagnostics section reads this, and it is the honest counterpart to
//     ProjectOpenXRSettings, which only records what the project asked for. If a
//     field was never populated it stays empty/false rather than guessing.
//
// Carries no OpenXR header dependency, so the editor UI can include it in builds
// with MODULARITY_HAS_OPENXR == 0 (where the snapshot simply reads "unavailable").

#include <cstdint>
#include <string>
#include <vector>

namespace Modularity::XR {

enum class XRLogLevel { Info, Warning, Error };

// Routed to stderr, which AndroidRuntime already redirects into logcat under the
// Modularity.stderr tag, so Quest builds get these through `adb logcat` with no
// extra plumbing. Also mirrored into the engine console when a sink is installed.
void XRLog(XRLogLevel level, const std::string& subsystem, const std::string& message);

inline void XRLogInfo(const std::string& message) { XRLog(XRLogLevel::Info, "OpenXR", message); }
inline void XRLogWarn(const std::string& message) { XRLog(XRLogLevel::Warning, "OpenXR", message); }
inline void XRLogError(const std::string& message) { XRLog(XRLogLevel::Error, "OpenXR", message); }

// Lets Engine forward XR logging into the editor console. Passing nullptr detaches.
using XRLogSink = void (*)(XRLogLevel level, const char* subsystem, const char* message);
void SetXRLogSink(XRLogSink sink);

// Human-readable XrResult name. Uses the runtime's own xrResultToString when an
// instance is available (so unknown/vendor results still resolve); otherwise falls
// back to a built-in table of the results Modularity can actually hit. Never
// returns an empty string.
std::string XRResultString(int32_t result);

// One-line failure report, e.g.
//   [OpenXR Error] xrCreateSession failed: XR_ERROR_GRAPHICS_DEVICE_INVALID
void XRLogResultFailure(const char* call, int32_t result);

// What the runtime reported. Everything is "not known yet" until the
// corresponding OpenXR call has succeeded.
struct XRDiagnosticsSnapshot {
    // --- availability ------------------------------------------------------
    bool compiledIn = false;      // MODULARITY_HAS_OPENXR
    bool loaderAvailable = false; // an OpenXR loader was dlopen'd
    bool instanceCreated = false;
    bool systemAcquired = false;
    bool sessionCreated = false;
    // Populated whenever a stage failed; empty on success.
    std::string lastError;
    // Where the loader was found, for "why is this not working" questions.
    std::string loaderPath;

    // --- runtime -----------------------------------------------------------
    std::string runtimeName;      // e.g. "Oculus"
    std::string runtimeVersion;
    std::string openXRVersion;    // API version the instance was created with
    std::string systemName;

    // --- graphics ----------------------------------------------------------
    std::string graphicsApi;      // "OpenGL ES"
    std::string graphicsVersion;  // GL_VERSION of the bound context
    std::string swapchainFormat;  // resolved colour format name

    // --- views -------------------------------------------------------------
    uint32_t viewCount = 0;
    std::string viewConfiguration;         // "Stereo"
    uint32_t recommendedWidth = 0;
    uint32_t recommendedHeight = 0;
    uint32_t maxWidth = 0;
    uint32_t maxHeight = 0;
    uint32_t recommendedSwapchainSampleCount = 0;
    float refreshRateHz = 0.0f;            // 0 = unknown / extension absent
    std::vector<std::string> blendModes;

    // --- capabilities ------------------------------------------------------
    // "Supported" here means the extension was advertised AND Modularity has an
    // implementation for it. A supported-but-unimplemented extension reports false.
    bool multiviewSupported = false;
    bool depthSubmissionSupported = false;
    bool handTrackingSupported = false;
    bool passthroughSupported = false;
    bool displayRefreshRateSupported = false;
    bool foveatedRenderingSupported = false;

    // What is actually running this frame, after runtime fallbacks. May differ
    // from the project settings (e.g. Multiview requested, Multi Pass in use).
    std::string activeRenderMode;
    std::string activeDepthSubmission;
    std::string activeTrackingOrigin;

    // --- extensions --------------------------------------------------------
    std::vector<std::string> availableExtensions;
    std::vector<std::string> enabledExtensions;

    // --- input -------------------------------------------------------------
    std::string activeInteractionProfileLeft;
    std::string activeInteractionProfileRight;
    bool leftControllerActive = false;
    bool rightControllerActive = false;

    bool hasExtension(const std::string& name) const;
    bool isExtensionEnabled(const std::string& name) const;
};

// Process-wide snapshot, written by the XR backend and read by the settings UI.
// Single-threaded by contract: only the render/main thread touches it, the same
// thread that owns the OpenXR session.
XRDiagnosticsSnapshot& MutableDiagnostics();
const XRDiagnosticsSnapshot& Diagnostics();
void ResetDiagnostics();

} // namespace Modularity::XR
