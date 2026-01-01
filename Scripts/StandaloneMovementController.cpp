#include "ScriptRuntime.h"
#include "SceneObject.h"
#include "ThirdParty/imgui/imgui.h"
#include <unordered_map>

namespace {
struct ControllerState {
    float pitch = 0.0f;
    float yaw = 0.0f;
    float verticalVelocity = 0.0f;
    glm::vec3 debugVelocity = glm::vec3(0.0f);
    bool debugGrounded = false;
    bool initialized = false;
};

std::unordered_map<int, ControllerState> g_states;

glm::vec3 moveTuning = glm::vec3(4.5f, 7.5f, 6.5f);      // walk speed, run speed, jump
glm::vec3 lookTuning = glm::vec3(0.12f, 200.0f, 0.0f);   // sensitivity, max delta clamp, reserved
glm::vec3 capsuleTuning = glm::vec3(1.8f, 0.4f, 0.2f);   // height, radius, ground snap
glm::vec3 gravityTuning = glm::vec3(-9.81f, 0.4f, 30.0f); // gravity, probe extra, max fall speed
bool enableMouseLook = true;
bool requireMouseButton = false;
bool enforceCollider = true;
bool enforceRigidbody = true;
bool showDebug = false;

ControllerState& getState(int id) {
    return g_states[id];
}

void bindSettings(ScriptContext& ctx) {
    ctx.AutoSetting("moveTuning", moveTuning);
    ctx.AutoSetting("lookTuning", lookTuning);
    ctx.AutoSetting("capsuleTuning", capsuleTuning);
    ctx.AutoSetting("gravityTuning", gravityTuning);
    ctx.AutoSetting("enableMouseLook", enableMouseLook);
    ctx.AutoSetting("requireMouseButton", requireMouseButton);
    ctx.AutoSetting("enforceCollider", enforceCollider);
    ctx.AutoSetting("enforceRigidbody", enforceRigidbody);
    ctx.AutoSetting("showDebug", showDebug);
}
} // namespace

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    bindSettings(ctx);

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
    ImGui::Checkbox("Show Debug", &showDebug);

    if (showDebug && ctx.object) {
        auto it = g_states.find(ctx.object->id);
        if (it != g_states.end()) {
            const ControllerState& state = it->second;
            ImGui::Separator();
            ImGui::Text("Move (%.2f, %.2f, %.2f)", state.debugVelocity.x, state.debugVelocity.y, state.debugVelocity.z);
            ImGui::Text("Grounded: %s", state.debugGrounded ? "yes" : "no");
        }
    }
}

void Begin(ScriptContext& ctx, float /*deltaTime*/) {
    if (!ctx.object) return;
    bindSettings(ctx);
    ControllerState& state = getState(ctx.object->id);
    if (!state.initialized) {
        state.pitch = ctx.object->rotation.x;
        state.yaw = ctx.object->rotation.y;
        state.verticalVelocity = 0.0f;
        state.initialized = true;
    }
    if (enforceCollider) ctx.EnsureCapsuleCollider(capsuleTuning.x, capsuleTuning.y);
    if (enforceRigidbody) ctx.EnsureRigidbody(true, false);
}

