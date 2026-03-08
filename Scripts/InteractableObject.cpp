#include "DialoguePortShared.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using namespace DialoguePort;

enum class InteractionType {
    Dialogue = 0,
    ToggleObjects = 1
};

struct InteractionOption {
    std::string optionName = "New Option";
    InteractionType interactionType = InteractionType::Dialogue;

    std::string dialogueSystemRef;
    std::vector<DialogueLine> dialogueLines;
    std::vector<std::string> dialogueItemsToEnableOnEnd;
    std::vector<std::string> dialogueItemsToDisableOnEnd;

    std::vector<std::string> itemsToEnable;
    std::vector<std::string> itemsToDisable;
};

struct InteractableConfig {
    bool canInteract = true;
    bool oneTimeUse = false;
    int selectedOptionIndex = 0;
    std::vector<InteractionOption> options;

    std::string selectionNameOverride;
    bool isSelected = false;
    std::vector<std::string> selectedStateEnable;
    std::vector<std::string> selectedStateDisable;

    bool interactOnKeyPress = true;
    bool requirePlayerInRange = false;
    std::string playerRef;
    float interactDistance = 2.5f;
    bool debugRange = false;
};

struct InteractableRuntimeState {
    bool prevInteractDown = false;
    bool hasSelectionState = false;
    bool lastSelectionState = false;
    float lastRangeDistance = -1.0f;
    bool lastRangeInRange = false;
    bool lastRangeHasPlayer = false;
    glm::vec3 lastPlayerWorldPos = glm::vec3(0.0f);
    glm::vec3 lastSelfWorldPos = glm::vec3(0.0f);
};

std::unordered_map<int, InteractableRuntimeState> g_runtimeStates;
std::unordered_map<int, int> g_selectedOptionEditorIndex;
std::unordered_map<int, int> g_selectedDialogueLineEditorIndex;

constexpr const char* kSettingCanInteract = "interactable.canInteract";
constexpr const char* kSettingOneTimeUse = "interactable.oneTimeUse";
constexpr const char* kSettingSelectedOption = "interactable.selectedOptionIndex";
constexpr const char* kSettingOptions = "interactable.options";
constexpr const char* kSettingSelectionName = "interactable.selectionNameOverride";
constexpr const char* kSettingIsSelected = "interactable.isSelected";
constexpr const char* kSettingSelectedEnable = "interactable.selectedStateEnable";
constexpr const char* kSettingSelectedDisable = "interactable.selectedStateDisable";
constexpr const char* kSettingInteractOnKey = "interactable.interactOnKeyPress";
constexpr const char* kSettingRequireRange = "interactable.requirePlayerInRange";
constexpr const char* kSettingPlayerRef = "interactable.playerRef";
constexpr const char* kSettingDistance = "interactable.interactDistance";
constexpr const char* kSettingDebugRange = "interactable.debugRange";

constexpr const char* kInteractionRequestSerial = "interaction.requestSerial";
constexpr const char* kInteractionRequestPending = "interaction.requestPending";
constexpr const char* kInteractionOverrideLines = "interaction.overrideDialogueLines";
constexpr const char* kInteractionEndEnable = "interaction.itemsToEnableOnEnd";
constexpr const char* kInteractionEndDisable = "interaction.itemsToDisableOnEnd";
constexpr const char* kInteractionPlayerRef = "interaction.playerRef";

bool setSettingIfChanged(ScriptContext& ctx, const std::string& key, const std::string& value) {
    if (ctx.GetSetting(key, "") == value) return false;
    ctx.SetSetting(key, value);
    return true;
}

std::string serializeOptions(const std::vector<InteractionOption>& options) {
    std::vector<std::string> encodedOptions;
    encodedOptions.reserve(options.size());
    for (const InteractionOption& option : options) {
        std::vector<std::string> fields;
        fields.reserve(8);
        fields.push_back(option.optionName);
        fields.push_back(std::to_string(static_cast<int>(option.interactionType)));
        fields.push_back(option.dialogueSystemRef);
        fields.push_back(SerializeDialogueLines(option.dialogueLines));
        fields.push_back(SerializeObjectRefs(option.dialogueItemsToEnableOnEnd));
        fields.push_back(SerializeObjectRefs(option.dialogueItemsToDisableOnEnd));
        fields.push_back(SerializeObjectRefs(option.itemsToEnable));
        fields.push_back(SerializeObjectRefs(option.itemsToDisable));
        encodedOptions.push_back(JoinEscaped(fields, '|'));
    }
    // Keep serialized options single-line for scene save parsing.
    return JoinEscaped(encodedOptions, '\t');
}

