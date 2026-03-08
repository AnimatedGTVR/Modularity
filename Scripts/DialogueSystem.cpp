#include "DialoguePortShared.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using namespace DialoguePort;

struct DialogueConfig {
    std::string characterNameTextRef;
    std::string dialogueTextRef;
    std::string playerRef;

    Language currentLanguage = Language::English;
    float pitchVariation = 0.1f;
    float openAnimationDelay = 0.3f;
    float closeAnimationDelay = 0.3f;
    float spacingFactor = 0.1f;
    float sizeMultiplier = 2.0f;

    std::string characterSoundClip;
    std::string triggerSoundClip;
    std::string enterSoundClip;
    std::string exitSoundClip;
    std::string skipSoundClip;

    bool rigidSimulated = true;
    bool disableSelfOnEnd = true;
    bool autoOpenOnBegin = false;

    std::vector<std::string> itemsToEnable;
    std::vector<std::string> itemsToDisable;
    std::vector<DialogueLine> dialogueLines;
};

struct DialogueRuntimeState {
    bool running = false;
    bool isTyping = false;
    bool isTextFullyDisplayed = false;
    bool autoOpened = false;

    int index = 0;
    size_t revealedCharacters = 0;
    float typeAccumulator = 0.0f;
    float holdDelay = 0.0f;
    float effectTimer = 0.0f;
    float soundCooldown = 0.0f;
    float openDelayRemaining = 0.0f;
    float closeDelayRemaining = 0.0f;
    bool pendingClose = false;

    int lastInteractionRequestSerial = 0;
    bool prevSubmitDown = false;

    std::string currentCleanSentence;
    std::string currentPlayerRef;

    std::vector<DialogueLine> activeLines;
    std::vector<std::string> endItemsToEnable;
    std::vector<std::string> endItemsToDisable;
};

std::unordered_map<int, DialogueRuntimeState> g_runtimeStates;
std::unordered_map<int, int> g_selectedLineByObject;

constexpr const char* kSettingCharacterNameRef = "dialogue.characterNameTextRef";
constexpr const char* kSettingDialogueTextRef = "dialogue.dialogueTextRef";
constexpr const char* kSettingPlayerRef = "dialogue.playerRef";
constexpr const char* kSettingLanguage = "dialogue.language";
constexpr const char* kSettingPitchVariation = "dialogue.pitchVariation";
constexpr const char* kSettingOpenDelay = "dialogue.openAnimationDelay";
constexpr const char* kSettingCloseDelay = "dialogue.closeAnimationDelay";
constexpr const char* kSettingSpacingFactor = "dialogue.spacingFactor";
constexpr const char* kSettingSizeMultiplier = "dialogue.sizeMultiplier";
constexpr const char* kSettingCharacterSound = "dialogue.characterSoundClip";
constexpr const char* kSettingTriggerSound = "dialogue.triggerSoundClip";
constexpr const char* kSettingEnterSound = "dialogue.enterSoundClip";
constexpr const char* kSettingExitSound = "dialogue.exitSoundClip";
constexpr const char* kSettingSkipSound = "dialogue.skipSoundClip";
constexpr const char* kSettingRigidSimulated = "dialogue.rigidSimulated";
constexpr const char* kSettingDisableOnEnd = "dialogue.disableSelfOnEnd";
constexpr const char* kSettingAutoOpenOnBegin = "dialogue.autoOpenOnBegin";
constexpr const char* kSettingItemsEnable = "dialogue.itemsToEnable";
constexpr const char* kSettingItemsDisable = "dialogue.itemsToDisable";
constexpr const char* kSettingLines = "dialogue.lines";

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

