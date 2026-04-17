#pragma once

#include "ModuCPPScriptApi.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>

namespace ModuCPP {

namespace detail {
inline bool isAudioPath(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= path.size()) return false;
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == "wav" || ext == "mp3" || ext == "ogg" || ext == "flac" ||
           ext == "aac" || ext == "m4a";
}

inline bool isValidClip(const ScriptContext& ctx, int clipIndex) {
    return clipIndex >= 0 && clipIndex < ctx.GetSpriteClipCount();
}
} // namespace detail

inline float GetProjectGravityScale() {
    if (ScriptContext* scriptCtx = ctxPtr()) {
        return scriptCtx->GetProjectGravityScale();
    }
    return 1.0f;
}

inline void SetProjectGravityScale(float scale) {
    if (ScriptContext* scriptCtx = ctxPtr()) {
        scriptCtx->SetProjectGravityScale(scale);
    }
}

inline bool EditFloat(const char* label, float& value, float speed = 0.1f,
                      float minValue = 0.0f, float maxValue = 0.0f,
                      const char* format = "%.2f", const char* keyOverride = nullptr) {
    ScriptContext& scriptCtx = ctx();
    const std::string key = keyOverride ? keyOverride : detail::settingKeyFromLabel(label);
    scriptCtx.AutoSetting(key, value);
    const bool changed = ImGui::DragFloat(label, &value, speed, minValue, maxValue, format);
    if (changed) {
        scriptCtx.SaveAutoSettings();
    }
    return changed;
}

inline bool EditVec3(const char* label, vec3& value, float speed = 0.1f,
                     float minValue = 0.0f, float maxValue = 0.0f,
                     const char* format = "%.2f", const char* keyOverride = nullptr) {
    ScriptContext& scriptCtx = ctx();
    const std::string key = keyOverride ? keyOverride : detail::settingKeyFromLabel(label);
    scriptCtx.AutoSetting(key, value);
    const bool changed = ImGui::DragFloat3(label, &value.x, speed, minValue, maxValue, format);
    if (changed) {
        scriptCtx.SaveAutoSettings();
    }
    return changed;
}

inline bool EditBool(const char* label, bool& value, const char* keyOverride = nullptr) {
    ScriptContext& scriptCtx = ctx();
    const std::string key = keyOverride ? keyOverride : detail::settingKeyFromLabel(label);
    scriptCtx.AutoSetting(key, value);
    const bool changed = ImGui::Checkbox(label, &value);
    if (changed) {
        scriptCtx.SaveAutoSettings();
    }
    return changed;
}

inline bool EditInt(const char* label, int& value, int step = 1,
                    int stepFast = 100, const char* keyOverride = nullptr) {
    ScriptContext& scriptCtx = ctx();
    const std::string key = keyOverride ? keyOverride : detail::settingKeyFromLabel(label);
    scriptCtx.AutoSetting(key, value);
    const bool changed = ImGui::InputInt(label, &value, step, stepFast);
    if (changed) {
        scriptCtx.SaveAutoSettings();
    }
    return changed;
}

inline bool EditString(const char* label, std::string& value,
                       size_t capacity = 512, const char* keyOverride = nullptr,
                       ImGuiInputTextFlags flags = 0,
                       bool multiline = false,
                       float multilineHeight = 90.0f) {
    ScriptContext& scriptCtx = ctx();
    const std::string key = keyOverride ? keyOverride : detail::settingKeyFromLabel(label);
    scriptCtx.AutoSetting(key, value);

    std::vector<char> buffer(capacity + 1, '\0');
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());

    bool changed = false;
    if (multiline) {
        changed = ImGui::InputTextMultiline(label, buffer.data(), buffer.size(),
                                            ImVec2(-FLT_MIN, multilineHeight), flags);
    } else {
        changed = ImGui::InputText(label, buffer.data(), buffer.size(), flags);
    }

    if (changed) {
        value = buffer.data();
        scriptCtx.SaveAutoSettings();
    }

    return changed;
}

