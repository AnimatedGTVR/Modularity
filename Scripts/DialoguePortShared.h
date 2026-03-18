#pragma once

#include "ModuCPP"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

namespace DialoguePort {

enum class Language {
    English = 0,
    German = 1,
    JapaneseKana = 2
};

enum class TextEffectType : int {
    None = 0,
    Wave = 1 << 0,
    Shake = 1 << 1,
    Bounce = 1 << 2,
    Rotate = 1 << 3,
    Fade = 1 << 4
};

struct DialogueLine {
    std::string characterName;
    std::string sentence;
    std::string sentenceGerman;
    std::string sentenceJapaneseKana;
    std::string characterSoundClip;
    float typingSpeed = 0.03f;
    TextEffectType textEffect = TextEffectType::None;
    float animationSpeed = 1.0f;
    float effectIntensity = 1.0f;
    std::string notTalkingObjectRef;
    std::string openMouthObjectRef;
    std::string closedMouthObjectRef;
    std::vector<std::string> itemsToEnable;
    std::vector<std::string> itemsToDisable;
};

inline std::string Trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(start, end - start);
}

inline int ParseInt(const std::string& value, int fallback = 0) {
    if (value.empty()) return fallback;
    char* endPtr = nullptr;
    long parsed = std::strtol(value.c_str(), &endPtr, 10);
    if (endPtr == value.c_str()) return fallback;
    if (parsed < std::numeric_limits<int>::min()) return std::numeric_limits<int>::min();
    if (parsed > std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
    return static_cast<int>(parsed);
}

inline float ParseFloat(const std::string& value, float fallback = 0.0f) {
    if (value.empty()) return fallback;
    char* endPtr = nullptr;
    float parsed = std::strtof(value.c_str(), &endPtr);
    if (endPtr == value.c_str()) return fallback;
    return parsed;
}

inline bool ParseBool(const std::string& value, bool fallback = false) {
    std::string lowered = Trim(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") return true;
    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") return false;
    return fallback;
}

inline std::string GetScriptSetting(const ScriptComponent* script, const std::string& key,
                                    const std::string& fallback = "") {
    if (!script) return fallback;
    for (const ScriptSetting& setting : script->settings) {
        if (setting.key == key) {
            return setting.value;
        }
    }
    return fallback;
}

inline bool SetScriptSetting(ScriptComponent* script, const std::string& key, const std::string& value) {
    if (!script) return false;
    for (ScriptSetting& setting : script->settings) {
        if (setting.key == key) {
            if (setting.value == value) return false;
            setting.value = value;
            return true;
        }
    }
    script->settings.push_back(ScriptSetting{key, value});
    return true;
}

inline std::string EscapeField(const std::string& value, char delimiter) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        if (c == '\\' || c == delimiter || c == '\n' || c == '\r') {
            out.push_back('\\');
            if (c == '\n') out.push_back('n');
            else if (c == '\r') out.push_back('r');
            else out.push_back(c);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

inline std::string UnescapeField(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    bool escaped = false;
    for (char c : value) {
        if (escaped) {
            if (c == 'n') out.push_back('\n');
            else if (c == 'r') out.push_back('\r');
            else out.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        out.push_back(c);
    }
    if (escaped) out.push_back('\\');
    return out;
}

inline std::vector<std::string> SplitEscaped(const std::string& value, char delimiter) {
    std::vector<std::string> fields;
    std::string current;
    bool escaped = false;
    for (char c : value) {
        if (escaped) {
            if (c == 'n') current.push_back('\n');
            else if (c == 'r') current.push_back('\r');
            else current.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == delimiter) {
            fields.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    if (escaped) current.push_back('\\');
    fields.push_back(current);
    return fields;
}

inline std::string JoinEscaped(const std::vector<std::string>& values, char delimiter) {
    std::string joined;
    for (size_t i = 0; i < values.size(); ++i) {
        joined += EscapeField(values[i], delimiter);
        if (i + 1 < values.size()) joined.push_back(delimiter);
    }
    return joined;
}

inline std::vector<std::string> DeserializeObjectRefs(const std::string& encoded) {
    std::vector<std::string> refs;
    if (encoded.empty()) return refs;
    std::vector<std::string> parsed = SplitEscaped(encoded, ';');
    refs.reserve(parsed.size());
    for (const std::string& item : parsed) {
        std::string trimmed = Trim(item);
        if (!trimmed.empty()) refs.push_back(trimmed);
    }
    return refs;
}

inline std::string SerializeObjectRefs(const std::vector<std::string>& refs) {
    std::vector<std::string> cleaned;
    cleaned.reserve(refs.size());
    for (const std::string& ref : refs) {
        std::string trimmed = Trim(ref);
        if (!trimmed.empty()) cleaned.push_back(trimmed);
    }
    return JoinEscaped(cleaned, ';');
}

inline std::string SerializeDialogueLines(const std::vector<DialogueLine>& lines) {
    std::vector<std::string> encodedLines;
    encodedLines.reserve(lines.size());
    for (const DialogueLine& line : lines) {
        std::vector<std::string> fields;
        fields.reserve(14);
        fields.push_back(line.characterName);
        fields.push_back(line.sentence);
        fields.push_back(line.sentenceGerman);
        fields.push_back(line.sentenceJapaneseKana);
        fields.push_back(line.characterSoundClip);
        fields.push_back(std::to_string(line.typingSpeed));
        fields.push_back(std::to_string(static_cast<int>(line.textEffect)));
        fields.push_back(std::to_string(line.animationSpeed));
        fields.push_back(std::to_string(line.effectIntensity));
        fields.push_back(line.notTalkingObjectRef);
        fields.push_back(line.openMouthObjectRef);
        fields.push_back(line.closedMouthObjectRef);
        fields.push_back(SerializeObjectRefs(line.itemsToEnable));
        fields.push_back(SerializeObjectRefs(line.itemsToDisable));
        encodedLines.push_back(JoinEscaped(fields, '|'));
    }
    // Use tab as the outer delimiter so values stay single-line in scene files.
    return JoinEscaped(encodedLines, '\t');
}

inline std::vector<DialogueLine> DeserializeDialogueLines(const std::string& encoded) {
    std::vector<DialogueLine> lines;
    if (encoded.empty()) return lines;

    std::vector<std::string> rawLines = SplitEscaped(encoded, '\t');
    // Backward compatibility for older saved data that used newline delimiters.
    if (rawLines.size() <= 1 && encoded.find('\t') == std::string::npos &&
        encoded.find('\n') != std::string::npos) {
        rawLines = SplitEscaped(encoded, '\n');
    }
    lines.reserve(rawLines.size());

    for (const std::string& rawLine : rawLines) {
        if (Trim(rawLine).empty()) continue;
        std::vector<std::string> fields = SplitEscaped(rawLine, '|');
        if (fields.empty()) continue;

        DialogueLine line;
        if (fields.size() > 0) line.characterName = fields[0];
        if (fields.size() > 1) line.sentence = fields[1];
        if (fields.size() > 2) line.sentenceGerman = fields[2];
        if (fields.size() > 3) line.sentenceJapaneseKana = fields[3];
        if (fields.size() > 4) line.characterSoundClip = fields[4];
        if (fields.size() > 5) line.typingSpeed = ParseFloat(fields[5], line.typingSpeed);
        if (fields.size() > 6) line.textEffect = static_cast<TextEffectType>(ParseInt(fields[6], 0));
        if (fields.size() > 7) line.animationSpeed = ParseFloat(fields[7], line.animationSpeed);
        if (fields.size() > 8) line.effectIntensity = ParseFloat(fields[8], line.effectIntensity);
        if (fields.size() > 9) line.notTalkingObjectRef = fields[9];
        if (fields.size() > 10) line.openMouthObjectRef = fields[10];
        if (fields.size() > 11) line.closedMouthObjectRef = fields[11];
        if (fields.size() > 12) line.itemsToEnable = DeserializeObjectRefs(fields[12]);
        if (fields.size() > 13) line.itemsToDisable = DeserializeObjectRefs(fields[13]);

        lines.push_back(std::move(line));
    }

    return lines;
}

inline std::string MakeObjectRef(int objectId) {
    return std::string("Object.ID-") + std::to_string(objectId);
}

inline bool IsAllDigits(const std::string& value) {
    if (value.empty()) return false;
    size_t start = (value[0] == '-' || value[0] == '+') ? 1 : 0;
    if (start >= value.size()) return false;
    for (size_t i = start; i < value.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(value[i]))) return false;
    }
    return true;
}

inline SceneObject* ResolveSceneObjectRef(ScriptContext& ctx, const std::string& objectRef) {
    std::string trimmed = Trim(objectRef);
    if (trimmed.empty()) return nullptr;

    if (SceneObject* resolved = ctx.ResolveObjectRef(trimmed)) {
        return resolved;
    }

    if (IsAllDigits(trimmed)) {
        return ctx.FindObjectById(ParseInt(trimmed, -1));
    }

    return ctx.FindObjectByName(trimmed);
}

inline bool SetObjectEnabledState(ScriptContext& ctx, SceneObject* object, bool enabled) {
    if (!object) return false;
    if (object->enabled == enabled) return false;
    object->enabled = enabled;
    ctx.MarkDirty();
    return true;
}

inline bool SetObjectsEnabledState(ScriptContext& ctx, const std::vector<std::string>& refs, bool enabled) {
    bool changed = false;
    for (const std::string& ref : refs) {
        SceneObject* object = ResolveSceneObjectRef(ctx, ref);
        if (SetObjectEnabledState(ctx, object, enabled)) {
            changed = true;
        }
    }
    return changed;
}

inline SceneObject* ResolveUITextTarget(ScriptContext& ctx, const std::string& objectRef) {
    SceneObject* object = ResolveSceneObjectRef(ctx, objectRef);
    if (!object) return nullptr;
    if (HasUIComponent(*object) && object->ui.type == UIElementType::Text) {
        return object;
    }

    std::vector<int> pendingChildIds = object->childIds;
    int childGuard = 0;
    while (!pendingChildIds.empty() && childGuard < 512) {
        const int childId = pendingChildIds.back();
        pendingChildIds.pop_back();
        ++childGuard;
        SceneObject* child = ctx.FindObjectById(childId);
        if (!child) continue;
        if (HasUIComponent(*child) && child->ui.type == UIElementType::Text) {
            return child;
        }
        pendingChildIds.insert(pendingChildIds.end(), child->childIds.begin(), child->childIds.end());
    }

    int parentId = object->parentId;
    int parentGuard = 0;
    while (parentId >= 0 && parentGuard < 256) {
        ++parentGuard;
        SceneObject* parent = ctx.FindObjectById(parentId);
        if (!parent) break;
        if (HasUIComponent(*parent) && parent->ui.type == UIElementType::Text) {
            return parent;
        }
        parentId = parent->parentId;
    }

    return nullptr;
}

inline bool SetUITextLabel(ScriptContext& ctx, const std::string& objectRef, const std::string& label) {
    SceneObject* object = ResolveUITextTarget(ctx, objectRef);
    if (!object) return false;
    if (object->ui.label == label) return false;
    object->ui.label = label;
    ctx.MarkDirty();
    return true;
}

inline bool SetUITextEffects(ScriptContext& ctx,
                             const std::string& objectRef,
                             TextEffectType effect,
                             float animationSpeed,
                             float effectIntensity) {
    SceneObject* object = ResolveUITextTarget(ctx, objectRef);
    if (!object) return false;

    const int flags = static_cast<int>(effect);
    const float speed = std::max(0.01f, animationSpeed);
    const float intensity = std::max(0.0f, effectIntensity);

    bool changed = false;
    if (object->ui.textEffectFlags != flags) {
        object->ui.textEffectFlags = flags;
        changed = true;
    }
    if (std::abs(object->ui.textEffectSpeed - speed) > 1e-5f) {
        object->ui.textEffectSpeed = speed;
        changed = true;
    }
    if (std::abs(object->ui.textEffectIntensity - intensity) > 1e-5f) {
        object->ui.textEffectIntensity = intensity;
        changed = true;
    }

    if (changed) {
        ctx.MarkDirty();
    }
    return changed;
}

inline bool SetRigidbody2DSimulated(ScriptContext& ctx, const std::string& objectRef, bool simulated) {
    SceneObject* object = ResolveSceneObjectRef(ctx, objectRef);
    if (!object || !object->hasRigidbody2D) return false;
    if (object->rigidbody2D.enabled == simulated) return false;
    object->rigidbody2D.enabled = simulated;
    ctx.MarkDirty();
    return true;
}

inline bool DrawStdStringInput(const char* label, std::string& value,
                               size_t capacity = 512,
                               ImGuiInputTextFlags flags = 0,
                               bool multiline = false,
                               float multilineHeight = 90.0f) {
    std::vector<char> buffer(capacity + 1, '\0');
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    bool edited = false;
    if (multiline) {
        edited = ImGui::InputTextMultiline(label, buffer.data(), buffer.size(),
                                           ImVec2(-FLT_MIN, multilineHeight), flags);
    } else {
        edited = ImGui::InputText(label, buffer.data(), buffer.size(), flags);
    }
    if (edited) {
        value = buffer.data();
    }
    return edited;
}

inline bool DrawObjectRefInput(ScriptContext& ctx, const char* label, std::string& objectRef) {
    bool changed = DrawStdStringInput(label, objectRef, 256);

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            if (payload->Data && payload->DataSize == static_cast<int>(sizeof(int))) {
                const int droppedId = *static_cast<const int*>(payload->Data);
                const std::string newRef = MakeObjectRef(droppedId);
                if (objectRef != newRef) {
                    objectRef = newRef;
                    changed = true;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    SceneObject* resolved = ResolveSceneObjectRef(ctx, objectRef);
    if (resolved) {
        ImGui::TextDisabled("-> %s (id=%d)", resolved->name.c_str(), resolved->id);
    } else if (!Trim(objectRef).empty()) {
        ImGui::TextDisabled("-> unresolved");
    }

    return changed;
}

inline bool IsAudioClipPath(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= path.size()) return false;
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == "wav" || ext == "mp3" || ext == "ogg" || ext == "flac" ||
           ext == "aac" || ext == "m4a";
}

inline bool DrawAudioClipInput(const char* label, std::string& clipPath, size_t capacity = 512) {
    bool changed = DrawStdStringInput(label, clipPath, capacity);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
            if (payload->Data && payload->DataSize > 0) {
                const char* droppedPath = static_cast<const char*>(payload->Data);
                if (droppedPath) {
                    std::string candidate = droppedPath;
                    if (IsAudioClipPath(candidate) && clipPath != candidate) {
                        clipPath = std::move(candidate);
                        changed = true;
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    return changed;
}

inline bool DrawObjectRefListEditor(ScriptContext& ctx, const char* label, std::vector<std::string>& refs) {
    bool changed = false;
    if (!ImGui::TreeNode(label)) {
        return false;
    }

    for (size_t i = 0; i < refs.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(-80.0f);
        changed |= DrawStdStringInput("##ref", refs[i], 256);
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            refs.erase(refs.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
            ImGui::PopID();
            --i;
            continue;
        }

        SceneObject* resolved = ResolveSceneObjectRef(ctx, refs[i]);
        if (resolved) {
            ImGui::TextDisabled("%s (id=%d)", resolved->name.c_str(), resolved->id);
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
                if (payload->Data && payload->DataSize == static_cast<int>(sizeof(int))) {
                    const int droppedId = *static_cast<const int*>(payload->Data);
                    refs[i] = MakeObjectRef(droppedId);
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PopID();
    }

    if (ImGui::Button("Add Reference")) {
        refs.emplace_back();
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Selected")) {
        const int selectedId = ctx.GetSelectedObjectId();
        if (selectedId >= 0) {
            refs.push_back(MakeObjectRef(selectedId));
            changed = true;
        }
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            if (payload->Data && payload->DataSize == static_cast<int>(sizeof(int))) {
                const int droppedId = *static_cast<const int*>(payload->Data);
                refs.push_back(MakeObjectRef(droppedId));
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::TreePop();
    return changed;
}

inline const char* LanguageLabel(Language language) {
    switch (language) {
        case Language::English: return "English";
        case Language::German: return "German";
        case Language::JapaneseKana: return "Japanese Kana";
        default: return "English";
    }
}

inline bool DrawLanguageCombo(const char* label, Language& language) {
    bool changed = false;
    if (ImGui::BeginCombo(label, LanguageLabel(language))) {
        std::array<Language, 3> values = {
            Language::English,
            Language::German,
            Language::JapaneseKana
        };
        for (Language value : values) {
            const bool selected = (language == value);
            if (ImGui::Selectable(LanguageLabel(value), selected)) {
                language = value;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

inline void DrawTextEffectFlagsEditor(TextEffectType& effect) {
    int bits = static_cast<int>(effect);
    auto drawToggle = [&](const char* label, TextEffectType flag, bool sameLineAfter = false) {
        bool enabled = (bits & static_cast<int>(flag)) != 0;
        if (ImGui::Checkbox(label, &enabled)) {
            if (enabled) bits |= static_cast<int>(flag);
            else bits &= ~static_cast<int>(flag);
        }
        if (sameLineAfter) ImGui::SameLine();
    };

    drawToggle("Wave", TextEffectType::Wave, true);
    drawToggle("Shake", TextEffectType::Shake, true);
    drawToggle("Bounce", TextEffectType::Bounce);
    drawToggle("Rotate", TextEffectType::Rotate, true);
    drawToggle("Fade", TextEffectType::Fade);
    effect = static_cast<TextEffectType>(bits);
}

inline bool DrawDialogueLineToolbar(std::vector<DialogueLine>& lines,
                                    int& selectedIndex,
                                    const char* addLabel,
                                    const char* removeLabel) {
    bool changed = false;
    if (ImGui::Button(addLabel)) {
        lines.push_back(lines.empty() ? DialogueLine{} : lines.back());
        selectedIndex = static_cast<int>(lines.size()) - 1;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(removeLabel) &&
        selectedIndex >= 0 &&
        selectedIndex < static_cast<int>(lines.size())) {
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(selectedIndex));
        selectedIndex = lines.empty()
            ? -1
            : std::clamp(selectedIndex, 0, static_cast<int>(lines.size()) - 1);
        changed = true;
    }
    return changed;
}

inline void DrawDialogueLineList(const std::vector<DialogueLine>& lines,
                                 int& selectedIndex,
                                 const char* childId) {
    ImGui::BeginChild(childId, ImVec2(230.0f, 220.0f), true);
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string label = std::to_string(i + 1) + ". " +
            (lines[i].characterName.empty() ? std::string("<Unnamed>") : lines[i].characterName);
        if (ImGui::Selectable(label.c_str(), selectedIndex == static_cast<int>(i))) {
            selectedIndex = static_cast<int>(i);
        }
    }
    ImGui::EndChild();
}

inline bool DrawDialogueLineEditor(ScriptContext& ctx,
                                   std::vector<DialogueLine>& lines,
                                   int& selectedIndex,
                                   const char* addLabel = "Add Line",
                                   const char* removeLabel = "Remove Line",
                                   const char* emptyText = "No dialogue lines configured.",
                                   const char* childId = "DialogueLinesList") {
    bool changed = DrawDialogueLineToolbar(lines, selectedIndex, addLabel, removeLabel);
    if (lines.empty()) {
        ImGui::TextDisabled("%s", emptyText);
        return changed;
    }

    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(lines.size()) - 1);
    DrawDialogueLineList(lines, selectedIndex, childId);

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
    const TextEffectType before = line.textEffect;
    DrawTextEffectFlagsEditor(line.textEffect);
    changed |= (before != line.textEffect);

    changed |= DrawObjectRefInput(ctx, "Not Talking Object", line.notTalkingObjectRef);
    changed |= DrawObjectRefInput(ctx, "Open Mouth Object", line.openMouthObjectRef);
    changed |= DrawObjectRefInput(ctx, "Closed Mouth Object", line.closedMouthObjectRef);
    changed |= DrawObjectRefListEditor(ctx, "Line Items To Enable", line.itemsToEnable);
    changed |= DrawObjectRefListEditor(ctx, "Line Items To Disable", line.itemsToDisable);

    ImGui::EndGroup();
    return changed;
}

template <typename RuntimeStateT>
inline void DrawDialogueRuntimeStatus(const RuntimeStateT* state) {
    if (!state) {
        ImGui::TextDisabled("Runtime: idle");
        return;
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Runtime");
    ImGui::TextDisabled("Running: %s", state->running ? "Yes" : "No");
    ImGui::TextDisabled("Typing: %s", state->isTyping ? "Yes" : "No");
    ImGui::TextDisabled("Line: %d", state->index + 1);
    ImGui::TextDisabled("Chars: %zu / %zu",
                        state->revealedCharacters,
                        state->currentCleanSentence.size());
}

inline bool IsRuntimeKeyDown(int glfwKey, ImGuiKey imguiKey) {
    if (ModuCPP::ctxPtr()) {
        return ModuCPP::KeyDown(glfwKey);
    }
    if (ImGui::IsKeyDown(imguiKey)) return true;
    GLFWwindow* window = glfwGetCurrentContext();
    if (!window) return false;
    return glfwGetKey(window, glfwKey) == GLFW_PRESS;
}

} // namespace DialoguePort