void TickUpdate(ScriptContext& ctx, float deltaTime) {
    if (!ctx.object) return;

    ControllerState& state = getState(ctx.object->id);
    if (enforceCollider) ctx.EnsureCapsuleCollider(capsuleTuning.x, capsuleTuning.y);
    if (enforceRigidbody) ctx.EnsureRigidbody(true, false);

    const float walkSpeed = moveTuning.x;
    const float runSpeed = moveTuning.y;
    const float jumpStrength = moveTuning.z;
    const float lookSensitivity = lookTuning.x;
    const float maxMouseDelta = glm::max(5.0f, lookTuning.y);
    const float height = capsuleTuning.x;
    const float radius = capsuleTuning.y;
    const float groundSnap = capsuleTuning.z;
    const float gravity = gravityTuning.x;
    const float probeExtra = gravityTuning.y;
    const float maxFall = glm::max(1.0f, gravityTuning.z);

    if (enableMouseLook) {
        bool allowLook = !requireMouseButton || ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (allowLook) {
            ImGuiIO& io = ImGui::GetIO();
            glm::vec2 delta(io.MouseDelta.x, io.MouseDelta.y);
            float len = glm::length(delta);
            if (len > maxMouseDelta) {
                delta *= (maxMouseDelta / len);
            }
            state.yaw -= delta.x * 50.0f * lookSensitivity * deltaTime;
            state.pitch -= delta.y * 50.0f * lookSensitivity * deltaTime;
            state.pitch = std::clamp(state.pitch, -89.0f, 89.0f);
        }
    }

    glm::vec3 planarForward(0.0f);
    glm::vec3 planarRight(0.0f);
    ctx.GetPlanarYawPitchVectors(state.pitch, state.yaw, planarForward, planarRight);

    glm::vec3 move(0.0f);
    if (ImGui::IsKeyDown(ImGuiKey_W)) move += planarForward;
    if (ImGui::IsKeyDown(ImGuiKey_S)) move -= planarForward;
    if (ImGui::IsKeyDown(ImGuiKey_D)) move += planarRight;
    if (ImGui::IsKeyDown(ImGuiKey_A)) move -= planarRight;
    if (glm::length(move) > 0.001f) move = glm::normalize(move);

    bool sprint = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
    float targetSpeed = sprint ? runSpeed : walkSpeed;
    glm::vec3 velocity = move * targetSpeed;

    float capsuleHalf = std::max(0.1f, height * 0.5f);
    glm::vec3 physVel;
    bool havePhysVel = ctx.GetRigidbodyVelocity(physVel);
    if (havePhysVel) state.verticalVelocity = physVel.y;

    glm::vec3 hitPos(0.0f);
    glm::vec3 hitNormal(0.0f, 1.0f, 0.0f);
    float hitDist = 0.0f;
    float probeDist = capsuleHalf + probeExtra;
    glm::vec3 rayStart = ctx.object->position + glm::vec3(0.0f, 0.1f, 0.0f);
    bool hitGround = ctx.RaycastClosest(rayStart, glm::vec3(0.0f, -1.0f, 0.0f), probeDist,
                                        &hitPos, &hitNormal, &hitDist);
    bool grounded = hitGround && hitNormal.y > 0.25f &&
                    hitDist <= capsuleHalf + groundSnap &&
                    state.verticalVelocity <= 0.35f;
    if (!hitGround) {
        grounded = ctx.object->position.y <= capsuleHalf + 0.12f && state.verticalVelocity <= 0.35f;
    }

    if (grounded) {
        state.verticalVelocity = 0.0f;
        if (!havePhysVel) {
            if (hitGround) {
                ctx.object->position.y = std::max(ctx.object->position.y, hitPos.y + capsuleHalf);
            } else {
                ctx.object->position.y = capsuleHalf;
            }
        }
        if (ImGui::IsKeyDown(ImGuiKey_Space)) {
            state.verticalVelocity = jumpStrength;
        }
    } else {
        state.verticalVelocity += gravity * deltaTime;
    }

    state.verticalVelocity = std::clamp(state.verticalVelocity, -maxFall, maxFall);
    velocity.y = state.verticalVelocity;

    glm::vec3 rotation(state.pitch, state.yaw, 0.0f);
    ctx.object->rotation = rotation;
    ctx.SetRigidbodyRotation(rotation);

    if (!ctx.SetRigidbodyVelocity(velocity)) {
        ctx.object->position += velocity * deltaTime;
    }

    if (showDebug) {
        state.debugVelocity = velocity;
        state.debugGrounded = grounded;
    }
}

void Spec(ScriptContext& /*ctx*/, float /*deltaTime*/) {}
void TestEditor(ScriptContext& /*ctx*/, float /*deltaTime*/) {}