inline bool hasRigidbody2D(ScriptContext& ctx) {
    return ctx.HasRigidbody2D();
}

inline bool hasRigidbody2D() {
    if (ScriptContext* scriptCtx = ctxPtr()) return hasRigidbody2D(*scriptCtx);
    return false;
}

inline vec2 getRigidbody2DVelocity(ScriptContext& ctx) {
    vec2 velocity(0.0f);
    ctx.GetRigidbody2DVelocity(velocity);
    return velocity;
}

inline vec2 getRigidbody2DVelocity() {
    if (ScriptContext* scriptCtx = ctxPtr()) return getRigidbody2DVelocity(*scriptCtx);
    return vec2(0.0f);
}

inline bool setRigidbody2DVelocity(ScriptContext& ctx, const vec2& velocity) {
    return ctx.SetRigidbody2DVelocity(velocity);
}

inline bool setRigidbody2DVelocity(const vec2& velocity) {
    if (ScriptContext* scriptCtx = ctxPtr()) return setRigidbody2DVelocity(*scriptCtx, velocity);
    return false;
}

inline vec2 moveTowards(const vec2& current, const vec2& target, float maxDelta) {
    if (maxDelta <= 0.0f) return current;
    vec2 delta = target - current;
    const float len = glm::length(delta);
    if (len <= maxDelta || len <= 1e-6f) {
        return target;
    }
    return current + (delta / len) * maxDelta;
}

inline bool TryMoveRigidbody2D(ScriptContext& ctx,
                               const vec2& targetVelocity, float acceleration, float drag, float dt,
                               vec2* outVelocity = nullptr) {
    if (!hasRigidbody2D(ctx)) return false;

    vec2 velocity = getRigidbody2DVelocity(ctx);
    if (acceleration <= 0.0f) {
        velocity = targetVelocity;
    } else {
        velocity = moveTowards(velocity, targetVelocity, acceleration * dt);
    }

    if (glm::length(targetVelocity) < 1e-4f && drag > 0.0f) {
        const float damp = std::max(0.0f, 1.0f - drag * dt);
        velocity *= damp;
    }

    setRigidbody2DVelocity(ctx, velocity);
    if (outVelocity) {
        *outVelocity = velocity;
    }
    return true;
}

inline bool TryMoveRigidbody2D(ScriptContext& ctx,
                               const vec2& targetVelocity, float acceleration, float drag, float dt,
                               vec2& outVelocity) {
    return TryMoveRigidbody2D(ctx, targetVelocity, acceleration, drag, dt, &outVelocity);
}

inline bool TryMoveRigidbody2D(const vec2& targetVelocity, float acceleration, float drag, float dt,
                               vec2* outVelocity = nullptr) {
    if (ScriptContext* scriptCtx = ctxPtr()) {
        return TryMoveRigidbody2D(*scriptCtx, targetVelocity, acceleration, drag, dt, outVelocity);
    }
    return false;
}

inline bool TryMoveRigidbody2D(const vec2& targetVelocity, float acceleration, float drag, float dt,
                               vec2& outVelocity) {
    if (ScriptContext* scriptCtx = ctxPtr()) {
        return TryMoveRigidbody2D(*scriptCtx, targetVelocity, acceleration, drag, dt, outVelocity);
    }
    return false;
}

inline void moveRigidbody2D(ScriptContext& ctx, const vec2& targetVelocity,
                            float acceleration, float drag, float dt) {
    (void)TryMoveRigidbody2D(ctx, targetVelocity, acceleration, drag, dt, nullptr);
}

inline void moveRigidbody2D(const vec2& targetVelocity, float acceleration, float drag, float dt) {
    if (ScriptContext* scriptCtx = ctxPtr()) {
        moveRigidbody2D(*scriptCtx, targetVelocity, acceleration, drag, dt);
    }
}

