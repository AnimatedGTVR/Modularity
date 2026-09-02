#pragma once

// Android runtime entry points. desktop builds exclude this file via CMakeLists;
// it only means anything under the NDK (__ANDROID__).

#ifdef __ANDROID__

#include <string>
#include <vector>

struct android_app;

namespace Modularity::AndroidRuntime {

// bootstraps the engine against a NativeActivity android_app: lifecycle callbacks,
// EGL context, then control goes to the normal Engine loop. impl in AndroidRuntime.cpp.
void Run(android_app* app);

// Hooks called from Engine::run(), so its main loop stays structurally identical
// to the desktop path. all of these are no-ops off Android.

// Drain pending Android lifecycle + input events. Returns false if the
// app has been asked to shut down, meaning the engine should break out of its loop.
bool PollEvents();

// present via eglSwapBuffers. returns false when there's no live surface
// (backgrounded, window torn down).
bool PresentFrame();

// true iff EGL has a live window surface. when false the engine should skip
// render + present and just pump events.
bool HasRenderSurface();

// current EGL surface size in pixels ( GLFW's null backend returns nothing useful ).
void GetSurfaceSize(int* outWidth, int* outHeight);

// per-app writable data dir (/data/data/<pkg>/files). nullptr until the activity
// pointer arrives. save paths key off this since HOME/APPDATA don't exist here.
const char* GetInternalDataPath();

// OpenXR platform handles
// XR_KHR_loader_init_android needs the JavaVM and the activity Context before any
// other OpenXR call, and XR_KHR_android_create_instance wants both again at
// xrCreateInstance. Returned as void* so src/XR/ never has to include <jni.h>.
// Both are null until APP_CMD_INIT_WINDOW has given us an activity, so callers
// must treat null as "not ready yet" rather than as an error.
void* GetJavaVM();
void* GetActivityObject();

// The live EGL objects the runtime already owns. OpenXR binds the *existing*
// context through XrGraphicsBindingOpenGLESAndroidKHR rather than creating a
// second one, so these are handed over as-is. EGL_NO_DISPLAY / nullptr /
// EGL_NO_CONTEXT are returned as null when EGL is not up.
void* GetEglDisplay();
void* GetEglConfig();
void* GetEglContext();

// Touch input
// the runtime tracks every live finger; the engine turns the primary one into ImGui
// mouse events plus a touch snapshot for scripts. coords are surface pixels, top-left origin.

// Number of touch pointers currently down (0..kMaxTouchPointers).
int GetPointerCount();

// Fills the i-th active pointer's id and position. Returns false if i is out
// of range. `id` is the Android pointer id (stable for a finger's lifetime).
bool GetPointer(int i, int* outId, float* outX, float* outY);

// the primary pointer (oldest finger down) as a mouse analog. x/y keep the last
// known position while inactive.
bool GetPrimaryPointer(float* outX, float* outY, bool* outActive);

// Keyboard
// SetSoftKeyboardVisible(io.WantTextInput) runs each frame but only JNI-toggles on change;
// PollInputChar / PollKeyEvent drain the key queues for ImGui. InputConnection-style soft
// keyboards never emit key events ( NativeActivity limitation ), hardware keyboards always work.
void SetSoftKeyboardVisible(bool visible);
bool PollInputChar(unsigned int* outChar);
bool PollKeyEvent(int* outKeyCode, bool* outDown);

// Android system file picker
// the Java bridge copies picked content URIs into app cache files, and this side hands
// those real paths to the existing File Browser import path.
struct FilePickerResult {
    std::vector<std::string> paths;
    std::string error;
    bool canceled = false;
};

bool SupportsFilePicker();
bool RequestFilePicker(bool allowMultiple, std::string& error);
bool PollFilePickerResult(FilePickerResult& result);

// Shared-storage access ("all files access")
// the editor wants projects in public Documents to mirror desktop, which needs a storage
// grant first. wraps the Java bridge; safe no-ops in the player APK (no bridge).

// public Documents dir, e.g. /storage/emulated/0/Documents. empty if unavailable
// or the bridge isn't packaged.
std::string GetExternalDocumentsPath();

// can we touch shared storage right now? API 30+ = isExternalStorageManager(),
// older = the WRITE_EXTERNAL_STORAGE permission. false without the bridge.
bool HasAllFilesAccess();

// kick off the storage request. API 30+ opens the system settings page, older pops the
// permission dialog. the grant lands async so poll HasAllFilesAccess() after.
// returns false (with error set) if the bridge is missing.
bool RequestAllFilesAccess(std::string& error);

// display density as an ImGui scale (dpi/160, clamped [1,4]). GLFW's content-scale
// stub reports nothing on Android so without this the editor renders tiny.
float GetDisplayDensityScale();

} // namespace Modularity::AndroidRuntime

#endif // __ANDROID__
