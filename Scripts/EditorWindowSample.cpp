// Minimal sample showing how to expose a custom editor tab from a script binary.
// Build via the engine’s “Compile Script” action. If compiling manually:
// Linux:  g++ -std=c++20 -fPIC -O2 -I../src -I../include -c EditorWindowSample.cpp -o ../Cache/ScriptBin/EditorWindowSample.o
//         g++ -shared ../Cache/ScriptBin/EditorWindowSample.o -o ../Cache/ScriptBin/EditorWindowSample.so -ldl -lpthread
// Windows: cl /nologo /std:c++20 /EHsc /MD /O2 /I ..\src /I ..\include /c EditorWindowSample.cpp /Fo ..\Cache\ScriptBin\EditorWindowSample.obj
//          link /nologo /DLL ..\Cache\ScriptBin\EditorWindowSample.obj /OUT:..\Cache\ScriptBin\EditorWindowSample.dll User32.lib Advapi32.lib

#include "ScriptRuntime.h"
#include "SceneObject.h"
#include "ThirdParty/imgui/imgui.h"
#include <string>

namespace {
bool toggle = false;
float sliderValue = 0.5f;
char note[128] = "Hello from script!";

void drawContent(ScriptContext& ctx) {
    ImGui::TextUnformatted("EditorWindowSample");
    ImGui::Separator();

    ImGui::Checkbox("Toggle", &toggle);
    ImGui::SliderFloat("Value", &sliderValue, 0.0f, 1.0f, "%.2f");
    ImGui::InputText("Note", note, sizeof(note));

    if (ImGui::Button("Log Message")) {
        ctx.AddConsoleMessage(std::string("Script tab says: ") + note);
    }

    if (ctx.object) {
        ImGui::Separator();
        ImGui::TextDisabled("Selected object: %s (id=%d)", ctx.object->name.c_str(), ctx.object->id);
        if (ImGui::Button("Nudge +Y")) {
            auto pos = ctx.object->position;
            pos.y += 0.25f;
            ctx.SetPosition(pos);
            ctx.MarkDirty();
        }
    } else {
        ImGui::TextDisabled("Select an object to enable actions");
    }
}
} // namespace

extern "C" void RenderEditorWindow(ScriptContext& ctx) {
    // Called every frame while the scripted window is open. Use ImGui freely here.
    drawContent(ctx);
}

extern "C" void ExitRenderEditorWindow(ScriptContext& ctx) {
    // Called once when the user closes the tab from View -> Scripted Windows.
    // Good place to persist settings or emit a final log.
    (void)ctx;
}