DialogueConfig loadConfig(ScriptContext& ctx) {
    DialogueConfig config;

    config.characterNameTextRef = ctx.GetSetting(kSettingCharacterNameRef, "");
    config.dialogueTextRef = ctx.GetSetting(kSettingDialogueTextRef, "");
    config.playerRef = ctx.GetSetting(kSettingPlayerRef, "");

    config.currentLanguage = static_cast<Language>(
        std::clamp(ParseInt(ctx.GetSetting(kSettingLanguage, "0"), 0), 0, 2));

    config.pitchVariation = ParseFloat(ctx.GetSetting(kSettingPitchVariation, "0.1"), 0.1f);
    config.openAnimationDelay = std::max(0.0f, ParseFloat(ctx.GetSetting(kSettingOpenDelay, "0.3"), 0.3f));
    config.closeAnimationDelay = std::max(0.0f, ParseFloat(ctx.GetSetting(kSettingCloseDelay, "0.3"), 0.3f));
    config.spacingFactor = ParseFloat(ctx.GetSetting(kSettingSpacingFactor, "0.1"), 0.1f);
    config.sizeMultiplier = std::max(0.1f, ParseFloat(ctx.GetSetting(kSettingSizeMultiplier, "2.0"), 2.0f));

    config.characterSoundClip = ctx.GetSetting(kSettingCharacterSound, "");
    config.triggerSoundClip = ctx.GetSetting(kSettingTriggerSound, "");
    config.enterSoundClip = ctx.GetSetting(kSettingEnterSound, "");
    config.exitSoundClip = ctx.GetSetting(kSettingExitSound, "");
    config.skipSoundClip = ctx.GetSetting(kSettingSkipSound, "");

    config.rigidSimulated = ParseBool(ctx.GetSetting(kSettingRigidSimulated, "1"), true);
    config.disableSelfOnEnd = ParseBool(ctx.GetSetting(kSettingDisableOnEnd, "1"), true);
    config.autoOpenOnBegin = ParseBool(ctx.GetSetting(kSettingAutoOpenOnBegin, "0"), false);

    config.itemsToEnable = DeserializeObjectRefs(ctx.GetSetting(kSettingItemsEnable, ""));
    config.itemsToDisable = DeserializeObjectRefs(ctx.GetSetting(kSettingItemsDisable, ""));
    config.dialogueLines = DeserializeDialogueLines(ctx.GetSetting(kSettingLines, ""));

    return config;
}

void saveConfig(ScriptContext& ctx, const DialogueConfig& config) {
    setSettingIfChanged(ctx, kSettingCharacterNameRef, config.characterNameTextRef);
    setSettingIfChanged(ctx, kSettingDialogueTextRef, config.dialogueTextRef);
    setSettingIfChanged(ctx, kSettingPlayerRef, config.playerRef);

    setSettingIfChanged(ctx, kSettingLanguage, std::to_string(static_cast<int>(config.currentLanguage)));
    setSettingIfChanged(ctx, kSettingPitchVariation, std::to_string(config.pitchVariation));
    setSettingIfChanged(ctx, kSettingOpenDelay, std::to_string(config.openAnimationDelay));
    setSettingIfChanged(ctx, kSettingCloseDelay, std::to_string(config.closeAnimationDelay));
    setSettingIfChanged(ctx, kSettingSpacingFactor, std::to_string(config.spacingFactor));
    setSettingIfChanged(ctx, kSettingSizeMultiplier, std::to_string(config.sizeMultiplier));

    setSettingIfChanged(ctx, kSettingCharacterSound, config.characterSoundClip);
    setSettingIfChanged(ctx, kSettingTriggerSound, config.triggerSoundClip);
    setSettingIfChanged(ctx, kSettingEnterSound, config.enterSoundClip);
    setSettingIfChanged(ctx, kSettingExitSound, config.exitSoundClip);
    setSettingIfChanged(ctx, kSettingSkipSound, config.skipSoundClip);

    setSettingIfChanged(ctx, kSettingRigidSimulated, config.rigidSimulated ? "1" : "0");
    setSettingIfChanged(ctx, kSettingDisableOnEnd, config.disableSelfOnEnd ? "1" : "0");
    setSettingIfChanged(ctx, kSettingAutoOpenOnBegin, config.autoOpenOnBegin ? "1" : "0");

    setSettingIfChanged(ctx, kSettingItemsEnable, SerializeObjectRefs(config.itemsToEnable));
    setSettingIfChanged(ctx, kSettingItemsDisable, SerializeObjectRefs(config.itemsToDisable));
    setSettingIfChanged(ctx, kSettingLines, SerializeDialogueLines(config.dialogueLines));
}

bool hasDialogueLines(const std::vector<DialogueLine>& lines) {
    return !lines.empty();
}

