#include "Engine.h"
#include "ThirdParty/imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;

struct Transform {
    glm::vec3 localPosition = glm::vec3(0.0f);
    glm::vec4 localRotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec3 localScale = glm::vec3(1.0f);
};

struct SceneNode {
    std::string name;
    SceneNode* parent = nullptr;
    std::vector<SceneNode*> children;
    Transform local;
    int objectId = -1;
    bool missing = false;
};

using SceneRoot = SceneNode;

enum class TangentMode {
    Free = 0,
    Auto,
    Linear
};

enum class TrackInterpolation {
    Constant = 0,
    Linear,
    Cubic
};

struct AnimKey {
    uint64_t uid = 0;
    float time = 0.0f;
    float value = 0.0f;
    float inTangent = 0.0f;
    float outTangent = 0.0f;
    TangentMode tangentMode = TangentMode::Free;
    TrackInterpolation interpolation = TrackInterpolation::Linear;
};

struct AnimTrack {
    std::string propertyId;
    std::vector<AnimKey> keys;
    bool visible = true;
    bool locked = false;
};

struct AnimBinding {
    std::string path;
    std::string targetType;
    std::vector<AnimTrack> tracks;
    SceneNode* resolvedTarget = nullptr;
    bool missingTarget = false;
};

struct AnimClip {
    std::string name = "Animation Clip";
    int rootObjectId = -1;
    float duration = 5.0f;
    float sampleRate = 30.0f;
    std::vector<AnimBinding> bindings;
};

struct KeyHandle {
    int binding = -1;
    int track = -1;
    uint64_t keyUid = 0;

    bool operator==(const KeyHandle& other) const {
        return binding == other.binding && track == other.track && keyUid == other.keyUid;
    }
};

struct KeyHandleHash {
    size_t operator()(const KeyHandle& h) const {
        size_t a = static_cast<size_t>(h.binding) * 73856093u;
        size_t b = static_cast<size_t>(h.track) * 19349663u;
        size_t c = static_cast<size_t>(h.keyUid) * 83492791u;
        return a ^ b ^ c;
    }
};

struct TrackHandle {
    int binding = -1;
    int track = -1;

    bool operator==(const TrackHandle& other) const {
        return binding == other.binding && track == other.track;
    }
};

struct EditorSelection {
    std::unordered_set<KeyHandle, KeyHandleHash> keys;
    std::optional<KeyHandle> lastClicked;
    std::optional<KeyHandle> rangeAnchor;
};

struct TimelineTransform {
    float pixelsPerSecond = 120.0f;
    float timeOffset = 0.0f;
    float minPixelsPerSecond = 12.0f;
    float maxPixelsPerSecond = 2400.0f;

    float TimeToScreen(float time, float xOrigin) const {
        return xOrigin + (time - timeOffset) * pixelsPerSecond;
    }

    float ScreenToTime(float x, float xOrigin) const {
        return timeOffset + (x - xOrigin) / std::max(1.0f, pixelsPerSecond);
    }

    void PanPixels(float deltaX) {
        timeOffset -= deltaX / std::max(1.0f, pixelsPerSecond);
        timeOffset = std::max(0.0f, timeOffset);
    }

    void ZoomAt(float mouseX, float xOrigin, float zoomFactor) {
        float oldScale = pixelsPerSecond;
        float oldTime = ScreenToTime(mouseX, xOrigin);
        pixelsPerSecond = std::clamp(pixelsPerSecond * zoomFactor, minPixelsPerSecond, maxPixelsPerSecond);
        float newTime = ScreenToTime(mouseX, xOrigin);
        timeOffset += oldTime - newTime;
        if (!std::isfinite(timeOffset)) {
            timeOffset = 0.0f;
        }
        if (!std::isfinite(pixelsPerSecond)) {
            pixelsPerSecond = oldScale;
        }
        timeOffset = std::max(0.0f, timeOffset);
    }
};

struct BindingTreeNode {
    std::string name;
    std::string fullPath;
    std::vector<int> bindingIndices;
    std::vector<BindingTreeNode> children;
};

enum class RowType {
    Path = 0,
    Track
};

struct TimelineRow {
    RowType type = RowType::Path;
    int depth = 0;
    std::string label;
    std::string fullPath;
    int binding = -1;
    int track = -1;
    bool missing = false;
    bool expandable = false;
};

struct SceneGraphBridge {
    std::vector<std::unique_ptr<SceneNode>> nodes;
    std::unordered_map<int, SceneNode*> byObjectId;
    SceneNode root;
};

enum class AnimationTab {
    Dopesheet = 0,
    Curves
};

struct CopiedKey {
    int binding = -1;
    int track = -1;
    AnimKey key;
};

struct AnimationEditorState {
    bool initialized = false;
    AnimClip clip;
    BindingTreeNode tree;
    EditorSelection selection;
    TimelineTransform timeline;
    AnimationTab activeTab = AnimationTab::Dopesheet;
    float leftPaneWidth = 300.0f;
    float currentTime = 0.0f;
    bool previewEnabled = true;
    bool isPlaying = false;
    bool loopPlayback = true;
    bool recordEnabled = false;
    bool recordWasEnabled = false;
    bool frameSnapEnabled = true;
    float frameSnapThresholdPx = 3.0f;

    float curveValueScale = 60.0f;
    float curveValueOffset = 0.0f;

    uint64_t nextKeyUid = 1;
    std::unordered_map<std::string, bool> expandedPaths;
    std::optional<TrackHandle> focusedTrack;

    bool boxSelectActive = false;
    ImVec2 boxSelectStart = ImVec2(0.0f, 0.0f);
    ImVec2 boxSelectEnd = ImVec2(0.0f, 0.0f);

    bool curveBoxSelectActive = false;
    ImVec2 curveBoxStart = ImVec2(0.0f, 0.0f);
    ImVec2 curveBoxEnd = ImVec2(0.0f, 0.0f);

    bool draggingKeys = false;
    bool dragWasDuplicated = false;
    bool dragDuplicateRequest = false;
    ImVec2 dragMouseStart = ImVec2(0.0f, 0.0f);
    std::unordered_map<KeyHandle, float, KeyHandleHash> dragStartTimes;
    std::unordered_map<KeyHandle, float, KeyHandleHash> dragStartValues;

    bool tangentDragActive = false;
    bool tangentDragIsIn = false;
    KeyHandle tangentDragKey;

    std::vector<CopiedKey> clipboard;
    std::unordered_map<std::string, float> recordLastValues;

    std::optional<TrackHandle> contextTrack;
    float contextTime = 0.0f;

    int loadedRootObjectId = -1;
    std::string loadedClipAssetPath;
    int lastObservedSelectedObjectId = -1;
    int renameClipSlotIndex = -1;
    bool clipDirty = false;
    char clipPathInput[512] = "";
    char newClipNameInput[128] = "";
    char renameClipInput[128] = "";
};

static std::string BuildRelativePathToRoot(const SceneObject& candidate,
                                           int rootId,
                                           const std::unordered_map<int, SceneObject*>& byId) {
    if (candidate.id == rootId) return "";
    std::vector<std::string> segments;
    const SceneObject* current = &candidate;
    while (current && current->id != rootId) {
        segments.push_back(current->name);
        if (current->parentId < 0) {
            return "";
        }
        auto it = byId.find(current->parentId);
        if (it == byId.end() || !it->second) {
            return "";
        }
        current = it->second;
    }
    if (!current || current->id != rootId) {
        return "";
    }
    std::string path;
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
        if (!path.empty()) path += '/';
        path += *it;
    }
    return path;
}

