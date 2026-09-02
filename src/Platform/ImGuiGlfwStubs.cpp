#ifdef __ANDROID__

#include "ImGuiGlfwStubs.h"

// No-op implementations of the imgui GLFW backend entry points. On Android
// the engine never calls these (the editor / window-driven code paths are
// gated off), but the engine source still references them, so link-time
// these resolve to inert symbols. Replace with a real Android-native ImGui
// platform layer once we wire the runtime in Stage 2.

// Both Init functions report success even though they do nothing, because the
// engine's init path checks the return and would bail otherwise. The
// runtime drives ImGui input via Android's event pump separately (see
// Stage 2.5+ in docs/AndroidRuntime.md), so an inert "successful" init
// is the right shape.
bool ImGui_ImplGlfw_InitForOpenGL(GLFWwindow*, bool) { return true; }
bool ImGui_ImplGlfw_InitForVulkan(GLFWwindow*, bool) { return true; }
void ImGui_ImplGlfw_Shutdown() {}
void ImGui_ImplGlfw_NewFrame() {}
void ImGui_ImplGlfw_Sleep(int) {}
float ImGui_ImplGlfw_GetContentScaleForWindow(GLFWwindow*) { return 1.0f; }
float ImGui_ImplGlfw_GetContentScaleForMonitor(GLFWmonitor*) { return 1.0f; }

#endif // __ANDROID__
