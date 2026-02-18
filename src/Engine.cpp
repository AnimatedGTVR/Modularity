#include "Engine.h"
#include "ModelLoader.h"
#include <iostream>
#include <fstream>
#include <functional>
#include <chrono>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <unordered_set>
#include <unordered_map>
#include <cmath>
#include <cctype>
#include <cstring>
#include <ctime>
#include "ThirdParty/glm/gtc/constants.hpp"
#include "ThirdParty/glfw/deps/stb_image_write.h"

#pragma region Material File IO Helpers
namespace {
struct MaterialFileData {
    MaterialProperties props;
    std::string albedo;
    std::string overlay;
    std::string normal;
    bool useOverlay = false;
    std::string vertexShader;
    std::string fragmentShader;
};

bool IsDefaultTransform(const SceneObject& obj) {
    auto nearZero = [](float v) { return std::abs(v) < 1e-4f; };
    auto nearOne = [](float v) { return std::abs(v - 1.0f) < 1e-4f; };
    return nearZero(obj.localPosition.x) &&
           nearZero(obj.localPosition.y) &&
           nearZero(obj.localPosition.z) &&
           nearZero(obj.localRotation.x) &&
           nearZero(obj.localRotation.y) &&
           nearZero(obj.localRotation.z) &&
           nearOne(obj.localScale.x) &&
           nearOne(obj.localScale.y) &&
           nearOne(obj.localScale.z);
}

void ApplyModelRootTransform(SceneObject& obj, const ModelSceneData& sceneData) {
    if (sceneData.nodes.empty()) return;
    if (obj.localInitialized && !IsDefaultTransform(obj)) return;
    const auto& root = sceneData.nodes.front();
    obj.localPosition = root.localPosition;
    obj.localRotation = root.localRotation;
    obj.localScale = root.localScale;
    obj.localInitialized = true;
    obj.position = obj.localPosition;
    obj.rotation = obj.localRotation;
    obj.scale = obj.localScale;
}

std::string sanitizeMaterialName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-') {
            out.push_back(c);
        } else if (c == ' ' || c == '.') {
            out.push_back('_');
        }
    }
    if (out.empty()) out = "Material";
    return out;
}

std::string resolveTexturePath(const std::string& texPath, const fs::path& modelPath) {
    if (texPath.empty() || texPath[0] == '*') return "";
    fs::path p(texPath);
    if (p.is_absolute()) return p.string();
    return (modelPath.parent_path() / p).string();
}

bool readMaterialFile(const std::string& path, MaterialFileData& outData) {
    std::ifstream f(path);
    if (!f.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(f, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        if (key == "color") {
            sscanf(val.c_str(), "%f,%f,%f", &outData.props.color.r, &outData.props.color.g, &outData.props.color.b);
        } else if (key == "ambient") {
            outData.props.ambientStrength = std::stof(val);
        } else if (key == "specular") {
            outData.props.specularStrength = std::stof(val);
        } else if (key == "shininess") {
            outData.props.shininess = std::stof(val);
        } else if (key == "textureMix") {
            outData.props.textureMix = std::stof(val);
        } else if (key == "textureFilter") {
            std::string lower = val;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower == "1" || lower == "point" || lower == "nearest") {
                outData.props.textureFilter = MaterialProperties::TextureFilter::Point;
            } else {
                outData.props.textureFilter = MaterialProperties::TextureFilter::Bilinear;
            }
        } else if (key == "albedo") {
            outData.albedo = val;
        } else if (key == "overlay") {
            outData.overlay = val;
        } else if (key == "normal") {
            outData.normal = val;
        } else if (key == "useOverlay") {
            outData.useOverlay = std::stoi(val) != 0;
        } else if (key == "vertexShader") {
            outData.vertexShader = val;
        } else if (key == "fragmentShader") {
            outData.fragmentShader = val;
        }
    }
    return true;
}

bool writeMaterialFile(const MaterialFileData& data, const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) {
        return false;
    }
    f << "# Material\n";
    f << "color=" << data.props.color.r << "," << data.props.color.g << "," << data.props.color.b << "\n";
    f << "ambient=" << data.props.ambientStrength << "\n";
    f << "specular=" << data.props.specularStrength << "\n";
    f << "shininess=" << data.props.shininess << "\n";
    f << "textureMix=" << data.props.textureMix << "\n";
    f << "textureFilter=" << static_cast<int>(data.props.textureFilter) << "\n";
    f << "useOverlay=" << (data.useOverlay ? 1 : 0) << "\n";
    f << "albedo=" << data.albedo << "\n";
    f << "overlay=" << data.overlay << "\n";
    f << "normal=" << data.normal << "\n";
    f << "vertexShader=" << data.vertexShader << "\n";
    f << "fragmentShader=" << data.fragmentShader << "\n";
    return true;
}

void ApplyObjectPreset(SceneObject& obj, ObjectType preset) {
    obj.type = preset;
    obj.hasRenderer = false;
    obj.renderType = RenderType::None;
    obj.hasLight = false;
    obj.hasCamera = false;
    obj.hasPostFX = false;
    obj.hasUI = false;
    obj.ui.type = UIElementType::None;

    switch (preset) {
        case ObjectType::Cube:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Cube;
            break;
        case ObjectType::Sphere:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Sphere;
            break;
        case ObjectType::Capsule:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Capsule;
            break;
        case ObjectType::OBJMesh:
            obj.hasRenderer = true;
            obj.renderType = RenderType::OBJMesh;
            break;
        case ObjectType::Model:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Model;
            break;
        case ObjectType::Mirror:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Mirror;
            obj.useOverlay = true;
            obj.material.textureMix = 1.0f;
            obj.material.color = glm::vec3(1.0f);
            obj.scale = glm::vec3(2.0f, 2.0f, 0.05f);
            break;
        case ObjectType::Plane:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Plane;
            obj.scale = glm::vec3(2.0f, 2.0f, 0.05f);
            break;
        case ObjectType::Torus:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Torus;
            break;
        case ObjectType::Sprite:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Sprite;
            obj.scale = glm::vec3(1.0f, 1.0f, 0.05f);
            obj.material.ambientStrength = 1.0f;
            break;
        case ObjectType::DirectionalLight:
            obj.hasLight = true;
            obj.light.type = LightType::Directional;
            break;
        case ObjectType::PointLight:
            obj.hasLight = true;
            obj.light.type = LightType::Point;
            obj.light.range = 12.0f;
            obj.light.intensity = 2.0f;
            break;
        case ObjectType::SpotLight:
            obj.hasLight = true;
            obj.light.type = LightType::Spot;
            obj.light.range = 15.0f;
            obj.light.intensity = 2.5f;
            break;
        case ObjectType::AreaLight:
            obj.hasLight = true;
            obj.light.type = LightType::Area;
            obj.light.range = 10.0f;
            obj.light.intensity = 3.0f;
            obj.light.size = glm::vec2(2.0f, 2.0f);
            break;
        case ObjectType::Camera:
            obj.hasCamera = true;
            obj.camera.type = SceneCameraType::Player;
            obj.camera.fov = 60.0f;
            break;
        case ObjectType::PostFXNode:
            obj.hasPostFX = true;
            obj.postFx.enabled = true;
            obj.postFx.bloomEnabled = true;
            obj.postFx.colorAdjustEnabled = true;
            break;
        case ObjectType::Canvas:
            obj.hasUI = true;
            obj.ui.type = UIElementType::Canvas;
            obj.ui.label = "Canvas";
            obj.ui.size = glm::vec2(600.0f, 400.0f);
            break;
        case ObjectType::UIImage:
            obj.hasUI = true;
            obj.ui.type = UIElementType::Image;
            obj.ui.label = "Image";
            obj.ui.size = glm::vec2(200.0f, 200.0f);
            break;
        case ObjectType::UISlider:
            obj.hasUI = true;
            obj.ui.type = UIElementType::Slider;
            obj.ui.label = "Slider";
            obj.ui.size = glm::vec2(240.0f, 32.0f);
            break;
        case ObjectType::UIButton:
            obj.hasUI = true;
            obj.ui.type = UIElementType::Button;
            obj.ui.label = "Button";
            obj.ui.size = glm::vec2(160.0f, 40.0f);
            break;
        case ObjectType::UIText:
            obj.hasUI = true;
            obj.ui.type = UIElementType::Text;
            obj.ui.label = "Text";
            obj.ui.size = glm::vec2(240.0f, 32.0f);
            break;
        case ObjectType::Sprite2D:
            obj.hasUI = true;
            obj.ui.type = UIElementType::Sprite2D;
            obj.ui.label = "Sprite2D";
            obj.ui.size = glm::vec2(128.0f, 128.0f);
            break;
        case ObjectType::Empty:
        default:
            break;
    }
}

RawMeshAsset buildCubeRMesh() {
    RawMeshAsset mesh;
    mesh.positions.reserve(24);
    mesh.normals.reserve(24);
    mesh.uvs.reserve(24);
    mesh.faces.reserve(12);

    const float h = 0.5f;
    auto pushFace = [&](const glm::vec3& n, const glm::vec3& uAxis, const glm::vec3& vAxis,
                        const glm::vec3& v0, const glm::vec3& v1,
                        const glm::vec3& v2, const glm::vec3& v3) {
        uint32_t base = static_cast<uint32_t>(mesh.positions.size());
        mesh.positions.push_back(v0);
        mesh.positions.push_back(v1);
        mesh.positions.push_back(v2);
        mesh.positions.push_back(v3);
        mesh.normals.push_back(n);
        mesh.normals.push_back(n);
        mesh.normals.push_back(n);
        mesh.normals.push_back(n);
        auto toUv = [&](const glm::vec3& p) -> glm::vec2 {
            float u = glm::dot(p, uAxis) / (2.0f * h) + 0.5f;
            float v = glm::dot(p, vAxis) / (2.0f * h) + 0.5f;
            return glm::vec2(u, v);
        };
        mesh.uvs.push_back(toUv(v0));
        mesh.uvs.push_back(toUv(v1));
        mesh.uvs.push_back(toUv(v2));
        mesh.uvs.push_back(toUv(v3));
        mesh.faces.push_back(glm::u32vec3(base, base + 1, base + 2));
        mesh.faces.push_back(glm::u32vec3(base, base + 2, base + 3));
    };

    // +Z (front)
    pushFace(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
             glm::vec3(-h, -h,  h), glm::vec3( h, -h,  h),
             glm::vec3( h,  h,  h), glm::vec3(-h,  h,  h));
    // -Z (back)
    pushFace(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
             glm::vec3( h, -h, -h), glm::vec3(-h, -h, -h),
             glm::vec3(-h,  h, -h), glm::vec3( h,  h, -h));
    // -X (left)
    pushFace(glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f),
             glm::vec3(-h, -h, -h), glm::vec3(-h, -h,  h),
             glm::vec3(-h,  h,  h), glm::vec3(-h,  h, -h));
    // +X (right)
    pushFace(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f),
             glm::vec3( h, -h,  h), glm::vec3( h, -h, -h),
             glm::vec3( h,  h, -h), glm::vec3( h,  h,  h));
    // +Y (top)
    pushFace(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
             glm::vec3(-h,  h,  h), glm::vec3( h,  h,  h),
             glm::vec3( h,  h, -h), glm::vec3(-h,  h, -h));
    // -Y (bottom)
    pushFace(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
             glm::vec3(-h, -h, -h), glm::vec3( h, -h, -h),
             glm::vec3( h, -h,  h), glm::vec3(-h, -h,  h));

    mesh.boundsMin = glm::vec3(-h);
    mesh.boundsMax = glm::vec3(h);
    mesh.hasNormals = true;
    mesh.hasUVs = true;
    return mesh;
}

RawMeshAsset buildPlaneRMesh() {
    RawMeshAsset mesh;
    mesh.positions = {
        glm::vec3(-0.5f, 0.0f,  0.5f),
        glm::vec3( 0.5f, 0.0f,  0.5f),
        glm::vec3( 0.5f, 0.0f, -0.5f),
        glm::vec3(-0.5f, 0.0f, -0.5f),
    };
    mesh.normals = {
        glm::vec3(0, 1, 0),
        glm::vec3(0, 1, 0),
        glm::vec3(0, 1, 0),
        glm::vec3(0, 1, 0),
    };
    mesh.uvs = {
        glm::vec2(0, 0),
        glm::vec2(1, 0),
        glm::vec2(1, 1),
        glm::vec2(0, 1),
    };
    mesh.faces = {
        glm::u32vec3(0, 1, 2),
        glm::u32vec3(0, 2, 3),
    };
    mesh.boundsMin = glm::vec3(-0.5f, 0.0f, -0.5f);
    mesh.boundsMax = glm::vec3(0.5f, 0.0f, 0.5f);
    mesh.hasNormals = true;
    mesh.hasUVs = true;
    return mesh;
}

RawMeshAsset buildSphereRMesh(int slices = 24, int stacks = 16) {
    RawMeshAsset mesh;
    const float radius = 0.5f;
    for (int i = 0; i <= stacks; ++i) {
        float v = static_cast<float>(i) / static_cast<float>(stacks);
        float phi = v * glm::pi<float>();
        float y = std::cos(phi);
        float r = std::sin(phi);
        for (int j = 0; j <= slices; ++j) {
            float u = static_cast<float>(j) / static_cast<float>(slices);
            float theta = u * glm::two_pi<float>();
            float x = r * std::cos(theta);
            float z = r * std::sin(theta);
            glm::vec3 pos = glm::vec3(x, y, z) * radius;
            mesh.positions.push_back(pos);
            mesh.normals.push_back(glm::normalize(glm::vec3(x, y, z)));
            mesh.uvs.push_back(glm::vec2(u, 1.0f - v));
        }
    }

    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            uint32_t i0 = i * (slices + 1) + j;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + (slices + 1);
            uint32_t i3 = i2 + 1;
            mesh.faces.push_back(glm::u32vec3(i0, i2, i1));
            mesh.faces.push_back(glm::u32vec3(i1, i2, i3));
        }
    }

    mesh.boundsMin = glm::vec3(-radius);
    mesh.boundsMax = glm::vec3(radius);
    mesh.hasNormals = true;
    mesh.hasUVs = true;
    return mesh;
}
} // namespace
#pragma endregion

#pragma region Build Helpers
namespace {
bool runCommandCapture(const std::string& command, std::string& output) {
    std::array<char, 256> buffer{};
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        output = "Failed to spawn process: " + command;
        return false;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

#ifdef _WIN32
    int returnCode = _pclose(pipe);
#else
    int returnCode = pclose(pipe);
#endif
    if (returnCode != 0) {
        return false;
    }
    return true;
}

bool runCommandStreaming(const std::string& command,
                         const std::function<void(const std::string&)>& onChunk,
                         int* exitCodeOut) {
    std::array<char, 256> buffer{};
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        if (exitCodeOut) *exitCodeOut = -1;
        return false;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        if (onChunk) {
            onChunk(buffer.data());
        }
    }

#ifdef _WIN32
    int returnCode = _pclose(pipe);
#else
    int returnCode = pclose(pipe);
#endif
    if (exitCodeOut) *exitCodeOut = returnCode;
    return returnCode == 0;
}

fs::path resolveExecutablePath(const fs::path& buildRoot, const char* exeBaseName) {
#ifdef _WIN32
    std::string exeName = std::string(exeBaseName) + ".exe";
#else
    std::string exeName = exeBaseName;
#endif

    std::vector<fs::path> candidates;
    candidates.push_back(buildRoot / exeName);
    candidates.push_back(buildRoot / "Release" / exeName);
    candidates.push_back(buildRoot / "RelWithDebInfo" / exeName);
    candidates.push_back(buildRoot / "MinSizeRel" / exeName);
    candidates.push_back(buildRoot / "Debug" / exeName);

    for (const auto& path : candidates) {
        if (fs::exists(path)) return path;
    }

    for (const auto& entry : fs::recursive_directory_iterator(buildRoot)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().filename() == exeName) return entry.path();
    }
    return {};
}

fs::path findCMakeSourceRoot(const fs::path& start) {
    std::error_code ec;
    fs::path cur = fs::absolute(start, ec);
    if (ec) return {};
    while (!cur.empty()) {
        fs::path candidate = cur / "CMakeLists.txt";
        if (fs::exists(candidate)) return cur;
        if (!cur.has_parent_path()) break;
        fs::path parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }
    return {};
}

bool copyDirectoryRecursive(const fs::path& from, const fs::path& to, std::string& error) {
    std::error_code ec;
    if (!fs::exists(from)) return true;
    fs::create_directories(to, ec);
    if (ec) {
        error = "Failed to create directory: " + to.string();
        return false;
    }

    for (const auto& entry : fs::recursive_directory_iterator(from)) {
        const auto& src = entry.path();
        fs::path rel = fs::relative(src, from, ec);
        if (ec) {
            error = "Failed to resolve relative path: " + src.string();
            return false;
        }
        fs::path dst = to / rel;
        if (entry.is_directory()) {
            fs::create_directories(dst, ec);
            if (ec) {
                error = "Failed to create directory: " + dst.string();
                return false;
            }
        } else if (entry.is_regular_file()) {
            fs::create_directories(dst.parent_path(), ec);
            if (ec) {
                error = "Failed to create directory: " + dst.parent_path().string();
                return false;
            }
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                error = "Failed to copy file: " + src.string();
                return false;
            }
        }
    }
    return true;
}

bool copyPrecompiledPackages(const fs::path& buildRoot, const fs::path& outDir, std::string& error) {
    std::error_code ec;
    if (!fs::exists(buildRoot)) return true;

    if (fs::exists(outDir)) {
        fs::remove_all(outDir, ec);
        if (ec) {
            error = "Failed to clear package cache: " + outDir.string();
            return false;
        }
    }
    fs::create_directories(outDir, ec);
    if (ec) {
        error = "Failed to create package folder: " + outDir.string();
        return false;
    }

    const std::vector<std::string> exts = { ".a", ".so", ".dylib", ".lib", ".dll" };
    for (const auto& entry : fs::recursive_directory_iterator(buildRoot)) {
        if (!entry.is_regular_file()) continue;
        fs::path src = entry.path();
        std::string ext = src.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (std::find(exts.begin(), exts.end(), ext) == exts.end()) {
            continue;
        }

        fs::path dst = outDir / src.filename();
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Failed to copy package binary: " + src.string();
            return false;
        }
    }
    return true;
}

bool copyPrecompiledEnginePackages(const fs::path& buildRoot, const fs::path& outDir, std::string& error) {
    std::error_code ec;
    if (!fs::exists(buildRoot)) return true;

    if (fs::exists(outDir)) {
        fs::remove_all(outDir, ec);
        if (ec) {
            error = "Failed to clear engine package cache: " + outDir.string();
            return false;
        }
    }
    fs::create_directories(outDir, ec);
    if (ec) {
        error = "Failed to create engine package folder: " + outDir.string();
        return false;
    }

    auto isEngineLib = [](const std::string& filename) {
        std::string name = filename;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        return name.rfind("libcore", 0) == 0 ||
               name.rfind("core_player", 0) == 0 ||
               name.rfind("core.", 0) == 0 ||
               name.rfind("core_player.", 0) == 0;
    };

    const std::vector<std::string> exts = { ".a", ".so", ".dylib", ".lib", ".dll" };
    for (const auto& entry : fs::recursive_directory_iterator(buildRoot)) {
        if (!entry.is_regular_file()) continue;
        fs::path src = entry.path();
        std::string ext = src.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (std::find(exts.begin(), exts.end(), ext) == exts.end()) {
            continue;
        }
        if (!isEngineLib(src.filename().string())) {
            continue;
        }

        fs::path dst = outDir / src.filename();
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Failed to copy engine binary: " + src.string();
            return false;
        }
    }
    return true;
}

std::string sanitizeBuildToken(const std::string& value, const char* fallback) {
    std::string out;
    out.reserve(value.size());
    bool lastWasDash = false;
    for (char c : value) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            out.push_back(c);
            lastWasDash = false;
        } else if (c == '.' || c == '_' || c == '-') {
            out.push_back(c);
            lastWasDash = (c == '-');
        } else if (std::isspace(uc)) {
            if (!lastWasDash && !out.empty()) {
                out.push_back('-');
                lastWasDash = true;
            }
        }
    }
    while (!out.empty() && (out.back() == '-' || out.back() == '.')) {
        out.pop_back();
    }
    if (out.empty()) out = fallback;
    return out;
}

std::string quotePath(const fs::path& path) {
    std::string value = path.string();
    size_t pos = 0;
    while ((pos = value.find('"', pos)) != std::string::npos) {
        value.insert(pos, "\\");
        pos += 2;
    }
    return "\"" + value + "\"";
}

void cleanExportOutput(const fs::path& exportRoot, const char* exeBaseName, std::string& error) {
    std::error_code ec;
#ifdef _WIN32
    fs::path exePath = exportRoot / (std::string(exeBaseName) + ".exe");
#else
    fs::path exePath = exportRoot / exeBaseName;
#endif
    if (fs::exists(exePath)) {
        fs::remove(exePath, ec);
        if (ec) {
            error = "Failed to remove existing executable.";
            return;
        }
    }

    fs::path projectDir = exportRoot / "Project";
    if (fs::exists(projectDir)) {
        fs::remove_all(projectDir, ec);
        if (ec) {
            error = "Failed to remove existing project files.";
            return;
        }
    }

    fs::path resourcesDir = exportRoot / "Resources";
    if (fs::exists(resourcesDir)) {
        fs::remove_all(resourcesDir, ec);
        if (ec) {
            error = "Failed to remove existing resources.";
            return;
        }
    }

    fs::path packagesDir = exportRoot / "Packages";
    if (fs::exists(packagesDir)) {
        fs::remove_all(packagesDir, ec);
        if (ec) {
            error = "Failed to remove existing packages.";
            return;
        }
    }

    fs::path autostart = exportRoot / "autostart.modu";
    if (fs::exists(autostart)) {
        fs::remove(autostart, ec);
        if (ec) {
            error = "Failed to remove existing autostart.modu.";
            return;
        }
    }
}

void cleanEditorExecutable(const fs::path& buildRoot) {
    std::error_code ec;
#ifdef _WIN32
    fs::path editorExe = buildRoot / "Modularity.exe";
#else
    fs::path editorExe = buildRoot / "Modularity";
#endif
    if (fs::exists(editorExe)) {
        fs::remove(editorExe, ec);
    }
}
} // namespace
#pragma endregion

#pragma region Window + Selection Utilities
void window_size_callback(GLFWwindow* window, int width, int height) {
    (void)window;
    glViewport(0, 0, width, height);
}

SceneObject* Engine::getSelectedObject() {
    if (selectedObjectId == -1) return nullptr;
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
        [this](const SceneObject& obj) { return obj.id == selectedObjectId; });
    return (it != sceneObjects.end()) ? &(*it) : nullptr;
}

glm::vec3 Engine::getSelectionCenterWorld(bool worldSpace) const {
    if (selectedObjectIds.empty()) return glm::vec3(0.0f);
    glm::vec3 acc(0.0f);
    int count = 0;
    auto findObj = [&](int id) -> const SceneObject* {
        auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(), [id](const SceneObject& o){ return o.id == id; });
        return it == sceneObjects.end() ? nullptr : &(*it);
    };
    for (int id : selectedObjectIds) {
        const SceneObject* o = findObj(id);
        if (!o) continue;
        acc += worldSpace ? o->position : glm::vec3(0.0f);
        count++;
    }
    if (count == 0) return glm::vec3(0.0f);
    return acc / (float)count;
}

void Engine::setPrimarySelection(int id, bool additive) {
    if (!additive) {
        selectedObjectIds.clear();
    }
    if (id >= 0) {
        selectedObjectIds.push_back(id);
        selectedObjectId = id;
    } else {
        selectedObjectIds.clear();
        selectedObjectId = -1;
    }
}

void Engine::clearSelection() {
    selectedObjectIds.clear();
    selectedObjectId = -1;
}

