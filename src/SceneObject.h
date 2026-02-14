#pragma once

#include "Common.h"

enum class ObjectType {
    Cube = 0,
    Sphere = 1,
    Capsule = 2,
    OBJMesh = 3,
    Model = 4,        // New type for Assimp-loaded models (FBX, GLTF, etc.)
    DirectionalLight = 5,
    PointLight = 6,
    SpotLight = 7,
    AreaLight = 8,
    Camera = 9,
    PostFXNode = 10,
    Mirror = 11,
    Plane = 12,
    Torus = 13,
    Sprite = 14,      // 3D quad sprite (lit/unlit with material)
    Sprite2D = 15,    // Screen-space sprite
    Canvas = 16,      // UI canvas root
    UIImage = 17,
    UISlider = 18,
    UIButton = 19,
    UIText = 20,
    Empty = 21
};

enum class RenderType {
    None = 0,
    Cube = 1,
    Sphere = 2,
    Capsule = 3,
    OBJMesh = 4,
    Model = 5,
    Mirror = 6,
    Plane = 7,
    Torus = 8,
    Sprite = 9
};

enum class UIElementType {
    None = 0,
    Canvas = 1,
    Image = 2,
    Slider = 3,
    Button = 4,
    Text = 5,
    Sprite2D = 6
};

struct MaterialProperties {
    glm::vec3 color = glm::vec3(1.0f);
    float ambientStrength = 0.2f;
    float specularStrength = 0.5f;
    float shininess = 32.0f;
    float textureMix = 0.3f;  // Blend factor between albedo and overlay
};

enum class LightType {
    Directional = 0,
    Point = 1,
    Spot = 2,
    Area = 3
};

enum class UIAnchor {
    Center = 0,
    TopLeft = 1,
    TopRight = 2,
    BottomLeft = 3,
    BottomRight = 4
};

enum class UISliderStyle {
    ImGui = 0,
    Fill = 1,
    Circle = 2
};

enum class UIButtonStyle {
    ImGui = 0,
    Outline = 1
};

enum class ReverbPreset {
    Room = 0,
    LivingRoom = 1,
    Hall = 2,
    Forest = 3,
    Custom = 4
};

enum class ReverbZoneShape {
    Box = 0,
    Sphere = 1
};

enum class AudioRolloffMode {
    Logarithmic = 0,
    Linear = 1,
    Exponential = 2,
    Custom = 3
};

enum class AnimationInterpolation {
    Linear = 0,
    SmoothStep = 1,
    EaseIn = 2,
    EaseOut = 3,
    EaseInOut = 4
};

enum class AnimationCurveMode {
    Preset = 0,
    Bezier = 1
};

struct AnimationKeyframe {
    float time = 0.0f;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    AnimationInterpolation interpolation = AnimationInterpolation::SmoothStep;
    AnimationCurveMode curveMode = AnimationCurveMode::Preset;
    glm::vec2 bezierIn = glm::vec2(0.25f, 0.0f);
    glm::vec2 bezierOut = glm::vec2(0.75f, 1.0f);
};

struct AnimationComponent {
    bool enabled = true;
    float clipLength = 2.0f;
    float playSpeed = 1.0f;
    bool loop = true;
    bool applyOnScrub = true;
    std::vector<AnimationKeyframe> keyframes;
};

struct SkeletalAnimationComponent {
    bool enabled = true;
    bool useGpuSkinning = true;
    bool allowCpuFallback = true;
    bool useAnimation = true;
    int clipIndex = 0;
    float time = 0.0f;
    float playSpeed = 1.0f;
    bool loop = true;
    int skeletonRootId = -1;
    int maxBones = 128;
    std::vector<std::string> boneNames;
    std::vector<int> boneNodeIds;
    std::vector<glm::mat4> inverseBindMatrices;
    std::vector<glm::mat4> finalMatrices;
};

struct LightComponent {
    LightType type = LightType::Point;
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    float range = 10.0f;
    float edgeFade = 0.2f; // 0 = sharp cutoff, 1 = fully softened edges (area lights)
    // Spot
    float innerAngle = 15.0f;
    float outerAngle = 25.0f;
    // Area (rect) size in world units
    glm::vec2 size = glm::vec2(1.0f, 1.0f);
    bool castShadows = false;
    bool softShadows = true;
    float shadowBias = 0.02f;
    float shadowSoftness = 0.04f;
    bool enabled = true;
};

enum class SceneCameraType {
    Scene = 0,
    Player = 1
};

struct CameraComponent {
    SceneCameraType type = SceneCameraType::Scene;
    float fov = FOV;
    float nearClip = NEAR_PLANE;
    float farClip = FAR_PLANE;
    bool applyPostFX = true;
    bool use2D = false;
    float pixelsPerUnit = 100.0f;
};

