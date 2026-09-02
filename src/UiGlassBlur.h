#pragma once

#include "imgui.h"

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

// Same capture, but driven by game UI instead of by a ModuGUI window: queue "grab the
// framebuffer as it stands right now and blur it" into `drawList`, then draw
// BackdropTexture() over the rect you want frosted (UVs = rect / viewport, since the
// capture always covers the whole viewport). Sprites you batched earlier must already be
// in the draw list - draw commands execute in order, so anything appended afterwards
// lands ON TOP of the capture instead of inside it.
//
// Returns false when the feature is unavailable (Vulkan, kill switch, a failed capture),
// in which case nothing is queued and the caller should just skip the frosted layer.
bool EmitBackdropCapture(ImDrawList* drawList);

// The blurred-framebuffer texture. Stable for the process lifetime, so it is safe to
// record into draw commands. 0 when the feature is unavailable.
ImTextureID BackdropTexture();

} // namespace UiGlassBlur
} // namespace Modularity