Camera Engine::makeCameraFromObject(const SceneObject& obj) const {
    Camera cam;
    cam.position = obj.position;
    glm::quat q = glm::quat(glm::radians(obj.rotation));
    glm::mat3 rot = glm::mat3_cast(q);
    cam.front = glm::normalize(rot * glm::vec3(0.0f, 0.0f, -1.0f));
    cam.up = glm::normalize(rot * glm::vec3(0.0f, 1.0f, 0.0f));
    if (!std::isfinite(cam.front.x) || glm::length(cam.front) < 1e-3f) {
        cam.front = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    if (!std::isfinite(cam.up.x) || glm::length(cam.up) < 1e-3f) {
        cam.up = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return cam;
}
#pragma endregion

#pragma region Transform Helpers
namespace {
// Equivalent to glm::extractEulerAngleXYZ without depending on the experimental header.
glm::vec3 ExtractEulerXYZ(const glm::mat3& m) {
    float T1 = std::atan2(m[2][1], m[2][2]);
    float C2 = std::sqrt(m[0][0] * m[0][0] + m[1][0] * m[1][0]);
    float T2 = std::atan2(-m[2][0], C2);
    float S1 = std::sin(T1);
    float C1 = std::cos(T1);
    float T3 = std::atan2(S1 * m[0][2] - C1 * m[0][1], C1 * m[1][1] - S1 * m[1][2]);
    // GLM's extractEulerAngleXYZ returns (-T1, -T2, -T3)
    return glm::vec3(-T1, -T2, -T3);
}

glm::quat QuatFromEulerXYZ(const glm::vec3& deg) {
    glm::vec3 r = glm::radians(deg);
    glm::mat4 m(1.0f);
    m = glm::rotate(m, r.x, glm::vec3(1.0f, 0.0f, 0.0f));
    m = glm::rotate(m, r.y, glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, r.z, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::quat_cast(glm::mat3(m));
}

fs::path findManagedProjectRoot(const fs::path& start) {
    std::error_code ec;
    fs::path current = start;
    for (int depth = 0; depth < 6 && !current.empty(); ++depth) {
        fs::path candidate = current / "Scripts" / "Managed" / "ModuCPP.csproj";
        if (fs::exists(candidate, ec)) {
            return current;
        }
        current = current.parent_path();
    }
    return {};
}

fs::path managedOutputPathFromProject(const fs::path& managedProject) {
    if (managedProject.empty()) return {};
    return managedProject.parent_path() / "bin" / "Debug" / "netstandard2.0" / "ModuCPP.dll";
}

bool isManagedGeneratedPath(const fs::path& path) {
    for (const auto& part : path) {
        std::string token = part.string();
        std::transform(token.begin(), token.end(), token.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (token == "obj" || token == "bin" || token == ".vs" || token == ".git") {
            return true;
        }
    }
    return false;
}

fs::path findEngineManagedRoot() {
    std::vector<fs::path> candidates;
    candidates.push_back(fs::current_path());
    candidates.push_back(fs::current_path().parent_path());
#if defined(__linux__)
    {
        std::error_code ec;
        fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            candidates.push_back(exe.parent_path());
            candidates.push_back(exe.parent_path().parent_path());
        }
    }
#elif defined(_WIN32)
    {
        wchar_t buffer[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        if (len > 0) {
            fs::path exe(buffer);
            candidates.push_back(exe.parent_path());
            candidates.push_back(exe.parent_path().parent_path());
        }
    }
#endif

    std::error_code ec;
    for (const auto& root : candidates) {
        if (root.empty()) continue;
        fs::path candidate = root / "Scripts" / "Managed" / "ModuCPP.cs";
        if (fs::exists(candidate, ec)) {
            return root;
        }
    }
    return {};
}

bool ensureProjectManagedCsproj(const fs::path& projectRoot, const fs::path& engineRoot, std::string& error) {
    if (projectRoot.empty() || engineRoot.empty()) {
        error = "Managed project setup failed: missing project or engine root.";
        return false;
    }
    fs::path managedDir = projectRoot / "Scripts" / "Managed";
    fs::path csprojPath = managedDir / "ModuCPP.csproj";
    std::error_code ec;
    if (fs::exists(csprojPath, ec)) return true;
    fs::create_directories(managedDir, ec);
    if (ec) {
        error = "Managed project setup failed: unable to create " + managedDir.string();
        return false;
    }

    fs::path engineApi = engineRoot / "Scripts" / "Managed" / "ModuCPP.cs";
    if (!fs::exists(engineApi, ec)) {
        error = "Managed project setup failed: engine ModuCPP.cs not found at " + engineApi.string();
        return false;
    }

    std::ofstream file(csprojPath);
    if (!file.is_open()) {
        error = "Managed project setup failed: unable to write " + csprojPath.string();
        return false;
    }

    file << "<Project Sdk=\"Microsoft.NET.Sdk\">\n";
    file << "  <PropertyGroup>\n";
    file << "    <TargetFramework>netstandard2.0</TargetFramework>\n";
    file << "    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n";
    file << "    <ImplicitUsings>enable</ImplicitUsings>\n";
    file << "    <Nullable>enable</Nullable>\n";
    file << "    <LangVersion>latest</LangVersion>\n";
    file << "    <GenerateRuntimeConfigurationFiles>false</GenerateRuntimeConfigurationFiles>\n";
    file << "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n";
    file << "  </PropertyGroup>\n";
    file << "  <ItemGroup>\n";
    file << "    <Compile Include=\"*.cs\" />\n";
    file << "    <Compile Include=\"**/*.cs\" />\n";
    file << "    <Compile Include=\"../*.cs\" />\n";
    file << "    <Compile Include=\"../**/*.cs\" Exclude=\"../Managed/**\" />\n";
    file << "    <Compile Include=\"" << engineApi.generic_string() << "\" Link=\"ModuCPP.cs\" />\n";
    file << "  </ItemGroup>\n";
    file << "</Project>\n";
    return true;
}
}

void Engine::DecomposeMatrix(const glm::mat4& matrix, glm::vec3& pos, glm::vec3& rot, glm::vec3& scale) {
    pos = glm::vec3(matrix[3]);
    scale.x = glm::length(glm::vec3(matrix[0]));
    scale.y = glm::length(glm::vec3(matrix[1]));
    scale.z = glm::length(glm::vec3(matrix[2]));

    glm::mat3 rotMat(matrix);
    if (scale.x != 0.0f) rotMat[0] /= scale.x;
    if (scale.y != 0.0f) rotMat[1] /= scale.y;
    if (scale.z != 0.0f) rotMat[2] /= scale.z;
    // Orthonormalize to reduce shear-induced rotation jitter.
    rotMat[0] = glm::normalize(rotMat[0]);
    rotMat[1] = glm::normalize(rotMat[1] - rotMat[0] * glm::dot(rotMat[0], rotMat[1]));
    rotMat[2] = glm::normalize(glm::cross(rotMat[0], rotMat[1]));

    // Use explicit XYZ extraction so yaw isn't clamped to [-90, 90] like glm::yaw/pitch/roll.
    rot = ExtractEulerXYZ(rotMat);
}

glm::mat4 Engine::ComposeTransform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale) {
    glm::mat4 m(1.0f);
    m = glm::translate(m, position);
    m *= glm::mat4_cast(rotation);
    m = glm::scale(m, scale);
    return m;
}

glm::mat4 Engine::ComposeTransform(const glm::vec3& position, const glm::vec3& rotationDeg, const glm::vec3& scale) {
    return ComposeTransform(position, QuatFromEulerXYZ(rotationDeg), scale);
}
#pragma endregion

#pragma region Undo / Redo
void Engine::recordState(const char* /*reason*/) {
    SceneSnapshot snap;
    snap.objects = sceneObjects;
    snap.selectedIds = selectedObjectIds;
    snap.nextId = nextObjectId;

    undoStack.push_back(std::move(snap));
    if (undoStack.size() > 64) {
        undoStack.erase(undoStack.begin());
    }
    redoStack.clear();
}

void Engine::undo() {
    if (undoStack.empty()) return;

    SceneSnapshot current;
    current.objects = sceneObjects;
    current.selectedIds = selectedObjectIds;
    current.nextId = nextObjectId;

    SceneSnapshot snap = undoStack.back();
    undoStack.pop_back();

    redoStack.push_back(std::move(current));
    sceneObjects = std::move(snap.objects);
    selectedObjectIds = snap.selectedIds;
    selectedObjectId = selectedObjectIds.empty() ? -1 : selectedObjectIds.back();
    nextObjectId = snap.nextId;
    projectManager.currentProject.hasUnsavedChanges = true;
}

void Engine::redo() {
    if (redoStack.empty()) return;

    SceneSnapshot current;
    current.objects = sceneObjects;
    current.selectedIds = selectedObjectIds;
    current.nextId = nextObjectId;

    SceneSnapshot snap = redoStack.back();
    redoStack.pop_back();

    undoStack.push_back(std::move(current));
    sceneObjects = std::move(snap.objects);
    selectedObjectIds = snap.selectedIds;
    selectedObjectId = selectedObjectIds.empty() ? -1 : selectedObjectIds.back();
    nextObjectId = snap.nextId;
    projectManager.currentProject.hasUnsavedChanges = true;
}
#pragma endregion

fs::path resolveScriptsConfigPath(const Project& project) {
    std::error_code ec;
    if (!project.scriptsConfigPath.empty() && fs::exists(project.scriptsConfigPath, ec)) {
        return project.scriptsConfigPath;
    }
    fs::path lower = project.projectPath / "scripts.modu";
    if (fs::exists(lower, ec)) {
        return lower;
    }
    return project.projectPath / "Scripts.modu";
}

#pragma region Engine Lifecycle
bool Engine::init() {
    std::cerr << "[DEBUG] Creating window..." << std::endl;
    editorWindow = window.makeWindow();
    if (!editorWindow) {
        std::cerr << "[DEBUG] Window creation failed!" << std::endl;
        return false;
    }
    std::cerr << "[DEBUG] Window created successfully" << std::endl;

    glfwSetWindowUserPointer(editorWindow, this);
    glfwSetWindowSizeCallback(editorWindow, window_size_callback);

    auto mouse_cb = [](GLFWwindow* window, double xpos, double ypos) {
        auto* engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
        if (!engine) return;

        int cursorMode = glfwGetInputMode(window, GLFW_CURSOR);
        if (!engine->viewportController.isViewportFocused() || cursorMode != GLFW_CURSOR_DISABLED) {
            return;
        }

        engine->camera.processMouse(xpos, ypos);
    };
    glfwSetCursorPosCallback(editorWindow, mouse_cb);

    std::cerr << "[DEBUG] Setting up ImGui..." << std::endl;
    setupImGui();
    std::cerr << "[DEBUG] ImGui setup complete" << std::endl;

    if (!audio.init()) {
        std::cerr << "[DEBUG] Audio init failed\n";
        addConsoleMessage("Audio initialization failed. Audio playback will be disabled.", ConsoleMessageType::Warning);
    }
    
    logToConsole("Engine initialized - Waiting for project selection");
    loadAutoStartConfig();
#ifdef MODULARITY_PLAYER
    playerMode = true;
    autoStartPlayerMode = true;
#endif
    if (autoStartRequested && !autoStartProjectPath.empty()) {
        startProjectLoad(autoStartProjectPath);
    }
    return true;
}

bool Engine::initRenderer() {
    if (rendererInitialized) return true;

    try {
        renderer.initialize();
        rendererInitialized = true;
        return true;
    } catch (...) {
        return false;
    }
}

void Engine::run() {
    std::cerr << "[DEBUG] Entering main loop, showLauncher=" << showLauncher << std::endl;
    
    while (!glfwWindowShouldClose(editorWindow)) {
        double frameStart = glfwGetTime();
        if (glfwGetWindowAttrib(editorWindow, GLFW_ICONIFIED)) {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        deltaTime = std::min(deltaTime, 1.0f / 30.0f);

        glfwPollEvents();
        pollProjectLoad();
        pollSceneLoad();

        if (!showLauncher) {
            handleKeyboardShortcuts();
        }

        if (gameViewCursorLocked) {
            cursorLocked = false;
            viewportController.setFocused(false);
        }
        viewportController.update(editorWindow, cursorLocked);
        if (!isPlaying) {
            gameViewCursorLocked = false;
        }

        // Scroll-wheel speed adjustment while freelook is active
        if (viewportController.isViewportFocused() && cursorLocked) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (std::abs(wheel) > 0.0001f) {
                float factor = std::pow(1.12f, wheel);
                float ratio = (camera.moveSpeed > 0.001f) ? (camera.sprintSpeed / camera.moveSpeed) : 2.0f;
                camera.moveSpeed = std::clamp(camera.moveSpeed * factor, 0.5f, 100.0f);
                camera.sprintSpeed = std::clamp(camera.moveSpeed * ratio, 0.5f, 200.0f);
            }
        }

        if (viewportController.isViewportFocused() && cursorLocked) {
            camera.processKeyboard(deltaTime, editorWindow);
        }

        // Run scripts only in play/spec/test modes to avoid edit-time side effects (e.g., cursor grabs)
        if (projectManager.currentProject.isLoaded) {
            bool runScripts = isPlaying || specMode || testMode;
            if (runScripts) {
                updateScripts(deltaTime);
            }
        }

        if (isPlaying) {
            updatePlayerController(deltaTime);
        }

        bool simulate2D = (isPlaying && !isPaused) || (!isPlaying && specMode) || (!isPlaying && testMode);
        if (simulate2D) {
            updateRigidbody2D(deltaTime);
        }

        updateCameraFollow2D(deltaTime);

        updateSkeletalAnimations(deltaTime);
        updateHierarchyWorldTransforms();

        bool simulatePhysics = physics.isReady() && ((isPlaying && !isPaused) || (!isPlaying && specMode));
        if (simulatePhysics) {
            physics.simulate(deltaTime, sceneObjects);
        }
        bool runAI = isPlaying || specMode || testMode;
        if (runAI) {
            updateAIAgents(deltaTime);
        }

        updateHierarchyWorldTransforms();
        updateSkinningMatrices();

        if (playerMode) {
            syncPlayerCamera();
        }

        auto pickRuntimeCameraObject = [this]() -> const SceneObject* {
            const SceneObject* playerCam = nullptr;
            const SceneObject* sceneCam = nullptr;
            const SceneObject* anyCam = nullptr;
            for (const auto& obj : sceneObjects) {
                if (!obj.enabled || !obj.hasCamera) continue;
                if (!anyCam) anyCam = &obj;
                if (!sceneCam && obj.camera.type == SceneCameraType::Scene) {
                    sceneCam = &obj;
                }
                if (obj.camera.type == SceneCameraType::Player) {
                    playerCam = &obj;
                    break;
                }
            }
            if (playerCam) return playerCam;
            if (sceneCam) return sceneCam;
            return anyCam;
        };

        bool audioShouldPlay = isPlaying || specMode || testMode;
        Camera listenerCamera = camera;
        if (const SceneObject* runtimeCam = pickRuntimeCameraObject()) {
            listenerCamera = makeCameraFromObject(*runtimeCam);
            listenerCamera.position = runtimeCam->position;
        }
        audio.update(sceneObjects, listenerCamera, audioShouldPlay);

        updateCompileJob();
        updateAutoCompileScripts();
        processAutoCompileQueue();
        pollExportBuild();

        if (playerMode && !showLauncher) {
            int displayW = 0;
            int displayH = 0;
            glfwGetFramebufferSize(editorWindow, &displayW, &displayH);
            if (displayW > 0 && displayH > 0) {
                viewportWidth = displayW;
                viewportHeight = displayH;
                if (rendererInitialized) {
                    renderer.resize(viewportWidth, viewportHeight);
                }
            }
        }

        if (!showLauncher && projectManager.currentProject.isLoaded && rendererInitialized) {
            glm::mat4 view = camera.getViewMatrix();
            float renderFov = buildSettings.editorCameraFov;
            float renderNear = buildSettings.editorCameraNear;
            float renderFar = buildSettings.editorCameraFar;
            if (playerMode) {
                if (const SceneObject* runtimeCam = pickRuntimeCameraObject()) {
                    renderFov = runtimeCam->camera.fov;
                    renderNear = std::max(0.01f, runtimeCam->camera.nearClip);
                    renderFar = std::max(renderNear + 0.01f, runtimeCam->camera.farClip);
                }
            }
            float aspect = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
            if (aspect <= 0.0f) aspect = 1.0f;
            glm::mat4 proj = glm::perspective(glm::radians(renderFov),
                                              aspect,
                                              renderNear,
                                              renderFar);

            renderer.beginRender(view, proj, camera.position);
            renderer.renderScene(camera, sceneObjects, selectedObjectId,
                                 renderFov,
                                 renderNear,
                                 renderFar,
                                 collisionWireframe);
            renderer.endRender();
        }

        if (firstFrame) {
            std::cerr << "[DEBUG] First frame: starting ImGui NewFrame" << std::endl;
        }
        
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        uiCanvas3DInputs.clear();

        if (pendingWorkspaceReload) {
            ImGuiID dockspaceId = ImGui::GetID("MainDockspace");
            ImGui::DockBuilderRemoveNode(dockspaceId);
            if (!pendingWorkspaceIniPath.empty() && fs::exists(pendingWorkspaceIniPath)) {
                ImGui::LoadIniSettingsFromDisk(pendingWorkspaceIniPath.string().c_str());
            }
            pendingWorkspaceReload = false;
        }

        if (firstFrame) {
            std::cerr << "[DEBUG] First frame: ImGui NewFrame complete, rendering UI..." << std::endl;
        }

        if (showLauncher) {
            if (firstFrame) {
                std::cerr << "[DEBUG] First frame: calling renderLauncher()" << std::endl;
            }
            #ifdef MODULARITY_PLAYER
            renderPlayerViewport();
            #else
            renderLauncher();
            #endif
        } else if (!playerMode) {
            setupDockspace([this]() { renderPlayControlsBar(); });
            renderMainMenuBar();

            if (!viewportFullscreen) {
                if (showHierarchy) renderHierarchyPanel();
                if (showInspector) renderInspectorPanel();
                if (showFileBrowser) renderFileBrowserPanel();
                if (showMeshBuilder) renderMeshBuilderPanel();
                if (showScriptingWindow) renderScriptingWindow();
                if (showEnvironmentWindow) renderEnvironmentWindow();
                if (showCameraWindow) renderCameraWindow();
                if (showAnimationWindow) renderAnimationWindow();
                if (showAIPathfindingWindow) renderAIPathfindingWindow();
                if (showProjectBrowser) renderProjectBrowserPanel();
            }

        if (showBuildSettings) renderBuildSettingsWindow();
        renderScriptEditorWindows();
        renderViewport();
        if (showGameViewport) renderGameViewportWindow();
        if (showConsole) renderConsolePanel();
        renderDialogs();
        renderLatestErrorBar();
    } else {
        renderPlayerViewport();
    }

        if (firstFrame) {
            std::cerr << "[DEBUG] First frame: UI rendering complete, finalizing frame..." << std::endl;
        }

        autosaveWorkspaceLayout();
        renderUiCanvas3DTargets();

        int displayW, displayH;
        glfwGetFramebufferSize(editorWindow, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.1f, 0.1f, 0.12f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        // Enforce cursor lock state at the end of the frame based on latest flags.
        bool anyLock = cursorLocked || gameViewCursorLocked;
        int desiredMode = anyLock ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
        if (glfwGetInputMode(editorWindow, GLFW_CURSOR) != desiredMode) {
            glfwSetInputMode(editorWindow, GLFW_CURSOR, desiredMode);
            if (anyLock && glfwRawMouseMotionSupported()) {
                glfwSetInputMode(editorWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
            } else if (!anyLock && glfwRawMouseMotionSupported()) {
                glfwSetInputMode(editorWindow, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
            }
        }

        glfwSwapBuffers(editorWindow);

        if (fpsCapEnabled && fpsCap > 1.0f) {
            double target = 1.0 / fpsCap;
            double frameEnd = glfwGetTime();
            double elapsed = frameEnd - frameStart;
            if (elapsed < target) {
                int sleepMs = static_cast<int>((target - elapsed) * 1000.0);
                if (sleepMs > 0) ImGui_ImplGlfw_Sleep(sleepMs);
            }
        }
        
        if (firstFrame) {
            std::cerr << "[DEBUG] First frame complete!" << std::endl;
        }
        firstFrame = false;
    }
    
    std::cerr << "[DEBUG] Exiting main loop" << std::endl;
}

void Engine::shutdown() {
    ImGuiContext* mainContext = ImGui::GetCurrentContext();
    if (mainContext && !playerMode) {
        saveWorkspaceLayout(currentWorkspace);
    }

    if (
#ifndef MODULARITY_PLAYER
        projectManager.currentProject.isLoaded && projectManager.currentProject.hasUnsavedChanges
#else
        false
#endif
    ) {
        saveCurrentScene();
    }

    if (compileWorker.joinable()) {
        compileWorker.join();
    }

    physics.onPlayStop();
    audio.onPlayStop();
    audio.shutdown();
    physics.shutdown();

    for (auto& entry : uiCanvas3DContexts) {
        if (!entry.second.context) continue;
        ImGui::SetCurrentContext(entry.second.context);
        if (entry.second.backendReady) {
            ImGui_ImplOpenGL3_Shutdown();
            entry.second.backendReady = false;
        }
        ImGui::DestroyContext(entry.second.context);
    }
    uiCanvas3DContexts.clear();

    if (mainContext) {
        ImGui::SetCurrentContext(mainContext);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(mainContext);
    }
    glfwTerminate();
}
#pragma endregion

#pragma region Asset Import
void Engine::importOBJToScene(const std::string& filepath, const std::string& objectName) {
    recordState("importOBJ");
    std::string errorMsg;
    int meshId = g_objLoader.loadOBJ(filepath, errorMsg);
    
    if (meshId < 0) {
        addConsoleMessage("Failed to load OBJ: " + errorMsg, ConsoleMessageType::Error);
        return;
    }
    
    int id = nextObjectId++;
    std::string name = objectName.empty() ? fs::path(filepath).stem().string() : objectName;
    
    SceneObject obj(name, ObjectType::Empty, id);
    obj.hasRenderer = true;
    obj.renderType = RenderType::OBJMesh;
    obj.type = ObjectType::OBJMesh;
    obj.meshPath = filepath;
    obj.meshId = meshId;
    
    sceneObjects.push_back(obj);
    setPrimarySelection(id);
    
    if (projectManager.currentProject.isLoaded) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }
    
    const auto* meshInfo = g_objLoader.getMeshInfo(meshId);
    if (meshInfo) {
        addConsoleMessage("Imported OBJ: " + name + " (" + 
                        std::to_string(meshInfo->vertexCount) + " vertices, " +
                        std::to_string(meshInfo->faceCount) + " faces)", 
                        ConsoleMessageType::Success);
    } else {
        addConsoleMessage("Imported OBJ: " + name, ConsoleMessageType::Success);
    }
}

void Engine::importModelToScene(const std::string& filepath, const std::string& objectName) {
    recordState("importModel");
    auto& modelLoader = getModelLoader();
    ModelSceneData sceneData;
    std::string error;
    if (!modelLoader.loadModelScene(filepath, sceneData, error)) {
        ModelLoadResult fallback = modelLoader.loadModel(filepath);
        if (!fallback.success) {
            addConsoleMessage("Failed to load model: " + error, ConsoleMessageType::Error);
            return;
        }
        int id = nextObjectId++;
        std::string name = objectName.empty() ? fs::path(filepath).stem().string() : objectName;
        SceneObject obj(name, ObjectType::Empty, id);
        obj.hasRenderer = true;
        obj.renderType = RenderType::Model;
        obj.type = ObjectType::Model;
        obj.meshPath = filepath;
        obj.meshId = fallback.meshIndex;
        sceneObjects.push_back(obj);
        setPrimarySelection(id);
        if (projectManager.currentProject.isLoaded) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        addConsoleMessage(
            "Imported model: " + name + " (" +
            std::to_string(fallback.vertexCount) + " verts, " +
            std::to_string(fallback.faceCount) + " faces, " +
            std::to_string(fallback.meshCount) + " meshes)",
            ConsoleMessageType::Success
        );
        return;
    }

    std::string baseName = objectName.empty() ? fs::path(filepath).stem().string() : objectName;
    std::vector<std::string> materialPaths(sceneData.materials.size());

    if (projectManager.currentProject.isLoaded && !sceneData.materials.empty()) {
        fs::path materialsDir = projectManager.currentProject.assetsPath / "Materials" / "Imported" / baseName;
        std::error_code ec;
        fs::create_directories(materialsDir, ec);
        if (ec) {
            addConsoleMessage("Failed to create materials folder: " + materialsDir.string(), ConsoleMessageType::Warning);
        } else {
            for (size_t i = 0; i < sceneData.materials.size(); ++i) {
                const auto& mat = sceneData.materials[i];
                std::string matName = sanitizeMaterialName(mat.name);
                fs::path matPath = materialsDir / (matName + ".mat");
                MaterialFileData data;
                data.props = mat.props;
                data.albedo = resolveTexturePath(mat.albedoPath, filepath);
                data.overlay.clear();
                data.normal = resolveTexturePath(mat.normalPath, filepath);
                data.useOverlay = false;
                data.vertexShader.clear();
                data.fragmentShader.clear();
                if (writeMaterialFile(data, matPath.string())) {
                    materialPaths[i] = matPath.string();
                }
            }
        }
    }

    constexpr size_t kStaticBatchMeshThreshold = 16;
    size_t validMeshCount = 0;
    for (int meshId : sceneData.meshIndices) {
        if (meshId >= 0) {
            ++validMeshCount;
        }
    }

    bool hasSkinnedMeshes = false;
    for (int meshId : sceneData.meshIndices) {
        if (meshId < 0) continue;
        const auto* info = modelLoader.getMeshInfo(meshId);
        if (info && info->isSkinned) {
            hasSkinnedMeshes = true;
            break;
        }
    }

    bool hasAnimations = !sceneData.animations.empty();
    bool singleMaterial = sceneData.materials.size() <= 1;
    if (!singleMaterial && !sceneData.meshMaterialIndices.empty()) {
        int firstMat = -2;
        bool mixed = false;
        for (int matIndex : sceneData.meshMaterialIndices) {
            if (matIndex < 0) continue;
            if (firstMat == -2) {
                firstMat = matIndex;
            } else if (matIndex != firstMat) {
                mixed = true;
                break;
            }
        }
        singleMaterial = !mixed;
    }

    if (validMeshCount >= kStaticBatchMeshThreshold &&
        !hasSkinnedMeshes && !hasAnimations && singleMaterial) {
        RawMeshAsset raw;
        glm::vec3 rootPos(0.0f);
        glm::vec3 rootRot(0.0f);
        glm::vec3 rootScale(1.0f);
        if (modelLoader.buildRawMeshFromScene(filepath, raw, error, &rootPos, &rootRot, &rootScale)) {
            std::string batchName = baseName + "_StaticBatch";
            int batchMeshId = modelLoader.addRawMesh(raw, filepath, batchName, error);
            if (batchMeshId >= 0) {
                int id = nextObjectId++;
                SceneObject obj(baseName, ObjectType::Empty, id);
                obj.hasRenderer = true;
                obj.renderType = RenderType::Model;
                obj.type = ObjectType::Model;
                obj.meshPath = filepath;
                obj.meshId = batchMeshId;
                obj.meshSourceIndex = -1;
                obj.localPosition = rootPos;
                obj.localRotation = rootRot;
                obj.localScale = rootScale;
                obj.localInitialized = true;
                obj.position = obj.localPosition;
                obj.rotation = obj.localRotation;
                obj.scale = obj.localScale;

                if (!materialPaths.empty() && !materialPaths[0].empty()) {
                    obj.materialPath = materialPaths[0];
                    loadMaterialFromFile(obj);
                } else if (!sceneData.materials.empty()) {
                    const auto& mat = sceneData.materials.front();
                    obj.material = mat.props;
                    obj.albedoTexturePath = resolveTexturePath(mat.albedoPath, filepath);
                    obj.normalMapPath = resolveTexturePath(mat.normalPath, filepath);
                }

                sceneObjects.push_back(obj);
                setPrimarySelection(id);
                if (projectManager.currentProject.isLoaded) {
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
                addConsoleMessage(
                    "Imported model (static batch): " + baseName + " (" +
                    std::to_string(validMeshCount) + " meshes)",
                    ConsoleMessageType::Success
                );
                return;
            }
        }
    }

    std::vector<int> nodeObjectIds(sceneData.nodes.size(), -1);
    int rootSelectionId = -1;

    for (size_t i = 0; i < sceneData.nodes.size(); ++i) {
        const auto& node = sceneData.nodes[i];
        std::string nodeName = node.name.empty()
            ? (baseName + "_Node" + std::to_string(i))
            : node.name;
        SceneObject obj(nodeName, ObjectType::Empty, nextObjectId++);
        obj.localPosition = node.localPosition;
        obj.localRotation = node.localRotation;
        obj.localScale = node.localScale;
        obj.localInitialized = true;
        obj.position = obj.localPosition;
        obj.rotation = obj.localRotation;
        obj.scale = obj.localScale;
        sceneObjects.push_back(obj);
        nodeObjectIds[i] = obj.id;
        if (rootSelectionId == -1) rootSelectionId = obj.id;
    }

    for (size_t i = 0; i < sceneData.nodes.size(); ++i) {
        int parentIndex = sceneData.nodes[i].parentIndex;
        if (parentIndex < 0 || parentIndex >= (int)nodeObjectIds.size()) continue;
        int parentId = nodeObjectIds[parentIndex];
        int childId = nodeObjectIds[i];
        if (parentId < 0 || childId < 0) continue;
        SceneObject* parentObj = findObjectById(parentId);
        SceneObject* childObj = findObjectById(childId);
        if (!parentObj || !childObj) continue;
        childObj->parentId = parentId;
        parentObj->childIds.push_back(childId);
    }

    for (size_t nodeIndex = 0; nodeIndex < sceneData.nodes.size(); ++nodeIndex) {
        const auto& node = sceneData.nodes[nodeIndex];
        int parentId = nodeObjectIds[nodeIndex];
        if (parentId < 0) continue;
        for (int meshSourceIndex : node.meshIndices) {
            if (meshSourceIndex < 0 || meshSourceIndex >= (int)sceneData.meshIndices.size()) continue;
            int meshId = sceneData.meshIndices[meshSourceIndex];
            if (meshId < 0) continue;

            const auto* meshInfo = modelLoader.getMeshInfo(meshId);
            std::string meshName = meshInfo && !meshInfo->name.empty()
                ? meshInfo->name
                : (baseName + "_Mesh");

            SceneObject meshObj(meshName, ObjectType::Empty, nextObjectId++);
            meshObj.hasRenderer = true;
            meshObj.renderType = RenderType::Model;
            meshObj.type = ObjectType::Model;
            meshObj.meshPath = filepath;
            meshObj.meshId = meshId;
            meshObj.meshSourceIndex = meshSourceIndex;
            meshObj.localPosition = glm::vec3(0.0f);
            meshObj.localRotation = glm::vec3(0.0f);
            meshObj.localScale = glm::vec3(1.0f);
            meshObj.localInitialized = true;
            meshObj.position = meshObj.localPosition;
            meshObj.rotation = meshObj.localRotation;
            meshObj.scale = meshObj.localScale;
            meshObj.parentId = parentId;

            if (meshInfo && meshInfo->isSkinned) {
                meshObj.hasSkeletalAnimation = true;
                meshObj.skeletal = SkeletalAnimationComponent{};
                meshObj.skeletal.skeletonRootId = rootSelectionId;
                meshObj.skeletal.boneNames = meshInfo->boneNames;
                meshObj.skeletal.inverseBindMatrices = meshInfo->inverseBindMatrices;
                meshObj.skeletal.finalMatrices.assign(meshInfo->boneNames.size(), glm::mat4(1.0f));
                meshObj.skeletal.boneNodeIds.assign(meshInfo->boneNames.size(), -1);
                for (size_t b = 0; b < meshInfo->boneNames.size(); ++b) {
                    for (size_t n = 0; n < sceneData.nodes.size(); ++n) {
                        if (sceneData.nodes[n].name == meshInfo->boneNames[b]) {
                            int nodeId = nodeObjectIds[n];
                            if (nodeId >= 0) {
                                meshObj.skeletal.boneNodeIds[b] = nodeId;
                            }
                            break;
                        }
                    }
                }
            }

            int matIndex = (meshSourceIndex >= 0 && meshSourceIndex < (int)sceneData.meshMaterialIndices.size())
                ? sceneData.meshMaterialIndices[meshSourceIndex]
                : -1;
            if (matIndex >= 0 && matIndex < (int)materialPaths.size() && !materialPaths[matIndex].empty()) {
                meshObj.materialPath = materialPaths[matIndex];
                loadMaterialFromFile(meshObj);
            } else if (matIndex >= 0 && matIndex < (int)sceneData.materials.size()) {
                const auto& mat = sceneData.materials[matIndex];
                meshObj.material = mat.props;
                meshObj.albedoTexturePath = resolveTexturePath(mat.albedoPath, filepath);
                meshObj.normalMapPath = resolveTexturePath(mat.normalPath, filepath);
            }

            sceneObjects.push_back(meshObj);
            if (SceneObject* parentObj = findObjectById(parentId)) {
                parentObj->childIds.push_back(meshObj.id);
            }
        }
    }

    updateHierarchyWorldTransforms();
    if (rootSelectionId != -1) {
        setPrimarySelection(rootSelectionId);
    }

    if (projectManager.currentProject.isLoaded) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }

    addConsoleMessage(
        "Imported model: " + baseName + " (" +
        std::to_string(sceneData.meshIndices.size()) + " meshes, " +
        std::to_string(sceneData.nodes.size()) + " nodes)",
        ConsoleMessageType::Success
    );
}

void Engine::convertModelToRawMesh(const std::string& filepath) {
    auto& modelLoader = getModelLoader();
    fs::path inPath(filepath);
    fs::path outPath = inPath;
    outPath.replace_extension(".rmesh");

    std::string error;
    if (modelLoader.exportRawMesh(filepath, outPath.string(), error)) {
        addConsoleMessage("Converted to raw mesh: " + outPath.string(), ConsoleMessageType::Success);
        fileBrowser.needsRefresh = true;
    } else {
        addConsoleMessage("Raw mesh export failed: " + error, ConsoleMessageType::Error);
    }
}

void Engine::createRMeshPrimitive(const std::string& primitiveName) {
    if (!projectManager.currentProject.isLoaded) {
        addConsoleMessage("Load a project before creating RMesh primitives", ConsoleMessageType::Warning);
        return;
    }

    fs::path root = projectManager.currentProject.assetsPath / "Models" / "RMeshes" / "Primitives";
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        addConsoleMessage("Failed to create RMesh folder: " + root.string(), ConsoleMessageType::Error);
        return;
    }

    RawMeshAsset asset;
    if (primitiveName == "Cube") {
        asset = buildCubeRMesh();
    } else if (primitiveName == "Sphere") {
        asset = buildSphereRMesh();
    } else if (primitiveName == "Plane") {
        asset = buildPlaneRMesh();
    } else {
        addConsoleMessage("Unknown RMesh primitive: " + primitiveName, ConsoleMessageType::Warning);
        return;
    }

    fs::path filePath = root / (primitiveName + ".rmesh");
    if (fs::exists(filePath)) {
        int suffix = 1;
        fs::path candidate;
        do {
            candidate = root / (primitiveName + "_" + std::to_string(suffix) + ".rmesh");
            ++suffix;
        } while (fs::exists(candidate));
        filePath = candidate;
    }

    std::string error;
    if (!getModelLoader().saveRawMesh(asset, filePath.string(), error)) {
        addConsoleMessage("Failed to save RMesh primitive: " + error, ConsoleMessageType::Error);
        return;
    }
    fileBrowser.needsRefresh = true;

    importModelToScene(filePath.string(), primitiveName);
}
#pragma endregion

#pragma region Mesh Editing
bool Engine::ensureMeshEditTarget(SceneObject* obj) {
    if (!obj) return false;
    fs::path ext = fs::path(obj->meshPath).extension();
    std::string extLower = ext.string();
    std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
    if (extLower != ".rmesh") return false;

    if (meshEditLoaded && meshEditPath == obj->meshPath) {
        if (meshEditSelectedVertices.empty() && !meshEditAsset.positions.empty()) {
            meshEditSelectedVertices.push_back(0);
        }
        return true;
    }

    std::string err;
    if (!getModelLoader().loadRawMesh(obj->meshPath, meshEditAsset, err)) {
        addConsoleMessage("Mesh edit load failed: " + err, ConsoleMessageType::Error);
        meshEditLoaded = false;
        return false;
    }
    meshEditLoaded = true;
    meshEditPath = obj->meshPath;
    meshEditDirty = false;
    meshEditSelectedVertices.clear();
    meshEditSelectedEdges.clear();
    meshEditSelectedFaces.clear();
    if (!meshEditAsset.positions.empty()) meshEditSelectedVertices.push_back(0);
    return true;
}

bool Engine::syncMeshEditToGPU(SceneObject* obj) {
    if (!obj || !meshEditLoaded) return false;
    std::string err;
    if (!getModelLoader().updateRawMesh(obj->meshId, meshEditAsset, err)) {
        addConsoleMessage("Mesh GPU sync failed: " + err, ConsoleMessageType::Error);
        return false;
    }
    projectManager.currentProject.hasUnsavedChanges = true;
    return true;
}

bool Engine::saveMeshEditAsset(std::string& error) {
    if (!meshEditLoaded) {
        error = "No mesh loaded for editing";
        return false;
    }
    if (meshEditPath.empty()) {
        error = "Mesh edit path is empty";
        return false;
    }
    if (!getModelLoader().saveRawMesh(meshEditAsset, meshEditPath, error)) {
        return false;
    }
    meshEditDirty = false;
    fileBrowser.needsRefresh = true;
    return true;
}
#pragma endregion

#pragma region Material IO
void Engine::loadMaterialFromFile(SceneObject& obj) {
    if (obj.materialPath.empty()) return;
    try {
        MaterialFileData data;
        if (!readMaterialFile(obj.materialPath, data)) {
            addConsoleMessage("Failed to open material: " + obj.materialPath, ConsoleMessageType::Error);
            return;
        }
        obj.material = data.props;
        obj.albedoTexturePath = data.albedo;
        obj.overlayTexturePath = data.overlay;
        obj.normalMapPath = data.normal;
        obj.useOverlay = data.useOverlay;
        obj.vertexShaderPath = data.vertexShader;
        obj.fragmentShaderPath = data.fragmentShader;
        addConsoleMessage("Applied material: " + obj.materialPath, ConsoleMessageType::Success);
        projectManager.currentProject.hasUnsavedChanges = true;
    } catch (...) {
        addConsoleMessage("Failed to read material: " + obj.materialPath, ConsoleMessageType::Error);
    }
}

bool Engine::loadMaterialData(const std::string& path, MaterialProperties& props,
                              std::string& albedo, std::string& overlay,
                              std::string& normal, bool& useOverlay,
                              std::string* vertexShaderOut,
                              std::string* fragmentShaderOut)
{
    MaterialFileData data;
    if (!readMaterialFile(path, data)) {
        return false;
    }
    props = data.props;
    albedo = data.albedo;
    overlay = data.overlay;
    normal = data.normal;
    useOverlay = data.useOverlay;
    if (vertexShaderOut) *vertexShaderOut = data.vertexShader;
    if (fragmentShaderOut) *fragmentShaderOut = data.fragmentShader;
    return true;
}

bool Engine::saveMaterialData(const std::string& path, const MaterialProperties& props,
                              const std::string& albedo, const std::string& overlay,
                              const std::string& normal, bool useOverlay,
                              const std::string& vertexShader,
                              const std::string& fragmentShader)
{
    MaterialFileData data;
    data.props = props;
    data.albedo = albedo;
    data.overlay = overlay;
    data.normal = normal;
    data.useOverlay = useOverlay;
    data.vertexShader = vertexShader;
    data.fragmentShader = fragmentShader;
    return writeMaterialFile(data, path);
}

void Engine::saveMaterialToFile(const SceneObject& obj) {
    if (obj.materialPath.empty()) {
        addConsoleMessage("Material path is empty", ConsoleMessageType::Warning);
        return;
    }
    try {
        MaterialFileData data;
        data.props = obj.material;
        data.albedo = obj.albedoTexturePath;
        data.overlay = obj.overlayTexturePath;
        data.normal = obj.normalMapPath;
        data.useOverlay = obj.useOverlay;
        data.vertexShader = obj.vertexShaderPath;
        data.fragmentShader = obj.fragmentShaderPath;

        if (!writeMaterialFile(data, obj.materialPath)) {
            addConsoleMessage("Failed to open material for writing: " + obj.materialPath, ConsoleMessageType::Error);
            return;
        }
        addConsoleMessage("Saved material: " + obj.materialPath, ConsoleMessageType::Success);
    } catch (...) {
        addConsoleMessage("Failed to save material: " + obj.materialPath, ConsoleMessageType::Error);
    }
}
#pragma endregion

#pragma region Editor Shortcuts
void Engine::handleKeyboardShortcuts() {
    static bool f11Pressed = false;
    if (glfwGetKey(editorWindow, GLFW_KEY_F11) == GLFW_PRESS && !f11Pressed) {
        viewportFullscreen = !viewportFullscreen;
        f11Pressed = true;
    }
    if (glfwGetKey(editorWindow, GLFW_KEY_F11) == GLFW_RELEASE) {
        f11Pressed = false;
    }

    static bool ctrlSPressed = false;
    bool ctrlDown = glfwGetKey(editorWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                   glfwGetKey(editorWindow, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    bool shiftDown = glfwGetKey(editorWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                     glfwGetKey(editorWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

    if (ctrlDown && glfwGetKey(editorWindow, GLFW_KEY_S) == GLFW_PRESS && !ctrlSPressed) {
        if (projectManager.currentProject.isLoaded) {
            saveCurrentScene();
        }
        ctrlSPressed = true;
    }
    if (glfwGetKey(editorWindow, GLFW_KEY_S) == GLFW_RELEASE) {
        ctrlSPressed = false;
    }

    static bool ctrlNPressed = false;
    if (ctrlDown && glfwGetKey(editorWindow, GLFW_KEY_N) == GLFW_PRESS && !ctrlNPressed) {
        if (projectManager.currentProject.isLoaded) {
            showNewSceneDialog = true;
            memset(newSceneName, 0, sizeof(newSceneName));
        }
        ctrlNPressed = true;
    }
    if (glfwGetKey(editorWindow, GLFW_KEY_N) == GLFW_RELEASE) {
        ctrlNPressed = false;
    }

    bool cameraActive = cursorLocked || (viewportController.isViewportFocused() && cursorLocked);
    if (!isPlaying && gameViewCursorLocked) {
        // Prevent edit-mode freelook from conflicting with game view capture
        gameViewCursorLocked = false;
    }
    if (!cameraActive) {
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_W)) mCurrentGizmoOperation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) mCurrentGizmoOperation = ImGuizmo::SCALE;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) mCurrentGizmoOperation = ImGuizmo::BOUNDS;
        if (ImGui::IsKeyPressed(ImGuiKey_T)) mCurrentGizmoOperation = ImGuizmo::UNIVERSAL;

        if (ImGui::IsKeyPressed(ImGuiKey_U)) {
            mCurrentGizmoMode = (mCurrentGizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_3)) {
        collisionWireframe = !collisionWireframe;
        addConsoleMessage(std::string("Collision wireframe ") + (collisionWireframe ? "enabled" : "disabled"), ConsoleMessageType::Info);
    }

    static bool snapPressed = false;
    static bool snapHeldByCtrl = false;
    static bool snapStateBeforeCtrl = false;

    if (!snapHeldByCtrl && ctrlDown) {
        snapStateBeforeCtrl = useSnap;
        snapHeldByCtrl = true;
        useSnap = true;
    } else if (snapHeldByCtrl && !ctrlDown) {
        useSnap = snapStateBeforeCtrl;
        snapHeldByCtrl = false;
    }

    if (!cameraActive) {
        if (ImGui::IsKeyPressed(ImGuiKey_Y) && !snapPressed) {
            useSnap = !useSnap;
            snapPressed = true;
        }
    }
    if (ImGui::IsKeyReleased(ImGuiKey_Y)) {
        snapPressed = false;
    }

    static bool undoPressed = false;
    if (ctrlDown && !shiftDown && glfwGetKey(editorWindow, GLFW_KEY_Z) == GLFW_PRESS && !undoPressed) {
        undo();
        undoPressed = true;
    }
    if (glfwGetKey(editorWindow, GLFW_KEY_Z) == GLFW_RELEASE) {
        undoPressed = false;
    }

    static bool redoPressed = false;
    if (ctrlDown &&
        ((glfwGetKey(editorWindow, GLFW_KEY_Y) == GLFW_PRESS) ||
         (shiftDown && glfwGetKey(editorWindow, GLFW_KEY_Z) == GLFW_PRESS)) &&
        !redoPressed)
    {
        redo();
        redoPressed = true;
    }
    if (glfwGetKey(editorWindow, GLFW_KEY_Y) == GLFW_RELEASE &&
        glfwGetKey(editorWindow, GLFW_KEY_Z) == GLFW_RELEASE)
    {
        redoPressed = false;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && gameViewCursorLocked) {
        gameViewCursorLocked = false;
    }
}
#pragma endregion

#pragma region Runtime Updates
void Engine::updateScripts(float delta) {
    if (sceneObjects.empty()) return;

    for (auto& obj : sceneObjects) {
        if (!obj.enabled) continue;
        for (auto& sc : obj.scripts) {
            if (!sc.enabled) continue;
            if (sc.path.empty()) continue;
            ScriptContext ctx;
            ctx.engine = this;
            ctx.object = &obj;
            ctx.script = &sc;
            if (sc.language == ScriptLanguage::CSharp) {
                fs::path assembly = resolveManagedAssembly(sc.path);
                if (assembly.empty() || !fs::exists(assembly)) continue;
                managedRuntime.tickModule(assembly, sc.managedType, ctx, delta, specMode, testMode);
            } else {
                fs::path binary = resolveScriptBinary(sc.path);
                std::error_code ec;
                bool hasBinary = !binary.empty() && fs::exists(binary, ec) && !ec;
                if (!hasBinary) {
                    fs::path scriptPath(sc.path);
                    std::string missingKey = scriptPath.lexically_normal().string();
                    if (nativeScriptMissingLogged.insert(missingKey).second) {
                        std::cerr << "[Script] Native script binary missing for '" << sc.path
                                  << "'. Compile scripts before running/exporting.\n";
                        addConsoleMessage(
                            "Native script binary missing for '" + sc.path +
                            "'. Compile scripts before running/exporting.",
                            ConsoleMessageType::Warning);
                    }
                    continue;
                }

                std::string binaryKey = binary.lexically_normal().string();
                nativeScriptMissingLogged.erase(fs::path(sc.path).lexically_normal().string());
                scriptRuntime.tickModule(binary, ctx, delta, specMode, testMode);
                const std::string& runtimeError = scriptRuntime.getLastError();
                if (!runtimeError.empty()) {
                    std::string errorKey = binaryKey + "|" + runtimeError;
                    if (nativeScriptLoadErrorLogged.insert(errorKey).second) {
                        std::cerr << "[Script] Failed to load native script '" << binary.filename().string()
                                  << "': " << runtimeError << "\n";
                        addConsoleMessage(
                            "Failed to load native script '" + binary.filename().string() +
                            "': " + runtimeError,
                            ConsoleMessageType::Error);
                    }
                }
            }
        }
    }
}

void Engine::queueAutoCompile(const fs::path& scriptPath, const fs::file_time_type& sourceTime) {
    std::error_code ec;
    fs::path scriptAbs = fs::absolute(scriptPath, ec);
    if (ec) scriptAbs = scriptPath;
    std::string key = scriptAbs.lexically_normal().string();
    if (!autoCompileQueued.insert(key).second) return;

    autoCompileQueue.push_back(scriptAbs);
    scriptLastAutoCompileTime[key] = sourceTime;
}

void Engine::updateAutoCompileScripts() {
    if (!projectManager.currentProject.isLoaded) return;
    if (showLauncher) return;

    double now = glfwGetTime();
    if (now - scriptAutoCompileLastCheck < scriptAutoCompileInterval) return;
    scriptAutoCompileLastCheck = now;

    fs::path configPath = resolveScriptsConfigPath(projectManager.currentProject);

    ScriptBuildConfig config;
    std::string error;
    if (!scriptCompiler.loadConfig(configPath, config, error)) {
        return;
    }
    packageManager.applyToBuildConfig(config);

    std::unordered_set<std::string> sources;
    auto addSource = [&](const fs::path& path) {
        if (path.empty()) return;
        std::error_code ec;
        fs::path absPath = fs::absolute(path, ec);
        if (ec) absPath = path;
        sources.insert(absPath.lexically_normal().string());
    };

    bool hasManagedScripts = false;
    for (const auto& obj : sceneObjects) {
        for (const auto& sc : obj.scripts) {
            if (sc.language == ScriptLanguage::CSharp) {
                hasManagedScripts = true;
                continue;
            }
            if (sc.path.empty()) continue;
            addSource(sc.path);
        }
    }

    fs::path scriptsDir = config.scriptsDir;
    if (!scriptsDir.is_absolute()) {
        scriptsDir = projectManager.currentProject.projectPath / scriptsDir;
    }
    std::error_code dirEc;
    if (fs::exists(scriptsDir, dirEc)) {
        for (auto it = fs::recursive_directory_iterator(scriptsDir, dirEc);
             it != fs::recursive_directory_iterator(); ++it) {
            if (it->is_directory()) continue;
            std::string ext = it->path().extension().string();
            if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c") {
                addSource(it->path());
            }
        }
    }

    for (const auto& sourceKey : sources) {
        fs::path sourcePath = sourceKey;
        std::error_code sourceEc;
        if (!fs::exists(sourcePath, sourceEc)) continue;
        auto sourceTime = fs::last_write_time(sourcePath, sourceEc);
        if (sourceEc) continue;

        ScriptBuildCommands commands;
        if (!scriptCompiler.makeCommands(config, sourcePath, commands, error)) {
            continue;
        }

        std::error_code binEc;
        bool binaryExists = fs::exists(commands.binaryPath, binEc);
        fs::file_time_type binaryTime{};
        if (binaryExists && !binEc) {
            binaryTime = fs::last_write_time(commands.binaryPath, binEc);
        }

        bool needsCompile = !binaryExists || (!binEc && sourceTime > binaryTime);
        if (!needsCompile) continue;

        auto it = scriptLastAutoCompileTime.find(sourceKey);
        if (it != scriptLastAutoCompileTime.end() && sourceTime <= it->second) continue;

        queueAutoCompile(sourcePath, sourceTime);
    }

    if (hasManagedScripts) {
        std::error_code managedEc;
        fs::path projectManagedProject =
            projectManager.currentProject.projectPath / "Scripts" / "Managed" / "ModuCPP.csproj";
        fs::path managedProject = fs::exists(projectManagedProject, managedEc)
            ? projectManagedProject
            : getManagedProjectPath();
        fs::path managedOutput = managedOutputPathFromProject(managedProject);
        if (fs::exists(managedProject, managedEc)) {
            fs::file_time_type newestSource{};
            bool hasSource = false;
            fs::path managedDir = managedProject.parent_path();
            if (fs::exists(managedDir, managedEc)) {
                for (auto it = fs::recursive_directory_iterator(managedDir, managedEc);
                     it != fs::recursive_directory_iterator(); ++it) {
                    if (it->is_directory()) {
                        if (isManagedGeneratedPath(it->path())) {
                            it.disable_recursion_pending();
                        }
                        continue;
                    }
                    if (it->path().extension() != ".cs") continue;
                    if (isManagedGeneratedPath(it->path())) continue;
                    auto sourceTime = fs::last_write_time(it->path(), managedEc);
                    if (managedEc) continue;
                    if (!hasSource || sourceTime > newestSource) {
                        newestSource = sourceTime;
                        hasSource = true;
                    }
                }
            }

            bool needsManaged = false;
            if (!fs::exists(managedOutput, managedEc)) {
                needsManaged = true;
            } else if (hasSource && !managedEc) {
                auto binaryTime = fs::last_write_time(managedOutput, managedEc);
                if (!managedEc && newestSource > binaryTime) {
                    needsManaged = true;
                }
            }

            if (needsManaged) {
                if (!compileInProgress) {
                    compileManagedScripts();
                } else {
                    managedAutoCompileQueued = true;
                }
            }
        }
    }
}

void Engine::processAutoCompileQueue() {
    if (compileInProgress) return;
    if (autoCompileQueue.empty()) return;

    fs::path next = autoCompileQueue.front();
    autoCompileQueue.pop_front();
    std::error_code ec;
    fs::path absPath = fs::absolute(next, ec);
    if (ec) absPath = next;
    autoCompileQueued.erase(absPath.lexically_normal().string());

    compileScriptFile(next);
}

void Engine::updatePlayerController(float delta) {
    if (!isPlaying) return;

    SceneObject* player = nullptr;
    for (auto& obj : sceneObjects) {
        if (obj.enabled && obj.hasPlayerController && obj.playerController.enabled) {
            player = &obj;
            activePlayerId = obj.id;
            break;
        }
    }
    if (!player) {
        activePlayerId = -1;
        return;
    }

    struct ControllerRuntimeState {
        glm::vec2 localVelocity = glm::vec2(0.0f);
        glm::vec3 slideVelocity = glm::vec3(0.0f);
        glm::vec3 lastGroundHitPos = glm::vec3(0.0f);
        bool hasGroundSample = false;
    };
    static std::unordered_map<int, ControllerRuntimeState> runtimeStates;
    ControllerRuntimeState& runtime = runtimeStates[player->id];
    auto moveTowardsVec2 = [](const glm::vec2& current, const glm::vec2& target, float maxDelta) {
        if (maxDelta <= 0.0f) return current;
        glm::vec2 deltaVec = target - current;
        float len = glm::length(deltaVec);
        if (len <= maxDelta || len <= 1e-5f) {
            return target;
        }
        return current + (deltaVec / len) * maxDelta;
    };
    auto sanitizePlanar = [](const glm::vec3& value) {
        glm::vec3 out(value.x, 0.0f, value.z);
        if (!std::isfinite(out.x) || !std::isfinite(out.z)) {
            return glm::vec3(0.0f);
        }
        return out;
    };

    auto& pc = player->playerController;
    // Maintain capsule sizing and collider defaults
    if (pc.pitch == 0.0f && pc.yaw == 0.0f && (glm::length(player->rotation) > 0.01f)) {
        pc.pitch = player->rotation.x;
        pc.yaw = player->rotation.y;
    }
    glm::vec3 capsuleSize(pc.radius * 2.0f, pc.height, pc.radius * 2.0f);
    player->hasCollider = true;
    player->collider.type = ColliderType::Capsule;
    player->collider.convex = true;
    player->collider.boxSize = capsuleSize;
    player->scale = capsuleSize;
    player->hasRigidbody = true;
    player->rigidbody.enabled = true;
    player->rigidbody.useGravity = true;
    player->rigidbody.isKinematic = false;

    // Mouse look when game viewport is focused
    if (gameViewportFocused || gameViewCursorLocked) {
        ImGuiIO& io = ImGui::GetIO();
        pc.yaw -= io.MouseDelta.x * 50.0f * pc.lookSensitivity * delta;
        pc.pitch -= io.MouseDelta.y * 50.0f * pc.lookSensitivity * delta;
        pc.pitch = std::clamp(pc.pitch, -89.0f, 89.0f);
    }

    // Movement input aligned to camera facing (-Z forward convention)
    auto key = [&](int k) { return glfwGetKey(editorWindow, k) == GLFW_PRESS; };
    glm::quat q = glm::quat(glm::radians(glm::vec3(pc.pitch, pc.yaw, 0.0f)));
    glm::vec3 forward = glm::normalize(q * glm::vec3(0.0f, 0.0f, -1.0f));
    glm::vec3 right = glm::normalize(q * glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 planarForward = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
    glm::vec3 planarRight = glm::normalize(glm::vec3(right.x, 0.0f, right.z));
    if (!std::isfinite(planarForward.x) || glm::length(planarForward) < 1e-3f) {
        planarForward = glm::vec3(0, 0, -1);
    }
    if (!std::isfinite(planarRight.x) || glm::length(planarRight) < 1e-3f) {
        planarRight = glm::vec3(1, 0, 0);
    }

    glm::vec3 move(0.0f);
    if (key(GLFW_KEY_W)) move += planarForward;
    if (key(GLFW_KEY_S)) move -= planarForward;
    if (key(GLFW_KEY_D)) move += planarRight;
    if (key(GLFW_KEY_A)) move -= planarRight;
    if (glm::length(move) > 0.001f) move = glm::normalize(move);

    glm::vec2 localInput(glm::dot(move, planarRight), glm::dot(move, planarForward));
    if (glm::length(localInput) > 1.0f) {
        localInput = glm::normalize(localInput);
    }
    bool sprinting = key(GLFW_KEY_LEFT_SHIFT) || key(GLFW_KEY_RIGHT_SHIFT);
    float targetSpeed = sprinting ? std::max(pc.moveSpeed, pc.runSpeed) : pc.moveSpeed;
    glm::vec2 targetLocalVelocity = localInput * targetSpeed;
    glm::vec3 velocity(0.0f);

    // Simple gravity and jump
    float capsuleHalf = std::max(0.1f, pc.height * 0.5f);
    glm::vec3 physVel;
    bool havePhysVel = physics.getLinearVelocity(player->id, physVel);
    if (havePhysVel) pc.verticalVelocity = physVel.y;

    // Ground check via PhysX scene query so mesh colliders work, not just the plane
    glm::vec3 hitPos;
    glm::vec3 hitNormal;
    glm::vec3 hitActorVelocity(0.0f);
    int hitActorId = -1;
    float hitStaticFriction = 0.9f;
    float hitDynamicFriction = 0.9f;
    float hitDist = 0.0f;
    float probeDist = capsuleHalf + 0.4f;
    glm::vec3 rayStart = player->position + glm::vec3(0.0f, 0.1f, 0.0f);
    bool hitGround = physics.raycastClosest(rayStart, glm::vec3(0.0f, -1.0f, 0.0f), probeDist,
                                            player->id, &hitPos, &hitNormal, &hitDist,
                                            &hitActorId, &hitActorVelocity,
                                            &hitStaticFriction, &hitDynamicFriction);
    bool grounded = hitGround && hitNormal.y > 0.25f && hitDist <= capsuleHalf + 0.2f && pc.verticalVelocity <= 0.35f;
    if (!hitGround) {
        // Fallback to simple height check to avoid regressions if queries fail
        grounded = player->position.y <= capsuleHalf + 0.12f && pc.verticalVelocity <= 0.35f;
    }

    (void)hitActorId;
    (void)hitStaticFriction;
    const float dynamicFriction = std::clamp(hitDynamicFriction, 0.0f, 2.0f);
    const float minSurfaceControl = std::clamp(pc.minSurfaceControl, 0.0f, 1.0f);
    const float grip = grounded ? std::clamp(dynamicFriction, minSurfaceControl, 1.0f) : 1.0f;
    const float groundAccel = std::max(0.0f, pc.groundAcceleration);
    const float airAccel = std::max(0.0f, pc.airAcceleration);
    const float braking = std::max(0.0f, pc.braking);
    const float slideGravity = std::max(0.0f, pc.slideGravity);
    const float platformCarry = std::clamp(pc.platformCarry, 0.0f, 3.0f);

    float accelRate = grounded ? groundAccel * grip : airAccel;
    runtime.localVelocity = (accelRate > 0.0f)
        ? moveTowardsVec2(runtime.localVelocity, targetLocalVelocity, accelRate * delta)
        : targetLocalVelocity;
    if (glm::dot(localInput, localInput) < 1e-4f && braking > 0.0f) {
        float brakeScale = grounded ? (0.5f + 0.5f * grip) : 0.15f;
        float damp = std::max(0.0f, 1.0f - braking * brakeScale * delta);
        runtime.localVelocity *= damp;
    }
    float localSpeed = glm::length(runtime.localVelocity);
    if (localSpeed > targetSpeed && targetSpeed > 0.0f) {
        runtime.localVelocity *= (targetSpeed / localSpeed);
    }

    glm::vec3 platformVelocity = sanitizePlanar(hitActorVelocity);
    if (grounded && hitGround) {
        if (runtime.hasGroundSample && delta > 1e-5f) {
            glm::vec3 pointVelocity = sanitizePlanar((hitPos - runtime.lastGroundHitPos) / delta);
            if (glm::dot(pointVelocity, pointVelocity) < (120.0f * 120.0f)) {
                if (glm::dot(platformVelocity, platformVelocity) < 1e-4f) {
                    platformVelocity = pointVelocity;
                } else {
                    platformVelocity = glm::mix(platformVelocity, pointVelocity, 0.35f);
                }
            }
        }
        runtime.lastGroundHitPos = hitPos;
        runtime.hasGroundSample = true;
    } else {
        runtime.hasGroundSample = false;
    }

    if (grounded && hitGround) {
        glm::vec3 n = glm::normalize(hitNormal);
        if (std::isfinite(n.x) && std::isfinite(n.y) && std::isfinite(n.z)) {
            glm::vec3 gravityDir(0.0f, -1.0f, 0.0f);
            glm::vec3 downSlope = gravityDir - n * glm::dot(gravityDir, n);
            float downLen = glm::length(downSlope);
            if (downLen > 1e-4f) {
                downSlope /= downLen;
                float slopeFactor = std::clamp((1.0f - n.y) * 3.0f, 0.0f, 1.5f);
                float slip = std::clamp(1.0f - dynamicFriction * 0.85f, 0.0f, 1.0f);
                float slideAccel = slideGravity * slopeFactor * (0.35f + slip);
                runtime.slideVelocity += downSlope * slideAccel * delta;
            }
        }
        float slideDamp = std::clamp((dynamicFriction + 0.15f) * 6.0f, 0.5f, 12.0f);
        runtime.slideVelocity *= std::max(0.0f, 1.0f - slideDamp * delta);
    } else {
        runtime.slideVelocity *= std::max(0.0f, 1.0f - 4.0f * delta);
    }
    runtime.slideVelocity = sanitizePlanar(runtime.slideVelocity);

    if (grounded) {
        pc.verticalVelocity = 0.0f;
        if (!havePhysVel) {
            if (hitGround) {
                player->position.y = std::max(player->position.y, hitPos.y + capsuleHalf);
            } else {
                player->position.y = capsuleHalf;
            }
        }
        if (key(GLFW_KEY_SPACE)) {
            pc.verticalVelocity = pc.jumpStrength;
            runtime.hasGroundSample = false;
        }
    } else {
        pc.verticalVelocity += -9.81f * delta;
    }

    platformVelocity = grounded ? platformVelocity * platformCarry : glm::vec3(0.0f);
    glm::vec3 planarVelocity =
        planarRight * runtime.localVelocity.x +
        planarForward * runtime.localVelocity.y +
        platformVelocity +
        runtime.slideVelocity;

    velocity.x = planarVelocity.x;
    velocity.z = planarVelocity.z;
    velocity.y = pc.verticalVelocity;
    velocity.y = std::clamp(velocity.y, -30.0f, 30.0f);

    // Apply yaw to physics actor and keep collider aligned
    physics.setActorYaw(player->id, pc.yaw);
    player->rotation = glm::vec3(pc.pitch, pc.yaw, 0.0f);

    if (!physics.setLinearVelocity(player->id, velocity)) {
        player->position += velocity * delta;
    }
    syncLocalTransform(*player);
}

void Engine::updateRigidbody2D(float delta) {
    if (delta <= 0.0f) return;
    const float gravity = -9.81f;
    const float minEdgeThickness = 0.01f;
    auto getParentOffset = [&](const SceneObject& obj) {
        glm::vec2 offset(0.0f);
        const SceneObject* current = &obj;
        while (current && current->parentId >= 0) {
            auto pit = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                [&](const SceneObject& o) { return o.id == current->parentId; });
            if (pit == sceneObjects.end()) break;
            current = &(*pit);
            if (current->hasUI && current->ui.type != UIElementType::None) {
                offset += glm::vec2(current->ui.position.x, current->ui.position.y);
            }
        }
        return offset;
    };
    auto rotatePoint = [](const glm::vec2& p, float c, float s) {
        return glm::vec2(p.x * c - p.y * s, p.x * s + p.y * c);
    };
    auto buildHexagon = [](float radius, std::vector<glm::vec2>& out) {
        out.clear();
        for (int i = 0; i < 6; ++i) {
            float ang = static_cast<float>(i) * (2.0f * PI / 6.0f);
            out.emplace_back(std::cos(ang) * radius, std::sin(ang) * radius);
        }
    };
    auto computeAabb = [](const std::vector<glm::vec2>& pts, glm::vec2& outMin, glm::vec2& outMax) {
        if (pts.empty()) {
            outMin = glm::vec2(0.0f);
            outMax = glm::vec2(0.0f);
            return;
        }
        outMin = pts[0];
        outMax = pts[0];
        for (const auto& p : pts) {
            outMin.x = std::min(outMin.x, p.x);
            outMin.y = std::min(outMin.y, p.y);
            outMax.x = std::max(outMax.x, p.x);
            outMax.y = std::max(outMax.y, p.y);
        }
    };
    auto polyCenter = [](const std::vector<glm::vec2>& pts) {
        glm::vec2 c(0.0f);
        if (pts.empty()) return c;
        for (const auto& p : pts) c += p;
        return c / static_cast<float>(pts.size());
    };
    auto satOverlap = [&](const std::vector<glm::vec2>& a, const std::vector<glm::vec2>& b, glm::vec2& outAxis, float& outDepth) {
        if (a.size() < 3 || b.size() < 3) return false;
        auto testAxes = [&](const std::vector<glm::vec2>& poly, glm::vec2& axis, float& depth) {
            for (size_t i = 0; i < poly.size(); ++i) {
                glm::vec2 p0 = poly[i];
                glm::vec2 p1 = poly[(i + 1) % poly.size()];
                glm::vec2 edge = p1 - p0;
                glm::vec2 n = glm::normalize(glm::vec2(-edge.y, edge.x));
                float minA = FLT_MAX, maxA = -FLT_MAX;
                float minB = FLT_MAX, maxB = -FLT_MAX;
                for (const auto& p : a) {
                    float d = glm::dot(p, n);
                    minA = std::min(minA, d);
                    maxA = std::max(maxA, d);
                }
                for (const auto& p : b) {
                    float d = glm::dot(p, n);
                    minB = std::min(minB, d);
                    maxB = std::max(maxB, d);
                }
                float overlap = std::min(maxA, maxB) - std::max(minA, minB);
                if (overlap <= 0.0f) return false;
                if (overlap < depth) {
                    depth = overlap;
                    axis = n;
                }
            }
            return true;
        };
        glm::vec2 axis(0.0f);
        float depth = FLT_MAX;
        if (!testAxes(a, axis, depth)) return false;
        if (!testAxes(b, axis, depth)) return false;
        glm::vec2 dir = polyCenter(b) - polyCenter(a);
        if (glm::dot(axis, dir) < 0.0f) axis = -axis;
        outAxis = axis;
        outDepth = depth;
        return true;
    };
    auto segmentRect = [minEdgeThickness](const glm::vec2& a, const glm::vec2& b, float thickness, std::vector<glm::vec2>& out) {
        glm::vec2 dir = b - a;
        float len = glm::length(dir);
        if (len < 1e-4f) {
            out.clear();
            return;
        }
        glm::vec2 n = glm::vec2(-dir.y, dir.x) / len;
        float half = std::max(minEdgeThickness, thickness) * 0.5f;
        out.clear();
        out.push_back(a + n * half);
        out.push_back(b + n * half);
        out.push_back(b - n * half);
        out.push_back(a - n * half);
    };
    struct Body2DRef {
        int index = -1;
        bool dynamic = false;
        glm::vec2 parentOffset = glm::vec2(0.0f);
        glm::vec2 pivotWorld = glm::vec2(0.0f);
        float rotationRad = 0.0f;
        std::vector<glm::vec2> poly;
        std::vector<std::pair<glm::vec2, glm::vec2>> segments;
        float edgeThickness = 0.01f;
        glm::vec2 aabbMin = glm::vec2(0.0f);
        glm::vec2 aabbMax = glm::vec2(0.0f);
        bool isEdge = false;
    };
    std::vector<Body2DRef> bodies;
    bodies.reserve(sceneObjects.size());
    for (auto& obj : sceneObjects) {
        if (!obj.enabled || !HasUIComponent(obj)) continue;
        bool hasDynamic = obj.hasRigidbody2D && obj.rigidbody2D.enabled;
        bool hasCollider2D = obj.hasCollider2D && obj.collider2D.enabled;
        if (!hasDynamic && !hasCollider2D) continue;

        if (hasDynamic) {
            glm::vec2 vel = obj.rigidbody2D.velocity;
            if (obj.rigidbody2D.useGravity) {
                vel.y += gravity * obj.rigidbody2D.gravityScale * delta;
            }
            float damping = std::max(0.0f, obj.rigidbody2D.linearDamping);
            if (damping > 0.0f) {
                vel -= vel * std::min(1.0f, damping * delta);
            }
            obj.ui.position += vel * delta;
            obj.rigidbody2D.velocity = vel;
        }

        Body2DRef body;
        body.index = static_cast<int>(&obj - &sceneObjects[0]);
        body.dynamic = hasDynamic;
        body.parentOffset = getParentOffset(obj);
        body.pivotWorld = body.parentOffset + obj.ui.position;
        body.rotationRad = glm::radians(obj.ui.rotation);
        float c = std::cos(body.rotationRad);
        float s = std::sin(body.rotationRad);
        glm::vec2 size = glm::vec2(std::max(1.0f, obj.ui.size.x), std::max(1.0f, obj.ui.size.y));
        Collider2DType type = Collider2DType::Box;
        glm::vec2 boxSize = size;
        std::vector<glm::vec2> localPoints;
        bool closed = false;
        float edgeThickness = minEdgeThickness;
        if (hasCollider2D) {
            type = obj.collider2D.type;
            boxSize = obj.collider2D.boxSize;
            if (boxSize.x <= 0.0f || boxSize.y <= 0.0f) {
                boxSize = size;
            }
            localPoints = obj.collider2D.points;
            closed = obj.collider2D.closed;
            edgeThickness = obj.collider2D.edgeThickness;
        }
        if (type == Collider2DType::Box) {
            glm::vec2 half = boxSize * 0.5f;
            localPoints = {
                glm::vec2(-half.x, -half.y),
                glm::vec2( half.x, -half.y),
                glm::vec2( half.x,  half.y),
                glm::vec2(-half.x,  half.y)
            };
        } else if (type == Collider2DType::Polygon) {
            if (localPoints.empty()) {
                float radius = 0.5f * std::min(boxSize.x, boxSize.y);
                buildHexagon(radius, localPoints);
            }
        } else if (type == Collider2DType::Edge) {
            if (localPoints.size() < 2) {
                float half = boxSize.x * 0.5f;
                localPoints = { glm::vec2(-half, 0.0f), glm::vec2(half, 0.0f) };
            }
        }

        if (type == Collider2DType::Edge) {
            body.isEdge = true;
            body.edgeThickness = edgeThickness;
            for (size_t i = 0; i + 1 < localPoints.size(); ++i) {
                glm::vec2 a = rotatePoint(localPoints[i], c, s) + body.pivotWorld;
                glm::vec2 b = rotatePoint(localPoints[i + 1], c, s) + body.pivotWorld;
                body.segments.emplace_back(a, b);
            }
            if (closed && localPoints.size() > 2) {
                glm::vec2 a = rotatePoint(localPoints.back(), c, s) + body.pivotWorld;
                glm::vec2 b = rotatePoint(localPoints.front(), c, s) + body.pivotWorld;
                body.segments.emplace_back(a, b);
            }
        } else {
            body.poly.reserve(localPoints.size());
            for (const auto& p : localPoints) {
                body.poly.push_back(rotatePoint(p, c, s) + body.pivotWorld);
            }
            computeAabb(body.poly, body.aabbMin, body.aabbMax);
        }
        bodies.push_back(body);
    }

    auto applySeparation = [&](Body2DRef& body, const glm::vec2& sep, const glm::vec2& normal) {
        SceneObject& obj = sceneObjects[body.index];
        if (body.dynamic) {
            obj.ui.position += sep;
            body.pivotWorld += sep;
            if (!body.poly.empty()) {
                for (auto& p : body.poly) p += sep;
                body.aabbMin += sep;
                body.aabbMax += sep;
            }
            if (!body.segments.empty()) {
                for (auto& seg : body.segments) {
                    seg.first += sep;
                    seg.second += sep;
                }
            }
            float vn = glm::dot(obj.rigidbody2D.velocity, normal);
            if (vn < 0.0f) {
                obj.rigidbody2D.velocity -= normal * vn;
            }
        }
    };

    for (size_t i = 0; i < bodies.size(); ++i) {
        for (size_t j = i + 1; j < bodies.size(); ++j) {
            Body2DRef& a = bodies[i];
            Body2DRef& b = bodies[j];
            if (!a.dynamic && !b.dynamic) continue;

            auto polyVsPoly = [&](Body2DRef& pA, Body2DRef& pB) {
                if (pA.poly.empty() || pB.poly.empty()) return;
                if (pA.aabbMax.x <= pB.aabbMin.x || pA.aabbMin.x >= pB.aabbMax.x ||
                    pA.aabbMax.y <= pB.aabbMin.y || pA.aabbMin.y >= pB.aabbMax.y) {
                    return;
                }
                glm::vec2 axis(0.0f);
                float depth = 0.0f;
                if (!satOverlap(pA.poly, pB.poly, axis, depth)) return;
                glm::vec2 sep = axis * depth;
                if (pA.dynamic && pB.dynamic) {
                    applySeparation(pA, -sep * 0.5f, -axis);
                    applySeparation(pB, sep * 0.5f, axis);
                } else if (pA.dynamic) {
                    applySeparation(pA, -sep, -axis);
                } else if (pB.dynamic) {
                    applySeparation(pB, sep, axis);
                }
            };

            auto polyVsEdge = [&](Body2DRef& polyBody, Body2DRef& edgeBody) {
                if (polyBody.poly.empty() || edgeBody.segments.empty()) return;
                std::vector<glm::vec2> rect;
                for (const auto& seg : edgeBody.segments) {
                    segmentRect(seg.first, seg.second, edgeBody.edgeThickness, rect);
                    if (rect.size() < 3) continue;
                    glm::vec2 axis(0.0f);
                    float depth = 0.0f;
                    if (!satOverlap(polyBody.poly, rect, axis, depth)) continue;
                    glm::vec2 sep = axis * depth;
                    if (polyBody.dynamic && edgeBody.dynamic) {
                        applySeparation(polyBody, -sep * 0.5f, -axis);
                        applySeparation(edgeBody, sep * 0.5f, axis);
                    } else if (polyBody.dynamic) {
                        applySeparation(polyBody, -sep, -axis);
                    } else if (edgeBody.dynamic) {
                        applySeparation(edgeBody, sep, axis);
                    }
                }
            };

            if (!a.isEdge && !b.isEdge) {
                polyVsPoly(a, b);
            } else if (!a.isEdge && b.isEdge) {
                polyVsEdge(a, b);
            } else if (a.isEdge && !b.isEdge) {
                polyVsEdge(b, a);
            }
        }
    }
}

void Engine::updateCameraFollow2D(float delta) {
    if (sceneObjects.empty()) return;

    std::unordered_map<int, size_t> indexById;
    indexById.reserve(sceneObjects.size());
    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        indexById[sceneObjects[i].id] = i;
    }

    auto getUiWorldPosition = [&](const SceneObject& target) {
        glm::vec2 pos(target.ui.position.x, target.ui.position.y);
        int parentId = target.parentId;
        while (parentId >= 0) {
            auto it = indexById.find(parentId);
            if (it == indexById.end()) break;
            const SceneObject& parent = sceneObjects[it->second];
            if (parent.hasUI && parent.ui.type != UIElementType::None) {
                pos += glm::vec2(parent.ui.position.x, parent.ui.position.y);
            }
            parentId = parent.parentId;
        }
        return pos;
    };

    for (auto& obj : sceneObjects) {
        if (!obj.enabled || !obj.hasCamera || !obj.hasCameraFollow2D || !obj.cameraFollow2D.enabled) continue;
        if (obj.cameraFollow2D.targetId < 0) continue;
        auto targetIt = indexById.find(obj.cameraFollow2D.targetId);
        if (targetIt == indexById.end()) continue;

        const SceneObject& target = sceneObjects[targetIt->second];
        glm::vec2 desired2D = (target.hasUI && target.ui.type != UIElementType::None)
            ? getUiWorldPosition(target)
            : glm::vec2(target.position.x, target.position.y);
        desired2D += obj.cameraFollow2D.offset;
        glm::vec3 desired(desired2D.x, desired2D.y, obj.position.z);

        if (obj.cameraFollow2D.smoothTime > 0.0001f) {
            float alpha = 1.0f - std::exp(-delta / obj.cameraFollow2D.smoothTime);
            obj.position = glm::mix(obj.position, desired, alpha);
        } else {
            obj.position = desired;
        }

        if (obj.parentId == -1) {
            obj.localPosition = obj.position;
            obj.localInitialized = true;
        } else {
            auto parentIt = indexById.find(obj.parentId);
            if (parentIt != indexById.end()) {
                const SceneObject& parent = sceneObjects[parentIt->second];
                updateLocalFromWorld(obj,
                                     parent.position,
                                     QuatFromEulerXYZ(parent.rotation),
                                     parent.scale);
            }
        }
    }
}
#pragma endregion

#pragma region Skeletal Animation
namespace {
glm::vec3 sampleVecKeys(const std::vector<ModelSceneData::AnimVecKey>& keys, float time, const glm::vec3& fallback) {
    if (keys.empty()) return fallback;
    if (time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (time >= keys[i].time && time <= keys[i + 1].time) {
            float span = keys[i + 1].time - keys[i].time;
            float t = span > 0.0f ? (time - keys[i].time) / span : 0.0f;
            return glm::mix(keys[i].value, keys[i + 1].value, t);
        }
    }
    return keys.back().value;
}

glm::quat sampleQuatKeys(const std::vector<ModelSceneData::AnimQuatKey>& keys, float time, const glm::quat& fallback) {
    if (keys.empty()) return fallback;
    if (time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (time >= keys[i].time && time <= keys[i + 1].time) {
            float span = keys[i + 1].time - keys[i].time;
            float t = span > 0.0f ? (time - keys[i].time) / span : 0.0f;
            return glm::slerp(keys[i].value, keys[i + 1].value, t);
        }
    }
    return keys.back().value;
}
}

void Engine::updateSkeletalAnimations(float delta) {
    for (auto& obj : sceneObjects) {
        if (!obj.enabled || !obj.hasSkeletalAnimation || !obj.skeletal.enabled) continue;
        if (!obj.skeletal.useAnimation) continue;
        if (obj.meshPath.empty()) continue;

        ModelSceneData sceneData;
        std::string err;
        if (!getModelLoader().loadModelScene(obj.meshPath, sceneData, err)) continue;
        if (obj.skeletal.clipIndex < 0 || obj.skeletal.clipIndex >= (int)sceneData.animations.size()) continue;

        const auto& clip = sceneData.animations[obj.skeletal.clipIndex];
        double tps = clip.ticksPerSecond != 0.0 ? clip.ticksPerSecond : 25.0;
        obj.skeletal.time += delta * obj.skeletal.playSpeed;
        double timeTicks = obj.skeletal.time * tps;
        if (clip.duration > 0.0) {
            if (obj.skeletal.loop) {
                timeTicks = std::fmod(timeTicks, clip.duration);
                if (timeTicks < 0.0) timeTicks += clip.duration;
            } else {
                timeTicks = std::clamp(timeTicks, 0.0, clip.duration);
            }
        }
        float time = static_cast<float>(timeTicks);

        for (size_t b = 0; b < obj.skeletal.boneNames.size(); ++b) {
            int boneId = obj.skeletal.boneNodeIds.size() > b ? obj.skeletal.boneNodeIds[b] : -1;
            if (boneId < 0) continue;
            SceneObject* boneObj = findObjectById(boneId);
            if (!boneObj) continue;

            const ModelSceneData::AnimChannel* channel = nullptr;
            for (const auto& ch : clip.channels) {
                if (ch.nodeName == obj.skeletal.boneNames[b]) {
                    channel = &ch;
                    break;
                }
            }
            if (!channel) continue;

            glm::vec3 pos = sampleVecKeys(channel->positions, time, boneObj->localPosition);
            glm::quat rot = sampleQuatKeys(channel->rotations, time, QuatFromEulerXYZ(boneObj->localRotation));
            glm::vec3 scale = sampleVecKeys(channel->scales, time, boneObj->localScale);

            boneObj->localPosition = pos;
            boneObj->localRotation = NormalizeEulerDegrees(glm::degrees(glm::eulerAngles(rot)));
            boneObj->localScale = scale;
            boneObj->localInitialized = true;
        }
    }
}

void Engine::updateSkinningMatrices() {
    for (auto& obj : sceneObjects) {
        if (!obj.enabled || !obj.hasSkeletalAnimation || !obj.skeletal.enabled) continue;
        if (obj.skeletal.inverseBindMatrices.empty()) continue;

        glm::mat4 meshWorld = ComposeTransform(obj.position, obj.rotation, obj.scale);
        glm::mat4 invMesh = glm::inverse(meshWorld);

        size_t boneCount = obj.skeletal.inverseBindMatrices.size();
        if (obj.skeletal.finalMatrices.size() != boneCount) {
            obj.skeletal.finalMatrices.assign(boneCount, glm::mat4(1.0f));
        }

        for (size_t b = 0; b < boneCount; ++b) {
            int boneId = obj.skeletal.boneNodeIds.size() > b ? obj.skeletal.boneNodeIds[b] : -1;
            if (boneId < 0) {
                obj.skeletal.finalMatrices[b] = glm::mat4(1.0f);
                continue;
            }
            SceneObject* boneObj = findObjectById(boneId);
            if (!boneObj) continue;
            glm::mat4 boneWorld = ComposeTransform(boneObj->position, boneObj->rotation, boneObj->scale);
            obj.skeletal.finalMatrices[b] = invMesh * boneWorld * obj.skeletal.inverseBindMatrices[b];
        }
    }
}
#pragma endregion

void Engine::rebuildSkeletalBindings() {
    std::unordered_map<std::string, int> nameToId;
    nameToId.reserve(sceneObjects.size());
    for (const auto& obj : sceneObjects) {
        if (!obj.name.empty()) {
            nameToId[obj.name] = obj.id;
        }
    }

    for (auto& obj : sceneObjects) {
        if (!obj.hasRenderer || obj.renderType != RenderType::Model || obj.meshId < 0) continue;
        const auto* meshInfo = getModelLoader().getMeshInfo(obj.meshId);
        if (!meshInfo || !meshInfo->isSkinned) continue;

        if (!obj.hasSkeletalAnimation) {
            obj.skeletal = SkeletalAnimationComponent{};
            obj.hasSkeletalAnimation = true;
        }
        obj.skeletal.skeletonRootId = obj.parentId;
        obj.skeletal.boneNames = meshInfo->boneNames;
        obj.skeletal.inverseBindMatrices = meshInfo->inverseBindMatrices;
        obj.skeletal.finalMatrices.assign(meshInfo->boneNames.size(), glm::mat4(1.0f));
        obj.skeletal.boneNodeIds.assign(meshInfo->boneNames.size(), -1);
        for (size_t b = 0; b < meshInfo->boneNames.size(); ++b) {
            auto it = nameToId.find(meshInfo->boneNames[b]);
            if (it != nameToId.end()) {
                obj.skeletal.boneNodeIds[b] = it->second;
            }
        }
    }
}

#pragma region Transform Hierarchy
void Engine::updateLocalFromWorld(SceneObject& obj, const glm::vec3& parentPos, const glm::quat& parentRot, const glm::vec3& parentScale) {
    auto safeDiv = [](float v, float d) { return (std::abs(d) > 1e-6f) ? (v / d) : 0.0f; };
    auto unwrapNear = [](float angle, float reference) {
        float result = angle;
        while (result - reference > 180.0f) result -= 360.0f;
        while (reference - result > 180.0f) result += 360.0f;
        return result;
    };

    glm::quat invParent = glm::inverse(parentRot);
    glm::vec3 localPos = invParent * (obj.position - parentPos);
    localPos.x = safeDiv(localPos.x, parentScale.x);
    localPos.y = safeDiv(localPos.y, parentScale.y);
    localPos.z = safeDiv(localPos.z, parentScale.z);

    glm::quat worldRot = QuatFromEulerXYZ(obj.rotation);
    glm::quat localRot = invParent * worldRot;
    glm::vec3 localRotDeg = glm::degrees(ExtractEulerXYZ(glm::mat3_cast(localRot)));
    glm::vec3 refRot = obj.localInitialized ? obj.localRotation : obj.rotation;
    localRotDeg.x = unwrapNear(localRotDeg.x, refRot.x);
    localRotDeg.y = unwrapNear(localRotDeg.y, refRot.y);
    localRotDeg.z = unwrapNear(localRotDeg.z, refRot.z);

    glm::vec3 localScale(1.0f);
    localScale.x = safeDiv(obj.scale.x, parentScale.x);
    localScale.y = safeDiv(obj.scale.y, parentScale.y);
    localScale.z = safeDiv(obj.scale.z, parentScale.z);

    obj.localPosition = localPos;
    obj.localRotation = localRotDeg;
    obj.localScale = localScale;
    obj.localInitialized = true;
}

void Engine::initializeLocalTransformsFromWorld(int sceneVersion) {
    if (sceneObjects.empty()) return;

    if (sceneVersion >= 10) {
        for (auto& obj : sceneObjects) {
            if (!obj.localInitialized) {
                obj.localPosition = obj.position;
                obj.localRotation = NormalizeEulerDegrees(obj.rotation);
                obj.localScale = obj.scale;
                obj.localInitialized = true;
            }
        }
        updateHierarchyWorldTransforms();
        return;
    }

    std::unordered_map<int, glm::mat4> worldById;
    worldById.reserve(sceneObjects.size());
    for (const auto& obj : sceneObjects) {
        worldById[obj.id] = ComposeTransform(obj.position, obj.rotation, obj.scale);
    }

    for (auto& obj : sceneObjects) {
        if (obj.parentId == -1) {
            obj.localPosition = obj.position;
            obj.localRotation = NormalizeEulerDegrees(obj.rotation);
            obj.localScale = obj.scale;
            obj.localInitialized = true;
            continue;
        }
        auto itParent = worldById.find(obj.parentId);
        if (itParent == worldById.end()) {
            obj.localPosition = obj.position;
            obj.localRotation = NormalizeEulerDegrees(obj.rotation);
            obj.localScale = obj.scale;
            obj.localInitialized = true;
            continue;
        }
        glm::vec3 pPos, pRotDeg, pScale;
        DecomposeMatrix(itParent->second, pPos, pRotDeg, pScale);
        updateLocalFromWorld(obj, pPos, QuatFromEulerXYZ(pRotDeg), pScale);
    }

    updateHierarchyWorldTransforms();
}

void Engine::updateHierarchyWorldTransforms() {
    if (sceneObjects.empty()) return;

    std::unordered_map<int, size_t> indexById;
    indexById.reserve(sceneObjects.size());
    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        indexById[sceneObjects[i].id] = i;
    }

    auto unwrapNear = [](float angle, float reference) {
        float result = angle;
        while (result - reference > 180.0f) result -= 360.0f;
        while (reference - result > 180.0f) result += 360.0f;
        return result;
    };

    std::unordered_set<int> visiting;
    std::unordered_set<int> visited;
    visiting.reserve(sceneObjects.size());
    visited.reserve(sceneObjects.size());

    std::function<void(int, const glm::vec3&, const glm::quat&, const glm::vec3&)> processNode =
        [&](int id, const glm::vec3& parentPos, const glm::quat& parentRot, const glm::vec3& parentScale) {
        if (visited.count(id)) return;
        if (visiting.count(id)) return;
        auto itIndex = indexById.find(id);
        if (itIndex == indexById.end()) return;

        visiting.insert(id);
        SceneObject& obj = sceneObjects[itIndex->second];
        if (!obj.localInitialized) {
            obj.localPosition = obj.position;
            obj.localRotation = NormalizeEulerDegrees(obj.rotation);
            obj.localScale = obj.scale;
            obj.localInitialized = true;
        }

        bool useWorldAuthoritative = obj.hasRigidbody && obj.rigidbody.enabled && !obj.rigidbody.isKinematic;
        glm::vec3 worldPos = obj.position;
        glm::quat worldRot = QuatFromEulerXYZ(obj.rotation);
        glm::vec3 worldScale = obj.scale;
        if (useWorldAuthoritative) {
            updateLocalFromWorld(obj, parentPos, parentRot, parentScale);
            worldPos = obj.position;
            worldRot = QuatFromEulerXYZ(obj.rotation);
            worldScale = obj.scale;
        } else if (obj.parentId == -1) {
            obj.position = obj.localPosition;
            obj.rotation = NormalizeEulerDegrees(obj.localRotation);
            obj.scale = obj.localScale;
            worldPos = obj.position;
            worldRot = QuatFromEulerXYZ(obj.rotation);
            worldScale = obj.scale;
        } else {
            glm::quat localRot = QuatFromEulerXYZ(obj.localRotation);
            worldRot = parentRot * localRot;
            worldScale = parentScale * obj.localScale;
            worldPos = parentPos + parentRot * (parentScale * obj.localPosition);
            glm::vec3 worldRotDeg = glm::degrees(ExtractEulerXYZ(glm::mat3_cast(worldRot)));
            worldRotDeg.x = unwrapNear(worldRotDeg.x, obj.rotation.x);
            worldRotDeg.y = unwrapNear(worldRotDeg.y, obj.rotation.y);
            worldRotDeg.z = unwrapNear(worldRotDeg.z, obj.rotation.z);
            obj.position = worldPos;
            obj.rotation = worldRotDeg;
            obj.scale = worldScale;
        }

        for (int childId : obj.childIds) {
            processNode(childId, worldPos, worldRot, worldScale);
        }

        visiting.erase(id);
        visited.insert(id);
    };

    for (const auto& obj : sceneObjects) {
        if (obj.parentId == -1 || indexById.find(obj.parentId) == indexById.end()) {
            processNode(obj.id, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
        }
    }
}
#pragma endregion

#pragma region Project Lifecycle
void Engine::OpenProjectPath(const std::string& path) {
    startProjectLoad(path);
}

void Engine::startProjectLoad(const std::string& path) {
    if (projectLoadInProgress) return;
    projectManager.errorMessage.clear();
    projectLoadInProgress = true;
    projectLoadStartTime = glfwGetTime();
    projectLoadPath = path;
    showLauncher = true;
    launcherTransitionPendingHide = false;
    launcherLoadingPreviewPath.clear();
    fs::path previewPath = getProjectPreviewPath(path);
    if (!previewPath.empty() && fs::exists(previewPath)) {
        launcherLoadingPreviewPath = previewPath.string();
    }

    projectLoadFuture = std::async(std::launch::async, [path]() {
        ProjectLoadResult result;
        result.path = path;
        try {
            Project project;
            if (project.load(path)) {
                result.success = true;
                result.project = std::move(project);
            } else {
                result.error = "Failed to load project file";
            }
        } catch (const std::exception& e) {
            result.error = std::string("Exception opening project: ") + e.what();
        } catch (...) {
            result.error = "Unknown exception opening project";
        }
        return result;
    });
}

void Engine::pollProjectLoad() {
    if (!projectLoadInProgress) return;
    if (!projectLoadFuture.valid()) {
        projectLoadInProgress = false;
        return;
    }

    auto state = projectLoadFuture.wait_for(std::chrono::milliseconds(0));
    if (state == std::future_status::ready) {
        ProjectLoadResult result = projectLoadFuture.get();
        projectLoadInProgress = false;
        finishProjectLoad(result);
    }
}

void Engine::beginDeferredSceneLoad(const std::string& sceneName) {
    if (sceneLoadInProgress || !projectManager.currentProject.isLoaded) return;

    sceneObjects.clear();
    clearSelection();
    nextObjectId = 0;
    undoStack.clear();
    redoStack.clear();

    sceneLoadInProgress = true;
    sceneLoadProgress = 0.0f;
    sceneLoadStatus = "Reading scene...";
    sceneLoadSceneName = sceneName;
    sceneLoadObjects.clear();
    sceneLoadAssetIndices.clear();
    sceneLoadAssetsDone = 0;
    sceneLoadNextId = 0;
    sceneLoadVersion = 9;
    sceneLoadTimeOfDay = -1.0f;
    showLauncher = true;
    projectLoadStartTime = glfwGetTime();
    launcherLoadingPreviewPath.clear();
    fs::path previewPath = getProjectPreviewPath(projectManager.currentProject.projectPath);
    if (!previewPath.empty() && fs::exists(previewPath)) {
        launcherLoadingPreviewPath = previewPath.string();
    }

    fs::path scenePath = projectManager.currentProject.getSceneFilePath(sceneName);
    if (!fs::exists(scenePath)) {
        sceneLoadInProgress = false;
        addConsoleMessage("Default scene not found, starting with a new scene.", ConsoleMessageType::Info);
        addObject(ObjectType::Cube, "Cube");
        showLauncher = false;
        return;
    }

    if (!SceneSerializer::loadSceneDeferred(scenePath, sceneLoadObjects, sceneLoadNextId, sceneLoadVersion, &sceneLoadTimeOfDay)) {
        sceneLoadInProgress = false;
        addConsoleMessage("Error: Failed to load scene: " + sceneName, ConsoleMessageType::Error);
        addObject(ObjectType::Cube, "Cube");
        showLauncher = false;
        return;
    }

    for (size_t i = 0; i < sceneLoadObjects.size(); ++i) {
        const auto& obj = sceneLoadObjects[i];
        if (!obj.hasRenderer) continue;
        if ((obj.renderType == RenderType::OBJMesh || obj.renderType == RenderType::Model) &&
            !obj.meshPath.empty()) {
            sceneLoadAssetIndices.push_back(i);
        }
    }

    if (sceneLoadAssetIndices.empty()) {
        sceneLoadProgress = 1.0f;
        sceneLoadStatus = "Finalizing scene...";
        finalizeDeferredSceneLoad();
    } else {
        sceneLoadProgress = 0.0f;
        sceneLoadStatus = "Loading scene assets...";
    }
}

void Engine::pollSceneLoad() {
    if (!sceneLoadInProgress) return;

    if (sceneLoadAssetIndices.empty()) {
        return;
    }

    constexpr size_t kAssetsPerFrame = 1;
    size_t processed = 0;
    while (sceneLoadAssetsDone < sceneLoadAssetIndices.size() && processed < kAssetsPerFrame) {
        size_t objIndex = sceneLoadAssetIndices[sceneLoadAssetsDone];
        SceneObject& obj = sceneLoadObjects[objIndex];

        if (obj.renderType == RenderType::OBJMesh) {
            std::string err;
            obj.meshId = g_objLoader.loadOBJ(obj.meshPath, err);
            if (obj.meshId < 0 && !err.empty()) {
                std::cerr << "Failed to load OBJ: " << err << std::endl;
            }
        } else if (obj.renderType == RenderType::Model) {
            ModelSceneData sceneData;
            std::string err;
            if (getModelLoader().loadModelScene(obj.meshPath, sceneData, err)) {
                int sourceIndex = obj.meshSourceIndex;
                if (sourceIndex < 0 || sourceIndex >= (int)sceneData.meshIndices.size()) {
                    sourceIndex = 0;
                }
                if (!sceneData.meshIndices.empty() &&
                    sourceIndex >= 0 && sourceIndex < (int)sceneData.meshIndices.size()) {
                    obj.meshId = sceneData.meshIndices[sourceIndex];
                }
                ApplyModelRootTransform(obj, sceneData);
            } else {
                std::cerr << "Failed to load model from scene: " << err << std::endl;
                obj.meshId = -1;
            }
        }

        ++sceneLoadAssetsDone;
        ++processed;
    }

    float total = static_cast<float>(sceneLoadAssetIndices.size());
    sceneLoadProgress = total > 0.0f ? (static_cast<float>(sceneLoadAssetsDone) / total) : 1.0f;
    sceneLoadStatus = "Loading scene assets (" + std::to_string(sceneLoadAssetsDone) + "/" +
                      std::to_string(sceneLoadAssetIndices.size()) + ")";

    if (sceneLoadAssetsDone >= sceneLoadAssetIndices.size()) {
        sceneLoadStatus = "Finalizing scene...";
        finalizeDeferredSceneLoad();
    }
}

void Engine::finalizeDeferredSceneLoad() {
    if (!sceneLoadInProgress) return;

    sceneObjects = std::move(sceneLoadObjects);
    nextObjectId = sceneLoadNextId;

    initializeLocalTransformsFromWorld(sceneLoadVersion);
    rebuildSkeletalBindings();

    projectManager.currentProject.currentSceneName = sceneLoadSceneName;
    projectManager.currentProject.hasUnsavedChanges = false;
    projectManager.currentProject.saveProjectFile();
    clearSelection();

    bool hasAnyLight = std::any_of(sceneObjects.begin(), sceneObjects.end(), [](const SceneObject& o) {
        return o.hasLight;
    });
    if (!hasAnyLight) {
        addObject(ObjectType::DirectionalLight, "Directional Light");
    }

    recordState("sceneLoaded");
    addConsoleMessage("Loaded scene: " + sceneLoadSceneName, ConsoleMessageType::Success);
    if (sceneLoadTimeOfDay >= 0.0f) {
        if (Skybox* skybox = renderer.getSkybox()) {
            skybox->setTimeOfDay(sceneLoadTimeOfDay);
        }
    }

    // Deferred loading can complete after play mode was already entered.
    // Rebuild runtime systems so physics/audio always match the loaded scene.
    if (isPlaying || specMode || testMode) {
        physics.onPlayStart(sceneObjects);
        audio.onPlayStart(sceneObjects);
        if (playerMode) {
            syncPlayerCamera();
        }
    }

    sceneLoadInProgress = false;
    sceneLoadProgress = 1.0f;
    sceneLoadStatus.clear();
    sceneLoadAssetIndices.clear();
    if (launcherTransitionActive) {
        launcherTransitionPendingHide = true;
        showLauncher = true;
    } else {
        showLauncher = false;
    }
}

void Engine::finishProjectLoad(ProjectLoadResult& result) {
    if (!result.success) {
        projectManager.errorMessage = result.error.empty() ? "Failed to load project file" : result.error;
        addConsoleMessage("Error opening project: " + projectManager.errorMessage, ConsoleMessageType::Error);
        showLauncher = true;
        return;
    }

    projectManager.currentProject = std::move(result.project);
    projectManager.addToRecentProjects(projectManager.currentProject.name, result.path);

    // Make sure project folders exist even for older/minimal projects
    if (!fs::exists(projectManager.currentProject.assetsPath)) {
        fs::create_directories(projectManager.currentProject.assetsPath);
    }
    if (!fs::exists(projectManager.currentProject.scenesPath)) {
        fs::create_directories(projectManager.currentProject.scenesPath);
    }

    packageManager.setProjectRoot(projectManager.currentProject.projectPath);

    if (!initRenderer()) {
        addConsoleMessage("Error: Failed to initialize renderer!", ConsoleMessageType::Error);
        showLauncher = true;
        return;
    }

    if (!physics.isReady() && !physics.init()) {
        addConsoleMessage("Warning: PhysX failed to initialize; physics disabled for this session", ConsoleMessageType::Warning);
    }

    loadBuildSettings();
    if (autoStartRequested && !autoStartSceneName.empty()) {
        beginDeferredSceneLoad(autoStartSceneName);
    } else {
        loadRecentScenes();
    }
    fs::path contentRoot = projectManager.currentProject.usesNewLayout
        ? projectManager.currentProject.assetsPath
        : projectManager.currentProject.projectPath;
    fileBrowser.setProjectRoot(contentRoot);
    fileBrowser.currentPath = contentRoot;
    loadEditorUserSettings();
    fileBrowser.needsRefresh = true;
    scriptEditorWindowsDirty = true;
    scriptEditorWindows.clear();
    scriptLastAutoCompileTime.clear();
    autoCompileQueue.clear();
    autoCompileQueued.clear();
    scriptAutoCompileLastCheck = 0.0;
    if (!sceneLoadInProgress) {
        if (launcherTransitionActive) {
            launcherTransitionPendingHide = true;
            showLauncher = true;
        } else {
            showLauncher = false;
        }
    }
    #ifdef MODULARITY_PLAYER
    applyAutoStartMode();
    #else
    if (autoStartRequested && autoStartPlayerMode) {
        applyAutoStartMode();
    } else {
        playerMode = false;
        startupSplashStartTime = -1.0;
    }
    #endif
    if (playerMode) {
        syncPlayerCamera();
    }
    applyBuildWindowTitle();
    addConsoleMessage("Opened project: " + projectManager.currentProject.name, ConsoleMessageType::Info);
}

void Engine::syncPlayerCamera() {
    const SceneObject* playerCamObj = nullptr;
    const SceneObject* sceneCamObj = nullptr;
    const SceneObject* fallbackCamObj = nullptr;
    for (const auto& obj : sceneObjects) {
        if (!obj.enabled || !obj.hasCamera) continue;
        if (!fallbackCamObj) fallbackCamObj = &obj;
        if (!sceneCamObj && obj.camera.type == SceneCameraType::Scene) {
            sceneCamObj = &obj;
        }
        if (obj.camera.type == SceneCameraType::Player) {
            playerCamObj = &obj;
            break;
        }
    }

    const SceneObject* activeCam = playerCamObj ? playerCamObj : (sceneCamObj ? sceneCamObj : fallbackCamObj);
    if (!activeCam) {
        return;
    }
    Camera cam = makeCameraFromObject(*activeCam);
    cam.position = activeCam->position;
    cam.firstMouse = true;
    camera = cam;
}

void Engine::loadAutoStartConfig() {
    autoStartRequested = false;
    autoStartPlayerMode = false;
    autoStartProjectPath.clear();
    autoStartSceneName.clear();

    fs::path configPath = fs::current_path() / "autostart.modu";
    if (!fs::exists(configPath)) return;

    std::ifstream file(configPath);
    if (!file.is_open()) return;

    auto trim = [](std::string& s) {
        auto start = s.find_first_not_of(" \t\r\n");
        auto end = s.find_last_not_of(" \t\r\n");
        if (start == std::string::npos || end == std::string::npos) {
            s.clear();
            return;
        }
        s = s.substr(start, end - start + 1);
    };

    std::string line;
    bool modeSpecified = false;
    bool sawKey = false;
    while (std::getline(file, line)) {
        trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            if (!sawKey && autoStartProjectPath.empty()) {
                autoStartProjectPath = line;
            }
            continue;
        }
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        trim(key);
        trim(value);
        sawKey = true;
        if (key == "project") {
            autoStartProjectPath = value;
        } else if (key == "scene") {
            autoStartSceneName = value;
        } else if (key == "mode") {
            autoStartPlayerMode = (value == "player");
            modeSpecified = true;
        }
    }

    if (!autoStartProjectPath.empty()) {
        fs::path path = autoStartProjectPath;
        if (path.is_relative()) {
            path = fs::current_path() / path;
        }
        autoStartProjectPath = path.lexically_normal().string();
        autoStartRequested = true;
        if (!modeSpecified) {
            autoStartPlayerMode = true;
        }
    }
}

void Engine::applyAutoStartMode() {
    playerMode = true;
    isPlaying = true;
    specMode = false;
    testMode = false;
    gameViewCursorLocked = true;
    gameViewportFocused = true;
    showHierarchy = false;
    showInspector = false;
    showFileBrowser = false;
    showConsole = false;
    showProjectBrowser = false;
    showMeshBuilder = false;
    showEnvironmentWindow = false;
    showCameraWindow = false;
    showAnimationWindow = false;
    showAIPathfindingWindow = false;
    showViewOutput = false;
    showSceneGizmos = false;
    showGameViewport = false;
    showBuildSettings = false;
    viewportFullscreen = true;
    if (editorWindow) {
        glfwFocusWindow(editorWindow);
        glfwSetInputMode(editorWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(editorWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
    }
    physics.onPlayStart(sceneObjects);
    audio.onPlayStart(sceneObjects);
    startupSplashStartTime = glfwGetTime();
    applyBuildWindowTitle();
}

fs::path Engine::resolveSplashImagePath() const {
    if (buildSettings.splashImagePath.empty()) {
        return {};
    }
    fs::path path = fs::path(buildSettings.splashImagePath);
    if (path.is_absolute()) {
        return path;
    }
    if (projectManager.currentProject.isLoaded) {
        return projectManager.currentProject.projectPath / path;
    }
    return fs::current_path() / path;
}

void Engine::applyBuildWindowTitle() {
    if (!editorWindow) return;

    std::string displayName = buildSettings.buildName;
    if (displayName.empty()) {
        displayName = projectManager.currentProject.isLoaded
            ? projectManager.currentProject.name
            : "Modularity";
    }
    std::string version = buildSettings.version;
    std::string title;
    if (playerMode) {
        title = displayName;
        if (!version.empty()) {
            title += " " + version;
        }
    } else {
        title = "Modularity";
        if (!displayName.empty()) {
            title += " - " + displayName;
        }
        if (!version.empty()) {
            title += " (v" + version + ")";
        }
    }
    glfwSetWindowTitle(editorWindow, title.c_str());
}

void Engine::resetBuildSettings() {
#ifdef _WIN32
    buildSettings.platform = BuildPlatform::Windows;
#else
    buildSettings.platform = BuildPlatform::Linux;
#endif
    buildSettings.architecture = "x86_64";
    buildSettings.companyName = "DefaultCompany";
    buildSettings.buildName = projectManager.currentProject.name.empty()
        ? "MyProject"
        : projectManager.currentProject.name;
    buildSettings.version = "0.1.0";
    buildSettings.splashImagePath.clear();
    buildSettings.splashEnabled = false;
    buildSettings.splashDurationSeconds = 2.5f;
    buildSettings.packageStandaloneArchive = true;
    buildSettings.developmentBuild = false;
    buildSettings.autoConnectProfiler = false;
    buildSettings.scriptDebugging = false;
    buildSettings.deepProfiling = false;
    buildSettings.scriptsOnlyBuild = false;
    buildSettings.serverBuild = false;
    buildSettings.compressionMethod = "Default";
    buildSettings.rendererAmbientColor = glm::vec3(0.2f, 0.2f, 0.2f);
    buildSettings.rendererShadowResolution = 512;
    buildSettings.rendererAutoReloadShaders = true;
    buildSettings.editorCameraFov = FOV;
    buildSettings.editorCameraNear = NEAR_PLANE;
    buildSettings.editorCameraFar = FAR_PLANE;
    buildSettings.scenes.clear();
    buildSettingsSelectedIndex = -1;
    buildSettingsDirty = false;
}

bool Engine::addSceneToBuildSettings(const std::string& sceneName, bool enabled) {
    if (sceneName.empty()) return false;
    for (const auto& entry : buildSettings.scenes) {
        if (entry.name == sceneName) return false;
    }
    buildSettings.scenes.push_back({sceneName, enabled});
    buildSettingsDirty = true;
    return true;
}

void Engine::loadBuildSettings() {
    resetBuildSettings();
    if (!projectManager.currentProject.isLoaded) return;

    fs::path buildPath = projectManager.currentProject.projectPath / "build.modu";
    if (!fs::exists(buildPath)) {
        if (!projectManager.currentProject.currentSceneName.empty()) {
            addSceneToBuildSettings(projectManager.currentProject.currentSceneName, true);
        }
        saveBuildSettings();
        return;
    }

    auto trim = [](std::string& s) {
        auto start = s.find_first_not_of(" \t\r\n");
        auto end = s.find_last_not_of(" \t\r\n");
        if (start == std::string::npos || end == std::string::npos) {
            s.clear();
            return;
        }
        s = s.substr(start, end - start + 1);
    };

    std::ifstream file(buildPath);
    std::string line;
    while (std::getline(file, line)) {
        trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("platform=", 0) == 0) {
            std::string value = line.substr(9);
            trim(value);
            if (value == "Windows") buildSettings.platform = BuildPlatform::Windows;
            else if (value == "Linux") buildSettings.platform = BuildPlatform::Linux;
            else if (value == "Android") buildSettings.platform = BuildPlatform::Android;
        } else if (line.rfind("architecture=", 0) == 0) {
            buildSettings.architecture = line.substr(13);
            trim(buildSettings.architecture);
        } else if (line.rfind("companyName=", 0) == 0) {
            buildSettings.companyName = line.substr(12);
            trim(buildSettings.companyName);
        } else if (line.rfind("buildName=", 0) == 0) {
            buildSettings.buildName = line.substr(10);
            trim(buildSettings.buildName);
        } else if (line.rfind("version=", 0) == 0) {
            buildSettings.version = line.substr(8);
            trim(buildSettings.version);
        } else if (line.rfind("splashImage=", 0) == 0) {
            buildSettings.splashImagePath = line.substr(12);
            trim(buildSettings.splashImagePath);
        } else if (line.rfind("splashEnabled=", 0) == 0) {
            buildSettings.splashEnabled = line.substr(14) == "1";
        } else if (line.rfind("splashDuration=", 0) == 0) {
            buildSettings.splashDurationSeconds = std::max(0.0f, std::stof(line.substr(15)));
        } else if (line.rfind("packageStandaloneArchive=", 0) == 0) {
            buildSettings.packageStandaloneArchive = line.substr(25) == "1";
        } else if (line.rfind("developmentBuild=", 0) == 0) {
            buildSettings.developmentBuild = line.substr(17) == "1";
        } else if (line.rfind("autoConnectProfiler=", 0) == 0) {
            buildSettings.autoConnectProfiler = line.substr(20) == "1";
        } else if (line.rfind("scriptDebugging=", 0) == 0) {
            buildSettings.scriptDebugging = line.substr(16) == "1";
        } else if (line.rfind("deepProfiling=", 0) == 0) {
            buildSettings.deepProfiling = line.substr(14) == "1";
        } else if (line.rfind("scriptsOnlyBuild=", 0) == 0) {
            buildSettings.scriptsOnlyBuild = line.substr(17) == "1";
        } else if (line.rfind("serverBuild=", 0) == 0) {
            buildSettings.serverBuild = line.substr(12) == "1";
        } else if (line.rfind("compressionMethod=", 0) == 0) {
            buildSettings.compressionMethod = line.substr(18);
            trim(buildSettings.compressionMethod);
        } else if (line.rfind("rendererAmbient=", 0) == 0) {
            std::string value = line.substr(16);
            trim(value);
            std::replace(value.begin(), value.end(), ',', ' ');
            std::stringstream ss(value);
            ss >> buildSettings.rendererAmbientColor.x
               >> buildSettings.rendererAmbientColor.y
               >> buildSettings.rendererAmbientColor.z;
        } else if (line.rfind("rendererShadowResolution=", 0) == 0) {
            buildSettings.rendererShadowResolution = std::clamp(std::atoi(line.substr(25).c_str()), 128, 4096);
        } else if (line.rfind("rendererAutoReloadShaders=", 0) == 0) {
            buildSettings.rendererAutoReloadShaders = line.substr(26) == "1";
        } else if (line.rfind("editorCameraFov=", 0) == 0) {
            buildSettings.editorCameraFov = std::clamp(std::stof(line.substr(16)), 20.0f, 140.0f);
        } else if (line.rfind("editorCameraNear=", 0) == 0) {
            buildSettings.editorCameraNear = std::max(0.01f, std::stof(line.substr(17)));
        } else if (line.rfind("editorCameraFar=", 0) == 0) {
            buildSettings.editorCameraFar = std::max(buildSettings.editorCameraNear + 1.0f, std::stof(line.substr(16)));
        } else if (line.rfind("scene=", 0) == 0) {
            std::string value = line.substr(6);
            trim(value);
            size_t comma = value.find(',');
            if (comma != std::string::npos) {
                std::string name = value.substr(0, comma);
                std::string enabledStr = value.substr(comma + 1);
                trim(name);
                trim(enabledStr);
                if (!name.empty()) {
                    buildSettings.scenes.push_back({name, enabledStr == "1"});
                }
            }
        }
    }

    if (buildSettings.scenes.empty() && !projectManager.currentProject.currentSceneName.empty()) {
        addSceneToBuildSettings(projectManager.currentProject.currentSceneName, true);
    }
    if (buildSettings.buildName.empty()) {
        buildSettings.buildName = projectManager.currentProject.name.empty()
            ? "MyProject"
            : projectManager.currentProject.name;
    }
    if (buildSettings.companyName.empty()) {
        buildSettings.companyName = "DefaultCompany";
    }
    if (buildSettings.version.empty()) {
        buildSettings.version = "0.1.0";
    }
    if (buildSettings.editorCameraFar <= buildSettings.editorCameraNear + 0.01f) {
        buildSettings.editorCameraFar = buildSettings.editorCameraNear + 0.01f;
    }
    buildSettings.splashDurationSeconds = std::clamp(buildSettings.splashDurationSeconds, 0.0f, 30.0f);
    if (rendererInitialized) {
        renderer.setAmbientColor(buildSettings.rendererAmbientColor);
        renderer.setShadowMapResolution(buildSettings.rendererShadowResolution);
        renderer.setShaderAutoReload(buildSettings.rendererAutoReloadShaders);
    }
    applyBuildWindowTitle();
    buildSettingsDirty = false;
}

void Engine::saveBuildSettings() {
    if (!projectManager.currentProject.isLoaded) return;
    fs::path buildPath = projectManager.currentProject.projectPath / "build.modu";
    std::ofstream file(buildPath);
    file << "# build.modu\n";
    const char* platformName = "Windows";
    if (buildSettings.platform == BuildPlatform::Linux) platformName = "Linux";
    else if (buildSettings.platform == BuildPlatform::Android) platformName = "Android";
    file << "platform=" << platformName << "\n";
    file << "architecture=" << buildSettings.architecture << "\n";
    file << "companyName=" << buildSettings.companyName << "\n";
    file << "buildName=" << buildSettings.buildName << "\n";
    file << "version=" << buildSettings.version << "\n";
    file << "splashImage=" << buildSettings.splashImagePath << "\n";
    file << "splashEnabled=" << (buildSettings.splashEnabled ? "1" : "0") << "\n";
    file << "splashDuration=" << buildSettings.splashDurationSeconds << "\n";
    file << "packageStandaloneArchive=" << (buildSettings.packageStandaloneArchive ? "1" : "0") << "\n";
    file << "developmentBuild=" << (buildSettings.developmentBuild ? "1" : "0") << "\n";
    file << "autoConnectProfiler=" << (buildSettings.autoConnectProfiler ? "1" : "0") << "\n";
    file << "scriptDebugging=" << (buildSettings.scriptDebugging ? "1" : "0") << "\n";
    file << "deepProfiling=" << (buildSettings.deepProfiling ? "1" : "0") << "\n";
    file << "scriptsOnlyBuild=" << (buildSettings.scriptsOnlyBuild ? "1" : "0") << "\n";
    file << "serverBuild=" << (buildSettings.serverBuild ? "1" : "0") << "\n";
    file << "compressionMethod=" << buildSettings.compressionMethod << "\n";
    file << "rendererAmbient="
         << buildSettings.rendererAmbientColor.x << ","
         << buildSettings.rendererAmbientColor.y << ","
         << buildSettings.rendererAmbientColor.z << "\n";
    file << "rendererShadowResolution=" << buildSettings.rendererShadowResolution << "\n";
    file << "rendererAutoReloadShaders=" << (buildSettings.rendererAutoReloadShaders ? "1" : "0") << "\n";
    file << "editorCameraFov=" << buildSettings.editorCameraFov << "\n";
    file << "editorCameraNear=" << buildSettings.editorCameraNear << "\n";
    file << "editorCameraFar=" << buildSettings.editorCameraFar << "\n";
    for (const auto& scene : buildSettings.scenes) {
        file << "scene=" << scene.name << "," << (scene.enabled ? "1" : "0") << "\n";
    }
    applyBuildWindowTitle();
    buildSettingsDirty = false;
}

void Engine::startExportBuild(const fs::path& outputDir, bool runAfter) {
    if (!projectManager.currentProject.isLoaded) {
        addConsoleMessage("No project loaded for export", ConsoleMessageType::Warning);
        return;
    }
    if (exportJob.active) return;

    if (projectManager.currentProject.hasUnsavedChanges) {
        saveCurrentScene();
    } else {
        projectManager.currentProject.saveProjectFile();
    }
    saveBuildSettings();

    std::error_code ec;
    fs::path normalizedOut = fs::absolute(outputDir, ec);
    if (ec) {
        addConsoleMessage("Export failed: invalid output path.", ConsoleMessageType::Error);
        return;
    }
    fs::create_directories(normalizedOut, ec);
    if (ec) {
        addConsoleMessage("Export failed: unable to create output folder.", ConsoleMessageType::Error);
        return;
    }

    fs::path sourceRoot = findCMakeSourceRoot(fs::current_path());
    if (sourceRoot.empty()) {
        addConsoleMessage("Export failed: could not locate CMakeLists.txt.", ConsoleMessageType::Error);
        return;
    }

    std::string startScene = projectManager.currentProject.currentSceneName;
    if (startScene.empty()) {
        for (const auto& scene : buildSettings.scenes) {
            if (scene.enabled) {
                startScene = scene.name;
                break;
            }
        }
    }

    std::string buildNameDisplay = buildSettings.buildName.empty()
        ? (projectManager.currentProject.name.empty() ? "Game" : projectManager.currentProject.name)
        : buildSettings.buildName;
    std::string buildVersionDisplay = buildSettings.version.empty() ? "0.1.0" : buildSettings.version;
    std::string executableStem = sanitizeBuildToken(buildNameDisplay, "Game");
    std::string safeVersion = sanitizeBuildToken(buildVersionDisplay, "0.1.0");
    std::string platformLabel = "Windows";
    if (buildSettings.platform == BuildPlatform::Linux) platformLabel = "Linux";
    else if (buildSettings.platform == BuildPlatform::Android) platformLabel = "Android";
    std::string packageStem = executableStem + "-" + safeVersion + "-" + platformLabel;
    fs::path exportRoot = normalizedOut / packageStem;
    fs::path archivePath = normalizedOut / (packageStem + ".tar.gz");
    std::string executableFileName = executableStem;
#ifdef _WIN32
    executableFileName += ".exe";
#endif

    {
        std::lock_guard<std::mutex> lock(exportMutex);
        exportJob = ExportJobState{};
        exportJob.active = true;
        exportJob.runAfter = runAfter;
        exportJob.progress = 0.02f;
        exportJob.status = "Preparing export...";
        exportJob.outputDir = exportRoot;
        exportJob.executableName = executableFileName;
        exportJob.archivePath = buildSettings.packageStandaloneArchive ? archivePath : fs::path{};
    }
    exportCancelRequested = false;

    fs::path projectRoot = projectManager.currentProject.projectPath;
    bool usesNewLayout = projectManager.currentProject.usesNewLayout;
    fs::path scenesPath = projectManager.currentProject.scenesPath;
    fs::path scriptsPath = projectManager.currentProject.scriptsPath;
    fs::path scriptsConfigPath = resolveScriptsConfigPath(projectManager.currentProject);
    std::vector<fs::path> assignedNativeScriptSources;
    {
        std::unordered_set<std::string> seenSources;
        for (const auto& obj : sceneObjects) {
            for (const auto& sc : obj.scripts) {
                if (sc.path.empty()) continue;
                if (sc.language != ScriptLanguage::C && sc.language != ScriptLanguage::Cpp) continue;
                fs::path sourcePath(sc.path);
                std::string key = sourcePath.lexically_normal().string();
                if (seenSources.insert(key).second) {
                    assignedNativeScriptSources.push_back(sourcePath);
                }
            }
        }
    }
    bool packageStandaloneArchive = buildSettings.packageStandaloneArchive;

    auto future = std::async(std::launch::async,
        [this, normalizedOut, exportRoot, archivePath, sourceRoot, projectRoot, startScene, usesNewLayout,
         scenesPath, scriptsPath, scriptsConfigPath, assignedNativeScriptSources,
         packageStandaloneArchive, executableStem, executableFileName, packageStem]() {
        ExportJobResult result;
        result.outputDir = exportRoot;
        result.executableName = executableFileName;
        result.archivePath = packageStandaloneArchive ? archivePath : fs::path{};

        auto setStatus = [this](float value, const std::string& status) {
            std::lock_guard<std::mutex> lock(exportMutex);
            exportJob.progress = value;
            exportJob.status = status;
        };
        auto appendLog = [this](const std::string& text) {
            std::lock_guard<std::mutex> lock(exportMutex);
            exportJob.log += text;
            if (!exportJob.log.empty() && exportJob.log.back() != '\n') {
                exportJob.log += '\n';
            }
        };

        std::error_code ec;
        if (exportCancelRequested.load()) {
            result.message = "Export cancelled.";
            result.success = false;
            return result;
        }
        fs::create_directories(exportRoot, ec);
        if (ec) {
            result.message = "Failed to create export directory.";
            return result;
        }

        setStatus(0.05f, "Cleaning export output...");
        std::string cleanError;
        cleanExportOutput(exportRoot, executableStem.c_str(), cleanError);
        if (!cleanError.empty()) {
            result.message = cleanError;
            return result;
        }
        if (packageStandaloneArchive && fs::exists(archivePath)) {
            fs::remove(archivePath, ec);
            if (ec) {
                result.message = "Failed to remove existing archive: " + archivePath.string();
                return result;
            }
        }

        fs::path sharedBuildRoot = sourceRoot / "build" / "player-cache";
        bool useSharedBuild = fs::exists(sharedBuildRoot / "CMakeCache.txt");
        fs::path buildRoot = useSharedBuild ? sharedBuildRoot : (normalizedOut / "_build");
        if (!useSharedBuild) {
            fs::create_directories(buildRoot, ec);
            if (ec) {
                result.message = "Failed to create build directory.";
                return result;
            }
        }
        cleanEditorExecutable(buildRoot);

        setStatus(0.1f, useSharedBuild ? "Configuring cached build..." : "Configuring build...");
        int configureExit = 0;
        std::string configureCmd = "cmake -S \"" + sourceRoot.string() + "\" -B \"" +
                                   buildRoot.string() + "\" -DCMAKE_BUILD_TYPE=Release -DMODULARITY_BUILD_EDITOR=OFF";
        appendLog("Running: " + configureCmd);
        if (!runCommandStreaming(configureCmd + " 2>&1", appendLog, &configureExit)) {
            result.message = "CMake configure failed (exit code " + std::to_string(configureExit) + ").";
            return result;
        }

        if (exportCancelRequested.load()) {
            result.message = "Export cancelled.";
            result.success = false;
            return result;
        }

        setStatus(0.45f, "Building...");
        int buildExit = 0;
        std::string buildCmd = "cmake --build \"" + buildRoot.string() + "\" --config Release --target ModularityPlayer";
        appendLog("Running: " + buildCmd);
        auto onBuildChunk = [this, &appendLog](const std::string& chunk) {
            appendLog(chunk);
            // Parse lines like: "[ 17%] Building CXX object ..."
            size_t open = chunk.find('[');
            size_t pct = chunk.find('%');
            if (open != std::string::npos && pct != std::string::npos && pct > open) {
                std::string num = chunk.substr(open + 1, pct - open - 1);
                num.erase(0, num.find_first_not_of(" \t"));
                num.erase(num.find_last_not_of(" \t") + 1);
                int value = std::atoi(num.c_str());
                if (value >= 0 && value <= 100) {
                    float progress = 0.45f + (value / 100.0f) * 0.25f;
                    std::string label = "Building (" + std::to_string(value) + "%)";
                    std::lock_guard<std::mutex> lock(exportMutex);
                    exportJob.progress = progress;
                    exportJob.status = label;
                }
            }
        };
        if (!runCommandStreaming(buildCmd + " 2>&1", onBuildChunk, &buildExit)) {
            result.message = "CMake build failed (exit code " + std::to_string(buildExit) + ").";
            return result;
        }

        if (exportCancelRequested.load()) {
            result.message = "Export cancelled.";
            result.success = false;
            return result;
        }

        setStatus(0.7f, "Copying runtime...");
        fs::path exePath = resolveExecutablePath(buildRoot, "ModularityPlayer");
        if (exePath.empty()) {
            result.message = "Built executable not found.";
            return result;
        }

        fs::path destExe = exportRoot / executableFileName;
        fs::copy_file(exePath, destExe, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            result.message = "Failed to copy executable.";
            return result;
        }

        std::string copyError;
        if (!copyDirectoryRecursive(sourceRoot / "Resources", exportRoot / "Resources", copyError)) {
            result.message = copyError;
            return result;
        }

        setStatus(0.78f, "Collecting precompiled packages...");
        if (!copyPrecompiledPackages(buildRoot, exportRoot / "Packages" / "ThirdParty", copyError)) {
            result.message = copyError;
            return result;
        }

        setStatus(0.82f, "Collecting engine cache...");
        if (!copyPrecompiledEnginePackages(buildRoot, exportRoot / "Packages" / "Engine", copyError)) {
            result.message = copyError;
            return result;
        }

        {
            ScriptBuildConfig scriptConfig;
            std::string configError;
            if (scriptCompiler.loadConfig(scriptsConfigPath, scriptConfig, configError)) {
                auto isNativeSourceFile = [](const fs::path& sourcePath) {
                    std::string ext = sourcePath.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".c++";
                };

                packageManager.applyToBuildConfig(scriptConfig);

                auto resolveAssignedSource = [&](const fs::path& input) -> fs::path {
                    if (input.empty()) return {};
                    std::error_code sourceEc;
                    fs::path abs = fs::absolute(input, sourceEc);
                    if (sourceEc) abs = input;
                    if (fs::exists(abs)) return abs;

                    fs::path scriptsDir = scriptConfig.scriptsDir;
                    if (!scriptsDir.is_absolute()) {
                        scriptsDir = projectRoot / scriptsDir;
                    }

                    if (input.is_relative()) {
                        fs::path candidate = projectRoot / input;
                        if (fs::exists(candidate)) return candidate;
                    }

                    auto remapSuffix = [&](const fs::path& marker) -> fs::path {
                        std::vector<fs::path> parts;
                        for (const auto& p : abs) parts.push_back(p);
                        std::vector<fs::path> markerParts;
                        for (const auto& p : marker) markerParts.push_back(p);
                        if (markerParts.empty()) return {};
                        for (size_t i = 0; i + markerParts.size() <= parts.size(); ++i) {
                            bool match = true;
                            for (size_t k = 0; k < markerParts.size(); ++k) {
                                if (parts[i + k] != markerParts[k]) {
                                    match = false;
                                    break;
                                }
                            }
                            if (match) {
                                fs::path suffix;
                                for (size_t j = i + markerParts.size(); j < parts.size(); ++j) {
                                    suffix /= parts[j];
                                }
                                if (!suffix.empty()) {
                                    fs::path candidate = scriptsDir / suffix;
                                    if (fs::exists(candidate)) return candidate;
                                }
                                break;
                            }
                        }
                        return {};
                    };

                    fs::path remapped = remapSuffix(fs::path("Assets") / "Scripts");
                    if (!remapped.empty()) return remapped;
                    remapped = remapSuffix("Scripts");
                    if (!remapped.empty()) return remapped;

                    if (!abs.filename().empty()) {
                        fs::path candidate = scriptsDir / abs.filename();
                        if (fs::exists(candidate)) return candidate;
                    }
                    return {};
                };

                std::vector<fs::path> nativeSourcesToCompile;
                std::unordered_set<std::string> seenNativeSources;
                auto addNativeSource = [&](const fs::path& sourcePath) {
                    if (sourcePath.empty()) return;
                    std::error_code absEc;
                    fs::path abs = fs::absolute(sourcePath, absEc);
                    if (absEc) abs = sourcePath;
                    std::string key = abs.lexically_normal().string();
                    if (seenNativeSources.insert(key).second) {
                        nativeSourcesToCompile.push_back(abs);
                    }
                };

                for (const fs::path& sourceRef : assignedNativeScriptSources) {
                    fs::path resolvedSource = resolveAssignedSource(sourceRef);
                    if (resolvedSource.empty()) {
                        appendLog("Warning: Native script source not found during export scan: " +
                                  sourceRef.string());
                        continue;
                    }
                    if (isNativeSourceFile(resolvedSource)) {
                        addNativeSource(resolvedSource);
                    }
                }

                std::vector<fs::path> scriptScanRoots;
                scriptScanRoots.push_back(scriptConfig.scriptsDir);
                scriptScanRoots.push_back(projectRoot / "Assets" / "Scripts");
                scriptScanRoots.push_back(projectRoot / "Scripts");
                if (!scriptsPath.empty()) {
                    scriptScanRoots.push_back(scriptsPath);
                }

                std::unordered_set<std::string> seenScanRoots;
                for (const auto& scanRoot : scriptScanRoots) {
                    std::error_code absEc;
                    fs::path absRoot = fs::absolute(scanRoot, absEc);
                    if (absEc) absRoot = scanRoot;
                    std::string rootKey = absRoot.lexically_normal().string();
                    if (!seenScanRoots.insert(rootKey).second) continue;

                    std::error_code scriptsEc;
                    if (!fs::exists(absRoot, scriptsEc)) continue;

                    for (auto it = fs::recursive_directory_iterator(absRoot, scriptsEc);
                         it != fs::recursive_directory_iterator(); ++it) {
                        if (scriptsEc) break;
                        if (!it->is_regular_file()) continue;
                        const fs::path sourcePath = it->path();
                        if (!isNativeSourceFile(sourcePath)) continue;
                        addNativeSource(sourcePath);
                    }
                }

                if (!nativeSourcesToCompile.empty()) {
                    setStatus(0.84f, "Compiling native scripts...");
                    for (const fs::path& resolvedSource : nativeSourcesToCompile) {
                        if (exportCancelRequested.load()) {
                            result.message = "Export cancelled.";
                            result.success = false;
                            return result;
                        }

                        ScriptBuildCommands commands;
                        std::string buildError;
                        if (!scriptCompiler.makeCommands(scriptConfig, resolvedSource, commands, buildError)) {
                            result.message = "Failed to prepare native script build for '" +
                                             resolvedSource.filename().string() + "': " + buildError;
                            return result;
                        }

                        appendLog("Compiling native script: " + resolvedSource.string());
                        ScriptCompileOutput compileOutput;
                        if (!scriptCompiler.compile(commands, compileOutput, buildError)) {
                            if (!compileOutput.compileLog.empty()) appendLog(compileOutput.compileLog);
                            if (!compileOutput.linkLog.empty()) appendLog(compileOutput.linkLog);
                            result.message = "Failed to compile native script '" +
                                             resolvedSource.filename().string() + "': " + buildError;
                            return result;
                        }
                        if (!compileOutput.compileLog.empty()) appendLog(compileOutput.compileLog);
                        if (!compileOutput.linkLog.empty()) appendLog(compileOutput.linkLog);
                    }
                }
            } else if (!assignedNativeScriptSources.empty()) {
                result.message = "Failed to load scripts config for export: " + configError;
                return result;
            }
        }

        setStatus(0.85f, "Copying project...");
        fs::path projectOut = exportRoot / "Project";
        if (fs::exists(projectRoot / "Assets")) {
            if (!copyDirectoryRecursive(projectRoot / "Assets", projectOut / "Assets", copyError)) {
                result.message = copyError;
                return result;
            }
        }
        if (!usesNewLayout) {
            if (fs::exists(scenesPath)) {
                if (!copyDirectoryRecursive(scenesPath, projectOut / "Scenes", copyError)) {
                    result.message = copyError;
                    return result;
                }
            }
            if (fs::exists(scriptsPath)) {
                if (!copyDirectoryRecursive(scriptsPath, projectOut / "Scripts", copyError)) {
                    result.message = copyError;
                    return result;
                }
            }
        }
        fs::path compiledScriptsSrc;
        fs::path compiledScriptsDst;
        {
            ScriptBuildConfig scriptConfig;
            std::string configError;
            if (scriptCompiler.loadConfig(scriptsConfigPath, scriptConfig, configError)) {
                compiledScriptsSrc = scriptConfig.outDir;
                if (!compiledScriptsSrc.is_absolute()) {
                    compiledScriptsSrc = projectRoot / compiledScriptsSrc;
                }
                std::error_code relEc;
                fs::path relOutDir = fs::relative(compiledScriptsSrc, projectRoot, relEc);
                if (!relEc && !relOutDir.empty()) {
                    bool hasDotDot = false;
                    for (const auto& part : relOutDir) {
                        if (part == "..") {
                            hasDotDot = true;
                            break;
                        }
                    }
                    if (!hasDotDot) {
                        compiledScriptsDst = projectOut / relOutDir;
                    }
                }
                if (compiledScriptsDst.empty()) {
                    compiledScriptsDst = projectOut / "Library" / "CompiledScripts";
                }
            }
        }
        if (compiledScriptsSrc.empty()) {
            compiledScriptsSrc = projectRoot / "Library" / "CompiledScripts";
            compiledScriptsDst = projectOut / "Library" / "CompiledScripts";
        }
        if (fs::exists(compiledScriptsSrc)) {
            if (!copyDirectoryRecursive(compiledScriptsSrc, compiledScriptsDst, copyError)) {
                result.message = copyError;
                return result;
            }
        }
        if (fs::exists(projectRoot / "Library" / "InstalledPackages")) {
            if (!copyDirectoryRecursive(projectRoot / "Library" / "InstalledPackages",
                                        projectOut / "Library" / "InstalledPackages", copyError)) {
                result.message = copyError;
                return result;
            }
        }

        std::vector<fs::path> projectFiles = {
            projectRoot / "project.modu",
            projectRoot / "build.modu",
            projectRoot / "scripts.modu",
            projectRoot / "Scripts.modu",
            projectRoot / "packages.modu"
        };
        if (!scriptsConfigPath.empty()) {
            projectFiles.push_back(scriptsConfigPath);
        }
        std::unordered_set<std::string> seenProjectFiles;
        for (const auto& src : projectFiles) {
            std::error_code srcAbsEc;
            fs::path srcAbs = fs::absolute(src, srcAbsEc);
            if (srcAbsEc) srcAbs = src;
            std::string srcKey = srcAbs.lexically_normal().string();
            if (!seenProjectFiles.insert(srcKey).second) continue;
            if (!fs::exists(src)) continue;
            fs::path dst = projectOut / src.filename();
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                result.message = "Failed to copy project file: " + src.filename().string();
                return result;
            }
        }

        if (!startScene.empty()) {
            fs::path srcScene = scenesPath / (startScene + ".scene");
            fs::path dstScene = usesNewLayout
                ? (projectOut / "Assets" / "Scenes" / (startScene + ".scene"))
                : (projectOut / "Scenes" / (startScene + ".scene"));
            if (fs::exists(srcScene)) {
                fs::create_directories(dstScene.parent_path(), ec);
                if (!ec) {
                    fs::copy_file(srcScene, dstScene, fs::copy_options::overwrite_existing, ec);
                }
                if (ec) {
                    result.message = "Failed to copy scene: " + srcScene.filename().string();
                    return result;
                }
            }
        }

        fs::path autoStartPath = exportRoot / "autostart.modu";
        std::ofstream autoStart(autoStartPath);
        if (!autoStart.is_open()) {
            result.message = "Failed to write autostart.modu.";
            return result;
        }
        autoStart << "project=Project/project.modu\n";
        if (!startScene.empty()) {
            autoStart << "scene=" << startScene << "\n";
        }
        autoStart << "mode=player\n";
        autoStart.close();

        fs::path buildAutoStartPath = buildRoot / "autostart.modu";
        std::ofstream buildAutoStart(buildAutoStartPath);
        if (buildAutoStart.is_open()) {
            buildAutoStart << "project=" << (exportRoot / "Project" / "project.modu").string() << "\n";
            if (!startScene.empty()) {
                buildAutoStart << "scene=" << startScene << "\n";
            }
            buildAutoStart << "mode=player\n";
            buildAutoStart.close();
        }

        if (packageStandaloneArchive) {
#ifdef _WIN32
            appendLog("Standalone archive packaging is skipped on Windows exports.");
            result.archivePath.clear();
#else
            setStatus(0.97f, "Packing standalone archive...");
            int archiveExit = 0;
            std::string archiveCmd = "cd " + quotePath(normalizedOut) + " && tar -czf " +
                                     quotePath(archivePath.filename()) + " " +
                                     quotePath(fs::path(packageStem));
            appendLog("Running: " + archiveCmd);
            if (!runCommandStreaming(archiveCmd + " 2>&1", appendLog, &archiveExit)) {
                result.message = "Failed to create standalone archive (exit code " + std::to_string(archiveExit) + ").";
                return result;
            }
#endif
        }

        setStatus(1.0f, "Export complete.");
        result.success = true;
        if (packageStandaloneArchive && !result.archivePath.empty()) {
            result.message = "Export complete. Archive: " + result.archivePath.string();
        } else {
            result.message = "Export complete.";
        }
        return result;
    });

    {
        std::lock_guard<std::mutex> lock(exportMutex);
        exportJob.future = std::move(future);
    }
}

void Engine::pollExportBuild() {
    if (!exportJob.active) return;
    if (!exportJob.future.valid()) {
        exportJob.active = false;
        return;
    }
    auto state = exportJob.future.wait_for(std::chrono::milliseconds(0));
    if (state != std::future_status::ready) return;

    ExportJobResult result = exportJob.future.get();
    {
        std::lock_guard<std::mutex> lock(exportMutex);
        exportJob.done = true;
        exportJob.active = false;
        exportJob.success = result.success;
        exportJob.status = result.message;
        exportJob.outputDir = result.outputDir;
        exportJob.executableName = result.executableName;
        exportJob.archivePath = result.archivePath;
        exportJob.cancelled = exportCancelRequested.load() && !result.success;
    }

    bool runAfter = false;
    std::string executableName;
    {
        std::lock_guard<std::mutex> lock(exportMutex);
        runAfter = exportJob.runAfter;
        executableName = exportJob.executableName;
    }

    if (result.success) {
        addConsoleMessage("Export finished: " + result.outputDir.string(), ConsoleMessageType::Success);
        if (runAfter) {
            if (executableName.empty()) {
                executableName =
#ifdef _WIN32
                    "ModularityPlayer.exe";
#else
                    "ModularityPlayer";
#endif
            }
            fs::path exePath = result.outputDir / executableName;
            if (fs::exists(exePath)) {
#ifdef _WIN32
                std::string runCmd = "start \"\" \"" + exePath.string() + "\"";
#else
                std::string runCmd = "\"" + exePath.string() + "\" &";
#endif
                std::string runOut;
                runCommandCapture(runCmd + " 2>&1", runOut);
            } else {
                addConsoleMessage("Export finished, but executable was not found to run.", ConsoleMessageType::Warning);
            }
        }
    } else if (exportJob.cancelled) {
        addConsoleMessage("Export cancelled.", ConsoleMessageType::Warning);
    } else {
        addConsoleMessage("Export failed: " + result.message, ConsoleMessageType::Error);
    }
}

void Engine::createNewProject(const char* name, const char* location) {
    fs::path basePath(location);
    fs::create_directories(basePath);

    Project newProject(name, basePath);
    if (newProject.create()) {
        projectManager.currentProject = newProject;
        projectManager.addToRecentProjects(name,
                                          (newProject.projectPath / "project.modu").string());

        packageManager.setProjectRoot(projectManager.currentProject.projectPath);

        if (!initRenderer()) {
            logToConsole("Error: Failed to initialize renderer!");
            return;
        }

        if (!physics.isReady() && !physics.init()) {
            addConsoleMessage("Warning: PhysX failed to initialize; physics disabled for this session", ConsoleMessageType::Warning);
        }

        sceneObjects.clear();
        clearSelection();
        nextObjectId = 0;

        addObject(ObjectType::Cube, "Cube");

        fs::path contentRoot = projectManager.currentProject.usesNewLayout
            ? projectManager.currentProject.assetsPath
            : projectManager.currentProject.projectPath;
        fileBrowser.setProjectRoot(contentRoot);
        fileBrowser.currentPath = contentRoot;
        fileBrowser.needsRefresh = true;
        scriptEditorWindowsDirty = true;
        scriptEditorWindows.clear();
        scriptLastAutoCompileTime.clear();
        autoCompileQueue.clear();
        autoCompileQueued.clear();
        scriptAutoCompileLastCheck = 0.0;

        showLauncher = false;
        firstFrame = true;

        addConsoleMessage("Created new project: " + std::string(name), ConsoleMessageType::Success);
        addConsoleMessage("Project location: " + newProject.projectPath.string(), ConsoleMessageType::Info);

        saveCurrentScene();
        loadBuildSettings();
    } else {
        projectManager.errorMessage = "Failed to create project directory";
    }
}
#pragma endregion

#pragma region Scene Management
void Engine::loadRecentScenes() {
    sceneObjects.clear();
    clearSelection();
    nextObjectId = 0;
    undoStack.clear();
    redoStack.clear();

    fs::path scenePath = projectManager.currentProject.getSceneFilePath(projectManager.currentProject.currentSceneName);
    if (fs::exists(scenePath)) {
        beginDeferredSceneLoad(projectManager.currentProject.currentSceneName);
        return;
    } else {
        addConsoleMessage("Default scene not found, starting with a new scene.", ConsoleMessageType::Info);
        addObject(ObjectType::Cube, "Cube");
        recordState("sceneLoaded");
    }

    fs::path contentRoot = projectManager.currentProject.usesNewLayout
        ? projectManager.currentProject.assetsPath
        : projectManager.currentProject.projectPath;
    fileBrowser.setProjectRoot(contentRoot);
    fileBrowser.currentPath = contentRoot;
    fileBrowser.needsRefresh = true;
}

fs::path Engine::getProjectPreviewPath(const fs::path& projectPathOrFile) const {
    if (projectPathOrFile.empty()) return {};
    fs::path root = projectPathOrFile;
    if (root.extension() == ".modu") {
        root = root.parent_path();
    }
    return root / "ProjectUserSettings" / "ProjectPreview.png";
}

void Engine::saveProjectPreview() {
    if (!projectManager.currentProject.isLoaded || !rendererInitialized) return;
    int width = renderer.getWidth();
    int height = renderer.getHeight();
    if (width <= 0 || height <= 0) return;

    unsigned int texId = renderer.getViewportTexture();
    if (!texId) return;

    fs::path previewPath = getProjectPreviewPath(projectManager.currentProject.projectPath);
    if (previewPath.empty()) return;
    fs::create_directories(previewPath.parent_path());

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    glBindTexture(GL_TEXTURE_2D, texId);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    const size_t rowBytes = static_cast<size_t>(width) * 4;
    stbi_write_png(previewPath.string().c_str(), width, height, 4, pixels.data(),
                   static_cast<int>(rowBytes));
}

void Engine::saveCurrentScene() {
    if (!projectManager.currentProject.isLoaded) return;

    fs::path scenePath = projectManager.currentProject.getSceneFilePath(projectManager.currentProject.currentSceneName);
    float timeOfDay = 0.0f;
    if (Skybox* skybox = renderer.getSkybox()) {
        timeOfDay = skybox->getTimeOfDay();
    }
    if (SceneSerializer::saveScene(scenePath, sceneObjects, nextObjectId, timeOfDay)) {
        projectManager.currentProject.hasUnsavedChanges = false;
        projectManager.currentProject.saveProjectFile();
        saveProjectPreview();
        addConsoleMessage("Saved scene: " + projectManager.currentProject.currentSceneName, ConsoleMessageType::Success);
    } else {
        addConsoleMessage("Error: Failed to save scene!", ConsoleMessageType::Error);
    }
}

void Engine::loadScene(const std::string& sceneName) {
    if (!projectManager.currentProject.isLoaded) return;

    if (projectManager.currentProject.hasUnsavedChanges) {
        saveCurrentScene();
    }

    fs::path scenePath = projectManager.currentProject.getSceneFilePath(sceneName);
    int sceneVersion = 9;
    float loadedTimeOfDay = -1.0f;
    if (SceneSerializer::loadScene(scenePath, sceneObjects, nextObjectId, sceneVersion, &loadedTimeOfDay)) {
        initializeLocalTransformsFromWorld(sceneVersion);
        rebuildSkeletalBindings();
        undoStack.clear();
        redoStack.clear();
        projectManager.currentProject.currentSceneName = sceneName;
        projectManager.currentProject.hasUnsavedChanges = false;
        projectManager.currentProject.saveProjectFile();
        clearSelection();
        bool hasAnyLight = std::any_of(sceneObjects.begin(), sceneObjects.end(), [](const SceneObject& o) {
            return o.hasLight;
        });
        if (!hasAnyLight) {
            addObject(ObjectType::DirectionalLight, "Directional Light");
        }
        recordState("sceneLoaded");
        addConsoleMessage("Loaded scene: " + sceneName, ConsoleMessageType::Success);
        if (loadedTimeOfDay >= 0.0f) {
            if (Skybox* skybox = renderer.getSkybox()) {
                skybox->setTimeOfDay(loadedTimeOfDay);
            }
        }
    } else {
        addConsoleMessage("Error: Failed to load scene: " + sceneName, ConsoleMessageType::Error);
    }
}

void Engine::createNewScene(const std::string& sceneName) {
    if (!projectManager.currentProject.isLoaded || sceneName.empty()) return;

    if (projectManager.currentProject.hasUnsavedChanges) {
        saveCurrentScene();
    }

    sceneObjects.clear();
    clearSelection();
    nextObjectId = 0;
    undoStack.clear();
    redoStack.clear();

    projectManager.currentProject.currentSceneName = sceneName;
    projectManager.currentProject.hasUnsavedChanges = true;

    addObject(ObjectType::Cube, "Cube");
    addObject(ObjectType::DirectionalLight, "Directional Light");
    saveCurrentScene();
    recordState("newScene");

    addConsoleMessage("Created new scene: " + sceneName, ConsoleMessageType::Success);
}
#pragma endregion

#pragma region Scene Objects
void Engine::addObject(ObjectType type, const std::string& baseName) {
    recordState("addObject");
    int id = nextObjectId++;
    std::string name = baseName + " " + std::to_string(id);
    SceneObject obj(name, ObjectType::Empty, id);
    ApplyObjectPreset(obj, type);
    obj.localPosition = obj.position;
    obj.localRotation = NormalizeEulerDegrees(obj.rotation);
    obj.localScale = obj.scale;
    obj.localInitialized = true;
    sceneObjects.push_back(obj);
    setPrimarySelection(id);
    if (projectManager.currentProject.isLoaded) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }
    logToConsole("Created: " + name);
}

void Engine::duplicateSelected() {
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
        [this](const SceneObject& obj) { return obj.id == selectedObjectId; });

    if (it != sceneObjects.end()) {
        recordState("duplicate");
        int id = nextObjectId++;
        SceneObject newObj(it->name + " (Copy)", ObjectType::Empty, id);
        newObj.type = it->type;
        newObj.position = it->position + glm::vec3(1.0f, 0.0f, 0.0f);
        newObj.rotation = it->rotation;
        newObj.scale = it->scale;
        newObj.hasRenderer = it->hasRenderer;
        newObj.renderType = it->renderType;
        newObj.hasLight = it->hasLight;
        newObj.hasCamera = it->hasCamera;
        newObj.hasPostFX = it->hasPostFX;
        newObj.hasUI = it->hasUI;
        newObj.meshPath = it->meshPath;
        newObj.meshId = it->meshId;
        newObj.meshSourceIndex = it->meshSourceIndex;
        newObj.material = it->material;
        newObj.materialPath = it->materialPath;
        newObj.albedoTexturePath = it->albedoTexturePath;
        newObj.overlayTexturePath = it->overlayTexturePath;
        newObj.normalMapPath = it->normalMapPath;
        newObj.vertexShaderPath = it->vertexShaderPath;
        newObj.fragmentShaderPath = it->fragmentShaderPath;
        newObj.useOverlay = it->useOverlay;
        newObj.light = it->light;
        newObj.camera = it->camera;
        newObj.postFx = it->postFx;
        newObj.hasRigidbody = it->hasRigidbody;
        newObj.rigidbody = it->rigidbody;
        newObj.hasRigidbody2D = it->hasRigidbody2D;
        newObj.rigidbody2D = it->rigidbody2D;
        newObj.hasCollider2D = it->hasCollider2D;
        newObj.collider2D = it->collider2D;
        newObj.hasParallaxLayer2D = it->hasParallaxLayer2D;
        newObj.parallaxLayer2D = it->parallaxLayer2D;
        newObj.hasCameraFollow2D = it->hasCameraFollow2D;
        newObj.cameraFollow2D = it->cameraFollow2D;
        newObj.hasCollider = it->hasCollider;
        newObj.collider = it->collider;
        newObj.hasPlayerController = it->hasPlayerController;
        newObj.playerController = it->playerController;
        newObj.localPosition = newObj.position;
        newObj.localRotation = NormalizeEulerDegrees(newObj.rotation);
        newObj.localScale = newObj.scale;
        newObj.localInitialized = true;
        newObj.hasAudioSource = it->hasAudioSource;
        newObj.audioSource = it->audioSource;
        newObj.hasReverbZone = it->hasReverbZone;
        newObj.reverbZone = it->reverbZone;
        newObj.hasGroundBakedType = it->hasGroundBakedType;
        newObj.groundBakedType = it->groundBakedType;
        newObj.hasObsticleObject = it->hasObsticleObject;
        newObj.obsticleObject = it->obsticleObject;
        newObj.hasAIAgent = it->hasAIAgent;
        newObj.aiAgent = it->aiAgent;
        newObj.hasAnimation = it->hasAnimation;
        newObj.animation = it->animation;
        newObj.hasSkeletalAnimation = it->hasSkeletalAnimation;
        newObj.skeletal = it->skeletal;
        newObj.ui = it->ui;
        
        sceneObjects.push_back(newObj);
        setPrimarySelection(id);
        if (projectManager.currentProject.isLoaded) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        logToConsole("Duplicated: " + newObj.name);
    }
}

