// Minimal sample script demonstrating Script_OnInspector usage.
// Build via the engine’s “Compile Script” action or:
// Linux:  g++ -std=c++20 -fPIC -O0 -g -I../src -I../include -c SampleInspector.cpp -o ../Cache/ScriptBin/SampleInspector.o
//         g++ -shared ../Cache/ScriptBin/SampleInspector.o -o ../Cache/ScriptBin/SampleInspector.so -ldl -lpthread
// Windows: cl /nologo /std:c++20 /EHsc /MD /Zi /Od /I ..\src /I ..\include /c SampleInspector.cpp /Fo ..\Cache\ScriptBin\SampleInspector.obj
//          link /nologo /DLL ..\Cache\ScriptBin\SampleInspector.obj /OUT:..\Cache\ScriptBin\SampleInspector.dll User32.lib Advapi32.lib

#include "ScriptRuntime.h"
#include "SceneObject.h"
#include "ThirdParty/imgui/imgui.h"
#include <string>

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    static bool autoRotate = false;
    static glm::vec3 spinSpeed = glm::vec3(0.0f, 45.0f, 0.0f);
    static glm::vec3 offset = glm::vec3(0.0f, 1.0f, 0.0f);
    static char targetName[128] = "MyTarget";

    ImGui::TextUnformatted("SampleInspector");
    ImGui::Separator();

    ImGui::Checkbox("Auto Rotate", &autoRotate);
    ImGui::DragFloat3("Spin Speed (deg/s)", &spinSpeed.x, 1.0f, -360.0f, 360.0f);
    ImGui::DragFloat3("Offset", &offset.x, 0.1f);

    ImGui::InputText("Target Name", targetName, sizeof(targetName));

    if (ctx.object) {
        ImGui::TextDisabled("Attached to: %s (id=%d)", ctx.object->name.c_str(), ctx.object->id);

        if (ImGui::Button("Apply Offset To Self")) {
            ctx.SetPosition(ctx.object->position + offset);
        }
    }

    if (ImGui::Button("Nudge Target")) {
        if (SceneObject* target = ctx.FindObjectByName(targetName)) {
            target->position += offset;
        }
    }

    if (autoRotate && ctx.object) {
        ctx.SetRotation(ctx.object->rotation + spinSpeed * (1.0f / 60.0f));
    }
}