struct PostFXSettings {
    bool enabled = true;
    bool bloomEnabled = true;
    float bloomThreshold = 1.1f;
    float bloomIntensity = 0.8f;
    float bloomRadius = 1.5f;
    bool colorAdjustEnabled = false;
    float exposure = 0.0f;      // in EV stops
    float contrast = 1.0f;
    float saturation = 1.0f;
    glm::vec3 colorFilter = glm::vec3(1.0f);
    bool motionBlurEnabled = false;
    float motionBlurStrength = 0.15f; // 0..1 blend with previous frame
    bool vignetteEnabled = false;
    float vignetteIntensity = 0.35f;
    float vignetteSmoothness = 0.25f;
    bool chromaticAberrationEnabled = false;
    float chromaticAmount = 0.0025f;
    bool ambientOcclusionEnabled = false;
    float aoRadius = 0.0035f;
    float aoStrength = 0.6f;
};

enum class ConsoleMessageType {
    Info,
    Warning,
    Error,
    Success
};

struct ScriptSetting {
    std::string key;
    std::string value;
};

enum class ScriptLanguage {
    Cpp = 0,
    CSharp = 1,
    C = 2
};

struct ScriptComponent {
    bool enabled = true;
    ScriptLanguage language = ScriptLanguage::Cpp;
    std::string path;
    std::string managedType;
    std::vector<ScriptSetting> settings;
    std::string lastBinaryPath;
    std::vector<void*> activeIEnums; // function pointers registered via IEnum_Start
};

struct RigidbodyComponent {
    bool enabled = true;
    float mass = 1.0f;
    bool useGravity = true;
    bool isKinematic = false;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    bool lockRotationX = true;
    bool lockRotationY = false;
    bool lockRotationZ = true;
};

enum class ColliderType {
    Box = 0,
    Mesh = 1,
    ConvexMesh = 2,
    Capsule = 3
};

struct ColliderComponent {
    bool enabled = true;
    ColliderType type = ColliderType::Box;
    glm::vec3 boxSize = glm::vec3(1.0f);
    bool convex = true; // For mesh colliders: true = convex hull, false = triangle mesh (static only)
    float staticFriction = 0.9f;
    float dynamicFriction = 0.8f;
    float restitution = 0.0f;
};

struct PlayerControllerComponent {
    bool enabled = true;
    float moveSpeed = 6.0f;
    float runSpeed = 9.0f;
    float lookSensitivity = 0.12f;
    float groundAcceleration = 24.0f;
    float airAcceleration = 8.0f;
    float braking = 16.0f;
    float minSurfaceControl = 0.2f;
    float slideGravity = 40.0f;
    float platformCarry = 1.0f;
    float height = 1.8f;
    float radius = 0.4f;
    float jumpStrength = 6.5f;
    float verticalVelocity = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
};

struct UIElementComponent {
    UIElementType type = UIElementType::None;
    UIAnchor anchor = UIAnchor::Center;
    glm::vec2 position = glm::vec2(0.0f); // offset in pixels from anchor
    glm::vec2 size = glm::vec2(160.0f, 40.0f);
    float rotation = 0.0f;
    float sliderValue = 0.5f;
    float sliderMin = 0.0f;
    float sliderMax = 1.0f;
    std::string label = "UI Element";
    bool buttonPressed = false;
    glm::vec4 color = glm::vec4(1.0f);
    bool interactable = true;
    UISliderStyle sliderStyle = UISliderStyle::ImGui;
    UIButtonStyle buttonStyle = UIButtonStyle::ImGui;
    std::string stylePreset = "Default";
    float textScale = 1.0f;
    bool renderIn3D = false;
    glm::ivec2 renderTargetSize = glm::ivec2(512, 512);
};

struct Rigidbody2DComponent {
    bool enabled = true;
    bool useGravity = false;
    float gravityScale = 1.0f;
    float linearDamping = 0.0f;
    glm::vec2 velocity = glm::vec2(0.0f);
};

enum class Collider2DType {
    Box = 0,
    Polygon = 1,
    Edge = 2
};

struct Collider2DComponent {
    bool enabled = true;
    Collider2DType type = Collider2DType::Box;
    glm::vec2 boxSize = glm::vec2(1.0f);
    std::vector<glm::vec2> points;
    bool closed = false;
    float edgeThickness = 0.05f;
};

struct ParallaxLayer2DComponent {
    bool enabled = true;
    int order = 0;
    float factor = 1.0f; // 1 = world locked, 0 = camera locked
    bool repeatX = false;
    bool repeatY = false;
    glm::vec2 repeatSpacing = glm::vec2(0.0f);
};