std::string getSentenceForLanguage(const DialogueLine& line, Language language) {
    switch (language) {
        case Language::English:
            return line.sentence;
        case Language::German:
            return line.sentenceGerman.empty() ? line.sentence : line.sentenceGerman;
        case Language::JapaneseKana:
            return line.sentenceJapaneseKana.empty() ? line.sentence : line.sentenceJapaneseKana;
        default:
            return line.sentence;
    }
}

std::string parseSentenceForDisplay(const std::string& sentence) {
    static const std::regex taggedWordPattern(R"(\(\[(\w+),\s*([\d.]+),\s*([\d.]+),\](.*?)\))");

    std::string clean;
    clean.reserve(sentence.size());

    size_t lastIndex = 0;
    for (std::sregex_iterator it(sentence.begin(), sentence.end(), taggedWordPattern), end; it != end; ++it) {
        const std::smatch& match = *it;
        const size_t matchPos = static_cast<size_t>(match.position());
        const size_t matchLen = static_cast<size_t>(match.length());

        if (matchPos > lastIndex) {
            clean += sentence.substr(lastIndex, matchPos - lastIndex);
        }

        if (match.size() >= 5) {
            clean += match[4].str();
        }

        lastIndex = matchPos + matchLen;
    }

    if (lastIndex < sentence.size()) {
        clean += sentence.substr(lastIndex);
    }

    return clean;
}

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

int findAnimationClipIndexByName(SceneObject& object, const std::string& desiredName) {
    if (!object.hasAnimation || !object.animation.enabled) return -1;
    NormalizeAnimationClipSlots(object.animation);
    if (object.animation.clips.empty()) return -1;

    const std::string needle = toLowerAscii(Trim(desiredName));
    for (int i = 0; i < static_cast<int>(object.animation.clips.size()); ++i) {
        const AnimationClipSlot& clip = object.animation.clips[static_cast<size_t>(i)];
        std::string clipName = clip.name.empty() ? AnimationClipNameFromPath(clip.assetPath) : clip.name;
        if (toLowerAscii(Trim(clipName)) == needle) {
            return i;
        }
    }
    return -1;
}

bool playDialogueStateAnimation(ScriptContext& ctx, const std::string& clipName) {
    if (!ctx.object || !ctx.object->hasAnimation || !ctx.object->animation.enabled) return false;
    SceneObject& object = *ctx.object;
    int clipIndex = findAnimationClipIndexByName(object, clipName);
    if (clipIndex < 0) return false;

    object.animation.activeClipIndex = clipIndex;
    object.animation.clipAssetPath = object.animation.clips[static_cast<size_t>(clipIndex)].assetPath;
    object.animation.runtimeClipPath.clear();
    object.animation.runtimeInitialized = false;
    object.animation.runtimePaused = false;
    object.animation.runtimeDirection = 1.0f;
    return ctx.PlayAnimation(true);
}

enum class MouthState {
    TalkingOpen,
    TalkingClosed,
    NotTalking
};

void applyMouthState(ScriptContext& ctx, const DialogueLine& line, MouthState mouthState) {
    const bool isOpen = (mouthState == MouthState::TalkingOpen);
    const bool isClosed = (mouthState == MouthState::TalkingClosed);
    const bool isIdle = (mouthState == MouthState::NotTalking);

    SceneObject* openObj = ResolveSceneObjectRef(ctx, line.openMouthObjectRef);
    SceneObject* closedObj = ResolveSceneObjectRef(ctx, line.closedMouthObjectRef);
    SceneObject* idleObj = ResolveSceneObjectRef(ctx, line.notTalkingObjectRef);

    SetObjectEnabledState(ctx, openObj, isOpen);
    SetObjectEnabledState(ctx, closedObj, isClosed);
    SetObjectEnabledState(ctx, idleObj, isIdle);
}

void resetTextWidgets(ScriptContext& ctx, const DialogueConfig& config) {
    SetUITextLabel(ctx, config.characterNameTextRef, "");
    SetUITextLabel(ctx, config.dialogueTextRef, "");
    SetUITextEffects(ctx, config.dialogueTextRef, TextEffectType::None, 1.0f, 0.0f);
}

void revealDialogueText(ScriptContext& ctx, const DialogueConfig& config, DialogueRuntimeState& state) {
    const size_t shown = std::min(state.revealedCharacters, state.currentCleanSentence.size());
    SetUITextLabel(ctx, config.dialogueTextRef, state.currentCleanSentence.substr(0, shown));
}

