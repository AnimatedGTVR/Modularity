#include "ScriptRuntime.h"
#include "SceneObject.h"
#include "ThirdParty/imgui/imgui.h"
#include <string>
#include <algorithm>

namespace {
bool clampTo120 = false;
float smoothFps = 0.0f;
float smoothing = 0.15f;
}

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    ctx.AutoSetting("ClampFPS120", clampTo120);
    if (ctx.script) {
        std::string saved = ctx.GetSetting("FpsSmoothing", "");
        if (!saved.empty()) {
            try { smoothing = std::stof(saved); } catch (...) {}
        }
    }

    bool changed = false;
    ImGui::TextUnformatted("FPS Display");
    ImGui::Separator();
    changed |= ImGui::Checkbox("Clamp FPS to 120", &clampTo120);
    changed |= ImGui::DragFloat("Smoothing", &smoothing, 0.01f, 0.0f, 1.0f, "%.2f");

    if (changed) {
        ctx.SetFPSCap(clampTo120, 120.0f);
        ctx.SetSetting("FpsSmoothing", std::to_string(smoothing));
        ctx.SaveAutoSettings();
    }

    ImGui::TextDisabled("Attach to a UI Text object.");
}

void TickUpdate(ScriptContext& ctx, float deltaTime) {
    if (!ctx.object || ctx.object->type != ObjectType::UIText) return;
    float fps = (deltaTime > 1e-6f) ? (1.0f / deltaTime) : 0.0f;
    float k = std::clamp(smoothing, 0.0f, 1.0f);
    if (smoothFps <= 0.0f) smoothFps = fps;
    smoothFps = smoothFps + (fps - smoothFps) * k;
    ctx.object->ui.label = "FPS: " + std::to_string(static_cast<int>(smoothFps + 0.5f));
}
