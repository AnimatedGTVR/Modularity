#pragma once

#include "ScriptSdkCommon.h"
#include "Lighting2DTypes.h"
#include "AudioFX.h"
#include <algorithm>
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
    ShadowCaster2D = 24,
    ParticleSystem2D = 25,
    ReflectionCast = 26
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
    float normalMapIntensity = 1.0f;
    float textureMix = 0.3f;  // Blend factor between albedo and overlay
    glm::vec2 uvTiling = glm::vec2(1.0f);
    glm::vec2 uvOffset = glm::vec2(0.0f);
    TextureFilter textureFilter = TextureFilter::Bilinear;
    float scrollSpeed = 0.5f;  // UV drift rate for the Scrolling UV shader preset
    glm::vec2 scrollDirection = glm::vec2(1.0f, 0.3f);  // drift axis (normalized in-shader)
    // Opt-in UV scrolling for 2D UI sprites. Off by default so nothing already
    // authored starts drifting: scrollSpeed already defaults to 0.5, so it can
    // not double as the switch. When on, the UI sprite takes a separate draw
    // path that tiles by splitting the quad at the UV seam, leaving the static
    // and sprite-sheet paths untouched.
    bool uvScrollEnabled = false;

    // Procedural Clouds shader preset. Scroll speed/direction above drive the
    // sideways drift; these control the cloud field itself.
    glm::vec3 cloudColor = glm::vec3(0.85f, 0.36f, 0.96f);
    glm::vec3 cloudSkyColor = glm::vec3(0.10f, 0.02f, 0.14f);
    float cloudScale = 3.0f;
    float cloudCoverage = 0.5f;
    float cloudSoftness = 0.35f;
    int cloudDetail = 5;  // fbm octaves, clamped to 1..8 in-shader
    float cloudSpeed = 0.15f;
    float cloudWarp = 0.35f;
    float cloudHighlight = 1.35f;
    float cloudStars = 0.0f;
    float cloudHorizon = 0.0f;
};

enum class LightType {
    Directional = 0,
    Point = 1,
    Spot = 2,
    Area = 3
};

enum class ReflectionCastUpdateMode {
    EveryFrame = 0,
    FirstFrame = 1
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
    Circle = 2,
    Vertical = 3,
    Ring = 4,
    Stepped = 5
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
    bool useAnimation = false;
    int clipIndex = 0;
    float time = 0.0f;
    float playSpeed = 1.0f;
    bool loop = true;
    int skeletonRootId = -1;
    int maxBones = 128;
    std::vector<std::string> boneNames;
    std::vector<int> boneNodeIds;
    std::vector<int> armatureNodeIds;
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
    int shadowResolution = 0; // 0 = use renderer default
    bool enabled = true;
};

struct ReflectionCastComponent {
    bool enabled = true;
    ReflectionCastUpdateMode updateMode = ReflectionCastUpdateMode::FirstFrame;
    glm::vec3 boxSize = glm::vec3(6.0f);
    float blendDistance = 8.0f;
    float intensity = 1.0f;
    int resolution = 128;
    bool baked = false;
};

enum class SceneCameraType {
    Scene = 0,
    Player = 1
};

// Unity-style camera projection controls. Orthographic here is the real 3D
// ortho mode (world-unit Size like Unity), separate from the legacy 2D
// pixels-per-unit camera which stays behind use2D / the 2D pipeline.
enum class SceneCameraProjection {
    Perspective = 0,
    Orthographic = 1
};

// Which screen axis the fov value describes. Horizontal is converted to the
// vertical fov the projection actually needs using the viewport aspect at
// render time (see ResolveCameraVerticalFovDeg).
enum class SceneCameraFovAxis {
    Vertical = 0,
    Horizontal = 1
};