void startLine(ScriptContext& ctx, DialogueRuntimeState& state, const DialogueConfig& config) {
    if (state.index < 0 || state.index >= static_cast<int>(state.activeLines.size())) {
        state.running = false;
        state.isTyping = false;
        state.isTextFullyDisplayed = false;
        return;
    }

    const DialogueLine& line = state.activeLines[static_cast<size_t>(state.index)];
    SetObjectsEnabledState(ctx, line.itemsToDisable, false);
    SetObjectsEnabledState(ctx, line.itemsToEnable, true);

    SetUITextLabel(ctx, config.characterNameTextRef, line.characterName);

    state.currentCleanSentence = parseSentenceForDisplay(getSentenceForLanguage(line, config.currentLanguage));
    state.revealedCharacters = 0;
    state.typeAccumulator = 0.0f;
    state.holdDelay = 0.0f;
    state.soundCooldown = 0.0f;
    state.isTyping = true;
    state.isTextFullyDisplayed = false;

    revealDialogueText(ctx, config, state);
    SetUITextEffects(ctx, config.dialogueTextRef, line.textEffect, line.animationSpeed, line.effectIntensity);
    applyMouthState(ctx, line, MouthState::TalkingClosed);
}

void completeTyping(ScriptContext& ctx, DialogueRuntimeState& state, const DialogueConfig& config) {
    if (state.index < 0 || state.index >= static_cast<int>(state.activeLines.size())) return;

    const DialogueLine& line = state.activeLines[static_cast<size_t>(state.index)];
    state.revealedCharacters = state.currentCleanSentence.size();
    state.isTyping = false;
    state.isTextFullyDisplayed = true;
    state.holdDelay = 0.0f;
    state.typeAccumulator = 0.0f;

    revealDialogueText(ctx, config, state);
    applyMouthState(ctx, line, MouthState::NotTalking);
}

void finalizeDialogueEnd(ScriptContext& ctx, DialogueRuntimeState& state, const DialogueConfig& config) {
    SetObjectsEnabledState(ctx, state.endItemsToDisable, false);
    SetObjectsEnabledState(ctx, state.endItemsToEnable, true);

    SetRigidbody2DSimulated(ctx, state.currentPlayerRef, config.rigidSimulated);

    resetTextWidgets(ctx, config);

    state.running = false;
    state.pendingClose = false;
    state.closeDelayRemaining = 0.0f;
    state.isTyping = false;
    state.isTextFullyDisplayed = false;
    state.revealedCharacters = 0;
    state.currentCleanSentence.clear();
    if (config.disableSelfOnEnd) {
        state.autoOpened = false;
    }

    if (config.disableSelfOnEnd) {
        ctx.SetObjectEnabled(false);
    }
}

void beginDialogueClose(ScriptContext& ctx, DialogueRuntimeState& state, const DialogueConfig& config) {
    if (state.pendingClose) return;
    state.pendingClose = true;
    state.closeDelayRemaining = std::max(0.0f, config.closeAnimationDelay);
    playDialogueStateAnimation(ctx, "DialogueStateClose");

    if (state.closeDelayRemaining <= 0.0f) {
        finalizeDialogueEnd(ctx, state, config);
    }
}

void openDialogue(ScriptContext& ctx,
                  DialogueRuntimeState& state,
                  const DialogueConfig& config,
                  const std::vector<DialogueLine>& lines,
                  const std::vector<std::string>& itemsToEnableOnEnd,
                  const std::vector<std::string>& itemsToDisableOnEnd,
                  const std::string& playerRef) {
    if (!hasDialogueLines(lines)) {
        ctx.AddConsoleMessage("DialogueSystem: no dialogue lines configured.", ConsoleMessageType::Warning);
        return;
    }

    state.activeLines = lines;
    state.endItemsToEnable = itemsToEnableOnEnd;
    state.endItemsToDisable = itemsToDisableOnEnd;
    state.currentPlayerRef = playerRef.empty() ? config.playerRef : playerRef;

    state.running = true;
    state.index = 0;
    state.revealedCharacters = 0;
    state.typeAccumulator = 0.0f;
    state.holdDelay = 0.0f;
    state.soundCooldown = 0.0f;
    state.effectTimer = 0.0f;
    state.isTyping = false;
    state.isTextFullyDisplayed = false;
    state.pendingClose = false;
    state.closeDelayRemaining = 0.0f;
    state.currentCleanSentence.clear();
    state.openDelayRemaining = config.openAnimationDelay;

    resetTextWidgets(ctx, config);
    SetRigidbody2DSimulated(ctx, state.currentPlayerRef, false);
    playDialogueStateAnimation(ctx, "DialogueStateOpen");

    if (!config.triggerSoundClip.empty()) {
        ctx.PlayAudioOneShot(config.triggerSoundClip);
    }

    if (state.openDelayRemaining <= 0.0f) {
        startLine(ctx, state, config);
    }
}

