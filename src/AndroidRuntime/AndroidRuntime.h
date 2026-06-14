#pragma once

// Placeholder header for the Android runtime entry points. The full Android
// bring-up is staged separately (see docs/AndroidRuntime.md). This file is
// excluded from desktop builds via CMakeLists.txt and only takes effect when
// compiling with the Android NDK toolchain (__ANDROID__ defined).

#ifdef __ANDROID__

struct android_app;

namespace Modularity::AndroidRuntime {

// Bootstraps the engine against a NativeActivity-style android_app. Wires up
// lifecycle callbacks, creates an EGL context, and hands control to the
// existing Engine update loop. Implemented in AndroidRuntime.cpp.
void Run(android_app* app);

// === Hooks called from Engine::run() ====================================
// Engine.cpp talks to AndroidRuntime through these so its main loop stays
// otherwise structurally identical to the desktop path. Each is a no-op
// outside of Android (and the call sites are gated with #ifdef __ANDROID__
// inside Engine.cpp).

// Drain pending Android lifecycle + input events. Returns false if the
// app has been asked to shut down, meaning the engine should break out of its loop.
bool PollEvents();

// Present the current backbuffer via eglSwapBuffers. No-op (returns
// false) if there's no live render surface (window torn down, paused
// in background, etc.).
bool PresentFrame();

// True iff EGL has a live window-surface bound right now. Engine should
// skip its render + present and just spin through event processing when
// this returns false (e.g. while the activity is backgrounded).
bool HasRenderSurface();

// Current EGL surface size in pixels. Useful for the engine's
// framebuffer-size queries when GLFW's null-backend returns nothing
// meaningful.
void GetSurfaceSize(int* outWidth, int* outHeight);

// Per-app writable data directory (NativeActivity::internalDataPath,
// typically /data/data/<pkg>/files). Returns nullptr if the runtime
// hasn't received the activity pointer yet. The engine's
// ProjectManager / save paths key off this on Android, since neither
// HOME nor APPDATA exists.
const char* GetInternalDataPath();

} // namespace Modularity::AndroidRuntime

#endif // __ANDROID__
