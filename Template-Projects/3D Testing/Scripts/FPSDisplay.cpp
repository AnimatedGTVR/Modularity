#include "ModuCPP"
#include "ThirdParty/imgui/imgui.h"

namespace
{
    bool clampTo120 = false;
}

extern "C" void Script_OnInspector(ScriptContext& ctx)
{
    ctx.AutoSetting("ClampFPS120", clampTo120);
    ImGui::TextUnformatted("FPS Display");
    ImGui::Separator();
    ImGui::Checkbox("Clamp FPS to 120", &clampTo120);
    ctx.SetFPSCap(clampTo120, 120.0f);
}

void TickUpdate(ScriptContext& ctx, float /*deltaTime*/)
{
    MODU_SCRIPT(ctx);
    ctx.SetFPSCap(clampTo120, 120.0f);
    obj.UILabel = "FPS: " + ModuCPP::IntR(ModuCPP::ModuEngine.FPS);
}
