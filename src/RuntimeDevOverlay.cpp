#include "EditorLocalization.h"
#include "Engine.h"
#include "ThirdParty/ModuGUI/imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Loc = Modularity::Loc;

// Runtime "lite" dev tools overlay.
//
// This ships INSIDE the player (core_player), not the editor, so it has to be
// runtime-safe: no editor-only panels, no host compiler, no desktop file
// dialogs. It's the on-device tooling path we settled on instead of trying to
// cram the whole editor onto a phone. You pick a scene object and nudge its
// transform / toggle it live, reusing the touch + ImGui input wiring that the
// Android runtime already feeds.
//
// It's gated to development builds (or the MODU_DEV_OVERLAY env var on desktop,
// which makes it easy to test the desktop player), so a shipped game never
// shows it. Edits go to local* transforms because the hierarchy pass derives
// world transforms from those (see updateHierarchyWorldTransforms); writing
// world position directly would just get stomped next frame.

namespace {
bool DevOverlayAllowed(bool developmentBuild) {
    if (developmentBuild) return true;
    const char* env = std::getenv("MODU_DEV_OVERLAY");
    return env && env[0] != '\0' && std::strcmp(env, "0") != 0;
}
} // namespace

void Engine::renderRuntimeDevOverlay() {
    // devToolsForced_ is set by --play; otherwise honor the dev-build flag / env.
    if (!devToolsForced_ && !DevOverlayAllowed(buildSettings.developmentBuild)) return;

    ImGuiIO& io = ImGui::GetIO();
    const float pad = 10.0f;

    // Always-visible toggle pill, top-left, big enough to tap with a thumb.
    ImGui::SetNextWindowPos(ImVec2(pad, pad), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    if (ImGui::Begin("##DevOverlayToggle", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav)) {
        if (ImGui::Button(devOverlayOpen_ ? "Dev Tools  v" : "Dev Tools  >")) {
            devOverlayOpen_ = !devOverlayOpen_;
        }
    }
    ImGui::End();

    if (!devOverlayOpen_) return;

    // Panel sized as a fraction of the screen so it stays usable on a phone.
    const float panelW = std::min(io.DisplaySize.x * 0.6f, 460.0f);
    const float panelH = std::max(220.0f, io.DisplaySize.y - 2.0f * pad - 60.0f);
    ImGui::SetNextWindowPos(ImVec2(pad, pad + 50.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.92f);
    if (ImGui::Begin(Loc::Window("WINDOW_RUNTIME_DEV_TOOLS", "Runtime Dev Tools"), &devOverlayOpen_, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("FPS %.0f   Objects %d", io.Framerate, static_cast<int>(sceneObjects.size()));
        ImGui::Separator();

        ImGui::TextDisabled("Objects (tap to select)");
        ImGui::BeginChild("##DevObjList", ImVec2(0.0f, panelH * 0.4f), true);
        for (const SceneObject& obj : sceneObjects) {
            char label[288];
            std::snprintf(label, sizeof(label), "%s  #%d%s",
                          obj.name.c_str(), obj.id, obj.enabled ? "" : "  (off)");
            if (ImGui::Selectable(label, obj.id == devOverlaySelectedId_)) {
                devOverlaySelectedId_ = obj.id;
            }
        }
        ImGui::EndChild();

        ImGui::Separator();

        SceneObject* sel = nullptr;
        for (SceneObject& obj : sceneObjects) {
            if (obj.id == devOverlaySelectedId_) { sel = &obj; break; }
        }
        if (!sel) {
            ImGui::TextDisabled("No object selected.");
        } else {
            ImGui::Text("%s", sel->name.c_str());
            bool enabled = sel->enabled;
            if (ImGui::Checkbox("Enabled", &enabled)) sel->enabled = enabled;

            auto editVec = [&](const char* lbl, glm::vec3& v, float speed) {
                float tmp[3] = { v.x, v.y, v.z };
                if (ImGui::DragFloat3(lbl, tmp, speed)) {
                    v = glm::vec3(tmp[0], tmp[1], tmp[2]);
                    sel->localInitialized = true; // keep our edit from being re-derived
                }
            };
            editVec("Position", sel->localPosition, 0.05f);
            editVec("Rotation", sel->localRotation, 0.5f);
            editVec("Scale",    sel->localScale,    0.02f);

            if (ImGui::Button("Reset Scale")) {
                sel->localScale = glm::vec3(1.0f);
                sel->localInitialized = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Deselect")) devOverlaySelectedId_ = -1;
        }
    }
    ImGui::End();
}