std::vector<InteractionOption> deserializeOptions(const std::string& encoded) {
    std::vector<InteractionOption> options;
    if (encoded.empty()) return options;

    std::vector<std::string> rawOptions = SplitEscaped(encoded, '\t');
    if (rawOptions.size() <= 1 && encoded.find('\t') == std::string::npos &&
        encoded.find('\n') != std::string::npos) {
        rawOptions = SplitEscaped(encoded, '\n');
    }
    options.reserve(rawOptions.size());

    for (const std::string& raw : rawOptions) {
        if (Trim(raw).empty()) continue;

        std::vector<std::string> fields = SplitEscaped(raw, '|');
        if (fields.empty()) continue;

        InteractionOption option;
        if (fields.size() > 0) option.optionName = fields[0];
        if (fields.size() > 1) {
            option.interactionType = static_cast<InteractionType>(
                std::clamp(ParseInt(fields[1], 0), 0, 1));
        }
        if (fields.size() > 2) option.dialogueSystemRef = fields[2];
        if (fields.size() > 3) option.dialogueLines = DeserializeDialogueLines(fields[3]);
        if (fields.size() > 4) option.dialogueItemsToEnableOnEnd = DeserializeObjectRefs(fields[4]);
        if (fields.size() > 5) option.dialogueItemsToDisableOnEnd = DeserializeObjectRefs(fields[5]);
        if (fields.size() > 6) option.itemsToEnable = DeserializeObjectRefs(fields[6]);
        if (fields.size() > 7) option.itemsToDisable = DeserializeObjectRefs(fields[7]);

        options.push_back(std::move(option));
    }

    return options;
}

InteractableConfig loadConfig(ScriptContext& ctx) {
    InteractableConfig config;
    config.canInteract = ParseBool(ctx.GetSetting(kSettingCanInteract, "1"), true);
    config.oneTimeUse = ParseBool(ctx.GetSetting(kSettingOneTimeUse, "0"), false);
    config.selectedOptionIndex = std::max(0, ParseInt(ctx.GetSetting(kSettingSelectedOption, "0"), 0));
    config.options = deserializeOptions(ctx.GetSetting(kSettingOptions, ""));

    config.selectionNameOverride = ctx.GetSetting(kSettingSelectionName, "");
    config.isSelected = ParseBool(ctx.GetSetting(kSettingIsSelected, "0"), false);
    config.selectedStateEnable = DeserializeObjectRefs(ctx.GetSetting(kSettingSelectedEnable, ""));
    config.selectedStateDisable = DeserializeObjectRefs(ctx.GetSetting(kSettingSelectedDisable, ""));

    config.interactOnKeyPress = ParseBool(ctx.GetSetting(kSettingInteractOnKey, "1"), true);
    config.requirePlayerInRange = ParseBool(ctx.GetSetting(kSettingRequireRange, "0"), false);
    config.playerRef = ctx.GetSetting(kSettingPlayerRef, "");
    config.interactDistance = std::max(0.0f, ParseFloat(ctx.GetSetting(kSettingDistance, "2.5"), 2.5f));
    config.debugRange = ParseBool(ctx.GetSetting(kSettingDebugRange, "0"), false);
    return config;
}

void saveConfig(ScriptContext& ctx, const InteractableConfig& config) {
    setSettingIfChanged(ctx, kSettingCanInteract, config.canInteract ? "1" : "0");
    setSettingIfChanged(ctx, kSettingOneTimeUse, config.oneTimeUse ? "1" : "0");
    setSettingIfChanged(ctx, kSettingSelectedOption, std::to_string(std::max(0, config.selectedOptionIndex)));
    setSettingIfChanged(ctx, kSettingOptions, serializeOptions(config.options));

    setSettingIfChanged(ctx, kSettingSelectionName, config.selectionNameOverride);
    setSettingIfChanged(ctx, kSettingIsSelected, config.isSelected ? "1" : "0");
    setSettingIfChanged(ctx, kSettingSelectedEnable, SerializeObjectRefs(config.selectedStateEnable));
    setSettingIfChanged(ctx, kSettingSelectedDisable, SerializeObjectRefs(config.selectedStateDisable));

    setSettingIfChanged(ctx, kSettingInteractOnKey, config.interactOnKeyPress ? "1" : "0");
    setSettingIfChanged(ctx, kSettingRequireRange, config.requirePlayerInRange ? "1" : "0");
    setSettingIfChanged(ctx, kSettingPlayerRef, config.playerRef);
    setSettingIfChanged(ctx, kSettingDistance, std::to_string(config.interactDistance));
    setSettingIfChanged(ctx, kSettingDebugRange, config.debugRange ? "1" : "0");
}

