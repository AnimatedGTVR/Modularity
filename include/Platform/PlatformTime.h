#pragma once

// Cross-platform timing + GL-context queries that the engine used to get
// from GLFW. Engine code calls these instead of glfwGetTime() /
// glfwGetCurrentContext() so the same source can run on desktop (GLFW)
// and Android (clock_gettime / EGL).

namespace Modularity::Platform {

// Monotonic time in seconds since some unspecified start point. Suitable
// for shader uniforms and frame deltas. Desktop defers to glfwGetTime();
// Android uses clock_gettime(CLOCK_MONOTONIC).
double GetTimeSeconds();

// True iff a usable GL/GLES context is current on this thread. Desktop
// checks glfwGetCurrentContext(); Android assumes true once the runtime's
// EGL setup has succeeded (the runtime ensures it before invoking render).
bool HasCurrentGLContext();

} // namespace Modularity::Platform
