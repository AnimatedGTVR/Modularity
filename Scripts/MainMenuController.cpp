#include "DialoguePortShared.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using namespace DialoguePort;

enum class MenuOrientation {
    Vertical = 0,
    Horizontal = 1
};

struct MenuAction {
    std::vector<std::string> enableRefs;
    std::vector<std::string> disableRefs;
};

struct MenuConfig {
    MenuOrientation orientation = MenuOrientation::Vertical;
    std::string heartRef;
    std::vector<std::string> menuItemRefs;
    std::string moveSoundRef;
    std::string selectSoundRef;
    float moveSpeed = 0.3f;
    float offset = 20.0f;
    float inputDelay = 0.2f;
    float initialDelay = 0.2f;
    std::vector<MenuAction> actions;
};

struct MenuRuntimeState {
    int currentIndex = 0;
    float nextInputTime = 0.0f;
    float startupDelayRemaining = 0.0f;
    float elapsed = 0.0f;
    glm::vec2 heartVisualPos = glm::vec2(0.0f);
    bool initialized = false;
};

std::unordered_map<int, MenuRuntimeState> g_runtimeStates;
std::unordered_map<int, int> g_selectedActionByObject;

constexpr const char* kSettingOrientation = "menu.orientation";
constexpr const char* kSettingHeartRef = "menu.heartRef";
constexpr const char* kSettingMenuItems = "menu.itemRefs";
constexpr const char* kSettingMoveSoundRef = "menu.moveSoundRef";
constexpr const char* kSettingSelectSoundRef = "menu.selectSoundRef";
constexpr const char* kSettingMoveSpeed = "menu.moveSpeed";
constexpr const char* kSettingOffset = "menu.offset";
constexpr const char* kSettingInputDelay = "menu.inputDelay";
constexpr const char* kSettingInitialDelay = "menu.initialDelay";
constexpr const char* kSettingActions = "menu.actions";

bool setSettingIfChanged(ScriptContext& ctx, const std::string& key, const std::string& value) {
    if (ctx.GetSetting(key, "") == value) return false;
    ctx.SetSetting(key, value);
    return true;
}

void normalizeActionCount(MenuConfig& config) {
    if (config.actions.size() < config.menuItemRefs.size()) {
        config.actions.resize(config.menuItemRefs.size());
    } else if (config.actions.size() > config.menuItemRefs.size()) {
        config.actions.resize(config.menuItemRefs.size());
    }
}

std::string serializeActions(const std::vector<MenuAction>& actions) {
    std::vector<std::string> encoded;
    encoded.reserve(actions.size());
    for (const MenuAction& action : actions) {
        std::vector<std::string> fields;
        fields.reserve(2);
        fields.push_back(SerializeObjectRefs(action.enableRefs));
        fields.push_back(SerializeObjectRefs(action.disableRefs));
        encoded.push_back(JoinEscaped(fields, '|'));
    }
    return JoinEscaped(encoded, '\t');
}

std::vector<MenuAction> deserializeActions(const std::string& encoded) {
    std::vector<MenuAction> actions;
    if (encoded.empty()) return actions;

    char outerDelimiter = '\t';
    if (encoded.find('\t') == std::string::npos && encoded.find('\n') != std::string::npos) {
        outerDelimiter = '\n';
    }

    std::vector<std::string> entries = SplitEscaped(encoded, outerDelimiter);
    actions.reserve(entries.size());
    for (const std::string& entry : entries) {
        if (Trim(entry).empty()) continue;
        std::vector<std::string> fields = SplitEscaped(entry, '|');
        MenuAction action;
        if (fields.size() > 0) action.enableRefs = DeserializeObjectRefs(fields[0]);
        if (fields.size() > 1) action.disableRefs = DeserializeObjectRefs(fields[1]);
        actions.push_back(std::move(action));
    }
    return actions;
}

