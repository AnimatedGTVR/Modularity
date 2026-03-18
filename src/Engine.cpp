#include "Engine.h"
#include "CrashReporter.h"
#include "ModelLoader.h"
#include "RuntimeContent.h"
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
#include <sstream>
#include "ThirdParty/glm/gtc/constants.hpp"
#include "ThirdParty/glfw/deps/stb_image_write.h"

#pragma region Material File IO Helpers
namespace {
constexpr int kRuntimeInternalWidth = 1280;
constexpr int kRuntimeInternalHeight = 720;

struct MaterialFileData {
    MaterialProperties props;
    std::string albedo;
    std::string overlay;
    std::string normal;
    bool useOverlay = false;
    std::string vertexShader;
    std::string fragmentShader;
};

std::string RenameTrim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() &&
           std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    size_t end = value.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(start, end - start);
}

bool RenameIsInteger(const std::string& value) {
    std::string trimmed = RenameTrim(value);
    if (trimmed.empty()) return false;
    size_t start = (trimmed[0] == '-' || trimmed[0] == '+') ? 1 : 0;
    if (start >= trimmed.size()) return false;
    for (size_t i = start; i < trimmed.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(trimmed[i]))) {
            return false;
        }
    }
    return true;
}

void RenameReplaceTrimmed(std::string& target, const std::string& replacement) {
    size_t start = 0;
    while (start < target.size() &&
           std::isspace(static_cast<unsigned char>(target[start])) != 0) {
        ++start;
    }
    size_t end = target.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(target[end - 1])) != 0) {
        --end;
    }
    target = target.substr(0, start) + replacement + target.substr(end);
}

