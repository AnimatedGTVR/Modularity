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

} // namespace Modularity::AndroidRuntime

#endif // __ANDROID__