bool canInteract(const InteractableConfig& config) {
    return config.canInteract && !config.options.empty();
}

const char* interactionTypeLabel(InteractionType type) {
    switch (type) {
        case InteractionType::Dialogue: return "Dialogue";
        case InteractionType::ToggleObjects: return "Toggle Objects";
        default: return "Dialogue";
    }
}

bool drawInteractionTypeCombo(InteractionType& type) {
    bool changed = false;
    if (ImGui::BeginCombo("Interaction Type", interactionTypeLabel(type))) {
        const bool dialogueSelected = (type == InteractionType::Dialogue);
        if (ImGui::Selectable("Dialogue", dialogueSelected)) {
            type = InteractionType::Dialogue;
            changed = true;
        }
        if (dialogueSelected) ImGui::SetItemDefaultFocus();

        const bool toggleSelected = (type == InteractionType::ToggleObjects);
        if (ImGui::Selectable("Toggle Objects", toggleSelected)) {
            type = InteractionType::ToggleObjects;
            changed = true;
        }
        if (toggleSelected) ImGui::SetItemDefaultFocus();

        ImGui::EndCombo();
    }
    return changed;
}

void drawTextEffectEditor(TextEffectType& effect) {
    int flags = static_cast<int>(effect);

    bool wave = (flags & static_cast<int>(TextEffectType::Wave)) != 0;
    bool shake = (flags & static_cast<int>(TextEffectType::Shake)) != 0;
    bool bounce = (flags & static_cast<int>(TextEffectType::Bounce)) != 0;
    bool rotate = (flags & static_cast<int>(TextEffectType::Rotate)) != 0;
    bool fade = (flags & static_cast<int>(TextEffectType::Fade)) != 0;

    if (ImGui::Checkbox("Wave", &wave)) {
        if (wave) flags |= static_cast<int>(TextEffectType::Wave);
        else flags &= ~static_cast<int>(TextEffectType::Wave);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Shake", &shake)) {
        if (shake) flags |= static_cast<int>(TextEffectType::Shake);
        else flags &= ~static_cast<int>(TextEffectType::Shake);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Bounce", &bounce)) {
        if (bounce) flags |= static_cast<int>(TextEffectType::Bounce);
        else flags &= ~static_cast<int>(TextEffectType::Bounce);
    }

    if (ImGui::Checkbox("Rotate", &rotate)) {
        if (rotate) flags |= static_cast<int>(TextEffectType::Rotate);
        else flags &= ~static_cast<int>(TextEffectType::Rotate);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Fade", &fade)) {
        if (fade) flags |= static_cast<int>(TextEffectType::Fade);
        else flags &= ~static_cast<int>(TextEffectType::Fade);
    }

    effect = static_cast<TextEffectType>(flags);
}

