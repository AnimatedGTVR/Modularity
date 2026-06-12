#pragma once

#include "ScriptRuntime.h"
#include "ModuUIScriptApi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace ModuCPP {

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using Vector2 = vec2;
using Vector3 = vec3;
using Vector4 = vec4;
using string = std::string;

using String = std::string;
template <typename T> using Array = std::vector<T>;
template <typename T> using List = std::vector<T>;
template <typename K, typename V> using Map = std::unordered_map<K, V>;

namespace Math {
template <typename T>
inline T Max(const T& a, const T& b) {
    return std::max(a, b);
}

template <typename T>
inline T Min(const T& a, const T& b) {
    return std::min(a, b);
}

template <typename T>
inline T Clamp(const T& value, const T& minValue, const T& maxValue) {
    return std::clamp(value, minValue, maxValue);
}

inline float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

template <typename T>
inline T Abs(const T& value) {
    using std::abs;
    return abs(value);
}

inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

template <typename T>
inline T Mix(const T& a, const T& b, float t) {
    return glm::mix(a, b, t);
}

inline float Distance(const Vector2& a, const Vector2& b) {
    return glm::distance(a, b);
}

inline float Distance(const Vector3& a, const Vector3& b) {
    return glm::distance(a, b);
}

inline float Length(const Vector2& v) { return glm::length(v); }
inline float Length(const Vector3& v) { return glm::length(v); }

inline float Sqrt(float v) { return std::sqrt(v); }
inline float Pow(float base, float exponent) { return std::pow(base, exponent); }
inline float Exp(float v) { return std::exp(v); }
inline float Sin(float radians) { return std::sin(radians); }
inline float Cos(float radians) { return std::cos(radians); }
inline float Tan(float radians) { return std::tan(radians); }
inline float Asin(float v) { return std::asin(v); }
inline float Acos(float v) { return std::acos(v); }
inline float Atan(float v) { return std::atan(v); }
inline float Atan2(float y, float x) { return std::atan2(y, x); }
inline float Round(float v) { return std::round(v); }
inline float Floor(float v) { return std::floor(v); }
inline float Ceil(float v) { return std::ceil(v); }
template <typename T> inline T Sign(const T& v) { return (v > T(0)) ? T(1) : ((v < T(0)) ? T(-1) : T(0)); }

inline float Radians(float degrees) { return glm::radians(degrees); }
inline float Degrees(float radians) { return glm::degrees(radians); }
inline float Pi() { return glm::pi<float>(); }
inline float TwoPi() { return glm::two_pi<float>(); }

inline float Dot(const Vector2& a, const Vector2& b) { return glm::dot(a, b); }
inline float Dot(const Vector3& a, const Vector3& b) { return glm::dot(a, b); }
inline Vector2 Normalize(const Vector2& v) { return glm::normalize(v); }
inline Vector3 Normalize(const Vector3& v) { return glm::normalize(v); }
inline Vector3 Cross(const Vector3& a, const Vector3& b) { return glm::cross(a, b); }
} // namespace Math

namespace UI {
inline void Separator() { ::ModuGUI::Separator(); }
inline void Text(const char* text) { ::ModuGUI::TextUnformatted(text ? text : ""); }
inline void Text(const std::string& text) { ::ModuGUI::TextUnformatted(text.c_str()); }
inline void TextDisabled(const char* text) { ::ModuGUI::TextDisabled("%s", text ? text : ""); }
inline void TextDisabled(const std::string& text) { ::ModuGUI::TextDisabled("%s", text.c_str()); }
inline void TextFmt(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ::ModuGUI::TextV(fmt ? fmt : "", args);
    va_end(args);
}
inline void TextDisabledFmt(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ::ModuGUI::PushStyleColor(ImGuiCol_Text, ::ModuGUI::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ::ModuGUI::TextV(fmt ? fmt : "", args);
    ::ModuGUI::PopStyleColor();
    va_end(args);
}
} // namespace UI

struct ScriptInt {
    int value = 0;

    constexpr operator int() const { return value; }
};

inline std::string operator+(const std::string& lhs, ScriptInt rhs) {
    return lhs + std::to_string(rhs.value);
}

inline std::string operator+(const char* lhs, ScriptInt rhs) {
    return std::string(lhs ? lhs : "") + std::to_string(rhs.value);
}

inline std::string operator+(ScriptInt lhs, const std::string& rhs) {
    return std::to_string(lhs.value) + rhs;
}

inline std::string operator+(ScriptInt lhs, const char* rhs) {
    return std::to_string(lhs.value) + std::string(rhs ? rhs : "");
}

inline ScriptInt IntRD(float value) {
    return ScriptInt{ static_cast<int>(std::floor(value)) };
}

inline ScriptInt IntR(float value) {
    return ScriptInt{ static_cast<int>(std::lround(value)) };
}

inline ScriptInt IntRU(float value) {
    return ScriptInt{ static_cast<int>(std::ceil(value)) };
}

// FloatR(value, decimals) — returns a string-convertible wrapper formatted to
// the requested number of decimals. Used like: "x: " + FloatR(x, 2).
struct ScriptFloat {
    std::string text;
    operator std::string() const { return text; }
};

inline ScriptFloat FloatR(float value, int decimals = 2) {
    if (decimals < 0) decimals = 0;
    if (decimals > 9) decimals = 9;
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return ScriptFloat{ std::string(buffer) };
}

inline std::string operator+(const std::string& lhs, const ScriptFloat& rhs) { return lhs + rhs.text; }
inline std::string operator+(const ScriptFloat& lhs, const std::string& rhs) { return lhs.text + rhs; }
inline std::string operator+(const char* lhs, const ScriptFloat& rhs) {
    return std::string(lhs ? lhs : "") + rhs.text;
}
inline std::string operator+(const ScriptFloat& lhs, const char* rhs) {
    return lhs.text + std::string(rhs ? rhs : "");
}

namespace detail {
#if __cplusplus >= 201703L
inline thread_local ScriptContext* gCtx = nullptr;
inline thread_local float gFrameDeltaTime = 0.0f;
inline thread_local float gFrameFps = 0.0f;
#else
static thread_local ScriptContext* gCtx = nullptr;
static thread_local float gFrameDeltaTime = 0.0f;
static thread_local float gFrameFps = 0.0f;
#endif

inline std::string settingKeyFromLabel(const char* label) {
    if (!label || !*label) return "value";
    std::string raw(label);
    const size_t hiddenPos = raw.find("##");
    if (hiddenPos != std::string::npos) {
        raw = raw.substr(0, hiddenPos);
    }

    std::string key;
    key.reserve(raw.size() + 8);
    bool prevUnderscore = false;
    for (char ch : raw) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) != 0) {
            key.push_back(static_cast<char>(std::tolower(c)));
            prevUnderscore = false;
        } else if (!prevUnderscore) {
            key.push_back('_');
            prevUnderscore = true;
        }
    }

    while (!key.empty() && key.front() == '_') key.erase(key.begin());
    while (!key.empty() && key.back() == '_') key.pop_back();
    if (key.empty()) key = "value";
    return key;
}

