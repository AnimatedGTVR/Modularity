// Minimal sample script demonstrating Script_OnInspector usage.
// Build via the engine’s “Compile Script” action or:
// Linux:  g++ -std=c++20 -fPIC -O0 -g -I../src -I../include -c SampleInspector.cpp -o ../Cache/ScriptBin/SampleInspector.o
//         g++ -shared ../Cache/ScriptBin/SampleInspector.o -o ../Cache/ScriptBin/SampleInspector.so -ldl -lpthread
// Windows: cl /nologo /std:c++20 /EHsc /MD /Zi /Od /I ..\src /I ..\include /c SampleInspector.cpp /Fo ..\Cache\ScriptBin\SampleInspector.obj
//          link /nologo /DLL ..\Cache\ScriptBin\SampleInspector.obj /OUT:..\Cache\ScriptBin\SampleInspector.dll User32.lib Advapi32.lib

#include "ScriptRuntime.h"
#include "SceneObject.h"
#include "ThirdParty/imgui/imgui.h"

namespace {
bool autoRotate = false;
glm::vec3 spinSpeed = glm::vec3(0.0f, 45.0f, 0.0f);
glm::vec3 offset = glm::vec3(0.0f, 1.0f, 0.0f);
char targetName[128] = "MyTarget";

void bindSettings(ScriptContext& ctx) {
    ctx.AutoSetting("autoRotate", autoRotate);
    ctx.AutoSetting("spinSpeed", spinSpeed);
    ctx.AutoSetting("offset", offset);
    ctx.AutoSetting("targetName", targetName, sizeof(targetName));
}

void applyAutoRotate(ScriptContext& ctx, float deltaTime) {
    if (!autoRotate || !ctx.object) return;
    if (ctx.HasRigidbody() && !ctx.object->rigidbody.isKinematic) {
        if (ctx.SetRigidbodyAngularVelocity(glm::radians(spinSpeed))) {
            return;
        }
    }
    ctx.SetRotation(ctx.object->rotation + spinSpeed * deltaTime);
}
} // namespace

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    bindSettings(ctx);

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
        if (SceneObject* target = ctx.ResolveObjectRef(targetName)) {
            target->position += offset;
        }
    }
}

// New lifecycle hooks supported by the compiler wrapper. These are optional stubs demonstrating usage.
void Begin(ScriptContext& ctx, float /*deltaTime*/) {
    // Initialize per-script state here.
    bindSettings(ctx);
}

void Spec(ScriptContext& ctx, float deltaTime) {
    // Special/speculative mode logic could go here.
    applyAutoRotate(ctx, deltaTime);
}

void TestEditor(ScriptContext& ctx, float deltaTime) {
    // Editor-time behavior entry point.
    applyAutoRotate(ctx, deltaTime);
}

void TickUpdate(ScriptContext& ctx, float deltaTime) {
    applyAutoRotate(ctx, deltaTime);
}