static std::vector<std::string> SplitPath(const std::string& path) {
    std::vector<std::string> segments;
    std::string current;
    for (char c : path) {
        if (c == '/') {
            if (!current.empty()) {
                segments.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) {
        segments.push_back(current);
    }
    return segments;
}

static SceneNode* FindByPath(SceneNode* root, const std::string& path) {
    if (!root) return nullptr;
    if (path.empty()) return root;

    SceneNode* current = root;
    const std::vector<std::string> segments = SplitPath(path);
    for (const std::string& segment : segments) {
        SceneNode* next = nullptr;
        for (SceneNode* child : current->children) {
            if (child && child->name == segment) {
                next = child;
                break;
            }
        }
        if (!next) return nullptr;
        current = next;
    }
    return current;
}

static float SampleTrackValue(const AnimTrack& track, float t) {
    if (track.keys.empty()) {
        return 0.0f;
    }

    if (t <= track.keys.front().time) {
        return track.keys.front().value;
    }
    if (t >= track.keys.back().time) {
        return track.keys.back().value;
    }

    for (size_t i = 0; i + 1 < track.keys.size(); ++i) {
        const AnimKey& a = track.keys[i];
        const AnimKey& b = track.keys[i + 1];
        if (t < a.time || t > b.time) {
            continue;
        }

        const float dt = std::max(0.0001f, b.time - a.time);
        const float u = std::clamp((t - a.time) / dt, 0.0f, 1.0f);

        if (a.interpolation == TrackInterpolation::Constant) {
            return a.value;
        }
        if (a.interpolation == TrackInterpolation::Linear) {
            return a.value + (b.value - a.value) * u;
        }

        const float u2 = u * u;
        const float u3 = u2 * u;
        const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
        const float h10 = u3 - 2.0f * u2 + u;
        const float h01 = -2.0f * u3 + 3.0f * u2;
        const float h11 = u3 - u2;
        const float m0 = a.outTangent * dt;
        const float m1 = b.inTangent * dt;
        return h00 * a.value + h10 * m0 + h01 * b.value + h11 * m1;
    }

    return track.keys.back().value;
}

static bool ApplyPropertyToNode(SceneNode& node, const std::string& propertyId, float value) {
    if (propertyId == "localPosition.x") { node.local.localPosition.x = value; return true; }
    if (propertyId == "localPosition.y") { node.local.localPosition.y = value; return true; }
    if (propertyId == "localPosition.z") { node.local.localPosition.z = value; return true; }
    if (propertyId == "localRotation.x") { node.local.localRotation.x = value; return true; }
    if (propertyId == "localRotation.y") { node.local.localRotation.y = value; return true; }
    if (propertyId == "localRotation.z") { node.local.localRotation.z = value; return true; }
    if (propertyId == "localRotation.w") { node.local.localRotation.w = value; return true; }
    if (propertyId == "localScale.x") { node.local.localScale.x = value; return true; }
    if (propertyId == "localScale.y") { node.local.localScale.y = value; return true; }
    if (propertyId == "localScale.z") { node.local.localScale.z = value; return true; }
    return false;
}

static float ReadPropertyFromNode(const SceneNode& node, const std::string& propertyId) {
    if (propertyId == "localPosition.x") return node.local.localPosition.x;
    if (propertyId == "localPosition.y") return node.local.localPosition.y;
    if (propertyId == "localPosition.z") return node.local.localPosition.z;
    if (propertyId == "localRotation.x") return node.local.localRotation.x;
    if (propertyId == "localRotation.y") return node.local.localRotation.y;
    if (propertyId == "localRotation.z") return node.local.localRotation.z;
    if (propertyId == "localRotation.w") return node.local.localRotation.w;
    if (propertyId == "localScale.x") return node.local.localScale.x;
    if (propertyId == "localScale.y") return node.local.localScale.y;
    if (propertyId == "localScale.z") return node.local.localScale.z;
    return 0.0f;
}

static bool IsLocalTransformProperty(const std::string& propertyId) {
    return propertyId.rfind("localPosition.", 0) == 0 ||
           propertyId.rfind("localRotation.", 0) == 0 ||
           propertyId.rfind("localScale.", 0) == 0;
}

static bool TryParseFloatString(const std::string& text, float& outValue) {
    const char* begin = text.c_str();
    char* end = nullptr;
    outValue = std::strtof(begin, &end);
    if (end == begin) return false;
    while (*end != '\0') {
        if (!std::isspace(static_cast<unsigned char>(*end))) return false;
        ++end;
    }
    return true;
}

static bool ReadAnimatableProperty(const SceneObject& obj, const std::string& propertyId, float& outValue) {
    if (propertyId == "localPosition.x") { outValue = obj.localPosition.x; return true; }
    if (propertyId == "localPosition.y") { outValue = obj.localPosition.y; return true; }
    if (propertyId == "localPosition.z") { outValue = obj.localPosition.z; return true; }
    if (propertyId == "localRotation.x") { outValue = obj.localRotation.x; return true; }
    if (propertyId == "localRotation.y") { outValue = obj.localRotation.y; return true; }
    if (propertyId == "localRotation.z") { outValue = obj.localRotation.z; return true; }
    if (propertyId == "localScale.x") { outValue = obj.localScale.x; return true; }
    if (propertyId == "localScale.y") { outValue = obj.localScale.y; return true; }
    if (propertyId == "localScale.z") { outValue = obj.localScale.z; return true; }
    if (propertyId == "UI.Position.x" && obj.hasUI) { outValue = obj.ui.position.x; return true; }
    if (propertyId == "UI.Position.y" && obj.hasUI) { outValue = obj.ui.position.y; return true; }
    if (propertyId == "UI.Size.x" && obj.hasUI) { outValue = obj.ui.size.x; return true; }
    if (propertyId == "UI.Size.y" && obj.hasUI) { outValue = obj.ui.size.y; return true; }
    if (propertyId == "UI.Rotation" && obj.hasUI) { outValue = obj.ui.rotation; return true; }
    if (propertyId == "UI.SliderValue" && obj.hasUI) { outValue = obj.ui.sliderValue; return true; }
    if (propertyId == "UI.TextScale" && obj.hasUI) { outValue = obj.ui.textScale; return true; }
    if (propertyId == "UI.SpriteFrame" && obj.hasUI) { outValue = static_cast<float>(obj.ui.spriteSheetFrame); return true; }
    if (propertyId == "UI.SpriteSheetFPS" && obj.hasUI) { outValue = obj.ui.spriteSheetFps; return true; }
    if (propertyId == "UI.SpriteSheetLoop" && obj.hasUI) { outValue = obj.ui.spriteSheetLoop ? 1.0f : 0.0f; return true; }

    if (propertyId == "Light.Intensity" && obj.hasLight) { outValue = obj.light.intensity; return true; }
    if (propertyId == "Light.Range" && obj.hasLight) { outValue = obj.light.range; return true; }
    if (propertyId == "Light.InnerAngle" && obj.hasLight) { outValue = obj.light.innerAngle; return true; }
    if (propertyId == "Light.OuterAngle" && obj.hasLight) { outValue = obj.light.outerAngle; return true; }
    if (propertyId == "Light.Enabled" && obj.hasLight) { outValue = obj.light.enabled ? 1.0f : 0.0f; return true; }

    if (propertyId == "Camera.FOV" && obj.hasCamera) { outValue = obj.camera.fov; return true; }
    if (propertyId == "Camera.NearClip" && obj.hasCamera) { outValue = obj.camera.nearClip; return true; }
    if (propertyId == "Camera.FarClip" && obj.hasCamera) { outValue = obj.camera.farClip; return true; }
    if (propertyId == "Camera.PixelsPerUnit" && obj.hasCamera) { outValue = obj.camera.pixelsPerUnit; return true; }

    if (propertyId == "PostFX.BloomIntensity" && obj.hasPostFX) { outValue = obj.postFx.bloomIntensity; return true; }
    if (propertyId == "PostFX.Exposure" && obj.hasPostFX) { outValue = obj.postFx.exposure; return true; }
    if (propertyId == "PostFX.Contrast" && obj.hasPostFX) { outValue = obj.postFx.contrast; return true; }
    if (propertyId == "PostFX.Saturation" && obj.hasPostFX) { outValue = obj.postFx.saturation; return true; }

    if (propertyId == "Rigidbody.Mass" && obj.hasRigidbody) { outValue = obj.rigidbody.mass; return true; }

    if (propertyId == "Audio.Volume" && obj.hasAudioSource) { outValue = obj.audioSource.volume; return true; }
    if (propertyId == "Audio.MinDistance" && obj.hasAudioSource) { outValue = obj.audioSource.minDistance; return true; }
    if (propertyId == "Audio.MaxDistance" && obj.hasAudioSource) { outValue = obj.audioSource.maxDistance; return true; }
    if (propertyId == "Audio.Loop" && obj.hasAudioSource) { outValue = obj.audioSource.loop ? 1.0f : 0.0f; return true; }
    if (propertyId == "Audio.Spatial" && obj.hasAudioSource) { outValue = obj.audioSource.spatial ? 1.0f : 0.0f; return true; }

    if (propertyId == "AIAgent.Speed" && obj.hasAIAgent) { outValue = obj.aiAgent.speed; return true; }
    if (propertyId == "AIAgent.StoppingDistance" && obj.hasAIAgent) { outValue = obj.aiAgent.stoppingDistance; return true; }

    int scriptIndex = -1;
    int settingIndex = -1;
    if (std::sscanf(propertyId.c_str(), "ScriptSetting.%d.%d", &scriptIndex, &settingIndex) == 2) {
        if (scriptIndex >= 0 && scriptIndex < static_cast<int>(obj.scripts.size())) {
            const auto& script = obj.scripts[scriptIndex];
            if (settingIndex >= 0 && settingIndex < static_cast<int>(script.settings.size())) {
                return TryParseFloatString(script.settings[settingIndex].value, outValue);
            }
        }
    }

    return false;
}

static bool WriteAnimatableProperty(SceneObject& obj, const std::string& propertyId, float value) {
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

static std::vector<std::string> BuildAnimatablePropertiesForObject(const SceneObject& obj) {
    std::vector<std::string> props;
    props.reserve(32);
    props.push_back("localPosition.x");
    props.push_back("localPosition.y");
    props.push_back("localPosition.z");
    props.push_back("localRotation.x");
    props.push_back("localRotation.y");
    props.push_back("localRotation.z");
    props.push_back("localScale.x");
    props.push_back("localScale.y");
    props.push_back("localScale.z");
    if (obj.hasUI) {
        props.push_back("UI.Position.x");
        props.push_back("UI.Position.y");
        props.push_back("UI.Size.x");
        props.push_back("UI.Size.y");
        props.push_back("UI.Rotation");
        props.push_back("UI.SliderValue");
        props.push_back("UI.TextScale");
        if (obj.ui.spriteSheetEnabled || obj.ui.spriteCustomFramesEnabled || obj.ui.type == UIElementType::Sprite2D || obj.ui.type == UIElementType::Image) {
            props.push_back("UI.SpriteFrame");
            props.push_back("UI.SpriteSheetFPS");
            props.push_back("UI.SpriteSheetLoop");
        }
    }

    if (obj.hasLight) {
        props.push_back("Light.Intensity");
        props.push_back("Light.Range");
        props.push_back("Light.InnerAngle");
        props.push_back("Light.OuterAngle");
        props.push_back("Light.Enabled");
    }
    if (obj.hasCamera) {
        props.push_back("Camera.FOV");
        props.push_back("Camera.NearClip");
        props.push_back("Camera.FarClip");
        props.push_back("Camera.PixelsPerUnit");
    }
    if (obj.hasPostFX) {
        props.push_back("PostFX.BloomIntensity");
        props.push_back("PostFX.Exposure");
        props.push_back("PostFX.Contrast");
        props.push_back("PostFX.Saturation");
    }
    if (obj.hasRigidbody) {
        props.push_back("Rigidbody.Mass");
    }
    if (obj.hasAudioSource) {
        props.push_back("Audio.Volume");
        props.push_back("Audio.MinDistance");
        props.push_back("Audio.MaxDistance");
        props.push_back("Audio.Loop");
        props.push_back("Audio.Spatial");
    }
    if (obj.hasAIAgent) {
        props.push_back("AIAgent.Speed");
        props.push_back("AIAgent.StoppingDistance");
    }
    for (size_t scriptIndex = 0; scriptIndex < obj.scripts.size(); ++scriptIndex) {
        const auto& script = obj.scripts[scriptIndex];
        for (size_t settingIndex = 0; settingIndex < script.settings.size(); ++settingIndex) {
            float parsed = 0.0f;
            if (!TryParseFloatString(script.settings[settingIndex].value, parsed)) {
                continue;
            }
            char path[64];
            std::snprintf(path, sizeof(path), "ScriptSetting.%zu.%zu", scriptIndex, settingIndex);
            props.push_back(path);
        }
    }
    return props;
}

static void SortTrackKeys(AnimTrack& track) {
    std::sort(track.keys.begin(), track.keys.end(), [](const AnimKey& a, const AnimKey& b) {
        if (std::abs(a.time - b.time) > 0.00001f) return a.time < b.time;
        return a.uid < b.uid;
    });
}

static AnimKey* FindKey(AnimClip& clip, const KeyHandle& handle) {
    if (handle.binding < 0 || handle.binding >= static_cast<int>(clip.bindings.size())) return nullptr;
    AnimBinding& binding = clip.bindings[handle.binding];
    if (handle.track < 0 || handle.track >= static_cast<int>(binding.tracks.size())) return nullptr;
    AnimTrack& track = binding.tracks[handle.track];
    for (AnimKey& key : track.keys) {
        if (key.uid == handle.keyUid) {
            return &key;
        }
    }
    return nullptr;
}

static int FindKeyIndexByUid(const AnimTrack& track, uint64_t uid) {
    for (size_t i = 0; i < track.keys.size(); ++i) {
        if (track.keys[i].uid == uid) return static_cast<int>(i);
    }
    return -1;
}

static BindingTreeNode BuildBindingTreeFromClip(AnimClip& clip) {
    BindingTreeNode root;
    root.name = "Root";
    root.fullPath = "";

    for (size_t bindingIndex = 0; bindingIndex < clip.bindings.size(); ++bindingIndex) {
        const AnimBinding& binding = clip.bindings[bindingIndex];
        BindingTreeNode* current = &root;

        if (!binding.path.empty()) {
            const std::vector<std::string> segments = SplitPath(binding.path);
            std::string walkPath;
            for (const std::string& segment : segments) {
                if (!walkPath.empty()) walkPath += '/';
                walkPath += segment;

                auto it = std::find_if(current->children.begin(), current->children.end(), [&](const BindingTreeNode& n) {
                    return n.name == segment;
                });
                if (it == current->children.end()) {
                    BindingTreeNode child;
                    child.name = segment;
                    child.fullPath = walkPath;
                    current->children.push_back(child);
                    current = &current->children.back();
                } else {
                    current = &(*it);
                }
            }
        }

        current->bindingIndices.push_back(static_cast<int>(bindingIndex));
    }

    std::function<void(BindingTreeNode&)> sortRecursive = [&](BindingTreeNode& node) {
        std::sort(node.children.begin(), node.children.end(), [](const BindingTreeNode& a, const BindingTreeNode& b) {
            return a.name < b.name;
        });
        for (BindingTreeNode& child : node.children) {
            sortRecursive(child);
        }
    };
    sortRecursive(root);

    return root;
}

static void ResolveBindingTargets(AnimClip& clip, SceneRoot* root) {
    for (AnimBinding& binding : clip.bindings) {
        binding.resolvedTarget = FindByPath(root, binding.path);
        binding.missingTarget = (binding.resolvedTarget == nullptr);
    }
}

static void EvaluateClipAtTime(AnimClip& clip, float t, SceneRoot* root) {
    ResolveBindingTargets(clip, root);
    for (AnimBinding& binding : clip.bindings) {
        if (!binding.resolvedTarget) continue;
        for (const AnimTrack& track : binding.tracks) {
            if (!track.visible || track.locked) continue;
            const float sampled = SampleTrackValue(track, t);
            ApplyPropertyToNode(*binding.resolvedTarget, track.propertyId, sampled);
        }
    }
}

static bool IsSelected(const EditorSelection& selection, const KeyHandle& handle) {
    return selection.keys.find(handle) != selection.keys.end();
}

static void ClearSelection(EditorSelection& selection) {
    selection.keys.clear();
    selection.lastClicked.reset();
}

static void SelectAllKeys(AnimationEditorState& state) {
    ClearSelection(state.selection);

    KeyHandle lastHandle;
    bool hasAny = false;
    for (int bindingIndex = 0; bindingIndex < static_cast<int>(state.clip.bindings.size()); ++bindingIndex) {
        const AnimBinding& binding = state.clip.bindings[static_cast<size_t>(bindingIndex)];
        for (int trackIndex = 0; trackIndex < static_cast<int>(binding.tracks.size()); ++trackIndex) {
            const AnimTrack& track = binding.tracks[static_cast<size_t>(trackIndex)];
            for (const AnimKey& key : track.keys) {
                lastHandle = KeyHandle{bindingIndex, trackIndex, key.uid};
                state.selection.keys.insert(lastHandle);
                hasAny = true;
            }
        }
    }

    if (hasAny) {
        state.selection.lastClicked = lastHandle;
        state.selection.rangeAnchor = lastHandle;
    } else {
        state.selection.rangeAnchor.reset();
    }
}

static void SelectSingle(EditorSelection& selection, const KeyHandle& handle) {
    selection.keys.clear();
    selection.keys.insert(handle);
    selection.lastClicked = handle;
    selection.rangeAnchor = handle;
}

static void ToggleSelection(EditorSelection& selection, const KeyHandle& handle) {
    auto it = selection.keys.find(handle);
    if (it == selection.keys.end()) {
        selection.keys.insert(handle);
    } else {
        selection.keys.erase(it);
    }
    selection.lastClicked = handle;
    selection.rangeAnchor = handle;
}

static float PickMajorTimeStep(float pixelsPerSecond) {
    static const float steps[] = {
        0.01f, 0.02f, 0.05f,
        0.1f, 0.2f, 0.5f,
        1.0f, 2.0f, 5.0f,
        10.0f, 20.0f, 30.0f,
        60.0f, 120.0f
    };
    for (float step : steps) {
        if (step * pixelsPerSecond >= 70.0f) {
            return step;
        }
    }
    return 120.0f;
}

static std::string FormatTimelineTime(float t, float sampleRate) {
    (void)sampleRate;
    const int totalCentiseconds = static_cast<int>(std::round(std::max(0.0f, t) * 100.0f));
    int seconds = totalCentiseconds / 100;
    int centiseconds = totalCentiseconds % 100;
    if (centiseconds < 0) centiseconds = 0;
    int minutes = seconds / 60;
    seconds = seconds % 60;
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", minutes, seconds, centiseconds);
    return buffer;
}

static float SnapTimeToFrames(const AnimationEditorState& state,
                              float time,
                              float pixelsPerSecond,
                              bool forceSnap) {
    if (!state.frameSnapEnabled || state.clip.sampleRate <= 1.0f) {
        return time;
    }
    const float fps = std::max(1.0f, state.clip.sampleRate);
    const float snapped = std::round(time * fps) / fps;
    if (forceSnap) {
        return snapped;
    }
    const float thresholdPx = std::max(0.0f, state.frameSnapThresholdPx);
    const float deltaPx = std::abs(snapped - time) * std::max(1.0f, pixelsPerSecond);
    return (deltaPx <= thresholdPx) ? snapped : time;
}

static float EstimateTrackSlopeAt(const AnimTrack& track, int keyIndex) {
    if (keyIndex < 0 || keyIndex >= static_cast<int>(track.keys.size())) return 0.0f;
    const AnimKey& key = track.keys[static_cast<size_t>(keyIndex)];
    if (keyIndex > 0 && keyIndex + 1 < static_cast<int>(track.keys.size())) {
        const AnimKey& prev = track.keys[static_cast<size_t>(keyIndex - 1)];
        const AnimKey& next = track.keys[static_cast<size_t>(keyIndex + 1)];
        const float dt = std::max(0.0001f, next.time - prev.time);
        return (next.value - prev.value) / dt;
    }
    if (keyIndex + 1 < static_cast<int>(track.keys.size())) {
        const AnimKey& next = track.keys[static_cast<size_t>(keyIndex + 1)];
        const float dt = std::max(0.0001f, next.time - key.time);
        return (next.value - key.value) / dt;
    }
    if (keyIndex > 0) {
        const AnimKey& prev = track.keys[static_cast<size_t>(keyIndex - 1)];
        const float dt = std::max(0.0001f, key.time - prev.time);
        return (key.value - prev.value) / dt;
    }
    return 0.0f;
}

enum class InterpolationPreset {
    Constant = 0,
    Linear,
    Lerp,
    LerpIn,
    LerpOut,
    LerpInOut,
    Cubic
};

static void ApplyInterpolationPreset(AnimationEditorState& state, InterpolationPreset preset) {
    for (const KeyHandle& handle : state.selection.keys) {
        if (handle.binding < 0 || handle.binding >= static_cast<int>(state.clip.bindings.size())) continue;
        AnimBinding& binding = state.clip.bindings[handle.binding];
        if (handle.track < 0 || handle.track >= static_cast<int>(binding.tracks.size())) continue;
        AnimTrack& track = binding.tracks[handle.track];
        const int keyIndex = FindKeyIndexByUid(track, handle.keyUid);
        if (keyIndex < 0) continue;
        AnimKey& key = track.keys[static_cast<size_t>(keyIndex)];
        const float slope = EstimateTrackSlopeAt(track, keyIndex);

        switch (preset) {
            case InterpolationPreset::Constant:
                key.interpolation = TrackInterpolation::Constant;
                key.tangentMode = TangentMode::Linear;
                break;
            case InterpolationPreset::Linear:
            case InterpolationPreset::Lerp:
                key.interpolation = TrackInterpolation::Linear;
                key.tangentMode = TangentMode::Linear;
                break;
            case InterpolationPreset::LerpIn:
                key.interpolation = TrackInterpolation::Cubic;
                key.tangentMode = TangentMode::Free;
                key.inTangent = 0.0f;
                key.outTangent = slope;
                break;
            case InterpolationPreset::LerpOut:
                key.interpolation = TrackInterpolation::Cubic;
                key.tangentMode = TangentMode::Free;
                key.inTangent = slope;
                key.outTangent = 0.0f;
                break;
            case InterpolationPreset::LerpInOut:
                key.interpolation = TrackInterpolation::Cubic;
                key.tangentMode = TangentMode::Auto;
                key.inTangent = 0.0f;
                key.outTangent = 0.0f;
                break;
            case InterpolationPreset::Cubic:
                key.interpolation = TrackInterpolation::Cubic;
                key.tangentMode = TangentMode::Auto;
                key.inTangent = slope;
                key.outTangent = slope;
                break;
        }
        state.clipDirty = true;
    }
}

static void DrawInterpolationPresetMenu(AnimationEditorState& state) {
    if (state.selection.keys.empty()) return;
    if (!ImGui::BeginMenu("Interpolation Presets")) return;
    if (ImGui::MenuItem("Constant")) ApplyInterpolationPreset(state, InterpolationPreset::Constant);
    if (ImGui::MenuItem("Linear")) ApplyInterpolationPreset(state, InterpolationPreset::Linear);
    if (ImGui::MenuItem("Lerp")) ApplyInterpolationPreset(state, InterpolationPreset::Lerp);
    if (ImGui::MenuItem("Lerp In")) ApplyInterpolationPreset(state, InterpolationPreset::LerpIn);
    if (ImGui::MenuItem("Lerp Out")) ApplyInterpolationPreset(state, InterpolationPreset::LerpOut);
    if (ImGui::MenuItem("Lerp In-Out")) ApplyInterpolationPreset(state, InterpolationPreset::LerpInOut);
    if (ImGui::MenuItem("Cubic")) ApplyInterpolationPreset(state, InterpolationPreset::Cubic);
    ImGui::EndMenu();
}

static bool PointInDiamond(const ImVec2& p, const ImVec2& center, float radius) {
    return (std::abs(p.x - center.x) + std::abs(p.y - center.y)) <= radius;
}

static ImRect NormalizeRect(const ImVec2& a, const ImVec2& b) {
    ImRect r;
    r.Min = ImVec2(std::min(a.x, b.x), std::min(a.y, b.y));
    r.Max = ImVec2(std::max(a.x, b.x), std::max(a.y, b.y));
    return r;
}

static std::string TrackGroupPrefix(const std::string& propertyId) {
    size_t dot = propertyId.find_last_of('.');
    if (dot == std::string::npos) return propertyId;
    return propertyId.substr(0, dot);
}

static void CollectVisibleRowsRecursive(const BindingTreeNode& node,
                                        AnimClip& clip,
                                        AnimationEditorState& state,
                                        int depth,
                                        std::vector<TimelineRow>& outRows) {
    TimelineRow pathRow;
    pathRow.type = RowType::Path;
    pathRow.depth = depth;
    pathRow.fullPath = node.fullPath;
    pathRow.label = node.fullPath.empty() ? "(Root)" : node.name;
    pathRow.expandable = !node.children.empty() || !node.bindingIndices.empty();
    outRows.push_back(pathRow);

    const std::string expansionKey = node.fullPath.empty() ? "<root>" : node.fullPath;
    const bool expanded = (state.expandedPaths.find(expansionKey) == state.expandedPaths.end())
        ? true
        : state.expandedPaths[expansionKey];

    if (!expanded) {
        return;
    }

    for (int bindingIndex : node.bindingIndices) {
        if (bindingIndex < 0 || bindingIndex >= static_cast<int>(clip.bindings.size())) continue;
        AnimBinding& binding = clip.bindings[bindingIndex];
        for (size_t trackIndex = 0; trackIndex < binding.tracks.size(); ++trackIndex) {
            TimelineRow row;
            row.type = RowType::Track;
            row.depth = depth + 1;
            row.binding = bindingIndex;
            row.track = static_cast<int>(trackIndex);
            row.label = binding.tracks[trackIndex].propertyId;
            row.fullPath = binding.path;
            row.missing = binding.missingTarget;
            outRows.push_back(row);
        }
    }

    for (const BindingTreeNode& child : node.children) {
        CollectVisibleRowsRecursive(child, clip, state, depth + 1, outRows);
    }
}

static std::vector<TimelineRow> BuildVisibleRows(AnimClip& clip, BindingTreeNode& tree, AnimationEditorState& state) {
    std::vector<TimelineRow> rows;
    rows.reserve(256);
    CollectVisibleRowsRecursive(tree, clip, state, 0, rows);
    return rows;
}

static AnimBinding& EnsureBinding(AnimClip& clip, const std::string& path, const std::string& targetType) {
    for (AnimBinding& binding : clip.bindings) {
        if (binding.path == path && binding.targetType == targetType) {
            return binding;
        }
    }
    AnimBinding binding;
    binding.path = path;
    binding.targetType = targetType;
    clip.bindings.push_back(binding);
    return clip.bindings.back();
}

static AnimTrack& EnsureTrack(AnimBinding& binding, const std::string& propertyId) {
    for (AnimTrack& track : binding.tracks) {
        if (track.propertyId == propertyId) {
            return track;
        }
    }
    AnimTrack track;
    track.propertyId = propertyId;
    binding.tracks.push_back(track);
    return binding.tracks.back();
}

static void AutoBindHierarchyTransformTracks(AnimationEditorState& state,
                                             int rootId,
                                             const std::unordered_map<int, SceneObject*>& byId) {
    auto rootIt = byId.find(rootId);
    if (rootIt == byId.end() || !rootIt->second) {
        return;
    }

    std::vector<const SceneObject*> stack;
    stack.push_back(rootIt->second);
    std::unordered_set<int> visited;
    visited.reserve(byId.size());

    while (!stack.empty()) {
        const SceneObject* obj = stack.back();
        stack.pop_back();
        if (!obj) continue;
        if (!visited.insert(obj->id).second) continue;

        const std::string relPath = BuildRelativePathToRoot(*obj, rootId, byId);
        if (obj->id == rootId || !relPath.empty()) {
            AnimBinding& binding = EnsureBinding(state.clip, relPath, "Transform");
            const std::vector<std::string> properties = BuildAnimatablePropertiesForObject(*obj);
            for (const std::string& property : properties) {
                EnsureTrack(binding, property);
            }
        }

        for (int childId : obj->childIds) {
            auto childIt = byId.find(childId);
            if (childIt != byId.end() && childIt->second) {
                stack.push_back(childIt->second);
            }
        }
    }
}

static void InitializeClipState(AnimationEditorState& state) {
    if (state.initialized) return;
    state.initialized = true;

    state.clip.name = "New Animation";
    state.clip.duration = 2.0f;
    state.clip.sampleRate = 30.0f;
    state.timeline.pixelsPerSecond = 130.0f;
    state.timeline.timeOffset = 0.0f;
    state.currentTime = 0.0f;
    state.previewEnabled = true;
    state.isPlaying = false;
    state.loopPlayback = true;
    state.leftPaneWidth = 300.0f;
    state.curveValueScale = 60.0f;
    state.curveValueOffset = 0.0f;
    state.activeTab = AnimationTab::Dopesheet;
    state.loadedRootObjectId = -1;
    state.loadedClipAssetPath.clear();
    state.lastObservedSelectedObjectId = -1;
    state.renameClipSlotIndex = -1;
    state.clipDirty = false;
    state.clipPathInput[0] = '\0';
    state.newClipNameInput[0] = '\0';
    state.renameClipInput[0] = '\0';

    state.expandedPaths["<root>"] = true;
}

static void ResetClipData(AnimationEditorState& state) {
    state.clip = AnimClip{};
    state.clip.name = "New Animation";
    state.clip.duration = 2.0f;
    state.clip.sampleRate = 30.0f;
    state.selection.keys.clear();
    state.selection.lastClicked.reset();
    state.selection.rangeAnchor.reset();
    state.focusedTrack.reset();
    state.tree = BuildBindingTreeFromClip(state.clip);
    state.currentTime = 0.0f;
    state.timeline.timeOffset = 0.0f;
    state.recordLastValues.clear();
    state.clipDirty = false;
    state.renameClipSlotIndex = -1;
    state.renameClipInput[0] = '\0';
}

static bool SaveClipToFile(const AnimClip& clip, const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "moduanimateVersion 1\n";
    out << "name " << std::quoted(clip.name) << "\n";
    out << "duration " << clip.duration << "\n";
    out << "sampleRate " << clip.sampleRate << "\n";
    out << "bindingCount " << clip.bindings.size() << "\n";
    for (const AnimBinding& binding : clip.bindings) {
        out << "binding " << std::quoted(binding.path) << " " << std::quoted(binding.targetType) << "\n";
        out << "trackCount " << binding.tracks.size() << "\n";
        for (const AnimTrack& track : binding.tracks) {
            out << "track " << std::quoted(track.propertyId) << " "
                << (track.visible ? 1 : 0) << " " << (track.locked ? 1 : 0) << "\n";
            out << "keyCount " << track.keys.size() << "\n";
            for (const AnimKey& key : track.keys) {
                out << "key "
                    << key.uid << " "
                    << key.time << " "
                    << key.value << " "
                    << key.inTangent << " "
                    << key.outTangent << " "
                    << static_cast<int>(key.tangentMode) << " "
                    << static_cast<int>(key.interpolation) << "\n";
            }
        }
    }
    return out.good();
}

static bool LoadClipFromFile(AnimClip& clip, uint64_t& nextUid, const fs::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    AnimClip loaded;
    loaded.bindings.clear();
    std::string token;
    size_t expectedBindings = 0;
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
            in >> expectedBindings;
            loaded.bindings.reserve(expectedBindings);
        } else if (token == "binding") {
            AnimBinding binding;
            in >> std::quoted(binding.path) >> std::quoted(binding.targetType);
            std::string trackCountToken;
            size_t trackCount = 0;
            in >> trackCountToken >> trackCount;
            if (trackCountToken != "trackCount") return false;
            binding.tracks.reserve(trackCount);
            for (size_t ti = 0; ti < trackCount; ++ti) {
                std::string trackToken;
                in >> trackToken;
                if (trackToken != "track") return false;
                AnimTrack track;
                int visible = 1;
                int locked = 0;
                in >> std::quoted(track.propertyId) >> visible >> locked;
                track.visible = (visible != 0);
                track.locked = (locked != 0);
                std::string keyCountToken;
                size_t keyCount = 0;
                in >> keyCountToken >> keyCount;
                if (keyCountToken != "keyCount") return false;
                track.keys.reserve(keyCount);
                for (size_t ki = 0; ki < keyCount; ++ki) {
                    std::string keyToken;
                    in >> keyToken;
                    if (keyToken != "key") return false;
                    AnimKey key;
                    int tangentMode = 0;
                    int interp = 1;
                    in >> key.uid >> key.time >> key.value >> key.inTangent >> key.outTangent >> tangentMode >> interp;
                    key.tangentMode = static_cast<TangentMode>(std::clamp(tangentMode, 0, 2));
                    key.interpolation = static_cast<TrackInterpolation>(std::clamp(interp, 0, 2));
                    track.keys.push_back(key);
                    nextUid = std::max(nextUid, key.uid + 1);
                }
                SortTrackKeys(track);
                binding.tracks.push_back(track);
            }
            loaded.bindings.push_back(binding);
        } else {
            std::string discard;
            std::getline(in, discard);
        }
    }

    if (!in.eof() && in.fail()) return false;
    loaded.duration = std::max(0.1f, loaded.duration);
    loaded.sampleRate = std::clamp(loaded.sampleRate, 1.0f, 240.0f);
    clip = std::move(loaded);
    return true;
}

static void UpsertTrackKey(AnimationEditorState& state,
                           AnimTrack& track,
                           float time,
                           float value) {
    const float tolerance = 0.25f / std::max(1.0f, state.clip.sampleRate);
    for (AnimKey& key : track.keys) {
        if (std::abs(key.time - time) <= tolerance) {
            key.time = time;
            key.value = value;
            state.clipDirty = true;
            return;
        }
    }

    AnimKey key;
    key.uid = state.nextKeyUid++;
    key.time = time;
    key.value = value;
    key.interpolation = TrackInterpolation::Linear;
    key.tangentMode = TangentMode::Linear;
    track.keys.push_back(key);
    SortTrackKeys(track);
    state.clipDirty = true;
}

static void KeyAllBoundTracksAtTime(AnimationEditorState& state,
                                    float time,
                                    std::unordered_map<int, SceneObject*>& objectById) {
    const float clamped = std::clamp(time, 0.0f, state.clip.duration);
    for (AnimBinding& binding : state.clip.bindings) {
        if (!binding.resolvedTarget) continue;
        SceneObject* targetObject = nullptr;
        auto itObj = objectById.find(binding.resolvedTarget->objectId);
        if (itObj != objectById.end()) {
            targetObject = itObj->second;
        }
        for (AnimTrack& track : binding.tracks) {
            if (track.locked) continue;
            float value = 0.0f;
            if (IsLocalTransformProperty(track.propertyId)) {
                value = ReadPropertyFromNode(*binding.resolvedTarget, track.propertyId);
            } else {
                if (!targetObject) continue;
                if (!ReadAnimatableProperty(*targetObject, track.propertyId, value)) continue;
            }
            UpsertTrackKey(state, track, clamped, value);
        }
    }
}

static float RecordThresholdForProperty(const std::string& propertyId) {
    if (propertyId == "Light.Enabled" ||
        propertyId == "Audio.Loop" ||
        propertyId == "Audio.Spatial") {
        return 0.5f;
    }
    if (propertyId.rfind("ScriptSetting.", 0) == 0) {
        return 0.0005f;
    }
    if (propertyId.rfind("localRotation.", 0) == 0) {
        return 0.05f;
    }
    if (propertyId.rfind("localPosition.", 0) == 0 ||
        propertyId.rfind("localScale.", 0) == 0) {
        return 0.0005f;
    }
    return 0.001f;
}

static void RecordChangedProperties(AnimationEditorState& state,
                                    int rootId,
                                    std::unordered_map<int, SceneObject*>& objectById) {
    auto itRoot = objectById.find(rootId);
    if (itRoot == objectById.end() || !itRoot->second) return;

    std::vector<SceneObject*> stack;
    stack.push_back(itRoot->second);
    std::unordered_set<int> visited;
    visited.reserve(objectById.size());

    const float clamped = std::clamp(state.currentTime, 0.0f, state.clip.duration);
    const bool firstFrame = state.recordLastValues.empty();

    while (!stack.empty()) {
        SceneObject* obj = stack.back();
        stack.pop_back();
        if (!obj) continue;
        if (!visited.insert(obj->id).second) continue;

        const std::string relPath = BuildRelativePathToRoot(*obj, rootId, objectById);
        if (!(obj->id == rootId || !relPath.empty())) {
            continue;
        }

        const std::vector<std::string> properties = BuildAnimatablePropertiesForObject(*obj);
        for (const std::string& propertyId : properties) {
            float currentValue = 0.0f;
            if (!ReadAnimatableProperty(*obj, propertyId, currentValue)) continue;

            const std::string key = relPath + "|" + propertyId;
            auto itPrev = state.recordLastValues.find(key);
            if (itPrev == state.recordLastValues.end()) {
                state.recordLastValues[key] = currentValue;
                if (firstFrame) continue;
                itPrev = state.recordLastValues.find(key);
            }

            const float threshold = RecordThresholdForProperty(propertyId);
            if (std::abs(currentValue - itPrev->second) >= threshold) {
                AnimBinding& binding = EnsureBinding(state.clip, relPath, "Transform");
                AnimTrack& track = EnsureTrack(binding, propertyId);
                UpsertTrackKey(state, track, clamped, currentValue);
                itPrev->second = currentValue;
            }
        }

        for (int childId : obj->childIds) {
            auto itChild = objectById.find(childId);
            if (itChild != objectById.end() && itChild->second) {
                stack.push_back(itChild->second);
            }
        }
    }
}

static SceneGraphBridge BuildSceneGraphFromObjects(const std::vector<SceneObject>& objects) {
    SceneGraphBridge graph;
    graph.root.name = "<SceneRoot>";
    graph.root.objectId = -1;

    graph.nodes.reserve(objects.size());
    for (const SceneObject& obj : objects) {
        auto node = std::make_unique<SceneNode>();
        node->name = obj.name;
        node->objectId = obj.id;
        node->parent = nullptr;
        node->local.localPosition = obj.localInitialized ? obj.localPosition : obj.position;
        node->local.localScale = obj.localInitialized ? obj.localScale : obj.scale;
        const glm::vec3 euler = obj.localInitialized ? obj.localRotation : obj.rotation;
        node->local.localRotation = glm::vec4(euler.x, euler.y, euler.z, 1.0f);
        graph.byObjectId[obj.id] = node.get();
        graph.nodes.push_back(std::move(node));
    }

    for (const SceneObject& obj : objects) {
        SceneNode* node = graph.byObjectId[obj.id];
        if (obj.parentId >= 0) {
            auto it = graph.byObjectId.find(obj.parentId);
            if (it != graph.byObjectId.end()) {
                node->parent = it->second;
                it->second->children.push_back(node);
                continue;
            }
        }
        node->parent = &graph.root;
        graph.root.children.push_back(node);
    }

    return graph;
}

static void ApplySceneGraphToObjects(SceneGraphBridge& graph,
                                     std::unordered_map<int, SceneObject*>& objectById) {
    for (auto& ptr : graph.nodes) {
        SceneNode& node = *ptr;
        auto it = objectById.find(node.objectId);
        if (it == objectById.end() || !it->second) continue;

        SceneObject& obj = *it->second;
        obj.localPosition = node.local.localPosition;
        obj.localRotation = NormalizeEulerDegrees(glm::vec3(
            node.local.localRotation.x,
            node.local.localRotation.y,
            node.local.localRotation.z
        ));
        obj.localScale = node.local.localScale;
        obj.localInitialized = true;
    }
}

static void EvaluateNonTransformTracksAtTime(AnimClip& clip,
                                             float time,
                                             std::unordered_map<int, SceneObject*>& objectById) {
    for (AnimBinding& binding : clip.bindings) {
        if (!binding.resolvedTarget) continue;
        auto itObj = objectById.find(binding.resolvedTarget->objectId);
        if (itObj == objectById.end() || !itObj->second) continue;
        SceneObject& obj = *itObj->second;

        for (const AnimTrack& track : binding.tracks) {
            if (!track.visible || track.locked) continue;
            if (IsLocalTransformProperty(track.propertyId)) continue;
            const float sampled = SampleTrackValue(track, time);
            WriteAnimatableProperty(obj, track.propertyId, sampled);
        }
    }
}

static std::vector<TrackHandle> BuildCurveTrackGroup(const AnimClip& clip,
                                                     const std::optional<TrackHandle>& focusedTrack) {
    std::vector<TrackHandle> group;
    if (!focusedTrack.has_value()) return group;

    const int bindingIndex = focusedTrack->binding;
    const int trackIndex = focusedTrack->track;
    if (bindingIndex < 0 || bindingIndex >= static_cast<int>(clip.bindings.size())) return group;
    const AnimBinding& binding = clip.bindings[bindingIndex];
    if (trackIndex < 0 || trackIndex >= static_cast<int>(binding.tracks.size())) return group;

    const std::string prefix = TrackGroupPrefix(binding.tracks[trackIndex].propertyId);
    for (int i = 0; i < static_cast<int>(binding.tracks.size()); ++i) {
        if (TrackGroupPrefix(binding.tracks[i].propertyId) == prefix) {
            group.push_back({bindingIndex, i});
        }
    }

    if (group.empty()) {
        group.push_back(*focusedTrack);
    }

    return group;
}

static ImU32 CurveColorByProperty(const std::string& propertyId) {
    if (propertyId.size() > 2 && propertyId.substr(propertyId.size() - 2) == ".x") return IM_COL32(240, 78, 78, 255);
    if (propertyId.size() > 2 && propertyId.substr(propertyId.size() - 2) == ".y") return IM_COL32(89, 203, 118, 255);
    if (propertyId.size() > 2 && propertyId.substr(propertyId.size() - 2) == ".z") return IM_COL32(84, 170, 255, 255);
    if (propertyId.size() > 2 && propertyId.substr(propertyId.size() - 2) == ".w") return IM_COL32(246, 220, 64, 255);
    return IM_COL32(230, 230, 230, 255);
}

static void DeleteSelectedKeys(AnimationEditorState& state) {
    std::unordered_map<int, std::unordered_set<uint64_t>> deletionByTrack;
    for (const KeyHandle& key : state.selection.keys) {
        const int composite = key.binding * 100000 + key.track;
        deletionByTrack[composite].insert(key.keyUid);
    }

    for (auto& kv : deletionByTrack) {
        int binding = kv.first / 100000;
        int track = kv.first % 100000;
        if (binding < 0 || binding >= static_cast<int>(state.clip.bindings.size())) continue;
        AnimBinding& b = state.clip.bindings[binding];
        if (track < 0 || track >= static_cast<int>(b.tracks.size())) continue;
        AnimTrack& t = b.tracks[track];
        t.keys.erase(std::remove_if(t.keys.begin(), t.keys.end(), [&](const AnimKey& k) {
            return kv.second.find(k.uid) != kv.second.end();
        }), t.keys.end());
        state.clipDirty = true;
    }

    ClearSelection(state.selection);
}

static void CopySelectedKeys(AnimationEditorState& state) {
    state.clipboard.clear();
    state.clipboard.reserve(state.selection.keys.size());
    for (const KeyHandle& handle : state.selection.keys) {
        const AnimKey* key = FindKey(state.clip, handle);
        if (!key) continue;
        CopiedKey copied;
        copied.binding = handle.binding;
        copied.track = handle.track;
        copied.key = *key;
        state.clipboard.push_back(copied);
    }
}

static void PasteKeysAt(AnimationEditorState& state, float targetTime) {
    if (state.clipboard.empty()) return;

    float sourceMin = std::numeric_limits<float>::max();
    for (const CopiedKey& ck : state.clipboard) {
        sourceMin = std::min(sourceMin, ck.key.time);
    }

    ClearSelection(state.selection);

    for (const CopiedKey& ck : state.clipboard) {
        if (ck.binding < 0 || ck.binding >= static_cast<int>(state.clip.bindings.size())) continue;
        AnimBinding& binding = state.clip.bindings[ck.binding];
        if (ck.track < 0 || ck.track >= static_cast<int>(binding.tracks.size())) continue;

        AnimKey pasted = ck.key;
        pasted.uid = state.nextKeyUid++;
        pasted.time = std::max(0.0f, targetTime + (ck.key.time - sourceMin));
        binding.tracks[ck.track].keys.push_back(pasted);
        SortTrackKeys(binding.tracks[ck.track]);
        state.clipDirty = true;

        KeyHandle newHandle;
        newHandle.binding = ck.binding;
        newHandle.track = ck.track;
        newHandle.keyUid = pasted.uid;
        state.selection.keys.insert(newHandle);
        state.selection.lastClicked = newHandle;
        state.selection.rangeAnchor = newHandle;
    }
}

static void DuplicateSelectionForDrag(AnimationEditorState& state) {
    std::vector<KeyHandle> orderedKeys;
    orderedKeys.reserve(state.selection.keys.size());
    for (const KeyHandle& k : state.selection.keys) {
        orderedKeys.push_back(k);
    }

    ClearSelection(state.selection);

    for (const KeyHandle& source : orderedKeys) {
        if (source.binding < 0 || source.binding >= static_cast<int>(state.clip.bindings.size())) continue;
        AnimBinding& binding = state.clip.bindings[source.binding];
        if (source.track < 0 || source.track >= static_cast<int>(binding.tracks.size())) continue;
        AnimTrack& track = binding.tracks[source.track];

        AnimKey* key = FindKey(state.clip, source);
        if (!key) continue;

        AnimKey duplicated = *key;
        duplicated.uid = state.nextKeyUid++;
        track.keys.push_back(duplicated);
        state.clipDirty = true;

        KeyHandle dst;
        dst.binding = source.binding;
        dst.track = source.track;
        dst.keyUid = duplicated.uid;
        state.selection.keys.insert(dst);
        state.selection.lastClicked = dst;
        state.selection.rangeAnchor = dst;
    }

    for (AnimBinding& b : state.clip.bindings) {
        for (AnimTrack& t : b.tracks) {
            SortTrackKeys(t);
        }
    }

    state.dragStartTimes.clear();
    state.dragStartValues.clear();
    for (const KeyHandle& selected : state.selection.keys) {
        if (AnimKey* key = FindKey(state.clip, selected)) {
            state.dragStartTimes[selected] = key->time;
            state.dragStartValues[selected] = key->value;
        }
    }
}

static void DrawLeftPane(AnimationEditorState& state,
                         std::vector<TimelineRow>& rows,
                         float rowHeight,
                         float height,
                         float splitterPadding,
                         bool& consumedWheel) {
    ImGui::BeginChild("AnimBindingTreePane", ImVec2(state.leftPaneWidth - splitterPadding, height), true);

    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;

    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        TimelineRow& row = rows[i];
        const ImVec2 rowMin(base.x, base.y + i * rowHeight);
        const ImVec2 rowMax(base.x + width, rowMin.y + rowHeight);
        const ImRect rowRect(rowMin, rowMax);

        if (row.type == RowType::Track && state.focusedTrack.has_value() &&
            state.focusedTrack->binding == row.binding && state.focusedTrack->track == row.track) {
            ImGui::GetWindowDrawList()->AddRectFilled(rowRect.Min, rowRect.Max, IM_COL32(48, 94, 156, 115));
        }

        if (i % 2 == 1) {
            ImGui::GetWindowDrawList()->AddRectFilled(rowRect.Min, rowRect.Max, IM_COL32(255, 255, 255, 10));
        }

        const float indent = 14.0f * static_cast<float>(row.depth);
        float x = rowRect.Min.x + indent + 6.0f;

        if (row.type == RowType::Path && row.expandable) {
            const std::string expansionKey = row.fullPath.empty() ? "<root>" : row.fullPath;
            const bool expanded = (state.expandedPaths.find(expansionKey) == state.expandedPaths.end())
                ? true
                : state.expandedPaths[expansionKey];
            const char* arrow = expanded ? "v" : ">";
            ImGui::GetWindowDrawList()->AddText(ImVec2(x, rowRect.Min.y + 2.0f), IM_COL32(220, 220, 220, 255), arrow);
            x += 12.0f;
        }

        const ImU32 textColor = row.missing ? IM_COL32(255, 140, 140, 255) : IM_COL32(220, 220, 220, 255);
        ImGui::GetWindowDrawList()->AddText(ImVec2(x, rowRect.Min.y + 2.0f), textColor, row.label.c_str());

        ImGui::SetCursorScreenPos(rowRect.Min);
        ImGui::PushID(i);
        ImGui::InvisibleButton("##left_row", ImVec2(width, rowHeight));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (row.type == RowType::Path && row.expandable) {
                const std::string expansionKey = row.fullPath.empty() ? "<root>" : row.fullPath;
                const bool expanded = (state.expandedPaths.find(expansionKey) == state.expandedPaths.end())
                    ? true
                    : state.expandedPaths[expansionKey];
                state.expandedPaths[expansionKey] = !expanded;
            } else if (row.type == RowType::Track) {
                state.focusedTrack = TrackHandle{row.binding, row.track};
            }
        }
        ImGui::PopID();
    }

    const float requiredHeight = rows.empty() ? rowHeight : rowHeight * static_cast<float>(rows.size());
    ImGui::Dummy(ImVec2(width, requiredHeight));

    if (ImGui::IsWindowHovered() && std::abs(ImGui::GetIO().MouseWheel) > 0.0f) {
        consumedWheel = true;
    }
    ImGui::EndChild();
}