inline std::string makeScriptInstanceKey(const ScriptContext& ctx, const std::string& suffix) {
    std::string key;
    if (ctx.script && !ctx.script->path.empty()) {
        key = ctx.script->path;
    } else if (ctx.script) {
        key = "script:" + std::to_string(reinterpret_cast<uintptr_t>(ctx.script));
    } else {
        key = "script:none";
    }

    if (ctx.object) {
        key += "|obj:" + std::to_string(ctx.object->id);
        if (ctx.script) {
            auto it = std::find_if(ctx.object->scripts.begin(), ctx.object->scripts.end(),
                                   [&](const ScriptComponent& s) { return &s == ctx.script; });
            if (it != ctx.object->scripts.end()) {
                key += "|slot:" + std::to_string(std::distance(ctx.object->scripts.begin(), it));
            }
        }
    }

    key += "|" + suffix;
    return key;
}

template <typename T>
std::unordered_map<std::string, T>& configStore() {
    static std::unordered_map<std::string, T> store;
    return store;
}

template <typename T>
std::unordered_map<std::string, T>& stateStore() {
    static std::unordered_map<std::string, T> store;
    return store;
}

struct TimerState {
    float interval = 0.0f;
    float elapsed = 0.0f;
    bool started = false;
};

inline std::unordered_map<const float*, TimerState>& timerStore() {
    static std::unordered_map<const float*, TimerState> store;
    return store;
}

template <typename T>
struct SubScriptSerializer;
} // namespace detail

struct Scope {
    explicit Scope(ScriptContext& ctx) : previous(detail::gCtx) {
        detail::gCtx = &ctx;
    }

