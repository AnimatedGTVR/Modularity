#include "ScriptRuntime.h"
#include "SceneObject.h"
#include "ThirdParty/imgui/imgui.h"
#include <string>
#include <algorithm>
namespace
{
    bool clampTo120 = false;
    float smoothFps = 0.0f;
    float smoothing = 0.15f;
}
extern "C" void Script_OnInspector(ScriptContext& ctx)
{
    ctx.AutoSetting("ClampFPS120", clampTo120);
    ctx.AutoSetting("FpsSmoothing", smoothing);
    ImGui::TextUnformatted("FPS Display");
    ImGui::Separator();
    ImGui::Checkbox("Clamp FPS to 120", &clampTo120);
    ImGui::DragFloat("Smoothing", &smoothing, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::TextDisabled("Attach to a UI Text object.");
    ctx.SetFPSCap(clampTo120, 120.0f);
}

void TickUpdate(ScriptContext& ctx, float deltaTime)
{
    if (!ctx.object || ctx.object->type != ObjectType::UIText)
    {
        return;
    }

    float fps = (deltaTime > 1e-6f) ? (1.0f / deltaTime) : 0.0f;
    float k = std::clamp(smoothing, 0.0f, 1.0f);

    if (smoothFps <= 0.0f)
    {
        smoothFps = fps;
    }

    smoothFps = smoothFps + (fps - smoothFps) * k;
    ctx.object->ui.label = "FPS: " + std::to_string(static_cast<int>(smoothFps + 0.5f));
}