static void DrawTimeRuler(ImDrawList* draw,
                          const ImRect& rulerRect,
                          const TimelineTransform& transform,
                          float xOrigin,
                          float sampleRate) {
    draw->AddRectFilled(rulerRect.Min, rulerRect.Max, IM_COL32(46, 46, 46, 255));
    draw->AddLine(ImVec2(rulerRect.Min.x, rulerRect.Max.y), rulerRect.Max, IM_COL32(75, 75, 75, 255), 1.0f);

    const float major = PickMajorTimeStep(transform.pixelsPerSecond);
    const float minor = major / 5.0f;

    const float tMin = transform.ScreenToTime(rulerRect.Min.x, xOrigin);
    const float tMax = transform.ScreenToTime(rulerRect.Max.x, xOrigin);
    const float startMinor = std::floor(tMin / minor) * minor;

    for (float t = startMinor; t <= tMax + major; t += minor) {
        const float x = transform.TimeToScreen(t, xOrigin);
        if (x < rulerRect.Min.x - 1.0f || x > rulerRect.Max.x + 1.0f) continue;

        const bool isMajor = std::fmod(std::abs(t), major) < (minor * 0.5f);
        const float y0 = isMajor ? rulerRect.Min.y + 2.0f : rulerRect.Min.y + 10.0f;
        const ImU32 color = isMajor ? IM_COL32(145, 145, 145, 255) : IM_COL32(95, 95, 95, 255);
        draw->AddLine(ImVec2(x, y0), ImVec2(x, rulerRect.Max.y), color, 1.0f);

        if (isMajor && t >= -0.001f) {
            const std::string label = FormatTimelineTime(t, sampleRate);
            draw->AddText(ImVec2(x + 2.0f, rulerRect.Min.y + 2.0f), IM_COL32(206, 206, 206, 255), label.c_str());
        }
    }
}