    ~Scope() {
        detail::gCtx = previous;
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    ScriptContext* previous;
};

inline ScriptContext* ctxPtr() {
    return detail::gCtx;
}

inline ScriptContext& ctx() {
    return *detail::gCtx;
}

// Resolve an object ref string the same way the transpiler-emitted helper does:
// 1. ScriptContext::ResolveObjectRef (handles "Object.ID-N" style refs)
// 2. fall back to FindObjectByName (handles bare scene-object names)
// Required because [ObjectList] entries can be stored as either form depending
// on how the inspector serialized them.
inline SceneObject* SmartResolveRef(const std::string& ref) {
    ScriptContext* c = ctxPtr();
    if (!c || ref.empty()) return nullptr;
    if (SceneObject* o = c->ResolveObjectRef(ref)) return o;
    return c->FindObjectByName(ref);
}

// UI accessors for [ObjectRef] string fields. The transpiler rewrites
//   field.UI.Exists  -> UIExists(field)
//   field.UI.Position           -> UIPosition(field)
//   field.UI.Position = value   -> SetUIPosition(field, value)
//   field.UI.Size               -> UISize(field)
//   field.UI.Size = value       -> SetUISize(field, value)
// when 'field' is a known [ObjectRef] field on the surrounding script class.
// Each call resolves the ref through ctxPtr(); reads return a zero default
// when the ref is empty/unresolvable, writes are no-ops in that case.
inline bool UIExists(const std::string& ref) {
    if (SceneObject* o = SmartResolveRef(ref)) return ::HasUIComponent(*o);
    return false;
}

inline glm::vec2 UIPosition(const std::string& ref) {
    if (SceneObject* o = SmartResolveRef(ref)) return o->ui.position;
    return glm::vec2(0.0f);
}

inline void SetUIPosition(const std::string& ref, const glm::vec2& value) {
    SceneObject* o = SmartResolveRef(ref);
    if (!o) return;
    if (o->ui.position != value) {
        o->ui.position = value;
        if (auto* c = ctxPtr()) c->MarkDirty();
    }
}

inline glm::vec2 UISize(const std::string& ref) {
    if (SceneObject* o = SmartResolveRef(ref)) return o->ui.size;
    return glm::vec2(0.0f);
}

inline void SetUISize(const std::string& ref, const glm::vec2& value) {
    SceneObject* o = SmartResolveRef(ref);
    if (!o) return;
    if (o->ui.size != value) {
        o->ui.size = value;
        if (auto* c = ctxPtr()) c->MarkDirty();
    }
}

// Audio.PlayOneShot(refOrPath, vol=1.0f)
// Smart-resolves the argument: if it matches a scene object with an audio source,
// plays that source's clip. Otherwise treats the argument as a literal clip path.
// Mirrors the original ctx.PlayAudioOneShot semantics; safe with empty input.
namespace Audio {
inline bool PlayOneShot(const std::string& refOrPath, float volumeScale = 1.0f) {
    ScriptContext* c = ctxPtr();
    if (!c || refOrPath.empty()) return false;
    if (SceneObject* source = SmartResolveRef(refOrPath)) {
        if (source->hasAudioSource && !source->audioSource.clipPath.empty()) {
            return c->PlayObjectAudioOneShot(source->id, source->audioSource.clipPath, volumeScale);
        }
    }
    return c->PlayAudioOneShot(refOrPath, volumeScale);
}
} // namespace Audio

template <typename T>
inline T& Config() {
    ScriptContext& scriptCtx = ctx();
    const std::string key = detail::makeScriptInstanceKey(
        scriptCtx,
        std::string("config:") + typeid(T).name());
    return detail::configStore<T>()[key];
}

template <typename T>
inline T& State() {
    ScriptContext& scriptCtx = ctx();
    const std::string key = detail::makeScriptInstanceKey(
        scriptCtx,
        std::string("state:") + typeid(T).name());
    return detail::stateStore<T>()[key];
}

inline void BindSetting(ScriptContext& ctx, const char* key, bool& value) {
    ctx.AutoSetting(key, value);
}

inline void BindSetting(ScriptContext& ctx, const std::string& key, bool& value) {
    ctx.AutoSetting(key, value);
}

inline void BindSetting(const char* key, bool& value) {
    if (ScriptContext* scriptCtx = ctxPtr()) BindSetting(*scriptCtx, key, value);
}

inline void BindSetting(ScriptContext& ctx, const char* key, float& value) {
    ctx.AutoSetting(key, value);
}

inline void BindSetting(ScriptContext& ctx, const std::string& key, float& value) {
    ctx.AutoSetting(key, value);
}

inline void BindSetting(const char* key, float& value) {
    if (ScriptContext* scriptCtx = ctxPtr()) BindSetting(*scriptCtx, key, value);
}

inline void BindSetting(ScriptContext& ctx, const char* key, int& value) {
    ctx.AutoSetting(key, value);
}

inline void BindSetting(ScriptContext& ctx, const std::string& key, int& value) {
    ctx.AutoSetting(key, value);
}

inline void BindSetting(const char* key, int& value) {
    if (ScriptContext* scriptCtx = ctxPtr()) BindSetting(*scriptCtx, key, value);
}

inline void BindSetting(ScriptContext& ctx, const char* key, vec3& value) {
    ctx.AutoSetting(key, value);
}

inline void BindSetting(ScriptContext& ctx, const std::string& key, vec3& value) {
    ctx.AutoSetting(key, value);
}

inline void BindSetting(const char* key, vec3& value) {
    if (ScriptContext* scriptCtx = ctxPtr()) BindSetting(*scriptCtx, key, value);
}

inline void BindSetting(ScriptContext& ctx, const char* key, std::string& value) {
    ctx.AutoSetting(key, value);
}

inline void BindSetting(ScriptContext& ctx, const std::string& key, std::string& value) {
    ctx.AutoSetting(key, value);
}

inline void BindSetting(const char* key, std::string& value) {
    if (ScriptContext* scriptCtx = ctxPtr()) BindSetting(*scriptCtx, key, value);
}

// Sheet-relative Sprite <-> setting string. Format: "sheetAssetPath|clipName|clipIndex".
// clipName is primary (reorder-stable); clipIndex is a fallback/cache.
inline std::string SerializeSprite(const Sprite& sprite) {
    return sprite.sheetAssetPath + "|" + sprite.clipName + "|" + std::to_string(sprite.clipIndex);
}

inline Sprite DeserializeSprite(const std::string& encoded) {
    Sprite sprite;
    const size_t firstBar = encoded.find('|');
    if (firstBar == std::string::npos) {
        // Back-compat: a bare integer is a self-sheet clip index (old Field::Clip
        // settings + the index-based inspector still decode correctly).
        if (!encoded.empty() && (std::isdigit(static_cast<unsigned char>(encoded[0])) || encoded[0] == '-')) {
            sprite.clipIndex = std::atoi(encoded.c_str());
        } else {
            sprite.clipName = encoded;
        }
        return sprite;
    }
    sprite.sheetAssetPath = encoded.substr(0, firstBar);
    const size_t secondBar = encoded.find('|', firstBar + 1);
    if (secondBar == std::string::npos) { sprite.clipName = encoded.substr(firstBar + 1); return sprite; }
    sprite.clipName = encoded.substr(firstBar + 1, secondBar - firstBar - 1);
    const std::string index = encoded.substr(secondBar + 1);
    sprite.clipIndex = index.empty() ? -1 : std::atoi(index.c_str());
    return sprite;
}

// Sprite isn't an AutoSetting primitive, so this loads from the string setting
// (Field::SetSprite / the inspector write it back on edit). Declared before
// BindArray so BindArray<Sprite,N> resolves it via unqualified lookup.
inline void BindSetting(ScriptContext& ctx, const std::string& key, Sprite& value) {
    value = DeserializeSprite(ctx.GetSetting(key, SerializeSprite(value)));
}

inline void BindSetting(ScriptContext& ctx, const char* key, Sprite& value) {
    BindSetting(ctx, std::string(key ? key : ""), value);
}

inline bool DrawObjectRefInput(ScriptContext& ctx, const char* label, std::string& objectRef) {
    bool changed = false;
    ModuGUI::PushID(label ? label : "ObjectRef");
    const char* popupId = "##SceneObjectPicker";
    ModuGUI::AlignTextToFramePadding();
    ModuGUI::TextUnformatted(label ? label : "Object");
    ModuGUI::SameLine();

    SceneObject* resolved = ctx.ResolveObjectRef(objectRef);
    std::string preview = "Empty";
    if (resolved) {
        preview = resolved->name;
    } else if (!objectRef.empty()) {
        preview = objectRef;
    }

    const float buttonSize = ModuGUI::GetFrameHeight();
    const float spacing = ModuGUI::GetStyle().ItemSpacing.x;
    const float fieldWidth = std::max(120.0f, ModuGUI::GetContentRegionAvail().x - buttonSize * 2.0f - spacing * 2.0f);
    if (ModuGUI::Button((preview + "##ObjectRefField").c_str(), ImVec2(fieldWidth, 0.0f))) {
        ModuGUI::OpenPopup(popupId);
    }
    if (ModuGUI::IsItemHovered()) {
        if (resolved) {
            ModuGUI::SetTooltip("%s (id=%d)\nDrag a scene object here, or click to choose from the scene.",
                                resolved->name.c_str(), resolved->id);
        } else {
            ModuGUI::SetTooltip("Drag a scene object here, or click to choose from the scene.");
        }
    }

    if (ModuGUI::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ModuGUI::AcceptDragDropPayload("SCENE_OBJECT")) {
            if (payload->Data && payload->DataSize == static_cast<int>(sizeof(int))) {
                const int droppedId = *static_cast<const int*>(payload->Data);
                const std::string newRef = std::string("Object.ID-") + std::to_string(droppedId);
                if (objectRef != newRef) {
                    objectRef = newRef;
                    changed = true;
                }
            }
        }
        ModuGUI::EndDragDropTarget();
    }

    ModuGUI::SameLine();
    if (ModuGUI::Button("...", ImVec2(buttonSize, 0.0f))) {
        ModuGUI::OpenPopup(popupId);
    }
    if (ModuGUI::IsItemHovered()) {
        ModuGUI::SetTooltip("Choose scene object");
    }

    ModuGUI::SameLine();
    if (ModuGUI::Button("X", ImVec2(buttonSize, 0.0f))) {
        if (!objectRef.empty()) {
            objectRef.clear();
            changed = true;
        }
    }

    if (ModuGUI::BeginPopup(popupId)) {
        // Single shared filter — only one ObjectRef popup is open at a time.
        // Keep this helper pointer-free so stale inspector rows cannot leave
        // dangling keys behind after hierarchy/object mutations.
        // The previous design keyed a static unordered_map by `&objectRef`,
        // which dangled whenever the SceneObject owning the field was
        // destroyed (e.g. inspector temporaries) and could SIGSEGV on the
        // next inspector open.
        static std::string filter;
        char searchBuf[128] = {};
        std::snprintf(searchBuf, sizeof(searchBuf), "%s", filter.c_str());
        if (ModuGUI::InputTextWithHint("##Filter", "Filter scene objects", searchBuf, sizeof(searchBuf))) {
            filter = searchBuf;
        }

        if (ModuGUI::Selectable("None", objectRef.empty())) {
            objectRef.clear();
            changed = true;
            ModuGUI::CloseCurrentPopup();
        }

        auto lower = [](std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        };
        const std::string filterLower = lower(filter);

        ModuGUI::Separator();
        ModuGUI::BeginChild("##SceneObjectList", ImVec2(320.0f, 220.0f), false);
        const int count = ctx.GetSceneObjectCount();
        for (int i = 0; i < count; ++i) {
            const SceneObject* candidate = ctx.GetSceneObjectAt(i);
            if (!candidate) continue;

            const std::string rowText = candidate->name + " (id=" + std::to_string(candidate->id) + ")";
            if (!filterLower.empty() && lower(rowText).find(filterLower) == std::string::npos) {
                continue;
            }

            const bool selected = resolved && resolved->id == candidate->id;
            if (ModuGUI::Selectable((rowText + "##" + std::to_string(candidate->id)).c_str(), selected)) {
                objectRef = std::string("Object.ID-") + std::to_string(candidate->id);
                changed = true;
                ModuGUI::CloseCurrentPopup();
            }
        }
        ModuGUI::EndChild();
        ModuGUI::EndPopup();
    }