enum class SceneCameraBackground {
    Skybox = 0,
    SolidColor = 1
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

enum class PostFXVhsSignalMode {
    Composite = 0,
    SVideo = 1,
    RF = 2,
    VhsSP = 3,
    VhsLP = 4,
    VhsEP = 5
};

// Where in the 2D draw stream a ModuVolume lands. 2D content is drawn in one
// stable order (layer, parallax order, then sorting order) and post FX has
// always run last, over the finished frame. Scoping picks an earlier insertion
// point instead, so sprites authored above the volume stay untouched.
enum class PostFX2DScopeMode {
    AtOrBelow = 0, // process everything drawn up to and including maxOrder
    Band = 1       // process only the [minOrder, maxOrder] slice
};

struct CameraComponent {
    SceneCameraType type = SceneCameraType::Player;
    float fov = FOV;
    float nearClip = NEAR_PLANE;
    float farClip = FAR_PLANE;
    bool applyPostFX = true;
    bool use2D = false;
    float pixelsPerUnit = 100.0f;
    // -- Unity-style additions (appended; SceneObject layout is script-ABI
    //    sensitive, keep new fields at the end) --
    SceneCameraProjection projection = SceneCameraProjection::Perspective;
    SceneCameraFovAxis fovAxis = SceneCameraFovAxis::Vertical;
    float orthoSize = 5.0f; // world-unit half-height when projection == Orthographic
    bool renderShadows = true; // per-camera realtime shadow toggle
    SceneCameraBackground background = SceneCameraBackground::Skybox;
    glm::vec3 backgroundColor = glm::vec3(0.10f, 0.10f, 0.12f);
    uint32_t cullingMask = 0xFFFFFFFFu; // bit per SceneObject::layer (project Layers)
};

// The projection matrix wants a vertical fov; when the camera is authored
// with a horizontal fov, convert using the viewport aspect (width / height).
inline float ResolveCameraVerticalFovDeg(const CameraComponent& cam, float aspectWidthOverHeight) {
    float fovDeg = glm::clamp(cam.fov, 1.0f, 179.0f);
    if (cam.fovAxis != SceneCameraFovAxis::Horizontal) return fovDeg;
    const float aspect = glm::max(aspectWidthOverHeight, 0.0001f);
    const float halfH = glm::radians(fovDeg) * 0.5f;
    return glm::clamp(glm::degrees(2.0f * std::atan(std::tan(halfH) / aspect)), 1.0f, 179.0f);
}

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
    bool staticMonochrome = false;
    float staticSparkle = 0.1f;
    bool staticDistortionEnabled = false;
    float staticDistortionHorizontalJitterAmount = 0.003f;
    float staticDistortionLineDensity = 128.0f;
    float staticDistortionGlitchFrequency = 1.5f;
    float staticDistortionStrength = 0.2f;
    bool lensDistortionEnabled = false;
    float lensDistortionAmount = 0.08f;
    float lensDistortionEdgeFalloff = 0.75f;
    glm::vec2 lensDistortionCenterOffset = glm::vec2(0.0f);
    bool lensDistortionEdgeVignetteEnabled = false;
    float lensDistortionEdgeVignetteIntensity = 1.0f;
    float lensDistortionEdgeVignetteRadius = 0.9f;
    float lensDistortionEdgeVignetteSoftness = 0.25f;
    glm::vec3 lensDistortionEdgeVignetteColor = glm::vec3(0.0f);
    bool pixelationEnabled = false;
    float pixelationSize = 4.0f;
    bool posterizeEnabled = false;
    int posterizeLevels = 8;
    bool scanlinesEnabled = false;
    float scanlinesIntensity = 0.25f;
    float scanlinesDensity = 1.0f;
    float scanlinesSpeed = 0.0f;
    bool vhsOverlayEnabled = false;
    float vhsOverlayOpacity = 0.6f;
    float vhsOverlayScanlineStrength = 0.55f;
    float vhsOverlayTapeNoise = 0.45f;
    float vhsOverlayChromaBleed = 0.15f;
    float vhsOverlayBottomNoiseBandHeight = 0.18f;
    float vhsOverlayBottomNoiseBandIntensity = 0.85f;
    float vhsOverlayDistortionStrength = 0.35f;
    float vhsOverlayAnimationSpeed = 1.0f;
    float vhsOverlayColorBleed = 0.4f;
    float vhsOverlayBanding = 0.25f;
    PostFXVhsSignalMode vhsOverlaySignalMode = PostFXVhsSignalMode::VhsSP;
    float vhsOverlayDropouts = 0.35f;
    bool wavyEnabled = false;
    float wavyAmplitude = 0.006f;
    float wavyFrequency = 16.0f;
    float wavySpeed = 1.0f;
    bool wavyVertical = false;
    // The original parameter-weighted ModuVolume behavior, retained as an
    // intentional under-layer. 1.0 is neutral.
    float distortionPolarization = 1.0f;
    // -- 2D draw-order scope (appended; PostFXSettings is embedded in
    //    SceneObject, whose layout is script-ABI sensitive - keep new fields at
    //    the end) --
    // Off by default so every existing volume keeps running full screen.
    bool scope2DEnabled = false;
    PostFX2DScopeMode scope2DMode = PostFX2DScopeMode::AtOrBelow;
    int scope2DMinOrder = -4096;
    int scope2DMaxOrder = 0;
};

inline bool PostFX2DScopeContains(const PostFXSettings& fx, int sortOrder) {
    if (fx.scope2DMode == PostFX2DScopeMode::Band && sortOrder < fx.scope2DMinOrder) {
        return false;
    }
    return sortOrder <= fx.scope2DMaxOrder;
}

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
    bool isTrigger = false; // Detect overlaps without producing a physical collision response
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

// Procedural view motion for a player controller: head bob, mouse-look sway,
// strafe/turn roll, a breathing idle, and a landing dip. Purely cosmetic - none of
// it touches the capsule or the physics pose, so movement and collision feel exactly
// the same with it on or off.
//
// Defaults to disabled so every scene authored before it existed plays unchanged;
// the numbers below are the tuned "on" values, not placeholders.
struct PlayerViewMotionSettings {
    bool enabled = false;

    // Walk/run bob. Frequency is stride cycles per second at Move Speed and scales
    // with actual speed, so walking and running differ in both rate and throw.
    float bobFrequency = 0.95f;
    float bobVertical = 0.035f;     // units of camera rise/fall
    float bobHorizontal = 0.025f;   // units of side-to-side
    float bobRoll = 0.55f;          // degrees of roll per stride
    float runMultiplier = 1.6f;     // amplitude scale at full run
    // How fast the gait itself changes. Deliberately independent of ground
    // acceleration: that is usually tuned near-instant for responsive control, and
    // inheriting it would make sprint read as a switch rather than a change of gait.
    float gaitBlend = 4.0f;
    float runLean = 1.2f;           // degrees of forward pitch at full run

    // Mouse-look sway: the view trails a fast flick and settles back.
    float lookSway = 0.9f;          // degrees per unit of mouse delta
    float lookSwayStiffness = 26.0f;
    float lookSwayDamping = 9.0f;
    float lookSwayMax = 3.5f;       // degrees, hard ceiling

    // Lean into a strafe, and into the direction you are turning.
    float strafeRoll = 1.6f;        // degrees at full sideways speed
    float turnRoll = 0.9f;          // degrees at a fast turn
    float rollSmoothing = 7.0f;

    // Idle breathing. Two detuned sines, so standing still never reads as a metronome.
    float idleAmount = 0.5f;        // degrees of pitch drift
    float idleFrequency = 0.25f;    // Hz
    // Slower than gaitBlend by default: settling into a stand should feel like
    // arriving, not like a crossfade finishing on the same clock as the footsteps.
    float idleBlend = 2.2f;

    // Camera sink while a charged jump winds up. Needs Charged Jump turned on -
    // an instant jump leaves no time to show an anticipation crouch.
    float jumpCrouchDip = 0.05f;    // units at full charge

    // Landing dip, scaled by impact speed.
    float landingDip = 0.06f;       // units at a hard landing
    float landingStiffness = 55.0f;
    float landingDamping = 9.0f;

    // Children of the player (flashlight, held props) lag the aim by their own
    // spring, so they swing behind the view instead of being welded to it.
    bool attachedSwayEnabled = true;
    float attachedSway = 1.3f;
    float attachedStiffness = 18.0f;
    float attachedDamping = 7.0f;
    float attachedMax = 4.0f;       // degrees
};