static void BeginKeyDrag(AnimationEditorState& state) {
    state.draggingKeys = true;
    state.dragWasDuplicated = false;
    state.dragDuplicateRequest = ImGui::GetIO().KeyAlt;
    state.dragMouseStart = ImGui::GetIO().MousePos;
    state.dragStartTimes.clear();
    state.dragStartValues.clear();
    for (const KeyHandle& handle : state.selection.keys) {
        if (AnimKey* key = FindKey(state.clip, handle)) {
            state.dragStartTimes[handle] = key->time;
            state.dragStartValues[handle] = key->value;
        }
    }
}

static void EndKeyDrag(AnimationEditorState& state) {
    state.draggingKeys = false;
    state.dragWasDuplicated = false;
    state.dragDuplicateRequest = false;
    state.dragStartTimes.clear();
    state.dragStartValues.clear();

    for (AnimBinding& binding : state.clip.bindings) {
        for (AnimTrack& track : binding.tracks) {
            SortTrackKeys(track);
        }
    }
}

static void FlipClipInTime(AnimationEditorState& state) {
    const float duration = std::max(0.0f, state.clip.duration);
    for (AnimBinding& binding : state.clip.bindings) {
        for (AnimTrack& track : binding.tracks) {
            for (AnimKey& key : track.keys) {
                const float oldIn = key.inTangent;
                const float oldOut = key.outTangent;
                key.time = std::clamp(duration - key.time, 0.0f, duration);
                // Reversing timeline swaps tangent sides and inverts slope direction.
                key.inTangent = -oldOut;
                key.outTangent = -oldIn;
            }
            SortTrackKeys(track);
        }
    }
    state.clipDirty = true;
}

