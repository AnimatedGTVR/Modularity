#include "ScriptRuntime.h"
#include "SceneObject.h"
#include "ThirdParty/imgui/imgui.h"

namespace {
float walkSpeed = 4.0f;
float runSpeed = 7.0f;
float acceleration = 18.0f;
float drag = 8.0f;
bool useRigidbody2D = true;
bool warnedMissingRb = false;
} // namespace

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    ctx.AutoSetting("walkSpeed", walkSpeed);
    ctx.AutoSetting("runSpeed", runSpeed);
    ctx.AutoSetting("acceleration", acceleration);
    ctx.AutoSetting("drag", drag);
    ctx.AutoSetting("useRigidbody2D", useRigidbody2D);

    ImGui::TextUnformatted("Top Down Movement 2D");
    ImGui::Separator();
    ImGui::DragFloat("Walk Speed", &walkSpeed, 0.1f, 0.0f, 50.0f, "%.2f");
    ImGui::DragFloat("Run Speed", &runSpeed, 0.1f, 0.0f, 80.0f, "%.2f");
    ImGui::DragFloat("Acceleration", &acceleration, 0.1f, 0.0f, 200.0f, "%.2f");
    ImGui::DragFloat("Drag", &drag, 0.1f, 0.0f, 200.0f, "%.2f");
    ImGui::Checkbox("Use Rigidbody2D", &useRigidbody2D);
}

void TickUpdate(ScriptContext& ctx, float dt) {
    if (!ctx.object || dt <= 0.0f) return;

    glm::vec2 input(0.0f);
    if (ImGui::IsKeyDown(ImGuiKey_W)) input.y += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_S)) input.y -= 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_D)) input.x += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_A)) input.x -= 1.0f;
    if (glm::length(input) > 1e-3f) input = glm::normalize(input);

    float speed = ctx.IsSprintDown() ? runSpeed : walkSpeed;
    glm::vec2 targetVel = input * speed;

    if (useRigidbody2D) {
        if (!ctx.HasRigidbody2D()) {
            if (!warnedMissingRb) {
                ctx.AddConsoleMessage("TopDownMovement2D: add Rigidbody2D to use velocity-based motion.", ConsoleMessageType::Warning);
                warnedMissingRb = true;
            }
            return;
        }
        glm::vec2 vel(0.0f);
        ctx.GetRigidbody2DVelocity(vel);
        if (acceleration <= 0.0f) {
            vel = targetVel;
        } else {
            glm::vec2 dv = targetVel - vel;
            float maxDelta = acceleration * dt;
            float len = glm::length(dv);
            if (len > maxDelta && len > 1e-4f) {
                dv *= (maxDelta / len);
            }
            vel += dv;
        }
        if (glm::length(input) < 1e-3f && drag > 0.0f) {
            float damp = std::max(0.0f, 1.0f - drag * dt);
            vel *= damp;
        }
        ctx.SetRigidbody2DVelocity(vel);
    } else {
        glm::vec2 pos = ctx.object->ui.position;
        pos += targetVel * dt;
        ctx.SetPosition2D(pos);
    }
}