// Movement audio, driven by the same gait model as the head bob so footsteps land
// on the footfalls rather than on a timer of their own - speed up, and the steps
// speed up with the stride and stay in step with the camera dip.
struct PlayerMovementAudioSettings {
    bool enabled = false;
    // Assign as many as you like; one is picked at random per footfall, never the
    // same one twice running. Empty means no footstep audio.
    std::vector<std::string> footstepClips;
    std::string jumpClip;
    std::string landClip;
    float volume = 0.8f;
    float runVolumeScale = 1.25f;   // footsteps get heavier as you speed up
    float landVolumeScale = 1.0f;   // scaled again by how hard the landing was
    float pitchVariance = 0.08f;    // +/- fraction, so repeats of one clip differ
};

// How input turns into motion. Separate from PlayerViewMotionSettings on purpose:
// everything in there is cosmetic, whereas these change the actual movement.
// Both default to the pre-existing behaviour so old scenes play identically.
struct PlayerControlFeelSettings {
    // 0 = raw one-to-one mouse look. Above that, input is spread over a short window
    // instead of applied whole on the frame it arrived. Nothing is lost - the buffer
    // drains completely, so total travel per unit of mouse movement is unchanged and
    // sensitivity does not shift; only the path there is smoothed.
    float lookSmoothing = 0.0f;     // 0..1, scaled to a time constant

    // Charged jump: hold to wind up, release to launch. Off means the original
    // "full height the instant you press" behaviour.
    bool chargedJump = false;
    float jumpChargeTime = 0.35f;   // seconds to reach full power (then auto-launches)
    float jumpChargeMinScale = 0.62f; // fraction of Jump Strength for a bare tap
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
    std::string textFont;
    bool textAutoWrap = true;
    UITextHAlign textHAlign = UITextHAlign::Left;
    UITextVAlign textVAlign = UITextVAlign::Top;
    int textEffectFlags = 0;
    float textEffectSpeed = 1.0f;
    float textEffectIntensity = 1.0f;
    bool renderIn3D = false;
    glm::ivec2 renderTargetSize = glm::ivec2(512, 512);
    MaterialProperties::TextureFilter renderTargetFilter = MaterialProperties::TextureFilter::Bilinear;
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
    std::string spriteSheetAssetPath; // provenance: sheet asset the frames came from (empty = baked/legacy)
    bool nineSliceEnabled = false;
    glm::vec4 nineSliceBorder = glm::vec4(12.0f, 12.0f, 12.0f, 12.0f); // left, right, top, bottom in source pixels
    bool nineSliceTileEdges = true;
    bool nineSliceTileCenter = false;
    bool receiveLighting2D = true;
    bool unlitLighting2D = false;
    float emissiveLighting2D = 0.0f;
    // Per-component color overrides (alpha == 0 means "use derived default")
    glm::vec4 fillColor = glm::vec4(0.0f);        // slider fill / image tint override
    glm::vec4 backgroundColor = glm::vec4(0.0f);  // slider/button background override
    glm::vec4 borderColor = glm::vec4(0.0f);      // slider/button border override
    glm::vec4 textColor = glm::vec4(0.0f);        // label/text color override
    float fontSize = 0.0f;                         // explicit font size in px (0 = inherit textScale)
    int sortingOrder = 0;                          // draw order within layer
    // Frosted-glass backdrop: blur whatever the runtime UI has already drawn behind this
    // element before the element itself paints. 0 = off, 1 = fully frosted (it doubles as
    // the blurred layer's opacity, so a script can fade it in). Shares the editor's
    // UiGlassBlur capture, so it is OpenGL-only and inert under Vulkan.
    float backdropBlur = 0.0f;
    float backdropRounding = 0.0f;                 // corner radius in px for the blurred layer
    // Runtime interaction state, set each frame by the rendering pass
    bool uiHovered = false;
    bool uiActive = false;
    // Script-driven typewriter reveal (Text elements). The label always holds the
    // FULL string so wrapping and centring are computed once and never move while
    // the message types itself in; unrevealed glyphs are skipped but still advance
    // the pen. Negative disables the whole path, which is the authored default, so
    // text with no script driving it renders exactly as before.
    // Deliberately NOT serialised: this is animation state, not authored data, and
    // persisting a half-typed value would restore a half-drawn label.
    float textRevealChars = -1.0f;    // fractional count of revealed glyphs
    float textRevealPopScale = 1.0f;  // size multiplier of a glyph at the instant it appears
    float textRevealSoftness = 1.0f;  // glyphs over which pop/fade settles back to normal
};

struct Rigidbody2DComponent {
    bool enabled = true;
    bool useGravity = false;
    bool lockRotation = false;
    float gravityScale = 1.0f;
    float linearDamping = 0.0f;
    glm::vec2 velocity = glm::vec2(0.0f);
};

// Existing values 0-2 are load-bearing: they are written into every saved scene
// as plain integers, so new shapes append and never renumber.
enum class Collider2DType {
    Box = 0,
    Polygon = 1,
    Edge = 2,
    Circle = 3,
    // A single segment with thickness, solved as a true capsule (round caps)
    // rather than the axis-aligned quad an Edge segment expands to. Uses
    // points[0..1] for the endpoints and edgeThickness as the diameter.
    Line = 4
};

struct Collider2DComponent {
    bool enabled = true;
    Collider2DType type = Collider2DType::Box;
    glm::vec2 boxSize = glm::vec2(1.0f);
    glm::vec2 offset = glm::vec2(0.0f);
    std::vector<glm::vec2> points;
    bool closed = false;
    float edgeThickness = 0.05f;
    // Circle radius, in the same world units as boxSize.
    float radius = 0.5f;
    // Sprite-outline generation settings. These drive an *edit-time* generator
    // that writes ordinary polygon points into `points`; the runtime never does
    // per-pixel work and the generated result stays hand-editable afterward.
    // Kept on the component (rather than in the editor) so regenerating an
    // outline reproduces the same geometry after a reload.
    float outlineAlphaThreshold = 0.5f;
    float outlineTolerance = 1.5f;   // simplification, in source pixels
    int outlineMaxVertices = 32;
    bool outlineClosed = true;
    std::string outlineSourcePath;   // provenance: sprite the outline came from
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

// Standalone, addable/removable AudioFX component. When present on an object that
// also has an Audio Source, its chain is applied to that source's sound before
// the global chain, so local and global effects combine predictably.
struct AudioFXComponent {
    bool enabled = true;
    // When true, this component's chain is applied to the global master bus
    // (every audio source) instead of only this object's own audio source.
    bool global = false;
    AudioFXChain chain;
};

struct VideoPlayerComponent {
    bool enabled = true;
    std::string videoPath;
    bool playOnAwake = true;
    bool loop = true;
    bool flipX = false;
    bool flipY = false;
    float playbackSpeed = 1.0f;
    bool playAudioFromVideo = true;
    bool routeAudioToSource = false;
    int outputAudioSourceObjectId = -1;
    float videoAudioVolume = 1.0f;
    bool videoAudioMuted = false;
    bool syncAudioToVideo = true;
    float audioSyncTolerance = 0.05f;
};

struct ParticleSystem2DComponent {
    struct MinMaxFloat {
        float min = 1.0f;
        float max = 1.0f;
        bool random = false;