bool isSubmitDown() {
    return IsRuntimeKeyDown(GLFW_KEY_ENTER, ImGuiKey_Enter) ||
           IsRuntimeKeyDown(GLFW_KEY_KP_ENTER, ImGuiKey_KeypadEnter);
}

void tickTyping(ScriptContext& ctx, DialogueRuntimeState& state, const DialogueConfig& config, float deltaTime) {
    if (!state.isTyping) return;
    if (state.index < 0 || state.index >= static_cast<int>(state.activeLines.size())) return;

    const DialogueLine& line = state.activeLines[static_cast<size_t>(state.index)];

    if (state.currentCleanSentence.empty()) {
        completeTyping(ctx, state, config);
        return;
    }

    const float typingSpeed = std::max(0.001f, line.typingSpeed);
    state.effectTimer += deltaTime;
    state.soundCooldown = std::max(0.0f, state.soundCooldown - deltaTime);

    float remaining = deltaTime;
    while (remaining > 0.0f && state.isTyping) {
        if (state.holdDelay > 0.0f) {
            const float consume = std::min(remaining, state.holdDelay);
            state.holdDelay -= consume;
            remaining -= consume;
            continue;
        }

        state.typeAccumulator += remaining;
        remaining = 0.0f;

        bool revealedAny = false;
        while (state.typeAccumulator >= typingSpeed && state.revealedCharacters < state.currentCleanSentence.size()) {
            state.typeAccumulator -= typingSpeed;
            ++state.revealedCharacters;
            revealedAny = true;

            const size_t charIndex = state.revealedCharacters - 1;
            const char character = state.currentCleanSentence[charIndex];

            if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
                const std::string& clip = line.characterSoundClip.empty() ? config.characterSoundClip : line.characterSoundClip;
                if (!clip.empty() && state.soundCooldown <= 0.0f) {
                    ctx.PlayAudioOneShot(clip);
                    state.soundCooldown = std::max(0.01f, typingSpeed * 0.6f);
                }
            }

            if ((charIndex % 2U) == 0U) {
                applyMouthState(ctx, line, MouthState::TalkingOpen);
            } else {
                applyMouthState(ctx, line, MouthState::TalkingClosed);
            }

            if (character == ',' || character == ';') {
                state.holdDelay += typingSpeed * 3.0f;
            } else if (character == '.' || character == '!' || character == '?') {
                if (character == '.' && charIndex + 2 < state.currentCleanSentence.size() &&
                    state.currentCleanSentence[charIndex + 1] == '.' &&
                    state.currentCleanSentence[charIndex + 2] == '.') {
                    state.holdDelay += typingSpeed * 5.0f;
                } else {
                    state.holdDelay += typingSpeed * 5.0f;
                }
            }
        }

        if (revealedAny) {
            revealDialogueText(ctx, config, state);
        }

        if (state.revealedCharacters >= state.currentCleanSentence.size()) {
            completeTyping(ctx, state, config);
            break;
        }

        if (state.typeAccumulator < typingSpeed) {
            break;
        }
    }
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

    if (ImGui::Button("Add Line")) {
        if (!lines.empty()) {
            lines.push_back(lines.back());
        } else {
            lines.emplace_back();
        }
        selectedIndex = static_cast<int>(lines.size()) - 1;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove Line") && selectedIndex >= 0 && selectedIndex < static_cast<int>(lines.size())) {
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(selectedIndex));
        if (lines.empty()) selectedIndex = -1;
        else selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(lines.size()) - 1);
        changed = true;
    }

    if (lines.empty()) {
        ImGui::TextDisabled("No dialogue lines configured.");
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

void drawRuntimeStatus(const DialogueRuntimeState* state) {
    if (!state) {
        ImGui::TextDisabled("Runtime: idle");
        return;
    }

    ImGui::Separator();
    ImGui::Text("Runtime");
    ImGui::TextDisabled("Running: %s", state->running ? "Yes" : "No");
    ImGui::TextDisabled("Typing: %s", state->isTyping ? "Yes" : "No");
    ImGui::TextDisabled("Line: %d", state->index + 1);
    ImGui::TextDisabled("Chars: %zu / %zu", state->revealedCharacters, state->currentCleanSentence.size());
}

} // namespace

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    DialogueConfig config = loadConfig(ctx);
    bool changed = false;

    ImGui::TextUnformatted("DialogueSystem (Unity Port)");
    ImGui::Separator();

    changed |= DrawObjectRefInput(ctx, "Character Name Text Ref", config.characterNameTextRef);
    changed |= DrawObjectRefInput(ctx, "Dialogue Text Ref", config.dialogueTextRef);
    changed |= DrawObjectRefInput(ctx, "Player Ref", config.playerRef);

    changed |= DrawLanguageCombo("Language", config.currentLanguage);
    changed |= ImGui::DragFloat("Pitch Variation", &config.pitchVariation, 0.01f, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::DragFloat("Open Animation Delay", &config.openAnimationDelay, 0.01f, 0.0f, 10.0f, "%.2f");
    changed |= ImGui::DragFloat("Close Animation Delay", &config.closeAnimationDelay, 0.01f, 0.0f, 10.0f, "%.2f");
    changed |= ImGui::DragFloat("Spacing Factor", &config.spacingFactor, 0.01f, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::DragFloat("Size Multiplier", &config.sizeMultiplier, 0.01f, 0.1f, 10.0f, "%.2f");

    changed |= DrawAudioClipInput("Character Sound Clip", config.characterSoundClip, 512);
    changed |= DrawAudioClipInput("Trigger Sound Clip", config.triggerSoundClip, 512);
    changed |= DrawAudioClipInput("Enter Sound Clip", config.enterSoundClip, 512);
    changed |= DrawAudioClipInput("Exit Sound Clip", config.exitSoundClip, 512);
    changed |= DrawAudioClipInput("Skip Sound Clip", config.skipSoundClip, 512);

    changed |= ImGui::Checkbox("Player Simulated On End", &config.rigidSimulated);
    changed |= ImGui::Checkbox("Disable Self On End", &config.disableSelfOnEnd);
    changed |= ImGui::Checkbox("Auto Open On Begin", &config.autoOpenOnBegin);

    changed |= DrawObjectRefListEditor(ctx, "Items To Enable On End", config.itemsToEnable);
    changed |= DrawObjectRefListEditor(ctx, "Items To Disable On End", config.itemsToDisable);

    ImGui::Separator();
    ImGui::TextUnformatted("Dialogue Lines");

    const int objectId = ctx.object ? ctx.object->id : -1;
    int& selectedLine = g_selectedLineByObject[objectId];
    changed |= drawDialogueLineEditor(ctx, config.dialogueLines, selectedLine);

    if (changed) {
        saveConfig(ctx, config);
    }

    auto stateIt = g_runtimeStates.find(objectId);
    drawRuntimeStatus(stateIt != g_runtimeStates.end() ? &stateIt->second : nullptr);
}

void Begin(ScriptContext& ctx, float /*deltaTime*/) {
    if (!ctx.object) return;

    DialogueConfig config = loadConfig(ctx);
    DialogueRuntimeState& state = g_runtimeStates[ctx.object->id];

    state.running = false;
    state.isTyping = false;
    state.isTextFullyDisplayed = false;
    state.autoOpened = false;
    state.index = 0;
    state.revealedCharacters = 0;
    state.typeAccumulator = 0.0f;
    state.holdDelay = 0.0f;
    state.effectTimer = 0.0f;
    state.soundCooldown = 0.0f;
    state.openDelayRemaining = 0.0f;
    state.closeDelayRemaining = 0.0f;
    state.pendingClose = false;
    state.prevSubmitDown = false;
    const int interactionSerial = ParseInt(ctx.GetSetting(kInteractionRequestSerial, "0"), 0);
    const bool interactionPending = ParseBool(ctx.GetSetting(kInteractionRequestPending, "0"), false);
    state.lastInteractionRequestSerial = interactionPending ? (interactionSerial - 1) : interactionSerial;
    state.currentCleanSentence.clear();
    state.currentPlayerRef = config.playerRef;
    state.activeLines.clear();
    state.endItemsToEnable.clear();
    state.endItemsToDisable.clear();

    resetTextWidgets(ctx, config);

    if (config.autoOpenOnBegin && hasDialogueLines(config.dialogueLines)) {
        openDialogue(ctx, state, config, config.dialogueLines, config.itemsToEnable, config.itemsToDisable, config.playerRef);
        state.autoOpened = true;
    }
}

void TickUpdate(ScriptContext& ctx, float deltaTime) {
    if (!ctx.object) return;

    DialogueConfig config = loadConfig(ctx);
    DialogueRuntimeState& state = g_runtimeStates[ctx.object->id];

    const int interactionSerial = ParseInt(ctx.GetSetting(kInteractionRequestSerial, "0"), 0);
    const bool interactionPending = ParseBool(ctx.GetSetting(kInteractionRequestPending, "0"), false);
    if (interactionSerial != state.lastInteractionRequestSerial || interactionPending) {
        state.lastInteractionRequestSerial = interactionSerial;
        if (interactionPending) {
            ctx.SetSetting(kInteractionRequestPending, "0");
        }

        std::vector<DialogueLine> overrideLines = DeserializeDialogueLines(ctx.GetSetting(kInteractionOverrideLines, ""));
        std::vector<std::string> overrideEnable = DeserializeObjectRefs(ctx.GetSetting(kInteractionEndEnable, ""));
        std::vector<std::string> overrideDisable = DeserializeObjectRefs(ctx.GetSetting(kInteractionEndDisable, ""));
        std::string overridePlayerRef = ctx.GetSetting(kInteractionPlayerRef, "");

        if (overrideLines.empty()) overrideLines = config.dialogueLines;
        if (overrideEnable.empty()) overrideEnable = config.itemsToEnable;
        if (overrideDisable.empty()) overrideDisable = config.itemsToDisable;
        if (overridePlayerRef.empty()) overridePlayerRef = config.playerRef;

        openDialogue(ctx, state, config, overrideLines, overrideEnable, overrideDisable, overridePlayerRef);
    }

    if (!state.running && config.autoOpenOnBegin && !state.autoOpened && hasDialogueLines(config.dialogueLines)) {
        openDialogue(ctx, state, config, config.dialogueLines, config.itemsToEnable, config.itemsToDisable, config.playerRef);
        state.autoOpened = true;
    }

    if (!state.running) {
        return;
    }

    if (state.pendingClose) {
        state.closeDelayRemaining -= std::max(0.0f, deltaTime);
        if (state.closeDelayRemaining <= 0.0f) {
            finalizeDialogueEnd(ctx, state, config);
        }
        return;
    }

    const bool submitDown = isSubmitDown();
    const bool submitPressed = submitDown && !state.prevSubmitDown;
    state.prevSubmitDown = submitDown;

    if (state.openDelayRemaining > 0.0f) {
        state.openDelayRemaining -= std::max(0.0f, deltaTime);
        if (state.openDelayRemaining <= 0.0f) {
            if (!config.enterSoundClip.empty()) {
                ctx.PlayAudioOneShot(config.enterSoundClip);
            }
            startLine(ctx, state, config);
        }
        return;
    }

    tickTyping(ctx, state, config, std::max(0.0f, deltaTime));

    if (submitPressed) {
        if (state.isTyping) {
            completeTyping(ctx, state, config);
            if (!config.skipSoundClip.empty()) {
                ctx.PlayAudioOneShot(config.skipSoundClip);
            }
        } else if (state.isTextFullyDisplayed) {
            if (state.index < static_cast<int>(state.activeLines.size()) - 1) {
                ++state.index;
                if (!config.enterSoundClip.empty()) {
                    ctx.PlayAudioOneShot(config.enterSoundClip);
                }
                startLine(ctx, state, config);
            } else {
                if (!config.exitSoundClip.empty()) {
                    ctx.PlayAudioOneShot(config.exitSoundClip);
                }
                beginDialogueClose(ctx, state, config);
            }
        }
    }
}