MenuConfig loadConfig(ScriptContext& ctx) {
    MenuConfig config;

    config.orientation = static_cast<MenuOrientation>(
        std::clamp(ParseInt(ctx.GetSetting(kSettingOrientation, "0"), 0), 0, 1));
    config.heartRef = ctx.GetSetting(kSettingHeartRef, "");
    config.menuItemRefs = DeserializeObjectRefs(ctx.GetSetting(kSettingMenuItems, ""));
    config.moveSoundRef = ctx.GetSetting(kSettingMoveSoundRef, "");
    config.selectSoundRef = ctx.GetSetting(kSettingSelectSoundRef, "");
    config.moveSpeed = std::max(0.0f, ParseFloat(ctx.GetSetting(kSettingMoveSpeed, "0.3"), 0.3f));
    config.offset = ParseFloat(ctx.GetSetting(kSettingOffset, "20"), 20.0f);
    config.inputDelay = std::max(0.0f, ParseFloat(ctx.GetSetting(kSettingInputDelay, "0.2"), 0.2f));
    config.initialDelay = std::max(0.0f, ParseFloat(ctx.GetSetting(kSettingInitialDelay, "0.2"), 0.2f));
    config.actions = deserializeActions(ctx.GetSetting(kSettingActions, ""));
    normalizeActionCount(config);

    return config;
}

void saveConfig(ScriptContext& ctx, MenuConfig& config) {
    normalizeActionCount(config);
    setSettingIfChanged(ctx, kSettingOrientation, std::to_string(static_cast<int>(config.orientation)));
    setSettingIfChanged(ctx, kSettingHeartRef, config.heartRef);
    setSettingIfChanged(ctx, kSettingMenuItems, SerializeObjectRefs(config.menuItemRefs));
    setSettingIfChanged(ctx, kSettingMoveSoundRef, config.moveSoundRef);
    setSettingIfChanged(ctx, kSettingSelectSoundRef, config.selectSoundRef);
    setSettingIfChanged(ctx, kSettingMoveSpeed, std::to_string(config.moveSpeed));
    setSettingIfChanged(ctx, kSettingOffset, std::to_string(config.offset));
    setSettingIfChanged(ctx, kSettingInputDelay, std::to_string(config.inputDelay));
    setSettingIfChanged(ctx, kSettingInitialDelay, std::to_string(config.initialDelay));
    setSettingIfChanged(ctx, kSettingActions, serializeActions(config.actions));
}

void playSoundFromRef(ScriptContext& ctx, const std::string& objectRef) {
    SceneObject* source = ResolveSceneObjectRef(ctx, objectRef);
    if (!source || !source->hasAudioSource || source->audioSource.clipPath.empty()) return;
    ctx.PlayAudioOneShot(source->audioSource.clipPath);
}

int getDirectionInput(MenuOrientation orientation) {
    if (orientation == MenuOrientation::Vertical) {
        if (IsRuntimeKeyDown(GLFW_KEY_DOWN, ImGuiKey_DownArrow) ||
            IsRuntimeKeyDown(GLFW_KEY_S, ImGuiKey_S)) {
            return 1;
        }
        if (IsRuntimeKeyDown(GLFW_KEY_UP, ImGuiKey_UpArrow) ||
            IsRuntimeKeyDown(GLFW_KEY_W, ImGuiKey_W)) {
            return -1;
        }
        return 0;
    }

    if (IsRuntimeKeyDown(GLFW_KEY_RIGHT, ImGuiKey_RightArrow) ||
        IsRuntimeKeyDown(GLFW_KEY_D, ImGuiKey_D)) {
        return 1;
    }
    if (IsRuntimeKeyDown(GLFW_KEY_LEFT, ImGuiKey_LeftArrow) ||
        IsRuntimeKeyDown(GLFW_KEY_A, ImGuiKey_A)) {
        return -1;
    }
    return 0;
}