std::vector<std::string> RenameSplitEscaped(const std::string& value, char delimiter) {
    std::vector<std::string> fields;
    std::string current;
    bool escaped = false;
    for (char c : value) {
        if (escaped) {
            if (c == 'n') current.push_back('\n');
            else if (c == 'r') current.push_back('\r');
            else if (c == 't') current.push_back('\t');
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

std::string RenameEscapeField(const std::string& value, char delimiter) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        if (c == '\\' || c == delimiter || c == '\n' || c == '\r' || c == '\t') {
            out.push_back('\\');
            if (c == '\n') out.push_back('n');
            else if (c == '\r') out.push_back('r');
            else if (c == '\t') out.push_back('t');
            else out.push_back(c);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string RenameJoinEscaped(const std::vector<std::string>& values, char delimiter) {
    std::string joined;
    for (size_t i = 0; i < values.size(); ++i) {
        joined += RenameEscapeField(values[i], delimiter);
        if (i + 1 < values.size()) joined.push_back(delimiter);
    }
    return joined;
}

bool RenameRewriteSingleObjectRef(std::string& value,
                                  const std::string& oldName,
                                  const std::string& newName) {
    std::string trimmed = RenameTrim(value);
    if (trimmed.empty()) return false;

    if (trimmed == oldName) {
        RenameReplaceTrimmed(value, newName);
        return true;
    }

    const std::string namePrefix = "Object.";
    if (trimmed.rfind(namePrefix, 0) == 0) {
        const std::string suffix = trimmed.substr(namePrefix.size());
        if (suffix == oldName) {
            RenameReplaceTrimmed(value, namePrefix + newName);
            return true;
        }
    }

    return false;
}

bool RenameRewriteManagedObjectSetting(std::string& value,
                                       const std::string& oldName,
                                       const std::string& newName) {
    const size_t firstBar = value.find('|');
    if (firstBar == std::string::npos) return false;
    if (value.find('|', firstBar + 1) != std::string::npos) return false;

    std::string idPart = value.substr(0, firstBar);
    if (!RenameIsInteger(idPart)) return false;

    std::string namePart = value.substr(firstBar + 1);
    std::string trimmedName = RenameTrim(namePart);
    if (trimmedName != oldName) return false;

    RenameReplaceTrimmed(namePart, newName);
    value = idPart + "|" + namePart;
    return true;
}

bool RenameRewriteObjectRefList(std::string& value,
                                const std::string& oldName,
                                const std::string& newName) {
    if (value.find(';') == std::string::npos) return false;

    std::vector<std::string> refs = RenameSplitEscaped(value, ';');
    bool changed = false;
    for (std::string& ref : refs) {
        changed |= RenameRewriteSingleObjectRef(ref, oldName, newName);
    }
    if (!changed) return false;

    value = RenameJoinEscaped(refs, ';');
    return true;
}

bool RenameRewriteDialogueLines(std::string& value,
                                const std::string& oldName,
                                const std::string& newName) {
    if (value.find('|') == std::string::npos) return false;

    char outerDelimiter = '\t';
    if (value.find('\t') == std::string::npos && value.find('\n') != std::string::npos) {
        outerDelimiter = '\n';
    }

    std::vector<std::string> lines = RenameSplitEscaped(value, outerDelimiter);
    bool changed = false;
    for (std::string& line : lines) {
        if (RenameTrim(line).empty()) continue;

        std::vector<std::string> fields = RenameSplitEscaped(line, '|');
        if (fields.size() < 14) continue;

        bool lineChanged = false;
        lineChanged |= RenameRewriteSingleObjectRef(fields[9], oldName, newName);
        lineChanged |= RenameRewriteSingleObjectRef(fields[10], oldName, newName);
        lineChanged |= RenameRewriteSingleObjectRef(fields[11], oldName, newName);
        lineChanged |= RenameRewriteObjectRefList(fields[12], oldName, newName);
        lineChanged |= RenameRewriteObjectRefList(fields[13], oldName, newName);
        if (lineChanged) {
            line = RenameJoinEscaped(fields, '|');
            changed = true;
        }
    }

    if (!changed) return false;
    value = RenameJoinEscaped(lines, outerDelimiter);
    return true;
}

bool RenameRewriteInteractableOptions(std::string& value,
                                      const std::string& oldName,
                                      const std::string& newName) {
    if (value.find('|') == std::string::npos) return false;

    char outerDelimiter = '\t';
    if (value.find('\t') == std::string::npos && value.find('\n') != std::string::npos) {
        outerDelimiter = '\n';
    }

    std::vector<std::string> options = RenameSplitEscaped(value, outerDelimiter);
    bool changed = false;
    for (std::string& option : options) {
        if (RenameTrim(option).empty()) continue;

        std::vector<std::string> fields = RenameSplitEscaped(option, '|');
        if (fields.size() < 8) continue;

        bool optionChanged = false;
        optionChanged |= RenameRewriteSingleObjectRef(fields[2], oldName, newName);
        optionChanged |= RenameRewriteDialogueLines(fields[3], oldName, newName);
        optionChanged |= RenameRewriteObjectRefList(fields[4], oldName, newName);
        optionChanged |= RenameRewriteObjectRefList(fields[5], oldName, newName);
        optionChanged |= RenameRewriteObjectRefList(fields[6], oldName, newName);
        optionChanged |= RenameRewriteObjectRefList(fields[7], oldName, newName);
        if (optionChanged) {
            option = RenameJoinEscaped(fields, '|');
            changed = true;
        }
    }

    if (!changed) return false;
    value = RenameJoinEscaped(options, outerDelimiter);
    return true;
}

bool RenameRewriteScriptSettingValue(std::string& value,
                                     const std::string& oldName,
                                     const std::string& newName) {
    if (value.empty()) return false;

    bool changed = false;
    changed |= RenameRewriteManagedObjectSetting(value, oldName, newName);
    changed |= RenameRewriteSingleObjectRef(value, oldName, newName);
    changed |= RenameRewriteObjectRefList(value, oldName, newName);
    changed |= RenameRewriteDialogueLines(value, oldName, newName);
    changed |= RenameRewriteInteractableOptions(value, oldName, newName);
    return changed;
}

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

float normalizeTimeOfDay(float timeOfDay) {
    if (!std::isfinite(timeOfDay)) {
        return 0.5f;
    }
    float wrapped = std::fmod(timeOfDay, 1.0f);
    if (wrapped < 0.0f) {
        wrapped += 1.0f;
    }
    return wrapped;
}

std::string shaderFilename(const std::string& shaderPath) {
    if (shaderPath.empty()) {
        return std::string();
    }
    return fs::path(shaderPath).filename().string();
}

bool isSupportedVulkanPreviewShaderPair(const SceneObject& obj) {
    const std::string vert = shaderFilename(obj.vertexShaderPath);
    const std::string frag = shaderFilename(obj.fragmentShaderPath);

    const bool vertSupported = vert.empty() ||
                               vert == "vert.glsl" ||
                               vert == "scroll_texture_vert.glsl";
    const bool fragSupported = frag.empty() ||
                               frag == "frag.glsl" ||
                               frag == "scroll_texture_frag.glsl";
    return vertSupported && fragSupported;
}

bool hasUnsupportedVulkanPreviewFeature(const SceneObject& obj) {
    if (!IsObjectEnabledInHierarchy(obj) || !obj.hasRenderer || obj.renderType == RenderType::None) {
        return false;
    }

    const bool supportedGeometry = obj.renderType == RenderType::Cube ||
                                   obj.renderType == RenderType::Plane ||
                                   obj.renderType == RenderType::Mirror;
    if (!supportedGeometry) {
        return true;
    }

    if (!isSupportedVulkanPreviewShaderPair(obj)) {
        return true;
    }

    return false;
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
        } else if (key == "opacity" || key == "alpha") {
            outData.props.alpha = std::clamp(std::stof(val), 0.0f, 1.0f);
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
    f << "opacity=" << data.props.alpha << "\n";
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
    obj.hasLight2D = false;
    obj.hasCamera = false;
    obj.hasPostFX = false;
    obj.hasUI = false;
    obj.hasShadowCaster2D = false;
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
        case ObjectType::Sprite25D:
            obj.hasUI = true;
            obj.ui.type = UIElementType::Sprite2D;
            obj.ui.label = "2.5D Object";
            obj.ui.size = glm::vec2(128.0f, 128.0f);
            obj.scale = glm::vec3(1.0f);
            break;
        case ObjectType::Light2D:
            obj.hasLight2D = true;
            obj.light2D.type = Light2DType::Point;
            obj.light2D.outerRadius = 5.0f;
            obj.light2D.radius = 5.0f;
            obj.light2D.intensity = 1.2f;
            break;
        case ObjectType::ShadowCaster2D:
            obj.hasShadowCaster2D = true;
            obj.shadowCaster2D.points = {
                glm::vec2(-0.5f, -0.5f),
                glm::vec2(0.5f, -0.5f),
                glm::vec2(0.5f, 0.5f),
                glm::vec2(-0.5f, 0.5f)
            };
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
            obj.scale = glm::vec3(6.0f);
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
} // namespace

void ModuRuntime2DRenderCounters_Reset();
void ModuRuntime2DRenderCounters_Read(uint64_t* outTextureBindCount,
                                      uint64_t* outStateBindCount);

namespace {
using Runtime2DClock = std::chrono::steady_clock;

double Runtime2DMsSince(const Runtime2DClock::time_point& start,
                        const Runtime2DClock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

struct Runtime2DImGuiDrawCounters {
    uint64_t drawCalls = 0;
    uint64_t textureBinds = 0;
    uint64_t stateBinds = 0;
};

Runtime2DImGuiDrawCounters CollectImGuiDrawCounters(const ImDrawData* drawData) {
    Runtime2DImGuiDrawCounters counters;
    if (!drawData || drawData->CmdListsCount <= 0) {
        return counters;
    }

    ImTextureID lastTexture = ImTextureID_Invalid;
    bool hasLastTexture = false;
    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
        const ImDrawList* drawList = drawData->CmdLists[listIndex];
        if (!drawList) continue;
        for (const ImDrawCmd& cmd : drawList->CmdBuffer) {
            ++counters.drawCalls;
            ++counters.stateBinds;
            const ImTextureID cmdTexture = cmd.GetTexID();
            if (!hasLastTexture || cmdTexture != lastTexture) {
                ++counters.textureBinds;
                lastTexture = cmdTexture;
                hasLastTexture = true;
            }
        }
    }
    return counters;
}

struct Runtime2DProfileFrame {
    double totalFrameMs = 0.0;
    double update2DTotalMs = 0.0;
    double physics2DTotalMs = 0.0;
    double broadphaseMs = 0.0;
    double narrowphaseSolveMs = 0.0;
    double transformUpdateMs = 0.0;
    double renderSubmissionMs = 0.0;
    double actualDrawRenderMs = 0.0;
    double uiRuntimeMs = 0.0;
    double spriteBatchBuildMs = 0.0;
    double presentWaitMs = 0.0;
    double fpsCapSleepMs = 0.0;
    uint64_t textureBindCount = 0;
    uint64_t stateBindCount = 0;
    uint64_t drawCallCount = 0;
    uint64_t visibleObjectCount = 0;
    uint64_t collisionPairCandidateCount = 0;
    uint64_t collisionTestCount = 0;
};

struct Runtime2DProfileAggregate {
    Runtime2DProfileFrame sum;
    int frameCount = 0;
    double windowStartSec = -1.0;
};

Runtime2DProfileFrame gRuntime2DProfileFrame;
Runtime2DProfileAggregate gRuntime2DProfileAggregate;
bool gRuntime2DProfileEnabled = false;

void resetRuntime2DFrame() {
    gRuntime2DProfileFrame = Runtime2DProfileFrame{};
}

void accumulateRuntime2DFrame() {
    Runtime2DProfileAggregate& agg = gRuntime2DProfileAggregate;
    agg.frameCount += 1;
    agg.sum.totalFrameMs += gRuntime2DProfileFrame.totalFrameMs;
    agg.sum.update2DTotalMs += gRuntime2DProfileFrame.update2DTotalMs;
    agg.sum.physics2DTotalMs += gRuntime2DProfileFrame.physics2DTotalMs;
    agg.sum.broadphaseMs += gRuntime2DProfileFrame.broadphaseMs;
    agg.sum.narrowphaseSolveMs += gRuntime2DProfileFrame.narrowphaseSolveMs;
    agg.sum.transformUpdateMs += gRuntime2DProfileFrame.transformUpdateMs;
    agg.sum.renderSubmissionMs += gRuntime2DProfileFrame.renderSubmissionMs;
    agg.sum.actualDrawRenderMs += gRuntime2DProfileFrame.actualDrawRenderMs;
    agg.sum.uiRuntimeMs += gRuntime2DProfileFrame.uiRuntimeMs;
    agg.sum.spriteBatchBuildMs += gRuntime2DProfileFrame.spriteBatchBuildMs;
    agg.sum.presentWaitMs += gRuntime2DProfileFrame.presentWaitMs;
    agg.sum.fpsCapSleepMs += gRuntime2DProfileFrame.fpsCapSleepMs;
    agg.sum.textureBindCount += gRuntime2DProfileFrame.textureBindCount;
    agg.sum.stateBindCount += gRuntime2DProfileFrame.stateBindCount;
    agg.sum.drawCallCount += gRuntime2DProfileFrame.drawCallCount;
    agg.sum.visibleObjectCount += gRuntime2DProfileFrame.visibleObjectCount;
    agg.sum.collisionPairCandidateCount += gRuntime2DProfileFrame.collisionPairCandidateCount;
    agg.sum.collisionTestCount += gRuntime2DProfileFrame.collisionTestCount;
}

void resetRuntime2DAggregate(double nowSec) {
    gRuntime2DProfileAggregate = Runtime2DProfileAggregate{};
    gRuntime2DProfileAggregate.windowStartSec = nowSec;
}

void emitRuntime2DProfileSummaryIfReady(double nowSec) {
    Runtime2DProfileAggregate& agg = gRuntime2DProfileAggregate;
    if (agg.frameCount <= 0) return;
    if (agg.windowStartSec < 0.0) {
        agg.windowStartSec = nowSec;
    }
    const double elapsedSec = std::max(0.0, nowSec - agg.windowStartSec);
    if (agg.frameCount < 60 && elapsedSec < 1.0) {
        return;
    }

    const double inv = 1.0 / static_cast<double>(agg.frameCount);
    Runtime2DProfileFrame avg;
    avg.totalFrameMs = agg.sum.totalFrameMs * inv;
    avg.update2DTotalMs = agg.sum.update2DTotalMs * inv;
    avg.physics2DTotalMs = agg.sum.physics2DTotalMs * inv;
    avg.broadphaseMs = agg.sum.broadphaseMs * inv;
    avg.narrowphaseSolveMs = agg.sum.narrowphaseSolveMs * inv;
    avg.transformUpdateMs = agg.sum.transformUpdateMs * inv;
    avg.renderSubmissionMs = agg.sum.renderSubmissionMs * inv;
    avg.actualDrawRenderMs = agg.sum.actualDrawRenderMs * inv;
    avg.uiRuntimeMs = agg.sum.uiRuntimeMs * inv;
    avg.spriteBatchBuildMs = agg.sum.spriteBatchBuildMs * inv;
    avg.presentWaitMs = agg.sum.presentWaitMs * inv;
    avg.fpsCapSleepMs = agg.sum.fpsCapSleepMs * inv;
    avg.textureBindCount = static_cast<uint64_t>(std::llround(agg.sum.textureBindCount * inv));
    avg.stateBindCount = static_cast<uint64_t>(std::llround(agg.sum.stateBindCount * inv));
    avg.drawCallCount = static_cast<uint64_t>(std::llround(agg.sum.drawCallCount * inv));
    avg.visibleObjectCount = static_cast<uint64_t>(std::llround(agg.sum.visibleObjectCount * inv));
    avg.collisionPairCandidateCount = static_cast<uint64_t>(std::llround(agg.sum.collisionPairCandidateCount * inv));
    avg.collisionTestCount = static_cast<uint64_t>(std::llround(agg.sum.collisionTestCount * inv));

    struct CostEntry {
        const char* name = "";
        double ms = 0.0;
    };
    std::array<CostEntry, 11> costs = {{
        { "update2D", avg.update2DTotalMs },
        { "physics2D", avg.physics2DTotalMs },
        { "broadphase", avg.broadphaseMs },
        { "narrowphase", avg.narrowphaseSolveMs },
        { "transform", avg.transformUpdateMs },
        { "renderSubmit", avg.renderSubmissionMs },
        { "drawRender", avg.actualDrawRenderMs },
        { "uiRuntime", avg.uiRuntimeMs },
        { "spriteBuild", avg.spriteBatchBuildMs },
        { "presentWait", avg.presentWaitMs },
        { "fpsSleep", avg.fpsCapSleepMs }
    }};
    std::sort(costs.begin(), costs.end(), [](const CostEntry& a, const CostEntry& b) {
        return a.ms > b.ms;
    });

    const double cpuUpdateMs = std::max(0.0, avg.update2DTotalMs - avg.physics2DTotalMs);
    const double physicsMs = avg.physics2DTotalMs;
    const double uiMs = avg.uiRuntimeMs;
    const double renderSubmitMs = avg.renderSubmissionMs;
    const double gpuDrawPresentMs = avg.actualDrawRenderMs + avg.presentWaitMs;
    const double syncWaitMs = avg.fpsCapSleepMs;

    std::array<CostEntry, 6> bottleneckGroups = {{
        { "CPU update", cpuUpdateMs },
        { "physics", physicsMs },
        { "UI", uiMs },
        { "render submission", renderSubmitMs },
        { "GPU/draw/present", gpuDrawPresentMs },
        { "synchronization/waiting", syncWaitMs }
    }};
    std::sort(bottleneckGroups.begin(), bottleneckGroups.end(),
              [](const CostEntry& a, const CostEntry& b) { return a.ms > b.ms; });
    const CostEntry& topGroup = bottleneckGroups.front();

    const bool presentLikelyBound = avg.presentWaitMs > 0.3 &&
        avg.presentWaitMs > avg.update2DTotalMs &&
        avg.presentWaitMs > avg.renderSubmissionMs;
    const bool capLikelyBound = avg.fpsCapSleepMs > 0.3;

    std::fprintf(stdout,
        "[2DProfile avg/%df] frame=%.3fms update2D=%.3fms physics=%.3fms (broad=%.3fms narrow=%.3fms) "
        "transform=%.3fms submit=%.3fms draw=%.3fms ui=%.3fms spriteBuild=%.3fms present=%.3fms sleep=%.3fms "
        "drawCalls=%llu texBinds=%llu stateBinds=%llu visible=%llu pairCandidates=%llu collisionTests=%llu\n",
        agg.frameCount,
        avg.totalFrameMs, avg.update2DTotalMs, avg.physics2DTotalMs, avg.broadphaseMs, avg.narrowphaseSolveMs,
        avg.transformUpdateMs, avg.renderSubmissionMs, avg.actualDrawRenderMs, avg.uiRuntimeMs,
        avg.spriteBatchBuildMs, avg.presentWaitMs, avg.fpsCapSleepMs,
        static_cast<unsigned long long>(avg.drawCallCount),
        static_cast<unsigned long long>(avg.textureBindCount),
        static_cast<unsigned long long>(avg.stateBindCount),
        static_cast<unsigned long long>(avg.visibleObjectCount),
        static_cast<unsigned long long>(avg.collisionPairCandidateCount),
        static_cast<unsigned long long>(avg.collisionTestCount));
    std::fprintf(stdout,
        "[2DProfile top5] 1)%s=%.3fms 2)%s=%.3fms 3)%s=%.3fms 4)%s=%.3fms 5)%s=%.3fms | bottleneck=%s%s%s\n",
        costs[0].name, costs[0].ms,
        costs[1].name, costs[1].ms,
        costs[2].name, costs[2].ms,
        costs[3].name, costs[3].ms,
        costs[4].name, costs[4].ms,
        topGroup.name,
        presentLikelyBound ? " (present-bound likely)" : "",
        capLikelyBound ? " (fps-cap sleep detected)" : "");
    std::fflush(stdout);

    resetRuntime2DAggregate(nowSec);
}
} // namespace

void ModuRuntime2DProfiler_RecordUiRuntime(double uiRuntimeMs,
                                           double spriteBatchBuildMs,
                                           uint32_t visibleObjectCount) {
    if (!gRuntime2DProfileEnabled) return;
    gRuntime2DProfileFrame.uiRuntimeMs += uiRuntimeMs;
    gRuntime2DProfileFrame.spriteBatchBuildMs += spriteBatchBuildMs;
    gRuntime2DProfileFrame.visibleObjectCount += static_cast<uint64_t>(visibleObjectCount);
}

bool Engine::isProject2DPipeline() const {
    if (!projectManager.currentProject.isLoaded) {
        return false;
    }
    return projectManager.currentProject.pipeline == ProjectPipeline::Pipeline2D;
}

bool Engine::is2DWorldEditingEnabled() const {
    return isProject2DPipeline() || uiWorldMode;
}

void Engine::applyProjectPipelineDefaults(bool force) {
    if (!projectManager.currentProject.isLoaded) {
        return;
    }

    if (isProject2DPipeline()) {
        uiWorldMode = true;
        pixelGridSnapEnabled = true;
        if (force || !useSnap) {
            useSnap = true;
        }
        float step = static_cast<float>(std::max(1, pixelGridSnapStep));
        if (force || snapValue[0] < 1.0f) {
            snapValue[0] = step;
            snapValue[1] = step;
            snapValue[2] = step;
        }
    } else if (force) {
        uiWorldMode = false;
    }
}

namespace {
bool HasMeaningfulSpriteFrameScales(const UIElementComponent& ui) {
    if (ui.spriteCustomFrameScales.size() != ui.spriteCustomFrames.size() ||
        ui.spriteCustomFrameScales.empty()) {
        return false;
    }
    for (const glm::vec2& scale : ui.spriteCustomFrameScales) {
        if (std::abs(scale.x - 1.0f) > 0.0001f || std::abs(scale.y - 1.0f) > 0.0001f) {
            return true;
        }
    }
    return false;
}

glm::vec2 ResolveSpriteFrameScale(const UIElementComponent& ui, int frameIndex) {
    if (!ui.spriteCustomFramesEnabled || ui.spriteCustomFrames.empty()) {
        return glm::vec2(1.0f);
    }

    const int frameCount = static_cast<int>(ui.spriteCustomFrames.size());
    const int frame = std::clamp(frameIndex, 0, frameCount - 1);
    if (HasMeaningfulSpriteFrameScales(ui)) {
        const glm::vec2 authored = ui.spriteCustomFrameScales[static_cast<size_t>(frame)];
        return glm::vec2(std::max(0.01f, authored.x), std::max(0.01f, authored.y));
    }

    const glm::ivec4& referenceRect = ui.spriteCustomFrames.front();
    const glm::ivec4& frameRect = ui.spriteCustomFrames[static_cast<size_t>(frame)];
    const float referenceWidth = static_cast<float>(std::max(1, referenceRect.z));
    const float referenceHeight = static_cast<float>(std::max(1, referenceRect.w));
    return glm::vec2(
        static_cast<float>(std::max(1, frameRect.z)) / referenceWidth,
        static_cast<float>(std::max(1, frameRect.w)) / referenceHeight);
}
}

int Engine::resolveSpriteSheetFrame(const SceneObject& obj) const {
    if (obj.ui.spriteCustomFramesEnabled && !obj.ui.spriteCustomFrames.empty()) {
        int total = static_cast<int>(obj.ui.spriteCustomFrames.size());
        int frame = std::max(0, obj.ui.spriteSheetFrame);
        return frame % total;
    }
    if (!obj.ui.spriteSheetEnabled) {
        return 0;
    }
    int columns = std::max(1, obj.ui.spriteSheetColumns);
    int rows = std::max(1, obj.ui.spriteSheetRows);
    int total = std::max(1, columns * rows);
    int frame = std::max(0, obj.ui.spriteSheetFrame);
    return frame % total;
}

glm::vec2 Engine::getSpriteDisplaySize(const SceneObject& obj) const {
    glm::vec2 size(std::max(0.01f, obj.ui.size.x), std::max(0.01f, obj.ui.size.y));
    // Keep frame-to-frame clip scale from bullying the object's authored size.
    return size * ResolveSpriteFrameScale(obj.ui, resolveSpriteSheetFrame(obj));
}

std::array<ImVec2, 4> Engine::buildSpriteSheetUvs(const SceneObject& obj) const {
    std::array<ImVec2, 4> uvs = {
        ImVec2(0.0f, 1.0f),
        ImVec2(1.0f, 1.0f),
        ImVec2(1.0f, 0.0f),
        ImVec2(0.0f, 0.0f)
    };
    if (!obj.ui.spriteSheetEnabled) {
        return uvs;
    }

    if (obj.ui.spriteCustomFramesEnabled &&
        !obj.ui.spriteCustomFrames.empty() &&
        obj.ui.spriteSourceWidth > 0 &&
        obj.ui.spriteSourceHeight > 0) {
        const glm::ivec4 rect = obj.ui.spriteCustomFrames[resolveSpriteSheetFrame(obj)];
        const float invW = 1.0f / static_cast<float>(obj.ui.spriteSourceWidth);
        const float invH = 1.0f / static_cast<float>(obj.ui.spriteSourceHeight);
        const float u0 = rect.x * invW;
        const float u1 = (rect.x + rect.z) * invW;
        const float vTop = 1.0f - rect.y * invH;
        const float vBottom = 1.0f - (rect.y + rect.w) * invH;
        uvs[0] = ImVec2(u0, vTop);
        uvs[1] = ImVec2(u1, vTop);
        uvs[2] = ImVec2(u1, vBottom);
        uvs[3] = ImVec2(u0, vBottom);
        return uvs;
    }

    int columns = std::max(1, obj.ui.spriteSheetColumns);
    int rows = std::max(1, obj.ui.spriteSheetRows);
    int frame = resolveSpriteSheetFrame(obj);
    int col = frame % columns;
    int row = frame / columns;

    float u0 = static_cast<float>(col) / static_cast<float>(columns);
    float u1 = static_cast<float>(col + 1) / static_cast<float>(columns);
    float vTop = 1.0f - static_cast<float>(row) / static_cast<float>(rows);
    float vBottom = 1.0f - static_cast<float>(row + 1) / static_cast<float>(rows);

    uvs[0] = ImVec2(u0, vTop);
    uvs[1] = ImVec2(u1, vTop);
    uvs[2] = ImVec2(u1, vBottom);
    uvs[3] = ImVec2(u0, vBottom);
    return uvs;
}

namespace {
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

fs::path resolveRecentProjectRoot(const std::string& recentPath) {
    fs::path path(recentPath);
    if (path.empty()) return {};
    if (path.extension() == ".modu") {
        return path.parent_path();
    }
    if (fs::is_directory(path)) {
        return path;
    }
    return path.has_parent_path() ? path.parent_path() : path;
}

bool importInstalledPackagesFromRecent(const ProjectManager& projectManager,
                                       const fs::path& targetProjectRoot,
                                       std::string& outSourceProjectName,
                                       std::string& outError) {
    std::error_code ec;
    fs::path targetCanonical = fs::weakly_canonical(targetProjectRoot, ec);
    if (ec || targetCanonical.empty()) {
        ec.clear();
        targetCanonical = fs::absolute(targetProjectRoot, ec);
        ec.clear();
    }

    for (const auto& rp : projectManager.recentProjects) {
        fs::path sourceRoot = resolveRecentProjectRoot(rp.path);
        if (sourceRoot.empty()) continue;

        fs::path sourceCanonical = fs::weakly_canonical(sourceRoot, ec);
        if (ec || sourceCanonical.empty()) {
            ec.clear();
            sourceCanonical = fs::absolute(sourceRoot, ec);
            ec.clear();
        }
        if (!sourceCanonical.empty() && !targetCanonical.empty() && sourceCanonical == targetCanonical) {
            continue;
        }

        fs::path sourceManifest = sourceRoot / "packages.modu";
        if (!fs::exists(sourceManifest)) continue;

        fs::path targetManifest = targetProjectRoot / "packages.modu";
        fs::copy_file(sourceManifest, targetManifest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            outError = "Failed to copy package manifest from " + sourceRoot.string() + ": " + ec.message();
            return false;
        }

        std::string copyError;
        if (!copyDirectoryRecursive(sourceRoot / "Library" / "InstalledPackages",
                                    targetProjectRoot / "Library" / "InstalledPackages",
                                    copyError)) {
            outError = copyError;
            return false;
        }

        outSourceProjectName = rp.name.empty() ? sourceRoot.filename().string() : rp.name;
        return true;
    }

    outError = "No recent project with a package manifest was found.";
    return false;
}

bool applyTemplateProject(const fs::path& templateProjectRoot,
                          Project& project,
                          const std::string& projectName,
                          ProjectPipeline pipeline,
                          Modularity::GraphicsBackend rendererBackend,
                          std::string& outError) {
    if (templateProjectRoot.empty()) {
        return true;
    }

    fs::path templateProjectFile = templateProjectRoot / "project.modu";
    if (!fs::exists(templateProjectFile)) {
        outError = "Template is missing project.modu: " + templateProjectRoot.string();
        return false;
    }

    std::string copyError;
    if (!copyDirectoryRecursive(templateProjectRoot, project.projectPath, copyError)) {
        outError = "Failed to copy template project: " + copyError;
        return false;
    }

    if (!project.load(project.projectPath / "project.modu")) {
        outError = "Failed to load copied template project.";
        return false;
    }

    project.name = projectName;
    project.pipeline = pipeline;
    project.rendererBackend = rendererBackend;
    if (project.currentSceneName.empty()) {
        project.currentSceneName = "Main";
    }
    project.saveProjectFile();
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

fs::path resolveCurrentExecutablePath() {
#if defined(__linux__)
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec && !exe.empty()) {
        return exe.lexically_normal();
    }
#elif defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len > 0) {
        return fs::path(buffer).lexically_normal();
    }
#endif

    std::error_code cwdEc;
#if defined(_WIN32)
    fs::path fallback = fs::current_path(cwdEc) / "Modularity.exe";
#else
    fs::path fallback = fs::current_path(cwdEc) / "Modularity";
#endif
    return fallback.lexically_normal();
}

bool relaunchEditorWithBackend(Modularity::GraphicsBackend backend,
                               const fs::path& projectFile,
                               std::string& error) {
    std::error_code ec;
    fs::path normalizedProject = projectFile;
    if (normalizedProject.empty()) {
        error = "Project file path is empty.";
        return false;
    }
    if (normalizedProject.is_relative()) {
        normalizedProject = fs::absolute(normalizedProject, ec);
        if (ec) {
            error = "Failed to resolve project file path.";
            return false;
        }
    }
    normalizedProject = normalizedProject.lexically_normal();

    fs::path autoStartPath = fs::current_path(ec) / "autostart.modu";
    if (ec) {
        error = "Failed to resolve autostart.modu path.";
        return false;
    }

    std::ofstream autostart(autoStartPath, std::ios::trunc);
    if (!autostart.is_open()) {
        error = "Failed to write autostart.modu.";
        return false;
    }
    autostart << "project=" << normalizedProject.string() << "\n";
    autostart << "mode=editor\n";
    autostart << "oneshot=1\n";
    autostart.close();

    fs::path executablePath = resolveCurrentExecutablePath();
    if (executablePath.empty()) {
        error = "Failed to resolve editor executable path.";
        return false;
    }
    if (!fs::exists(executablePath, ec) || ec) {
        error = "Editor executable not found: " + executablePath.string();
        return false;
    }

    const std::string backendValue =
        (backend == Modularity::GraphicsBackend::Vulkan) ? "vulkan" : "opengl";
    std::string command;
#if defined(_WIN32)
    command = "cmd /C \"set MODULARITY_RENDER_BACKEND=" + backendValue + " && start \"\" " +
              quotePath(executablePath) + "\"";
#else
    command = "MODULARITY_RENDER_BACKEND=" + backendValue + " " +
              quotePath(executablePath) + " >/dev/null 2>&1 &";
#endif

    if (std::system(command.c_str()) != 0) {
        error = "Failed to launch editor process.";
        return false;
    }
    return true;
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

    fs::path templatesDir = exportRoot / "Template-Projects";
    if (fs::exists(templatesDir)) {
        fs::remove_all(templatesDir, ec);
        if (ec) {
            error = "Failed to remove existing template projects.";
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

    fs::path runtimeBundle = exportRoot / "content.modbundle";
    if (fs::exists(runtimeBundle)) {
        fs::remove(runtimeBundle, ec);
        if (ec) {
            error = "Failed to remove existing runtime bundle.";
            return;
        }
    }

    fs::path runtimeStage = exportRoot / "_runtime_stage";
    if (fs::exists(runtimeStage)) {
        fs::remove_all(runtimeStage, ec);
        if (ec) {
            error = "Failed to remove existing runtime staging directory.";
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

std::string RuntimeHashHex(uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

uint64_t RuntimePathHash(const std::string& value) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : value) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

bool RuntimePathInsideRoot(const fs::path& path, const fs::path& root) {
    std::error_code ec;
    fs::path normalizedPath = fs::weakly_canonical(path, ec);
    if (ec) normalizedPath = fs::absolute(path, ec);
    if (ec) normalizedPath = path.lexically_normal();

    ec.clear();
    fs::path normalizedRoot = fs::weakly_canonical(root, ec);
    if (ec) normalizedRoot = fs::absolute(root, ec);
    if (ec) normalizedRoot = root.lexically_normal();

    auto pathIt = normalizedPath.begin();
    auto rootIt = normalizedRoot.begin();
    for (; rootIt != normalizedRoot.end(); ++rootIt, ++pathIt) {
        if (pathIt == normalizedPath.end() || *pathIt != *rootIt) {
            return false;
        }
    }
    return true;
}

fs::path ResolveRuntimeSourcePath(const std::string& value, const fs::path& projectRoot) {
    if (value.empty()) return {};

    std::error_code ec;
    fs::path input(value);
    if (input.is_absolute()) {
        fs::path absolute = fs::absolute(input, ec);
        if (ec) absolute = input;
        return absolute.lexically_normal();
    }

    fs::path candidate = projectRoot / input;
    fs::path absolute = fs::absolute(candidate, ec);
    if (ec) absolute = candidate;
    return absolute.lexically_normal();
}

bool EnsureDirectoryForFile(const fs::path& filePath, std::string& error) {
    std::error_code ec;
    const fs::path parent = filePath.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec) {
            error = "Failed to create directory: " + parent.string();
            return false;
        }
    }
    return true;
}

bool CopyFileIntoRuntimeRoot(const fs::path& sourcePath,
                             const fs::path& runtimeRoot,
                             const fs::path& relativePath,
                             std::string& error) {
    if (sourcePath.empty() || relativePath.empty()) {
        error = "Runtime copy received an empty path.";
        return false;
    }

    const fs::path destination = runtimeRoot / relativePath;
    if (!EnsureDirectoryForFile(destination, error)) {
        return false;
    }

    std::error_code ec;
    fs::copy_file(sourcePath, destination, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "Failed to copy runtime file '" + sourcePath.string() +
                "' to '" + destination.string() + "'.";
        return false;
    }
    return true;
}

bool CopyDirectoryIntoRuntimeRoot(const fs::path& sourceDir,
                                  const fs::path& runtimeRoot,
                                  const fs::path& relativeDir,
                                  std::string& error) {
    if (sourceDir.empty() || relativeDir.empty()) {
        error = "Runtime directory copy received an empty path.";
        return false;
    }

    std::error_code ec;
    if (!fs::exists(sourceDir, ec)) {
        error = "Runtime directory source does not exist: " + sourceDir.string();
        return false;
    }
    const fs::path destination = runtimeRoot / relativeDir;
    fs::create_directories(destination, ec);
    if (ec) {
        error = "Failed to create runtime directory: " + destination.string();
        return false;
    }

    for (auto it = fs::recursive_directory_iterator(sourceDir, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (ec) {
            error = "Failed while scanning runtime directory: " + sourceDir.string();
            return false;
        }
        const fs::path entryPath = it->path();
        const fs::path rel = fs::relative(entryPath, sourceDir, ec);
        if (ec) {
            error = "Failed to relativize runtime directory entry: " + entryPath.string();
            return false;
        }
        const fs::path targetPath = destination / rel;
        if (it->is_directory()) {
            fs::create_directories(targetPath, ec);
            if (ec) {
                error = "Failed to create runtime directory: " + targetPath.string();
                return false;
            }
            continue;
        }
        if (!it->is_regular_file()) continue;
        if (!EnsureDirectoryForFile(targetPath, error)) {
            return false;
        }
        fs::copy_file(entryPath, targetPath, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Failed to copy runtime directory file: " + entryPath.string();
            return false;
        }
    }
    return true;
}

class RuntimeExportStager {
public:
    RuntimeExportStager(fs::path projectRootPath, fs::path runtimeRootPath)
        : projectRoot(std::move(projectRootPath)),
          runtimeRoot(std::move(runtimeRootPath)) {}

    bool stageFileReference(std::string& value,
                            const std::string& externalCategory,
                            std::string& error) {
        if (value.empty()) return true;
        const fs::path sourcePath = ResolveRuntimeSourcePath(value, projectRoot);
        std::error_code ec;
        if (sourcePath.empty() || !fs::exists(sourcePath, ec) || ec) {
            error = "Missing runtime asset: " + value;
            return false;
        }

        const std::string sourceKey = sourcePath.lexically_normal().string();
        auto cacheIt = stagedFileRefs.find(sourceKey);
        if (cacheIt != stagedFileRefs.end()) {
            value = cacheIt->second;
            return true;
        }

        fs::path relativePath;
        if (RuntimePathInsideRoot(sourcePath, projectRoot)) {
            relativePath = fs::relative(sourcePath, projectRoot, ec);
            if (ec || relativePath.empty()) {
                error = "Failed to resolve runtime asset path relative to project: " + sourcePath.string();
                return false;
            }
        } else {
            const std::string hashedName = sourcePath.stem().string() + "_" +
                                           RuntimeHashHex(RuntimePathHash(sourceKey)) +
                                           sourcePath.extension().string();
            relativePath = fs::path("Assets") / "RuntimeImported" / externalCategory / hashedName;
        }

        if (!CopyFileIntoRuntimeRoot(sourcePath, runtimeRoot, relativePath, error)) {
            return false;
        }

        const std::string remapped = relativePath.generic_string();
        stagedFileRefs[sourceKey] = remapped;
        value = remapped;
        return true;
    }

    bool stageDirectoryBackedReference(std::string& value,
                                       const std::string& externalCategory,
                                       std::string& error) {
        if (value.empty()) return true;
        const fs::path sourcePath = ResolveRuntimeSourcePath(value, projectRoot);
        std::error_code ec;
        if (sourcePath.empty() || !fs::exists(sourcePath, ec) || ec) {
            error = "Missing runtime directory-backed asset: " + value;
            return false;
        }

        const fs::path sourceDir = sourcePath.parent_path();
        const std::string sourceKey = sourceDir.lexically_normal().string();
        auto dirIt = stagedDirectoryRoots.find(sourceKey);
        fs::path relativeDir;
        if (dirIt != stagedDirectoryRoots.end()) {
            relativeDir = dirIt->second;
        } else if (RuntimePathInsideRoot(sourceDir, projectRoot)) {
            relativeDir = fs::relative(sourceDir, projectRoot, ec);
            if (ec || relativeDir.empty()) {
                error = "Failed to resolve runtime directory relative path: " + sourceDir.string();
                return false;
            }
            if (!CopyDirectoryIntoRuntimeRoot(sourceDir, runtimeRoot, relativeDir, error)) {
                return false;
            }
            stagedDirectoryRoots[sourceKey] = relativeDir;
        } else {
            const std::string dirName = sourceDir.filename().empty()
                ? "dir"
                : sourceDir.filename().string();
            relativeDir = fs::path("Assets") / "RuntimeImported" / externalCategory /
                          (dirName + "_" + RuntimeHashHex(RuntimePathHash(sourceKey)));
            if (!CopyDirectoryIntoRuntimeRoot(sourceDir, runtimeRoot, relativeDir, error)) {
                return false;
            }
            stagedDirectoryRoots[sourceKey] = relativeDir;
        }

        value = (relativeDir / sourcePath.filename()).generic_string();
        return true;
    }

private:
    fs::path projectRoot;
    fs::path runtimeRoot;
    std::unordered_map<std::string, std::string> stagedFileRefs;
    std::unordered_map<std::string, fs::path> stagedDirectoryRoots;
};

bool RemapSceneObjectForRuntime(SceneObject& obj,
                                RuntimeExportStager& stager,
                                std::string& error) {
    if (!stager.stageFileReference(obj.materialPath, "Materials", error)) return false;
    if (!stager.stageFileReference(obj.albedoTexturePath, "Textures", error)) return false;
    if (!stager.stageFileReference(obj.overlayTexturePath, "Textures", error)) return false;
    if (!stager.stageFileReference(obj.normalMapPath, "Textures", error)) return false;
    if (!stager.stageFileReference(obj.vertexShaderPath, "Shaders", error)) return false;
    if (!stager.stageFileReference(obj.fragmentShaderPath, "Shaders", error)) return false;
    if (!stager.stageDirectoryBackedReference(obj.meshPath, "Models", error)) return false;
    if (obj.hasAudioSource && !stager.stageFileReference(obj.audioSource.clipPath, "Audio", error)) return false;
    if (obj.hasLight2D && !stager.stageFileReference(obj.light2D.cookieTexturePath, "Textures", error)) return false;
    if (obj.hasAnimation) {
        if (!stager.stageFileReference(obj.animation.clipAssetPath, "Animations", error)) return false;
        for (auto& clip : obj.animation.clips) {
            if (!stager.stageFileReference(clip.assetPath, "Animations", error)) return false;
        }
    }
    for (std::string& materialRef : obj.additionalMaterialPaths) {
        if (!stager.stageFileReference(materialRef, "Materials", error)) return false;
    }
    return true;
}

bool StageRuntimeScene(const fs::path& sourceScenePath,
                       const std::string& sceneName,
                       RuntimeExportStager& stager,
                       const fs::path& runtimeRoot,
                       std::string& error) {
    std::vector<SceneObject> sceneObjects;
    int nextId = 0;
    int sceneVersion = 20;
    float timeOfDay = -1.0f;
    if (!SceneSerializer::loadSceneDeferred(sourceScenePath, sceneObjects, nextId, sceneVersion, &timeOfDay)) {
        error = "Failed to load scene for runtime export: " + sourceScenePath.string();
        return false;
    }

    for (SceneObject& obj : sceneObjects) {
        if (!RemapSceneObjectForRuntime(obj, stager, error)) {
            error = "Scene '" + sceneName + "': " + error;
            return false;
        }
    }

    const fs::path runtimeScenePath = runtimeRoot / "Assets" / "Scenes" / (sceneName + ".scene");
    if (!EnsureDirectoryForFile(runtimeScenePath, error)) {
        return false;
    }
    if (!SceneSerializer::saveScene(runtimeScenePath, sceneObjects, nextId, timeOfDay < 0.0f ? 0.5f : timeOfDay)) {
        error = "Failed to save staged runtime scene: " + runtimeScenePath.string();
        return false;
    }
    return true;
}

bool CopyRuntimeResourcesSubset(const fs::path& sourceRoot,
                                const fs::path& runtimeRoot,
                                std::string& error) {
    const fs::path resourcesRoot = sourceRoot / "Resources";
    if (!CopyDirectoryIntoRuntimeRoot(resourcesRoot / "Shaders", runtimeRoot, fs::path("Resources") / "Shaders", error)) {
        return false;
    }
    if (!CopyDirectoryIntoRuntimeRoot(resourcesRoot / "Textures", runtimeRoot, fs::path("Resources") / "Textures", error)) {
        return false;
    }
    if (fs::exists(resourcesRoot / "Fonts")) {
        if (!CopyDirectoryIntoRuntimeRoot(resourcesRoot / "Fonts", runtimeRoot, fs::path("Resources") / "Fonts", error)) {
            return false;
        }
    }

    const fs::path logoPath = resourcesRoot / "Engine-Root" / "Modu-Logo.png";
    if (fs::exists(logoPath)) {
        if (!CopyFileIntoRuntimeRoot(logoPath, runtimeRoot, fs::path("Resources") / "Engine-Root" / "Modu-Logo.png", error)) {
            return false;
        }
    }
    return true;
}

std::vector<RuntimeBundleEntry> CollectRuntimeBundleEntries(const fs::path& runtimeRoot) {
    std::vector<RuntimeBundleEntry> entries;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(runtimeRoot, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (ec || !it->is_regular_file()) continue;
        const fs::path sourcePath = it->path();
        const fs::path archivePath = fs::relative(sourcePath, runtimeRoot, ec);
        if (ec || archivePath.empty()) continue;
        entries.push_back({sourcePath, archivePath});
    }
    std::sort(entries.begin(), entries.end(),
              [](const RuntimeBundleEntry& a, const RuntimeBundleEntry& b) {
                  return a.archivePath.generic_string() < b.archivePath.generic_string();
              });
    return entries;
}

fs::path BuildRuntimeCacheRoot(const fs::path& appDataPath,
                               const fs::path& bundlePath) {
    std::error_code ec;
    const uint64_t fileSize = fs::exists(bundlePath, ec) ? fs::file_size(bundlePath, ec) : 0ull;
    ec.clear();
    const auto writeTime = fs::exists(bundlePath, ec) ? fs::last_write_time(bundlePath, ec) : fs::file_time_type{};
    const auto timeTicks = static_cast<uint64_t>(writeTime.time_since_epoch().count());
    const std::string seed = bundlePath.lexically_normal().string() + "|" +
                             std::to_string(fileSize) + "|" +
                             std::to_string(timeTicks);
    const std::string cacheId = RuntimeHashHex(RuntimePathHash(seed));
    return appDataPath / "RuntimeCache" / cacheId;
}
} // namespace
#pragma endregion

#pragma region Window + Selection Utilities
void window_size_callback(GLFWwindow* window, int width, int height) {
    if (auto* engine = static_cast<Engine*>(glfwGetWindowUserPointer(window))) {
        engine->onWindowResized(width, height);
    }
    if (glfwGetCurrentContext() != nullptr) {
        glViewport(0, 0, width, height);
    }
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
        if (std::find(selectedObjectIds.begin(), selectedObjectIds.end(), id) == selectedObjectIds.end()) {
            selectedObjectIds.push_back(id);
        }
        selectedObjectId = id;
        hierarchyRangeAnchorId = id;
    } else {
        selectedObjectIds.clear();
        selectedObjectId = -1;
        hierarchyRangeAnchorId = -1;
    }
}

void Engine::clearSelection() {
    selectedObjectIds.clear();
    selectedObjectId = -1;
    hierarchyRangeAnchorId = -1;
}

Camera Engine::makeCameraFromObject(const SceneObject& obj) const {
    Camera cam;
    cam.position = obj.position;
    cam.orthographic = isProject2DPipeline() || obj.camera.use2D;
    cam.pixelsPerUnit = std::max(1.0f, obj.camera.pixelsPerUnit);
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

void Engine::capturePlayModeSnapshot() {
    playModeSnapshot.scene.objects = sceneObjects;
    playModeSnapshot.scene.selectedIds = selectedObjectIds;
    playModeSnapshot.scene.nextId = nextObjectId;
    playModeSnapshot.hadUnsavedChanges = projectManager.currentProject.hasUnsavedChanges;
    playModeSnapshot.valid = true;
}

void Engine::restorePlayModeSnapshot() {
    if (!playModeSnapshot.valid) {
        return;
    }

    sceneObjects = playModeSnapshot.scene.objects;
    selectedObjectIds = playModeSnapshot.scene.selectedIds;
    selectedObjectId = selectedObjectIds.empty() ? -1 : selectedObjectIds.back();
    nextObjectId = playModeSnapshot.scene.nextId;
    projectManager.currentProject.hasUnsavedChanges = playModeSnapshot.hadUnsavedChanges;

    sceneObjectIndexById.clear();
    sceneObjectIndexData = nullptr;
    sceneObjectIndexCount = 0;
    markRuntimeScriptBindingsDirty();
    aiAgentRuntimeStates.clear();
    activePlayerId = -1;
    updateHierarchyWorldTransforms();
    gizmoHistoryCaptured = false;
    worldUiGizmoHistoryCaptured = false;
    gameUiGizmoHistoryCaptured = false;

    playModeSnapshot = {};
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
    sceneObjectIndexById.clear();
    sceneObjectIndexData = nullptr;
    sceneObjectIndexCount = 0;
    markRuntimeScriptBindingsDirty();
    aiAgentRuntimeStates.clear();
    activePlayerId = -1;
    updateHierarchyWorldTransforms();
    gizmoHistoryCaptured = false;
    worldUiGizmoHistoryCaptured = false;
    gameUiGizmoHistoryCaptured = false;
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
    sceneObjectIndexById.clear();
    sceneObjectIndexData = nullptr;
    sceneObjectIndexCount = 0;
    markRuntimeScriptBindingsDirty();
    aiAgentRuntimeStates.clear();
    activePlayerId = -1;
    updateHierarchyWorldTransforms();
    gizmoHistoryCaptured = false;
    worldUiGizmoHistoryCaptured = false;
    gameUiGizmoHistoryCaptured = false;
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
Modularity::GraphicsBackend Engine::resolveRequestedBackend() const {
    const char* value = std::getenv("MODULARITY_RENDER_BACKEND");
    if (!value || !*value) {
        value = std::getenv("MODULARITY_GFX_API");
    }
    if (!value || !*value) {
        return Modularity::GraphicsBackend::OpenGL;
    }

    Modularity::GraphicsBackend backend = Modularity::GraphicsBackendFromString(value);
#if !MODULARITY_HAS_VULKAN
    if (backend == Modularity::GraphicsBackend::Vulkan) {
        std::cerr << "[WARN] Vulkan was requested but this build has no Vulkan support. Falling back to OpenGL.\n";
        return Modularity::GraphicsBackend::OpenGL;
    }
#endif
    return backend;
}

void Engine::applySceneTimeOfDay(float timeOfDay) {
    sceneTimeOfDay = normalizeTimeOfDay(timeOfDay);

    if (Skybox* skybox = renderer.getSkybox()) {
        skybox->setTimeOfDay(sceneTimeOfDay);
    }

    if (usingVulkan() && vulkanRendererInitialized && vulkanRenderer) {
        vulkanRenderer->setSkyboxTimeOfDay(sceneTimeOfDay);
    }
}

float Engine::getSceneTimeOfDay() {
    if (!usingVulkan()) {
        if (Skybox* skybox = renderer.getSkybox()) {
            sceneTimeOfDay = normalizeTimeOfDay(skybox->getTimeOfDay());
        }
    }
    return sceneTimeOfDay;
}

void Engine::getRuntimeInternalResolution(int& outWidth, int& outHeight) const {
    switch (gameViewportResolutionIndex) {
        case 1:
            outWidth = 1920;
            outHeight = 1080;
            break;
        case 2:
            outWidth = 1280;
            outHeight = 720;
            break;
        case 3:
            outWidth = 2560;
            outHeight = 1440;
            break;
        case 4:
            outWidth = std::clamp(gameViewportCustomWidth, 64, 8192);
            outHeight = std::clamp(gameViewportCustomHeight, 64, 8192);
            break;
        case 0:
        default:
            outWidth = kRuntimeInternalWidth;
            outHeight = kRuntimeInternalHeight;
            break;
    }
}

float Engine::getRuntimeInternalAspect() const {
    int width = kRuntimeInternalWidth;
    int height = kRuntimeInternalHeight;
    getRuntimeInternalResolution(width, height);
    return static_cast<float>(width) / static_cast<float>(std::max(1, height));
}

void Engine::onWindowResized(int width, int height) {
    if (width <= 0 || height <= 0) return;
    viewportWidth = width;
    viewportHeight = height;

    if (usingVulkan()) {
        if (vulkanRenderer) {
            vulkanRenderer->notifyResize();
        }
        return;
    }

    if (rendererInitialized) {
        renderer.resize(width, height);
    }
}

bool Engine::init() {
    std::cerr << "[DEBUG] Creating window..." << std::endl;
    graphicsBackend = resolveRequestedBackend();
    editorWindow = window.makeWindow(graphicsBackend);
    if (!editorWindow && graphicsBackend == Modularity::GraphicsBackend::Vulkan) {
        std::cerr << "[WARN] Vulkan window creation failed. Falling back to OpenGL.\n";
        graphicsBackend = Modularity::GraphicsBackend::OpenGL;
        editorWindow = window.makeWindow(graphicsBackend);
    }
    if (!editorWindow) {
        std::cerr << "[DEBUG] Window creation failed!" << std::endl;
        return false;
    }
    std::cerr << "[DEBUG] Graphics backend: " << Modularity::ToString(graphicsBackend) << std::endl;
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

    auto drop_cb = [](GLFWwindow* window, int pathCount, const char* paths[]) {
        auto* engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
        if (!engine || pathCount <= 0 || !paths) return;

        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        engine->pendingExternalFileDrops.reserve(engine->pendingExternalFileDrops.size() + static_cast<size_t>(pathCount));
        for (int i = 0; i < pathCount; ++i) {
            if (!paths[i] || !*paths[i]) continue;
            Engine::ExternalFileDropEvent evt;
            evt.path = fs::path(paths[i]);
            evt.mouseX = mouseX;
            evt.mouseY = mouseY;
            engine->pendingExternalFileDrops.push_back(std::move(evt));
        }
    };
    glfwSetDropCallback(editorWindow, drop_cb);

    loadAutoStartConfig();
#ifdef MODULARITY_PLAYER
    playerMode = true;
    autoStartPlayerMode = true;
#endif
    if (autoStartRequested && !autoStartBundlePath.empty()) {
        const fs::path bundlePath = fs::path(autoStartBundlePath);
        const fs::path runtimeCacheRoot = BuildRuntimeCacheRoot(projectManager.appDataPath, bundlePath);
        std::string bundleError;
        if (!ExtractRuntimeContentBundle(bundlePath, runtimeCacheRoot, bundleError)) {
            addConsoleMessage("Failed to extract packaged runtime content: " + bundleError,
                              ConsoleMessageType::Error);
            autoStartRequested = false;
            showLauncher = true;
        } else {
            std::error_code cwdEc;
            fs::current_path(runtimeCacheRoot, cwdEc);
            if (cwdEc) {
                addConsoleMessage("Failed to switch runtime content root: " + cwdEc.message(),
                                  ConsoleMessageType::Error);
                autoStartRequested = false;
                showLauncher = true;
            } else {
                fs::path projectPath = autoStartProjectPath.empty()
                    ? (runtimeCacheRoot / "project.modu")
                    : (runtimeCacheRoot / fs::path(autoStartProjectPath));
                autoStartProjectPath = projectPath.lexically_normal().string();
            }
        }
    }

    std::cerr << "[DEBUG] Setting up ImGui..." << std::endl;
    setupImGui();
    std::cerr << "[DEBUG] ImGui setup complete" << std::endl;

    if (usingVulkan()) {
        if (!initVulkanRenderer()) {
            std::cerr << "[DEBUG] Vulkan renderer init failed!" << std::endl;
            return false;
        }
    }

    if (!audio.init()) {
        std::cerr << "[DEBUG] Audio init failed\n";
        addConsoleMessage("Audio initialization failed. Audio playback will be disabled.", ConsoleMessageType::Warning);
    }
    
    logToConsole("Engine initialized - Waiting for project selection");
    if (autoStartRequested && !autoStartProjectPath.empty()) {
        startProjectLoad(autoStartProjectPath);
    }
    return true;
}

bool Engine::initRenderer() {
    if (usingVulkan()) {
        return vulkanRendererInitialized;
    }
    if (rendererInitialized) return true;

    try {
        renderer.initialize();
        rendererInitialized = true;
        applySceneTimeOfDay(sceneTimeOfDay);
        return true;
    } catch (...) {
        return false;
    }
}

bool Engine::initVulkanRenderer() {
    if (!usingVulkan()) {
        return false;
    }
    if (vulkanRendererInitialized) {
        return true;
    }

#if !MODULARITY_HAS_VULKAN
    addConsoleMessage("Vulkan backend requested, but this build was compiled without Vulkan support.",
                      ConsoleMessageType::Error);
    return false;
#else
    vulkanRenderer = std::make_unique<Modularity::VulkanRenderer>();
    if (!vulkanRenderer->initialize(editorWindow)) {
        addConsoleMessage("Vulkan initialization failed: " + vulkanRenderer->getLastError(),
                          ConsoleMessageType::Error);
        vulkanRenderer.reset();
        return false;
    }
    if (!vulkanRenderer->initImGuiBackend()) {
        addConsoleMessage("Vulkan ImGui backend initialization failed: " + vulkanRenderer->getLastError(),
                          ConsoleMessageType::Error);
        vulkanRenderer->shutdown();
        vulkanRenderer.reset();
        return false;
    }

    vulkanRendererInitialized = true;
    applySceneTimeOfDay(sceneTimeOfDay);
    addConsoleMessage("Initialized Vulkan backend (experimental).", ConsoleMessageType::Info);
    addConsoleMessage("Vulkan viewport preview supports built-in GLSL materials, albedo/overlay/normal textures, scene lights, and sky time-of-day.",
                      ConsoleMessageType::Warning);
    return true;
#endif
}

void Engine::run() {
    std::cerr << "[DEBUG] Entering main loop, showLauncher=" << showLauncher << std::endl;
    constexpr float kRigidbody2DFixedStep = 1.0f / 120.0f;
    constexpr int kMaxRigidbody2DStepsPerFrame = 4;
    float rigidbody2DAccumulator = 0.0f;
    bool runtime2DProfileWasEnabled = false;
    
    while (!glfwWindowShouldClose(editorWindow)) {
        double frameStart = glfwGetTime();
        const auto frameStartClock = Runtime2DClock::now();
        const bool runtime2DProfileThisFrame =
            projectManager.currentProject.isLoaded &&
            !showLauncher &&
            isProject2DPipeline() &&
            (playerMode || isPlaying || specMode || testMode);
        gRuntime2DProfileEnabled = runtime2DProfileThisFrame;
        if (runtime2DProfileThisFrame) {
            if (!runtime2DProfileWasEnabled || gRuntime2DProfileAggregate.windowStartSec < 0.0) {
                resetRuntime2DAggregate(frameStart);
            }
            resetRuntime2DFrame();
            ModuRuntime2DRenderCounters_Reset();
        }
        runtime2DProfileWasEnabled = runtime2DProfileThisFrame;

        double profilePhysics2DMs = 0.0;
        double profileTransformMs = 0.0;
        double profileRenderSubmissionMs = 0.0;
        double profileActualDrawMs = 0.0;
        Runtime2DImGuiDrawCounters imguiDrawCounters;
        Runtime2DClock::time_point update2DStart;
        if (runtime2DProfileThisFrame) {
            update2DStart = Runtime2DClock::now();
        }

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

        const bool termsPending = requiresTermsOfServiceAcceptance();

        if (!showLauncher && !termsPending) {
            handleKeyboardShortcuts();
        }

        if (termsPending) {
            cursorLocked = false;
            gameViewCursorLocked = false;
            viewportController.setFocused(false);
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
            rigidbody2DAccumulator = std::min(
                rigidbody2DAccumulator + deltaTime,
                kRigidbody2DFixedStep * static_cast<float>(kMaxRigidbody2DStepsPerFrame));

            int simulateSteps = 0;
            while (rigidbody2DAccumulator >= kRigidbody2DFixedStep &&
                   simulateSteps < kMaxRigidbody2DStepsPerFrame) {
                Runtime2DClock::time_point physicsStepStart;
                if (runtime2DProfileThisFrame) {
                    physicsStepStart = Runtime2DClock::now();
                }
                updateRigidbody2D(kRigidbody2DFixedStep);
                if (runtime2DProfileThisFrame) {
                    profilePhysics2DMs += Runtime2DMsSince(physicsStepStart, Runtime2DClock::now());
                }
                rigidbody2DAccumulator -= kRigidbody2DFixedStep;
                ++simulateSteps;
            }
        } else {
            rigidbody2DAccumulator = 0.0f;
        }

        if (isPlaying) {
            updateCameraFollow2D(deltaTime);
        }

        float runtimeAnimDelta = ((isPlaying && isPaused) ? 0.0f : deltaTime);
        updateRuntimeAnimations(runtimeAnimDelta);
        updateSkeletalAnimations(deltaTime);
        if (runtime2DProfileThisFrame) {
            const auto transformStart = Runtime2DClock::now();
            updateHierarchyWorldTransforms();
            profileTransformMs += Runtime2DMsSince(transformStart, Runtime2DClock::now());
        } else {
            updateHierarchyWorldTransforms();
        }

        bool hasRuntime3DPhysics = false;
        bool hasRuntimeAIAgent = false;
        bool hasRuntimeSkeletal = false;
        const bool runtimeSystemsActive = (isPlaying || specMode || testMode);
        if (runtimeSystemsActive) {
            for (const SceneObject& obj : sceneObjects) {
                if (!IsObjectEnabledInHierarchy(obj)) continue;
                if (!hasRuntime3DPhysics &&
                    ((obj.hasRigidbody && obj.rigidbody.enabled) ||
                     (obj.hasCollider && obj.collider.enabled))) {
                    hasRuntime3DPhysics = true;
                }
                if (!hasRuntimeAIAgent && obj.hasAIAgent) {
                    hasRuntimeAIAgent = true;
                }
                if (!hasRuntimeSkeletal &&
                    obj.hasSkeletalAnimation &&
                    obj.skeletal.enabled &&
                    !obj.skeletal.finalMatrices.empty()) {
                    hasRuntimeSkeletal = true;
                }
                if (hasRuntime3DPhysics && hasRuntimeAIAgent && hasRuntimeSkeletal) {
                    break;
                }
            }
        }

        bool simulatePhysics = physics.isReady() &&
                               ((isPlaying && !isPaused) || (!isPlaying && specMode)) &&
                               hasRuntime3DPhysics;
        if (simulatePhysics) {
            physics.simulate(deltaTime, sceneObjects);
        }
        bool runAI = (isPlaying || specMode || testMode) && hasRuntimeAIAgent;
        if (runAI) {
            updateAIAgents(deltaTime);
        }

        if (runtime2DProfileThisFrame) {
            const auto transformStart = Runtime2DClock::now();
            updateHierarchyWorldTransforms();
            profileTransformMs += Runtime2DMsSince(transformStart, Runtime2DClock::now());
        } else {
            updateHierarchyWorldTransforms();
        }
        if (hasRuntimeSkeletal) {
            updateSkinningMatrices();
        }
        if (runtime2DProfileThisFrame) {
            gRuntime2DProfileFrame.update2DTotalMs += Runtime2DMsSince(update2DStart, Runtime2DClock::now());
            gRuntime2DProfileFrame.physics2DTotalMs += profilePhysics2DMs;
            gRuntime2DProfileFrame.transformUpdateMs += profileTransformMs;
        }

        if (playerMode) {
            syncPlayerCamera();
        }

        auto pickRuntimeCameraObject = [this]() -> const SceneObject* {
            const SceneObject* playerCam = nullptr;
            const SceneObject* sceneCam = nullptr;
            const SceneObject* anyCam = nullptr;
            for (const auto& obj : sceneObjects) {
                if (!IsObjectEnabledInHierarchy(obj) || !obj.hasCamera) continue;
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

        const SceneObject* runtimeCameraObject = pickRuntimeCameraObject();

        if (usingVulkan() && vulkanRendererInitialized && vulkanRenderer) {
            if (!showLauncher && projectManager.currentProject.isLoaded) {
                vulkanRenderer->setSkyboxTimeOfDay(getSceneTimeOfDay());

                if (!vulkanMaterialFeatureWarningShown) {
                    const bool hasUnsupportedVulkanMaterials = std::any_of(
                        sceneObjects.begin(), sceneObjects.end(),
                        [](const SceneObject& obj) { return hasUnsupportedVulkanPreviewFeature(obj); });
                    if (hasUnsupportedVulkanMaterials) {
                        addConsoleMessage(
                            "Vulkan preview fallback is active for some scene features. "
                            "Unsupported in Vulkan preview: custom shader pairs and non-cube primitive/mesh geometry.",
                            ConsoleMessageType::Warning);
                        vulkanMaterialFeatureWarningShown = true;
                    }
                }

                vulkanRenderer->setViewportSceneData(
                    sceneObjects,
                    camera,
                    buildSettings.editorCameraFov,
                    buildSettings.editorCameraNear,
                    buildSettings.editorCameraFar);

                if (runtimeCameraObject) {
                    const SceneObject* runtimeCam = runtimeCameraObject;
                    Camera gameCamera = makeCameraFromObject(*runtimeCam);
                    gameCamera.position = runtimeCam->position;
                    vulkanRenderer->setGameSceneData(
                        sceneObjects,
                        &gameCamera,
                        runtimeCam->camera.fov,
                        runtimeCam->camera.nearClip,
                        runtimeCam->camera.farClip);
                } else {
                    vulkanRenderer->clearGameSceneData();
                }
            } else {
                vulkanRenderer->clearViewportSceneData();
                vulkanRenderer->clearGameSceneData();
            }
        }

        bool audioShouldPlay = isPlaying || specMode || testMode;
        Camera listenerCamera = camera;
        if (runtimeCameraObject) {
            const SceneObject* runtimeCam = runtimeCameraObject;
            listenerCamera = makeCameraFromObject(*runtimeCam);
            listenerCamera.position = runtimeCam->position;
        }
        audio.update(sceneObjects, listenerCamera, audioShouldPlay);

        if (!playerMode) {
            updateCompileJob();
            updateAutoCompileScripts();
            processAutoCompileQueue();
        }
        pollExportBuild();

        if (playerMode && !showLauncher) {
            int displayW = 0;
            int displayH = 0;
            glfwGetFramebufferSize(editorWindow, &displayW, &displayH);
            if (displayW > 0 && displayH > 0) {
                int runtimeRenderWidth = kRuntimeInternalWidth;
                int runtimeRenderHeight = kRuntimeInternalHeight;
                getRuntimeInternalResolution(runtimeRenderWidth, runtimeRenderHeight);
                viewportWidth = runtimeRenderWidth;
                viewportHeight = runtimeRenderHeight;
                if (rendererInitialized) {
                    renderer.resize(viewportWidth, viewportHeight);
                } else if (vulkanRendererInitialized && vulkanRenderer) {
                    vulkanRenderer->notifyResize();
                }
            }
        }

        if (playerMode && !showLauncher && projectManager.currentProject.isLoaded && rendererInitialized) {
            Runtime2DClock::time_point renderSubmissionStart;
            if (runtime2DProfileThisFrame) {
                renderSubmissionStart = Runtime2DClock::now();
            }
            int runtimeRenderWidth = kRuntimeInternalWidth;
            int runtimeRenderHeight = kRuntimeInternalHeight;
            getRuntimeInternalResolution(runtimeRenderWidth, runtimeRenderHeight);
            glm::mat4 view = camera.getViewMatrix();
            float renderFov = buildSettings.editorCameraFov;
            float renderNear = buildSettings.editorCameraNear;
            float renderFar = buildSettings.editorCameraFar;
            if (playerMode) {
                if (runtimeCameraObject) {
                    const SceneObject* runtimeCam = runtimeCameraObject;
                    renderFov = runtimeCam->camera.fov;
                    renderNear = std::max(0.01f, runtimeCam->camera.nearClip);
                    renderFar = std::max(renderNear + 0.01f, runtimeCam->camera.farClip);
                }
            }
            glm::mat4 proj = glm::perspective(glm::radians(renderFov),
                                              static_cast<float>(runtimeRenderWidth) /
                                                  static_cast<float>(std::max(1, runtimeRenderHeight)),
                                              renderNear,
                                              renderFar);

            renderer.beginRender(view, proj, camera.position);
            renderer.renderScene(camera, sceneObjects, selectedObjectId,
                                 renderFov,
                                 renderNear,
                                 renderFar,
                                 false,
                                 &selectedObjectIds);
            renderer.endRender();
            if (runtime2DProfileThisFrame) {
                profileRenderSubmissionMs += Runtime2DMsSince(renderSubmissionStart, Runtime2DClock::now());
            }
        }

        if (firstFrame) {
            std::cerr << "[DEBUG] First frame: starting ImGui NewFrame" << std::endl;
        }
        
        if (usingVulkan()) {
#if MODULARITY_HAS_VULKAN
            if (vulkanRendererInitialized &&
                vulkanRenderer &&
                projectManager.currentProject.isLoaded &&
                !showLauncher) {
                if (!vulkanRenderer->prepareFrameResources()) {
                    static bool loggedVulkanPrepareError = false;
                    if (!loggedVulkanPrepareError) {
                        addConsoleMessage("Vulkan scene target preparation failed: " + vulkanRenderer->getLastError(),
                                          ConsoleMessageType::Error);
                        loggedVulkanPrepareError = true;
                    }
                }
            }
            ImGui_ImplVulkan_NewFrame();
#endif
        } else {
            ImGui_ImplOpenGL3_NewFrame();
        }
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        uiCanvas3DInputs.clear();

        if (firstFrame) {
            std::cerr << "[DEBUG] First frame: ImGui NewFrame complete, rendering UI..." << std::endl;
        }

        if (showLauncher) {
            mainDockspaceId = 0;
            if (firstFrame) {
                std::cerr << "[DEBUG] First frame: calling renderLauncher()" << std::endl;
            }
            #ifdef MODULARITY_PLAYER
            renderPlayerViewport();
            #else
            renderLauncher();
            #endif
        } else if (!playerMode) {
            mainDockspaceId = setupDockspace([this]() { renderPlayControlsBar(); });
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
                if (showPixelSpriteEditorWindow) renderPixelSpriteEditorWindow();
                if (showProjectBrowser) renderProjectBrowserPanel();
            }

            if (showBuildSettings) renderBuildSettingsWindow();
            renderScriptEditorWindows();
            renderViewport();
            if (showGameViewport) renderGameViewportWindow();
            if (showConsole) renderConsolePanel();
            renderLatestErrorBar();
        } else {
            mainDockspaceId = 0;
            renderPlayerViewport();
        }

        if (!playerMode) {
            renderTermsOfServiceModal();
            renderDialogs();
        }

        if (firstFrame) {
            std::cerr << "[DEBUG] First frame: UI rendering complete, finalizing frame..." << std::endl;
        }

        updateTouchSwipeScrolling();
        autosaveWorkspaceLayout();
        renderUiCanvas3DTargets();
        ImGui::Render();
        if (runtime2DProfileThisFrame) {
            imguiDrawCounters = CollectImGuiDrawCounters(ImGui::GetDrawData());
        }

        Runtime2DClock::time_point drawRenderStart;
        if (runtime2DProfileThisFrame) {
            drawRenderStart = Runtime2DClock::now();
        }

        if (usingVulkan()) {
            if (vulkanRendererInitialized && vulkanRenderer) {
                const ImVec4 clearColor(0.1f, 0.1f, 0.12f, 1.0f);
                if (!vulkanRenderer->renderFrame(ImGui::GetDrawData(), clearColor)) {
                    static bool loggedVulkanFrameError = false;
                    if (!loggedVulkanFrameError) {
                        addConsoleMessage("Vulkan frame submission failed: " + vulkanRenderer->getLastError(),
                                          ConsoleMessageType::Error);
                        loggedVulkanFrameError = true;
                    }
                }
            }
        } else {
            int displayW = 0;
            int displayH = 0;
            glfwGetFramebufferSize(editorWindow, &displayW, &displayH);
            glViewport(0, 0, displayW, displayH);
            glClearColor(0.1f, 0.1f, 0.12f, 1.00f);
            glClear(GL_COLOR_BUFFER_BIT);

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                const ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
                if (platformIo.Viewports.Size > 1) {
                    GLFWwindow* backup_current_context = glfwGetCurrentContext();
                    ImGui::UpdatePlatformWindows();
                    ImGui::RenderPlatformWindowsDefault();
                    glfwMakeContextCurrent(backup_current_context);
                }
            }
        }
        if (runtime2DProfileThisFrame) {
            profileActualDrawMs += Runtime2DMsSince(drawRenderStart, Runtime2DClock::now());
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

        if (!usingVulkan()) {
            if (runtime2DProfileThisFrame) {
                const auto presentStart = Runtime2DClock::now();
                glfwSwapBuffers(editorWindow);
                gRuntime2DProfileFrame.presentWaitMs += Runtime2DMsSince(presentStart, Runtime2DClock::now());
            } else {
                glfwSwapBuffers(editorWindow);
            }
        }

        if (fpsCapEnabled && fpsCap > 1.0f) {
            double target = 1.0 / fpsCap;
            double frameEnd = glfwGetTime();
            double elapsed = frameEnd - frameStart;
            if (elapsed < target) {
                int sleepMs = static_cast<int>((target - elapsed) * 1000.0);
                if (sleepMs > 0) {
                    if (runtime2DProfileThisFrame) {
                        const auto sleepStart = Runtime2DClock::now();
                        ImGui_ImplGlfw_Sleep(sleepMs);
                        gRuntime2DProfileFrame.fpsCapSleepMs += Runtime2DMsSince(sleepStart, Runtime2DClock::now());
                    } else {
                        ImGui_ImplGlfw_Sleep(sleepMs);
                    }
                }
            }
        }

        if (runtime2DProfileThisFrame) {
            gRuntime2DProfileFrame.renderSubmissionMs += profileRenderSubmissionMs;
            gRuntime2DProfileFrame.actualDrawRenderMs += profileActualDrawMs;

            uint64_t rendererTextureBinds = 0;
            uint64_t rendererStateBinds = 0;
            ModuRuntime2DRenderCounters_Read(&rendererTextureBinds, &rendererStateBinds);

            uint64_t rendererDrawCalls = 0;
            if (rendererInitialized) {
                rendererDrawCalls = static_cast<uint64_t>(std::max(0, renderer.getLastViewportStats().drawCalls));
            }

            gRuntime2DProfileFrame.drawCallCount += rendererDrawCalls + imguiDrawCounters.drawCalls;
            gRuntime2DProfileFrame.textureBindCount += rendererTextureBinds + imguiDrawCounters.textureBinds;
            gRuntime2DProfileFrame.stateBindCount += rendererStateBinds + imguiDrawCounters.stateBinds;
            gRuntime2DProfileFrame.totalFrameMs += Runtime2DMsSince(frameStartClock, Runtime2DClock::now());

            accumulateRuntime2DFrame();
            emitRuntime2DProfileSummaryIfReady(glfwGetTime());
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
    if (mainContext && !playerMode && projectManager.currentProject.isLoaded && !showLauncher) {
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
        if (entry.second.backendReady && !usingVulkan()) {
            ImGui_ImplOpenGL3_Shutdown();
            entry.second.backendReady = false;
        }
        ImGui::DestroyContext(entry.second.context);
    }
    uiCanvas3DContexts.clear();

    if (mainContext) {
        ImGui::SetCurrentContext(mainContext);
#if MODULARITY_HAS_VULKAN
        if (usingVulkan()) {
            if (vulkanRenderer) {
                vulkanRenderer->shutdownImGuiBackend();
            }
        } else
#endif
        {
            ImGui_ImplOpenGL3_Shutdown();
        }
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(mainContext);
    }

    if (vulkanRenderer) {
        vulkanRenderer->shutdown();
        vulkanRenderer.reset();
    }
    vulkanRendererInitialized = false;
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
        if (meshEditAsset.materialSlots.empty()) {
            meshEditAsset.materialSlots.push_back("Default");
        }
        if (meshEditAsset.faceMaterialIndices.size() != meshEditAsset.faces.size()) {
            meshEditAsset.faceMaterialIndices.resize(meshEditAsset.faces.size(), 0u);
        }
        if (meshEditActiveMaterialSlot < 0 || meshEditActiveMaterialSlot >= static_cast<int>(meshEditAsset.materialSlots.size())) {
            meshEditActiveMaterialSlot = 0;
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
    if (meshEditAsset.materialSlots.empty()) {
        meshEditAsset.materialSlots.push_back("Default");
    }
    if (meshEditAsset.faceMaterialIndices.size() != meshEditAsset.faces.size()) {
        meshEditAsset.faceMaterialIndices.resize(meshEditAsset.faces.size(), 0u);
    }
    meshEditActiveMaterialSlot = std::clamp(meshEditActiveMaterialSlot, 0, static_cast<int>(meshEditAsset.materialSlots.size()) - 1);
    meshEditSelectedVertices.clear();
    meshEditSelectedEdges.clear();
    meshEditSelectedFaces.clear();
    return true;
}

bool Engine::syncMeshEditToGPU(SceneObject* obj) {
    if (!obj || !meshEditLoaded) return false;
    if (obj->meshId < 0 && !obj->meshPath.empty()) {
        ModelLoadResult loaded = getModelLoader().loadModel(obj->meshPath);
        if (loaded.success) {
            obj->meshId = loaded.meshIndex;
        } else {
            addConsoleMessage("Mesh GPU sync failed: " + loaded.errorMessage, ConsoleMessageType::Error);
            return false;
        }
    }
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

    const bool textInputFocused = ImGui::GetIO().WantTextInput;
    static bool deletePressed = false;
    if (!textInputFocused && glfwGetKey(editorWindow, GLFW_KEY_DELETE) == GLFW_PRESS && !deletePressed) {
        deleteSelected();
        deletePressed = true;
    }
    if (glfwGetKey(editorWindow, GLFW_KEY_DELETE) == GLFW_RELEASE) {
        deletePressed = false;
    }

    static bool ctrlCPressed = false;
    if (!textInputFocused && ctrlDown && !shiftDown && glfwGetKey(editorWindow, GLFW_KEY_C) == GLFW_PRESS && !ctrlCPressed) {
        copySelected();
        ctrlCPressed = true;
    }
    if (glfwGetKey(editorWindow, GLFW_KEY_C) == GLFW_RELEASE) {
        ctrlCPressed = false;
    }

    static bool ctrlVPressed = false;
    if (!textInputFocused && ctrlDown && !shiftDown && glfwGetKey(editorWindow, GLFW_KEY_V) == GLFW_PRESS && !ctrlVPressed) {
        pasteClipboard();
        ctrlVPressed = true;
    }
    if (glfwGetKey(editorWindow, GLFW_KEY_V) == GLFW_RELEASE) {
        ctrlVPressed = false;
    }

    static bool ctrlAPressed = false;
    if (!textInputFocused && ctrlDown && !shiftDown && glfwGetKey(editorWindow, GLFW_KEY_A) == GLFW_PRESS && !ctrlAPressed) {
        selectAllObjects();
        ctrlAPressed = true;
    }
    if (glfwGetKey(editorWindow, GLFW_KEY_A) == GLFW_RELEASE) {
        ctrlAPressed = false;
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
        addConsoleMessage(std::string("Selection collider bounds ") + (collisionWireframe ? "enabled" : "disabled"),
                          ConsoleMessageType::Info);
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
    if (runtimeScriptBindingsCachedVersion != runtimeScriptBindingsVersion) {
        rebuildRuntimeScriptBindings();
    }
    if (runtimeScriptBindings.empty()) return;
    refreshSceneObjectIndexCache();

    for (const RuntimeScriptBinding& binding : runtimeScriptBindings) {
        auto objIt = sceneObjectIndexById.find(binding.objectId);
        if (objIt == sceneObjectIndexById.end()) continue;
        SceneObject& obj = sceneObjects[objIt->second];
        if (!IsObjectEnabledInHierarchy(obj)) continue;
        if (binding.scriptIndex >= obj.scripts.size()) continue;
        ScriptComponent& sc = obj.scripts[binding.scriptIndex];
        if (!sc.enabled) continue;
        if (sc.path.empty()) continue;

        ScriptContext ctx;
        ctx.engine = this;
        ctx.object = &obj;
        ctx.script = &sc;
        if (sc.language == ScriptLanguage::CSharp) {
            fs::path assembly;
            if (!sc.lastBinaryPath.empty()) {
                if (sc.lastBinaryVerified) {
                    assembly = fs::path(sc.lastBinaryPath);
                } else {
                    fs::path cachedAssembly = sc.lastBinaryPath;
                    if (fs::exists(cachedAssembly)) {
                        assembly = std::move(cachedAssembly);
                        sc.lastBinaryVerified = true;
                    }
                }
            }
            if (assembly.empty()) {
                assembly = resolveManagedAssembly(sc.path);
                sc.lastBinaryPath = assembly.string();
                std::error_code ec;
                sc.lastBinaryVerified = !assembly.empty() && fs::exists(assembly, ec) && !ec;
            }
            if (assembly.empty()) continue;
            if (!sc.lastBinaryVerified) {
                std::error_code ec;
                if (!fs::exists(assembly, ec) || ec) continue;
                sc.lastBinaryVerified = true;
            }
            managedRuntime.tickModule(assembly, sc.managedType, ctx, delta, specMode, testMode);
        } else {
            fs::path binary;
            if (!sc.lastBinaryPath.empty()) {
                if (sc.lastBinaryVerified) {
                    binary = fs::path(sc.lastBinaryPath);
                } else {
                    fs::path cachedBinary = sc.lastBinaryPath;
                    if (fs::exists(cachedBinary)) {
                        binary = std::move(cachedBinary);
                        sc.lastBinaryVerified = true;
                    }
                }
            }
            if (binary.empty()) {
                binary = resolveScriptBinary(sc.path);
                sc.lastBinaryPath = binary.string();
            }
            bool hasBinary = !binary.empty();
            if (!hasBinary || !sc.lastBinaryVerified) {
                std::error_code ec;
                hasBinary = !binary.empty() && fs::exists(binary, ec) && !ec;
                sc.lastBinaryVerified = hasBinary;
            }
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
    auto addSourceTo = [&](std::unordered_set<std::string>& target, const fs::path& path) {
        if (path.empty()) return;
        std::error_code ec;
        fs::path absPath = fs::absolute(path, ec);
        if (ec) absPath = path;
        target.insert(absPath.lexically_normal().string());
    };

    bool hasManagedScripts = false;
    for (const auto& obj : sceneObjects) {
        for (const auto& sc : obj.scripts) {
            if (sc.language == ScriptLanguage::CSharp) {
                hasManagedScripts = true;
                continue;
            }
            if (sc.path.empty()) continue;
            addSourceTo(sources, sc.path);
        }
    }

    if (now - scriptAutoCompileLastDirectoryScan >= scriptAutoCompileDirectoryScanInterval) {
        scriptAutoCompileLastDirectoryScan = now;
        scriptAutoCompileDiscoveredSources.clear();

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
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" ||
                    ext == ".moducpp") {
                    addSourceTo(scriptAutoCompileDiscoveredSources, it->path());
                }
            }
        }
    }

    sources.insert(scriptAutoCompileDiscoveredSources.begin(), scriptAutoCompileDiscoveredSources.end());

    for (auto it = scriptAutoCompileCheckedSourceTime.begin();
         it != scriptAutoCompileCheckedSourceTime.end();) {
        if (sources.find(it->first) == sources.end()) {
            it = scriptAutoCompileCheckedSourceTime.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = scriptAutoCompileBinaryCache.begin();
         it != scriptAutoCompileBinaryCache.end();) {
        if (sources.find(it->first) == sources.end()) {
            it = scriptAutoCompileBinaryCache.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = scriptLastAutoCompileTime.begin();
         it != scriptLastAutoCompileTime.end();) {
        if (sources.find(it->first) == sources.end()) {
            it = scriptLastAutoCompileTime.erase(it);
        } else {
            ++it;
        }
    }

    for (const auto& sourceKey : sources) {
        fs::path sourcePath = sourceKey;
        std::error_code sourceEc;
        if (!fs::exists(sourcePath, sourceEc)) continue;
        auto sourceTime = fs::last_write_time(sourcePath, sourceEc);
        if (sourceEc) continue;

        fs::path binaryPath;
        auto checkedIt = scriptAutoCompileCheckedSourceTime.find(sourceKey);
        auto cachedBinaryIt = scriptAutoCompileBinaryCache.find(sourceKey);
        bool canUseCachedBinary = (checkedIt != scriptAutoCompileCheckedSourceTime.end() &&
                                   checkedIt->second == sourceTime &&
                                   cachedBinaryIt != scriptAutoCompileBinaryCache.end());
        if (canUseCachedBinary) {
            binaryPath = cachedBinaryIt->second;
        } else {
            ScriptBuildCommands commands;
            if (!scriptCompiler.makeCommands(config, sourcePath, commands, error)) {
                continue;
            }
            binaryPath = commands.binaryPath;
            scriptAutoCompileBinaryCache[sourceKey] = binaryPath;
            scriptAutoCompileCheckedSourceTime[sourceKey] = sourceTime;
        }

        std::error_code binEc;
        bool binaryExists = !binaryPath.empty() && fs::exists(binaryPath, binEc);
        fs::file_time_type binaryTime{};
        if (binaryExists && !binEc) {
            binaryTime = fs::last_write_time(binaryPath, binEc);
        }

        if (binaryExists && !binEc && sourceTime <= binaryTime) continue;

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
            fs::path managedDir = managedProject.parent_path();
            bool refreshManagedScan =
                (managedDir != managedAutoCompileCachedProjectDir) ||
                (now - managedAutoCompileLastScan >= managedAutoCompileScanInterval);
            if (refreshManagedScan) {
                managedAutoCompileCachedProjectDir = managedDir;
                managedAutoCompileLastScan = now;
                managedAutoCompileHasSource = false;
                managedAutoCompileNewestSource = fs::file_time_type{};

                std::error_code scanEc;
                if (fs::exists(managedDir, scanEc)) {
                    for (auto it = fs::recursive_directory_iterator(managedDir, scanEc);
                         it != fs::recursive_directory_iterator(); ++it) {
                        if (it->is_directory()) {
                            if (isManagedGeneratedPath(it->path())) {
                                it.disable_recursion_pending();
                            }
                            continue;
                        }
                        if (it->path().extension() != ".cs") continue;
                        if (isManagedGeneratedPath(it->path())) continue;
                        auto sourceTime = fs::last_write_time(it->path(), scanEc);
                        if (scanEc) continue;
                        if (!managedAutoCompileHasSource || sourceTime > managedAutoCompileNewestSource) {
                            managedAutoCompileNewestSource = sourceTime;
                            managedAutoCompileHasSource = true;
                        }
                    }
                }
            }

            bool needsManaged = false;
            if (!fs::exists(managedOutput, managedEc)) {
                needsManaged = true;
            } else if (managedAutoCompileHasSource && !managedEc) {
                auto binaryTime = fs::last_write_time(managedOutput, managedEc);
                if (!managedEc && managedAutoCompileNewestSource > binaryTime) {
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
        if (IsObjectEnabledInHierarchy(obj) && obj.hasPlayerController && obj.playerController.enabled) {
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
    const bool profileEnabled = gRuntime2DProfileEnabled;
    uint64_t profileCollisionTests = 0;
    Runtime2DClock::time_point broadphaseStart;
    Runtime2DClock::time_point broadphaseEnd;
    Runtime2DClock::time_point narrowphaseStart;
    Runtime2DClock::time_point narrowphaseEnd;

    refreshSceneObjectIndexCache();
    const float gravity = -9.81f;
    const float minEdgeThickness = 0.01f;
    struct UiHierarchyCache {
        const std::vector<SceneObject>& objects;
        const std::unordered_map<int, size_t>& indexById;
        std::unordered_map<int, glm::vec2> worldPositionCache;

        UiHierarchyCache(const std::vector<SceneObject>& objects,
                         const std::unordered_map<int, size_t>& indexById)
            : objects(objects), indexById(indexById) {
            worldPositionCache.reserve(objects.size());
        }

        glm::vec2 getWorldPosition(const SceneObject& obj) {
            if (!(obj.hasUI && obj.ui.type != UIElementType::None)) {
                return glm::vec2(obj.position.x, obj.position.y);
            }

            auto cached = worldPositionCache.find(obj.id);
            if (cached != worldPositionCache.end()) {
                return cached->second;
            }

            glm::vec2 pos(obj.ui.position.x, obj.ui.position.y);
            if (obj.parentId >= 0) {
                auto it = indexById.find(obj.parentId);
                if (it != indexById.end()) {
                    pos += getWorldPosition(objects[it->second]);
                }
            }

            worldPositionCache.emplace(obj.id, pos);
            return pos;
        }

        glm::vec2 getParentOffset(const SceneObject& obj) {
            if (obj.parentId < 0) {
                return glm::vec2(0.0f);
            }

            auto it = indexById.find(obj.parentId);
            if (it == indexById.end()) {
                return glm::vec2(0.0f);
            }
            return getWorldPosition(objects[it->second]);
        }
    };
    UiHierarchyCache uiHierarchyCache(sceneObjects, sceneObjectIndexById);
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
    float broadPhaseExtentSum = 0.0f;
    size_t broadPhaseExtentCount = 0;
    for (auto& obj : sceneObjects) {
        if (!IsObjectEnabledInHierarchy(obj) || !HasUIComponent(obj)) continue;
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
        body.pivotWorld = uiHierarchyCache.getParentOffset(obj) + obj.ui.position;
        body.rotationRad = glm::radians(obj.ui.rotation);
        float c = std::cos(body.rotationRad);
        float s = std::sin(body.rotationRad);
        glm::vec2 size = glm::vec2(std::max(1.0f, obj.ui.size.x), std::max(1.0f, obj.ui.size.y));
        Collider2DType type = Collider2DType::Box;
        glm::vec2 boxSize = size;
        glm::vec2 colliderOffset(0.0f);
        std::vector<glm::vec2> localPoints;
        bool closed = false;
        float edgeThickness = minEdgeThickness;
        if (hasCollider2D) {
            type = obj.collider2D.type;
            boxSize = obj.collider2D.boxSize;
            colliderOffset = obj.collider2D.offset;
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
                glm::vec2(-half.x, -half.y) + colliderOffset,
                glm::vec2( half.x, -half.y) + colliderOffset,
                glm::vec2( half.x,  half.y) + colliderOffset,
                glm::vec2(-half.x,  half.y) + colliderOffset
            };
        } else if (type == Collider2DType::Polygon) {
            if (localPoints.empty()) {
                float radius = 0.5f * std::min(boxSize.x, boxSize.y);
                buildHexagon(radius, localPoints);
            }
        } else if (type == Collider2DType::Edge) {
            if (localPoints.size() < 2) {
                float half = boxSize.x * 0.5f;
                localPoints = {
                    glm::vec2(-half, 0.0f) + colliderOffset,
                    glm::vec2(half, 0.0f) + colliderOffset
                };
            }
        }
        if (type != Collider2DType::Box && (colliderOffset.x != 0.0f || colliderOffset.y != 0.0f)) {
            for (glm::vec2& point : localPoints) {
                point += colliderOffset;
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
            bool hasBounds = false;
            for (const auto& seg : body.segments) {
                const float halfThickness = std::max(minEdgeThickness, body.edgeThickness) * 0.5f;
                glm::vec2 segMin(std::min(seg.first.x, seg.second.x) - halfThickness,
                                 std::min(seg.first.y, seg.second.y) - halfThickness);
                glm::vec2 segMax(std::max(seg.first.x, seg.second.x) + halfThickness,
                                 std::max(seg.first.y, seg.second.y) + halfThickness);
                if (!hasBounds) {
                    body.aabbMin = segMin;
                    body.aabbMax = segMax;
                    hasBounds = true;
                } else {
                    body.aabbMin = glm::min(body.aabbMin, segMin);
                    body.aabbMax = glm::max(body.aabbMax, segMax);
                }
            }
        } else {
            body.poly.reserve(localPoints.size());
            for (const auto& p : localPoints) {
                body.poly.push_back(rotatePoint(p, c, s) + body.pivotWorld);
            }
            computeAabb(body.poly, body.aabbMin, body.aabbMax);
        }
        glm::vec2 bodySize = body.aabbMax - body.aabbMin;
        broadPhaseExtentSum += std::max(bodySize.x, bodySize.y);
        ++broadPhaseExtentCount;
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

    if (profileEnabled) {
        broadphaseStart = Runtime2DClock::now();
    }
    const float broadPhaseCellSize = std::clamp(
        broadPhaseExtentCount > 0 ? (broadPhaseExtentSum / static_cast<float>(broadPhaseExtentCount)) : 64.0f,
        16.0f,
        512.0f);
    auto makeCellKey = [](int x, int y) -> uint64_t {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
               static_cast<uint32_t>(y);
    };
    struct BroadPhaseCellEntry {
        uint64_t key = 0;
        int bodyIndex = -1;
    };
    static thread_local std::vector<BroadPhaseCellEntry> broadPhaseEntries;
    static thread_local std::vector<uint64_t> candidatePairs;
    broadPhaseEntries.clear();
    candidatePairs.clear();
    broadPhaseEntries.reserve(bodies.size() * 4);
    candidatePairs.reserve(bodies.size() * 8);

    for (size_t bodyIndex = 0; bodyIndex < bodies.size(); ++bodyIndex) {
        const Body2DRef& body = bodies[bodyIndex];
        int minCellX = static_cast<int>(std::floor(body.aabbMin.x / broadPhaseCellSize));
        int maxCellX = static_cast<int>(std::floor(body.aabbMax.x / broadPhaseCellSize));
        int minCellY = static_cast<int>(std::floor(body.aabbMin.y / broadPhaseCellSize));
        int maxCellY = static_cast<int>(std::floor(body.aabbMax.y / broadPhaseCellSize));
        for (int cellY = minCellY; cellY <= maxCellY; ++cellY) {
            for (int cellX = minCellX; cellX <= maxCellX; ++cellX) {
                BroadPhaseCellEntry entry;
                entry.key = makeCellKey(cellX, cellY);
                entry.bodyIndex = static_cast<int>(bodyIndex);
                broadPhaseEntries.push_back(entry);
            }
        }
    }

    std::sort(broadPhaseEntries.begin(), broadPhaseEntries.end(),
              [](const BroadPhaseCellEntry& a, const BroadPhaseCellEntry& b) {
                  if (a.key != b.key) return a.key < b.key;
                  return a.bodyIndex < b.bodyIndex;
              });

    size_t runStart = 0;
    while (runStart < broadPhaseEntries.size()) {
        size_t runEnd = runStart + 1;
        const uint64_t cellKey = broadPhaseEntries[runStart].key;
        while (runEnd < broadPhaseEntries.size() && broadPhaseEntries[runEnd].key == cellKey) {
            ++runEnd;
        }

        for (size_t i = runStart; i < runEnd; ++i) {
            for (size_t j = i + 1; j < runEnd; ++j) {
                int idxA = broadPhaseEntries[i].bodyIndex;
                int idxB = broadPhaseEntries[j].bodyIndex;
                if (idxA == idxB) continue;
                uint32_t aIndex = static_cast<uint32_t>(std::min(idxA, idxB));
                uint32_t bIndex = static_cast<uint32_t>(std::max(idxA, idxB));
                candidatePairs.push_back((static_cast<uint64_t>(aIndex) << 32) | bIndex);
            }
        }
        runStart = runEnd;
    }

    std::sort(candidatePairs.begin(), candidatePairs.end());
    candidatePairs.erase(std::unique(candidatePairs.begin(), candidatePairs.end()), candidatePairs.end());
    if (profileEnabled) {
        broadphaseEnd = Runtime2DClock::now();
        narrowphaseStart = broadphaseEnd;
        gRuntime2DProfileFrame.collisionPairCandidateCount += static_cast<uint64_t>(candidatePairs.size());
    }

    for (uint64_t pairKey : candidatePairs) {
        const size_t i = static_cast<size_t>(pairKey >> 32);
        const size_t j = static_cast<size_t>(pairKey & 0xffffffffu);
        if (i >= bodies.size() || j >= bodies.size()) continue;

        Body2DRef& a = bodies[i];
        Body2DRef& b = bodies[j];
        if (!a.dynamic && !b.dynamic) continue;
        if (a.aabbMax.x <= b.aabbMin.x || a.aabbMin.x >= b.aabbMax.x ||
            a.aabbMax.y <= b.aabbMin.y || a.aabbMin.y >= b.aabbMax.y) {
            continue;
        }

        auto polyVsPoly = [&](Body2DRef& pA, Body2DRef& pB) {
            if (pA.poly.empty() || pB.poly.empty()) return;
            if (profileEnabled) {
                ++profileCollisionTests;
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
            rect.reserve(4);
            for (const auto& seg : edgeBody.segments) {
                segmentRect(seg.first, seg.second, edgeBody.edgeThickness, rect);
                if (rect.size() < 3) continue;
                glm::vec2 rectMin(0.0f);
                glm::vec2 rectMax(0.0f);
                computeAabb(rect, rectMin, rectMax);
                if (polyBody.aabbMax.x <= rectMin.x || polyBody.aabbMin.x >= rectMax.x ||
                    polyBody.aabbMax.y <= rectMin.y || polyBody.aabbMin.y >= rectMax.y) {
                    continue;
                }
                if (profileEnabled) {
                    ++profileCollisionTests;
                }
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
    if (profileEnabled) {
        narrowphaseEnd = Runtime2DClock::now();
        gRuntime2DProfileFrame.broadphaseMs += Runtime2DMsSince(broadphaseStart, broadphaseEnd);
        gRuntime2DProfileFrame.narrowphaseSolveMs += Runtime2DMsSince(narrowphaseStart, narrowphaseEnd);
        gRuntime2DProfileFrame.collisionTestCount += profileCollisionTests;
    }
}

void Engine::updateCameraFollow2D(float delta) {
    if (sceneObjects.empty()) return;
    refreshSceneObjectIndexCache();
    struct UiHierarchyCache {
        const std::vector<SceneObject>& objects;
        const std::unordered_map<int, size_t>& indexById;
        std::unordered_map<int, glm::vec2> worldPositionCache;

        UiHierarchyCache(const std::vector<SceneObject>& objects,
                         const std::unordered_map<int, size_t>& indexById)
            : objects(objects), indexById(indexById) {
            worldPositionCache.reserve(objects.size());
        }

        glm::vec2 getWorldPosition(const SceneObject& obj) {
            if (!(obj.hasUI && obj.ui.type != UIElementType::None)) {
                return glm::vec2(obj.position.x, obj.position.y);
            }

            auto cached = worldPositionCache.find(obj.id);
            if (cached != worldPositionCache.end()) {
                return cached->second;
            }

            glm::vec2 pos(obj.ui.position.x, obj.ui.position.y);
            if (obj.parentId >= 0) {
                auto it = indexById.find(obj.parentId);
                if (it != indexById.end()) {
                    pos += getWorldPosition(objects[it->second]);
                }
            }

            worldPositionCache.emplace(obj.id, pos);
            return pos;
        }
    };
    UiHierarchyCache uiHierarchyCache(sceneObjects, sceneObjectIndexById);

    for (auto& obj : sceneObjects) {
        if (!IsObjectEnabledInHierarchy(obj) || !obj.hasCamera || !obj.hasCameraFollow2D || !obj.cameraFollow2D.enabled) continue;
        if (obj.cameraFollow2D.targetId < 0) continue;
        auto targetIt = sceneObjectIndexById.find(obj.cameraFollow2D.targetId);
        if (targetIt == sceneObjectIndexById.end()) continue;

        const SceneObject& target = sceneObjects[targetIt->second];
        if (!IsObjectEnabledInHierarchy(target)) continue;
        glm::vec2 desired2D = (target.hasUI && target.ui.type != UIElementType::None)
            ? uiHierarchyCache.getWorldPosition(target)
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
            auto parentIt = sceneObjectIndexById.find(obj.parentId);
            if (parentIt != sceneObjectIndexById.end()) {
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

#pragma region Runtime Animation
fs::path Engine::resolveAnimationClipPath(const std::string& storedPath) const {
    if (storedPath.empty()) return {};
    fs::path clipPath(storedPath);
    if (clipPath.is_absolute()) {
        return clipPath.lexically_normal();
    }
    if (projectManager.currentProject.isLoaded && !projectManager.currentProject.projectPath.empty()) {
        return (projectManager.currentProject.projectPath / clipPath).lexically_normal();
    }
    return clipPath.lexically_normal();
}

bool Engine::loadRuntimeAnimationClipFile(const fs::path& path, RuntimeAnimationClip& outClip) const {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    RuntimeAnimationClip loaded;
    std::string token;
    while (in >> token) {
        if (token == "moduanimateVersion") {
            int version = 0;
            in >> version;
            if (version != 1) return false;
        } else if (token == "name") {
            in >> std::quoted(loaded.name);
        } else if (token == "duration") {
            in >> loaded.duration;
        } else if (token == "sampleRate") {
            in >> loaded.sampleRate;
        } else if (token == "bindingCount") {
            size_t bindingCount = 0;
            in >> bindingCount;
            loaded.bindings.reserve(bindingCount);
        } else if (token == "binding") {
            RuntimeAnimBinding binding;
            std::string targetType;
            in >> std::quoted(binding.path) >> std::quoted(targetType);

            std::string trackCountToken;
            size_t trackCount = 0;
            in >> trackCountToken >> trackCount;
            if (trackCountToken != "trackCount") return false;
            binding.tracks.reserve(trackCount);

            for (size_t ti = 0; ti < trackCount; ++ti) {
                std::string trackToken;
                in >> trackToken;
                if (trackToken != "track") return false;

                RuntimeAnimTrack track;
                int visible = 1;
                int locked = 0;
                in >> std::quoted(track.propertyId) >> visible >> locked;
                (void)visible;
                (void)locked;

                std::string keyCountToken;
                size_t keyCount = 0;
                in >> keyCountToken >> keyCount;
                if (keyCountToken != "keyCount") return false;
                track.keys.reserve(keyCount);

                for (size_t ki = 0; ki < keyCount; ++ki) {
                    std::string keyToken;
                    in >> keyToken;
                    if (keyToken != "key") return false;

                    uint64_t uid = 0;
                    int tangentMode = 0;
                    RuntimeAnimKey key;
                    in >> uid >> key.time >> key.value >> key.inTangent >> key.outTangent >> tangentMode >> key.interpolation;
                    (void)uid;
                    (void)tangentMode;
                    key.interpolation = std::clamp(key.interpolation, 0, 2);
                    track.keys.push_back(key);
                }
                std::sort(track.keys.begin(), track.keys.end(), [](const RuntimeAnimKey& a, const RuntimeAnimKey& b) {
                    return a.time < b.time;
                });
                binding.tracks.push_back(std::move(track));
            }
            loaded.bindings.push_back(std::move(binding));
        } else {
            std::string discard;
            std::getline(in, discard);
        }
    }

    if (!in.eof() && in.fail()) return false;
    loaded.duration = std::max(0.01f, loaded.duration);
    loaded.sampleRate = std::clamp(loaded.sampleRate, 1.0f, 240.0f);
    outClip = std::move(loaded);
    return true;
}

const Engine::RuntimeAnimationClip* Engine::getRuntimeAnimationClip(const std::string& storedPath) {
    if (storedPath.empty()) return nullptr;
    const fs::path absPath = resolveAnimationClipPath(storedPath);
    if (absPath.empty()) return nullptr;

    const std::string cacheKey = absPath.generic_string();
    RuntimeClipCacheEntry& entry = runtimeAnimationClipCache[cacheKey];

    std::error_code ec;
    const fs::file_time_type stamp = fs::last_write_time(absPath, ec);
    const bool haveStamp = !ec;
    if (entry.hasWriteTime == haveStamp &&
        (!haveStamp || entry.lastWriteTime == stamp)) {
        return entry.valid ? &entry.clip : nullptr;
    }

    RuntimeAnimationClip loaded;
    const bool ok = haveStamp && loadRuntimeAnimationClipFile(absPath, loaded);
    entry.hasWriteTime = haveStamp;
    entry.lastWriteTime = stamp;
    entry.valid = ok;
    if (ok) {
        entry.clip = std::move(loaded);
    } else {
        entry.clip = RuntimeAnimationClip{};
    }
    return entry.valid ? &entry.clip : nullptr;
}

float Engine::getAnimationDurationForObject(const SceneObject& obj) const {
    const std::string activeClipPath = AnimationGetActiveClipAssetPath(obj.animation);
    if (!activeClipPath.empty()) {
        const fs::path absPath = resolveAnimationClipPath(activeClipPath);
        auto it = runtimeAnimationClipCache.find(absPath.generic_string());
        if (it != runtimeAnimationClipCache.end() && it->second.valid) {
            return std::max(0.01f, it->second.clip.duration);
        }
    }
    return std::max(0.01f, obj.animation.clipLength);
}

bool Engine::applyRuntimeAnimatedProperty(SceneObject& obj, const std::string& propertyId, float value) {
    if (propertyId == "localPosition.x") { obj.localPosition.x = value; obj.localInitialized = true; return true; }
    if (propertyId == "localPosition.y") { obj.localPosition.y = value; obj.localInitialized = true; return true; }
    if (propertyId == "localPosition.z") { obj.localPosition.z = value; obj.localInitialized = true; return true; }
    if (propertyId == "localRotation.x") { obj.localRotation.x = value; obj.localInitialized = true; return true; }
    if (propertyId == "localRotation.y") { obj.localRotation.y = value; obj.localInitialized = true; return true; }
    if (propertyId == "localRotation.z") { obj.localRotation.z = value; obj.localInitialized = true; return true; }
    if (propertyId == "localScale.x") { obj.localScale.x = std::max(0.0001f, value); obj.localInitialized = true; return true; }
    if (propertyId == "localScale.y") { obj.localScale.y = std::max(0.0001f, value); obj.localInitialized = true; return true; }
    if (propertyId == "localScale.z") { obj.localScale.z = std::max(0.0001f, value); obj.localInitialized = true; return true; }

    if (propertyId == "UI.Position.x" && obj.hasUI) { obj.ui.position.x = value; return true; }
    if (propertyId == "UI.Position.y" && obj.hasUI) { obj.ui.position.y = value; return true; }
    if ((propertyId == "UI.Size.x" || propertyId == "UI.Size.y") && obj.hasUI) {
        const float minUiSize = (obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D)
            ? 0.01f
            : 1.0f;
        if (propertyId == "UI.Size.x") {
            obj.ui.size.x = std::max(minUiSize, value);
        } else {
            obj.ui.size.y = std::max(minUiSize, value);
        }
        return true;
    }
    if (propertyId == "UI.Rotation" && obj.hasUI) { obj.ui.rotation = value; return true; }
    if (propertyId == "UI.SliderValue" && obj.hasUI) { obj.ui.sliderValue = std::clamp(value, obj.ui.sliderMin, obj.ui.sliderMax); return true; }
    if (propertyId == "UI.TextScale" && obj.hasUI) { obj.ui.textScale = std::max(0.01f, value); return true; }
    if (propertyId == "UI.SpriteFrame" && obj.hasUI) {
        int frameCount = 1;
        if (obj.ui.spriteCustomFramesEnabled && !obj.ui.spriteCustomFrames.empty()) {
            frameCount = static_cast<int>(obj.ui.spriteCustomFrames.size());
        } else {
            frameCount = std::max(1, obj.ui.spriteSheetColumns * obj.ui.spriteSheetRows);
        }
        obj.ui.spriteSheetFrame = std::clamp(static_cast<int>(std::round(value)), 0, std::max(0, frameCount - 1));
        return true;
    }
    if (propertyId == "UI.SpriteSheetFPS" && obj.hasUI) { obj.ui.spriteSheetFps = std::clamp(value, 1.0f, 120.0f); return true; }
    if (propertyId == "UI.SpriteSheetLoop" && obj.hasUI) { obj.ui.spriteSheetLoop = value >= 0.5f; return true; }

    if (propertyId == "Light.Intensity" && obj.hasLight) { obj.light.intensity = value; return true; }
    if (propertyId == "Light.Range" && obj.hasLight) { obj.light.range = std::max(0.0f, value); return true; }
    if (propertyId == "Light.InnerAngle" && obj.hasLight) { obj.light.innerAngle = std::clamp(value, 0.0f, 180.0f); return true; }
    if (propertyId == "Light.OuterAngle" && obj.hasLight) { obj.light.outerAngle = std::clamp(value, 0.0f, 180.0f); return true; }
    if (propertyId == "Light.Enabled" && obj.hasLight) { obj.light.enabled = value >= 0.5f; return true; }

    if (propertyId == "Camera.FOV" && obj.hasCamera) { obj.camera.fov = std::clamp(value, 1.0f, 179.0f); return true; }
    if (propertyId == "Camera.NearClip" && obj.hasCamera) { obj.camera.nearClip = std::max(0.001f, value); return true; }
    if (propertyId == "Camera.FarClip" && obj.hasCamera) { obj.camera.farClip = std::max(obj.camera.nearClip + 0.01f, value); return true; }
    if (propertyId == "Camera.PixelsPerUnit" && obj.hasCamera) { obj.camera.pixelsPerUnit = std::max(1.0f, value); return true; }

    if (propertyId == "PostFX.BloomIntensity" && obj.hasPostFX) { obj.postFx.bloomIntensity = std::max(0.0f, value); return true; }
    if (propertyId == "PostFX.Exposure" && obj.hasPostFX) { obj.postFx.exposure = value; return true; }
    if (propertyId == "PostFX.Contrast" && obj.hasPostFX) { obj.postFx.contrast = std::max(0.0f, value); return true; }
    if (propertyId == "PostFX.Saturation" && obj.hasPostFX) { obj.postFx.saturation = std::max(0.0f, value); return true; }

    if (propertyId == "Rigidbody.Mass" && obj.hasRigidbody) { obj.rigidbody.mass = std::max(0.001f, value); return true; }

    if (propertyId == "Audio.Volume" && obj.hasAudioSource) { obj.audioSource.volume = std::clamp(value, 0.0f, 2.0f); return true; }
    if (propertyId == "Audio.MinDistance" && obj.hasAudioSource) { obj.audioSource.minDistance = std::max(0.01f, value); return true; }
    if (propertyId == "Audio.MaxDistance" && obj.hasAudioSource) { obj.audioSource.maxDistance = std::max(obj.audioSource.minDistance + 0.01f, value); return true; }
    if (propertyId == "Audio.Loop" && obj.hasAudioSource) { obj.audioSource.loop = value >= 0.5f; return true; }
    if (propertyId == "Audio.Spatial" && obj.hasAudioSource) { obj.audioSource.spatial = value >= 0.5f; return true; }

    if (propertyId == "AIAgent.Speed" && obj.hasAIAgent) { obj.aiAgent.speed = std::max(0.05f, value); return true; }
    if (propertyId == "AIAgent.StoppingDistance" && obj.hasAIAgent) { obj.aiAgent.stoppingDistance = std::max(0.0f, value); return true; }

    int scriptIndex = -1;
    int settingIndex = -1;
    if (std::sscanf(propertyId.c_str(), "ScriptSetting.%d.%d", &scriptIndex, &settingIndex) == 2) {
        if (scriptIndex >= 0 && scriptIndex < static_cast<int>(obj.scripts.size())) {
            auto& script = obj.scripts[scriptIndex];
            if (settingIndex >= 0 && settingIndex < static_cast<int>(script.settings.size())) {
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "%.6g", value);
                script.settings[settingIndex].value = buffer;
                return true;
            }
        }
    }

    return false;
}

void Engine::evaluateRuntimeAnimationClip(const RuntimeAnimationClip& clip, float time, int rootObjectId) {
    SceneObject* root = findObjectById(rootObjectId);
    if (!root) return;

    auto resolvePath = [&](const std::string& path) -> SceneObject* {
        if (path.empty()) return root;
        SceneObject* current = root;
        size_t start = 0;
        while (start <= path.size()) {
            size_t slash = path.find('/', start);
            std::string segment = path.substr(start, slash == std::string::npos ? std::string::npos : (slash - start));
            if (segment.empty()) {
                start = (slash == std::string::npos) ? path.size() + 1 : slash + 1;
                continue;
            }

            SceneObject* next = nullptr;
            for (int childId : current->childIds) {
                SceneObject* child = findObjectById(childId);
                if (child && child->name == segment) {
                    next = child;
                    break;
                }
            }
            if (!next) return nullptr;
            current = next;
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        return current;
    };

    auto sampleTrack = [](const RuntimeAnimTrack& track, float t) -> float {
        if (track.keys.empty()) return 0.0f;
        if (t <= track.keys.front().time) return track.keys.front().value;
        if (t >= track.keys.back().time) return track.keys.back().value;
        for (size_t i = 0; i + 1 < track.keys.size(); ++i) {
            const RuntimeAnimKey& a = track.keys[i];
            const RuntimeAnimKey& b = track.keys[i + 1];
            if (t < a.time || t > b.time) continue;
            float span = b.time - a.time;
            if (span <= 1e-6f) return b.value;
            float u = std::clamp((t - a.time) / span, 0.0f, 1.0f);
            if (a.interpolation == 0) {
                return a.value;
            }
            if (a.interpolation == 2) {
                float u2 = u * u;
                float u3 = u2 * u;
                float h00 = (2.0f * u3) - (3.0f * u2) + 1.0f;
                float h10 = u3 - (2.0f * u2) + u;
                float h01 = (-2.0f * u3) + (3.0f * u2);
                float h11 = u3 - u2;
                float m0 = a.outTangent * span;
                float m1 = b.inTangent * span;
                return (h00 * a.value) + (h10 * m0) + (h01 * b.value) + (h11 * m1);
            }
            return a.value + (b.value - a.value) * u;
        }
        return track.keys.back().value;
    };

    for (const RuntimeAnimBinding& binding : clip.bindings) {
        SceneObject* target = resolvePath(binding.path);
        if (!target) continue;
        for (const RuntimeAnimTrack& track : binding.tracks) {
            if (track.keys.empty()) continue;
            const float value = sampleTrack(track, time);
            applyRuntimeAnimatedProperty(*target, track.propertyId, value);
        }
    }
}

void Engine::updateRuntimeAnimations(float delta) {
    const bool runRuntimeAnimations = isPlaying || specMode || testMode;
    if (!runRuntimeAnimations || sceneObjects.empty()) return;

    for (SceneObject& obj : sceneObjects) {
        if (!IsObjectEnabledInHierarchy(obj)) continue;
        if (!obj.hasAnimation || !obj.animation.enabled) continue;
        NormalizeAnimationClipSlots(obj.animation);
        const std::string activeClipPath = AnimationGetActiveClipAssetPath(obj.animation);
        if (activeClipPath.empty()) continue;

        if (obj.animation.runtimeClipPath != activeClipPath) {
            obj.animation.runtimeClipPath = activeClipPath;
            obj.animation.runtimeTime = 0.0f;
            obj.animation.runtimeDirection = 1.0f;
            obj.animation.runtimePaused = false;
            obj.animation.runtimePlaying = obj.animation.playOnAwake;
            obj.animation.runtimeInitialized = true;
        } else if (!obj.animation.runtimeInitialized) {
            obj.animation.runtimeTime = 0.0f;
            obj.animation.runtimeDirection = 1.0f;
            obj.animation.runtimePaused = false;
            obj.animation.runtimePlaying = obj.animation.playOnAwake;
            obj.animation.runtimeInitialized = true;
        }

        const RuntimeAnimationClip* clip = getRuntimeAnimationClip(activeClipPath);
        if (!clip) continue;

        const float duration = std::max(0.01f, clip->duration);
        obj.animation.clipLength = duration;

        if (obj.animation.runtimePlaying) {
            if (!obj.animation.runtimePaused && delta > 0.0f) {
                float speed = std::max(0.0f, obj.animation.playSpeed);
                float dir = (obj.animation.runtimeDirection < 0.0f) ? -1.0f : 1.0f;
                obj.animation.runtimeTime += delta * speed * dir;
                if (obj.animation.loop) {
                    while (obj.animation.runtimeTime < 0.0f) obj.animation.runtimeTime += duration;
                    while (obj.animation.runtimeTime > duration) obj.animation.runtimeTime -= duration;
                } else {
                    if (obj.animation.runtimeTime <= 0.0f) {
                        obj.animation.runtimeTime = 0.0f;
                        if (dir < 0.0f) obj.animation.runtimePlaying = false;
                    } else if (obj.animation.runtimeTime >= duration) {
                        obj.animation.runtimeTime = duration;
                        if (dir > 0.0f) obj.animation.runtimePlaying = false;
                    }
                }
            }

            const float evalTime = std::clamp(obj.animation.runtimeTime, 0.0f, duration);
            evaluateRuntimeAnimationClip(*clip, evalTime, obj.id);
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
        if (!IsObjectEnabledInHierarchy(obj) || !obj.hasSkeletalAnimation || !obj.skeletal.enabled) continue;
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
        if (!IsObjectEnabledInHierarchy(obj) || !obj.hasSkeletalAnimation || !obj.skeletal.enabled) continue;
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

void Engine::refreshSceneObjectIndexCache() {
    const SceneObject* currentData = sceneObjects.empty() ? nullptr : sceneObjects.data();
    if (sceneObjectIndexData == currentData &&
        sceneObjectIndexCount == sceneObjects.size() &&
        sceneObjectIndexById.size() == sceneObjects.size()) {
        return;
    }

    sceneObjectIndexById.clear();
    sceneObjectIndexById.reserve(sceneObjects.size());
    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        sceneObjectIndexById[sceneObjects[i].id] = i;
    }
    sceneObjectIndexData = currentData;
    sceneObjectIndexCount = sceneObjects.size();
}

void Engine::rebuildRuntimeScriptBindings() {
    runtimeScriptBindings.clear();
    runtimeScriptBindings.reserve(sceneObjects.size());
    for (const auto& obj : sceneObjects) {
        if (obj.scripts.empty()) continue;
        for (size_t i = 0; i < obj.scripts.size(); ++i) {
            runtimeScriptBindings.push_back({obj.id, i});
        }
    }
    runtimeScriptBindingsCachedVersion = runtimeScriptBindingsVersion;
}

void Engine::updateHierarchyWorldTransforms() {
    if (sceneObjects.empty()) return;
    refreshSceneObjectIndexCache();

    bool hasHierarchyLinks = false;
    for (const auto& obj : sceneObjects) {
        if (obj.parentId != -1 || !obj.childIds.empty()) {
            hasHierarchyLinks = true;
            break;
        }
    }

    if (!hasHierarchyLinks) {
        const glm::vec3 rootPos(0.0f);
        const glm::quat rootRot(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::vec3 rootScale(1.0f);
        for (auto& obj : sceneObjects) {
            obj.hierarchyEnabled = true;
            if (!obj.localInitialized) {
                obj.localPosition = obj.position;
                obj.localRotation = NormalizeEulerDegrees(obj.rotation);
                obj.localScale = obj.scale;
                obj.localInitialized = true;
            }

            bool useWorldAuthoritative = obj.hasRigidbody && obj.rigidbody.enabled && !obj.rigidbody.isKinematic;
            if (useWorldAuthoritative) {
                updateLocalFromWorld(obj, rootPos, rootRot, rootScale);
                continue;
            }

            obj.position = obj.localPosition;
            obj.rotation = NormalizeEulerDegrees(obj.localRotation);
            obj.scale = obj.localScale;
        }
        return;
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

    std::function<void(int, const glm::vec3&, const glm::quat&, const glm::vec3&, bool)> processNode =
        [&](int id,
            const glm::vec3& parentPos,
            const glm::quat& parentRot,
            const glm::vec3& parentScale,
            bool parentHierarchyEnabled) {
        if (visited.count(id)) return;
        if (visiting.count(id)) return;
        auto itIndex = sceneObjectIndexById.find(id);
        if (itIndex == sceneObjectIndexById.end()) return;

        visiting.insert(id);
        SceneObject& obj = sceneObjects[itIndex->second];
        obj.hierarchyEnabled = parentHierarchyEnabled;
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

        const bool childParentHierarchyEnabled = IsObjectEnabledInHierarchy(obj);
        for (int childId : obj.childIds) {
            processNode(childId, worldPos, worldRot, worldScale, childParentHierarchyEnabled);
        }

        visiting.erase(id);
        visited.insert(id);
    };

    for (const auto& obj : sceneObjects) {
        if (obj.parentId == -1 || sceneObjectIndexById.find(obj.parentId) == sceneObjectIndexById.end()) {
            processNode(obj.id,
                        glm::vec3(0.0f),
                        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                        glm::vec3(1.0f),
                        true);
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
        applySceneTimeOfDay(0.5f);
        addObject(ObjectType::Cube, "Cube");
        showLauncher = false;
        return;
    }

    if (!SceneSerializer::loadSceneDeferred(scenePath, sceneLoadObjects, sceneLoadNextId, sceneLoadVersion, &sceneLoadTimeOfDay)) {
        sceneLoadInProgress = false;
        addConsoleMessage("Error: Failed to load scene: " + sceneName, ConsoleMessageType::Error);
        applySceneTimeOfDay(0.5f);
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

    recordState("sceneLoaded");
    addConsoleMessage("Loaded scene: " + sceneLoadSceneName, ConsoleMessageType::Success);
    if (sceneLoadTimeOfDay >= 0.0f) {
        applySceneTimeOfDay(sceneLoadTimeOfDay);
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
    vulkanMaterialFeatureWarningShown = false;

    if (projectManager.currentProject.rendererBackend != graphicsBackend) {
        const Modularity::GraphicsBackend targetBackend = projectManager.currentProject.rendererBackend;
        std::string relaunchError;
        if (relaunchEditorWithBackend(targetBackend, result.path, relaunchError)) {
            addConsoleMessage(
                "Project renderer is " + std::string(Modularity::ToString(targetBackend)) +
                ". Restarting editor with matching backend...",
                ConsoleMessageType::Info);
            glfwSetWindowShouldClose(editorWindow, GLFW_TRUE);
            return;
        }

        addConsoleMessage(
            "Project renderer is " + std::string(Modularity::ToString(targetBackend)) +
            ", current session is " + std::string(Modularity::ToString(graphicsBackend)) +
            ". Automatic backend switch failed (" + relaunchError + "). Restart editor to apply project renderer.",
            ConsoleMessageType::Warning);
    }

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
    if (!playerMode) {
        fileBrowser.setProjectRoot(contentRoot);
        fileBrowser.currentPath = contentRoot;
        loadEditorUserSettings();
    }
    applyProjectPipelineDefaults(false);
    fileBrowser.needsRefresh = !playerMode;
    scriptEditorWindowsDirty = true;
    scriptEditorWindows.clear();
    scriptLastAutoCompileTime.clear();
    scriptAutoCompileCheckedSourceTime.clear();
    scriptAutoCompileBinaryCache.clear();
    scriptAutoCompileDiscoveredSources.clear();
    autoCompileQueue.clear();
    autoCompileQueued.clear();
    scriptAutoCompileLastCheck = 0.0;
    scriptAutoCompileLastDirectoryScan = 0.0;
    managedAutoCompileLastScan = 0.0;
    managedAutoCompileCachedProjectDir.clear();
    managedAutoCompileNewestSource = fs::file_time_type{};
    managedAutoCompileHasSource = false;
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
        if (!IsObjectEnabledInHierarchy(obj) || !obj.hasCamera) continue;
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
    autoStartBundlePath.clear();
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
    bool oneShot = false;
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
        } else if (key == "bundle") {
            autoStartBundlePath = value;
        } else if (key == "scene") {
            autoStartSceneName = value;
        } else if (key == "mode") {
            autoStartPlayerMode = (value == "player");
            modeSpecified = true;
        } else if (key == "oneshot") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            oneShot = (lower == "1" || lower == "true" || lower == "yes");
        }
    }

    if (!autoStartProjectPath.empty()) {
        fs::path path = autoStartProjectPath;
        if (path.is_relative() && autoStartBundlePath.empty()) {
            path = fs::current_path() / path;
        }
        autoStartProjectPath = path.lexically_normal().string();
        autoStartRequested = true;
        if (!modeSpecified) {
            autoStartPlayerMode = true;
        }
    }

    if (!autoStartBundlePath.empty()) {
        fs::path bundlePath = autoStartBundlePath;
        if (bundlePath.is_relative()) {
            bundlePath = fs::current_path() / bundlePath;
        }
        autoStartBundlePath = bundlePath.lexically_normal().string();
        autoStartRequested = true;
        if (!modeSpecified) {
            autoStartPlayerMode = true;
        }
    }

    if (oneShot) {
        std::error_code removeEc;
        fs::remove(configPath, removeEc);
    }
}

void Engine::applyAutoStartMode() {
    for (SceneObject& obj : sceneObjects) {
        if (!obj.hasAnimation) continue;
        obj.animation.runtimePlaying = false;
        obj.animation.runtimePaused = false;
        obj.animation.runtimeTime = 0.0f;
        obj.animation.runtimeDirection = 1.0f;
        obj.animation.runtimeInitialized = false;
        obj.animation.runtimeClipPath.clear();
    }
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
    showPixelSpriteEditorWindow = false;
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
    gameViewportResolutionIndex = 0;
    gameViewportCustomWidth = 1920;
    gameViewportCustomHeight = 1080;
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
        } else if (line.rfind("gameViewportResolutionIndex=", 0) == 0) {
            gameViewportResolutionIndex = std::clamp(std::atoi(line.substr(28).c_str()), 0, 4);
        } else if (line.rfind("gameViewportCustomWidth=", 0) == 0) {
            gameViewportCustomWidth = std::clamp(std::atoi(line.substr(24).c_str()), 64, 8192);
        } else if (line.rfind("gameViewportCustomHeight=", 0) == 0) {
            gameViewportCustomHeight = std::clamp(std::atoi(line.substr(25).c_str()), 64, 8192);
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
    file << "gameViewportResolutionIndex=" << std::clamp(gameViewportResolutionIndex, 0, 4) << "\n";
    file << "gameViewportCustomWidth=" << std::clamp(gameViewportCustomWidth, 64, 8192) << "\n";
    file << "gameViewportCustomHeight=" << std::clamp(gameViewportCustomHeight, 64, 8192) << "\n";
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
    fs::path scenesPath = projectManager.currentProject.scenesPath;
    fs::path scriptsPath = projectManager.currentProject.scriptsPath;
    fs::path scriptsConfigPath = resolveScriptsConfigPath(projectManager.currentProject);
    std::vector<std::string> runtimeSceneNames;
    {
        std::unordered_set<std::string> seenScenes;
        for (const auto& scene : buildSettings.scenes) {
            if (!scene.enabled || scene.name.empty()) continue;
            if (seenScenes.insert(scene.name).second) {
                runtimeSceneNames.push_back(scene.name);
            }
        }
        if (!startScene.empty() && seenScenes.insert(startScene).second) {
            runtimeSceneNames.push_back(startScene);
        }
    }
    std::string exportSplashImagePath = buildSettings.splashImagePath;
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
        [this, normalizedOut, exportRoot, archivePath, sourceRoot, projectRoot, startScene,
         scenesPath, scriptsPath, scriptsConfigPath, runtimeSceneNames, exportSplashImagePath,
         assignedNativeScriptSources,
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

        fs::path runtimeStageRoot = exportRoot / "_runtime_stage";
        fs::create_directories(runtimeStageRoot, ec);
        if (ec) {
            result.message = "Failed to create runtime staging directory.";
            return result;
        }

        std::string copyError;
        RuntimeExportStager runtimeStager(projectRoot, runtimeStageRoot);

        setStatus(0.74f, "Staging runtime resources...");
        if (!CopyRuntimeResourcesSubset(sourceRoot, runtimeStageRoot, copyError)) {
            result.message = copyError;
            return result;
        }

        setStatus(0.78f, "Collecting precompiled packages...");
        if (!copyPrecompiledPackages(buildRoot, runtimeStageRoot / "Packages" / "ThirdParty", copyError)) {
            result.message = copyError;
            return result;
        }

        setStatus(0.82f, "Collecting engine cache...");
        if (!copyPrecompiledEnginePackages(buildRoot, runtimeStageRoot / "Packages" / "Engine", copyError)) {
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
                    return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" ||
                           ext == ".c++" || ext == ".moducpp";
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

        setStatus(0.85f, "Staging runtime project...");
        if (!CopyFileIntoRuntimeRoot(projectRoot / "project.modu", runtimeStageRoot, "project.modu", copyError)) {
            result.message = copyError;
            return result;
        }

        if (!runtimeSceneNames.empty()) {
            const float sceneCount = static_cast<float>(runtimeSceneNames.size());
            for (size_t i = 0; i < runtimeSceneNames.size(); ++i) {
                if (exportCancelRequested.load()) {
                    result.message = "Export cancelled.";
                    result.success = false;
                    return result;
                }

                const std::string& sceneName = runtimeSceneNames[i];
                const fs::path sourceScenePath = scenesPath / (sceneName + ".scene");
                const float progressBase = 0.86f;
                const float progressRange = 0.04f;
                const float progress = progressBase +
                    (progressRange * static_cast<float>(i) / std::max(1.0f, sceneCount));
                setStatus(progress, "Serializing runtime scene: " + sceneName + "...");
                if (!StageRuntimeScene(sourceScenePath, sceneName, runtimeStager, runtimeStageRoot, copyError)) {
                    result.message = copyError;
                    return result;
                }
            }
        }

        std::string stagedSplashPath = exportSplashImagePath;
        if (!stagedSplashPath.empty()) {
            if (!runtimeStager.stageFileReference(stagedSplashPath, "Splash", copyError)) {
                result.message = copyError;
                return result;
            }
        }

        {
            const fs::path buildSettingsSource = projectRoot / "build.modu";
            if (fs::exists(buildSettingsSource)) {
                std::ifstream in(buildSettingsSource);
                if (!in.is_open()) {
                    result.message = "Failed to read build.modu for runtime export.";
                    return result;
                }

                std::vector<std::string> lines;
                std::string line;
                bool replacedSplash = false;
                while (std::getline(in, line)) {
                    if (line.rfind("splashImage=", 0) == 0) {
                        lines.push_back("splashImage=" + stagedSplashPath);
                        replacedSplash = true;
                    } else {
                        lines.push_back(line);
                    }
                }
                if (!replacedSplash && !stagedSplashPath.empty()) {
                    lines.push_back("splashImage=" + stagedSplashPath);
                }

                const fs::path buildSettingsDest = runtimeStageRoot / "build.modu";
                if (!EnsureDirectoryForFile(buildSettingsDest, copyError)) {
                    result.message = copyError;
                    return result;
                }
                std::ofstream out(buildSettingsDest, std::ios::trunc);
                if (!out.is_open()) {
                    result.message = "Failed to write staged build.modu.";
                    return result;
                }
                for (const std::string& entry : lines) {
                    out << entry << "\n";
                }
                out.close();
            }
        }

        std::vector<fs::path> projectFiles = {
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
            if (!CopyFileIntoRuntimeRoot(src, runtimeStageRoot, src.filename(), copyError)) {
                result.message = copyError;
                return result;
            }
        }

        fs::path compiledScriptsSrc;
        fs::path compiledScriptsDstRelative;
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
                        compiledScriptsDstRelative = relOutDir;
                    }
                }
                if (compiledScriptsDstRelative.empty()) {
                    compiledScriptsDstRelative = fs::path("Library") / "CompiledScripts";
                }
            }
        }
        if (compiledScriptsSrc.empty()) {
            compiledScriptsSrc = projectRoot / "Library" / "CompiledScripts";
            compiledScriptsDstRelative = fs::path("Library") / "CompiledScripts";
        }
        if (fs::exists(compiledScriptsSrc)) {
            if (!CopyDirectoryIntoRuntimeRoot(compiledScriptsSrc, runtimeStageRoot, compiledScriptsDstRelative, copyError)) {
                result.message = copyError;
                return result;
            }
        }
        if (fs::exists(projectRoot / "Library" / "InstalledPackages")) {
            if (!CopyDirectoryIntoRuntimeRoot(projectRoot / "Library" / "InstalledPackages",
                                              runtimeStageRoot,
                                              fs::path("Library") / "InstalledPackages",
                                              copyError)) {
                result.message = copyError;
                return result;
            }
        }

        {
            const fs::path managedProject = projectRoot / "Scripts" / "Managed" / "ModuCPP.csproj";
            if (fs::exists(managedProject)) {
                if (!CopyFileIntoRuntimeRoot(managedProject,
                                             runtimeStageRoot,
                                             fs::path("Scripts") / "Managed" / "ModuCPP.csproj",
                                             copyError)) {
                    result.message = copyError;
                    return result;
                }

                const fs::path managedOutputDir = managedOutputPathFromProject(managedProject).parent_path();
                if (fs::exists(managedOutputDir)) {
                    if (!CopyDirectoryIntoRuntimeRoot(managedOutputDir,
                                                      runtimeStageRoot,
                                                      fs::path("Scripts") / "Managed" / "bin" / "Debug" / "netstandard2.0",
                                                      copyError)) {
                        result.message = copyError;
                        return result;
                    }
                }
            }
        }

        setStatus(0.92f, "Packing runtime content...");
        std::vector<RuntimeBundleEntry> bundleEntries = CollectRuntimeBundleEntries(runtimeStageRoot);
        if (bundleEntries.empty()) {
            result.message = "Runtime bundle staging produced no files.";
            return result;
        }
        if (!WriteRuntimeContentBundle(exportRoot / "content.modbundle", bundleEntries, copyError)) {
            result.message = copyError;
            return result;
        }

        fs::remove_all(runtimeStageRoot, ec);
        if (ec) {
            result.message = "Failed to clean runtime staging directory.";
            return result;
        }

        fs::path autoStartPath = exportRoot / "autostart.modu";
        std::ofstream autoStart(autoStartPath);
        if (!autoStart.is_open()) {
            result.message = "Failed to write autostart.modu.";
            return result;
        }
        autoStart << "bundle=content.modbundle\n";
        autoStart << "project=project.modu\n";
        if (!startScene.empty()) {
            autoStart << "scene=" << startScene << "\n";
        }
        autoStart << "mode=player\n";
        autoStart.close();

        fs::path buildAutoStartPath = buildRoot / "autostart.modu";
        std::ofstream buildAutoStart(buildAutoStartPath);
        if (buildAutoStart.is_open()) {
            buildAutoStart << "bundle=" << (exportRoot / "content.modbundle").string() << "\n";
            buildAutoStart << "project=project.modu\n";
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
    newProject.pipeline = (projectManager.newProjectPipelineMode == 1)
        ? ProjectPipeline::Pipeline2D
        : ProjectPipeline::Pipeline3D;
    newProject.rendererBackend = (projectManager.newProjectRendererMode == 1)
        ? Modularity::GraphicsBackend::Vulkan
        : Modularity::GraphicsBackend::OpenGL;
#if !MODULARITY_HAS_VULKAN
    if (newProject.rendererBackend == Modularity::GraphicsBackend::Vulkan) {
        newProject.rendererBackend = Modularity::GraphicsBackend::OpenGL;
        addConsoleMessage("Vulkan was selected for new project, but this build does not include Vulkan. Using OpenGL.",
                          ConsoleMessageType::Warning);
    }
#endif
    if (newProject.create()) {
        if (!projectManager.newProjectTemplatePath.empty()) {
            std::string templateError;
            if (!applyTemplateProject(fs::path(projectManager.newProjectTemplatePath),
                                      newProject,
                                      name,
                                      newProject.pipeline,
                                      newProject.rendererBackend,
                                      templateError)) {
                projectManager.errorMessage = templateError;
                return;
            }
        }

        bool importedPackages = false;
        std::string importedFromProject;
        if (projectManager.newProjectImportLastPackages) {
            std::string importError;
            if (importInstalledPackagesFromRecent(projectManager,
                                                  newProject.projectPath,
                                                  importedFromProject,
                                                  importError)) {
                importedPackages = true;
            } else if (!importError.empty()) {
                addConsoleMessage("Installed package import skipped: " + importError,
                                  ConsoleMessageType::Warning);
            }
        }

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
        scriptAutoCompileCheckedSourceTime.clear();
        scriptAutoCompileBinaryCache.clear();
        scriptAutoCompileDiscoveredSources.clear();
        autoCompileQueue.clear();
        autoCompileQueued.clear();
        scriptAutoCompileLastCheck = 0.0;
        scriptAutoCompileLastDirectoryScan = 0.0;
        managedAutoCompileLastScan = 0.0;
        managedAutoCompileCachedProjectDir.clear();
        managedAutoCompileNewestSource = fs::file_time_type{};
        managedAutoCompileHasSource = false;

        showLauncher = false;
        firstFrame = true;

        addConsoleMessage("Created new project: " + std::string(name), ConsoleMessageType::Success);
        addConsoleMessage("Project location: " + newProject.projectPath.string(), ConsoleMessageType::Info);
        addConsoleMessage("Pipeline: " + std::string(isProject2DPipeline() ? "2D" : "3D"), ConsoleMessageType::Info);
        if (importedPackages) {
            addConsoleMessage("Imported installed packages from: " + importedFromProject, ConsoleMessageType::Info);
        }

        saveCurrentScene();
        loadBuildSettings();
        applyProjectPipelineDefaults(true);
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
    float timeOfDay = getSceneTimeOfDay();
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
        markRuntimeScriptBindingsDirty();
        initializeLocalTransformsFromWorld(sceneVersion);
        rebuildSkeletalBindings();
        undoStack.clear();
        redoStack.clear();
        projectManager.currentProject.currentSceneName = sceneName;
        projectManager.currentProject.hasUnsavedChanges = false;
        projectManager.currentProject.saveProjectFile();
        clearSelection();
        recordState("sceneLoaded");
        addConsoleMessage("Loaded scene: " + sceneName, ConsoleMessageType::Success);
        if (loadedTimeOfDay >= 0.0f) {
            applySceneTimeOfDay(loadedTimeOfDay);
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
    markRuntimeScriptBindingsDirty();
    clearSelection();
    nextObjectId = 0;
    undoStack.clear();
    redoStack.clear();

    projectManager.currentProject.currentSceneName = sceneName;
    projectManager.currentProject.hasUnsavedChanges = true;
    applySceneTimeOfDay(0.5f);

    addObject(ObjectType::Cube, "Cube");
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
    markRuntimeScriptBindingsDirty();
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
        newObj.hasLight2D = it->hasLight2D;
        newObj.hasCamera = it->hasCamera;
        newObj.hasPostFX = it->hasPostFX;
        newObj.hasUI = it->hasUI;
        newObj.hasShadowCaster2D = it->hasShadowCaster2D;
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
        newObj.light2D = it->light2D;
        newObj.shadowCaster2D = it->shadowCaster2D;
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
        markRuntimeScriptBindingsDirty();
        setPrimarySelection(id);
        if (projectManager.currentProject.isLoaded) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        logToConsole("Duplicated: " + newObj.name);
    }
}

void Engine::copySelected() {
    std::vector<int> ids = selectedObjectIds;
    if (ids.empty() && selectedObjectId >= 0) {
        ids.push_back(selectedObjectId);
    }
    if (ids.empty()) {
        return;
    }

    std::unordered_set<int> idSet(ids.begin(), ids.end());
    objectClipboard.clear();
    objectClipboard.reserve(idSet.size());
    for (const auto& obj : sceneObjects) {
        if (idSet.count(obj.id) == 0) continue;
        SceneObject copy = obj;
        copy.childIds.erase(std::remove_if(copy.childIds.begin(), copy.childIds.end(),
                          [&idSet](int id) { return idSet.count(id) == 0; }),
                          copy.childIds.end());
        objectClipboard.push_back(std::move(copy));
    }

    addConsoleMessage("Copied " + std::to_string(objectClipboard.size()) + " object(s).", ConsoleMessageType::Info);
}

void Engine::pasteClipboard() {
    if (objectClipboard.empty()) {
        return;
    }

    recordState("paste");

    std::unordered_map<int, int> idMap;
    idMap.reserve(objectClipboard.size());
    std::vector<int> newIds;
    newIds.reserve(objectClipboard.size());
    std::vector<int> oldParents;
    oldParents.reserve(objectClipboard.size());

    for (const auto& tpl : objectClipboard) {
        SceneObject copy = tpl;
        const int oldId = copy.id;
        const int newId = nextObjectId++;
        idMap[oldId] = newId;
        oldParents.push_back(copy.parentId);

        copy.id = newId;
        copy.name += " (Copy)";
        copy.position = tpl.position + glm::vec3(1.0f, 0.0f, 0.0f);
        copy.parentId = -1;
        copy.childIds.clear();
        copy.localPosition = copy.position;
        copy.localRotation = NormalizeEulerDegrees(copy.rotation);
        copy.localScale = copy.scale;
        copy.localInitialized = true;

        sceneObjects.push_back(std::move(copy));
        newIds.push_back(newId);
    }

    for (size_t i = 0; i < newIds.size(); ++i) {
        SceneObject* newObj = findObjectById(newIds[i]);
        if (!newObj) continue;

        const int oldParentId = oldParents[i];
        int resolvedParent = -1;
        auto mapped = idMap.find(oldParentId);
        if (mapped != idMap.end()) {
            resolvedParent = mapped->second;
        } else if (findObjectById(oldParentId) != nullptr) {
            resolvedParent = oldParentId;
        }
        newObj->parentId = resolvedParent;

        if (resolvedParent != -1) {
            if (SceneObject* parent = findObjectById(resolvedParent)) {
                if (std::find(parent->childIds.begin(), parent->childIds.end(), newObj->id) == parent->childIds.end()) {
                    parent->childIds.push_back(newObj->id);
                }
            }
        }

        glm::vec3 parentPos(0.0f);
        glm::quat parentRot(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 parentScale(1.0f);
        if (newObj->parentId != -1) {
            if (SceneObject* parent = findObjectById(newObj->parentId)) {
                parentPos = parent->position;
                parentRot = QuatFromEulerXYZ(parent->rotation);
                parentScale = parent->scale;
            }
        }
        updateLocalFromWorld(*newObj, parentPos, parentRot, parentScale);
    }

    updateHierarchyWorldTransforms();
    markRuntimeScriptBindingsDirty();
    if (projectManager.currentProject.isLoaded) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }
    selectedObjectIds = newIds;
    selectedObjectId = selectedObjectIds.empty() ? -1 : selectedObjectIds.back();
    addConsoleMessage("Pasted " + std::to_string(newIds.size()) + " object(s).", ConsoleMessageType::Success);
}

void Engine::selectAllObjects() {
    selectedObjectIds.clear();
    selectedObjectIds.reserve(sceneObjects.size());
    for (const auto& obj : sceneObjects) {
        selectedObjectIds.push_back(obj.id);
    }
    selectedObjectId = selectedObjectIds.empty() ? -1 : selectedObjectIds.back();
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
        markRuntimeScriptBindingsDirty();
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

void Engine::setParent(int childId, int parentId, int beforeSiblingId) {
    recordState("reparent");
    auto childIt = std::find_if(sceneObjects.begin(), sceneObjects.end(),
        [childId](const SceneObject& obj) { return obj.id == childId; });

    if (childIt == sceneObjects.end()) return;
    if (parentId == childId) return;
    if (beforeSiblingId == childId) {
        if (parentId == childIt->parentId) {
            return;
        }
        beforeSiblingId = -1;
    }

    if (parentId != -1) {
        int current = parentId;
        while (current != -1) {
            if (current == childId) return;
            SceneObject* ancestor = findObjectById(current);
            current = ancestor ? ancestor->parentId : -1;
        }
    }

    const int oldParentId = childIt->parentId;

    if (oldParentId != -1) {
        auto oldParentIt = std::find_if(sceneObjects.begin(), sceneObjects.end(),
            [oldParentId](const SceneObject& obj) { return obj.id == oldParentId; });
        if (oldParentIt != sceneObjects.end()) {
            auto& children = oldParentIt->childIds;
            children.erase(std::remove(children.begin(), children.end(), childId), children.end());
        }
    }

    int resolvedParentId = parentId;
    if (resolvedParentId != -1) {
        auto newParentIt = std::find_if(sceneObjects.begin(), sceneObjects.end(),
            [resolvedParentId](const SceneObject& obj) { return obj.id == resolvedParentId; });
        if (newParentIt == sceneObjects.end()) {
            resolvedParentId = -1;
        }
    }

    childIt = std::find_if(sceneObjects.begin(), sceneObjects.end(),
        [childId](const SceneObject& obj) { return obj.id == childId; });
    if (childIt == sceneObjects.end()) return;
    childIt->parentId = resolvedParentId;

    if (resolvedParentId != -1) {
        auto newParentIt = std::find_if(sceneObjects.begin(), sceneObjects.end(),
            [resolvedParentId](const SceneObject& obj) { return obj.id == resolvedParentId; });
        if (newParentIt != sceneObjects.end()) {
            auto& children = newParentIt->childIds;
            children.erase(std::remove(children.begin(), children.end(), childId), children.end());
            if (beforeSiblingId != -1) {
                auto beforeIt = std::find(children.begin(), children.end(), beforeSiblingId);
                if (beforeIt != children.end()) {
                    children.insert(beforeIt, childId);
                } else {
                    children.push_back(childId);
                }
            } else {
                children.push_back(childId);
            }
        }
    } else {
        auto currentIt = std::find_if(sceneObjects.begin(), sceneObjects.end(),
            [childId](const SceneObject& obj) { return obj.id == childId; });
        if (currentIt != sceneObjects.end()) {
            size_t insertIndex = sceneObjects.size();
            if (beforeSiblingId != -1) {
                auto beforeIt = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                    [beforeSiblingId](const SceneObject& obj) { return obj.id == beforeSiblingId && obj.parentId == -1; });
                if (beforeIt != sceneObjects.end()) {
                    insertIndex = static_cast<size_t>(std::distance(sceneObjects.begin(), beforeIt));
                }
            }

            size_t oldIndex = static_cast<size_t>(std::distance(sceneObjects.begin(), currentIt));
            SceneObject moved = std::move(*currentIt);
            sceneObjects.erase(currentIt);
            if (oldIndex < insertIndex && insertIndex > 0) {
                --insertIndex;
            }
            if (insertIndex > sceneObjects.size()) insertIndex = sceneObjects.size();
            sceneObjects.insert(sceneObjects.begin() + static_cast<std::ptrdiff_t>(insertIndex), std::move(moved));
        }
    }
    {
        glm::vec3 parentPos(0.0f);
        glm::quat parentRot(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 parentScale(1.0f);
        if (resolvedParentId != -1) {
            if (SceneObject* parent = findObjectById(resolvedParentId)) {
                parentPos = parent->position;
                parentRot = QuatFromEulerXYZ(parent->rotation);
                parentScale = parent->scale;
            }
        }
        childIt = std::find_if(sceneObjects.begin(), sceneObjects.end(),
            [childId](const SceneObject& obj) { return obj.id == childId; });
        if (childIt == sceneObjects.end()) return;
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
    Modularity::CrashReporter::AppendLogLine(std::string("[") + timeStr + "] " + message);

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
    refreshSceneObjectIndexCache();
    auto it = sceneObjectIndexById.find(id);
    if (it == sceneObjectIndexById.end()) return nullptr;
    if (it->second >= sceneObjects.size()) return nullptr;
    return &sceneObjects[it->second];
}

bool Engine::propagateObjectRenameReferences(const std::string& oldName,
                                             const std::string& newName,
                                             int renamedObjectId) {
    (void)renamedObjectId;
    if (oldName.empty() || oldName == newName) return false;

    bool changed = false;
    for (SceneObject& obj : sceneObjects) {
        for (ScriptComponent& script : obj.scripts) {
            for (ScriptSetting& setting : script.settings) {
                if (RenameRewriteScriptSettingValue(setting.value, oldName, newName)) {
                    changed = true;
                }
            }
        }
    }

    if (changed) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }
    return changed;
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

bool Engine::playAudioOneShotFromScript(int id, const std::string& clipPath, float volumeScale) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAudioSource) return false;
    return audio.playObjectOneShot(*obj, clipPath, volumeScale);
}

bool Engine::hasAnimationFromScript(int id) const {
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(), [id](const SceneObject& obj) {
        return obj.id == id;
    });
    if (it == sceneObjects.end()) return false;
    return it->hasAnimation && it->animation.enabled && !AnimationGetActiveClipAssetPath(it->animation).empty();
}

bool Engine::playAnimationFromScript(int id, bool restart) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAnimation || !obj->animation.enabled) return false;
    NormalizeAnimationClipSlots(obj->animation);
    const std::string activeClipPath = AnimationGetActiveClipAssetPath(obj->animation);
    if (activeClipPath.empty()) return false;

    obj->animation.runtimeInitialized = true;
    obj->animation.runtimeClipPath = activeClipPath;
    obj->animation.runtimeDirection = 1.0f;
    obj->animation.runtimePaused = false;
    obj->animation.runtimePlaying = true;

    const RuntimeAnimationClip* clip = getRuntimeAnimationClip(activeClipPath);
    const float duration = clip ? std::max(0.01f, clip->duration) : std::max(0.01f, obj->animation.clipLength);
    if (clip) {
        obj->animation.clipLength = duration;
    }
    if (restart) {
        obj->animation.runtimeTime = 0.0f;
    }
    obj->animation.runtimeTime = std::clamp(obj->animation.runtimeTime, 0.0f, duration);
    return true;
}

bool Engine::stopAnimationFromScript(int id, bool resetTime) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAnimation) return false;
    NormalizeAnimationClipSlots(obj->animation);
    const std::string activeClipPath = AnimationGetActiveClipAssetPath(obj->animation);
    if (activeClipPath.empty()) return false;

    obj->animation.runtimePlaying = false;
    obj->animation.runtimePaused = false;
    obj->animation.runtimeInitialized = true;
    obj->animation.runtimeClipPath = activeClipPath;
    if (resetTime) {
        obj->animation.runtimeTime = 0.0f;
    }
    return true;
}

bool Engine::pauseAnimationFromScript(int id, bool pause) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAnimation) return false;
    if (AnimationGetActiveClipAssetPath(obj->animation).empty()) return false;
    if (!obj->animation.runtimePlaying) return false;
    obj->animation.runtimePaused = pause;
    return true;
}

bool Engine::reverseAnimationFromScript(int id, bool restartIfStopped) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAnimation || !obj->animation.enabled) return false;
    NormalizeAnimationClipSlots(obj->animation);
    const std::string activeClipPath = AnimationGetActiveClipAssetPath(obj->animation);
    if (activeClipPath.empty()) return false;

    const RuntimeAnimationClip* clip = getRuntimeAnimationClip(activeClipPath);
    const float duration = clip ? std::max(0.01f, clip->duration) : std::max(0.01f, obj->animation.clipLength);
    if (clip) {
        obj->animation.clipLength = duration;
    }

    obj->animation.runtimeInitialized = true;
    obj->animation.runtimeClipPath = activeClipPath;
    obj->animation.runtimeDirection = -1.0f;
    if (!obj->animation.runtimePlaying && restartIfStopped) {
        obj->animation.runtimeTime = duration;
    }
    obj->animation.runtimeTime = std::clamp(obj->animation.runtimeTime, 0.0f, duration);
    obj->animation.runtimePaused = false;
    obj->animation.runtimePlaying = true;
    return true;
}

bool Engine::setAnimationTimeFromScript(int id, float timeSeconds) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAnimation) return false;
    NormalizeAnimationClipSlots(obj->animation);
    const std::string activeClipPath = AnimationGetActiveClipAssetPath(obj->animation);
    if (activeClipPath.empty()) return false;

    const RuntimeAnimationClip* clip = getRuntimeAnimationClip(activeClipPath);
    const float duration = clip ? std::max(0.01f, clip->duration) : std::max(0.01f, obj->animation.clipLength);
    if (clip) {
        obj->animation.clipLength = duration;
    }

    obj->animation.runtimeInitialized = true;
    obj->animation.runtimeClipPath = activeClipPath;
    obj->animation.runtimeTime = std::clamp(timeSeconds, 0.0f, duration);
    return true;
}

float Engine::getAnimationTimeFromScript(int id) const {
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(), [id](const SceneObject& obj) {
        return obj.id == id;
    });
    if (it == sceneObjects.end() || !it->hasAnimation) return 0.0f;
    return it->animation.runtimeTime;
}

bool Engine::isAnimationPlayingFromScript(int id) const {
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(), [id](const SceneObject& obj) {
        return obj.id == id;
    });
    if (it == sceneObjects.end() || !it->hasAnimation) return false;
    return it->animation.runtimePlaying && !it->animation.runtimePaused;
}

bool Engine::setAnimationLoopFromScript(int id, bool loop) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAnimation) return false;
    obj->animation.loop = loop;
    markProjectDirty();
    return true;
}

bool Engine::setAnimationPlaySpeedFromScript(int id, float speed) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAnimation) return false;
    obj->animation.playSpeed = std::max(0.0f, speed);
    markProjectDirty();
    return true;
}

bool Engine::setAnimationPlayOnAwakeFromScript(int id, bool playOnAwake) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAnimation) return false;
    obj->animation.playOnAwake = playOnAwake;
    markProjectDirty();
    return true;
}
#pragma endregion

#pragma region Script Compilation + Editor Tabs
void Engine::resetScriptRuntimeStateForReload(bool clearBinaryPaths) {
    scriptRuntime.unloadAll();
    managedRuntime.unloadAll();

    for (SceneObject& obj : sceneObjects) {
        for (ScriptComponent& sc : obj.scripts) {
            sc.activeIEnums.clear();
            sc.lastBinaryVerified = false;
            if (clearBinaryPaths) {
                sc.lastBinaryPath.clear();
            }
        }
    }

    nativeScriptMissingLogged.clear();
    nativeScriptLoadErrorLogged.clear();
    markRuntimeScriptBindingsDirty();
    scriptEditorWindowsDirty = true;
}

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
    fs::path projectRoot = projectManager.currentProject.projectPath;

    compileInProgress = true;
    compileResultReady = false;
    compileWorker = std::thread([this, scriptPath, configPath, projectRoot]() {
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

                    fs::path compiledSourcePath = scriptPath;
                    if (compiledSourcePath.is_relative()) {
                        std::error_code projectEc;
                        fs::path projectCandidate = projectRoot / compiledSourcePath;
                        if (fs::exists(projectCandidate, projectEc) && !projectEc) {
                            compiledSourcePath = projectCandidate;
                        }
                    }

                    std::error_code sourceEc;
                    fs::path sourceAbs = fs::absolute(compiledSourcePath, sourceEc);
                    if (sourceEc) sourceAbs = compiledSourcePath;
                    fs::path sourceCanonical = fs::weakly_canonical(sourceAbs, sourceEc);
                    if (!sourceEc) sourceAbs = sourceCanonical;
                    result.compiledSource = sourceAbs.lexically_normal().string();
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
            // Ensure every runtime/script instance is rebuilt against freshly compiled binaries.
            resetScriptRuntimeStateForReload(true);

            lastCompileSuccess = true;
            lastCompileStatus = "Reloading scripts";
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
                        sc.lastBinaryVerified = true;
                    }
                }
            } else {
                auto normalizeSourcePath = [&](const fs::path& path, bool treatAsProjectRelative) {
                    if (path.empty()) return std::string();

                    fs::path candidate = path;
                    if (treatAsProjectRelative && candidate.is_relative() &&
                        projectManager.currentProject.isLoaded) {
                        candidate = projectManager.currentProject.projectPath / candidate;
                    }

                    std::error_code ec;
                    fs::path absolutePath = fs::absolute(candidate, ec);
                    if (ec) absolutePath = candidate;
                    fs::path canonicalPath = fs::weakly_canonical(absolutePath, ec);
                    if (!ec) absolutePath = canonicalPath;

                    std::string normalized = absolutePath.lexically_normal().string();
#if defined(_WIN32)
                    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
                    return normalized;
                };

                const std::string compiledFromSource = normalizeSourcePath(
                    result.compiledSource.empty() ? result.scriptPath : fs::path(result.compiledSource), false);
                const std::string compiledFromRequest = normalizeSourcePath(result.scriptPath, true);
                const std::string compiledBinary = result.binaryPath.string();

                for (auto& obj : sceneObjects) {
                    for (auto& sc : obj.scripts) {
                        if (sc.language == ScriptLanguage::CSharp) continue;

                        const std::string scriptSource = normalizeSourcePath(sc.path, true);
                        const bool isCompiledScript =
                            (!compiledFromSource.empty() && scriptSource == compiledFromSource) ||
                            (!compiledFromRequest.empty() && scriptSource == compiledFromRequest);

                        if (isCompiledScript) {
                            sc.lastBinaryPath = compiledBinary;
                            sc.lastBinaryVerified = !compiledBinary.empty();
                        } else {
                            // Force re-resolution after compile so inspector/runtime can't stay on stale binaries.
                            sc.lastBinaryPath.clear();
                            sc.lastBinaryVerified = false;
                        }
                    }
                }

                nativeScriptMissingLogged.clear();
                nativeScriptLoadErrorLogged.clear();
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
#if defined(__ANDROID__)
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
#endif
    if (usingVulkan()) {
        io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    } else {
    #ifndef __linux__
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    #endif
    }
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
    if (usingVulkan()) {
        if (!ImGui_ImplGlfw_InitForVulkan(editorWindow, true)) {
            throw std::runtime_error("ImGui GLFW Vulkan init failed");
        }
        std::cerr << "[DEBUG] setupImGui: Vulkan backend selected; renderer backend will be initialized after Vulkan setup." << std::endl;
    } else {
        if (!ImGui_ImplGlfw_InitForOpenGL(editorWindow, true)) {
            throw std::runtime_error("ImGui GLFW OpenGL init failed");
        }

        std::cerr << "[DEBUG] setupImGui: initializing ImGui OpenGL3 backend..." << std::endl;
        if (!ImGui_ImplOpenGL3_Init("#version 330")) {
            std::cerr << "[DEBUG] ImGui OpenGL3 init failed!" << std::endl;
            throw std::runtime_error("ImGui error");
        }
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
    const bool vulkanLayout = usingVulkan();
    const char* filename = vulkanLayout ? "imgui_vulkan.ini" : "imgui.ini";
    if (mode == WorkspaceMode::Animation) {
        filename = vulkanLayout ? "anim_vulkan.ini" : "anim.ini";
    } else if (mode == WorkspaceMode::Scripting) {
        filename = vulkanLayout ? "scripter_vulkan.ini" : "scripter.ini";
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
    if (showLauncher || !projectManager.currentProject.isLoaded) {
        return;
    }
    if (pendingWorkspaceReload || workspaceLayoutDirty) {
        return;
    }
    if (mainDockspaceId == 0 || ImGui::DockBuilderGetNode(mainDockspaceId) == nullptr) {
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
    if (!projectManager.currentProject.isLoaded) {
        return;
    }
    if (pendingWorkspaceReload || workspaceLayoutDirty) {
        return;
    }

    if (glfwGetTime() < workspaceLayoutStabilizeUntil) {
        return;
    }

    if (workspaceLayoutAutoRepairPending &&
        mainDockspaceId != 0 &&
        ImGui::DockBuilderGetNode(mainDockspaceId) != nullptr) {
        int trackedWindows = 0;
        int undockedWindows = 0;
        auto countDockState = [&](const char* name, bool shouldBeVisible) {
            if (!shouldBeVisible || !name) {
                return;
            }
            ImGuiWindow* window = ImGui::FindWindowByName(name);
            if (!window) {
                return;
            }
            ++trackedWindows;
            if (window->DockId == 0 || ImGui::DockBuilderGetNode(window->DockId) == nullptr) {
                ++undockedWindows;
            }
        };

        countDockState("Hierarchy", showHierarchy);
        countDockState("Inspector", showInspector);
        countDockState("Project", showFileBrowser);
        countDockState("Console", showConsole);
        countDockState("Viewport", true);
        countDockState("Game Viewport", showGameViewport);
        countDockState("Environment", showEnvironmentWindow);
        countDockState("Camera", showCameraWindow);
        countDockState("Animation", showAnimationWindow);
        countDockState("AI Pathfinding", showAIPathfindingWindow);
        countDockState("Pixel Sprite Editor", showPixelSpriteEditorWindow);
        countDockState("Scripting", showScriptingWindow);
        countDockState("Project Settings", showProjectBrowser);

        if (trackedWindows >= 4) {
            if (undockedWindows >= 2) {
                buildWorkspaceLayout(currentWorkspace);
                workspaceLayoutSavePending = true;
                workspaceLayoutStabilizeUntil = glfwGetTime() + 0.75;
            }
            workspaceLayoutAutoRepairPending = false;
        }
    }

    if (context->SettingsDirtyTimer > 0.0f) {
        workspaceLayoutSavePending = true;
        return;
    }

    if (workspaceLayoutSavePending) {
        saveWorkspaceLayout(currentWorkspace);
        workspaceLayoutSavePending = false;

        auto layoutFileHasDockNodesForDockspace = [](const fs::path& path, ImGuiID dockspaceId) -> bool {
            if (dockspaceId == 0) {
                return false;
            }
            std::ifstream in(path);
            if (!in.is_open()) {
                return false;
            }
            char dockspaceIdHex[16];
            std::snprintf(dockspaceIdHex, sizeof(dockspaceIdHex), "0x%08X", dockspaceId);
            bool hasDockingData = false;
            bool hasDockNodes = false;
            bool hasMatchingDockspace = false;
            std::string line;
            while (std::getline(in, line)) {
                if (line == "[Docking][Data]") {
                    hasDockingData = true;
                    continue;
                }
                if (!hasDockingData) {
                    continue;
                }
                if (!line.empty() && line.front() == '[') {
                    break;
                }
                if (line.find("DockNode") != std::string::npos) {
                    hasDockNodes = true;
                }
                if (line.find("DockSpace") != std::string::npos &&
                    line.find(dockspaceIdHex) != std::string::npos) {
                    hasMatchingDockspace = true;
                }
            }
            return hasDockingData && hasDockNodes && hasMatchingDockspace;
        };

        fs::path layoutPath = getWorkspaceLayoutPath(currentWorkspace);
        if (!layoutPath.empty() &&
            fs::exists(layoutPath) &&
            !layoutFileHasDockNodesForDockspace(layoutPath, mainDockspaceId) &&
            mainDockspaceId != 0) {
            buildWorkspaceLayout(currentWorkspace);
            saveWorkspaceLayout(currentWorkspace);
        }
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
    workspaceTabVisible = { true, true, true };
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
        } else if (key == "workspaceTab.Default") {
            workspaceTabVisible[0] = (value == "1" || value == "true" || value == "yes");
        } else if (key == "workspaceTab.Animation") {
            workspaceTabVisible[1] = (value == "1" || value == "true" || value == "yes");
        } else if (key == "workspaceTab.Scripting") {
            workspaceTabVisible[2] = (value == "1" || value == "true" || value == "yes");
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
        } else if (key == "showPixelSpriteEditorWindow") {
            showPixelSpriteEditorWindow = (value == "1" || value == "true" || value == "yes");
        } else if (key == "showSceneGizmos") {
            showSceneGizmos = (value == "1" || value == "true" || value == "yes");
        } else if (key == "gizmoShowCameraOverlays") {
            gizmoShowCameraOverlays = (value == "1" || value == "true" || value == "yes");
        } else if (key == "gizmoShowCameraFrustumLabels") {
            gizmoShowCameraFrustumLabels = (value == "1" || value == "true" || value == "yes");
        } else if (key == "gizmoShowLightOverlays") {
            gizmoShowLightOverlays = (value == "1" || value == "true" || value == "yes");
        } else if (key == "gizmoShowLightIntensityLabels") {
            gizmoShowLightIntensityLabels = (value == "1" || value == "true" || value == "yes");
        } else if (key == "showViewportHintOverlay") {
            showViewportHintOverlay = (value == "1" || value == "true" || value == "yes");
        } else if (key == "showLight2DStatsOverlay") {
            showLight2DStatsOverlay = (value == "1" || value == "true" || value == "yes");
        } else if (key == "gizmoShowLight2DBounds") {
            gizmoShowLight2DBounds = (value == "1" || value == "true" || value == "yes");
        } else if (key == "gizmoShowLight2DShapes") {
            gizmoShowLight2DShapes = (value == "1" || value == "true" || value == "yes");
        } else if (key == "gizmoShowShadowCaster2DBounds") {
            gizmoShowShadowCaster2DBounds = (value == "1" || value == "true" || value == "yes");
        } else if (key == "sceneGizmoIconScale") {
            try { sceneGizmoIconScale = std::stof(value); } catch (...) {}
        } else if (key == "sceneGizmoOverlayScale") {
            try { sceneGizmoOverlayScale = std::stof(value); } catch (...) {}
        } else if (key == "showSceneGrid3D") {
            showSceneGrid3D = (value == "1" || value == "true" || value == "yes");
        } else if (key == "showCanvasOverlay") {
            showCanvasOverlay = (value == "1" || value == "true" || value == "yes");
        } else if (key == "showUIWorldGrid") {
            showUIWorldGrid = (value == "1" || value == "true" || value == "yes");
        } else if (key == "pixelGridSnapEnabled") {
            pixelGridSnapEnabled = (value == "1" || value == "true" || value == "yes");
        } else if (key == "pixelGridSnapStep") {
            try { pixelGridSnapStep = std::clamp(std::stoi(value), 1, 64); } catch (...) {}
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
            try { gameViewportZoom = std::clamp(std::stof(value), 1.0f, 8.0f); } catch (...) {}
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
    sceneGizmoIconScale = std::clamp(sceneGizmoIconScale, 0.4f, 3.0f);
    sceneGizmoOverlayScale = std::clamp(sceneGizmoOverlayScale, 0.4f, 3.0f);
    camera.moveSpeed = std::max(0.01f, camera.moveSpeed);
    camera.sprintSpeed = std::max(camera.moveSpeed, camera.sprintSpeed);
    camera.acceleration = std::max(0.1f, camera.acceleration);
    camera.mouseSensitivity = std::clamp(camera.mouseSensitivity, 0.001f, 2.0f);
    fpsCap = std::max(1.0f, fpsCap);
    gameViewportCustomWidth = std::clamp(gameViewportCustomWidth, 64, 8192);
    gameViewportCustomHeight = std::clamp(gameViewportCustomHeight, 64, 8192);
    gameViewportZoom = std::clamp(gameViewportZoom, 1.0f, 8.0f);
    pixelGridSnapStep = std::clamp(pixelGridSnapStep, 1, 64);
    scriptAutoCompileInterval = std::clamp(scriptAutoCompileInterval, 0.1, 10.0);

    if (!workspaceTabVisible[0] && !workspaceTabVisible[1] && !workspaceTabVisible[2]) {
        workspaceTabVisible[0] = true;
    }
    auto workspaceToIndex = [](WorkspaceMode mode) {
        switch (mode) {
            case WorkspaceMode::Default: return 0;
            case WorkspaceMode::Animation: return 1;
            case WorkspaceMode::Scripting: return 2;
        }
        return 0;
    };
    if (!workspaceTabVisible[workspaceToIndex(currentWorkspace)]) {
        if (workspaceTabVisible[0]) {
            currentWorkspace = WorkspaceMode::Default;
        } else if (workspaceTabVisible[1]) {
            currentWorkspace = WorkspaceMode::Animation;
        } else {
            currentWorkspace = WorkspaceMode::Scripting;
        }
    }

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
    file << "workspaceTab.Default=" << (workspaceTabVisible[0] ? "1" : "0") << "\n";
    file << "workspaceTab.Animation=" << (workspaceTabVisible[1] ? "1" : "0") << "\n";
    file << "workspaceTab.Scripting=" << (workspaceTabVisible[2] ? "1" : "0") << "\n";
    file << "fileBrowserIconScale=" << fileBrowserIconScale << "\n";
    file << "fileBrowserViewMode=" << (fileBrowser.viewMode == FileBrowserViewMode::List ? "List" : "Grid") << "\n";
    file << "fileBrowserSidebarWidth=" << fileBrowserSidebarWidth << "\n";
    file << "fileBrowserSidebarVisible=" << (showFileBrowserSidebar ? "1" : "0") << "\n";
    file << "consoleWrapText=" << (consoleWrapText ? "1" : "0") << "\n";
    file << "showAnimationWindow=" << (showAnimationWindow ? "1" : "0") << "\n";
    file << "showAIPathfindingWindow=" << (showAIPathfindingWindow ? "1" : "0") << "\n";
    file << "showPixelSpriteEditorWindow=" << (showPixelSpriteEditorWindow ? "1" : "0") << "\n";
    file << "showSceneGizmos=" << (showSceneGizmos ? "1" : "0") << "\n";
    file << "gizmoShowCameraOverlays=" << (gizmoShowCameraOverlays ? "1" : "0") << "\n";
    file << "gizmoShowCameraFrustumLabels=" << (gizmoShowCameraFrustumLabels ? "1" : "0") << "\n";
    file << "gizmoShowLightOverlays=" << (gizmoShowLightOverlays ? "1" : "0") << "\n";
    file << "gizmoShowLightIntensityLabels=" << (gizmoShowLightIntensityLabels ? "1" : "0") << "\n";
    file << "showViewportHintOverlay=" << (showViewportHintOverlay ? "1" : "0") << "\n";
    file << "showLight2DStatsOverlay=" << (showLight2DStatsOverlay ? "1" : "0") << "\n";
    file << "gizmoShowLight2DBounds=" << (gizmoShowLight2DBounds ? "1" : "0") << "\n";
    file << "gizmoShowLight2DShapes=" << (gizmoShowLight2DShapes ? "1" : "0") << "\n";
    file << "gizmoShowShadowCaster2DBounds=" << (gizmoShowShadowCaster2DBounds ? "1" : "0") << "\n";
    file << "sceneGizmoIconScale=" << std::clamp(sceneGizmoIconScale, 0.4f, 3.0f) << "\n";
    file << "sceneGizmoOverlayScale=" << std::clamp(sceneGizmoOverlayScale, 0.4f, 3.0f) << "\n";
    file << "showSceneGrid3D=" << (showSceneGrid3D ? "1" : "0") << "\n";
    file << "showCanvasOverlay=" << (showCanvasOverlay ? "1" : "0") << "\n";
    file << "showUIWorldGrid=" << (showUIWorldGrid ? "1" : "0") << "\n";
    file << "pixelGridSnapEnabled=" << (pixelGridSnapEnabled ? "1" : "0") << "\n";
    file << "pixelGridSnapStep=" << std::clamp(pixelGridSnapStep, 1, 64) << "\n";
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