        bool operator==(const MinMaxFloat& other) const {
            return min == other.min && max == other.max && random == other.random;
        }

        bool operator!=(const MinMaxFloat& other) const {
            return !(*this == other);
        }
    };

    struct Particle {
        bool alive = false;
        float age = 0.0f;
        float lifetime = 1.0f;
        glm::vec2 position = glm::vec2(0.0f);
        glm::vec2 velocity = glm::vec2(0.0f);
        float size = 1.0f;
        float rotation = 0.0f;
        float angularVelocity = 0.0f;
        glm::vec4 startColor = glm::vec4(1.0f);
        uint32_t seed = 1u;
    };

    bool enabled = true;
    bool looping = true;
    bool prewarm = false;
    bool playOnAwake = true;
    bool playing = true;
    bool paused = false;
    bool autoRandomSeed = true;
    uint32_t randomSeed = 1u;
    float startDelay = 0.0f;
    MinMaxFloat startLifetime{5.0f, 5.0f, false};
    MinMaxFloat startSpeed{2.0f, 5.0f, true};
    MinMaxFloat startSize{0.18f, 0.35f, true};
    MinMaxFloat startRotation{0.0f, 360.0f, true};
    glm::vec4 startColor = glm::vec4(1.0f);
    float gravityModifier = 0.0f;
    float simulationSpeed = 1.0f;
    int maxParticles = 1000;
    float emissionRate = 20.0f;
    int burstCount = 0;
    float burstTime = 0.0f;
    bool burstLoop = false;
    int shape = 0; // 0 point, 1 circle, 2 box
    float shapeRadius = 0.5f;
    glm::vec2 shapeBox = glm::vec2(1.0f);
    bool velocityOverLifetimeEnabled = false;
    glm::vec2 velocityOverLifetime = glm::vec2(0.0f);
    bool colorOverLifetimeEnabled = true;
    glm::vec4 colorOverLifetime = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    bool sizeOverLifetimeEnabled = true;
    float sizeOverLifetime = 0.0f;
    bool rotationOverLifetimeEnabled = false;
    float rotationOverLifetime = 0.0f;
    bool noiseEnabled = false;
    float noiseStrength = 0.0f;
    float noiseFrequency = 1.0f;
    std::string texturePath;
    std::string materialPath;
    bool receiveLighting2D = true;
    bool unlitLighting2D = false;
    float emissiveLighting2D = 0.0f;
    float runtimeAccumulator = 0.0f;
    float runtimeTime = 0.0f;
    double runtimeLastUpdateTime = 0.0;
    bool runtimeInitialized = false;
    std::vector<Particle> particles;
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
    float turnSpeed = 540.0f;          // deg/sec when aligning to path; 0 = snap instantly.
    float avoidancePadding = 0.3f;     // extra clearance from obstacles/walls during pathing (world units).
};

struct OffMeshLinkComponent {
    bool enabled = true;
    glm::vec3 startPoint = glm::vec3(0.0f);
    glm::vec3 endPoint = glm::vec3(0.0f);
    bool bidirectional = true;
    float costOverride = 0.0f; // 0 = use planar distance
};

struct Rig25DRootComponent {
    bool enabled = true;
};

// ---- Map Maker (rooms / sectors / doorway transitions) ------------------
// Sectors and transitions are ordinary SceneObjects carrying these
// components, so serialization, undo snapshots, and hierarchy behavior all
// come from the existing object system. Cross references use stable string
// ids (generated once, survive copy/duplicate) instead of raw pointers or
// scene-local integer ids.

enum class MapTransitionKind {
    Door = 0,
    OpenPassage = 1,
    Elevator = 2,
    Ladder = 3,
    Teleport = 4,
    Loading = 5,
    Custom = 6
};

struct MapRootComponent {
    bool enabled = true;
    std::string mapId;             // stable id for this map
    std::string startSectorId;     // reachability root for validation
    std::string activeSectorId;    // sector the editor currently focuses
    // 0 = show all sectors, 1 = current + adjacent, 2 = current only.
    // Editor-side visibility only; runtime streaming stays separate data.
    int sectorVisibilityMode = 0;
    std::string notes;
};

struct MapSectorComponent {
    bool enabled = true;
    std::string sectorId;          // stable id referenced by transitions/portals
    glm::vec2 graphPosition = glm::vec2(0.0f); // Sector Map node position (editor metadata)
    glm::vec3 color = glm::vec3(0.31f, 0.55f, 0.90f); // editor tint for graph/hierarchy
    bool useCustomBounds = false;
    glm::vec3 boundsCenter = glm::vec3(0.0f);
    glm::vec3 boundsSize = glm::vec3(8.0f, 4.0f, 8.0f);
    std::string streamingTag;      // loading/streaming metadata for runtime systems
    std::string notes;
    float estimatedMemoryMB = 0.0f; // reserved for future cost estimation
};

struct MapTransitionComponent {
    bool enabled = true;
    std::string transitionId;
    std::string sourceSectorId;
    std::string destinationSectorId;
    std::string sourcePortalId;      // doorway portal on the source side (optional)
    std::string destinationPortalId; // doorway portal on the destination side (optional)
    bool bidirectional = true;
    bool locked = false;
    std::string condition;         // key/flag/tag string interpreted by gameplay code
    MapTransitionKind kind = MapTransitionKind::Door;
    std::string editorLabel;       // editor-only label shown in the Sector Map
    bool hasEntryTransform = false;
    glm::vec3 entryPosition = glm::vec3(0.0f); // runtime spawn point (world space)
    float entryYawDeg = 0.0f;
};

struct MapPortalComponent {
    bool enabled = true;
    std::string portalId;
    std::string transitionId;      // transition this doorway belongs to (may be empty)
    std::string sectorId;          // owning sector
    glm::vec2 openingSize = glm::vec2(1.2f, 2.2f); // width, height in world units
};

struct MapMeshComponent {
    bool enabled = true;
    float gridSize = 0.5f;
    bool snapToGrid = true;
    bool vertexSnapping = false;
    bool surfaceSnapping = false;
    bool autoCollision = true;     // keep a static mesh collider in sync after edits
};

// -- Assemblage -------------------------------------------------------------
//
// Assemblage is the structured (grid-based) 2D authoring mode, the counterpart
// to the freeform sprite workflow. The cells themselves live in a versioned
// .moduasm asset, never in the scene: a scene holds one lightweight root object
// per Assemblage plus one child object per layer, so a 10,000 cell map costs two
// or three SceneObjects rather than 10,000. That keeps undo (which snapshots the
// whole object vector) cheap and keeps a map out of the scene file entirely.
//
// A scene may contain any number of Assemblages, each with its own grid.
struct AssemblageComponent {
    bool enabled = true;
    std::string assemblageId;   // matches the asset's own id; empty until bound
    std::string assetPath;      // project-relative .moduasm
    // Editor display preferences only. Cell geometry (size, origin) lives in the
    // asset so the editor and the runtime can never disagree about where a cell
    // is; the object's own transform places the grid in the world.
    bool showGrid = true;
    bool snapToGrid = true;
    glm::vec4 gridColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.12f);
};