void Engine::deleteSelected() {
    if (selectedObjectId < 0 && selectedObjectIds.empty()) {
        return;
    }

    recordState("delete");

    std::unordered_map<int, SceneObject*> idLookup;
    idLookup.reserve(sceneObjects.size());
    for (auto& obj : sceneObjects) {
        idLookup.emplace(obj.id, &obj);
    }

    std::unordered_set<int> toDelete;
    std::vector<int> stack;
    if (!selectedObjectIds.empty()) {
        for (int id : selectedObjectIds) {
            if (id >= 0 && toDelete.insert(id).second) {
                stack.push_back(id);
            }
        }
    } else if (selectedObjectId >= 0) {
        toDelete.insert(selectedObjectId);
        stack.push_back(selectedObjectId);
    }

    while (!stack.empty()) {
        int currentId = stack.back();
        stack.pop_back();
        auto it = idLookup.find(currentId);
        if (it == idLookup.end() || !it->second) continue;

        for (int childId : it->second->childIds) {
            if (childId >= 0 && toDelete.insert(childId).second) {
                stack.push_back(childId);
            }
        }

        for (const auto& obj : sceneObjects) {
            if (obj.parentId == currentId && toDelete.insert(obj.id).second) {
                stack.push_back(obj.id);
            }
        }
    }

    auto it = std::remove_if(sceneObjects.begin(), sceneObjects.end(),
        [&toDelete](const SceneObject& obj) { return toDelete.count(obj.id) > 0; });

    if (it != sceneObjects.end()) {
        sceneObjects.erase(it, sceneObjects.end());
        for (auto& obj : sceneObjects) {
            if (toDelete.count(obj.parentId) > 0) {
                obj.parentId = -1;
            }
            obj.childIds.erase(std::remove_if(obj.childIds.begin(), obj.childIds.end(),
                [&toDelete](int id) { return toDelete.count(id) > 0; }), obj.childIds.end());
        }
        updateHierarchyWorldTransforms();
        logToConsole("Deleted object");
        clearSelection();
        if (projectManager.currentProject.isLoaded) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
    }
}