bool drawDialogueLineEditor(ScriptContext& ctx, std::vector<DialogueLine>& lines, int& selectedIndex) {
    bool changed = false;

    if (ImGui::Button("Add Dialogue Line")) {
        if (!lines.empty()) {
            lines.push_back(lines.back());
        } else {
            lines.emplace_back();
        }
        selectedIndex = static_cast<int>(lines.size()) - 1;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove Dialogue Line") && selectedIndex >= 0 && selectedIndex < static_cast<int>(lines.size())) {
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(selectedIndex));
        if (lines.empty()) selectedIndex = -1;
        else selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(lines.size()) - 1);
        changed = true;
    }

    if (lines.empty()) {
        ImGui::TextDisabled("No override dialogue lines configured.");
        return changed;
    }

    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(lines.size()) - 1);

    ImGui::BeginChild("DialogueLinesList", ImVec2(230.0f, 220.0f), true);
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string label = std::to_string(i + 1) + ". " +
            (lines[i].characterName.empty() ? std::string("<Unnamed>") : lines[i].characterName);
        if (ImGui::Selectable(label.c_str(), selectedIndex == static_cast<int>(i))) {
            selectedIndex = static_cast<int>(i);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginGroup();
    DialogueLine& line = lines[static_cast<size_t>(selectedIndex)];

    changed |= DrawStdStringInput("Character Name", line.characterName, 256);
    changed |= DrawStdStringInput("Sentence (EN)", line.sentence, 2048, 0, true, 60.0f);
    changed |= DrawStdStringInput("Sentence (DE)", line.sentenceGerman, 2048, 0, true, 60.0f);
    changed |= DrawStdStringInput("Sentence (JP Kana)", line.sentenceJapaneseKana, 2048, 0, true, 60.0f);

    changed |= DrawAudioClipInput("Character Voice Clip", line.characterSoundClip, 512);
    changed |= ImGui::DragFloat("Typing Speed", &line.typingSpeed, 0.001f, 0.001f, 1.0f, "%.3f");
    changed |= ImGui::DragFloat("Animation Speed", &line.animationSpeed, 0.01f, 0.01f, 10.0f, "%.2f");
    changed |= ImGui::DragFloat("Effect Intensity", &line.effectIntensity, 0.01f, 0.0f, 10.0f, "%.2f");

    ImGui::TextUnformatted("Text Effects");
    TextEffectType before = line.textEffect;
    drawTextEffectEditor(line.textEffect);
    changed |= (before != line.textEffect);

    changed |= DrawObjectRefInput(ctx, "Not Talking Object", line.notTalkingObjectRef);
    changed |= DrawObjectRefInput(ctx, "Open Mouth Object", line.openMouthObjectRef);
    changed |= DrawObjectRefInput(ctx, "Closed Mouth Object", line.closedMouthObjectRef);

    changed |= DrawObjectRefListEditor(ctx, "Line Items To Enable", line.itemsToEnable);
    changed |= DrawObjectRefListEditor(ctx, "Line Items To Disable", line.itemsToDisable);

    ImGui::EndGroup();
    return changed;
}

void applySelectedState(ScriptContext& ctx, const InteractableConfig& config) {
    SetObjectsEnabledState(ctx, config.selectedStateEnable, config.isSelected);
    SetObjectsEnabledState(ctx, config.selectedStateDisable, !config.isSelected);
}

bool isInteractDown() {
    return IsRuntimeKeyDown(GLFW_KEY_E, ImGuiKey_E);
}

glm::vec3 getObjectReferencePosition(ScriptContext& ctx, const SceneObject& object) {
    if (HasUIComponent(object) && object.ui.type != UIElementType::None) {
        glm::vec2 worldUiPos(object.ui.position.x, object.ui.position.y);
        int parentId = object.parentId;
        int guard = 0;
        while (parentId >= 0 && guard < 256) {
            ++guard;
            SceneObject* parent = ctx.FindObjectById(parentId);
            if (!parent) break;
            if (HasUIComponent(*parent) && parent->ui.type != UIElementType::None) {
                worldUiPos += parent->ui.position;
                parentId = parent->parentId;
                continue;
            }
            worldUiPos += glm::vec2(parent->position.x, parent->position.y);
            break;
        }
        return glm::vec3(worldUiPos.x, worldUiPos.y, object.position.z);
    }
    return object.position;
}

bool isPlayerInRange(ScriptContext& ctx, const InteractableConfig& config,
                     InteractableRuntimeState* runtimeState = nullptr) {
    if (!ctx.object) return false;

    SceneObject* player = ResolveSceneObjectRef(ctx, config.playerRef);
    if (!player) {
        if (runtimeState) {
            runtimeState->lastRangeHasPlayer = false;
            runtimeState->lastRangeInRange = false;
            runtimeState->lastRangeDistance = -1.0f;
            runtimeState->lastSelfWorldPos = getObjectReferencePosition(ctx, *ctx.object);
            runtimeState->lastPlayerWorldPos = glm::vec3(0.0f);
        }
        return !config.requirePlayerInRange;
    }

    const glm::vec3 selfPos = getObjectReferencePosition(ctx, *ctx.object);
    const glm::vec3 playerPos = getObjectReferencePosition(ctx, *player);
    const glm::vec3 delta = playerPos - selfPos;
    const float range = std::max(0.0f, config.interactDistance);
    const float distanceSq = glm::dot(delta, delta);
    const bool inRange = distanceSq <= (range * range);

    if (runtimeState) {
        runtimeState->lastRangeHasPlayer = true;
        runtimeState->lastRangeInRange = inRange;
        runtimeState->lastRangeDistance = std::sqrt(std::max(0.0f, distanceSq));
        runtimeState->lastSelfWorldPos = selfPos;
        runtimeState->lastPlayerWorldPos = playerPos;
    }

    return !config.requirePlayerInRange || inRange;
}