    ModuGUI::PopID();
    return changed;
}

template <typename T>
inline std::string SerializeSubScript(const T& value) {
    return detail::SubScriptSerializer<T>::Serialize(value);
}

template <typename T>
inline T DeserializeSubScript(const std::string& encoded) {
    return detail::SubScriptSerializer<T>::Deserialize(encoded);
}

template <typename T>
inline std::string SerializeSubScriptArray(const std::vector<T>& values) {
    return detail::SubScriptSerializer<T>::SerializeArray(values);
}

template <typename T>
inline std::vector<T> DeserializeSubScriptArray(const std::string& encoded) {
    return detail::SubScriptSerializer<T>::DeserializeArray(encoded);
}

template <typename T>
inline bool EditSubScript(const char* label, T& value) {
    return detail::SubScriptSerializer<T>::Edit(label, value);
}

template <typename T>
inline bool EditSubScriptArray(const char* label, std::vector<T>& values) {
    return detail::SubScriptSerializer<T>::EditArray(label, values);
}

template <typename T, size_t N>
inline void BindArray(ScriptContext& ctx, const std::string& keyPrefix, std::array<T, N>& values) {
    for (size_t i = 0; i < N; ++i) {
        BindSetting(ctx, keyPrefix + std::to_string(i), values[i]);
    }
}

template <typename T, size_t N>
inline void BindArray(ScriptContext& ctx, const char* keyPrefix, std::array<T, N>& values) {
    BindArray(ctx, std::string(keyPrefix ? keyPrefix : ""), values);
}

template <typename T, size_t N>
inline void BindArray(const std::string& keyPrefix, std::array<T, N>& values) {
    if (ScriptContext* scriptCtx = ctxPtr()) {
        BindArray(*scriptCtx, keyPrefix, values);
    }
}

template <typename T, size_t N>
inline void BindArray(const char* keyPrefix, std::array<T, N>& values) {
    if (ScriptContext* scriptCtx = ctxPtr()) {
        BindArray(*scriptCtx, keyPrefix, values);
    }
}

template <typename T, size_t Rows, size_t Cols>
inline void BindArray2D(ScriptContext& ctx, const std::string& keyPrefix,
                        std::array<std::array<T, Cols>, Rows>& values) {
    for (size_t row = 0; row < Rows; ++row) {
        for (size_t col = 0; col < Cols; ++col) {
            BindSetting(ctx, keyPrefix + std::to_string(row) + "_" + std::to_string(col),
                        values[row][col]);
        }
    }
}

template <typename T, size_t Rows, size_t Cols>
inline void BindArray2D(ScriptContext& ctx, const char* keyPrefix,
                        std::array<std::array<T, Cols>, Rows>& values) {
    BindArray2D(ctx, std::string(keyPrefix ? keyPrefix : ""), values);
}

template <typename T, size_t Rows, size_t Cols>
inline void BindArray2D(const std::string& keyPrefix, std::array<std::array<T, Cols>, Rows>& values) {
    if (ScriptContext* scriptCtx = ctxPtr()) {
        BindArray2D(*scriptCtx, keyPrefix, values);
    }
}

template <typename T, size_t Rows, size_t Cols>
inline void BindArray2D(const char* keyPrefix, std::array<std::array<T, Cols>, Rows>& values) {
    if (ScriptContext* scriptCtx = ctxPtr()) {
        BindArray2D(*scriptCtx, keyPrefix, values);
    }
}

inline void SetFrameDeltaTime(float deltaTime) {
    detail::gFrameDeltaTime = deltaTime;
    detail::gFrameFps = deltaTime > 1e-6f ? (1.0f / deltaTime) : 0.0f;
}

inline void StartTimer(float& timerValue, float interval) {
    timerValue = 0.0f;
    detail::TimerState& state = detail::timerStore()[&timerValue];
    state.interval = std::max(0.0f, interval);
    state.elapsed = 0.0f;
    state.started = true;
}

inline bool TimerReady(float& timerValue) {
    detail::TimerState& state = detail::timerStore()[&timerValue];
    if (!state.started) {
        state.interval = std::max(0.0f, timerValue);
        state.elapsed = 0.0f;
        state.started = true;
    }

    if (state.interval <= 0.0f) {
        timerValue = 0.0f;
        return true;
    }

    state.elapsed = std::max(0.0f, state.elapsed + std::max(0.0f, detail::gFrameDeltaTime));
    if (state.elapsed + 1e-6f < state.interval) {
        timerValue = state.elapsed;
        return false;
    }

    const float cycleCount = std::floor(state.elapsed / state.interval);
    state.elapsed -= std::max(1.0f, cycleCount) * state.interval;
    if (state.elapsed < 1e-6f) {
        state.elapsed = 0.0f;
    }
    timerValue = state.elapsed;
    return true;
}

inline bool TimerReady(float& timerValue, float interval) {
    detail::TimerState& state = detail::timerStore()[&timerValue];
    state.interval = std::max(0.0f, interval);
    state.started = true;
    return TimerReady(timerValue);
}

struct UILabelProxy {
    ScriptContext* scriptCtx = nullptr;

    UILabelProxy& operator=(const std::string& label) {
        if (scriptCtx) {
            scriptCtx->SetUILabel(label);
        }
        return *this;
    }

    UILabelProxy& operator=(const char* label) {
        if (scriptCtx) {
            scriptCtx->SetUILabel(label ? label : "");
        }
        return *this;
    }

    operator std::string() const {
        if (!scriptCtx || !scriptCtx->object) {
            return {};
        }
        return scriptCtx->object->ui.label;
    }
};