static bool PerformTrackRangeSelection(AnimationEditorState& state,
                                       const KeyHandle& clicked,
                                       bool additive) {
    if (!state.selection.lastClicked.has_value()) return false;
    const KeyHandle anchor = *state.selection.lastClicked;
    if (anchor.binding != clicked.binding || anchor.track != clicked.track) return false;

    if (clicked.binding < 0 || clicked.binding >= static_cast<int>(state.clip.bindings.size())) return false;
    AnimBinding& binding = state.clip.bindings[clicked.binding];
    if (clicked.track < 0 || clicked.track >= static_cast<int>(binding.tracks.size())) return false;
    const AnimTrack& track = binding.tracks[clicked.track];

    const int a = FindKeyIndexByUid(track, anchor.keyUid);
    const int b = FindKeyIndexByUid(track, clicked.keyUid);
    if (a < 0 || b < 0) return false;

    const int start = std::min(a, b);
    const int end = std::max(a, b);

    if (!additive) {
        state.selection.keys.clear();
    }

    for (int i = start; i <= end; ++i) {
        KeyHandle h;
        h.binding = clicked.binding;
        h.track = clicked.track;
        h.keyUid = track.keys[i].uid;
        state.selection.keys.insert(h);
    }

    state.selection.lastClicked = clicked;
    return true;
}

