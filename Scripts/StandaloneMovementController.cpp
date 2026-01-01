#include "ScriptRuntime.h"
#include "SceneObject.h"
#include "ThirdParty/imgui/imgui.h"
#include <unordered_map>
namespace
{
    struct ControllerState
    {
        ScriptContext::StandaloneMovementState movement; ScriptContext::StandaloneMovementDebug debug;
        bool initialized = false;
    };
    std::unordered_map<int, ControllerState> g_states;
    ScriptContext::StandaloneMovementSettings g_settings;
    ControllerState& getState(int id) {return g_states[id];}
    // aliases for readability
    glm::vec3& moveTuning = g_settings.moveTuning;
    glm::vec3& lookTuning = g_settings.lookTuning;
    glm::vec3& capsuleTuning = g_settings.capsuleTuning;
    glm::vec3& gravityTuning = g_settings.gravityTuning;
    bool& enableMouseLook = g_settings.enableMouseLook;
    bool& requireMouseButton = g_settings.requireMouseButton;
    bool& enforceCollider = g_settings.enforceCollider;
    bool& enforceRigidbody = g_settings.enforceRigidbody;
}
extern "C" void Script_OnInspector(ScriptContext& ctx)
{
    ctx.BindStandaloneMovementSettings(g_settings);
    ImGui::TextUnformatted("Standalone Movement Controller");
    ImGui::Separator();
    ImGui::DragFloat3("Walk/Run/Jump", &moveTuning.x, 0.05f, 0.0f, 25.0f, "%.2f");
    ImGui::DragFloat2("Look Sens/Clamp", &lookTuning.x, 0.01f, 0.0f, 500.0f, "%.2f");
    ImGui::DragFloat3("Height/Radius/Snap", &capsuleTuning.x, 0.02f, 0.0f, 5.0f, "%.2f");
    ImGui::DragFloat3("Gravity/Probe/MaxFall", &gravityTuning.x, 0.05f, -50.0f, 50.0f, "%.2f");
    ImGui::Checkbox("Enable Mouse Look", &enableMouseLook);
    ImGui::Checkbox("Hold RMB to Look", &requireMouseButton);
    ImGui::Checkbox("Force Collider", &enforceCollider);
    ImGui::Checkbox("Force Rigidbody", &enforceRigidbody);
}

void Begin(ScriptContext& ctx, float)
{
    if (!ctx.object) return; ControllerState& s = getState(ctx.object->id);
    if (!s.initialized)
    {
        s.movement.pitch = ctx.object->rotation.x;
        s.movement.yaw = ctx.object->rotation.y;
        s.initialized = true;
    }
    if (enforceCollider) ctx.EnsureCapsuleCollider(capsuleTuning.x, capsuleTuning.y);
    if (enforceRigidbody) ctx.EnsureRigidbody(true, false);
}

void TickUpdate(ScriptContext& ctx, float dt)
{
    if (!ctx.object) return; ControllerState& s = getState(ctx.object->id); ctx.TickStandaloneMovement(s.movement, g_settings, dt, nullptr);
}