inline void movePosition2D(ScriptContext& ctx, const vec2& delta) {
    if (!ctx.object) return;
    vec2 pos = ctx.object->ui.position;
    pos += delta;
    ctx.SetPosition2D(pos);
}

inline void movePosition2D(const vec2& delta) {
    if (ScriptContext* scriptCtx = ctxPtr()) movePosition2D(*scriptCtx, delta);
}

inline bool warnOnce(ScriptContext& ctx, bool& alreadyWarned, const std::string& message,
                     ConsoleMessageType type = ConsoleMessageType::Warning) {
    if (alreadyWarned) return false;
    ctx.AddConsoleMessage(message, type);
    alreadyWarned = true;
    return true;
}

inline bool warnOnce(bool& alreadyWarned, const std::string& message,
                     ConsoleMessageType type = ConsoleMessageType::Warning) {
    if (ScriptContext* scriptCtx = ctxPtr()) return warnOnce(*scriptCtx, alreadyWarned, message, type);
    return false;
}

inline bool warnMissingComponentOnce(ScriptContext& ctx,
                                     bool& alreadyWarned,
                                     const char* scriptName,
                                     const char* componentName) {
    return warnOnce(ctx, alreadyWarned,
                    std::string(scriptName ? scriptName : "Script") +
                    ": missing " +
                    (componentName ? componentName : "required component") +
                    ".");
}

inline bool warnMissingComponentOnce(bool& alreadyWarned,
                                     const char* scriptName,
                                     const char* componentName) {
    if (ScriptContext* scriptCtx = ctxPtr()) {
        return warnMissingComponentOnce(*scriptCtx, alreadyWarned, scriptName, componentName);
    }
    return false;
}

inline bool hasAudioSource(ScriptContext& ctx) {
    return ctx.HasAudioSource();
}

inline bool hasAudioSource() {
    if (ScriptContext* scriptCtx = ctxPtr()) return hasAudioSource(*scriptCtx);
    return false;
}

inline bool playSound(ScriptContext& ctx, const std::string& clipPath = "", float volumeScale = 1.0f) {
    return ctx.PlayAudioOneShot(clipPath, volumeScale);
}

inline bool playSound(const std::string& clipPath = "", float volumeScale = 1.0f) {
    if (ScriptContext* scriptCtx = ctxPtr()) return playSound(*scriptCtx, clipPath, volumeScale);
    return false;
}

struct AudioFacade {
    bool HasSource() const { return hasAudioSource(); }
    bool PlayOneShot(const std::string& clipPath = "", float volumeScale = 1.0f) const {
        return playSound(clipPath, volumeScale);
    }
    bool Play() const {
        if (ScriptContext* scriptCtx = ctxPtr()) {
            return scriptCtx->PlayAudio();
        }
        return false;
    }
    bool Stop() const {
        if (ScriptContext* scriptCtx = ctxPtr()) {
            return scriptCtx->StopAudio();
        }
        return false;
    }
};

inline const AudioFacade audio{};

struct TimeFacade {
    float& deltaTime;
};

inline thread_local TimeFacade time{ detail::gFrameDeltaTime };

struct EngineFacade {
    float& FPS;
};

inline thread_local EngineFacade ModuEngine{ detail::gFrameFps };

struct SpriteFacade {
    bool HasClips() const {
        if (ScriptContext* scriptCtx = ctxPtr()) {
            return scriptCtx->GetSpriteClipCount() > 0;
        }
        return false;
    }

    int ClipCount() const {
        if (ScriptContext* scriptCtx = ctxPtr()) {
            return scriptCtx->GetSpriteClipCount();
        }
        return 0;
    }

    int ClipIndex() const {
        if (ScriptContext* scriptCtx = ctxPtr()) {
            return scriptCtx->GetSpriteClipIndex();
        }
        return -1;
    }