ScriptComponent* findDialogueScript(SceneObject* object) {
    if (!object) return nullptr;

    for (ScriptComponent& script : object->scripts) {
        if (script.path.find("DialogueSystem") != std::string::npos) {
            return &script;
        }
    }

    for (ScriptComponent& script : object->scripts) {
        if (script.language == ScriptLanguage::Cpp) {
            return &script;
        }
    }

    if (!object->scripts.empty()) {
        return &object->scripts.front();
    }

    return nullptr;
}

bool executeDialogueOption(ScriptContext& ctx,
                           const InteractionOption& option,
                           const InteractableConfig& config) {
    SceneObject* dialogueObject = ResolveSceneObjectRef(ctx, option.dialogueSystemRef);
    if (!dialogueObject) {
        ctx.AddConsoleMessage("InteractableObject: dialogue option is missing a valid DialogueSystem object reference.",
                              ConsoleMessageType::Warning);
        return false;
    }

    ScriptComponent* dialogueScript = findDialogueScript(dialogueObject);
    if (!dialogueScript) {
        ctx.AddConsoleMessage("InteractableObject: target DialogueSystem object has no script component.",
                              ConsoleMessageType::Warning);
        return false;
    }

    bool changed = false;
    changed |= SetScriptSetting(dialogueScript, kInteractionRequestPending, "1");
    changed |= SetScriptSetting(dialogueScript, kInteractionOverrideLines, SerializeDialogueLines(option.dialogueLines));
    changed |= SetScriptSetting(dialogueScript, kInteractionEndEnable, SerializeObjectRefs(option.dialogueItemsToEnableOnEnd));
    changed |= SetScriptSetting(dialogueScript, kInteractionEndDisable, SerializeObjectRefs(option.dialogueItemsToDisableOnEnd));
    changed |= SetScriptSetting(dialogueScript, kInteractionPlayerRef, config.playerRef);

    const int currentSerial = ParseInt(GetScriptSetting(dialogueScript, kInteractionRequestSerial, "0"), 0);
    changed |= SetScriptSetting(dialogueScript, kInteractionRequestSerial, std::to_string(currentSerial + 1));

    if (!dialogueObject->enabled) {
        dialogueObject->enabled = true;
        changed = true;
    }

    if (changed) {
        ctx.MarkDirty();
    }

    return true;
}

bool executeOption(ScriptContext& ctx,
                   const InteractionOption& option,
                   const InteractableConfig& config) {
    if (option.interactionType == InteractionType::Dialogue) {
        return executeDialogueOption(ctx, option, config);
    }

    SetObjectsEnabledState(ctx, option.itemsToEnable, true);
    SetObjectsEnabledState(ctx, option.itemsToDisable, false);
    return true;
}

bool interact(ScriptContext& ctx, InteractableConfig& config, InteractableRuntimeState* runtimeState = nullptr) {
    if (!canInteract(config)) {
        return false;
    }

    if (!isPlayerInRange(ctx, config, runtimeState)) {
        if (config.requirePlayerInRange && config.debugRange) {
            if (runtimeState && runtimeState->lastRangeHasPlayer) {
                char msg[256];
                std::snprintf(msg, sizeof(msg),
                              "InteractableObject: out of range (distance %.2f > %.2f).",
                              runtimeState->lastRangeDistance,
                              std::max(0.0f, config.interactDistance));
                ctx.AddConsoleMessage(msg, ConsoleMessageType::Info);
            } else {
                ctx.AddConsoleMessage("InteractableObject: player reference is missing/unresolved for range check.",
                                      ConsoleMessageType::Warning);
            }
        }
        return false;
    }

    const int optionIndex = std::clamp(config.selectedOptionIndex, 0,
                                       static_cast<int>(config.options.size()) - 1);
    config.selectedOptionIndex = optionIndex;
    const InteractionOption& option = config.options[static_cast<size_t>(optionIndex)];

    const bool executed = executeOption(ctx, option, config);
    if (executed && config.oneTimeUse) {
        config.canInteract = false;
    }

    return executed;
}