static void DrawDopesheet(AnimationEditorState& state,
                          std::vector<TimelineRow>& rows,
                          float height,
                          bool& consumedWheel) {
    constexpr float kRulerHeight = 24.0f;
    constexpr float kRowHeight = 20.0f;
    constexpr float kDiamondSize = 6.0f;

    ImGui::BeginChild("AnimDopesheetPane", ImVec2(0.0f, height), true,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float width = ImMax(ImGui::GetContentRegionAvail().x, 100.0f);
    const float contentHeight = kRulerHeight + kRowHeight * static_cast<float>(rows.size());

    ImGui::InvisibleButton("##DopesheetCanvas", ImVec2(width, contentHeight),
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    const ImRect canvasRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const ImRect rulerRect(canvasRect.Min, ImVec2(canvasRect.Max.x, canvasRect.Min.y + kRulerHeight));

    draw->AddRectFilled(canvasRect.Min, canvasRect.Max, IM_COL32(38, 38, 38, 255));
    DrawTimeRuler(draw, rulerRect, state.timeline, canvasRect.Min.x, state.clip.sampleRate);

    const float majorStep = PickMajorTimeStep(state.timeline.pixelsPerSecond);
    const float startTime = state.timeline.ScreenToTime(canvasRect.Min.x, canvasRect.Min.x);
    const float endTime = state.timeline.ScreenToTime(canvasRect.Max.x, canvasRect.Min.x);
    const float beginGrid = std::floor(startTime / majorStep) * majorStep;

    // I forgot I already had a helper for this in UltraTreronEngine... in freaking C#.
    for (float t = beginGrid; t <= endTime + majorStep; t += majorStep) {
        const float x = state.timeline.TimeToScreen(t, canvasRect.Min.x);
        draw->AddLine(ImVec2(x, rulerRect.Max.y), ImVec2(x, canvasRect.Max.y), IM_COL32(70, 70, 70, 180), 1.0f);
    }

    KeyHandle hoveredKey;
    bool hasHoveredKey = false;
    float hoveredDistance = std::numeric_limits<float>::max();

    std::optional<TrackHandle> hoveredTrack;

    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        TimelineRow& row = rows[i];
        const float y0 = canvasRect.Min.y + kRulerHeight + i * kRowHeight;
        const float y1 = y0 + kRowHeight;
        const ImRect rowRect(ImVec2(canvasRect.Min.x, y0), ImVec2(canvasRect.Max.x, y1));

        if (i % 2 == 1) {
            draw->AddRectFilled(rowRect.Min, rowRect.Max, IM_COL32(255, 255, 255, 6));
        }

        if (row.type != RowType::Track) {
            draw->AddLine(ImVec2(rowRect.Min.x, rowRect.Max.y), ImVec2(rowRect.Max.x, rowRect.Max.y), IM_COL32(60, 60, 60, 255), 1.0f);
            continue;
        }

        hoveredTrack = std::nullopt;
        if (ImGui::GetIO().MousePos.y >= rowRect.Min.y && ImGui::GetIO().MousePos.y <= rowRect.Max.y &&
            ImGui::GetIO().MousePos.x >= rowRect.Min.x && ImGui::GetIO().MousePos.x <= rowRect.Max.x) {
            hoveredTrack = TrackHandle{row.binding, row.track};
        }

        if (row.binding < 0 || row.binding >= static_cast<int>(state.clip.bindings.size())) continue;
        AnimBinding& binding = state.clip.bindings[row.binding];
        if (row.track < 0 || row.track >= static_cast<int>(binding.tracks.size())) continue;
        AnimTrack& track = binding.tracks[row.track];

        for (const AnimKey& key : track.keys) {
            const float x = state.timeline.TimeToScreen(key.time, canvasRect.Min.x);
            const ImVec2 center(x, 0.5f * (rowRect.Min.y + rowRect.Max.y));
            const bool selected = IsSelected(state.selection, KeyHandle{row.binding, row.track, key.uid});
            const ImU32 col = selected ? IM_COL32(255, 206, 78, 255) : IM_COL32(220, 220, 220, 255);
            draw->AddQuadFilled(
                ImVec2(center.x, center.y - kDiamondSize),
                ImVec2(center.x + kDiamondSize, center.y),
                ImVec2(center.x, center.y + kDiamondSize),
                ImVec2(center.x - kDiamondSize, center.y),
                col
            );
            draw->AddQuad(
                ImVec2(center.x, center.y - kDiamondSize),
                ImVec2(center.x + kDiamondSize, center.y),
                ImVec2(center.x, center.y + kDiamondSize),
                ImVec2(center.x - kDiamondSize, center.y),
                IM_COL32(20, 20, 20, 255)
            );

            if (PointInDiamond(ImGui::GetIO().MousePos, center, kDiamondSize + 2.0f)) {
                const float dist = std::abs(ImGui::GetIO().MousePos.x - center.x) + std::abs(ImGui::GetIO().MousePos.y - center.y);
                if (!hasHoveredKey || dist < hoveredDistance) {
                    hoveredDistance = dist;
                    hoveredKey = KeyHandle{row.binding, row.track, key.uid};
                    hasHoveredKey = true;
                }
            }
        }

        draw->AddLine(ImVec2(rowRect.Min.x, rowRect.Max.y), ImVec2(rowRect.Max.x, rowRect.Max.y), IM_COL32(60, 60, 60, 255), 1.0f);
    }

    const float playheadX = state.timeline.TimeToScreen(state.currentTime, canvasRect.Min.x);
    draw->AddLine(ImVec2(playheadX, canvasRect.Min.y), ImVec2(playheadX, canvasRect.Max.y), IM_COL32(255, 64, 64, 255), 1.5f);

    const bool hoveredCanvas = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();

    if (hoveredCanvas) {
        if (io.MouseWheel != 0.0f && io.KeyCtrl) {
            consumedWheel = true;
            const float zoomFactor = std::pow(1.15f, io.MouseWheel);
            state.timeline.ZoomAt(io.MousePos.x, canvasRect.Min.x, zoomFactor);
        }

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
            state.timeline.PanPixels(io.MouseDelta.x);
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const bool ctrl = io.KeyCtrl;
            const bool shift = io.KeyShift;
            const bool alt = io.KeyAlt;

            if (io.MousePos.y <= rulerRect.Max.y) {
                float current = std::clamp(state.timeline.ScreenToTime(io.MousePos.x, canvasRect.Min.x), 0.0f, state.clip.duration);
                current = SnapTimeToFrames(state, current, state.timeline.pixelsPerSecond, false);
                state.currentTime = std::clamp(current, 0.0f, state.clip.duration);
            }

            if (hasHoveredKey) {
                if (shift) {
                    if (!PerformTrackRangeSelection(state, hoveredKey, ctrl)) {
                        if (!ctrl) state.selection.keys.clear();
                        state.selection.keys.insert(hoveredKey);
                        state.selection.lastClicked = hoveredKey;
                    }
                } else if (ctrl) {
                    ToggleSelection(state.selection, hoveredKey);
                } else {
                    if (!IsSelected(state.selection, hoveredKey)) {
                        SelectSingle(state.selection, hoveredKey);
                    }
                }

                if (!ctrl && !shift) {
                    state.dragDuplicateRequest = alt;
                    BeginKeyDrag(state);
                }
            } else if (io.MousePos.y > rulerRect.Max.y) {
                if (!ctrl && !shift) {
                    ClearSelection(state.selection);
                }
                state.boxSelectActive = true;
                state.boxSelectStart = io.MousePos;
                state.boxSelectEnd = io.MousePos;
            }
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            state.contextTrack = hoveredTrack;
            state.contextTime = std::clamp(state.timeline.ScreenToTime(io.MousePos.x, canvasRect.Min.x), 0.0f, state.clip.duration);
            ImGui::OpenPopup("DopesheetContextMenu");
        }
    }

    if (state.draggingKeys) {
        if (state.dragDuplicateRequest && !state.dragWasDuplicated) {
            DuplicateSelectionForDrag(state);
            state.dragWasDuplicated = true;
        }

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float deltaTime = (io.MousePos.x - state.dragMouseStart.x) / std::max(1.0f, state.timeline.pixelsPerSecond);
            for (const auto& kv : state.dragStartTimes) {
                const KeyHandle& handle = kv.first;
                if (AnimKey* key = FindKey(state.clip, handle)) {
                    float newTime = std::max(0.0f, kv.second + deltaTime);
                    newTime = SnapTimeToFrames(state, newTime, state.timeline.pixelsPerSecond, io.KeyShift);
                    key->time = std::clamp(newTime, 0.0f, state.clip.duration);
                    state.clipDirty = true;
                }
            }
            for (AnimBinding& binding : state.clip.bindings) {
                for (AnimTrack& track : binding.tracks) {
                    SortTrackKeys(track);
                }
            }
        } else {
            EndKeyDrag(state);
        }
    }

    if (state.boxSelectActive) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            state.boxSelectEnd = io.MousePos;
            const ImRect box = NormalizeRect(state.boxSelectStart, state.boxSelectEnd);
            draw->AddRectFilled(box.Min, box.Max, IM_COL32(98, 166, 255, 35));
            draw->AddRect(box.Min, box.Max, IM_COL32(98, 166, 255, 220));
        } else {
            const bool additive = io.KeyCtrl;
            if (!additive) {
                state.selection.keys.clear();
            }

            const ImRect box = NormalizeRect(state.boxSelectStart, state.boxSelectEnd);
            for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
                TimelineRow& row = rows[i];
                if (row.type != RowType::Track) continue;
                if (row.binding < 0 || row.binding >= static_cast<int>(state.clip.bindings.size())) continue;
                AnimBinding& b = state.clip.bindings[row.binding];
                if (row.track < 0 || row.track >= static_cast<int>(b.tracks.size())) continue;
                AnimTrack& track = b.tracks[row.track];

                const float y0 = canvasRect.Min.y + kRulerHeight + i * kRowHeight;
                const float y1 = y0 + kRowHeight;
                const float cy = 0.5f * (y0 + y1);
                for (const AnimKey& key : track.keys) {
                    const float cx = state.timeline.TimeToScreen(key.time, canvasRect.Min.x);
                    if (box.Contains(ImVec2(cx, cy))) {
                        state.selection.keys.insert(KeyHandle{row.binding, row.track, key.uid});
                    }
                }
            }

            state.boxSelectActive = false;
        }
    }

    if (ImGui::BeginPopup("DopesheetContextMenu")) {
        if (ImGui::MenuItem("Add Key", nullptr, false, state.contextTrack.has_value())) {
            if (state.contextTrack.has_value()) {
                TrackHandle t = *state.contextTrack;
                if (t.binding >= 0 && t.binding < static_cast<int>(state.clip.bindings.size())) {
                    AnimBinding& binding = state.clip.bindings[t.binding];
                    if (t.track >= 0 && t.track < static_cast<int>(binding.tracks.size())) {
                        AnimTrack& track = binding.tracks[t.track];
                        float value = 0.0f;
                        if (binding.resolvedTarget) {
                            value = ReadPropertyFromNode(*binding.resolvedTarget, track.propertyId);
                        } else if (!track.keys.empty()) {
                            value = SampleTrackValue(track, state.contextTime);
                        }
                        AnimKey key;
                        key.uid = state.nextKeyUid++;
                        key.time = state.contextTime;
                        key.value = value;
                        key.interpolation = TrackInterpolation::Linear;
                        track.keys.push_back(key);
                        SortTrackKeys(track);
                        state.clipDirty = true;
                        SelectSingle(state.selection, KeyHandle{t.binding, t.track, key.uid});
                    }
                }
            }
        }

        if (ImGui::MenuItem("Select All Keys", "Ctrl+A")) {
            SelectAllKeys(state);
        }

        if (ImGui::MenuItem("Delete Keys", "Del", false, !state.selection.keys.empty())) {
            DeleteSelectedKeys(state);
        }
        if (ImGui::MenuItem("Copy Keys", "Ctrl+C", false, !state.selection.keys.empty())) {
            CopySelectedKeys(state);
        }
        if (ImGui::MenuItem("Paste Keys", "Ctrl+V", false, !state.clipboard.empty())) {
            PasteKeysAt(state, state.contextTime);
        }
        DrawInterpolationPresetMenu(state);

        ImGui::EndPopup();
    }

    ImGui::EndChild();
}

