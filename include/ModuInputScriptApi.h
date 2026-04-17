#pragma once

#include "ModuCPPScriptApi.h"

#include <GLFW/glfw3.h>

namespace ModuCPP {

namespace detail {
inline ImGuiKey glfwToImGuiKey(int key) {
    switch (key) {
        case GLFW_KEY_W: return ImGuiKey_W;
        case GLFW_KEY_A: return ImGuiKey_A;
        case GLFW_KEY_S: return ImGuiKey_S;
        case GLFW_KEY_D: return ImGuiKey_D;
        case GLFW_KEY_E: return ImGuiKey_E;
        case GLFW_KEY_UP: return ImGuiKey_UpArrow;
        case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
        case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
        case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
        case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
        case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
        case GLFW_KEY_SPACE: return ImGuiKey_Space;
        case GLFW_KEY_ENTER: return ImGuiKey_Enter;
        case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
        default: return ImGuiKey_None;
    }
}
} // namespace detail

constexpr int KEY_W = GLFW_KEY_W;
constexpr int KEY_A = GLFW_KEY_A;
constexpr int KEY_S = GLFW_KEY_S;
constexpr int KEY_D = GLFW_KEY_D;
constexpr int KEY_E = GLFW_KEY_E;
constexpr int KEY_UP = GLFW_KEY_UP;
constexpr int KEY_DOWN = GLFW_KEY_DOWN;
constexpr int KEY_LEFT = GLFW_KEY_LEFT;
constexpr int KEY_RIGHT = GLFW_KEY_RIGHT;
constexpr int KEY_SHIFT_LEFT = GLFW_KEY_LEFT_SHIFT;
constexpr int KEY_SHIFT_RIGHT = GLFW_KEY_RIGHT_SHIFT;
constexpr int KEY_SPACE = GLFW_KEY_SPACE;
constexpr int KEY_ENTER = GLFW_KEY_ENTER;
constexpr int KEY_KP_ENTER = GLFW_KEY_KP_ENTER;

inline bool KeyDown(ScriptContext& ctx, int key) {
    return ctx.IsKeyDown(key, detail::glfwToImGuiKey(key));
}

inline bool KeyDown(int key) {
    if (ScriptContext* scriptCtx = ctxPtr()) return KeyDown(*scriptCtx, key);
    return false;
}

inline bool KeyPressed(ScriptContext& ctx, int key) {
    return ctx.IsKeyPressed(key, detail::glfwToImGuiKey(key));
}

inline bool KeyPressed(int key) {
    if (ScriptContext* scriptCtx = ctxPtr()) return KeyPressed(*scriptCtx, key);
    return false;
}

struct InputFacade {
    vec2 WASD() const {
        vec2 move(0.0f);
        if (KeyDown(KEY_W)) move.y += 1.0f;
        if (KeyDown(KEY_S)) move.y -= 1.0f;
        if (KeyDown(KEY_D)) move.x += 1.0f;
        if (KeyDown(KEY_A)) move.x -= 1.0f;
        return move;
    }

    vec2 WASDNormalized() const {
        vec2 move = WASD();
        const float len = glm::length(move);
        if (len > 1e-4f) {
            move /= len;
        }
        return move;
    }

    bool sprint() const {
        return KeyDown(KEY_SHIFT_LEFT) || KeyDown(KEY_SHIFT_RIGHT);
    }

    bool jump() const {
        return KeyDown(KEY_SPACE);
    }
};

inline const InputFacade input{};

inline bool IsRuntimeKeyDown(int glfwKey, ImGuiKey imguiKey) {
    if (ctxPtr()) {
        return KeyDown(glfwKey);
    }
    if (ImGui::IsKeyDown(imguiKey)) return true;
    GLFWwindow* window = glfwGetCurrentContext();
    if (!window) return false;
    return glfwGetKey(window, glfwKey) == GLFW_PRESS;
}

inline bool IsSubmitDown() {
    return IsRuntimeKeyDown(GLFW_KEY_ENTER, ImGuiKey_Enter) ||
           IsRuntimeKeyDown(GLFW_KEY_KP_ENTER, ImGuiKey_KeypadEnter);
}

} // namespace ModuCPP