const char* selectionName(const InteractableConfig& config, const SceneObject* object) {
    if (!Trim(config.selectionNameOverride).empty()) {
        return config.selectionNameOverride.c_str();
    }
    if (object) return object->name.c_str();
    return "Interactable";
}

void drawRuntimeStatus(const InteractableConfig& config, const InteractableRuntimeState* runtimeState) {
    ImGui::Separator();
    ImGui::TextUnformatted("Runtime");
    ImGui::TextDisabled("Can Interact: %s", config.canInteract ? "Yes" : "No");
    ImGui::TextDisabled("Options: %zu", config.options.size());
    ImGui::TextDisabled("Selected Option: %d", config.selectedOptionIndex);
    if (!runtimeState) return;
    if (config.requirePlayerInRange || config.debugRange) {
        if (!runtimeState->lastRangeHasPlayer) {
            ImGui::TextDisabled("Range: player not found");
        } else {
            ImGui::TextDisabled("Range Distance: %.2f / %.2f",
                                runtimeState->lastRangeDistance,
                                std::max(0.0f, config.interactDistance));
            ImGui::TextDisabled("In Range: %s", runtimeState->lastRangeInRange ? "Yes" : "No");
            if (config.debugRange) {
                ImGui::TextDisabled("Self Pos: (%.2f, %.2f, %.2f)",
                                    runtimeState->lastSelfWorldPos.x,
                                    runtimeState->lastSelfWorldPos.y,
                                    runtimeState->lastSelfWorldPos.z);
                ImGui::TextDisabled("Player Pos: (%.2f, %.2f, %.2f)",
                                    runtimeState->lastPlayerWorldPos.x,
                                    runtimeState->lastPlayerWorldPos.y,
                                    runtimeState->lastPlayerWorldPos.z);
            }
        }
    }
}

} // namespace

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    InteractableConfig config = loadConfig(ctx);
    bool changed = false;
    const int objectId = ctx.object ? ctx.object->id : -1;
    InteractableRuntimeState& runtimeState = g_runtimeStates[objectId];
    isPlayerInRange(ctx, config, &runtimeState);

    ImGui::TextUnformatted("InteractableObject (Unity Port)");
    ImGui::Separator();

    ImGui::TextDisabled("Selection Name: %s", selectionName(config, ctx.object));

    changed |= ImGui::Checkbox("Can Interact", &config.canInteract);
    changed |= ImGui::Checkbox("One Time Use", &config.oneTimeUse);
    changed |= ImGui::Checkbox("Interact On E", &config.interactOnKeyPress);
    changed |= ImGui::Checkbox("Require Player In Range", &config.requirePlayerInRange);
    changed |= ImGui::DragFloat("Interaction Distance", &config.interactDistance, 0.05f, 0.0f, 100.0f, "%.2f");
    changed |= ImGui::Checkbox("Debug Range", &config.debugRange);

    changed |= DrawObjectRefInput(ctx, "Player Ref", config.playerRef);
    changed |= DrawStdStringInput("Selection Name Override", config.selectionNameOverride, 256);

    if (ImGui::Checkbox("Is Selected", &config.isSelected)) {
        applySelectedState(ctx, config);
        changed = true;
    }

    changed |= DrawObjectRefListEditor(ctx, "Selected State Enable", config.selectedStateEnable);
    changed |= DrawObjectRefListEditor(ctx, "Selected State Disable", config.selectedStateDisable);

    if (ImGui::Button("Interact Now")) {
        if (interact(ctx, config, &runtimeState)) {
            changed = true;
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Interaction Options");

    if (ImGui::Button("Add Option")) {
        config.options.emplace_back();
        const int objectId = ctx.object ? ctx.object->id : -1;
        g_selectedOptionEditorIndex[objectId] = static_cast<int>(config.options.size()) - 1;
        changed = true;
    }
    ImGui::SameLine();
    {
        const int objectId = ctx.object ? ctx.object->id : -1;
        int& selected = g_selectedOptionEditorIndex[objectId];
        if (ImGui::Button("Remove Option") && selected >= 0 && selected < static_cast<int>(config.options.size())) {
            config.options.erase(config.options.begin() + static_cast<std::ptrdiff_t>(selected));
            if (config.options.empty()) selected = -1;
            else selected = std::clamp(selected, 0, static_cast<int>(config.options.size()) - 1);
            changed = true;
        }
    }

    if (!config.options.empty()) {
        config.selectedOptionIndex = std::clamp(config.selectedOptionIndex, 0, static_cast<int>(config.options.size()) - 1);
        changed |= ImGui::SliderInt("Selected Option Index", &config.selectedOptionIndex, 0,
                                    static_cast<int>(config.options.size()) - 1);

        const int objectId = ctx.object ? ctx.object->id : -1;
        int& selected = g_selectedOptionEditorIndex[objectId];
        selected = std::clamp(selected, 0, static_cast<int>(config.options.size()) - 1);

        ImGui::BeginChild("OptionList", ImVec2(230.0f, 180.0f), true);
        for (size_t i = 0; i < config.options.size(); ++i) {
            const std::string label = std::to_string(i + 1) + ". " +
                (config.options[i].optionName.empty() ? std::string("<Unnamed>") : config.options[i].optionName);
            if (ImGui::Selectable(label.c_str(), selected == static_cast<int>(i))) {
                selected = static_cast<int>(i);
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginGroup();

        InteractionOption& option = config.options[static_cast<size_t>(selected)];
        changed |= DrawStdStringInput("Option Name", option.optionName, 256);
        changed |= drawInteractionTypeCombo(option.interactionType);

        if (option.interactionType == InteractionType::Dialogue) {
            ImGui::Separator();
            ImGui::TextUnformatted("Dialogue Settings");
            changed |= DrawObjectRefInput(ctx, "Dialogue System Ref", option.dialogueSystemRef);

            int& selectedLine = g_selectedDialogueLineEditorIndex[objectId];
            changed |= drawDialogueLineEditor(ctx, option.dialogueLines, selectedLine);

            changed |= DrawObjectRefListEditor(ctx, "Dialogue End Enable", option.dialogueItemsToEnableOnEnd);
            changed |= DrawObjectRefListEditor(ctx, "Dialogue End Disable", option.dialogueItemsToDisableOnEnd);
        } else {
            ImGui::Separator();
            ImGui::TextUnformatted("Object Toggle Settings");
            changed |= DrawObjectRefListEditor(ctx, "Items To Enable", option.itemsToEnable);
            changed |= DrawObjectRefListEditor(ctx, "Items To Disable", option.itemsToDisable);
        }

        ImGui::EndGroup();
    } else {
        ImGui::TextDisabled("No options configured.");
    }

    if (changed) {
        saveConfig(ctx, config);
    }

    drawRuntimeStatus(config, &runtimeState);
}

void Begin(ScriptContext& ctx, float /*deltaTime*/) {
    if (!ctx.object) return;

    const InteractableConfig config = loadConfig(ctx);
    InteractableRuntimeState& state = g_runtimeStates[ctx.object->id];
    state.prevInteractDown = false;
    state.hasSelectionState = true;
    state.lastSelectionState = config.isSelected;
    state.lastRangeDistance = -1.0f;
    state.lastRangeInRange = false;
    state.lastRangeHasPlayer = false;
    state.lastPlayerWorldPos = glm::vec3(0.0f);
    state.lastSelfWorldPos = ctx.object ? getObjectReferencePosition(ctx, *ctx.object) : glm::vec3(0.0f);

    applySelectedState(ctx, config);
}

void TickUpdate(ScriptContext& ctx, float /*deltaTime*/) {
    if (!ctx.object) return;

    InteractableConfig config = loadConfig(ctx);
    InteractableRuntimeState& state = g_runtimeStates[ctx.object->id];

    if (!state.hasSelectionState || state.lastSelectionState != config.isSelected) {
        applySelectedState(ctx, config);
        state.hasSelectionState = true;
        state.lastSelectionState = config.isSelected;
    }

    isPlayerInRange(ctx, config, &state);

    const bool interactDown = isInteractDown();
    const bool interactPressed = interactDown && !state.prevInteractDown;
    state.prevInteractDown = interactDown;

    if (!config.interactOnKeyPress || !interactPressed) {
        return;
    }

    if (interact(ctx, config, &state)) {
        saveConfig(ctx, config);
    }
}
