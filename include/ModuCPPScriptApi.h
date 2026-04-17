#pragma once

#include "ScriptRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace ModuCPP {

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using Vector2 = vec2;
using Vector3 = vec3;
using string = std::string;

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

template <typename T>
inline T Abs(const T& value) {
    using std::abs;
    return abs(value);
}
} // namespace Math

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

namespace detail {
inline thread_local ScriptContext* gCtx = nullptr;
inline thread_local float gFrameDeltaTime = 0.0f;
inline thread_local float gFrameFps = 0.0f;

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

struct ObjectFacade {
    ScriptContext* scriptCtx = nullptr;
    UILabelProxy UILabel{};

    explicit ObjectFacade(ScriptContext& ctx)
        : scriptCtx(&ctx), UILabel{ &ctx } {}

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

} // namespace ModuCPP

#define MODUCPP_JOIN_IMPL(a, b) a##b
#define MODUCPP_JOIN(a, b) MODUCPP_JOIN_IMPL(a, b)
#define MODU_SCRIPT(ctx) \
    ::ModuCPP::Scope MODUCPP_JOIN(_modu_scope_, __LINE__){(ctx)}; \
    auto obj = ::ModuCPP::MakeObjectFacade((ctx))