bool isSubmitPressed() {
    return IsRuntimeKeyDown(GLFW_KEY_ENTER, ImGuiKey_Enter) ||
           IsRuntimeKeyDown(GLFW_KEY_KP_ENTER, ImGuiKey_KeypadEnter);
}

SceneObject* resolveMenuItem(ScriptContext& ctx, const MenuConfig& config, int index) {
    if (index < 0 || index >= static_cast<int>(config.menuItemRefs.size())) return nullptr;
    return ResolveSceneObjectRef(ctx, config.menuItemRefs[static_cast<size_t>(index)]);
}

void updateHeartPosition(ScriptContext& ctx,
                         const MenuConfig& config,
                         MenuRuntimeState& state,
                         float deltaTime) {
    SceneObject* heart = ResolveSceneObjectRef(ctx, config.heartRef);
    SceneObject* target = resolveMenuItem(ctx, config, state.currentIndex);
    if (!heart || !target || !HasUIComponent(*heart) || !HasUIComponent(*target)) return;

    glm::vec2 targetPosition = target->ui.position;
    const float itemHalfW = std::max(0.0f, target->ui.size.x * 0.5f);
    const float itemHalfH = std::max(0.0f, target->ui.size.y * 0.5f);
    const float heartHalfW = std::max(0.0f, heart->ui.size.x * 0.5f);
    const float heartHalfH = std::max(0.0f, heart->ui.size.y * 0.5f);
    if (config.orientation == MenuOrientation::Vertical) {
        targetPosition.x -= itemHalfW + heartHalfW + config.offset;
    } else {
        targetPosition.y -= itemHalfH + heartHalfH + config.offset;
    }

    if (!state.initialized) {
        state.heartVisualPos = heart->ui.position;
    }
    if (config.moveSpeed <= 0.0001f) {
        state.heartVisualPos = targetPosition;
    } else {
        float alpha = std::clamp(deltaTime / config.moveSpeed, 0.0f, 1.0f);
        state.heartVisualPos = glm::mix(state.heartVisualPos, targetPosition, alpha);
    }

    if (glm::distance(state.heartVisualPos, heart->ui.position) > 0.001f) {
        heart->ui.position = state.heartVisualPos;
        ctx.MarkDirty();
    }
}