    bool SetClip(int clipIndex) const {
        if (ScriptContext* scriptCtx = ctxPtr()) {
            return scriptCtx->SetSpriteClipIndex(clipIndex);
        }
        return false;
    }

    bool SetClip(const std::string& clipName) const {
        if (ScriptContext* scriptCtx = ctxPtr()) {
            return scriptCtx->SetSpriteClipName(clipName);
        }
        return false;
    }

    std::string ClipNameAt(int clipIndex) const {
        if (ScriptContext* scriptCtx = ctxPtr()) {
            return scriptCtx->GetSpriteClipNameAt(clipIndex);
        }
        return {};
    }
};

inline const SpriteFacade sprite{};

inline bool EditClipSelector(const char* label, int& clipIndex) {
    ScriptContext* scriptCtx = ctxPtr();
    if (!scriptCtx) return false;

    const int clipCount = scriptCtx->GetSpriteClipCount();
    std::string preview = detail::isValidClip(*scriptCtx, clipIndex)
        ? scriptCtx->GetSpriteClipNameAt(clipIndex)
        : std::string("<None>");

    bool changed = false;
    if (ImGui::BeginCombo(label, preview.c_str())) {
        const bool noneSelected = (clipIndex < 0 || clipIndex >= clipCount);
        if (ImGui::Selectable("<None>", noneSelected)) {
            clipIndex = -1;
            changed = true;
        }
        if (noneSelected) ImGui::SetItemDefaultFocus();

        for (int i = 0; i < clipCount; ++i) {
            const std::string clipName = scriptCtx->GetSpriteClipNameAt(i);
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

inline bool EditDirectionalClipGrid(std::array<int, 4>& idleClips,
                                    std::array<std::array<int, 4>, 4>& walkClips) {
    ScriptContext* scriptCtx = ctxPtr();
    if (!scriptCtx) return false;

    const int clipCount = scriptCtx->GetSpriteClipCount();
    if (clipCount <= 0) {
        ImGui::TextDisabled("Enable Sprite Sheet clips on this object to assign animation frames.");
        return false;
    }

    static constexpr const char* kDirectionLabels[4] = { "Down", "Up", "Right", "Left" };
    bool changed = false;
    ImGui::TextDisabled("%d sprite clips available.", clipCount);
    for (int dir = 0; dir < 4; ++dir) {
        if (ImGui::CollapsingHeader(kDirectionLabels[dir], ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= EditClipSelector(("Idle##" + std::string(kDirectionLabels[dir])).c_str(), idleClips[dir]);
            for (int frame = 0; frame < 4; ++frame) {
                changed |= EditClipSelector(
                    ("Walk " + std::to_string(frame + 1) + "##" + std::string(kDirectionLabels[dir])).c_str(),
                    walkClips[dir][frame]);
            }
        }
    }
    return changed;
}

template <size_t N>
inline bool EditSoundSet(const char* heading, std::array<std::string, N>& sounds,
                         const char* itemPrefix = "Sound") {
    ImGui::TextUnformatted(heading);
    bool changed = false;

    for (size_t i = 0; i < N; ++i) {
        ImGui::PushID(static_cast<int>(i));

        std::vector<char> buffer(512, '\0');
        std::snprintf(buffer.data(), buffer.size(), "%s", sounds[i].c_str());
        ImGui::SetNextItemWidth(-84.0f);
        if (ImGui::InputText("##clip", buffer.data(), buffer.size())) {
            sounds[i] = buffer.data();
            changed = true;
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
                if (payload->Data && payload->DataSize > 0) {
                    const char* droppedPath = static_cast<const char*>(payload->Data);
                    if (droppedPath) {
                        std::string candidate = droppedPath;
                        if (detail::isAudioPath(candidate)) {
                            sounds[i] = candidate;
                            changed = true;
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            sounds[i].clear();
            changed = true;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%s %zu", itemPrefix, i + 1);

        ImGui::PopID();
    }

    return changed;
}

} // namespace ModuCPP