void Engine::setParent(int childId, int parentId) {
    recordState("reparent");
    auto childIt = std::find_if(sceneObjects.begin(), sceneObjects.end(),
        [childId](const SceneObject& obj) { return obj.id == childId; });

    if (childIt == sceneObjects.end()) return;

    if (childIt->parentId != -1) {
        auto oldParentIt = std::find_if(sceneObjects.begin(), sceneObjects.end(),
            [&childIt](const SceneObject& obj) { return obj.id == childIt->parentId; });
        if (oldParentIt != sceneObjects.end()) {
            auto& children = oldParentIt->childIds;
            children.erase(std::remove(children.begin(), children.end(), childId), children.end());
        }
    }

    childIt->parentId = parentId;

    if (parentId != -1) {
        auto newParentIt = std::find_if(sceneObjects.begin(), sceneObjects.end(),
            [parentId](const SceneObject& obj) { return obj.id == parentId; });
        if (newParentIt != sceneObjects.end()) {
            newParentIt->childIds.push_back(childId);
        }
    }
    {
        glm::vec3 parentPos(0.0f);
        glm::quat parentRot(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 parentScale(1.0f);
        if (parentId != -1) {
            if (SceneObject* parent = findObjectById(parentId)) {
                parentPos = parent->position;
                parentRot = QuatFromEulerXYZ(parent->rotation);
                parentScale = parent->scale;
            }
        }
        updateLocalFromWorld(*childIt, parentPos, parentRot, parentScale);
    }

    if (projectManager.currentProject.isLoaded) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }
}
#pragma endregion