void executeAction(ScriptContext& ctx, const MenuConfig& config, int index) {
    if (index < 0 || index >= static_cast<int>(config.actions.size())) {
        ctx.AddConsoleMessage("MainMenuController: no action assigned for current menu item.",
                              ConsoleMessageType::Warning);
        return;
    }
    const MenuAction& action = config.actions[static_cast<size_t>(index)];
    SetObjectsEnabledState(ctx, action.disableRefs, false);
    SetObjectsEnabledState(ctx, action.enableRefs, true);
}

} // namespace

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    MenuConfig config = loadConfig(ctx);
    bool changed = false;

    const char* orientationLabels[] = { "Vertical", "Horizontal" };
    int orientation = static_cast<int>(config.orientation);
    if (ImGui::Combo("Menu Orientation", &orientation, orientationLabels, IM_ARRAYSIZE(orientationLabels))) {
        config.orientation = static_cast<MenuOrientation>(std::clamp(orientation, 0, 1));
        changed = true;
    }

    changed |= DrawObjectRefInput(ctx, "Heart Object", config.heartRef);
    changed |= DrawObjectRefInput(ctx, "Move Sound Source", config.moveSoundRef);
    changed |= DrawObjectRefInput(ctx, "Select Sound Source", config.selectSoundRef);
    changed |= DrawObjectRefListEditor(ctx, "Menu Item Refs", config.menuItemRefs);

    changed |= ImGui::DragFloat("Move Speed (s)", &config.moveSpeed, 0.01f, 0.0f, 5.0f, "%.2f");
    config.moveSpeed = std::max(0.0f, config.moveSpeed);
    changed |= ImGui::DragFloat("Offset", &config.offset, 0.5f, -500.0f, 500.0f, "%.1f");
    changed |= ImGui::DragFloat("Input Delay", &config.inputDelay, 0.01f, 0.0f, 2.0f, "%.2f");
    config.inputDelay = std::max(0.0f, config.inputDelay);
    changed |= ImGui::DragFloat("Initial Delay", &config.initialDelay, 0.01f, 0.0f, 5.0f, "%.2f");
    config.initialDelay = std::max(0.0f, config.initialDelay);

    normalizeActionCount(config);
    if (!config.menuItemRefs.empty()) {
        int& selected = g_selectedActionByObject[ctx.object ? ctx.object->id : -1];
        selected = std::clamp(selected, 0, static_cast<int>(config.menuItemRefs.size()) - 1);
        if (ImGui::BeginCombo("Edit Action", std::to_string(selected + 1).c_str())) {
            for (int i = 0; i < static_cast<int>(config.menuItemRefs.size()); ++i) {
                std::string label = std::to_string(i + 1);
                if (SceneObject* item = resolveMenuItem(ctx, config, i)) {
                    label += ". " + item->name;
                }
                bool isSelected = (selected == i);
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    selected = i;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (selected >= 0 && selected < static_cast<int>(config.actions.size())) {
            MenuAction& action = config.actions[static_cast<size_t>(selected)];
            changed |= DrawObjectRefListEditor(ctx, "Enable Objects", action.enableRefs);
            changed |= DrawObjectRefListEditor(ctx, "Disable Objects", action.disableRefs);
        }
    } else {
        ImGui::TextDisabled("Add menu item references to configure actions.");
    }

    if (changed) {
        saveConfig(ctx, config);
    }
}

void Begin(ScriptContext& ctx, float /*deltaTime*/) {
    if (!ctx.object) return;
    MenuConfig config = loadConfig(ctx);
    MenuRuntimeState& state = g_runtimeStates[ctx.object->id];
    state.currentIndex = 0;
    state.nextInputTime = 0.0f;
    state.startupDelayRemaining = config.initialDelay;
    state.elapsed = 0.0f;
    state.initialized = false;
    updateHeartPosition(ctx, config, state, 1.0f);
    state.initialized = true;
}

void TickUpdate(ScriptContext& ctx, float deltaTime) {
    if (!ctx.object || deltaTime <= 0.0f) return;

    MenuConfig config = loadConfig(ctx);
    normalizeActionCount(config);

    MenuRuntimeState& state = g_runtimeStates[ctx.object->id];
    if (!state.initialized) {
        state.currentIndex = 0;
        state.nextInputTime = 0.0f;
        state.startupDelayRemaining = config.initialDelay;
        state.elapsed = 0.0f;
        state.initialized = true;
    }

    const int itemCount = static_cast<int>(config.menuItemRefs.size());
    if (itemCount <= 0) {
        updateHeartPosition(ctx, config, state, deltaTime);
        return;
    }

    state.currentIndex = std::clamp(state.currentIndex, 0, itemCount - 1);
    state.elapsed += deltaTime;
    state.startupDelayRemaining = std::max(0.0f, state.startupDelayRemaining - deltaTime);
    const bool canMove = state.startupDelayRemaining <= 0.0f;

    if (canMove && state.elapsed >= state.nextInputTime) {
        const int direction = getDirectionInput(config.orientation);
        if (direction != 0) {
            state.currentIndex += direction;
            if (state.currentIndex < 0) state.currentIndex = itemCount - 1;
            if (state.currentIndex >= itemCount) state.currentIndex = 0;
            state.nextInputTime = state.elapsed + std::max(0.0f, config.inputDelay);
            playSoundFromRef(ctx, config.moveSoundRef);
        }

        if (isSubmitPressed()) {
            executeAction(ctx, config, state.currentIndex);
            playSoundFromRef(ctx, config.selectSoundRef);
            state.nextInputTime = state.elapsed + std::max(0.0f, config.inputDelay);
        }
    }

    updateHeartPosition(ctx, config, state, deltaTime);
}