// One per tile layer, on a child of the Assemblage root. Draw order deliberately
// reuses the object's own `layer` plus this sortingOrder, so tile layers sort in
// the same pass as freeform sprites and a freeform object can sit between two
// tile layers without any special casing. Visibility is the object's `enabled`
// flag and the layer name is the object's name - nothing is duplicated here.
struct AssemblageLayerComponent {
    bool enabled = true;
    int layerId = -1;           // id into the owning asset's layer table
    int sortingOrder = 0;       // folded into the existing 2D draw order
    bool locked = false;        // editor: blocks painting and picking
    float opacity = 1.0f;
    glm::vec4 tint = glm::vec4(1.0f);
    bool receiveLighting2D = true;
    bool unlitLighting2D = false;
    float emissiveLighting2D = 0.0f;
    bool collisionEnabled = true;
};

struct Rig25DNodeComponent {
    bool enabled = true;
    int nodeId = -1;
    std::string nodeName;
};

// Marks an object as belonging to a ModuOBJ instance. Stored on the object
// itself (like every other component) rather than in an editor-side table, so it
// survives scene save/load and object duplication for free.
//
// Deliberately lightweight: membership is answered from this struct alone, so
// children do not need a separate visible component just to be marked.
struct ModuObjInstanceRef {
    std::string assetId;        // stable id of the source ModuOBJ asset
    std::string instanceId;     // which instantiation this object belongs to
    std::string lastKnownPath;  // project-relative, recovery metadata only
    int sourceLocalId = -1;     // which object inside the source this came from
    bool isRoot = false;        // an instance root (placement transform lives here)
    bool sourceMissing = false; // source asset could not be resolved on load
};

// Marks an object as network-replicated. The authoritative runtime state lives in
// Net::NetworkSession; this is the *authored* half that survives scene save/load
// and tells the session which scene objects want a network identity.
//
// networkId / owner / hasAuthority are runtime-assigned and deliberately NOT
// serialized: they are session-scoped and meaningless in a saved scene.
// ---------------------------------------------------------------------------
// XR components
//
// These are the scene-graph half of the OpenXR backend in src/XR/. Everything
// here is plain configuration; the per-frame work (reading tracked poses,
// composing the origin transform, running interactions) lives in
// src/XR/XRComponents.cpp so this header stays dependency-free.
//
// They carry no OpenXR types on purpose - a scene with an XR rig in it must load
// and serialize identically in a build with OpenXR compiled out, it just will not
// track anything.
// ---------------------------------------------------------------------------

// Which hand a controller-ish component follows. Matches Modularity::XR::XRDevice
// values so the two convert with a cast, but declared separately so SceneObject.h
// does not have to include the XR headers.
enum class XRHand { Left = 0, Right = 1 };

// Grip = where the controller physically is, Aim = where it points. Never the
// same transform (section 15).
enum class XRControllerPoseSource { Grip = 0, Aim = 1 };

// The player's tracking-space origin: the thing a game moves to move the player
// through the world without fighting headset tracking.
//
//     move the XR Origin  -> the player teleports/walks through the world
//     move your head      -> the XR Camera moves relative to the Origin
//
// Exactly one enabled XR Origin is used per scene; extras are ignored with a
// warning rather than silently fighting each other.
struct XROriginComponent {
    bool enabled = true;
    // Overrides the project-wide Tracking Origin for this rig. "Floor" puts the
    // origin on the physical floor (roomscale), "Eye Level" at the recentre pose.
    // Follow Project Settings is the default so one setting drives everything.
    enum class Mode { FollowProjectSettings = 0, Floor = 1, EyeLevel = 2 };
    Mode trackingOriginMode = Mode::FollowProjectSettings;
    // Uniform scale applied to tracked poses. >1 makes the player feel smaller
    // (the world larger). Applied to the origin transform, not to the objects,
    // so it cannot desynchronize the two eyes.
    float rigScale = 1.0f;
    // Extra height added under the camera for a seated/Eye Level setup, so a
    // seated player is not standing on the floor plane.
    float cameraYOffset = 0.0f;
};

// Connects a Modularity Camera to the tracked HMD. Only cameras carrying this
// component receive head tracking; every other camera behaves exactly as before.
struct XRCameraComponent {
    bool enabled = true;
    bool trackPosition = true;
    bool trackRotation = true;
    // When false the camera keeps its authored transform and only the projection
    // comes from OpenXR. Useful for a fixed cinematic view in VR.
    bool applyTracking = true;
};

