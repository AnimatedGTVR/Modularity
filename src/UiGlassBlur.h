#pragma once

// engine side of the ModuGUI frosted glass windows (the ModuGUI half lives in
// imgui.cpp, search for GlassBlur). ModuGUI emits a draw callback right before a
// translucent window paints its background; that callback lands here, we grab the
// framebuffer as it exists at that exact moment, downsample + blur it into a small
// texture, and ModuGUI samples it back under the window tint. windows render
// back-to-front so "the framebuffer right now" IS everything behind that window.
//
// OpenGL only. under Vulkan just never call Install() and the whole feature stays
// inert (ModuGUI checks for a registered renderer before emitting anything).
namespace Modularity {
namespace UiGlassBlur {

// call once after ImGui_ImplOpenGL3_Init, with the GL context current.
// safe to call again after a Shutdown() (project reload etc).
void Install();

// frees the GL objects and unhooks from ImGui. GL context must still be current.
void Shutdown();

} // namespace UiGlassBlur
} // namespace Modularity