static void DrawCurves(AnimationEditorState& state,
                       std::vector<TimelineRow>& rows,
                       float height,
                       bool& consumedWheel) {
    (void)rows;
    constexpr float kRulerHeight = 24.0f;
    constexpr float kPointRadius = 4.0f;
    constexpr float kTangentHandlePx = 42.0f;

    ImGui::BeginChild("AnimCurvesPane", ImVec2(0.0f, height), true,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);

    const std::vector<TrackHandle> group = BuildCurveTrackGroup(state.clip, state.focusedTrack);
    if (group.empty()) {
        ImGui::TextDisabled("Select a track in the left pane to edit curves.");
        ImGui::EndChild();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float width = ImMax(ImGui::GetContentRegionAvail().x, 100.0f);
    const float contentHeight = ImMax(height * 1.1f, 240.0f);

    ImGui::InvisibleButton("##CurveCanvas", ImVec2(width, contentHeight),
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    const ImRect canvasRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const ImRect rulerRect(canvasRect.Min, ImVec2(canvasRect.Max.x, canvasRect.Min.y + kRulerHeight));
    const ImRect graphRect(ImVec2(canvasRect.Min.x, rulerRect.Max.y), canvasRect.Max);

    draw->AddRectFilled(canvasRect.Min, canvasRect.Max, IM_COL32(33, 33, 33, 255));
    DrawTimeRuler(draw, rulerRect, state.timeline, canvasRect.Min.x, state.clip.sampleRate);

    auto valueToY = [&](float value) {
        const float centerY = 0.5f * (graphRect.Min.y + graphRect.Max.y);
        return centerY - (value - state.curveValueOffset) * state.curveValueScale;
    };

    auto yToValue = [&](float y) {
        const float centerY = 0.5f * (graphRect.Min.y + graphRect.Max.y);
        return state.curveValueOffset + (centerY - y) / std::max(1.0f, state.curveValueScale);
    };

    const float majorStep = PickMajorTimeStep(state.timeline.pixelsPerSecond);
    const float startTime = state.timeline.ScreenToTime(graphRect.Min.x, canvasRect.Min.x);
    const float endTime = state.timeline.ScreenToTime(graphRect.Max.x, canvasRect.Min.x);
    const float beginGrid = std::floor(startTime / majorStep) * majorStep;

    // Grid first, chaos second.
    for (float t = beginGrid; t <= endTime + majorStep; t += majorStep) {
        const float x = state.timeline.TimeToScreen(t, canvasRect.Min.x);
        draw->AddLine(ImVec2(x, graphRect.Min.y), ImVec2(x, graphRect.Max.y), IM_COL32(66, 66, 66, 220), 1.0f);
    }

    const float valueGridStep = std::max(0.1f, 40.0f / std::max(1.0f, state.curveValueScale));
    const float minVal = yToValue(graphRect.Max.y);
    const float maxVal = yToValue(graphRect.Min.y);
    const float startVal = std::floor(minVal / valueGridStep) * valueGridStep;
    for (float v = startVal; v <= maxVal + valueGridStep; v += valueGridStep) {
        const float y = valueToY(v);
        draw->AddLine(ImVec2(graphRect.Min.x, y), ImVec2(graphRect.Max.x, y), IM_COL32(58, 58, 58, 200), 1.0f);
    }

    KeyHandle hoveredKey;
    bool hasHoveredKey = false;
    float hoveredDist = std::numeric_limits<float>::max();

    for (const TrackHandle& th : group) {
        if (th.binding < 0 || th.binding >= static_cast<int>(state.clip.bindings.size())) continue;
        AnimBinding& binding = state.clip.bindings[th.binding];
        if (th.track < 0 || th.track >= static_cast<int>(binding.tracks.size())) continue;
        AnimTrack& track = binding.tracks[th.track];

        if (track.keys.empty()) continue;

        const ImU32 curveColor = CurveColorByProperty(track.propertyId);

        for (size_t i = 0; i + 1 < track.keys.size(); ++i) {
            const AnimKey& a = track.keys[i];
            const AnimKey& b = track.keys[i + 1];
            const float x0 = state.timeline.TimeToScreen(a.time, canvasRect.Min.x);
            const float y0 = valueToY(a.value);
            const float x1 = state.timeline.TimeToScreen(b.time, canvasRect.Min.x);
            const float y1 = valueToY(b.value);

            if (a.interpolation == TrackInterpolation::Constant) {
                draw->AddLine(ImVec2(x0, y0), ImVec2(x1, y0), curveColor, 1.5f);
                draw->AddLine(ImVec2(x1, y0), ImVec2(x1, y1), curveColor, 1.0f);
            } else if (a.interpolation == TrackInterpolation::Linear) {
                draw->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), curveColor, 1.5f);
            } else {
                const int segments = 24;
                ImVec2 prev(x0, y0);
                const float dt = std::max(0.0001f, b.time - a.time);
                for (int s = 1; s <= segments; ++s) {
                    const float u = static_cast<float>(s) / static_cast<float>(segments);
                    const float u2 = u * u;
                    const float u3 = u2 * u;
                    const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
                    const float h10 = u3 - 2.0f * u2 + u;
                    const float h01 = -2.0f * u3 + 3.0f * u2;
                    const float h11 = u3 - u2;
                    const float m0 = a.outTangent * dt;
                    const float m1 = b.inTangent * dt;
                    const float value = h00 * a.value + h10 * m0 + h01 * b.value + h11 * m1;
                    const float time = a.time + u * dt;
                    const ImVec2 pt(state.timeline.TimeToScreen(time, canvasRect.Min.x), valueToY(value));
                    draw->AddLine(prev, pt, curveColor, 1.5f);
                    prev = pt;
                }
            }
        }

        for (const AnimKey& key : track.keys) {
            const ImVec2 p(state.timeline.TimeToScreen(key.time, canvasRect.Min.x), valueToY(key.value));
            const bool selected = IsSelected(state.selection, KeyHandle{th.binding, th.track, key.uid});
            draw->AddCircleFilled(p, selected ? kPointRadius + 1.0f : kPointRadius, selected ? IM_COL32(255, 206, 78, 255) : curveColor, 10);
            draw->AddCircle(p, selected ? kPointRadius + 1.0f : kPointRadius, IM_COL32(20, 20, 20, 255), 10, 1.0f);

            const float dist = std::hypot(io.MousePos.x - p.x, io.MousePos.y - p.y);
            if (dist <= (kPointRadius + 3.0f) && dist < hoveredDist) {
                hoveredDist = dist;
                hoveredKey = KeyHandle{th.binding, th.track, key.uid};
                hasHoveredKey = true;
            }
        }
    }

    const float playheadX = state.timeline.TimeToScreen(state.currentTime, canvasRect.Min.x);
    draw->AddLine(ImVec2(playheadX, canvasRect.Min.y), ImVec2(playheadX, canvasRect.Max.y), IM_COL32(255, 64, 64, 255), 1.5f);

    const bool hoveredCanvas = ImGui::IsItemHovered();

    if (hoveredCanvas) {
        if (io.MouseWheel != 0.0f) {
            if (io.KeyCtrl) {
                consumedWheel = true;
                state.timeline.ZoomAt(io.MousePos.x, canvasRect.Min.x, std::pow(1.15f, io.MouseWheel));
            } else if (io.KeyAlt) {
                consumedWheel = true;
                const float centerY = 0.5f * (graphRect.Min.y + graphRect.Max.y);
                const float valueBefore = state.curveValueOffset + (centerY - io.MousePos.y) / std::max(1.0f, state.curveValueScale);
                state.curveValueScale = std::clamp(state.curveValueScale * std::pow(1.15f, io.MouseWheel), 8.0f, 280.0f);
                const float valueAfter = state.curveValueOffset + (centerY - io.MousePos.y) / std::max(1.0f, state.curveValueScale);
                state.curveValueOffset += (valueBefore - valueAfter);
            }
        }

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
            state.timeline.PanPixels(io.MouseDelta.x);
            state.curveValueOffset += io.MouseDelta.y / std::max(1.0f, state.curveValueScale);
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const bool ctrl = io.KeyCtrl;
            const bool shift = io.KeyShift;
            const bool alt = io.KeyAlt;

            if (io.MousePos.y <= rulerRect.Max.y) {
                float current = std::clamp(state.timeline.ScreenToTime(io.MousePos.x, canvasRect.Min.x), 0.0f, state.clip.duration);
                current = SnapTimeToFrames(state, current, state.timeline.pixelsPerSecond, false);
                state.currentTime = std::clamp(current, 0.0f, state.clip.duration);
            }

            if (state.tangentDragActive) {
                state.tangentDragActive = false;
            }

            if (hasHoveredKey) {
                if (shift) {
                    if (!PerformTrackRangeSelection(state, hoveredKey, ctrl)) {
                        if (!ctrl) state.selection.keys.clear();
                        state.selection.keys.insert(hoveredKey);
                        state.selection.lastClicked = hoveredKey;
                    }
                } else if (ctrl) {
                    ToggleSelection(state.selection, hoveredKey);
                } else {
                    if (!IsSelected(state.selection, hoveredKey)) {
                        SelectSingle(state.selection, hoveredKey);
                    }
                }

                if (!ctrl && !shift) {
                    state.dragDuplicateRequest = alt;
                    BeginKeyDrag(state);
                }
            } else if (io.MousePos.y > rulerRect.Max.y) {
                if (!ctrl && !shift) {
                    ClearSelection(state.selection);
                }
                state.curveBoxSelectActive = true;
                state.curveBoxStart = io.MousePos;
                state.curveBoxEnd = io.MousePos;
            }
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            state.contextTrack = state.focusedTrack;
            state.contextTime = std::clamp(state.timeline.ScreenToTime(io.MousePos.x, canvasRect.Min.x), 0.0f, state.clip.duration);
            ImGui::OpenPopup("CurveContextMenu");
        }
    }

    if (!state.tangentDragActive) {
        for (const KeyHandle& selected : state.selection.keys) {
            AnimKey* key = FindKey(state.clip, selected);
            if (!key) continue;
            if (key->interpolation != TrackInterpolation::Cubic) continue;

            const float keyX = state.timeline.TimeToScreen(key->time, canvasRect.Min.x);
            const float keyY = valueToY(key->value);
            const float dt = kTangentHandlePx / std::max(1.0f, state.timeline.pixelsPerSecond);

            const float inTime = key->time - dt;
            const float inValue = key->value - key->inTangent * dt;
            const float outTime = key->time + dt;
            const float outValue = key->value + key->outTangent * dt;

            const ImVec2 pKey(keyX, keyY);
            const ImVec2 pIn(state.timeline.TimeToScreen(inTime, canvasRect.Min.x), valueToY(inValue));
            const ImVec2 pOut(state.timeline.TimeToScreen(outTime, canvasRect.Min.x), valueToY(outValue));

            draw->AddLine(pKey, pIn, IM_COL32(200, 200, 200, 180), 1.0f);
            draw->AddLine(pKey, pOut, IM_COL32(200, 200, 200, 180), 1.0f);
            draw->AddCircleFilled(pIn, 3.0f, IM_COL32(220, 220, 220, 255), 8);
            draw->AddCircleFilled(pOut, 3.0f, IM_COL32(220, 220, 220, 255), 8);

            if (hoveredCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const float inDist = std::hypot(io.MousePos.x - pIn.x, io.MousePos.y - pIn.y);
                const float outDist = std::hypot(io.MousePos.x - pOut.x, io.MousePos.y - pOut.y);
                if (inDist <= 6.0f) {
                    state.tangentDragActive = true;
                    state.tangentDragIsIn = true;
                    state.tangentDragKey = selected;
                } else if (outDist <= 6.0f) {
                    state.tangentDragActive = true;
                    state.tangentDragIsIn = false;
                    state.tangentDragKey = selected;
                }
            }
        }
    }

    if (state.tangentDragActive) {
        AnimKey* key = FindKey(state.clip, state.tangentDragKey);
        if (key) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const float mouseTime = state.timeline.ScreenToTime(io.MousePos.x, canvasRect.Min.x);
                const float mouseValue = yToValue(io.MousePos.y);
                if (state.tangentDragIsIn) {
                    const float dt = std::max(0.0001f, key->time - mouseTime);
                    key->inTangent = (key->value - mouseValue) / dt;
                } else {
                    const float dt = std::max(0.0001f, mouseTime - key->time);
                    key->outTangent = (mouseValue - key->value) / dt;
                }
                key->interpolation = TrackInterpolation::Cubic;
                key->tangentMode = TangentMode::Free;
                state.clipDirty = true;
            } else {
                state.tangentDragActive = false;
            }
        } else {
            state.tangentDragActive = false;
        }
    }

    if (state.draggingKeys) {
        if (state.dragDuplicateRequest && !state.dragWasDuplicated) {
            DuplicateSelectionForDrag(state);
            state.dragWasDuplicated = true;
        }

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float deltaTime = (io.MousePos.x - state.dragMouseStart.x) / std::max(1.0f, state.timeline.pixelsPerSecond);
            const float deltaValue = -(io.MousePos.y - state.dragMouseStart.y) / std::max(1.0f, state.curveValueScale);

            for (const auto& kv : state.dragStartTimes) {
                if (AnimKey* key = FindKey(state.clip, kv.first)) {
                    float newTime = std::max(0.0f, kv.second + deltaTime);
                    newTime = SnapTimeToFrames(state, newTime, state.timeline.pixelsPerSecond, io.KeyShift);
                    key->time = std::clamp(newTime, 0.0f, state.clip.duration);
                    state.clipDirty = true;
                }
            }
            for (const auto& kv : state.dragStartValues) {
                if (AnimKey* key = FindKey(state.clip, kv.first)) {
                    key->value = kv.second + deltaValue;
                    state.clipDirty = true;
                }
            }

            for (AnimBinding& binding : state.clip.bindings) {
                for (AnimTrack& track : binding.tracks) {
                    SortTrackKeys(track);
                }
            }
        } else {
            EndKeyDrag(state);
        }
    }

    if (state.curveBoxSelectActive) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            state.curveBoxEnd = io.MousePos;
            const ImRect box = NormalizeRect(state.curveBoxStart, state.curveBoxEnd);
            draw->AddRectFilled(box.Min, box.Max, IM_COL32(98, 166, 255, 35));
            draw->AddRect(box.Min, box.Max, IM_COL32(98, 166, 255, 220));
        } else {
            const bool additive = io.KeyCtrl;
            if (!additive) {
                state.selection.keys.clear();
            }

            const ImRect box = NormalizeRect(state.curveBoxStart, state.curveBoxEnd);
            for (const TrackHandle& th : group) {
                if (th.binding < 0 || th.binding >= static_cast<int>(state.clip.bindings.size())) continue;
                AnimBinding& binding = state.clip.bindings[th.binding];
                if (th.track < 0 || th.track >= static_cast<int>(binding.tracks.size())) continue;
                AnimTrack& track = binding.tracks[th.track];

                for (const AnimKey& key : track.keys) {
                    const ImVec2 p(state.timeline.TimeToScreen(key.time, canvasRect.Min.x), valueToY(key.value));
                    if (box.Contains(p)) {
                        state.selection.keys.insert(KeyHandle{th.binding, th.track, key.uid});
                    }
                }
            }

            state.curveBoxSelectActive = false;
        }
    }

    if (ImGui::BeginPopup("CurveContextMenu")) {
        if (ImGui::MenuItem("Select All Keys", "Ctrl+A")) {
            SelectAllKeys(state);
        }
        if (ImGui::MenuItem("Delete Keys", "Del", false, !state.selection.keys.empty())) {
            DeleteSelectedKeys(state);
        }
        if (ImGui::MenuItem("Copy Keys", "Ctrl+C", false, !state.selection.keys.empty())) {
            CopySelectedKeys(state);
        }
        if (ImGui::MenuItem("Paste Keys", "Ctrl+V", false, !state.clipboard.empty())) {
            PasteKeysAt(state, state.contextTime);
        }

        DrawInterpolationPresetMenu(state);

        ImGui::EndPopup();
    }

    ImGui::EndChild();
}

} // namespace

