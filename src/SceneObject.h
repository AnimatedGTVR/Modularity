#pragma once

#include "Common.h"
#include "Lighting2D.h"
#include <unordered_set>

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
    Empty = 21,
    Sprite25D = 22,
    Light2D = 23,
    ShadowCaster2D = 24
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
    enum class TextureFilter {
        Bilinear = 0,
        Point = 1
    };

    glm::vec3 color = glm::vec3(1.0f);
    float alpha = 1.0f;
    float ambientStrength = 0.2f;
    float specularStrength = 0.5f;
    float shininess = 32.0f;
    float textureMix = 0.3f;  // Blend factor between albedo and overlay
    TextureFilter textureFilter = TextureFilter::Bilinear;
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

enum class UITextHAlign {
    Left = 0,
    Center = 1,
    Right = 2
};

enum class UITextVAlign {
    Top = 0,
    Middle = 1,
    Bottom = 2
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
    EaseInOut = 4,
    Step = 5,
    Cubic = 6
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

struct AnimationEvent {
    float time = 0.0f;
    std::string eventId;
    std::string payload;
};

struct AnimationPropertyKeyframe {
    float time = 0.0f;
    float value = 0.0f;
    AnimationInterpolation interpolation = AnimationInterpolation::SmoothStep;
    AnimationCurveMode curveMode = AnimationCurveMode::Preset;
    glm::vec2 bezierIn = glm::vec2(0.25f, 0.0f);
    glm::vec2 bezierOut = glm::vec2(0.75f, 1.0f);
};

struct AnimationPropertyTrack {
    bool enabled = true;
    std::string path;
    std::string label;
    float defaultValue = 0.0f;
    std::vector<AnimationPropertyKeyframe> keyframes;
};

struct AnimationClipSlot {
    std::string name;
    std::string assetPath;
};

struct AnimationComponent {
    bool enabled = true;
    std::string clipAssetPath;
    std::vector<AnimationClipSlot> clips;
    int activeClipIndex = -1;
    float clipLength = 2.0f;
    float playSpeed = 1.0f;
    bool loop = true;
    bool playOnAwake = true;
    bool applyOnScrub = true;
    bool runtimePlaying = false;
    bool runtimePaused = false;
    float runtimeTime = 0.0f;
    float runtimeDirection = 1.0f;
    bool runtimeInitialized = false;
    std::string runtimeClipPath;
    std::vector<AnimationKeyframe> keyframes;
    std::vector<AnimationEvent> events;
    std::vector<AnimationPropertyTrack> tracks;
};

inline std::string AnimationClipNameFromPath(const std::string& assetPath) {
    if (assetPath.empty()) return "Animation";
    fs::path path(assetPath);
    std::string stem = path.stem().string();
    if (!stem.empty()) return stem;
    std::string fileName = path.filename().string();
    if (!fileName.empty()) return fileName;
    return "Animation";
}

inline int AnimationGetActiveClipIndex(const AnimationComponent& animation) {
    if (animation.clips.empty()) return -1;
    if (animation.activeClipIndex >= 0 &&
        animation.activeClipIndex < static_cast<int>(animation.clips.size())) {
        return animation.activeClipIndex;
    }
    if (!animation.clipAssetPath.empty()) {
        for (int i = 0; i < static_cast<int>(animation.clips.size()); ++i) {
            if (animation.clips[i].assetPath == animation.clipAssetPath) {
                return i;
            }
        }
    }
    return 0;
}

inline const AnimationClipSlot* AnimationGetActiveClip(const AnimationComponent& animation) {
    const int index = AnimationGetActiveClipIndex(animation);
    if (index < 0 || index >= static_cast<int>(animation.clips.size())) return nullptr;
    return &animation.clips[index];
}

inline AnimationClipSlot* AnimationGetActiveClip(AnimationComponent& animation) {
    const int index = AnimationGetActiveClipIndex(animation);
    if (index < 0 || index >= static_cast<int>(animation.clips.size())) return nullptr;
    return &animation.clips[index];
}

inline std::string AnimationGetActiveClipAssetPath(const AnimationComponent& animation) {
    const AnimationClipSlot* clip = AnimationGetActiveClip(animation);
    if (clip) return clip->assetPath;
    return animation.clipAssetPath;
}

inline std::string AnimationGetActiveClipName(const AnimationComponent& animation) {
    const AnimationClipSlot* clip = AnimationGetActiveClip(animation);
    if (clip == nullptr) {
        if (!animation.clipAssetPath.empty()) {
            return AnimationClipNameFromPath(animation.clipAssetPath);
        }
        return {};
    }
    if (!clip->name.empty()) return clip->name;
    return AnimationClipNameFromPath(clip->assetPath);
}

inline void NormalizeAnimationClipSlots(AnimationComponent& animation) {
    if (!animation.clipAssetPath.empty()) {
        bool foundLegacyPath = false;
        for (AnimationClipSlot& clip : animation.clips) {
            if (clip.assetPath == animation.clipAssetPath) {
                foundLegacyPath = true;
            }
            if (clip.name.empty()) {
                clip.name = AnimationClipNameFromPath(clip.assetPath);
            }
        }
        if (!foundLegacyPath) {
            AnimationClipSlot clip;
            clip.assetPath = animation.clipAssetPath;
            clip.name = AnimationClipNameFromPath(clip.assetPath);
            animation.clips.push_back(std::move(clip));
        }
    } else {
        for (AnimationClipSlot& clip : animation.clips) {
            if (clip.name.empty()) {
                clip.name = AnimationClipNameFromPath(clip.assetPath);
            }
        }
    }

    if (animation.clips.empty()) {
        animation.activeClipIndex = -1;
        animation.clipAssetPath.clear();
        return;
    }

    int resolvedIndex = AnimationGetActiveClipIndex(animation);
    if (resolvedIndex < 0 || resolvedIndex >= static_cast<int>(animation.clips.size())) {
        resolvedIndex = 0;
    }
    animation.activeClipIndex = resolvedIndex;
    animation.clipAssetPath = animation.clips[resolvedIndex].assetPath;
}

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

enum class PostFXToneMapper {
    None = 0,
    Reinhard = 1,
    ACES = 2
};

enum class PostFXDitherPalette {
    FullColor = 0,
    PS1Warm = 1,
    PS1Cool = 2,
    Mono = 3,
    Sepia = 4
};

enum class PostFXDitherPattern {
    Classic4x4 = 0,
    Bayer8x8 = 1,
    Bayer16x16 = 2,
    Checker = 3,
    HybridPS1 = 4
};

struct CameraComponent {
    SceneCameraType type = SceneCameraType::Player;
    float fov = FOV;
    float nearClip = NEAR_PLANE;
    float farClip = FAR_PLANE;
    bool applyPostFX = true;
    bool use2D = false;
    float pixelsPerUnit = 100.0f;
};

struct PostFXSettings {
    bool enabled = true;
    bool isGlobal = true;
    float priority = 0.0f;
    float blendWeight = 1.0f;
    float blendRadius = 4.0f;
    bool hdrEnabled = true;
    PostFXToneMapper toneMapper = PostFXToneMapper::ACES;
    float whitePoint = 4.0f;
    float gamma = 2.2f;
    bool bloomEnabled = true;
    float bloomThreshold = 1.1f;
    float bloomSoftKnee = 0.25f;
    float bloomIntensity = 0.8f;
    float bloomRadius = 1.5f;
    bool colorAdjustEnabled = false;
    float exposure = 0.0f;      // in EV stops
    float contrast = 1.0f;
    float saturation = 1.0f;
    glm::vec3 colorFilter = glm::vec3(1.0f);
    bool motionBlurEnabled = false;
    float motionBlurStrength = 0.15f; // 0..1 blend with previous frame
    float motionBlurThreshold = 0.04f;
    float motionBlurClamp = 0.35f;
    bool vignetteEnabled = false;
    float vignetteIntensity = 0.35f;
    float vignetteSmoothness = 0.25f;
    bool chromaticAberrationEnabled = false;
    float chromaticAmount = 0.0025f;
    bool sharpenEnabled = false;
    float sharpenStrength = 0.15f;
    bool ambientOcclusionEnabled = false;
    float aoRadius = 0.0035f;
    float aoStrength = 0.6f;
    bool ditherEnabled = false;
    float ditherIntensity = 0.65f;
    int ditherColorBits = 5;
    float ditherDarkAdjustment = 0.35f;
    float ditherPixelation = 0.0f;
    float ditherSize = 1.0f;
    float ditherContrast = 0.35f;
    float ditherOffset = 0.0f;
    PostFXDitherPalette ditherPalette = PostFXDitherPalette::FullColor;
    PostFXDitherPattern ditherPattern = PostFXDitherPattern::HybridPS1;
    bool staticEnabled = false;
    float staticIntensity = 0.15f;
    float staticGrainScale = 2.0f;
    float staticDarkAreaInfluence = 0.5f;
    float staticSpeed = 1.0f;
    bool staticDistortionEnabled = false;
    float staticDistortionHorizontalJitterAmount = 0.003f;
    float staticDistortionLineDensity = 128.0f;
    float staticDistortionGlitchFrequency = 1.5f;
    float staticDistortionStrength = 0.2f;
    bool lensDistortionEnabled = false;
    float lensDistortionAmount = 0.08f;
    float lensDistortionEdgeFalloff = 0.75f;
    glm::vec2 lensDistortionCenterOffset = glm::vec2(0.0f);
    bool vhsOverlayEnabled = false;
    float vhsOverlayOpacity = 0.6f;
    float vhsOverlayScanlineStrength = 0.55f;
    float vhsOverlayTapeNoise = 0.45f;
    float vhsOverlayChromaBleed = 0.15f;
    float vhsOverlayBottomNoiseBandHeight = 0.18f;
    float vhsOverlayBottomNoiseBandIntensity = 0.85f;
    bool wavyEnabled = false;
    float wavyAmplitude = 0.006f;
    float wavyFrequency = 16.0f;
    float wavySpeed = 1.0f;
    bool wavyVertical = false;
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
    int inspectorId = 0;
    std::vector<ScriptSetting> settings;
    std::string lastBinaryPath;
    bool lastBinaryVerified = false;
    std::vector<void*> activeIEnums; // function pointers registered via IEnum_Start
};

struct RigidbodyComponent {
    bool enabled = true;
    float mass = 1.0f;
    bool useCustomCenterOfMass = false;
    glm::vec3 centerOfMass = glm::vec3(0.0f);
    bool useGravity = true;
    bool isKinematic = false;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    bool lockRotationX = false;
    bool lockRotationY = false;
    bool lockRotationZ = false;
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
    glm::vec3 offset = glm::vec3(0.0f);
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
    bool maskChildren = true; // Canvas-only: clip descendants to this canvas rect.
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
    bool textAutoWrap = true;
    UITextHAlign textHAlign = UITextHAlign::Left;
    UITextVAlign textVAlign = UITextVAlign::Top;
    int textEffectFlags = 0;
    float textEffectSpeed = 1.0f;
    float textEffectIntensity = 1.0f;
    bool renderIn3D = false;
    glm::ivec2 renderTargetSize = glm::ivec2(512, 512);
    bool pseudo3DEnabled = false;
    bool pseudo3DUseOffscreenSurface = true;
    glm::vec2 pseudo3DPanelSize = glm::vec2(0.0f); // <= 0 uses ui.size
    glm::vec2 pseudo3DTopLeftOffset = glm::vec2(0.0f);
    glm::vec2 pseudo3DTopRightOffset = glm::vec2(0.0f);
    glm::vec2 pseudo3DBottomRightOffset = glm::vec2(0.0f);
    glm::vec2 pseudo3DBottomLeftOffset = glm::vec2(0.0f);
    glm::vec2 pseudo3DPivot = glm::vec2(0.5f, 0.5f);
    float pseudo3DPerspectiveIntensity = 0.0f;
    float pseudo3DSkewAmount = 0.0f;
    float pseudo3DCurvatureAmount = 0.0f;
    int pseudo3DAnchorTargetId = -1;
    bool pseudo3DDistanceScalingEnabled = false;
    bool pseudo3DAdjustPerspectiveWithDistance = false;
    float pseudo3DMinDistance = 1.0f;
    float pseudo3DMaxDistance = 20.0f;
    float pseudo3DInteractionDistance = 0.0f; // 0 disables distance gating
    int pseudo3DDepthSort = 0;
    bool pseudo3DAllowInteraction = true;
    bool spriteSheetEnabled = false;
    int spriteSheetColumns = 1;
    int spriteSheetRows = 1;
    int spriteSheetFrame = 0;
    float spriteSheetFps = 12.0f;
    bool spriteSheetLoop = true;
    bool spriteCustomFramesEnabled = false;
    int spriteSourceWidth = 0;
    int spriteSourceHeight = 0;
    std::vector<glm::ivec4> spriteCustomFrames;
    std::vector<std::string> spriteCustomFrameNames;
    std::vector<glm::vec2> spriteCustomFrameScales;
    bool nineSliceEnabled = false;
    glm::vec4 nineSliceBorder = glm::vec4(12.0f, 12.0f, 12.0f, 12.0f); // left, right, top, bottom in source pixels
    bool nineSliceTileEdges = true;
    bool nineSliceTileCenter = false;
    bool receiveLighting2D = true;
    bool unlitLighting2D = false;
    float emissiveLighting2D = 0.0f;
};

struct Rigidbody2DComponent {
    bool enabled = true;
    bool useGravity = false;
    bool lockRotation = false;
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
    glm::vec2 offset = glm::vec2(0.0f);
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
    bool disableCulling = false;
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
    float spatialBlend = 1.0f; // 0 = fully 2D/centered, 1 = fully placed in world
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

struct GroundBakedTypeComponent {
    bool enabled = true;
    bool includeInBake = true;
    float areaCost = 1.0f;
};

struct ObsticleObjectComponent {
    bool enabled = true;
    bool carve = true;
    float padding = 0.2f;
};

struct AIAgentComponent {
    bool enabled = true;
    bool useTargetObject = true;
    int targetId = -1;
    glm::vec3 destination = glm::vec3(0.0f);
    float speed = 2.5f;
    float stoppingDistance = 0.2f;
    float repathInterval = 0.5f;
    bool autoRepath = true;
    bool alignToPath = true;
    bool debugDrawPath = true;
};

struct Rig25DRootComponent {
    bool enabled = true;
};

struct Rig25DNodeComponent {
    bool enabled = true;
    int nodeId = -1;
    std::string nodeName;
};

class SceneObject {
public:
    std::string name;
    ObjectType type;
    bool enabled = true;
    bool IsInvariable = false;
    // Derived each hierarchy update: true when all ancestors are locally enabled.
    bool hierarchyEnabled = true;
    int layer = 0;
    std::string tag = "Untagged";
    bool hasRenderer = false;
    RenderType renderType = RenderType::None;
    bool faceCamera = false;
    bool hasLight = false;
    bool hasLight2D = false;
    bool hasCamera = false;
    bool hasPostFX = false;
    bool hasUI = false;
    bool hasShadowCaster2D = false;
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
    Light2DComponent light2D;
    ShadowCaster2DComponent shadowCaster2D;
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
    bool hasGroundBakedType = false;
    GroundBakedTypeComponent groundBakedType;
    bool hasObsticleObject = false;
    ObsticleObjectComponent obsticleObject;
    bool hasAIAgent = false;
    AIAgentComponent aiAgent;
    bool hasAnimation = false;
    AnimationComponent animation;
    bool hasSkeletalAnimation = false;
    SkeletalAnimationComponent skeletal;
    bool hasRig25DRoot = false;
    Rig25DRootComponent rig25DRoot;
    bool hasRig25DNode = false;
    Rig25DNodeComponent rig25DNode;
    UIElementComponent ui;
    std::vector<std::string> inspectorComponentOrder;
    int nextInspectorScriptId = 1;

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

inline bool IsRawMeshPath(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".rmesh";
}

inline bool IsRawMeshPath(const std::string& path) {
    if (path.empty()) return false;
    return IsRawMeshPath(fs::path(path));
}

inline bool IsMMeshPath(const fs::path& path) {
    if (path.empty()) return false;
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".mmesh";
}

inline bool IsMMeshPath(const std::string& path) {
    if (path.empty()) return false;
    return IsMMeshPath(fs::path(path));
}

inline bool IsObjectEnabledInHierarchy(const SceneObject& obj) {
    return obj.enabled && obj.hierarchyEnabled;
}

inline bool HasUIComponent(const SceneObject& obj) {
    return obj.hasUI && obj.ui.type != UIElementType::None;
}

inline bool UsesUIOnly2DPhysics(const SceneObject& obj) {
    return HasUIComponent(obj) && obj.type != ObjectType::Sprite25D;
}

inline bool HasRig25DRootComponent(const SceneObject& obj) {
    return obj.hasRig25DRoot;
}

inline bool HasRig25DNodeComponent(const SceneObject& obj) {
    return obj.hasRig25DNode;
}

inline bool IsRig25DObject(const SceneObject& obj) {
    return HasRig25DRootComponent(obj) || HasRig25DNodeComponent(obj);
}

inline bool IsRig25DNodeTargetable(const SceneObject& obj) {
    return HasRig25DNodeComponent(obj) && obj.rig25DNode.nodeId >= 0;
}

inline bool IsPipeline25DOnlyRigObject(const SceneObject& obj) {
    return IsRig25DObject(obj);
}

inline float GetAudioSpatialBlend(const AudioSourceComponent& src) {
    return std::clamp(src.spatial ? src.spatialBlend : 0.0f, 0.0f, 1.0f);
}

inline bool AudioSourceUsesSpatialization(const AudioSourceComponent& src) {
    return GetAudioSpatialBlend(src) > 0.001f;
}

inline std::string MakeInspectorScriptComponentKey(int inspectorId) {
    return "script:" + std::to_string(inspectorId);
}

inline bool IsInspectorScriptComponentKey(const std::string& key) {
    return key.rfind("script:", 0) == 0;
}

inline int ParseInspectorScriptComponentId(const std::string& key) {
    if (!IsInspectorScriptComponentKey(key)) return -1;
    try {
        return std::stoi(key.substr(7));
    } catch (...) {
        return -1;
    }
}

inline const std::vector<std::string>& GetDefaultInspectorComponentOrderTemplate() {
    static const std::vector<std::string> order = {
        "ui",
        "collider",
        "player_controller",
        "rigidbody3d",
        "rigidbody2d",
        "collider2d",
        "parallax2d",
        "audio_source",
        "ground_baked",
        "obstacle",
        "ai_agent",
        "rig25d_root",
        "rig25d_node",
        "animation",
        "skeletal_animation",
        "reverb_zone",
        "camera",
        "camera_follow2d",
        "post_fx",
        "script",
        "renderer",
        "light",
        "light2d",
        "shadow_caster2d"
    };
    return order;
}

inline void EnsureInspectorComponentMetadata(SceneObject& obj) {
    int nextScriptId = std::max(1, obj.nextInspectorScriptId);
    std::unordered_set<int> usedScriptIds;
    for (ScriptComponent& script : obj.scripts) {
        if (script.inspectorId > 0 && usedScriptIds.insert(script.inspectorId).second) {
            nextScriptId = std::max(nextScriptId, script.inspectorId + 1);
            continue;
        }
        while (usedScriptIds.count(nextScriptId) > 0) {
            ++nextScriptId;
        }
        script.inspectorId = nextScriptId++;
        usedScriptIds.insert(script.inspectorId);
    }
    obj.nextInspectorScriptId = nextScriptId;

    std::vector<std::string> presentKeys;
    presentKeys.reserve(23 + obj.scripts.size());
    if (HasUIComponent(obj)) presentKeys.push_back("ui");
    if (obj.hasCollider) presentKeys.push_back("collider");
    if (obj.hasPlayerController) presentKeys.push_back("player_controller");
    if (obj.hasRigidbody) presentKeys.push_back("rigidbody3d");
    if (obj.hasRigidbody2D) presentKeys.push_back("rigidbody2d");
    if (obj.hasCollider2D) presentKeys.push_back("collider2d");
    if (obj.hasParallaxLayer2D) presentKeys.push_back("parallax2d");
    if (obj.hasAudioSource) presentKeys.push_back("audio_source");
    if (obj.hasGroundBakedType) presentKeys.push_back("ground_baked");
    if (obj.hasObsticleObject) presentKeys.push_back("obstacle");
    if (obj.hasAIAgent) presentKeys.push_back("ai_agent");
    if (obj.hasRig25DRoot) presentKeys.push_back("rig25d_root");
    if (obj.hasRig25DNode) presentKeys.push_back("rig25d_node");
    if (obj.hasAnimation) presentKeys.push_back("animation");
    if (obj.hasSkeletalAnimation) presentKeys.push_back("skeletal_animation");
    if (obj.hasReverbZone) presentKeys.push_back("reverb_zone");
    if (obj.hasCamera) presentKeys.push_back("camera");
    if (obj.hasCameraFollow2D) presentKeys.push_back("camera_follow2d");
    if (obj.hasPostFX) presentKeys.push_back("post_fx");
    if (obj.hasRenderer) presentKeys.push_back("renderer");
    if (obj.hasLight) presentKeys.push_back("light");
    if (obj.hasLight2D) presentKeys.push_back("light2d");
    if (obj.hasShadowCaster2D) presentKeys.push_back("shadow_caster2d");
    for (const ScriptComponent& script : obj.scripts) {
        presentKeys.push_back(MakeInspectorScriptComponentKey(script.inspectorId));
    }

    std::unordered_set<std::string> presentSet(presentKeys.begin(), presentKeys.end());
    std::vector<std::string> sanitizedOrder;
    sanitizedOrder.reserve(presentKeys.size());
    std::unordered_set<std::string> seenKeys;
    for (const std::string& key : obj.inspectorComponentOrder) {
        if (presentSet.count(key) == 0) continue;
        if (!seenKeys.insert(key).second) continue;
        sanitizedOrder.push_back(key);
    }

    const auto appendIfMissing = [&](const std::string& key) {
        if (presentSet.count(key) == 0) return;
        if (!seenKeys.insert(key).second) return;
        sanitizedOrder.push_back(key);
    };

    for (const std::string& templateKey : GetDefaultInspectorComponentOrderTemplate()) {
        if (templateKey == "script") {
            for (const ScriptComponent& script : obj.scripts) {
                appendIfMissing(MakeInspectorScriptComponentKey(script.inspectorId));
            }
            continue;
        }
        appendIfMissing(templateKey);
    }

    obj.inspectorComponentOrder = std::move(sanitizedOrder);
}
