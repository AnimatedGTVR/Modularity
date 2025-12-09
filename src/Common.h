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

#include <glad/glad.h>
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"
#include "ThirdParty/imgui/backends/imgui_impl_glfw.h"
#include "ThirdParty/imgui/backends/imgui_impl_opengl3.h"
#include "ThirdParty/ImGuizmo/ImGuizmo.h"
#include "ThirdParty/glm/glm.hpp"
#include "ThirdParty/glm/gtc/matrix_transform.hpp"
#include "ThirdParty/glm/gtc/type_ptr.hpp"
#include "ThirdParty/glm/gtc/quaternion.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

// Constants
constexpr float SENSITIVITY = 0.1f;
constexpr float CAMERA_SPEED = 2.5f;
constexpr float FOV = 45.0f;
constexpr float NEAR_PLANE = 0.1f;
constexpr float FAR_PLANE = 100.0f;
constexpr float PI = 3.14159265359f;

// Forward declarations
class Mesh;
class OBJLoader;
class Renderer;
class Camera;
class ViewportController;
class Project;
class ProjectManager;
class Engine;

// Global OBJ loader instance (extern declaration)
extern OBJLoader g_objLoader;
