#pragma once

// Forward-declare the GLFW handles so the function signatures here match
// the real ImGui GLFW backend (which we don't ship on Android). On Android
// these calls are never executed (the editor path is gated off), but the
// engine source still references them and needs them to be link-complete.
struct GLFWwindow;
struct GLFWmonitor;

bool  ImGui_ImplGlfw_InitForOpenGL(GLFWwindow* window, bool install_callbacks);
bool  ImGui_ImplGlfw_InitForVulkan(GLFWwindow* window, bool install_callbacks);
void  ImGui_ImplGlfw_Shutdown();
void  ImGui_ImplGlfw_NewFrame();
void  ImGui_ImplGlfw_Sleep(int milliseconds);
float ImGui_ImplGlfw_GetContentScaleForWindow(GLFWwindow* window);
float ImGui_ImplGlfw_GetContentScaleForMonitor(GLFWmonitor* monitor);

// The raw glfw* functions used by the engine (glfwGetTime, glfwGetKey,
// glfwSetWindowShouldClose, etc.) are provided by the real glfw3.h +
// GLFW's null backend on Android, so no stubs needed here.