// ---------------------------------------------------------------------------
// Per-component access proxies used by SceneObj, ObjectFacade (the local
// `obj` injected by MODU_SCRIPT) and any user SceneObj-typed value. Each
// proxy resolves the underlying SceneObject* either from a stored
// pointer-to-pointer (when bound to a SceneObj's `ptr` field) or, when the
// pointer is nullptr, by reading the current thread-local ScriptContext's
// active object. This gives `obj.Rigidbody3D.velocity` and
// `someObj.Rigidbody3D.velocity` an identical surface.
// ---------------------------------------------------------------------------

namespace detail {
inline SceneObject* resolveAccessTarget(SceneObject* const* pp) {
    if (pp) return *pp;
    if (ScriptContext* c = ctxPtr()) return c->object;
    return nullptr;
}
inline bool isAccessTargetCurrentCtx(SceneObject* const* pp) {
    if (!pp) return true;
    if (ScriptContext* c = ctxPtr()) return *pp == c->object;
    return false;
}
} // namespace detail

struct Rigidbody3DAccess {
    SceneObject* const* pp = nullptr;

    struct Velocity {
        SceneObject* const* pp = nullptr;
        Vector3 get() const;
        void set(const Vector3& v) const;
        operator Vector3() const { return get(); }
        Velocity& operator=(const Vector3& v) { set(v); return *this; }
        Velocity& operator+=(const Vector3& v) { set(get() + v); return *this; }
        Velocity& operator-=(const Vector3& v) { set(get() - v); return *this; }
    };

    struct VelocityYProxy {
        SceneObject* const* pp = nullptr;
        float get() const;
        void set(float v) const;
        operator float() const { return get(); }
        VelocityYProxy& operator=(float v) { set(v); return *this; }
        VelocityYProxy& operator+=(float v) { set(get() + v); return *this; }
        VelocityYProxy& operator-=(float v) { set(get() - v); return *this; }
    };

    Velocity velocity{};
    VelocityYProxy VelocityY{};

    Rigidbody3DAccess() = default;
    explicit Rigidbody3DAccess(SceneObject* const* p) : pp(p), velocity{p}, VelocityY{p} {}
    Rigidbody3DAccess(const Rigidbody3DAccess& o) : pp(o.pp), velocity{o.pp}, VelocityY{o.pp} {}
    Rigidbody3DAccess& operator=(const Rigidbody3DAccess& o) {
        pp = o.pp; velocity.pp = o.pp; VelocityY.pp = o.pp; return *this;
    }

    SceneObject* target() const { return detail::resolveAccessTarget(pp); }
    int targetId() const { auto* t = target(); return t ? t->id : -1; }
    bool Exists() const;
    bool IsKinematic() const { auto* t = target(); return t && t->hasRigidbody && t->rigidbody.isKinematic; }
    bool IsEnabled() const { auto* t = target(); return t && t->hasRigidbody && t->rigidbody.enabled; }
    void Accelerate(const Vector3& direction, float targetSpeed,
                    float acceleration, float deceleration) const;
    void FlattenY() const;
    void AddVelocityY(float y) const;
    void AddForce(const Vector3& force) const;
    void AddImpulse(const Vector3& impulse) const;
};

struct TransformAccess {
    SceneObject* const* pp = nullptr;

    struct PositionProxy {
        SceneObject* const* pp = nullptr;
        Vector3 get() const;
        void set(const Vector3& v) const;
        operator Vector3() const { return get(); }
        PositionProxy& operator=(const Vector3& v) { set(v); return *this; }
        PositionProxy& operator+=(const Vector3& v) { set(get() + v); return *this; }
    };

    struct RotationProxy {
        SceneObject* const* pp = nullptr;
        Vector3 get() const;
        void set(const Vector3& v) const;
        operator Vector3() const { return get(); }
        RotationProxy& operator=(const Vector3& v) { set(v); return *this; }
    };

    PositionProxy position{};
    RotationProxy rotation{};

    TransformAccess() = default;
    explicit TransformAccess(SceneObject* const* p) : pp(p), position{p}, rotation{p} {}
    TransformAccess(const TransformAccess& o) : pp(o.pp), position{o.pp}, rotation{o.pp} {}
    TransformAccess& operator=(const TransformAccess& o) {
        pp = o.pp; position.pp = o.pp; rotation.pp = o.pp; return *this;
    }

    SceneObject* target() const { return detail::resolveAccessTarget(pp); }
    void Face(const Vector3& direction) const;
    void RotateToward(const Vector3& direction, float degreesPerSecond) const;
    Vector3 Forward() const;
    Vector3 Right() const;
};

struct PhysicsAccess {
    SceneObject* const* pp = nullptr;

    PhysicsAccess() = default;
    explicit PhysicsAccess(SceneObject* const* p) : pp(p) {}

    SceneObject* target() const { return detail::resolveAccessTarget(pp); }
    bool IsGrounded(float distance = 0.1f) const;
    bool Raycast(const Vector3& direction, float distance) const;
    bool RaycastHit(const Vector3& direction, float distance,
                    Vector3& outHitPos, Vector3& outHitNormal) const;
};

struct ObjectFacade {
    ScriptContext* scriptCtx = nullptr;
    UILabelProxy UILabel{};
    UIProxy UI;
    Rigidbody3DAccess Rigidbody3D{};
    TransformAccess   Transform{};
    PhysicsAccess     Physics{};

    explicit ObjectFacade(ScriptContext& ctx)
        : scriptCtx(&ctx), UILabel{ &ctx },
          UI(MakeUIProxy(ctx, detail::makeScriptInstanceKey(ctx, "ui"))) {}

    SceneObject* raw() const {
        return scriptCtx ? scriptCtx->object : nullptr;
    }

    explicit operator bool() const {
        return raw() != nullptr;
    }

    bool operator!() const {
        return raw() == nullptr;
    }

    operator SceneObject*() const {
        return raw();
    }

    SceneObject* operator->() const {
        return raw();
    }
};

inline ObjectFacade MakeObjectFacade(ScriptContext& ctx) {
    return ObjectFacade(ctx);
}

namespace Type {
#if __cplusplus >= 201703L
inline constexpr ConsoleMessageType Info = ConsoleMessageType::Info;
inline constexpr ConsoleMessageType Warning = ConsoleMessageType::Warning;
inline constexpr ConsoleMessageType Error = ConsoleMessageType::Error;
inline constexpr ConsoleMessageType Success = ConsoleMessageType::Success;
#else
static constexpr ConsoleMessageType Info = ConsoleMessageType::Info;
static constexpr ConsoleMessageType Warning = ConsoleMessageType::Warning;
static constexpr ConsoleMessageType Error = ConsoleMessageType::Error;
static constexpr ConsoleMessageType Success = ConsoleMessageType::Success;
#endif
} // namespace Type

inline void AddLog(const std::string& message, ConsoleMessageType type = ConsoleMessageType::Info) {
    if (ScriptContext* scriptCtx = ctxPtr()) {
        scriptCtx->AddConsoleMessage(message, type);
    }
}

