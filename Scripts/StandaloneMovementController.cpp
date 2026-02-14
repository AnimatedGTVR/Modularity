#include "ScriptRuntime.h"
#include "SceneObject.h"
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
    glm::vec3& capsuleTuning = g_settings.capsuleTuning;
    bool& enforceCollider = g_settings.enforceCollider;
    bool& enforceRigidbody = g_settings.enforceRigidbody;
}
extern "C" void Script_OnInspector(ScriptContext& ctx)
{
    ctx.DrawStandaloneMovementInspector(g_settings, nullptr);
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