#pragma region Console Logging
void Engine::addConsoleMessage(const std::string& message, ConsoleMessageType type) {
    if (type == ConsoleMessageType::Error && audio.isReady()) {
        audio.playPreview("Resources/Sounds/Script Error.mp3", 0.95f, false);
    }

    auto now = std::chrono::system_clock::now();
    std::time_t clockTime = std::chrono::system_clock::to_time_t(now);
    char timeStr[16] = "00:00:00";
    std::tm tmNow{};
#if defined(_WIN32)
    localtime_s(&tmNow, &clockTime);
#else
    localtime_r(&clockTime, &tmNow);
#endif
    std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &tmNow);

    ConsoleEntry entry;
    entry.timestamp = timeStr;
    entry.message = message;
    entry.type = type;
    consoleLog.push_back(std::move(entry));

    if (type == ConsoleMessageType::Error) {
        latestErrorMessage = message;
        latestErrorTimestamp = timeStr;
    }

    if (consoleLog.size() > 1000) {
        consoleLog.erase(consoleLog.begin());
    }
}

void Engine::logToConsole(const std::string& message) {
    addConsoleMessage(message, ConsoleMessageType::Info);
}

void Engine::addConsoleMessageFromScript(const std::string& message, ConsoleMessageType type) {
    addConsoleMessage(message, type);
}

