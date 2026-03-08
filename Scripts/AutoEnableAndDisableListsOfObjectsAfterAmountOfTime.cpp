#include "DialoguePortShared.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace {
using namespace DialoguePort;

struct ToggleConfig {
    std::vector<std::string> objectsToEnable;
    std::vector<std::string> objectsToDisable;
    float intervalSeconds = 1.0f;
};

struct ToggleRuntimeState {
    float timer = 0.0f;
    bool initialized = false;
    bool configInitialized = false;
    std::string cachedEnableRefsRaw;
    std::string cachedDisableRefsRaw;
    std::string cachedIntervalRaw;
    ToggleConfig cachedConfig;
};

std::unordered_map<int, ToggleRuntimeState> g_runtimeStates;

constexpr const char* kSettingEnableRefs = "toggle.objectsToEnable";
constexpr const char* kSettingDisableRefs = "toggle.objectsToDisable";
constexpr const char* kSettingInterval = "toggle.intervalSeconds";

bool setSettingIfChanged(ScriptContext& ctx, const std::string& key, const std::string& value) {
    if (ctx.GetSetting(key, "") == value) return false;
    ctx.SetSetting(key, value);
    return true;
}

ToggleConfig loadConfig(ScriptContext& ctx) {
    ToggleConfig config;
    config.objectsToEnable = DeserializeObjectRefs(ctx.GetSetting(kSettingEnableRefs, ""));
    config.objectsToDisable = DeserializeObjectRefs(ctx.GetSetting(kSettingDisableRefs, ""));
    config.intervalSeconds = std::max(0.0f, ParseFloat(ctx.GetSetting(kSettingInterval, "1"), 1.0f));
    return config;
}

const ToggleConfig& getCachedConfig(ScriptContext& ctx, ToggleRuntimeState& state) {
    const std::string enableRefsRaw = ctx.GetSetting(kSettingEnableRefs, "");
    const std::string disableRefsRaw = ctx.GetSetting(kSettingDisableRefs, "");
    const std::string intervalRaw = ctx.GetSetting(kSettingInterval, "1");

    const bool configChanged =
        !state.configInitialized ||
        state.cachedEnableRefsRaw != enableRefsRaw ||
        state.cachedDisableRefsRaw != disableRefsRaw ||
        state.cachedIntervalRaw != intervalRaw;

    if (configChanged) {
        state.cachedEnableRefsRaw = enableRefsRaw;
        state.cachedDisableRefsRaw = disableRefsRaw;
        state.cachedIntervalRaw = intervalRaw;
        state.cachedConfig.objectsToEnable = DeserializeObjectRefs(enableRefsRaw);
        state.cachedConfig.objectsToDisable = DeserializeObjectRefs(disableRefsRaw);
        state.cachedConfig.intervalSeconds = std::max(0.0f, ParseFloat(intervalRaw, 1.0f));
        state.configInitialized = true;
    }

    return state.cachedConfig;
}

void saveConfig(ScriptContext& ctx, const ToggleConfig& config) {
    setSettingIfChanged(ctx, kSettingEnableRefs, SerializeObjectRefs(config.objectsToEnable));
    setSettingIfChanged(ctx, kSettingDisableRefs, SerializeObjectRefs(config.objectsToDisable));
    setSettingIfChanged(ctx, kSettingInterval, std::to_string(config.intervalSeconds));
}

bool enableObjects(ScriptContext& ctx, const std::vector<std::string>& refs) {
    bool changed = false;
    for (const std::string& ref : refs) {
        SceneObject* obj = ResolveSceneObjectRef(ctx, ref);
        if (!obj) continue;

        if (!obj->enabled) {
            obj->enabled = true;
            changed = true;
        }
        if (obj->hasCollider && !obj->collider.enabled) {
            obj->collider.enabled = true;
            changed = true;
        }
        if (obj->hasCollider2D && !obj->collider2D.enabled) {
            obj->collider2D.enabled = true;
            changed = true;
        }
    }
    if (changed) {
        ctx.MarkDirty();
    }
    return changed;
}

bool disableObjects(ScriptContext& ctx, const std::vector<std::string>& refs) {
    return SetObjectsEnabledState(ctx, refs, false);
}

} // namespace

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    ToggleConfig config = loadConfig(ctx);
    bool changed = false;

    changed |= ImGui::DragFloat("Interval (s)", &config.intervalSeconds, 0.01f, 0.0f, 120.0f, "%.2f");
    config.intervalSeconds = std::max(0.0f, config.intervalSeconds);

    changed |= DrawObjectRefListEditor(ctx, "Objects To Enable", config.objectsToEnable);
    changed |= DrawObjectRefListEditor(ctx, "Objects To Disable", config.objectsToDisable);

    if (changed) {
        saveConfig(ctx, config);
    }
}

void Begin(ScriptContext& ctx, float /*deltaTime*/) {
    if (!ctx.object) return;
    ToggleRuntimeState& state = g_runtimeStates[ctx.object->id];
    state.timer = 0.0f;
    state.initialized = true;
}

void TickUpdate(ScriptContext& ctx, float deltaTime) {
    if (!ctx.object || deltaTime <= 0.0f) return;

    ToggleRuntimeState& state = g_runtimeStates[ctx.object->id];
    if (!state.initialized) {
        state.timer = 0.0f;
        state.initialized = true;
    }
    const ToggleConfig& config = getCachedConfig(ctx, state);

    state.timer += deltaTime;
    if (state.timer < config.intervalSeconds) return;

    enableObjects(ctx, config.objectsToEnable);
    disableObjects(ctx, config.objectsToDisable);
    state.timer = 0.0f;
}
