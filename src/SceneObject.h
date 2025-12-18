#pragma once

#include "Common.h"

enum class ObjectType {
    Cube,
    Sphere,
    Capsule,
    OBJMesh,
    Model,       // New type for Assimp-loaded models (FBX, GLTF, etc.)
    DirectionalLight,
    PointLight,
    SpotLight,
    AreaLight,
    Camera,
    PostFXNode
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

struct ScriptComponent {
    bool enabled = true;
    std::string path;
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
};

struct PlayerControllerComponent {
    bool enabled = true;
    float moveSpeed = 6.0f;
    float lookSensitivity = 0.12f;
    float height = 1.8f;
    float radius = 0.4f;
    float jumpStrength = 6.5f;
    float verticalVelocity = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
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
};

class SceneObject {
public:
    std::string name;
    ObjectType type;
    bool enabled = true;
    int layer = 0;
    std::string tag = "Untagged";
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
    int id;
    int parentId = -1;
    std::vector<int> childIds;
    bool isExpanded = true;
    std::string meshPath;  // Path to imported model file
    int meshId = -1;       // Index into loaded mesh caches (OBJLoader / ModelLoader)
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
    bool hasCollider = false;
    ColliderComponent collider;
    bool hasPlayerController = false;
    PlayerControllerComponent playerController;
    bool hasAudioSource = false;
    AudioSourceComponent audioSource;

    SceneObject(const std::string& name, ObjectType type, int id)
        : name(name), type(type), position(0.0f), rotation(0.0f), scale(1.0f), id(id) {}
};