bool Engine::isRuntimeKeyDown(int key) const {
    return editorWindow && glfwGetKey(editorWindow, key) == GLFW_PRESS;
}

bool Engine::isRuntimeMouseDown(int button) const {
    return editorWindow && glfwGetMouseButton(editorWindow, button) == GLFW_PRESS;
}

glm::vec2 Engine::getRuntimeMouseDelta() const {
    ImGuiIO& io = ImGui::GetIO();
    glm::vec2 delta(io.MouseDelta.x, io.MouseDelta.y);
    if (glm::dot(delta, delta) > 1e-8f) {
        return delta;
    }
    if (!editorWindow) {
        return glm::vec2(0.0f);
    }

    struct CursorCache {
        int frame = -1;
        bool hasPos = false;
        double lastX = 0.0;
        double lastY = 0.0;
        glm::vec2 delta = glm::vec2(0.0f);
    };
    static std::unordered_map<const Engine*, CursorCache> cacheByEngine;
    CursorCache& cache = cacheByEngine[this];

    int frame = ImGui::GetFrameCount();
    if (cache.frame == frame) {
        return cache.delta;
    }

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(editorWindow, &x, &y);

    glm::vec2 computed(0.0f);
    if (cache.hasPos) {
        computed.x = static_cast<float>(x - cache.lastX);
        computed.y = static_cast<float>(y - cache.lastY);
    }

    cache.lastX = x;
    cache.lastY = y;
    cache.hasPos = true;
    cache.frame = frame;
    cache.delta = computed;
    return computed;
}
#pragma endregion