void Engine::renderAnimationWindow() {
    if (!showAnimationWindow) return;

    static AnimationEditorState state;
    InitializeClipState(state);

    SceneGraphBridge graph = BuildSceneGraphFromObjects(sceneObjects);
    std::unordered_map<int, SceneObject*> objectById;
    objectById.reserve(sceneObjects.size());
    for (SceneObject& obj : sceneObjects) {
        objectById[obj.id] = &obj;
    }

    auto resolveSelectionAnimationRoot = [&](int objectId) -> int {
        if (objectId < 0) return -1;
        SceneObject* cursor = findObjectById(objectId);
        SceneObject* fallback = cursor;
        while (cursor) {
            if (cursor->hasAnimation) {
                AnimationComponent probe = cursor->animation;
                NormalizeAnimationClipSlots(probe);
                if (!probe.clips.empty() || !AnimationGetActiveClipAssetPath(probe).empty()) {
                    return cursor->id;
                }
            }
            if (cursor->parentId < 0) break;
            cursor = findObjectById(cursor->parentId);
        }
        return fallback ? fallback->id : -1;
    };

    if (animationTargetId >= 0) {
        const int resolved = resolveSelectionAnimationRoot(animationTargetId);
        state.clip.rootObjectId = (resolved >= 0) ? resolved : animationTargetId;
        state.lastObservedSelectedObjectId = animationTargetId;
        animationTargetId = -1;
    } else if (selectedObjectId != state.lastObservedSelectedObjectId) {
        state.lastObservedSelectedObjectId = selectedObjectId;
        if (selectedObjectId >= 0) {
            state.clip.rootObjectId = resolveSelectionAnimationRoot(selectedObjectId);
        }
    } else if (state.clip.rootObjectId < 0 && selectedObjectId >= 0) {
        state.clip.rootObjectId = resolveSelectionAnimationRoot(selectedObjectId);
    }

    ImGui::Begin("Animation", &showAnimationWindow, ImGuiWindowFlags_NoCollapse);
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            SelectAllKeys(state);
        }
    }
    auto resolveClipPath = [&](const std::string& storedPath) -> fs::path {
        if (storedPath.empty()) return {};
        fs::path p = storedPath;
        if (p.is_absolute()) return p;
        if (projectManager.currentProject.isLoaded && !projectManager.currentProject.projectPath.empty()) {
            return projectManager.currentProject.projectPath / p;
        }
        return p;
    };
    auto toStoredClipPath = [&](const fs::path& absolutePath) -> std::string {
        if (absolutePath.empty()) return {};
        if (projectManager.currentProject.isLoaded && !projectManager.currentProject.projectPath.empty()) {
            std::error_code ec;
            fs::path rel = fs::relative(absolutePath, projectManager.currentProject.projectPath, ec);
            if (!ec && !rel.empty() && rel.native().find("..") != 0) {
                return rel.generic_string();
            }
        }
        return absolutePath.generic_string();
    };
    auto findClipSlotByPath = [](const AnimationComponent& animation, const std::string& storedPath) -> int {
        for (int i = 0; i < static_cast<int>(animation.clips.size()); ++i) {
            if (animation.clips[i].assetPath == storedPath) {
                return i;
            }
        }
        return -1;
    };
    auto trim = [](const std::string& value) -> std::string {
        size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    };

    SceneObject* rootObject = (state.clip.rootObjectId >= 0) ? findObjectById(state.clip.rootObjectId) : nullptr;
    if (rootObject && !rootObject->hasAnimation) {
        rootObject->hasAnimation = true;
        projectManager.currentProject.hasUnsavedChanges = true;
    }
    if (rootObject) {
        NormalizeAnimationClipSlots(rootObject->animation);
    }
    if (rootObject && (state.clip.name.empty() || state.clip.name == "New Animation")) {
        state.clip.name = rootObject->name + "_Animation";
    }

    const int currentRootId = rootObject ? rootObject->id : -1;
    const std::string desiredClipAssetPath = rootObject ? AnimationGetActiveClipAssetPath(rootObject->animation) : "";
    const std::string desiredClipName = rootObject ? AnimationGetActiveClipName(rootObject->animation) : "";
    const int desiredClipIndex = rootObject ? AnimationGetActiveClipIndex(rootObject->animation) : -1;
    const bool rootOrAssetChanged =
        (state.loadedRootObjectId != currentRootId) ||
        (state.loadedClipAssetPath != desiredClipAssetPath);
    if (rootOrAssetChanged) {
        if (state.clipDirty && !state.loadedClipAssetPath.empty()) {
            SaveClipToFile(state.clip, resolveClipPath(state.loadedClipAssetPath));
        }
        ResetClipData(state);
        state.loadedRootObjectId = currentRootId;
        state.loadedClipAssetPath = desiredClipAssetPath;
        state.clip.rootObjectId = currentRootId;
        if (!state.loadedClipAssetPath.empty()) {
            LoadClipFromFile(state.clip, state.nextKeyUid, resolveClipPath(state.loadedClipAssetPath));
            state.clip.rootObjectId = currentRootId;
            if (!desiredClipName.empty()) {
                state.clip.name = desiredClipName;
            }
            state.tree = BuildBindingTreeFromClip(state.clip);
        }
        std::snprintf(state.clipPathInput, sizeof(state.clipPathInput), "%s", state.loadedClipAssetPath.c_str());
        if (rootObject && state.newClipNameInput[0] == '\0') {
            std::snprintf(state.newClipNameInput, sizeof(state.newClipNameInput), "%s.moduanimate", rootObject->name.c_str());
        }
        if (!desiredClipName.empty()) {
            std::snprintf(state.renameClipInput, sizeof(state.renameClipInput), "%s", desiredClipName.c_str());
        } else {
            state.renameClipInput[0] = '\0';
        }
        state.renameClipSlotIndex = desiredClipIndex;
    }

    const char* clipHeaderName = !desiredClipName.empty() ? desiredClipName.c_str() : "<none>";
    ImGui::Text("Clip: %s", clipHeaderName);
    ImGui::SameLine();
    ImGui::TextDisabled("Root: %s", rootObject ? rootObject->name.c_str() : "<none>");

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
    if (ImGui::Button("Set Root") && selectedObjectId >= 0) {
        state.clip.rootObjectId = selectedObjectId;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        state.clip.rootObjectId = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Bind Hierarchy")) {
        if (state.clip.rootObjectId >= 0) {
            AutoBindHierarchyTransformTracks(state, state.clip.rootObjectId, objectById);
            state.clipDirty = true;
        }
    }
    ImGui::SameLine();
    bool requestKeyAll = ImGui::Button("Key All");
    ImGui::SameLine();
    bool requestFlip = ImGui::Button("Flip");
    ImGui::SameLine();
    bool saveClipPressed = ImGui::Button("Save");
    ImGui::PopStyleVar();
    if (saveClipPressed && rootObject && !desiredClipAssetPath.empty()) {
        if (SaveClipToFile(state.clip, resolveClipPath(desiredClipAssetPath))) {
            state.clipDirty = false;
        }
    }
    if (requestFlip) {
        FlipClipInTime(state);
    }
    if (state.clipDirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.42f, 1.0f), "*unsaved");
    }

    if (!rootObject) {
        ImGui::Separator();
        ImGui::TextDisabled("Select a root object first.");
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Clip Manager", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!rootObject->animation.clips.empty()) {
            int activeIndex = AnimationGetActiveClipIndex(rootObject->animation);
            const char* activePreview = (activeIndex >= 0 && activeIndex < static_cast<int>(rootObject->animation.clips.size()))
                ? rootObject->animation.clips[activeIndex].name.c_str()
                : "<none>";
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::BeginCombo("Animation", activePreview)) {
                for (int i = 0; i < static_cast<int>(rootObject->animation.clips.size()); ++i) {
                    const bool selected = (i == activeIndex);
                    const char* clipName = rootObject->animation.clips[i].name.empty()
                        ? "<unnamed>"
                        : rootObject->animation.clips[i].name.c_str();
                    if (ImGui::Selectable(clipName, selected)) {
                        rootObject->animation.activeClipIndex = i;
                        NormalizeAnimationClipSlots(rootObject->animation);
                        state.loadedRootObjectId = -1;
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextDisabled("Animation: <none>");
        }

        if (state.newClipNameInput[0] == '\0') {
            std::snprintf(state.newClipNameInput, sizeof(state.newClipNameInput), "%s.moduanimate", rootObject->name.c_str());
        }
        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputText("New Clip", state.newClipNameInput, sizeof(state.newClipNameInput));
        ImGui::SameLine();
        if (ImGui::Button("Create Clip")) {
            std::string fileName = trim(state.newClipNameInput);
            if (fileName.empty()) fileName = rootObject->name + ".moduanimate";
            if (fileName.find(".moduanimate") == std::string::npos) {
                fileName += ".moduanimate";
            }
            fs::path baseDir = projectManager.currentProject.isLoaded
                ? (projectManager.currentProject.assetsPath / "Animations")
                : fs::path("Assets/Animations");
            fs::path absPath = baseDir / fileName;
            const std::string storedPath = toStoredClipPath(absPath);

            ResetClipData(state);
            state.clip.name = fs::path(fileName).stem().string();
            state.clip.rootObjectId = rootObject->id;
            AutoBindHierarchyTransformTracks(state, rootObject->id, objectById);

            if (SaveClipToFile(state.clip, absPath)) {
                int slotIndex = findClipSlotByPath(rootObject->animation, storedPath);
                if (slotIndex < 0) {
                    AnimationClipSlot slot;
                    slot.name = state.clip.name;
                    slot.assetPath = storedPath;
                    rootObject->animation.clips.push_back(std::move(slot));
                    slotIndex = static_cast<int>(rootObject->animation.clips.size()) - 1;
                } else {
                    rootObject->animation.clips[slotIndex].name = state.clip.name;
                    rootObject->animation.clips[slotIndex].assetPath = storedPath;
                }
                rootObject->animation.activeClipIndex = slotIndex;
                NormalizeAnimationClipSlots(rootObject->animation);
                std::snprintf(state.clipPathInput, sizeof(state.clipPathInput), "%s", storedPath.c_str());
                state.loadedRootObjectId = -1;
                projectManager.currentProject.hasUnsavedChanges = true;
            }
        }

        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("Clip Path", state.clipPathInput, sizeof(state.clipPathInput));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
                const char* droppedPath = reinterpret_cast<const char*>(payload->Data);
                if (droppedPath) {
                    std::snprintf(state.clipPathInput, sizeof(state.clipPathInput), "%s", droppedPath);
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Clip")) {
            fs::path loadPath = trim(state.clipPathInput);
            if (loadPath.is_relative() && projectManager.currentProject.isLoaded &&
                !projectManager.currentProject.projectPath.empty()) {
                loadPath = projectManager.currentProject.projectPath / loadPath;
            }
            if (!loadPath.empty() && loadPath.extension() == ".moduanimate" && fs::exists(loadPath)) {
                const std::string storedPath = toStoredClipPath(loadPath);
                int slotIndex = findClipSlotByPath(rootObject->animation, storedPath);
                if (slotIndex < 0) {
                    AnimationClipSlot slot;
                    slot.assetPath = storedPath;
                    slot.name = AnimationClipNameFromPath(storedPath);
                    rootObject->animation.clips.push_back(std::move(slot));
                    slotIndex = static_cast<int>(rootObject->animation.clips.size()) - 1;
                }
                rootObject->animation.activeClipIndex = slotIndex;
                NormalizeAnimationClipSlots(rootObject->animation);
                state.loadedRootObjectId = -1;
                projectManager.currentProject.hasUnsavedChanges = true;
            }
        }

        const int activeClipIndex = AnimationGetActiveClipIndex(rootObject->animation);
        if (activeClipIndex >= 0 && activeClipIndex < static_cast<int>(rootObject->animation.clips.size())) {
            if (state.renameClipSlotIndex != activeClipIndex) {
                std::snprintf(state.renameClipInput,
                              sizeof(state.renameClipInput),
                              "%s",
                              rootObject->animation.clips[activeClipIndex].name.c_str());
                state.renameClipSlotIndex = activeClipIndex;
            }
            ImGui::SetNextItemWidth(280.0f);
            ImGui::InputText("Clip Name", state.renameClipInput, sizeof(state.renameClipInput));
            ImGui::SameLine();
            if (ImGui::Button("Rename")) {
                const std::string renamed = trim(state.renameClipInput);
                if (!renamed.empty()) {
                    rootObject->animation.clips[activeClipIndex].name = renamed;
                    state.clip.name = renamed;
                    state.clipDirty = true;
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
            }
        }
    }

    if (AnimationGetActiveClipAssetPath(rootObject->animation).empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.73f, 0.35f, 1.0f),
                           "No .moduanimate asset assigned for this root.");
        ImGui::TextDisabled("Create or load a .moduanimate file to enable timeline editing.");
        ImGui::End();
        return;
    }

    SceneNode* clipRoot = nullptr;
    if (state.clip.rootObjectId >= 0) {
        auto it = graph.byObjectId.find(state.clip.rootObjectId);
        if (it != graph.byObjectId.end()) {
            clipRoot = it->second;
        }
    }

    state.tree = BuildBindingTreeFromClip(state.clip);
    ResolveBindingTargets(state.clip, clipRoot);
    if (requestKeyAll) {
        KeyAllBoundTracksAtTime(state, state.currentTime, objectById);
    }

    ImGui::Separator();

    const float transportHeight = ImGui::GetFrameHeightWithSpacing() * 1.8f + 8.0f;
    const float mainHeight = std::max(140.0f, ImGui::GetContentRegionAvail().y - transportHeight);

    const float splitterWidth = 6.0f;
    bool consumedWheel = false;

    ImGui::BeginChild("AnimationMainArea",
                      ImVec2(0.0f, mainHeight),
                      false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const float minLeftWidth = 180.0f;
    const float minRightWidth = 220.0f;
    const float totalWidth = ImGui::GetContentRegionAvail().x;
    const float maxLeftWidth = std::max(minLeftWidth, totalWidth - minRightWidth - splitterWidth);
    state.leftPaneWidth = std::clamp(state.leftPaneWidth, minLeftWidth, maxLeftWidth);

    std::vector<TimelineRow> rows = BuildVisibleRows(state.clip, state.tree, state);

    DrawLeftPane(state, rows, 20.0f, ImGui::GetContentRegionAvail().y, splitterWidth, consumedWheel);

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 70, 70, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(88, 88, 88, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(98, 98, 98, 255));
    ImGui::Button("##AnimSplitter", ImVec2(splitterWidth, ImGui::GetContentRegionAvail().y));
    if (ImGui::IsItemActive()) {
        state.leftPaneWidth = std::clamp(state.leftPaneWidth + ImGui::GetIO().MouseDelta.x,
                                         minLeftWidth,
                                         maxLeftWidth);
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("AnimationTimelineArea",
                      ImVec2(0.0f, 0.0f),
                      false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (ImGui::BeginTabBar("AnimationTabs")) {
        if (ImGui::BeginTabItem("Dopesheet")) {
            state.activeTab = AnimationTab::Dopesheet;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Curves")) {
            state.activeTab = AnimationTab::Curves;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    const float timelineContentHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y);
    if (state.activeTab == AnimationTab::Dopesheet) {
        DrawDopesheet(state, rows, timelineContentHeight, consumedWheel);
    } else {
        DrawCurves(state, rows, timelineContentHeight, consumedWheel);
    }

    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::Separator();

    ImGui::Checkbox("Preview", &state.previewEnabled);
    ImGui::SameLine();

    if (ImGui::Button(state.isPlaying ? "Pause" : "Play")) {
        state.isPlaying = !state.isPlaying;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        state.isPlaying = false;
        state.currentTime = 0.0f;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &state.loopPlayback);
    ImGui::SameLine();
    if (state.recordEnabled) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(180, 54, 54, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(210, 72, 72, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(220, 82, 82, 255));
    }
    if (ImGui::Button(state.recordEnabled ? "Record ●" : "Record")) {
        state.recordEnabled = !state.recordEnabled;
    }
    if (state.recordEnabled) {
        ImGui::PopStyleColor(3);
    }
    if (state.recordEnabled != state.recordWasEnabled) {
        state.recordLastValues.clear();
        state.recordWasEnabled = state.recordEnabled;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::DragFloat("Time", &state.currentTime, 0.01f, 0.0f, state.clip.duration, "%.3f")) {
        const float fps = std::max(1.0f, state.clip.sampleRate);
        state.currentTime = std::round(state.currentTime * fps) / fps;
        state.currentTime = std::clamp(state.currentTime, 0.0f, state.clip.duration);
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::DragFloat("Duration", &state.clip.duration, 0.01f, 0.1f, 300.0f, "%.2f")) {
        state.clip.duration = std::max(0.1f, state.clip.duration);
        state.currentTime = std::clamp(state.currentTime, 0.0f, state.clip.duration);
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::DragFloat("Samples", &state.clip.sampleRate, 0.1f, 1.0f, 240.0f, "%.0f");

    if (state.focusedTrack.has_value()) {
        const TrackHandle focused = *state.focusedTrack;
        if (focused.binding >= 0 && focused.binding < static_cast<int>(state.clip.bindings.size())) {
            const AnimBinding& binding = state.clip.bindings[focused.binding];
            if (focused.track >= 0 && focused.track < static_cast<int>(binding.tracks.size())) {
                const AnimTrack& track = binding.tracks[focused.track];
                ImGui::TextDisabled("Focused Track: %s/%s", binding.path.empty() ? "(Root)" : binding.path.c_str(), track.propertyId.c_str());
            }
        }
    }

    if (state.isPlaying) {
        state.currentTime += ImGui::GetIO().DeltaTime;
        if (state.currentTime > state.clip.duration) {
            if (state.loopPlayback) {
                state.currentTime = std::fmod(state.currentTime, std::max(0.001f, state.clip.duration));
            } else {
                state.currentTime = state.clip.duration;
                state.isPlaying = false;
            }
        }
    }

    if (state.recordEnabled && !state.isPlaying && state.clip.rootObjectId >= 0) {
        RecordChangedProperties(state, state.clip.rootObjectId, objectById);
    }

    if (state.previewEnabled && clipRoot) {
        EvaluateClipAtTime(state.clip, state.currentTime, clipRoot);
        ApplySceneGraphToObjects(graph, objectById);
        EvaluateNonTransformTracksAtTime(state.clip, state.currentTime, objectById);
        updateHierarchyWorldTransforms();
    }

    const std::string activeClipPathForSave = rootObject ? AnimationGetActiveClipAssetPath(rootObject->animation) : std::string();
    if (state.clipDirty && !activeClipPathForSave.empty()) {
        SaveClipToFile(state.clip, resolveClipPath(activeClipPathForSave));
        state.clipDirty = false;
    }

    ImGui::End();
}