inline void AddLog(const char* message, ConsoleMessageType type = ConsoleMessageType::Info) {
    AddLog(std::string(message ? message : ""), type);
}

// Read-side access to an object's audio component (direct field reads). The
// action side (play/stop/loop changes) stays in the ObjectAudio:: helpers.
struct AudioAccess {
    SceneObject* const* pp = nullptr;
    SceneObject* target() const { return detail::resolveAccessTarget(pp); }
    bool Exists() const { auto* t = target(); return t && t->hasAudioSource; }
    bool Enabled() const { auto* t = target(); return t && t->hasAudioSource && t->audioSource.enabled; }
    bool Loop() const { auto* t = target(); return t && t->hasAudioSource && t->audioSource.loop; }
    std::string ClipPath() const { auto* t = target(); return (t && t->hasAudioSource) ? t->audioSource.clipPath : std::string(); }
};

// Thin pointer-handle around SceneObject. Implicit conversion to/from
// SceneObject* keeps it interchangeable with existing pointer-based code.
struct SceneObj {
    SceneObject* ptr = nullptr;

    Rigidbody3DAccess Rigidbody3D{&ptr};
    TransformAccess   Transform{&ptr};
    PhysicsAccess     Physics{&ptr};
    AudioAccess       Audio{&ptr};

    // Direct-read identity/state (C#-like, no ctx round-trip).
    int Id() const { return ptr ? ptr->id : -1; }
    std::string Name() const { return ptr ? ptr->name : std::string(); }
    bool Enabled() const { return ptr && ptr->enabled; }
    bool HasRigidbody() const { return ptr && ptr->hasRigidbody; }
    bool HasAudio() const { return ptr && ptr->hasAudioSource; }
    // Enable/disable this object, mirroring the list `each X.state(b)` form.
    void State(bool enabled) const {
        if (ptr && ptr->enabled != enabled) {
            ptr->enabled = enabled;
            if (ScriptContext* c = ctxPtr()) c->MarkDirty();
        }
    }

    SceneObj() = default;
    SceneObj(SceneObject* p) : ptr(p) {}
    SceneObj(std::nullptr_t) : ptr(nullptr) {}

    SceneObj(const SceneObj& o)
        : ptr(o.ptr), Rigidbody3D(&ptr), Transform(&ptr), Physics(&ptr), Audio(&ptr) {}
    SceneObj& operator=(const SceneObj& o) {
        ptr = o.ptr;
        return *this;
    }
    SceneObj(SceneObj&& o) noexcept
        : ptr(o.ptr), Rigidbody3D(&ptr), Transform(&ptr), Physics(&ptr), Audio(&ptr) {}
    SceneObj& operator=(SceneObj&& o) noexcept {
        ptr = o.ptr;
        return *this;
    }
    SceneObj& operator=(SceneObject* p) { ptr = p; return *this; }
    SceneObj& operator=(std::nullptr_t) { ptr = nullptr; return *this; }

    explicit operator bool() const { return ptr != nullptr; }
    bool operator!() const { return ptr == nullptr; }

    SceneObject* operator->() const { return ptr; }
    SceneObject& operator*() const { return *ptr; }
    operator SceneObject*() const { return ptr; }

    friend bool operator==(const SceneObj& a, const SceneObj& b) { return a.ptr == b.ptr; }
    friend bool operator!=(const SceneObj& a, const SceneObj& b) { return a.ptr != b.ptr; }
    friend bool operator==(const SceneObj& a, std::nullptr_t) { return a.ptr == nullptr; }
    friend bool operator!=(const SceneObj& a, std::nullptr_t) { return a.ptr != nullptr; }
};

// --- Access proxy implementations -----------------------------------------

inline Vector3 Rigidbody3DAccess::Velocity::get() const {
    ScriptContext* c = ctxPtr();
    if (!c) return Vector3(0.0f);
    SceneObject* t = detail::resolveAccessTarget(pp);
    if (!t) return Vector3(0.0f);
    Vector3 v(0.0f);
    if (detail::isAccessTargetCurrentCtx(pp)) {
        c->GetRigidbodyVelocity(v);
    } else {
        c->GetObjectRigidbodyVelocity(t->id, v);
    }
    return v;
}

inline void Rigidbody3DAccess::Velocity::set(const Vector3& v) const {
    ScriptContext* c = ctxPtr();
    if (!c) return;
    SceneObject* t = detail::resolveAccessTarget(pp);
    if (!t) return;
    if (detail::isAccessTargetCurrentCtx(pp)) {
        c->SetRigidbodyVelocity(v);
    } else {
        c->SetObjectRigidbodyVelocity(t->id, v);
    }
}

inline float Rigidbody3DAccess::VelocityYProxy::get() const {
    return Rigidbody3DAccess::Velocity{pp}.get().y;
}

inline void Rigidbody3DAccess::VelocityYProxy::set(float y) const {
    Rigidbody3DAccess::Velocity p{pp};
    Vector3 v = p.get();
    v.y = y;
    p.set(v);
}

inline bool Rigidbody3DAccess::Exists() const {
    ScriptContext* c = ctxPtr();
    if (!c) return false;
    SceneObject* t = target();
    if (!t) return false;
    if (detail::isAccessTargetCurrentCtx(pp)) return c->HasRigidbody();
    return t->hasRigidbody;
}

inline void Rigidbody3DAccess::AddVelocityY(float y) const {
    Velocity p{pp};
    Vector3 v = p.get();
    v.y += y;
    p.set(v);
}

inline void Rigidbody3DAccess::FlattenY() const {
    Velocity p{pp};
    Vector3 v = p.get();
    v.y = 0.0f;
    p.set(v);
}

inline void Rigidbody3DAccess::Accelerate(const Vector3& direction, float targetSpeed,
                                          float acceleration, float deceleration) const {
    ScriptContext* c = ctxPtr();
    if (!c) return;
    const float dt = detail::gFrameDeltaTime;
    if (dt <= 0.0f) return;

    Velocity proxy{pp};
    Vector3 current = proxy.get();
    Vector3 planar(current.x, 0.0f, current.z);
    Vector3 dirPlanar(direction.x, 0.0f, direction.z);
    const float dirLen = glm::length(dirPlanar);
    if (dirLen > 1e-4f) dirPlanar /= dirLen; else dirPlanar = Vector3(0.0f);

    Vector3 targetVel = dirPlanar * targetSpeed;
    const float planarMag = glm::length(planar);
    const float targetMag = glm::length(targetVel);
    const float rate = (targetMag >= planarMag - 1e-4f) ? acceleration : deceleration;
    const float maxStep = std::max(0.0f, rate * dt);

    Vector3 delta = targetVel - planar;
    const float deltaLen = glm::length(delta);
    Vector3 newPlanar = (deltaLen <= maxStep || deltaLen <= 1e-6f)
        ? targetVel
        : planar + (delta / deltaLen) * maxStep;

    proxy.set(Vector3(newPlanar.x, current.y, newPlanar.z));
}