#pragma region Object Lookup
SceneObject* Engine::findObjectByName(const std::string& name) {
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(), [&](const SceneObject& o) {
        return o.name == name;
    });
    if (it != sceneObjects.end()) return &(*it);
    return nullptr;
}

SceneObject* Engine::findObjectById(int id) {
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(), [&](const SceneObject& o) {
        return o.id == id;
    });
    if (it != sceneObjects.end()) return &(*it);
    return nullptr;
}
#pragma endregion

#pragma region Script Hooks
fs::path Engine::resolveScriptBinary(const fs::path& sourcePath) {
    ScriptBuildConfig config;
    std::string error;
    fs::path cfg = resolveScriptsConfigPath(projectManager.currentProject);
    bool haveConfig = scriptCompiler.loadConfig(cfg, config, error);
    if (haveConfig) {
        auto resolveSource = [&](const fs::path& input) -> fs::path {
            if (input.empty()) return {};
            std::error_code ec;
            fs::path abs = fs::absolute(input, ec);
            if (ec) abs = input;
            if (fs::exists(abs)) return abs;

            fs::path scriptsDir = config.scriptsDir;
            if (!scriptsDir.is_absolute()) {
                scriptsDir = projectManager.currentProject.projectPath / scriptsDir;
            }

            if (input.is_relative()) {
                fs::path candidate = projectManager.currentProject.projectPath / input;
                if (fs::exists(candidate)) return candidate;
            }

            auto remapSuffix = [&](const fs::path& marker) -> fs::path {
                std::vector<fs::path> parts;
                for (const auto& p : abs) parts.push_back(p);
                std::vector<fs::path> markerParts;
                for (const auto& p : marker) markerParts.push_back(p);
                if (markerParts.empty()) return {};
                for (size_t i = 0; i + markerParts.size() <= parts.size(); ++i) {
                    bool match = true;
                    for (size_t k = 0; k < markerParts.size(); ++k) {
                        if (parts[i + k] != markerParts[k]) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        fs::path suffix;
                        for (size_t j = i + markerParts.size(); j < parts.size(); ++j) {
                            suffix /= parts[j];
                        }
                        if (!suffix.empty()) {
                            fs::path candidate = scriptsDir / suffix;
                            if (fs::exists(candidate)) return candidate;
                        }
                        break;
                    }
                }
                return {};
            };

            fs::path remapped = remapSuffix(fs::path("Assets") / "Scripts");
            if (!remapped.empty()) return remapped;
            remapped = remapSuffix("Scripts");
            if (!remapped.empty()) return remapped;

            if (!abs.filename().empty()) {
                fs::path candidate = scriptsDir / abs.filename();
                if (fs::exists(candidate)) return candidate;
            }
            return {};
        };

        fs::path resolvedSource = resolveSource(sourcePath);
        if (!resolvedSource.empty()) {
            ScriptBuildCommands cmds;
            if (scriptCompiler.makeCommands(config, resolvedSource, cmds, error)) {
                return cmds.binaryPath;
            }
        }
    }

    std::vector<fs::path> searchDirs;
    if (haveConfig) {
        fs::path compiledDir = config.outDir;
        if (!compiledDir.is_absolute()) {
            compiledDir = projectManager.currentProject.projectPath / compiledDir;
        }
        searchDirs.push_back(compiledDir);
    }
    searchDirs.push_back(projectManager.currentProject.projectPath / "Cache" / "ScriptBin");
    searchDirs.push_back(projectManager.currentProject.projectPath / "Library" / "CompiledScripts");

    std::unordered_set<std::string> seenDirs;
    std::string stem = sourcePath.stem().string();
    if (!stem.empty()) {
        for (const auto& dir : searchDirs) {
            std::error_code absEc;
            fs::path absDir = fs::absolute(dir, absEc);
            if (absEc) absDir = dir;
            std::string dirKey = absDir.lexically_normal().string();
            if (!seenDirs.insert(dirKey).second) continue;
            if (!fs::exists(absDir)) continue;

            std::error_code dirEc;
            for (auto it = fs::recursive_directory_iterator(absDir, dirEc);
                 it != fs::recursive_directory_iterator(); ++it) {
                if (it->is_directory()) continue;
                fs::path p = it->path();
#ifdef _WIN32
                if (p.stem() == stem && p.extension() == ".dll") return p;
#else
                if (p.stem() == stem && p.extension() == ".so") return p;
#endif
            }
        }
    }

    return {};
}

fs::path Engine::resolveManagedAssembly(const fs::path& sourcePath) {
    if (sourcePath.empty()) return {};
    std::error_code ec;
    std::string ext = sourcePath.extension().string();
    if (ext == ".cs" || ext == ".csproj") {
        fs::path output = getManagedOutputDll();
        if (fs::exists(output)) return output;
    }
    fs::path abs = fs::absolute(sourcePath, ec);
    if (!ec && fs::exists(abs)) return abs;
    fs::path candidate = projectManager.currentProject.projectPath / sourcePath;
    if (fs::exists(candidate)) return candidate;
    return {};
}

fs::path Engine::getManagedProjectPath() const {
    fs::path root;
    if (projectManager.currentProject.isLoaded) {
        root = findManagedProjectRoot(projectManager.currentProject.projectPath);
    }
    if (root.empty()) {
        root = findManagedProjectRoot(fs::current_path());
    }
#if defined(__linux__)
    if (root.empty()) {
        std::error_code ec;
        fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            root = findManagedProjectRoot(exe.parent_path());
        }
    }
#endif
    if (root.empty()) {
        return fs::current_path() / "Scripts" / "Managed" / "ModuCPP.csproj";
    }
    return root / "Scripts" / "Managed" / "ModuCPP.csproj";
}

fs::path Engine::getManagedOutputDll() const {
    return managedOutputPathFromProject(getManagedProjectPath());
}

void Engine::markProjectDirty() {
    projectManager.currentProject.hasUnsavedChanges = true;
}

bool Engine::setRigidbodyVelocityFromScript(int id, const glm::vec3& velocity) {
    return physics.setLinearVelocity(id, velocity);
}

bool Engine::getRigidbodyVelocityFromScript(int id, glm::vec3& outVelocity) {
    return physics.getLinearVelocity(id, outVelocity);
}

bool Engine::setRigidbodyAngularVelocityFromScript(int id, const glm::vec3& velocity) {
    return physics.setAngularVelocity(id, velocity);
}

bool Engine::getRigidbodyAngularVelocityFromScript(int id, glm::vec3& outVelocity) {
    return physics.getAngularVelocity(id, outVelocity);
}

bool Engine::teleportPhysicsActorFromScript(int id, const glm::vec3& position, const glm::vec3& rotationDeg) {
    return physics.setActorPose(id, position, rotationDeg);
}

bool Engine::addRigidbodyForceFromScript(int id, const glm::vec3& force) {
    return physics.addForce(id, force);
}

bool Engine::addRigidbodyImpulseFromScript(int id, const glm::vec3& impulse) {
    return physics.addImpulse(id, impulse);
}

bool Engine::addRigidbodyTorqueFromScript(int id, const glm::vec3& torque) {
    return physics.addTorque(id, torque);
}

bool Engine::addRigidbodyAngularImpulseFromScript(int id, const glm::vec3& impulse) {
    return physics.addAngularImpulse(id, impulse);
}

bool Engine::setRigidbodyYawFromScript(int id, float yawDegrees) {
    return physics.setActorYaw(id, yawDegrees);
}

int Engine::getSelectedObjectId() const {
    return selectedObjectId;
}

bool Engine::raycastClosestFromScript(const glm::vec3& origin, const glm::vec3& dir, float distance,
                                      int ignoreId, glm::vec3* hitPos, glm::vec3* hitNormal,
                                      float* hitDistance, int* hitActorId,
                                      glm::vec3* hitActorVelocity,
                                      float* hitStaticFriction,
                                      float* hitDynamicFriction) const {
    return physics.raycastClosest(origin, dir, distance, ignoreId, hitPos, hitNormal, hitDistance,
                                  hitActorId, hitActorVelocity, hitStaticFriction, hitDynamicFriction);
}

void Engine::syncLocalTransform(SceneObject& obj) {
    if (obj.parentId == -1) {
        obj.localPosition = obj.position;
        obj.localRotation = NormalizeEulerDegrees(obj.rotation);
        obj.localScale = obj.scale;
        obj.localInitialized = true;
        return;
    }
    glm::vec3 parentPos(0.0f);
    glm::quat parentRot(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 parentScale(1.0f);
    if (obj.parentId != -1) {
        if (SceneObject* parent = findObjectById(obj.parentId)) {
            parentPos = parent->position;
            parentRot = QuatFromEulerXYZ(parent->rotation);
            parentScale = parent->scale;
        }
    }
    updateLocalFromWorld(obj, parentPos, parentRot, parentScale);
}

void Engine::setFrameRateCapFromScript(bool enabled, float cap) {
    fpsCapEnabled = enabled;
    fpsCap = std::max(1.0f, cap);
}

bool Engine::playAudioFromScript(int id) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAudioSource) return false;
    return audio.playObjectSound(*obj);
}

bool Engine::stopAudioFromScript(int id) {
    return audio.stopObjectSound(id);
}

bool Engine::setAudioLoopFromScript(int id, bool loop) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAudioSource) return false;
    obj->audioSource.loop = loop;
    markProjectDirty();
    return audio.setObjectLoop(*obj, loop);
}

bool Engine::setAudioVolumeFromScript(int id, float volume) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAudioSource) return false;
    obj->audioSource.volume = std::clamp(volume, 0.0f, 2.0f);
    markProjectDirty();
    return audio.setObjectVolume(*obj, obj->audioSource.volume);
}

bool Engine::setAudioClipFromScript(int id, const std::string& path) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAudioSource) return false;
    obj->audioSource.clipPath = path;
    markProjectDirty();
    // Ensure clip is loaded; do not auto-play unless PlayAudio is called.
    audio.setObjectLoop(*obj, obj->audioSource.loop);
    return true;
}
#pragma endregion

#pragma region Script Compilation + Editor Tabs
void Engine::compileScriptFile(const fs::path& scriptPath) {
    if (!projectManager.currentProject.isLoaded) {
        addConsoleMessage("No project is loaded", ConsoleMessageType::Warning);
        return;
    }

    std::string ext = scriptPath.extension().string();
    if (ext == ".cs" || ext == ".csproj") {
        compileManagedScripts();
        return;
    }

    if (compileInProgress) {
        showCompilePopup = true;
        lastCompileStatus = "Compile already in progress";
        return;
    }
    if (compileWorker.joinable()) {
        compileWorker.join();
    }

    showCompilePopup = true;
    compilePopupHideTime = 0.0;
    lastCompileLog.clear();
    lastCompileStatus = "Compiling " + scriptPath.filename().string();
    lastCompileSuccess = false;
    if (audio.isReady()) {
        audio.playPreview("Resources/Sounds/Notification.mp3", 0.95f, false);
    }
    {
        std::lock_guard<std::mutex> lock(compileMutex);
        compileProgress = 0.05f;
        compileStage = "Preparing";
    }

    fs::path configPath = resolveScriptsConfigPath(projectManager.currentProject);

    compileInProgress = true;
    compileResultReady = false;
    compileWorker = std::thread([this, scriptPath, configPath]() {
        auto setProgress = [this](float value, const char* stage) {
            std::lock_guard<std::mutex> lock(compileMutex);
            compileProgress = value;
            compileStage = stage;
        };
        ScriptCompileJobResult result;
        result.scriptPath = scriptPath;
        std::string error;
        ScriptBuildConfig config;
        if (!scriptCompiler.loadConfig(configPath, config, error)) {
            result.error = error;
        } else {
            packageManager.applyToBuildConfig(config);
            ScriptBuildCommands commands;
            if (!scriptCompiler.makeCommands(config, scriptPath, commands, error)) {
                result.error = error;
            } else {
                setProgress(0.15f, "Compiling");
                ScriptCompileOutput output;
                if (!scriptCompiler.compile(commands, output, error)) {
                    result.compileLog = output.compileLog;
                    result.linkLog = output.linkLog;
                    result.error = error;
                    setProgress(0.9f, "Finalizing");
                } else {
                    result.success = true;
                    result.compileLog = output.compileLog;
                    result.linkLog = output.linkLog;
                    result.binaryPath = commands.binaryPath;
                    result.compiledSource = fs::absolute(scriptPath).lexically_normal().string();
                    setProgress(0.85f, "Reloading");
                }
            }
        }
        std::lock_guard<std::mutex> lock(compileMutex);
        compileResult = std::move(result);
        compileResultReady = true;
        compileInProgress = false;
    });
}

void Engine::compileManagedScripts() {
    if (!projectManager.currentProject.isLoaded) {
        addConsoleMessage("No project is loaded", ConsoleMessageType::Warning);
        return;
    }

    if (compileInProgress) {
        showCompilePopup = true;
        lastCompileStatus = "Compile already in progress";
        return;
    }
    if (compileWorker.joinable()) {
        compileWorker.join();
    }

    fs::path projectRoot = projectManager.currentProject.projectPath;
    fs::path projectManagedProject = projectRoot / "Scripts" / "Managed" / "ModuCPP.csproj";
    if (!fs::exists(projectManagedProject)) {
        std::string setupError;
        fs::path engineRoot = findEngineManagedRoot();
        if (!engineRoot.empty()) {
            ensureProjectManagedCsproj(projectRoot, engineRoot, setupError);
        } else {
            setupError = "Managed project setup failed: engine root not found.";
        }
        if (!setupError.empty()) {
            addConsoleMessage(setupError, ConsoleMessageType::Error);
        }
    }

    fs::path managedProject = fs::exists(projectManagedProject)
        ? projectManagedProject
        : getManagedProjectPath();
    if (!fs::exists(managedProject)) {
        addConsoleMessage("Managed project not found: " + managedProject.string(), ConsoleMessageType::Error);
        return;
    }

    showCompilePopup = true;
    compilePopupHideTime = 0.0;
    lastCompileLog.clear();
    lastCompileStatus = "Compiling managed scripts";
    lastCompileSuccess = false;
    if (audio.isReady()) {
        audio.playPreview("Resources/Sounds/Notification.mp3", 0.95f, false);
    }
    {
        std::lock_guard<std::mutex> lock(compileMutex);
        compileProgress = 0.05f;
        compileStage = "Preparing";
    }

    compileInProgress = true;
    compileResultReady = false;
    compileWorker = std::thread([this, managedProject]() {
        auto setProgress = [this](float value, const char* stage) {
            std::lock_guard<std::mutex> lock(compileMutex);
            compileProgress = value;
            compileStage = stage;
        };

        ScriptCompileJobResult result;
        result.isManaged = true;
        result.scriptPath = managedProject;

        setProgress(0.2f, "Building");

        std::string command = "dotnet build \"" + managedProject.string() + "\" -c Debug 2>&1";
        std::string output;
        int exitCode = -1;

        auto runCommand = [&]() -> bool {
#if defined(_WIN32)
            FILE* pipe = _popen(command.c_str(), "r");
#else
            FILE* pipe = popen(command.c_str(), "r");
#endif
            if (!pipe) return false;
            char buffer[256];
            while (fgets(buffer, sizeof(buffer), pipe)) {
                output += buffer;
            }
#if defined(_WIN32)
            exitCode = _pclose(pipe);
#else
            exitCode = pclose(pipe);
#endif
            return true;
        };

        if (!runCommand()) {
            result.error = "Failed to launch dotnet build";
            result.compileLog = output;
        } else if (exitCode != 0) {
            result.error = "dotnet build failed";
            result.compileLog = output;
        } else {
            result.success = true;
            result.compileLog = output;
            result.binaryPath = managedOutputPathFromProject(managedProject);
            result.compiledSource = managedProject.string();
            setProgress(0.85f, "Reloading");
        }

        std::lock_guard<std::mutex> lock(compileMutex);
        compileResult = std::move(result);
        compileResultReady = true;
        compileInProgress = false;
    });
}