// Drives a SceneOBJ transform from a tracked controller pose.
struct XRControllerComponent {
    bool enabled = true;
    XRHand hand = XRHand::Left;
    XRControllerPoseSource poseSource = XRControllerPoseSource::Grip;
    bool trackPosition = true;
    bool trackRotation = true;
    // Disables the object (and therefore its children, including any hand model)
    // while the controller is not tracked, instead of leaving it frozen in space.
    bool hideWhenNotTracked = false;
};

// The higher-level controller: turns raw XR input into interaction intent, which
// the interactors below consume. Split from XRControllerComponent so a plain
// tracked object does not have to carry interaction state it never uses.
struct XRActionBasedControllerComponent {
    bool enabled = true;
    XRHand hand = XRHand::Left;

    // Which physical control drives each intent. Values are Modularity::XR::XRButton.
    int selectButton = 0;    // XRButton::Trigger
    int activateButton = 2;  // XRButton::PrimaryButton
    int uiPressButton = 0;   // XRButton::Trigger

    bool enableHaptics = true;
    float hapticAmplitude = 0.5f;
    float hapticDuration = 0.1f;

    // --- runtime state, not serialized -----------------------------------
    // Edge-detected from the buttons above each frame so interactors can ask
    // "did select start this frame" without each of them tracking history.
    bool selectHeld = false;
    bool selectStarted = false;
    bool selectEnded = false;
    bool activateHeld = false;
    bool activateStarted = false;
    bool activateEnded = false;
    bool uiPressHeld = false;
};

// Distant interaction: casts a ray from the Aim pose and hovers/selects whatever
// it hits.
struct XRRayInteractorComponent {
    bool enabled = true;
    // Straight is the only implemented type. Curved/projectile rays for
    // teleportation are the reason this is an enum rather than a bool.
    enum class RayType { Straight = 0 };
    RayType rayType = RayType::Straight;
    float maxDistance = 20.0f;
    // Layer mask of things the ray can hit; default is everything.
    uint32_t interactionMask = 0xFFFFFFFFu;
    bool uiInteraction = true;
    bool showLineVisual = true;
    glm::vec4 lineColor = glm::vec4(0.35f, 0.75f, 1.0f, 0.85f);

    // --- runtime state, not serialized -----------------------------------
    int hoveredObjectId = -1;
    int selectedObjectId = -1;
    glm::vec3 hitPoint = glm::vec3(0.0f);
    bool hasHit = false;
};

// Near interaction: grabs whatever is inside a small sphere around the
// controller. Uses the engine's own overlap test rather than an XR-specific one.
struct XRDirectInteractorComponent {
    bool enabled = true;
    float interactionRadius = 0.1f;
    uint32_t interactionMask = 0xFFFFFFFFu;

    // --- runtime state, not serialized -----------------------------------
    int hoveredObjectId = -1;
    int selectedObjectId = -1;
};

// Makes an object grabbable by the interactors above.
struct XRGrabInteractableComponent {
    bool enabled = true;
    // How a held object follows the hand.
    //   Instant          - transform is written directly. Never fights physics
    //                      because it turns the body kinematic while held.
    //   Kinematic        - same, but keeps the rigidbody kinematic flag managed.
    //   VelocityTracking - drives the rigidbody's velocity toward the hand, so
    //                      the object still collides with the world while held.
    enum class MovementType { Instant = 0, Kinematic = 1, VelocityTracking = 2 };
    MovementType movementType = MovementType::VelocityTracking;

    bool allowLeftHand = true;
    bool allowRightHand = true;
    // Optional child object whose transform is the grab point. -1 = grab at the
    // object's own origin.
    int attachTransformId = -1;
    bool trackPosition = true;
    bool trackRotation = true;
    bool throwOnDetach = true;
    float throwVelocityScale = 1.0f;
    float throwAngularVelocityScale = 1.0f;

    // --- runtime state, not serialized -----------------------------------
    int heldByObjectId = -1;    // interactor currently holding this, or -1
    int hoveredByObjectId = -1; // interactor currently hovering this, or -1
    bool wasKinematic = false;  // rigidbody state to restore on release
    bool hadGravity = true;
    bool stateSaved = false;

    // One-frame interaction events, cleared at the top of every interaction
    // update. Scripts poll these through ScriptContext (XRSelectEntered() and
    // friends) rather than receiving callbacks - polling in TickUpdate is how the
    // rest of the ModuCPP API works, and it avoids inventing a second event
    // dispatch path alongside the existing one.
    bool hoverEnteredThisFrame = false;
    bool hoverExitedThisFrame = false;
    bool selectEnteredThisFrame = false;
    bool selectExitedThisFrame = false;
    bool activatedThisFrame = false;
    bool deactivatedThisFrame = false;
};

struct NetworkIdentityComponent {
    bool enabled = true;
    // ModuOBJ asset this object is spawned from, when it is a networked prefab.
    // Empty for a scene-placed object that is networked in place.
    std::string assetId;
    // Replication settings, mirrored into Net::SyncSettings when the session
    // adopts the object. Kept as plain fields so the Inspector and the scene
    // serializer need no knowledge of the networking layer.
    bool syncPosition = true;
    bool syncRotation = true;
    bool syncScale = false;
    bool syncVelocity = false;
    int syncMode = 1;              // 0 Snapshot, 1 Interpolate, 2 Extrapolate
    int sendRateHz = 0;            // 0 = use the session rate
    float interpolationDelay = 0.1f;
    float maxExtrapolation = 0.25f;
    // Only the owner may write this object; everyone else receives it.
    bool ownerOnlyWrites = true;
    // Hand ownership to whoever spawned it rather than the host.
    bool spawnerOwns = true;

    // --- runtime only, never serialized ---
    uint32_t runtimeNetworkId = 0;
    int runtimeOwner = 0;
    bool runtimeHasAuthority = false;
    bool runtimeSpawned = false;
};

