#include "ScriptRuntime.h"
#include "SceneObject.h"
#include "ThirdParty/imgui/imgui.h"
#include <string>

namespace
{
bool toggle = false;
float sliderValue = 0.5f;
char note[128] = "Hello from script!";
void drawContent(ScriptContext& ctx)
{
    ImGui::TextUnformatted("EditorWindowSample");
    ImGui::Separator();
    ImGui::Checkbox("Toggle", &toggle);
    ImGui::SliderFloat("Value", &sliderValue, 0.0f, 1.0f, "%.2f");
    ImGui::InputText("Note", note, sizeof(note));
    if (ImGui::Button("Log Message"))
    {
        ctx.AddConsoleMessage(std::string("Script tab says: ") + note);
    }
    if (ctx.object)
    {
        ImGui::Separator();
        ImGui::TextDisabled("Selected object: %s (id=%d)", ctx.object->name.c_str(), ctx.object->id);
        if (ImGui::Button("Nudge +Y"))
        {
            auto pos = ctx.object->position;
            pos.y += 0.25f;
            ctx.SetPosition(pos);
            ctx.MarkDirty();
        }
    } else {
        ImGui::TextDisabled("Select an object to enable actions");
    }
}
}

extern "C" void RenderEditorWindow(ScriptContext& ctx)
{
    drawContent(ctx);
}

extern "C" void ExitRenderEditorWindow(ScriptContext& ctx)
{
    (void)ctx;
}