void Engine::updateCompileJob() {
    if (compileResultReady) {
        if (compileWorker.joinable()) {
            compileWorker.join();
        }
        ScriptCompileJobResult result;
        {
            std::lock_guard<std::mutex> lock(compileMutex);
            result = compileResult;
            compileResultReady = false;
        }

        if (!result.success) {
            lastCompileSuccess = false;
            lastCompileStatus = "Compile failed";
            lastCompileLog = result.compileLog + result.linkLog + result.error;
            if (!result.error.empty()) {
                addConsoleMessage("Compile failed: " + result.error, ConsoleMessageType::Error);
            } else {
                addConsoleMessage("Compile failed", ConsoleMessageType::Error);
            }
            if (!result.compileLog.empty()) addConsoleMessage(result.compileLog, ConsoleMessageType::Info);
            if (!result.linkLog.empty()) addConsoleMessage(result.linkLog, ConsoleMessageType::Info);
        } else {
            if (result.isManaged) {
                managedRuntime.unloadAll();
            } else {
                scriptRuntime.unloadAll();
            }

            lastCompileSuccess = true;
            lastCompileStatus = result.isManaged ? "Reloading ModuCPP" : "Reloading ModuCore";
            lastCompileLog = result.compileLog + result.linkLog;
            if (audio.isReady()) {
                audio.playPreview("Resources/Sounds/Success Script.mp3", 0.95f, false);
            }
            addConsoleMessage("Compiled script -> " + result.binaryPath.string(), ConsoleMessageType::Success);
            if (!result.compileLog.empty()) addConsoleMessage(result.compileLog, ConsoleMessageType::Info);
            if (!result.linkLog.empty()) addConsoleMessage(result.linkLog, ConsoleMessageType::Info);

            if (result.isManaged) {
                for (auto& obj : sceneObjects) {
                    for (auto& sc : obj.scripts) {
                        if (sc.language != ScriptLanguage::CSharp) continue;
                        sc.lastBinaryPath = result.binaryPath.string();
                    }
                }
            } else {
                for (auto& obj : sceneObjects) {
                    for (auto& sc : obj.scripts) {
                        std::error_code ec;
                        fs::path scAbs = fs::absolute(sc.path, ec);
                        std::string scPathNorm = (ec ? fs::path(sc.path) : scAbs).lexically_normal().string();
                        if (scPathNorm == result.compiledSource) {
                            sc.lastBinaryPath = result.binaryPath.string();
                        }
                    }
                }
            }

            scriptEditorWindowsDirty = true;
            refreshScriptEditorWindows();
        }

        {
            std::lock_guard<std::mutex> lock(compileMutex);
            compileProgress = 1.0f;
            compileStage = lastCompileSuccess ? "Done" : "Failed";
        }
        compilePopupHideTime = glfwGetTime() + 1.0;
        showCompilePopup = true;
    }

    if (!compileInProgress && showCompilePopup && compilePopupHideTime > 0.0 &&
        glfwGetTime() >= compilePopupHideTime) {
        showCompilePopup = false;
        compilePopupOpened = false;
        compilePopupHideTime = 0.0;
    }

    if (!compileInProgress && managedAutoCompileQueued) {
        managedAutoCompileQueued = false;
        compileManagedScripts();
    }
}

void Engine::refreshScriptEditorWindows() {
    if (!scriptEditorWindowsDirty) return;
    scriptEditorWindowsDirty = false;

    if (!projectManager.currentProject.isLoaded) {
        scriptEditorWindows.clear();
        return;
    }

    std::unordered_map<std::string, bool> previousState;
    for (const auto& entry : scriptEditorWindows) {
        previousState[entry.binaryPath.lexically_normal().string()] = entry.open;
    }

    std::unordered_set<std::string> seen;
    std::vector<ScriptEditorWindowEntry> updated;

    auto tryAddEntry = [&](const fs::path& binaryPath) {
        if (binaryPath.empty() || !fs::exists(binaryPath)) return;
        std::string key = binaryPath.lexically_normal().string();
        if (!seen.insert(key).second) return;
        if (!scriptRuntime.hasEditorWindow(binaryPath)) return;

        ScriptEditorWindowEntry entry;
        entry.binaryPath = binaryPath;
        entry.label = binaryPath.stem().string();
        if (entry.label.empty()) entry.label = "ScriptWindow";
        auto it = previousState.find(key);
        entry.open = (it != previousState.end()) ? it->second : false;
        updated.push_back(std::move(entry));
    };

    for (const auto& obj : sceneObjects) {
        for (const auto& sc : obj.scripts) {
            fs::path binaryPath;
            if (!sc.lastBinaryPath.empty()) {
                binaryPath = fs::path(sc.lastBinaryPath);
            }
            if (binaryPath.empty() || !fs::exists(binaryPath)) {
                binaryPath = resolveScriptBinary(sc.path);
            }
            tryAddEntry(binaryPath);
        }
    }

    // Also scan the configured script output directory for standalone editor tabs.
    fs::path configPath = resolveScriptsConfigPath(projectManager.currentProject);
    ScriptBuildConfig config;
    std::string error;
    if (scriptCompiler.loadConfig(configPath, config, error)) {
        fs::path outDir = config.outDir;
        if (!outDir.is_absolute()) {
            outDir = projectManager.currentProject.projectPath / outDir;
        }
        std::error_code ec;
        if (fs::exists(outDir, ec)) {
            for (auto it = fs::recursive_directory_iterator(outDir, ec);
                 it != fs::recursive_directory_iterator(); ++it) {
                if (it->is_directory()) continue;
                auto ext = it->path().extension().string();
                if (ext == ".so" || ext == ".dll" || ext == ".dylib") {
                    tryAddEntry(it->path());
                }
            }
        }
    }

    scriptEditorWindows.swap(updated);
}

void Engine::renderScriptEditorWindows() {
    if (scriptEditorWindows.empty()) return;

    ScriptContext ctx;
    ctx.engine = this;
    ctx.object = getSelectedObject();
    ctx.script = nullptr;

    ImGuiID scriptDockId = 0;
    const char* dockAnchors[] = { "Scripting", "Inspector", "Project", "Viewport" };
    for (const char* anchorName : dockAnchors) {
        ImGuiWindow* anchorWindow = ImGui::FindWindowByName(anchorName);
        if (!anchorWindow || anchorWindow->DockId == 0) continue;
        if (ImGui::DockBuilderGetNode(anchorWindow->DockId) == nullptr) continue;
        scriptDockId = anchorWindow->DockId;
        break;
    }

    for (auto& entry : scriptEditorWindows) {
        if (!entry.open) continue;

        if (scriptDockId != 0) {
            ImGui::SetNextWindowDockID(scriptDockId, ImGuiCond_FirstUseEver);
        }
        std::string title = entry.label + "###" + entry.binaryPath.string();
        if (ImGui::Begin(title.c_str(), &entry.open)) {
            scriptRuntime.callEditorWindow(entry.binaryPath, ctx);
        }
        ImGui::End();

        if (!entry.open) {
            scriptRuntime.callExitEditorWindow(entry.binaryPath, ctx);
        }
    }
}
#pragma endregion

#pragma region ImGui Setup
void Engine::setupImGui() {
    std::cerr << "[DEBUG] setupImGui: getting primary monitor..." << std::endl;
    float mainScale = 1.0f;
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    if (primaryMonitor) {
        std::cerr << "[DEBUG] setupImGui: got primary monitor, getting content scale..." << std::endl;
        mainScale = ImGui_ImplGlfw_GetContentScaleForMonitor(primaryMonitor);
        std::cerr << "[DEBUG] setupImGui: content scale = " << mainScale << std::endl;
    } else {
        std::cerr << "[DEBUG] setupImGui: WARNING - no primary monitor found!" << std::endl;
    }
    
    std::cerr << "[DEBUG] setupImGui: creating ImGui context..." << std::endl;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    #ifndef __linux__
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    #endif
    io.IniFilename = nullptr;

    std::cerr << "[DEBUG] setupImGui: applying theme..." << std::endl;
    applyModernTheme();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(mainScale);
    style.FontScaleDpi = mainScale;

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
    initUIStylePresets();

    std::cerr << "[DEBUG] setupImGui: initializing ImGui GLFW backend..." << std::endl;
    ImGui_ImplGlfw_InitForOpenGL(editorWindow, true);
    
    std::cerr << "[DEBUG] setupImGui: initializing ImGui OpenGL3 backend..." << std::endl;
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        std::cerr << "[DEBUG] ImGui OpenGL3 init failed!" << std::endl;
        throw std::runtime_error("ImGui error");
    }
    std::cerr << "[DEBUG] setupImGui: complete!" << std::endl;
}
#pragma endregion

void Engine::initUIStylePresets() {
    uiStylePresets.clear();
    uiStylePresets.shrink_to_fit();

    UIStylePreset current;
    current.name = "Default";
    current.style = ImGui::GetStyle();
    current.builtin = true;
    uiStylePresets.push_back(current);

    UIStylePreset imguiDefault;
    imguiDefault.name = "Imgui Default";
    imguiDefault.style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&imguiDefault.style);
    applyEditorLayoutPreset(imguiDefault.style);
    imguiDefault.builtin = true;
    uiStylePresets.push_back(imguiDefault);

    UIStylePreset pixel;
    pixel.name = "Pixel";
    pixel.style = ImGui::GetStyle();
    applyPixelStyle(pixel.style);
    pixel.builtin = true;
    uiStylePresets.push_back(pixel);

    UIStylePreset superRound;
    superRound.name = "Super Round";
    superRound.style = ImGui::GetStyle();
    applySuperRoundStyle(superRound.style);
    superRound.builtin = true;
    uiStylePresets.push_back(superRound);

    uiStylePresetIndex = findUIStylePreset(uiStylePresetName);
    if (uiStylePresetIndex < 0) {
        uiStylePresetIndex = 0;
        uiStylePresetName = uiStylePresets[0].name;
    }
}

int Engine::findUIStylePreset(const std::string& name) const {
    for (size_t i = 0; i < uiStylePresets.size(); ++i) {
        if (uiStylePresets[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

const Engine::UIStylePreset* Engine::getUIStylePreset(const std::string& name) const {
    int idx = findUIStylePreset(name);
    if (idx < 0) return nullptr;
    return &uiStylePresets[idx];
}

void Engine::registerUIStylePreset(const std::string& name, const ImGuiStyle& style, bool replace) {
    if (name.empty()) return;
    int idx = findUIStylePreset(name);
    if (idx >= 0) {
        if (replace) {
            uiStylePresets[idx].style = style;
        }
        return;
    }
    UIStylePreset preset;
    preset.name = name;
    preset.style = style;
    preset.builtin = false;
    uiStylePresets.push_back(preset);
}

void Engine::registerUIStylePresetFromScript(const std::string& name, const ImGuiStyle& style, bool replace) {
    registerUIStylePreset(name, style, replace);
}

bool Engine::applyUIStylePresetByName(const std::string& name) {
    int idx = findUIStylePreset(name);
    if (idx < 0) {
        return false;
    }
    ImVec4 preservedColors[ImGuiCol_COUNT];
    ImGuiStyle& currentStyle = ImGui::GetStyle();
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        preservedColors[i] = currentStyle.Colors[i];
    }
    uiStylePresetIndex = idx;
    uiStylePresetName = uiStylePresets[idx].name;
    currentStyle = uiStylePresets[idx].style;
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        currentStyle.Colors[i] = preservedColors[i];
    }
    return true;
}

fs::path Engine::getEditorUserSettingsPath() const {
    if (!projectManager.currentProject.isLoaded) {
        return fs::path();
    }
    fs::path settingsDir = projectManager.currentProject.projectPath / "ProjectUserSettings";
    return settingsDir / "EditorUI.ini";
}

fs::path Engine::getEditorLayoutPath() const {
    return getWorkspaceLayoutPath(WorkspaceMode::Default);
}

fs::path Engine::getWorkspaceLayoutPath(WorkspaceMode mode) const {
    const char* filename = "imgui.ini";
    if (mode == WorkspaceMode::Animation) {
        filename = "anim.ini";
    } else if (mode == WorkspaceMode::Scripting) {
        filename = "scripter.ini";
    }

    if (projectManager.currentProject.isLoaded) {
        fs::path settingsDir =
            projectManager.currentProject.projectPath / "ProjectUserSettings" / "ProjectLayout";
        return settingsDir / filename;
    }

    return fs::path("Resources") / filename;
}

void Engine::saveWorkspaceLayout(WorkspaceMode mode) const {
    if (!ImGui::GetCurrentContext()) {
        return;
    }

    fs::path layoutPath = getWorkspaceLayoutPath(mode);
    if (layoutPath.empty()) {
        return;
    }

    std::error_code ec;
    fs::create_directories(layoutPath.parent_path(), ec);
    ImGui::SaveIniSettingsToDisk(layoutPath.string().c_str());
}

void Engine::autosaveWorkspaceLayout() {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context || showLauncher || playerMode) {
        return;
    }

    if (context->SettingsDirtyTimer > 0.0f) {
        workspaceLayoutSavePending = true;
        return;
    }

    if (workspaceLayoutSavePending) {
        saveWorkspaceLayout(currentWorkspace);
        workspaceLayoutSavePending = false;
    }
}

void Engine::loadEditorUserSettings() {
    if (!projectManager.currentProject.isLoaded) {
        return;
    }
    fs::path settingsPath = getEditorUserSettingsPath();
    if (settingsPath.empty() || !fs::exists(settingsPath)) {
        return;
    }

    auto trim = [](std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        size_t end = s.find_last_not_of(" \t\r\n");
        if (start == std::string::npos || end == std::string::npos) {
            s.clear();
            return;
        }
        s = s.substr(start, end - start + 1);
    };

    fileBrowserFavorites.clear();
    std::vector<ImVec4> loadedColors(ImGuiCol_COUNT);
    std::vector<bool> hasColor(ImGuiCol_COUNT, false);
    static std::unordered_map<std::string, int> colorIndex;
    if (colorIndex.empty()) {
        for (int i = 0; i < ImGuiCol_COUNT; ++i) {
            colorIndex.emplace(ImGui::GetStyleColorName(i), i);
        }
    }

    std::ifstream file(settingsPath);
    std::string line;
    while (std::getline(file, line)) {
        trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        trim(key);
        trim(value);
        if (key == "uiStyle") {
            uiStylePresetName = value;
        } else if (key == "uiAnimationMode") {
            if (value == "Fluid") {
                uiAnimationMode = UIAnimationMode::Fluid;
            } else if (value == "Snappy") {
                uiAnimationMode = UIAnimationMode::Snappy;
            } else {
                uiAnimationMode = UIAnimationMode::Off;
            }
        } else if (key == "workspace") {
            if (value == "Animation") {
                currentWorkspace = WorkspaceMode::Animation;
            } else if (value == "Scripting") {
                currentWorkspace = WorkspaceMode::Scripting;
            } else {
                currentWorkspace = WorkspaceMode::Default;
            }
        } else if (key == "fileBrowserIconScale") {
            try {
                fileBrowserIconScale = std::stof(value);
            } catch (...) {
            }
        } else if (key == "fileBrowserViewMode") {
            if (value == "List") {
                fileBrowser.viewMode = FileBrowserViewMode::List;
            } else {
                fileBrowser.viewMode = FileBrowserViewMode::Grid;
            }
        } else if (key == "fileBrowserSidebarWidth") {
            try {
                fileBrowserSidebarWidth = std::stof(value);
            } catch (...) {
            }
        } else if (key == "fileBrowserSidebarVisible") {
            showFileBrowserSidebar = (value == "1" || value == "true" || value == "yes");
        } else if (key == "consoleWrapText") {
            consoleWrapText = (value == "1" || value == "true" || value == "yes");
        } else if (key == "showAnimationWindow") {
            showAnimationWindow = (value == "1" || value == "true" || value == "yes");
        } else if (key == "showAIPathfindingWindow") {
            showAIPathfindingWindow = (value == "1" || value == "true" || value == "yes");
        } else if (key == "showSceneGizmos") {
            showSceneGizmos = (value == "1" || value == "true" || value == "yes");
        } else if (key == "showSceneGrid3D") {
            showSceneGrid3D = (value == "1" || value == "true" || value == "yes");
        } else if (key == "showCanvasOverlay") {
            showCanvasOverlay = (value == "1" || value == "true" || value == "yes");
        } else if (key == "showUIWorldGrid") {
            showUIWorldGrid = (value == "1" || value == "true" || value == "yes");
        } else if (key == "showGameProfiler") {
            showGameProfiler = (value == "1" || value == "true" || value == "yes");
        } else if (key == "collisionWireframe") {
            collisionWireframe = (value == "1" || value == "true" || value == "yes");
        } else if (key == "fpsCapEnabled") {
            fpsCapEnabled = (value == "1" || value == "true" || value == "yes");
        } else if (key == "fpsCap") {
            try { fpsCap = std::max(1.0f, std::stof(value)); } catch (...) {}
        } else if (key == "cameraMoveSpeed") {
            try { camera.moveSpeed = std::max(0.01f, std::stof(value)); } catch (...) {}
        } else if (key == "cameraSprintSpeed") {
            try { camera.sprintSpeed = std::max(camera.moveSpeed, std::stof(value)); } catch (...) {}
        } else if (key == "cameraSmoothMovement") {
            camera.smoothMovement = (value == "1" || value == "true" || value == "yes");
        } else if (key == "cameraAcceleration") {
            try { camera.acceleration = std::max(0.1f, std::stof(value)); } catch (...) {}
        } else if (key == "cameraMouseSensitivity") {
            try { camera.mouseSensitivity = std::max(0.001f, std::stof(value)); } catch (...) {}
        } else if (key == "gameViewportResolutionIndex") {
            try { gameViewportResolutionIndex = std::max(0, std::stoi(value)); } catch (...) {}
        } else if (key == "gameViewportCustomWidth") {
            try { gameViewportCustomWidth = std::clamp(std::stoi(value), 64, 8192); } catch (...) {}
        } else if (key == "gameViewportCustomHeight") {
            try { gameViewportCustomHeight = std::clamp(std::stoi(value), 64, 8192); } catch (...) {}
        } else if (key == "gameViewportAutoFit") {
            gameViewportAutoFit = (value == "1" || value == "true" || value == "yes");
        } else if (key == "gameViewportZoom") {
            try { gameViewportZoom = std::clamp(std::stof(value), 0.2f, 4.0f); } catch (...) {}
        } else if (key == "scriptAutoCompileInterval") {
            try { scriptAutoCompileInterval = std::clamp(std::stod(value), 0.1, 10.0); } catch (...) {}
        } else if (key == "scriptAutoCompileOnSave") {
            scriptEditorState.autoCompileOnSave = (value == "1" || value == "true" || value == "yes");
        } else if (key.rfind("color.", 0) == 0) {
            std::string name = key.substr(6);
            auto it = colorIndex.find(name);
            if (it != colorIndex.end()) {
                std::string parseValue = value;
                std::replace(parseValue.begin(), parseValue.end(), ',', ' ');
                std::stringstream ss(parseValue);
                float r = 0.0f;
                float g = 0.0f;
                float b = 0.0f;
                float a = 1.0f;
                if (ss >> r >> g >> b >> a) {
                    loadedColors[it->second] = ImVec4(r, g, b, a);
                    hasColor[it->second] = true;
                }
            }
        } else if (key == "favorite") {
            if (value.empty()) {
                continue;
            }
            fs::path favPath = fs::path(value);
            fs::path baseRoot = fileBrowser.projectRoot.empty()
                ? projectManager.currentProject.projectPath
                : fileBrowser.projectRoot;
            if (favPath.is_relative()) {
                favPath = baseRoot / favPath;
            }
            std::error_code ec;
            fs::path canonical = fs::weakly_canonical(favPath, ec);
            if (!ec) {
                favPath = canonical;
            }
            fileBrowserFavorites.push_back(favPath);
        }
    }

    fileBrowserIconScale = std::clamp(fileBrowserIconScale, 0.6f, 2.0f);
    fileBrowserSidebarWidth = std::clamp(fileBrowserSidebarWidth, 160.0f, 360.0f);
    camera.moveSpeed = std::max(0.01f, camera.moveSpeed);
    camera.sprintSpeed = std::max(camera.moveSpeed, camera.sprintSpeed);
    camera.acceleration = std::max(0.1f, camera.acceleration);
    camera.mouseSensitivity = std::clamp(camera.mouseSensitivity, 0.001f, 2.0f);
    fpsCap = std::max(1.0f, fpsCap);
    gameViewportCustomWidth = std::clamp(gameViewportCustomWidth, 64, 8192);
    gameViewportCustomHeight = std::clamp(gameViewportCustomHeight, 64, 8192);
    gameViewportZoom = std::clamp(gameViewportZoom, 0.2f, 4.0f);
    scriptAutoCompileInterval = std::clamp(scriptAutoCompileInterval, 0.1, 10.0);

    applyUIStylePresetByName(uiStylePresetName);
    ImGuiStyle& style = ImGui::GetStyle();
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        if (hasColor[i]) {
            style.Colors[i] = loadedColors[i];
        }
    }
    style.Colors[ImGuiCol_Button] = ImVec4(0.22f, 0.23f, 0.32f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.30f, 0.42f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.33f, 0.36f, 0.48f, 1.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.21f, 0.30f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.28f, 0.40f, 1.00f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.34f, 0.46f, 1.00f);

    applyWorkspacePreset(currentWorkspace, false);
    scriptingFilesDirty = true;
}

void Engine::saveEditorUserSettings() const {
    if (!projectManager.currentProject.isLoaded) {
        return;
    }
    fs::path settingsPath = getEditorUserSettingsPath();
    if (settingsPath.empty()) {
        return;
    }
    fs::create_directories(settingsPath.parent_path());

    std::ofstream file(settingsPath);
    if (!file.is_open()) {
        return;
    }

    file << "# Editor UI settings\n";
    file << std::fixed << std::setprecision(4);
    file << "uiStyle=" << uiStylePresetName << "\n";
    const char* animMode = "Off";
    if (uiAnimationMode == UIAnimationMode::Fluid) {
        animMode = "Fluid";
    } else if (uiAnimationMode == UIAnimationMode::Snappy) {
        animMode = "Snappy";
    }
    file << "uiAnimationMode=" << animMode << "\n";
    const char* workspaceName = "Default";
    if (currentWorkspace == WorkspaceMode::Animation) {
        workspaceName = "Animation";
    } else if (currentWorkspace == WorkspaceMode::Scripting) {
        workspaceName = "Scripting";
    }
    file << "workspace=" << workspaceName << "\n";
    file << "fileBrowserIconScale=" << fileBrowserIconScale << "\n";
    file << "fileBrowserViewMode=" << (fileBrowser.viewMode == FileBrowserViewMode::List ? "List" : "Grid") << "\n";
    file << "fileBrowserSidebarWidth=" << fileBrowserSidebarWidth << "\n";
    file << "fileBrowserSidebarVisible=" << (showFileBrowserSidebar ? "1" : "0") << "\n";
    file << "consoleWrapText=" << (consoleWrapText ? "1" : "0") << "\n";
    file << "showAnimationWindow=" << (showAnimationWindow ? "1" : "0") << "\n";
    file << "showAIPathfindingWindow=" << (showAIPathfindingWindow ? "1" : "0") << "\n";
    file << "showSceneGizmos=" << (showSceneGizmos ? "1" : "0") << "\n";
    file << "showSceneGrid3D=" << (showSceneGrid3D ? "1" : "0") << "\n";
    file << "showCanvasOverlay=" << (showCanvasOverlay ? "1" : "0") << "\n";
    file << "showUIWorldGrid=" << (showUIWorldGrid ? "1" : "0") << "\n";
    file << "showGameProfiler=" << (showGameProfiler ? "1" : "0") << "\n";
    file << "collisionWireframe=" << (collisionWireframe ? "1" : "0") << "\n";
    file << "fpsCapEnabled=" << (fpsCapEnabled ? "1" : "0") << "\n";
    file << "fpsCap=" << fpsCap << "\n";
    file << "cameraMoveSpeed=" << camera.moveSpeed << "\n";
    file << "cameraSprintSpeed=" << camera.sprintSpeed << "\n";
    file << "cameraSmoothMovement=" << (camera.smoothMovement ? "1" : "0") << "\n";
    file << "cameraAcceleration=" << camera.acceleration << "\n";
    file << "cameraMouseSensitivity=" << camera.mouseSensitivity << "\n";
    file << "gameViewportResolutionIndex=" << gameViewportResolutionIndex << "\n";
    file << "gameViewportCustomWidth=" << gameViewportCustomWidth << "\n";
    file << "gameViewportCustomHeight=" << gameViewportCustomHeight << "\n";
    file << "gameViewportAutoFit=" << (gameViewportAutoFit ? "1" : "0") << "\n";
    file << "gameViewportZoom=" << gameViewportZoom << "\n";
    file << "scriptAutoCompileInterval=" << scriptAutoCompileInterval << "\n";
    file << "scriptAutoCompileOnSave=" << (scriptEditorState.autoCompileOnSave ? "1" : "0") << "\n";
    const ImGuiStyle& style = ImGui::GetStyle();
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        const ImVec4& c = style.Colors[i];
        file << "color." << ImGui::GetStyleColorName(i) << "="
             << c.x << "," << c.y << "," << c.z << "," << c.w << "\n";
    }

    fs::path baseRoot = fileBrowser.projectRoot.empty()
        ? projectManager.currentProject.projectPath
        : fileBrowser.projectRoot;
    for (const auto& fav : fileBrowserFavorites) {
        fs::path stored = fav;
        std::error_code ec;
        fs::path rel = fs::relative(fav, baseRoot, ec);
        std::string relStr = rel.generic_string();
        if (!ec && !rel.empty() && relStr.find("..") != 0) {
            stored = rel;
        }
        file << "favorite=" << stored.generic_string() << "\n";
    }
}

void Engine::exportEditorThemeLayout() {
    if (!projectManager.currentProject.isLoaded) {
        addConsoleMessage("No project loaded to export UI settings", ConsoleMessageType::Warning);
        return;
    }
    saveEditorUserSettings();
    fs::path layoutPath = getWorkspaceLayoutPath(currentWorkspace);
    if (layoutPath.empty()) {
        addConsoleMessage("Failed to resolve layout export path", ConsoleMessageType::Error);
        return;
    }
    fs::create_directories(layoutPath.parent_path());
    ImGui::SaveIniSettingsToDisk(layoutPath.string().c_str());
    addConsoleMessage("Exported UI layout to: " + layoutPath.string(), ConsoleMessageType::Success);
}