struct CameraFollow2DComponent {
    bool enabled = true;
    int targetId = -1;
    glm::vec2 offset = glm::vec2(0.0f);
    float smoothTime = 0.0f; // seconds; 0 snaps to target
};

struct AudioSourceComponent {
    bool enabled = true;
    std::string clipPath;
    float volume = 1.0f;
    bool loop = true;
    bool playOnStart = true;
    bool spatial = true;
    float minDistance = 1.0f;
    float maxDistance = 25.0f;
    AudioRolloffMode rolloffMode = AudioRolloffMode::Logarithmic;
    float rolloff = 1.0f;
    float customMidDistance = 0.5f;
    float customMidGain = 0.6f;
    float customEndGain = 0.0f;
};

struct ReverbZoneComponent {
    bool enabled = true;
    ReverbPreset preset = ReverbPreset::Room;
    ReverbZoneShape shape = ReverbZoneShape::Box;
    glm::vec3 boxSize = glm::vec3(6.0f);
    float radius = 6.0f;
    float blendDistance = 1.0f;
    float minDistance = 1.0f;
    float maxDistance = 15.0f;
    float room = -1000.0f;          // dB
    float roomHF = -100.0f;         // dB
    float roomLF = 0.0f;            // dB
    float decayTime = 1.49f;        // s
    float decayHFRatio = 0.83f;     // 0.1..2
    float reflections = -2602.0f;   // dB
    float reflectionsDelay = 0.007f; // s
    float reverb = 200.0f;          // dB
    float reverbDelay = 0.011f;     // s
    float hfReference = 5000.0f;    // Hz
    float lfReference = 250.0f;     // Hz
    float roomRolloffFactor = 0.0f;
    float diffusion = 100.0f;       // 0..100
    float density = 100.0f;         // 0..100
};

class SceneObject {
public:
    std::string name;
    ObjectType type;
    bool enabled = true;
    int layer = 0;
    std::string tag = "Untagged";
    bool hasRenderer = false;
    RenderType renderType = RenderType::None;
    bool hasLight = false;
    bool hasCamera = false;
    bool hasPostFX = false;
    bool hasUI = false;
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
    glm::vec3 localPosition;
    glm::vec3 localRotation;
    glm::vec3 localScale;
    bool localInitialized = false;
    int id;
    int parentId = -1;
    std::vector<int> childIds;
    bool isExpanded = true;
    std::string meshPath;  // Path to imported model file
    int meshId = -1;       // Index into loaded mesh caches (OBJLoader / ModelLoader)
    int meshSourceIndex = -1; // Source mesh index for multi-mesh models
    MaterialProperties material;
    std::string materialPath;       // Optional external material asset
    std::string albedoTexturePath;
    std::string overlayTexturePath;
    std::string normalMapPath;
    std::string vertexShaderPath;
    std::string fragmentShaderPath;
    bool useOverlay = false;
    LightComponent light;  // Only used when type is a light
    CameraComponent camera; // Only used when type is camera
    PostFXSettings postFx;  // Only used when type is PostFXNode
    std::vector<ScriptComponent> scripts;
    std::vector<std::string> additionalMaterialPaths;
    bool hasRigidbody = false;
    RigidbodyComponent rigidbody;
    bool hasRigidbody2D = false;
    Rigidbody2DComponent rigidbody2D;
    bool hasCollider2D = false;
    Collider2DComponent collider2D;
    bool hasParallaxLayer2D = false;
    ParallaxLayer2DComponent parallaxLayer2D;
    bool hasCameraFollow2D = false;
    CameraFollow2DComponent cameraFollow2D;
    bool hasCollider = false;
    ColliderComponent collider;
    bool hasPlayerController = false;
    PlayerControllerComponent playerController;
    bool hasAudioSource = false;
    AudioSourceComponent audioSource;
    bool hasReverbZone = false;
    ReverbZoneComponent reverbZone;
    bool hasAnimation = false;
    AnimationComponent animation;
    bool hasSkeletalAnimation = false;
    SkeletalAnimationComponent skeletal;
    UIElementComponent ui;

    SceneObject(const std::string& name, ObjectType type, int id)
        : name(name),
          type(type),
          position(0.0f),
          rotation(0.0f),
          scale(1.0f),
          localPosition(0.0f),
          localRotation(0.0f),
          localScale(1.0f),
          localInitialized(true),
          id(id) {}
};

inline bool HasRendererComponent(const SceneObject& obj) {
    return obj.hasRenderer && obj.renderType != RenderType::None;
}

inline bool HasUIComponent(const SceneObject& obj) {
    return obj.hasUI && obj.ui.type != UIElementType::None;
}
