#include "ScriptRuntime.h"
#include "SceneObject.h"
#include "ThirdParty/imgui/imgui.h"
#include <array>
#include <string>
#include <unordered_map>

namespace {
float walkSpeed = 4.0f;
float runSpeed = 7.0f;
float acceleration = 18.0f;
float drag = 8.0f;
float animationFps = 10.0f;
float runAnimationFps = 14.0f;
float movementThreshold = 0.15f;
bool useRigidbody2D = true;
bool useSpriteAnimation = true;

enum class FacingDirection : int {
    Down = 0,
    Up = 1,
    Right = 2,
    Left = 3
};

struct ControllerState {
    FacingDirection facing = FacingDirection::Down;
    float animationTime = 0.0f;
    bool warnedMissingRb = false;
    bool warnedMissingSprite = false;
};

std::unordered_map<int, ControllerState> controllerStates;
std::array<int, 4> idleClips = { 0, 0, 0, 0 };
std::array<std::array<int, 4>, 4> walkClips = {{
    {{ 0, 0, 0, 0 }},
    {{ 0, 0, 0, 0 }},
    {{ 0, 0, 0, 0 }},
    {{ 0, 0, 0, 0 }}
}};
constexpr const char* kDirectionLabels[4] = { "Down", "Up", "Right", "Left" };

int loadIntSetting(ScriptContext& ctx, const std::string& key, int fallback) {
    const std::string raw = ctx.GetSetting(key, "");
    if (raw.empty()) return fallback;
    try {
        return std::stoi(raw);
    } catch (...) {
        return fallback;
    }
}

void saveIntSettingIfChanged(ScriptContext& ctx, const std::string& key, int value) {
    const std::string desired = std::to_string(value);
    if (ctx.GetSetting(key, "") != desired) {
        ctx.SetSetting(key, desired);
    }
}

void bindSettings(ScriptContext& ctx) {
    ctx.AutoSetting("walkSpeed", walkSpeed);
    ctx.AutoSetting("runSpeed", runSpeed);
    ctx.AutoSetting("acceleration", acceleration);
    ctx.AutoSetting("drag", drag);
    ctx.AutoSetting("animationFps", animationFps);
    ctx.AutoSetting("runAnimationFps", runAnimationFps);
    ctx.AutoSetting("movementThreshold", movementThreshold);
    ctx.AutoSetting("useRigidbody2D", useRigidbody2D);
    ctx.AutoSetting("useSpriteAnimation", useSpriteAnimation);

    for (int dir = 0; dir < 4; ++dir) {
        idleClips[dir] = loadIntSetting(ctx, "idle" + std::to_string(dir), idleClips[dir]);
        for (int frame = 0; frame < 4; ++frame) {
            walkClips[dir][frame] = loadIntSetting(ctx,
                                                   "walk" + std::to_string(dir) + "_" + std::to_string(frame),
                                                   walkClips[dir][frame]);
        }
    }
}

bool isValidClip(const ScriptContext& ctx, int clip) {
    return clip >= 0 && clip < ctx.GetSpriteClipCount();
}

int facingIndex(FacingDirection dir) {
    return static_cast<int>(dir);
}

FacingDirection resolveFacing(const glm::vec2& motion, FacingDirection fallback) {
    if (glm::dot(motion, motion) <= 1e-6f) {
        return fallback;
    }
    if (std::abs(motion.x) > std::abs(motion.y)) {
        return (motion.x >= 0.0f) ? FacingDirection::Right : FacingDirection::Left;
    }
    return (motion.y >= 0.0f) ? FacingDirection::Up : FacingDirection::Down;
}

int selectWalkClip(const ScriptContext& ctx, FacingDirection dir, int frameIndex) {
    const std::array<int, 4>& clips = walkClips[facingIndex(dir)];
    int candidate = clips[std::clamp(frameIndex, 0, 3)];
    if (isValidClip(ctx, candidate)) return candidate;
    for (int clip : clips) {
        if (isValidClip(ctx, clip)) return clip;
    }
    candidate = idleClips[facingIndex(dir)];
    return isValidClip(ctx, candidate) ? candidate : -1;
}

void applySpriteAnimation(ScriptContext& ctx, ControllerState& state, const glm::vec2& motion, float dt, bool isRunning) {
    if (!useSpriteAnimation || !ctx.object) return;

    const int clipCount = ctx.GetSpriteClipCount();
    if (clipCount <= 0) {
        if (!state.warnedMissingSprite) {
            ctx.AddConsoleMessage("TopDownMovement2D: sprite animation needs Sprite Sheet clips on this object.",
                                  ConsoleMessageType::Warning);
            state.warnedMissingSprite = true;
        }
        return;
    }

    state.warnedMissingSprite = false;
    const float speed = glm::length(motion);
    if (speed > movementThreshold) {
        state.facing = resolveFacing(motion, state.facing);
        state.animationTime += dt;
        const float activeAnimationFps = std::max(1.0f, isRunning ? runAnimationFps : animationFps);
        const int walkFrame = static_cast<int>(state.animationTime * activeAnimationFps) % 4;
        const int clip = selectWalkClip(ctx, state.facing, walkFrame);
        if (clip >= 0) {
            ctx.SetSpriteClipIndex(clip);
        }
        return;
    }

    state.animationTime = 0.0f;
    const int idleClip = idleClips[facingIndex(state.facing)];
    if (isValidClip(ctx, idleClip)) {
        ctx.SetSpriteClipIndex(idleClip);
    }
}

bool drawClipSelector(ScriptContext& ctx, const char* label, int& clipIndex) {
    const int clipCount = ctx.GetSpriteClipCount();
    bool changed = false;
    std::string preview = isValidClip(ctx, clipIndex)
        ? ctx.GetSpriteClipNameAt(clipIndex)
        : std::string("<None>");

    if (ImGui::BeginCombo(label, preview.c_str())) {
        const bool noneSelected = (clipIndex < 0 || clipIndex >= clipCount);
        if (ImGui::Selectable("<None>", noneSelected)) {
            clipIndex = -1;
            changed = true;
        }
        if (noneSelected) ImGui::SetItemDefaultFocus();

        for (int i = 0; i < clipCount; ++i) {
            const std::string clipName = ctx.GetSpriteClipNameAt(i);
            const bool selected = (clipIndex == i);
            if (ImGui::Selectable(clipName.c_str(), selected)) {
                clipIndex = i;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    return changed;
}
} // namespace

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    bindSettings(ctx);

    ImGui::TextUnformatted("Top Down Movement 2D");
    ImGui::Separator();
    ImGui::DragFloat("Walk Speed", &walkSpeed, 0.1f, 0.0f, 50.0f, "%.2f");
    ImGui::DragFloat("Run Speed", &runSpeed, 0.1f, 0.0f, 80.0f, "%.2f");
    ImGui::DragFloat("Acceleration", &acceleration, 0.1f, 0.0f, 200.0f, "%.2f");
    ImGui::DragFloat("Drag", &drag, 0.1f, 0.0f, 200.0f, "%.2f");
    ImGui::DragFloat("Animation FPS", &animationFps, 0.1f, 1.0f, 30.0f, "%.1f");
    ImGui::DragFloat("Run Animation FPS", &runAnimationFps, 0.1f, 1.0f, 60.0f, "%.1f");
    ImGui::DragFloat("Move Threshold", &movementThreshold, 0.01f, 0.01f, 10.0f, "%.2f");
    ImGui::Checkbox("Use Rigidbody2D", &useRigidbody2D);
    ImGui::Checkbox("Use Sprite Animation", &useSpriteAnimation);

    bool clipSettingsChanged = false;
    if (useSpriteAnimation) {
        const int clipCount = ctx.GetSpriteClipCount();
        ImGui::Separator();
        ImGui::TextUnformatted("Sprite Clips");
        if (clipCount <= 0) {
            ImGui::TextDisabled("Enable Sprite Sheet clips on this object to assign animation frames.");
        } else {
            ImGui::TextDisabled("%d sprite clips available.", clipCount);
            for (int dir = 0; dir < 4; ++dir) {
                if (ImGui::CollapsingHeader(kDirectionLabels[dir], ImGuiTreeNodeFlags_DefaultOpen)) {
                    clipSettingsChanged |= drawClipSelector(ctx,
                                                            ("Idle##" + std::string(kDirectionLabels[dir])).c_str(),
                                                            idleClips[dir]);
                    for (int frame = 0; frame < 4; ++frame) {
                        clipSettingsChanged |= drawClipSelector(ctx,
                                                                ("Walk " + std::to_string(frame + 1) + "##" +
                                                                 std::string(kDirectionLabels[dir])).c_str(),
                                                                walkClips[dir][frame]);
                    }
                }
            }
        }
    }

    ctx.SaveAutoSettings();
    if (clipSettingsChanged) {
        for (int dir = 0; dir < 4; ++dir) {
            saveIntSettingIfChanged(ctx, "idle" + std::to_string(dir), idleClips[dir]);
            for (int frame = 0; frame < 4; ++frame) {
                saveIntSettingIfChanged(ctx,
                                        "walk" + std::to_string(dir) + "_" + std::to_string(frame),
                                        walkClips[dir][frame]);
            }
        }
    }
}

void TickUpdate(ScriptContext& ctx, float dt) {
    if (!ctx.object || dt <= 0.0f) return;
    bindSettings(ctx);

    ControllerState& state = controllerStates[ctx.object->id];

    glm::vec2 input(0.0f);
    if (ImGui::IsKeyDown(ImGuiKey_W)) input.y += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_S)) input.y -= 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_D)) input.x += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_A)) input.x -= 1.0f;
    if (glm::length(input) > 1e-3f) input = glm::normalize(input);

    const bool isRunning = ctx.IsSprintDown();
    float speed = isRunning ? runSpeed : walkSpeed;
    glm::vec2 targetVel = input * speed;
    glm::vec2 actualVelocity = targetVel;

    if (useRigidbody2D) {
        if (!ctx.HasRigidbody2D()) {
            if (!state.warnedMissingRb) {
                ctx.AddConsoleMessage("TopDownMovement2D: add Rigidbody2D to use velocity-based motion.", ConsoleMessageType::Warning);
                state.warnedMissingRb = true;
            }
            applySpriteAnimation(ctx, state, targetVel, dt, isRunning);
            return;
        }
        state.warnedMissingRb = false;
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
        actualVelocity = vel;
    } else {
        glm::vec2 pos = ctx.object->ui.position;
        pos += targetVel * dt;
        ctx.SetPosition2D(pos);
    }

    applySpriteAnimation(ctx, state, actualVelocity, dt, isRunning);
}
