#pragma once

#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <ctime>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cfloat>
#include <string>
#include <vector>
#include <memory>
#include <type_traits>

#ifdef _WIN32
// Keep Win32 macros from leaking into public script-facing headers and helper code.
// Windows headers must come before GLFW's APIENTRY definitions, otherwise MSVC
// can report duplicate-scope errors from minwindef.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#ifdef APIENTRY
#undef APIENTRY
#endif
#endif

#include "../include/Graphics/OpenGL.h"
#include "ThirdParty/ModuGUI/imgui.h"
#include "ThirdParty/ModuGUI/imgui_internal.h"
#include "ThirdParty/ModuGUI/backends/imgui_impl_glfw.h"
#include "ThirdParty/ModuGUI/backends/imgui_impl_opengl3.h"
#if MODULARITY_HAS_VULKAN
#include "ThirdParty/ModuGUI/backends/imgui_impl_vulkan.h"
#endif
#include "ThirdParty/ImGuizmo/ImGuizmo.h"
#include "ThirdParty/glm/glm.hpp"
#include "ThirdParty/glm/gtc/matrix_transform.hpp"
#include "ThirdParty/glm/gtc/type_ptr.hpp"
#include "ThirdParty/glm/gtc/quaternion.hpp"
#include "../include/Graphics/GraphicsBackend.h"

namespace fs = std::filesystem;

#ifndef MODULARITY_COMMON_SHARED_DECLS
#define MODULARITY_COMMON_SHARED_DECLS

// Constants
constexpr float SENSITIVITY = 0.1f;
constexpr float CAMERA_SPEED = 2.5f;
constexpr float FOV = 45.0f;
constexpr float NEAR_PLANE = 0.1f;
constexpr float FAR_PLANE = 100.0f;
constexpr float PI = 3.14159265359f;

inline glm::vec3 NormalizeEulerDegrees(const glm::vec3& deg) {
    auto wrap = [](float a) {
        float r = std::fmod(a, 360.0f);
        if (r < 0.0f) r += 360.0f;
        return r;
    };
    return glm::vec3(wrap(deg.x), wrap(deg.y), wrap(deg.z));
}

// Forward declarations
class Mesh;
class OBJLoader;
class Renderer;
class Camera;
class ViewportController;
class SceneObject;
class Project;
class ProjectManager;
class Engine;

// Global OBJ loader instance (extern declaration)
extern OBJLoader g_objLoader;

#endif // MODULARITY_COMMON_SHARED_DECLS
