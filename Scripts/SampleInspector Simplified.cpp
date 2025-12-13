#include "ScriptRuntime.h"
#include "SceneObject.h"
#include "ThirdParty/imgui/imgui.h"

namespace {
    // Script state (persisted by AutoSetting binder)
    bool autoRotate = false;
    glm::vec3 spinSpeed = glm::vec3(0.0f, 45.0f, 0.0f); // deg/sec
    glm::vec3 offset = glm::vec3(0.0f, 1.0f, 0.0f);
    char targetName[128] = "MyTarget";

    // Runtime behavior
    static void ApplyAutoRotate(ScriptContext& ctx, float deltaTime) {
        if (!autoRotate || !ctx.object) return;
        ctx.SetRotation(ctx.object->rotation + spinSpeed * deltaTime);
    }
}

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    // Auto settings (loaded once, saved only when changed)
    ctx.AutoSetting("autoRotate", autoRotate);
    ctx.AutoSetting("spinSpeed",  spinSpeed);
    ctx.AutoSetting("offset",     offset);
    ctx.AutoSetting("targetName", targetName, sizeof(targetName));

    ImGui::TextUnformatted("SampleInspector");
    ImGui::Separator();

    bool changed = false;
    changed |= ImGui::Checkbox("Auto Rotate", &autoRotate);
    changed |= ImGui::DragFloat3("Spin Speed (deg/s)", &spinSpeed.x, 1.0f, -360.0f, 360.0f);
    changed |= ImGui::DragFloat3("Offset", &offset.x, 0.1f);
    changed |= ImGui::InputText("Target Name", targetName, sizeof(targetName));

    if (changed) {
        ctx.SaveAutoSettings();
    }

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
}

void Begin(ScriptContext& ctx, float /*deltaTime*/) {
}

void Spec(ScriptContext& ctx, float deltaTime) {
    ApplyAutoRotate(ctx, deltaTime);
}

void TestEditor(ScriptContext& ctx, float deltaTime) {
    ApplyAutoRotate(ctx, deltaTime);
}

void TickUpdate(ScriptContext& ctx, float deltaTime) {
    ApplyAutoRotate(ctx, deltaTime);
}