// Scene-level networking configuration. Exactly one object per scene may carry
// this; see CountNetworkManagers() for the editor validation.
//
// NOTE ON CREDENTIALS: appId is a Photon credential. It is stored here so the
// Inspector can expose it per the component spec, but it must never be written to
// logs, and a project that ships publicly should prefer supplying it from project
// settings or an environment override at runtime.
struct NetworkManagerComponent {
    bool enabled = true;
    std::string appId;
    std::string appVersion = "1.0";
    std::string region;            // empty = automatic / best ping
    std::string nickname = "Player";
    bool autoConnect = false;
    bool offlineMode = false;
    int maxPlayers = 0;            // 0 = backend default
    bool autoJoinLobby = true;
    std::string defaultRoomName;
    int sendRateHz = 20;
    int serializationRateHz = 10;
    int maxOutboundBytesPerSecond = 0;   // 0 = unlimited
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
    // Editor presentation only; the runtime never reads these. Kept on the object
    // (rather than an editor-side map) so they save with the scene like any other field.
    glm::vec4 editorIconTint = glm::vec4(1.0f);
    std::string editorIconPath;          // custom hierarchy/gizmo image, empty = built-in icon
    bool editorIconShowInViewport = false;  // billboard the icon at the object origin
    bool hasRenderer = false;
    RenderType renderType = RenderType::None;
    bool faceCamera = false;
    bool hasLight = false;
    bool hasLight2D = false;
    bool hasReflectionCast = false;
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
    // ModuOBJ instance membership. Defaults to false so every existing scene and
    // every non-instance object loads exactly as before.
    bool hasModuObjInstance = false;
    ModuObjInstanceRef moduObjInstance;
    // Networking components. Both default to absent, so scenes authored before
    // networking existed load unchanged.
    bool hasNetworkIdentity = false;
    NetworkIdentityComponent networkIdentity;
    bool hasNetworkManager = false;
    NetworkManagerComponent networkManager;
    bool isExpanded = true;
    std::string meshPath;  // Path to imported model file
    int meshId = -1;       // Index into loaded mesh caches (OBJLoader / ModelLoader)
    int meshSourceIndex = -1; // Source mesh index for multi-mesh models
    MaterialProperties material;
    std::string materialPath;       // Optional external material asset
    std::string albedoTexturePath;
    std::string overlayTexturePath;
    std::string normalMapPath;
    std::string shaderPackPath;
    std::string vertexShaderPath;
    std::string fragmentShaderPath;
    bool useOverlay = false;
    LightComponent light;  // Only used when type is a light
    ReflectionCastComponent reflectionCast;
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
    bool hasAudioFX = false;
    AudioFXComponent audioFX;
    bool hasVideoPlayer = false;
    VideoPlayerComponent videoPlayer;
    bool hasParticleSystem2D = false;
    ParticleSystem2DComponent particleSystem2D;
    bool hasReverbZone = false;
    ReverbZoneComponent reverbZone;
    // XR. All default to absent, so every existing scene loads with no XR rig and
    // behaves exactly as it did.
    bool hasXROrigin = false;
    XROriginComponent xrOrigin;
    bool hasXRCamera = false;
    XRCameraComponent xrCamera;
    bool hasXRController = false;
    XRControllerComponent xrController;
    bool hasXRActionBasedController = false;
    XRActionBasedControllerComponent xrActionBasedController;
    bool hasXRRayInteractor = false;
    XRRayInteractorComponent xrRayInteractor;
    bool hasXRDirectInteractor = false;
    XRDirectInteractorComponent xrDirectInteractor;
    bool hasXRGrabInteractable = false;
    XRGrabInteractableComponent xrGrabInteractable;
    bool hasGroundBakedType = false;
    GroundBakedTypeComponent groundBakedType;
    bool hasObsticleObject = false;
    ObsticleObjectComponent obsticleObject;
    bool hasAIAgent = false;
    AIAgentComponent aiAgent;
    bool hasOffMeshLink = false;
    OffMeshLinkComponent offMeshLink;
    bool hasAnimation = false;
    AnimationComponent animation;
    bool hasSkeletalAnimation = false;
    SkeletalAnimationComponent skeletal;
    bool hasRig25DRoot = false;
    Rig25DRootComponent rig25DRoot;
    bool hasRig25DNode = false;
    Rig25DNodeComponent rig25DNode;
    UIElementComponent ui;
    bool runtimeHasAlbedoTextureOverride = false;
    unsigned int runtimeAlbedoTextureOverrideId = 0;
    bool runtimeAlbedoTextureFlipX = false;
    bool runtimeAlbedoTextureFlipY = false;
    std::vector<std::string> inspectorComponentOrder;
    int nextInspectorScriptId = 1;
    // Map Maker components (appended; SceneObject layout is script-ABI
    // sensitive, keep new fields at the end)
    bool hasMapRoot = false;
    MapRootComponent mapRoot;
    bool hasMapSector = false;
    MapSectorComponent mapSector;
    bool hasMapTransition = false;
    MapTransitionComponent mapTransition;
    bool hasMapPortal = false;
    MapPortalComponent mapPortal;
    bool hasMapMesh = false;
    MapMeshComponent mapMesh;
    // Transient editor-only flag driven by the Map Maker sector visibility
    // mode. Never serialized; cleared while playing so runtime behavior is
    // untouched (see Engine::applyMapSectorVisibility).
    bool editorSectorHidden = false;
    // Assemblage components (appended; SceneObject layout is script-ABI
    // sensitive, keep new fields at the end). Both default to absent, so every
    // scene authored before Assemblage existed loads and saves unchanged.
    bool hasAssemblage = false;
    AssemblageComponent assemblage;
    bool hasAssemblageLayer = false;
    AssemblageLayerComponent assemblageLayer;
    // Lives out here rather than inside PlayerControllerComponent on purpose:
    // growing that struct would shift every SceneObject member after it, and the
    // script API dereferences those offsets from inline headers baked into compiled
    // script DLLs. Appending keeps the existing layout byte-for-byte, so no ABI bump
    // and no forced script recompile. Serialized under the "pc" key prefix with the
    // rest of the player controller, which is where it belongs conceptually.
    PlayerViewMotionSettings playerViewMotion;
    // Appended for the same ABI reason as playerViewMotion above, and likewise
    // serialized under the "pc" key prefix with the rest of the player controller.
    PlayerControlFeelSettings playerControlFeel;
    // Appended for the same ABI reason as the two above.
    PlayerMovementAudioSettings playerMovementAudio;

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

// Editor validation: only one NetworkManager may be active in a scene. Returns
// how many enabled managers exist and, when there is more than one, the id of the
// first so the Inspector can point at the duplicate.
inline int CountNetworkManagers(const std::vector<SceneObject>& objects, int* outFirstId = nullptr) {
    int count = 0;
    for (const SceneObject& obj : objects) {
        if (!obj.hasNetworkManager || !obj.networkManager.enabled) continue;
        if (count == 0 && outFirstId) *outFirstId = obj.id;
        ++count;
    }
    return count;
}

// Scene object id of the single active NetworkManager, or -1 when there is none
// (or more than one, which is an authoring error the editor should surface).
inline int FindActiveNetworkManager(const std::vector<SceneObject>& objects) {
    int firstId = -1;
    return CountNetworkManagers(objects, &firstId) == 1 ? firstId : -1;
}

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

// formats the RMesh edit tooling can open (.mmesh converts through RawMeshAsset)
inline bool IsMeshEditablePath(const fs::path& path) {
    return IsRawMeshPath(path) || IsMMeshPath(path);
}

inline bool IsMeshEditablePath(const std::string& path) {
    if (path.empty()) return false;
    return IsMeshEditablePath(fs::path(path));
}

inline bool IsMMeshPath(const std::string& path) {
    if (path.empty()) return false;
    return IsMMeshPath(fs::path(path));
}

inline bool IsObjectEnabledInHierarchy(const SceneObject& obj) {
    return obj.enabled && obj.hierarchyEnabled && !obj.editorSectorHidden;
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
        "map_root",
        "map_sector",
        "map_transition",
        "map_portal",
        "map_mesh",
        "assemblage",
        "assemblage_layer",
        "ui",
        "collider",
        "player_controller",
        "rigidbody3d",
        "rigidbody2d",
        "collider2d",
        "parallax2d",
        "audio_source",
        "audio_fx",
        "video_player",
        "network_manager",
        "network_identity",
        "particle_system2d",
        "ground_baked",
        "obstacle",
        "ai_agent",
        "rig25d_root",
        "rig25d_node",
        "animation",
        "skeletal_animation",
        "reverb_zone",
        // XR. A component key that is not listed here is silently dropped from
        // inspectorComponentOrder even when its hasXxx flag is set, so the
        // component exists on the object but never draws. Listed above "camera"
        // so an XR Camera's tracking settings read before the camera they drive.
        "xr_origin",
        "xr_camera",
        "xr_controller",
        "xr_action_controller",
        "xr_ray_interactor",
        "xr_direct_interactor",
        "xr_grab_interactable",
        "camera",
        "camera_follow2d",
        "post_fx",
        "reflection_cast",
        "script",
        "renderer",
        "light",
        "light2d",
        "shadow_caster2d"
    };
    return order;
}

