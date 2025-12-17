// Minimal rigidbody smoke-test script for Modularity script compilation.
// Build via the engine's "Compile Script" action (wrapper is generated automatically).

#include "ScriptRuntime.h"
#include "SceneObject.h"
#include "ThirdParty/imgui/imgui.h"

namespace {
bool autoLaunch = true;
glm::vec3 launchVelocity = glm::vec3(0.0f, 6.0f, 8.0f);
glm::vec3 teleportOffset = glm::vec3(0.0f, 1.0f, 0.0f);
bool showVelocity = true;

void Launch(ScriptContext& ctx) {
    if (ctx.HasRigidbody()) {
        ctx.SetRigidbodyVelocity(launchVelocity);
    } else {
        ctx.AddConsoleMessage("RigidbodyTest: attach a Rigidbody component", ConsoleMessageType::Warning);
    }
}

void Teleport(ScriptContext& ctx) {
    if (!ctx.object) return;
    const glm::vec3 targetPos = ctx.object->position + teleportOffset;
    const glm::vec3 targetRot = ctx.object->rotation;
    if (!ctx.TeleportRigidbody(targetPos, targetRot)) {
        ctx.AddConsoleMessage("RigidbodyTest: teleport failed (needs Rigidbody)", ConsoleMessageType::Warning);
    }
}
} // namespace

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    // Persist settings automatically between sessions.
    ctx.AutoSetting("autoLaunch", autoLaunch);
    ctx.AutoSetting("launchVelocity", launchVelocity);
    ctx.AutoSetting("teleportOffset", teleportOffset);
    ctx.AutoSetting("showVelocity", showVelocity);

    ImGui::TextUnformatted("RigidbodyTest");
    ImGui::Separator();

    bool changed = false;
    changed |= ImGui::Checkbox("Launch on Begin", &autoLaunch);
    changed |= ImGui::Checkbox("Show Velocity Readback", &showVelocity);
    changed |= ImGui::DragFloat3("Launch Velocity", &launchVelocity.x, 0.25f, -50.0f, 50.0f);
    changed |= ImGui::DragFloat3("Teleport Offset", &teleportOffset.x, 0.1f, -10.0f, 10.0f);

    if (changed) {
        ctx.SaveAutoSettings();
    }

    if (ImGui::Button("Launch Now")) {
        Launch(ctx);
    }
    ImGui::SameLine();
    if (ImGui::Button("Zero Velocity")) {
        if (!ctx.SetRigidbodyVelocity(glm::vec3(0.0f))) {
            ctx.AddConsoleMessage("RigidbodyTest: zeroing velocity requires a Rigidbody", ConsoleMessageType::Warning);
        }
    }

    if (ImGui::Button("Teleport Offset")) {
        Teleport(ctx);
    }

    glm::vec3 currentVelocity;
    if (showVelocity && ctx.GetRigidbodyVelocity(currentVelocity)) {
        ImGui::Text("Velocity: (%.2f, %.2f, %.2f)", currentVelocity.x, currentVelocity.y, currentVelocity.z);
    } else if (showVelocity) {
        ImGui::TextDisabled("Velocity readback requires a Rigidbody");
    }

    if (ctx.object) {
        ImGui::TextDisabled("Attached to: %s (id=%d)", ctx.object->name.c_str(), ctx.object->id);
    }
}

void Begin(ScriptContext& ctx, float /*deltaTime*/) {
    if (autoLaunch) {
        Launch(ctx);
    }
}

void Spec(ScriptContext& ctx, float /*deltaTime*/) {
}

void TestEditor(ScriptContext& ctx, float /*deltaTime*/) {
}

void TickUpdate(ScriptContext& ctx, float /*deltaTime*/) {
}
