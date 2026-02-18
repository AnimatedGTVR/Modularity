#include "ScriptRuntime.h"
#include "SceneObject.h"

#include <unordered_map>

namespace
{
    struct ControllerState
    {
        ScriptContext::StandaloneMovementState movement;
        ScriptContext::StandaloneMovementDebug debug;
        bool initialized = false;
    };

    std::unordered_map<int, ControllerState> g_states;
    ScriptContext::StandaloneMovementSettings g_settings = {
        glm::vec3(4.5f, 7.5f, 6.5f),      // Walk / Run / Jump
        glm::vec3(0.12f, 200.0f, 0.0f),   // Look sensitivity / max mouse delta
        glm::vec3(1.8f, 0.4f, 0.2f),      // Height / Radius / Ground snap
        glm::vec3(-9.81f, 0.4f, 30.0f),   // Gravity / Probe extra / Max fall
        glm::vec3(24.0f, 8.0f, 16.0f),    // Ground accel / Air accel / Braking
        glm::vec3(0.2f, 40.0f, 1.0f),     // Min control / Slide gravity / Platform carry
        true,                             // Enable mouse look
        false,                            // Require mouse button for look
        true,                             // Ensure collider
        true                              // Ensure rigidbody
    };
    bool g_showDebug = false;

    ControllerState& getState(int id) { return g_states[id]; }

    void resetStateFromObject(ControllerState& state, const SceneObject& object)
    {
        state.movement.pitch = object.rotation.x;
        state.movement.yaw = object.rotation.y;
        state.movement.verticalVelocity = 0.0f;
        state.movement.localVelocity = glm::vec2(0.0f);
        state.movement.slideVelocity = glm::vec3(0.0f);
        state.movement.lastGroundHitPos = glm::vec3(0.0f);
        state.movement.hasGroundSample = false;
        state.debug = ScriptContext::StandaloneMovementDebug{};
        state.initialized = true;
    }
}

extern "C" void Script_OnInspector(ScriptContext& ctx)
{
    ctx.DrawStandaloneMovementInspector(g_settings, &g_showDebug);
}

void Begin(ScriptContext& ctx, float /*deltaTime*/)
{
    if (!ctx.object) return;

    ControllerState& state = getState(ctx.object->id);
    resetStateFromObject(state, *ctx.object);

    if (g_settings.enforceCollider) {
        ctx.EnsureCapsuleCollider(g_settings.capsuleTuning.x, g_settings.capsuleTuning.y);
    }
    if (g_settings.enforceRigidbody) {
        ctx.EnsureRigidbody(true, false);
    }
}

void TickUpdate(ScriptContext& ctx, float dt)
{
    if (!ctx.object || dt <= 0.0f) return;

    ControllerState& state = getState(ctx.object->id);
    if (!state.initialized) {
        resetStateFromObject(state, *ctx.object);
    }

    ctx.TickStandaloneMovement(state.movement, g_settings, dt, &state.debug);
}