// The sorting key a 2D object draws with. An Assemblage layer carries its order
// on its own component rather than on a UI component it does not have, which is
// what lets a Freeform sprite sort between two tile layers; a ModuVolume scope
// has to read the same key the draw list was sorted by or the two disagree.
inline int RuntimeUiSortOrderOf(const SceneObject& obj) {
    if (obj.hasAssemblageLayer) return obj.assemblageLayer.sortingOrder;
    return obj.hasUI ? obj.ui.sortingOrder : 0;
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
    presentKeys.reserve(30 + obj.scripts.size());
    if (obj.hasMapRoot) presentKeys.push_back("map_root");
    if (obj.hasMapSector) presentKeys.push_back("map_sector");
    if (obj.hasMapTransition) presentKeys.push_back("map_transition");
    if (obj.hasMapPortal) presentKeys.push_back("map_portal");
    if (obj.hasMapMesh) presentKeys.push_back("map_mesh");
    if (obj.hasAssemblage) presentKeys.push_back("assemblage");
    if (obj.hasAssemblageLayer) presentKeys.push_back("assemblage_layer");
    if (HasUIComponent(obj)) presentKeys.push_back("ui");
    if (obj.hasCollider) presentKeys.push_back("collider");
    if (obj.hasPlayerController) presentKeys.push_back("player_controller");
    if (obj.hasRigidbody) presentKeys.push_back("rigidbody3d");
    if (obj.hasRigidbody2D) presentKeys.push_back("rigidbody2d");
    if (obj.hasCollider2D) presentKeys.push_back("collider2d");
    if (obj.hasParallaxLayer2D) presentKeys.push_back("parallax2d");
    if (obj.hasAudioSource) presentKeys.push_back("audio_source");
    if (obj.hasAudioFX) presentKeys.push_back("audio_fx");
    if (obj.hasVideoPlayer) presentKeys.push_back("video_player");
    if (obj.hasNetworkManager) presentKeys.push_back("network_manager");
    if (obj.hasNetworkIdentity) presentKeys.push_back("network_identity");
    if (obj.hasParticleSystem2D) presentKeys.push_back("particle_system2d");
    if (obj.hasGroundBakedType) presentKeys.push_back("ground_baked");
    if (obj.hasObsticleObject) presentKeys.push_back("obstacle");
    if (obj.hasAIAgent) presentKeys.push_back("ai_agent");
    if (obj.hasOffMeshLink) presentKeys.push_back("offmesh_link");
    if (obj.hasRig25DRoot) presentKeys.push_back("rig25d_root");
    if (obj.hasRig25DNode) presentKeys.push_back("rig25d_node");
    if (obj.hasAnimation) presentKeys.push_back("animation");
    if (obj.hasSkeletalAnimation) presentKeys.push_back("skeletal_animation");
    if (obj.hasReverbZone) presentKeys.push_back("reverb_zone");
    if (obj.hasXROrigin) presentKeys.push_back("xr_origin");
    if (obj.hasXRCamera) presentKeys.push_back("xr_camera");
    if (obj.hasXRController) presentKeys.push_back("xr_controller");
    if (obj.hasXRActionBasedController) presentKeys.push_back("xr_action_controller");
    if (obj.hasXRRayInteractor) presentKeys.push_back("xr_ray_interactor");
    if (obj.hasXRDirectInteractor) presentKeys.push_back("xr_direct_interactor");
    if (obj.hasXRGrabInteractable) presentKeys.push_back("xr_grab_interactable");
    if (obj.hasCamera) presentKeys.push_back("camera");
    if (obj.hasCameraFollow2D) presentKeys.push_back("camera_follow2d");
    if (obj.hasPostFX) presentKeys.push_back("post_fx");
    if (obj.hasReflectionCast) presentKeys.push_back("reflection_cast");
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