inline void Rigidbody3DAccess::AddForce(const Vector3& force) const {
    ScriptContext* c = ctxPtr();
    if (!c) return;
    SceneObject* t = target();
    if (!t) return;
    if (detail::isAccessTargetCurrentCtx(pp)) {
        c->AddRigidbodyForce(force);
    } else {
        c->AddObjectRigidbodyImpulse(t->id, force * detail::gFrameDeltaTime);
    }
}

inline void Rigidbody3DAccess::AddImpulse(const Vector3& impulse) const {
    ScriptContext* c = ctxPtr();
    if (!c) return;
    SceneObject* t = target();
    if (!t) return;
    if (detail::isAccessTargetCurrentCtx(pp)) {
        c->AddRigidbodyImpulse(impulse);
    } else {
        c->AddObjectRigidbodyImpulse(t->id, impulse);
    }
}

inline Vector3 TransformAccess::PositionProxy::get() const {
    SceneObject* t = detail::resolveAccessTarget(pp);
    return t ? t->position : Vector3(0.0f);
}

inline void TransformAccess::PositionProxy::set(const Vector3& v) const {
    ScriptContext* c = ctxPtr();
    if (!c) return;
    SceneObject* t = detail::resolveAccessTarget(pp);
    if (!t) return;
    if (detail::isAccessTargetCurrentCtx(pp)) {
        c->SetPosition(v);
    } else {
        t->position = v;
        c->TeleportObjectRigidbody(t->id, v, t->rotation);
    }
}

inline Vector3 TransformAccess::RotationProxy::get() const {
    SceneObject* t = detail::resolveAccessTarget(pp);
    return t ? t->rotation : Vector3(0.0f);
}

inline void TransformAccess::RotationProxy::set(const Vector3& v) const {
    ScriptContext* c = ctxPtr();
    if (!c) return;
    SceneObject* t = detail::resolveAccessTarget(pp);
    if (!t) return;
    if (detail::isAccessTargetCurrentCtx(pp)) {
        c->SetRotation(v);
    } else {
        t->rotation = v;
        c->TeleportObjectRigidbody(t->id, t->position, v);
    }
}

inline void TransformAccess::Face(const Vector3& direction) const {
    SceneObject* t = target();
    if (!t) return;
    Vector3 planar(direction.x, 0.0f, direction.z);
    if (glm::length(planar) < 1e-4f) return;
    const float yawRadians = std::atan2(planar.x, -planar.z);
    Vector3 rot = t->rotation;
    rot.y = glm::degrees(yawRadians);
    rotation.set(rot);
}

inline void TransformAccess::RotateToward(const Vector3& direction, float degreesPerSecond) const {
    SceneObject* t = target();
    if (!t) return;
    Vector3 planar(direction.x, 0.0f, direction.z);
    if (glm::length(planar) < 1e-4f) return;
    const float targetYaw = glm::degrees(std::atan2(planar.x, -planar.z));
    float currentYaw = t->rotation.y;
    float delta = targetYaw - currentYaw;
    while (delta > 180.0f) delta -= 360.0f;
    while (delta < -180.0f) delta += 360.0f;
    const float maxStep = degreesPerSecond * detail::gFrameDeltaTime;
    if (std::abs(delta) > maxStep) delta = (delta > 0.0f) ? maxStep : -maxStep;
    Vector3 rot = t->rotation;
    rot.y = currentYaw + delta;
    rotation.set(rot);
}

inline Vector3 TransformAccess::Forward() const {
    SceneObject* t = target();
    if (!t) return Vector3(0.0f, 0.0f, -1.0f);
    const glm::quat q = glm::quat(glm::radians(t->rotation));
    return glm::normalize(q * Vector3(0.0f, 0.0f, -1.0f));
}

inline Vector3 TransformAccess::Right() const {
    SceneObject* t = target();
    if (!t) return Vector3(1.0f, 0.0f, 0.0f);
    const glm::quat q = glm::quat(glm::radians(t->rotation));
    return glm::normalize(q * Vector3(1.0f, 0.0f, 0.0f));
}

inline bool PhysicsAccess::IsGrounded(float distance) const {
    ScriptContext* c = ctxPtr();
    if (!c) return false;
    SceneObject* t = target();
    if (!t) return false;
    const Vector3 origin = t->position;
    const Vector3 down(0.0f, -1.0f, 0.0f);
    const float probe = std::max(0.0f, distance) + 0.0001f;
    float hitDist = 0.0f;
    if (!c->RaycastClosest(origin, down, probe, nullptr, nullptr, &hitDist)) return false;
    return hitDist <= distance + 1e-4f;
}

inline bool PhysicsAccess::Raycast(const Vector3& direction, float distance) const {
    ScriptContext* c = ctxPtr();
    if (!c) return false;
    SceneObject* t = target();
    if (!t) return false;
    return c->RaycastClosest(t->position, direction, distance, nullptr, nullptr, nullptr);
}

inline bool PhysicsAccess::RaycastHit(const Vector3& direction, float distance,
                                      Vector3& outHitPos, Vector3& outHitNormal) const {
    ScriptContext* c = ctxPtr();
    if (!c) return false;
    SceneObject* t = target();
    if (!t) return false;
    return c->RaycastClosest(t->position, direction, distance, &outHitPos, &outHitNormal, nullptr);
}

inline SceneObj CurrentObject() {
    ScriptContext* c = ctxPtr();
    return SceneObj{ c ? c->object : nullptr };
}

inline SceneObj ResolveObject(const std::string& ref) {
    ScriptContext* c = ctxPtr();
    return SceneObj{ c ? c->ResolveObjectRef(ref) : nullptr };
}

inline bool HasUI(const SceneObject* obj) {
    return obj && ::HasUIComponent(*obj);
}

inline bool HasUI(const SceneObject& obj) {
    return ::HasUIComponent(obj);
}

inline void SetActive(SceneObject* obj, bool active) {
    if (!obj) return;
    if (obj->enabled == active) return;
    obj->enabled = active;
    if (ScriptContext* c = ctxPtr()) c->MarkDirty();
}

inline void SetActive(SceneObject& obj, bool active) {
    SetActive(&obj, active);
}

inline bool PlaySound(const std::string& objectRef, float volumeScale = 1.0f) {
    ScriptContext* c = ctxPtr();
    if (!c) return false;
    SceneObject* source = c->ResolveObjectRef(objectRef);
    if (!source || !source->hasAudioSource || source->audioSource.clipPath.empty()) return false;
    return c->PlayObjectAudioOneShot(source->id, source->audioSource.clipPath, volumeScale);
}

