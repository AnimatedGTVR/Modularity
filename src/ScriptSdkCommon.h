#pragma once

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/glm/glm.hpp"
#include "ThirdParty/glm/gtc/matrix_transform.hpp"
#include "ThirdParty/glm/gtc/quaternion.hpp"
#include "ThirdParty/glm/gtc/type_ptr.hpp"

namespace fs = std::filesystem;

#ifndef MODULARITY_COMMON_SHARED_DECLS
#define MODULARITY_COMMON_SHARED_DECLS

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

class Mesh;
class OBJLoader;
class Renderer;
class Camera;
class ViewportController;
class SceneObject;
class Project;
class ProjectManager;
class Engine;

extern OBJLoader g_objLoader;

#endif // MODULARITY_COMMON_SHARED_DECLS