inline void MarkDirty() {
    if (ScriptContext* c = ctxPtr()) c->MarkDirty();
}

inline float DeltaTime() {
    return detail::gFrameDeltaTime;
}

inline bool PlaySoundClip(const std::string& clipPath, float volumeScale = 1.0f) {
    ScriptContext* c = ctxPtr();
    if (!c) return false;
    return c->PlayAudioOneShot(clipPath, volumeScale);
}

inline std::string GetSetting(const std::string& key, const std::string& fallback = "") {
    ScriptContext* c = ctxPtr();
    return c ? c->GetSetting(key, fallback) : fallback;
}

inline void SetSetting(const std::string& key, const std::string& value) {
    ScriptContext* c = ctxPtr();
    if (!c) return;
    // ctx.SetSetting always MarkDirty()s; skip the call entirely when the stored value already matches.
    static const std::string kSentinel("\x01__modu_unset__");
    if (c->GetSetting(key, kSentinel) == value) return;
    c->SetSetting(key, value);
}

inline bool GetSettingBool(const std::string& key, bool fallback = false) {
    ScriptContext* c = ctxPtr();
    return c ? c->GetSettingBool(key, fallback) : fallback;
}

inline void SetSettingBool(const std::string& key, bool value) {
    ScriptContext* c = ctxPtr();
    if (!c) return;
    // GetSettingBool falls back when the key is absent, so we must distinguish "absent" from "value matches fallback".
    static const std::string kSentinel("\x01__modu_unset__");
    const std::string raw = c->GetSetting(key, kSentinel);
    const std::string desired = value ? std::string("1") : std::string("0");
    if (raw == desired) return;
    c->SetSettingBool(key, value);
}

inline float GetSettingFloat(const std::string& key, float fallback = 0.0f) {
    ScriptContext* c = ctxPtr();
    return c ? c->GetSettingFloat(key, fallback) : fallback;
}

inline void SetSettingFloat(const std::string& key, float value) {
    ScriptContext* c = ctxPtr();
    if (!c) return;
    static const std::string kSentinel("\x01__modu_unset__");
    const std::string raw = c->GetSetting(key, kSentinel);
    if (raw != kSentinel) {
        try { if (std::stof(raw) == value) return; } catch (...) {}
    }
    c->SetSettingFloat(key, value);
}

inline Vector3 GetSettingVec3(const std::string& key, const Vector3& fallback = Vector3(0.0f)) {
    ScriptContext* c = ctxPtr();
    return c ? c->GetSettingVec3(key, fallback) : fallback;
}

inline void SetSettingVec3(const std::string& key, const Vector3& value) {
    ScriptContext* c = ctxPtr();
    if (!c) return;
    static const std::string kSentinel("\x01__modu_unset__");
    const std::string raw = c->GetSetting(key, kSentinel);
    if (raw != kSentinel) {
        // Re-parse and compare component-wise to avoid float-stringification round-trips.
        Vector3 current = c->GetSettingVec3(key, value);
        if (current == value) return;
    }
    c->SetSettingVec3(key, value);
}

// Cross-script setting writer that auto-dirties on real change.
// Wraps the legacy SetScriptSetting (which only reports change without dirtying).
inline bool WriteScriptSetting(ScriptComponent* script, const std::string& key, const std::string& value) {
    if (!script) return false;
    for (ScriptSetting& setting : script->settings) {
        if (setting.key == key) {
            if (setting.value == value) return false;
            setting.value = value;
            if (ScriptContext* c = ctxPtr()) c->MarkDirty();
            return true;
        }
    }
    script->settings.push_back(ScriptSetting{key, value});
    if (ScriptContext* c = ctxPtr()) c->MarkDirty();
    return true;
}

inline void SaveAutoSettings() {
    if (ScriptContext* c = ctxPtr()) c->SaveAutoSettings();
}

inline void SetPosition(const Vector3& pos) {
    ScriptContext* c = ctxPtr();
    if (!c) return;
    if (c->object && c->object->position == pos) return;
    c->SetPosition(pos);
}

inline void SetRotation(const Vector3& rot) {
    ScriptContext* c = ctxPtr();
    if (!c) return;
    if (c->object && c->object->rotation == rot) return;
    c->SetRotation(rot);
}

inline void SetScale(const Vector3& scl) {
    ScriptContext* c = ctxPtr();
    if (!c) return;
    if (c->object && c->object->scale == scl) return;
    c->SetScale(scl);
}

inline void SetFPSCap(bool enabled, float cap = 120.0f) {
    if (ScriptContext* c = ctxPtr()) c->SetFPSCap(enabled, cap);
}

inline bool EnsureCapsuleCollider(float height, float radius) {
    ScriptContext* c = ctxPtr();
    return c ? c->EnsureCapsuleCollider(height, radius) : false;
}

inline bool EnsureRigidbody(bool useGravity = true, bool kinematic = false) {
    ScriptContext* c = ctxPtr();
    return c ? c->EnsureRigidbody(useGravity, kinematic) : false;
}

namespace UI {
inline bool Button(const char* label) { return ::ModuGUI::Button(label ? label : ""); }
inline bool Checkbox(const char* label, bool* value) { return ::ModuGUI::Checkbox(label ? label : "", value); }
inline bool DragFloat(const char* label, float* v, float speed = 1.0f, float vMin = 0.0f, float vMax = 0.0f, const char* fmt = "%.3f") {
    return ::ModuGUI::DragFloat(label ? label : "", v, speed, vMin, vMax, fmt);
}
inline bool DragFloat2(const char* label, float v[2], float speed = 1.0f, float vMin = 0.0f, float vMax = 0.0f, const char* fmt = "%.3f") {
    return ::ModuGUI::DragFloat2(label ? label : "", v, speed, vMin, vMax, fmt);
}
inline bool DragFloat3(const char* label, float v[3], float speed = 1.0f, float vMin = 0.0f, float vMax = 0.0f, const char* fmt = "%.3f") {
    return ::ModuGUI::DragFloat3(label ? label : "", v, speed, vMin, vMax, fmt);
}
inline bool SliderFloat(const char* label, float* v, float vMin, float vMax, const char* fmt = "%.3f") {
    return ::ModuGUI::SliderFloat(label ? label : "", v, vMin, vMax, fmt);
}
} // namespace UI

} // namespace ModuCPP

#define MODUCPP_JOIN_IMPL(a, b) a##b
#define MODUCPP_JOIN(a, b) MODUCPP_JOIN_IMPL(a, b)
#define MODU_SCRIPT(ctx) \
    ::ModuCPP::Scope MODUCPP_JOIN(_modu_scope_, __LINE__){(ctx)}; \
    auto obj = ::ModuCPP::MakeObjectFacade((ctx))
