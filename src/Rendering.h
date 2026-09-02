#pragma once

#include "Common.h"
#include "SceneObject.h"
#include "RendererResizePolicy.h"
#include "../include/Shaders/Shader.h"
#include "../include/Textures/Texture.h"
#include "../include/Skybox/Skybox.h"
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <limits>

// Cube vertex data (position + normal + texcoord)
extern float vertices[288];
extern float mirrorPlaneVertices[48];

constexpr int kRendererMaxRealtimeLights = 32;

enum class SceneRenderMode { Normal = 0, ShadedWireframe = 1, Wireframe = 2 };

// storage precision for offscreen color targets. Auto = the classic float pipeline
// (HDR/bloom needs it), SDR8 halves bandwidth for LDR-only projects.
enum class RendererColorPrecision { Auto = 0, SDR8 = 1, HDR16F = 2 };

// Primitive generation functions
std::vector<float> generateSphere(int segments = 32, int rings = 16);
std::vector<float> generateCapsule(int segments = 16, int rings = 8);
std::vector<float> generateTorus(int segments = 32, int sides = 16);

class Mesh {
private:
    unsigned int VAO, VBO;
    unsigned int boneVBO = 0;
    int vertexCount;
    int strideFloats = 8;
    bool dynamic = false;
    bool hasBones = false;

public:
    Mesh(const float* vertexData, size_t dataSizeBytes);
    Mesh(const float* vertexData, size_t dataSizeBytes, bool dynamicUsage,
         const void* boneData, size_t boneDataBytes);
    ~Mesh();
    
    void draw() const;
    void updateVertices(const float* vertexData, size_t dataSizeBytes);
    int getVertexCount() const { return vertexCount; }
    bool isDynamic() const { return dynamic; }
    bool usesBones() const { return hasBones; }
};

class OBJLoader {
public:
    struct LoadedMesh {
        struct SubMesh {
            std::unique_ptr<Mesh> mesh;
            int materialIndex = 0;
            int vertexCount = 0;
            int faceCount = 0;
        };

        std::string path;
        std::unique_ptr<Mesh> mesh;
        std::string name;
        int vertexCount = 0;
        int faceCount = 0;
        bool hasNormals = false;
        bool hasTexCoords = false;
        glm::vec3 boundsMin = glm::vec3(FLT_MAX);
        glm::vec3 boundsMax = glm::vec3(-FLT_MAX);
        std::vector<glm::vec3> triangleVertices; // positions duplicated per-triangle for picking
        std::vector<glm::vec3> positions; // unique vertex positions for physics
        std::vector<uint32_t> triangleIndices; // triangle indices into positions
        bool isSkinned = false;
        std::vector<std::string> boneNames;
        std::vector<std::string> boneNodePaths;
        std::vector<glm::mat4> inverseBindMatrices;
        std::vector<glm::ivec4> boneIds;
        std::vector<glm::vec4> boneWeights;
        std::vector<float> baseVertices;
        std::vector<SubMesh> subMeshes;
        std::vector<std::string> materialSlots;
    };
    
private:
    std::vector<LoadedMesh> loadedMeshes;
    
public:
    int loadOBJ(const std::string& filepath, std::string& errorMsg);
    Mesh* getMesh(int index);
    const LoadedMesh* getMeshInfo(int index) const;
    const std::vector<LoadedMesh>& getAllMeshes() const { return loadedMeshes; }
    void clear() { loadedMeshes.clear(); }
    size_t getMeshCount() const { return loadedMeshes.size(); }
};

class Camera;

// Builds the camera's projection matrix (perspective, 3D ortho via orthoSize,
// or legacy 2D ortho via pixelsPerUnit). Defined in Rendering.cpp.
glm::mat4 BuildCameraProjection(const Camera& camera, int width, int height,
                                float fovDeg, float nearPlane, float farPlane);

// CPU-side triangle stream backing an object's renderer, interleaved as
// pos3 / normal3 / uv2 (stride 8 floats) in object space. This is the same data
// the static-merge path consumes; the lightmap baker needs it too. Returns
// nullptr when the object has no CPU geometry (sprites, mirrors, meshes whose
// asset has not finished loading). Defined in Rendering.cpp.
const std::vector<float>* GetObjectTriangleVertexStream(const SceneObject& obj);

class Renderer {
public:
    struct RenderStats {
        int drawCalls = 0;
        int meshDraws = 0;
        int fullscreenDraws = 0;
    };
    struct StaticMergeBatch {
        std::unique_ptr<Mesh> mesh;
        std::string vertPath;
        std::string fragPath;
        MaterialProperties material;
        std::string materialPath;
        std::string albedoTexturePath;
        std::string overlayTexturePath;
        std::string normalMapPath;
        bool useOverlay = false;
        bool unlit = false;
        bool doubleSided = false;
        glm::vec3 boundsCenter = glm::vec3(0.0f);
        float boundsRadius = 0.0f;
    };

    struct PostProcessStats {
        float totalMs = 0.0f;
        float resolveMs = 0.0f;
        float bloomExtractMs = 0.0f;
        float bloomBlurMs = 0.0f;
        float compositeMs = 0.0f;
        int activeVolumeCount = 0;
        int resolvedVolumeId = -1;
        std::string resolvedVolumeName;
        float resolvedBlend = 0.0f;
        int activeEffectCount = 0;
        bool hdrEnabled = false;
        bool bloomUsed = false;
        bool motionBlurUsed = false;
        bool executionBegan = false;
        bool compositeExecuted = false;
        bool finalTextureDiffersFromSource = false;
        unsigned int sourceTextureId = 0;
        unsigned int sourceFramebufferId = 0;
        unsigned int bloomExtractDestinationTextureId = 0;
        unsigned int bloomExtractDestinationFramebufferId = 0;
        unsigned int bloomBlurTextureId = 0;
        unsigned int bloomBlurFramebufferId = 0;
        unsigned int compositeDestinationTextureId = 0;
        unsigned int compositeDestinationFramebufferId = 0;
        unsigned int finalPresentedTextureId = 0;
        unsigned int finalPresentedFramebufferId = 0;
        std::string skipReason;
    };

    struct UiTargetInfo {
        unsigned int fbo = 0;
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        int x = 0;
        int y = 0;
        int clearX = 0;
        int clearY = 0;
        int clearWidth = 0;
        int clearHeight = 0;
        glm::vec4 uvTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
        bool usesAtlas = false;
        bool invalidated = false;
        uint64_t atlasKey = 0;
    };

private:
    unsigned int framebuffer = 0, viewportTexture = 0, rbo = 0;
    int currentWidth = 800, currentHeight = 600;
    // Guards for Renderer::resize. Defensive validation shared by all platforms;
    // see the checks at the top of resize().
    bool resizeInProgress = false;
    bool rendererShuttingDown = false;
    struct RenderTarget {
        unsigned int fbo = 0;
        unsigned int texture = 0;
        unsigned int rbo = 0;
        int width = 0;
        int height = 0;
        bool hasAlpha = false;
        bool hdr = false;
        bool hasDepth = true;
        MaterialProperties::TextureFilter textureFilter = MaterialProperties::TextureFilter::Bilinear;
    };
    struct ResolvedPostFX {
        PostFXSettings settings;
        PostFXSettings polarizationSettings;
        bool hasPolarizationLayer = false;
        int activeVolumeCount = 0;
        int resolvedVolumeId = -1;
        std::string resolvedVolumeName;
        float resolvedBlend = 0.0f;
    };
    RenderTarget previewTarget;
    std::unordered_map<int, RenderTarget> extraPreviewTargets;
    std::unordered_map<int, uint64_t> extraPreviewTargetLastUsed;
    RenderTarget postTarget;
    RenderTarget previewPostTarget;
    RenderTarget polarizationTarget;
    RenderTarget overlayCaptureTarget;
    // Band-scoped ModuVolume: bandLayerTarget receives the isolated slice of the
    // 2D draw stream (full framebuffer size, so ImGui's viewport and projection
    // keep mapping 1:1), bandCaptureTarget is the region cut out of it for the
    // post FX pass.
    RenderTarget bandLayerTarget;
    RenderTarget bandCaptureTarget;
    int bandRegionX = 0;
    int bandRegionY = 0;
    int bandRegionWidth = 0;
    int bandRegionHeight = 0;
    int bandPreviousFramebuffer = 0;
    bool bandRegionActive = false;
    RenderTarget historyTarget;
    RenderTarget bloomTargetA;
    RenderTarget bloomTargetB;
    bool postFxSettingsOverrideActive = false;
    PostFXSettings postFxSettingsOverride;
    RenderTarget* postFxTargetOverride = nullptr;
    RenderTarget selectionMaskTarget;
    static constexpr int kMaxShadowMaps = 4;
    struct ShadowCubeMap {
        unsigned int fbo = 0;
        unsigned int depthCube = 0;
        int resolution = 0;
    };
    struct ShadowDirectionalMap {
        unsigned int fbo = 0;
        unsigned int depthTexture = 0;
        int resolution = 0;
    };
    struct MirrorUpdateState {
        glm::vec3 lastCameraPos = glm::vec3(FLT_MAX);
        glm::vec3 lastCameraFront = glm::vec3(0.0f);
        glm::vec3 lastCameraUp = glm::vec3(0.0f);
        glm::vec3 lastMirrorPos = glm::vec3(FLT_MAX);
        glm::vec3 lastMirrorRot = glm::vec3(FLT_MAX);
        glm::vec3 lastMirrorScale = glm::vec3(FLT_MAX);
        double lastUpdateTime = -1.0;
    };
    struct ReflectionCastTarget {
        unsigned int fbo = 0;
        unsigned int cube = 0;
        unsigned int depth = 0;
        int resolution = 0;
        bool hasCapture = false;
    };
    struct SkyboxReflectionTarget {
        unsigned int fbo = 0;
        unsigned int cube = 0;
        unsigned int depth = 0;
        int resolution = 0;
        uint64_t signature = 0;
        bool hasCapture = false;
        double lastCaptureSec = -1.0;
    };
    struct MaskDrawItem {
        const SceneObject* obj = nullptr;
        Mesh* mesh = nullptr;
        bool wantsGpuSkinning = false;
        bool doubleSided = false;
    };
    struct UiAtlasSlot {
        int contentWidth = 0;
        int contentHeight = 0;
        int x = 0;
        int y = 0;
        int clearX = 0;
        int clearY = 0;
        int clearWidth = 0;
        int clearHeight = 0;
    };
    struct UiAtlasTarget {
        RenderTarget target;
        int size = 0;
        int cursorX = 0;
        int cursorY = 0;
        int rowHeight = 0;
        std::unordered_map<int, UiAtlasSlot> slots;
    };
    Shader* shader = nullptr;
    Shader* defaultShader = nullptr;
    Shader* postShader = nullptr;
    Shader* compositeShader = nullptr;
    Shader* brightShader = nullptr;
    Shader* blurShader = nullptr;
    Shader* shadowDepthShader = nullptr;
    Shader* directionalShadowDepthShader = nullptr;
    Texture* texture1 = nullptr;
    Texture* texture2 = nullptr;
    unsigned int debugWhiteTexture = 0;
    unsigned int missingMaterialFallbackTexture = 0;
    struct CachedTextureEntry {
        std::unique_ptr<Texture> texture;
        size_t approxBytes = 0;
        double lastUsedTime = 0.0;
    };
    std::unordered_map<std::string, CachedTextureEntry> textureCacheBilinear;
    std::unordered_map<std::string, CachedTextureEntry> textureCachePoint;
    std::unordered_map<std::string, double> missingTextureRetryAfter;
    std::unordered_map<std::string, TextureFormatPolicy> textureFormatOverrides;
    std::string textureKeyRoot;
    struct ShaderEntry {
        std::unique_ptr<Shader> shader;
        fs::file_time_type vertTime;
        fs::file_time_type fragTime;
        std::string vertPath;
        std::string fragPath;
        double nextReloadCheckTime = 0.0;
    };
    std::unordered_map<std::string, ShaderEntry> shaderCache;
    std::string defaultVertPath = "Resources/Shaders/vert.glsl";
    std::string skinnedVertPath = "Resources/Shaders/skinned_vert.glsl";
    std::string defaultFragPath = "Resources/Shaders/frag.glsl";
    std::string postVertPath = "Resources/Shaders/postfx_vert.glsl";
    std::string postFragPath = "Resources/Shaders/postfx_frag.glsl";
    std::string postCompositeFragPath = "Resources/Shaders/postfx_composite_frag.glsl";
    std::string postBrightFragPath = "Resources/Shaders/postfx_bright_frag.glsl";
    std::string postBlurFragPath = "Resources/Shaders/postfx_blur_frag.glsl";
    std::string selectionMaskVertPath = "Resources/Shaders/selection_mask_vert.glsl";
    std::string selectionMaskFragPath = "Resources/Shaders/selection_mask_frag.glsl";
    std::string wireframeFragPath = "Resources/Shaders/wireframe_frag.glsl";
    std::string selectionOutlineFragPath = "Resources/Shaders/selection_outline_frag.glsl";
    std::string shadowDepthVertPath = "Resources/Shaders/shadow_depth_vert.glsl";
    std::string shadowDepthFragPath = "Resources/Shaders/shadow_depth_frag.glsl";
    std::string directionalShadowDepthVertPath = "Resources/Shaders/shadow_directional_depth_vert.glsl";
    std::string directionalShadowDepthFragPath = "Resources/Shaders/shadow_directional_depth_frag.glsl";
    bool autoReloadShaders = true;
    int maxRealtimeLights = 10;
    int shadowMapResolution = 512;
    glm::vec3 ambientColor = glm::vec3(0.2f, 0.2f, 0.2f);
    // Rendering Path light policy (Lighting Manager tab). the budget caps how many lights
    // compete for shader slots; the directional allowance is guaranteed on top so suns never
    // get squeezed out by point/spot spam.
    int sceneLightBudget = 20;
    int directionalLightAllowance = 1;
    bool mainLightEnabled = true;         // directional lights contribute
    bool mainLightShadows = true;         // directional lights may cast shadows
    bool additionalLightsEnabled = true;  // point/spot/area lights contribute
    bool additionalLightsShadows = true;  // point/spot/area lights may cast shadows
    bool specularEnabled = true;           // false keeps ambient + diffuse only
    bool softShadowsAllowed = true;       // global gate for PCF-soft shadow mode
    float shadowMaxDistance = 0.0f;       // directional shadow range cap, 0 = camera far plane
    RendererColorPrecision colorPrecision = RendererColorPrecision::Auto;
    TextureFormatPolicy defaultTextureFormatPolicy = TextureFormatPolicy::Auto;
    Mesh* cubeMesh = nullptr;
    Mesh* sphereMesh = nullptr;
    Mesh* capsuleMesh = nullptr;
    Mesh* planeMesh = nullptr;
    Mesh* torusMesh = nullptr;
    Skybox* skybox = nullptr;
    unsigned int quadVAO = 0;
    unsigned int quadVBO = 0;
    unsigned int displayTexture = 0;
    bool historyValid = false;
    size_t textureCacheBudgetBytes = 384ull * 1024ull * 1024ull;
    size_t textureCacheUsageBytes = 0;
    uint64_t staticMergeSceneSignature = 0;
    uint64_t frameSerial = 0;
    uint64_t previewTargetUseSerial = 0;
    uint64_t lastStaticMergeCheckFrameSerial = std::numeric_limits<uint64_t>::max();
    std::unordered_map<int, ShadowCubeMap> shadowCubeMaps;
    std::unordered_map<int, ShadowDirectionalMap> shadowDirectionalMaps;
    std::unordered_map<int, RenderTarget> mirrorTargets;
    std::unordered_map<int, MirrorUpdateState> mirrorUpdateStates;
    std::unordered_map<int, ReflectionCastTarget> reflectionCastTargets;
    SkyboxReflectionTarget skyboxReflectionTarget;
    std::unordered_map<int, RenderTarget> uiTargets;
    std::unordered_map<int, UiTargetInfo> uiTargetViews;
    std::unordered_map<uint64_t, UiAtlasTarget> uiTargetAtlases;
    int uiAtlasMaxSize = 4096;
    // View frustums (6 normalized planes each) of every scene render this frame:
    // editor viewport, previews, mirror and reflection captures. Cleared on
    // setFrameSerial; lets offscreen-surface passes skip work for objects no
    // active view can sample.
    std::vector<std::array<glm::vec4, 6>> capturedSceneFrustums;
    std::vector<StaticMergeBatch> staticMergeBatches;
    std::unordered_set<int> staticMergeSourceIds;
    RenderStats viewportStats;
    RenderStats previewStats;
    PostProcessStats viewportPostStats;
    PostProcessStats previewPostStats;
    bool reflectionCapturePass = false;
    // Replaces BuildCameraProjection inside renderSceneInternal while set.
    //
    // Exists for one reason: an OpenXR eye frustum is asymmetric (the four
    // half-angles differ), and BuildCameraProjection can only build a symmetric
    // frustum from a single vertical FOV. Feeding a symmetric matrix to a headset
    // looks almost right and makes people ill. Non-null only for the duration of
    // renderSceneToTarget, so every existing caller is unaffected.
    const glm::mat4* sceneProjectionOverride = nullptr;
    RenderStats* activeStats = nullptr;
    float selectionOutlineBlend = 0.0f;
    double selectionOutlineLastUpdateSec = 0.0;
    std::vector<int> selectionOutlineVisualIds;
    std::vector<int> selectionOutlinePrevIds;
    std::vector<int> selectionOutlineSourceScratch;
    std::vector<int> selectionOutlineInputScratch;
    std::unordered_set<int> selectionOutlineSelectedScratch;
    std::vector<MaskDrawItem> selectionOutlineCurrentDrawScratch;
    std::vector<MaskDrawItem> selectionOutlinePreviousDrawScratch;
    float selectionOutlineSwapBlend = 1.0f;

    void setupFBO();
    void ensureRenderTarget(RenderTarget& target, int w, int h);
    void ensureRenderTarget(RenderTarget& target, int w, int h, bool alpha);
    void ensureRenderTarget(RenderTarget& target, int w, int h, bool alpha, bool hdr);
    void ensureRenderTarget(RenderTarget& target, int w, int h, bool alpha, bool hdr, bool depth);
    void releaseRenderTarget(RenderTarget& target);
    void releaseReflectionCastTarget(ReflectionCastTarget& target);
    void releaseSkyboxReflectionTarget(SkyboxReflectionTarget& target);
    void updateMirrorTargets(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, float fovDeg, float nearPlane, float farPlane);
    void updateReflectionCastTargets(const std::vector<SceneObject>& sceneObjects, float nearPlane, float farPlane);
    void updateSkyboxReflectionTarget();
    void ensureQuad();
    void drawFullscreenQuad();
    void purgeTextureCacheIfNeeded();
    void purgePreviewTargetsIfNeeded(int keepSlot);
    void rebuildStaticMergeBatches(const std::vector<SceneObject>& sceneObjects);
    void resetStats(RenderStats& stats);
    void recordDrawCall();
    void recordMeshDraw();
    void recordFullscreenDraw();
    void clearHistory();
    void clearTarget(RenderTarget& target);
    void renderSceneInternal(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, bool unbindFramebuffer, float fovDeg, float nearPlane, float farPlane, bool drawMirrorObjects, bool drawSkybox);
    void renderWireframeOverlay(const Camera& camera,
                                const std::vector<SceneObject>& sceneObjects,
                                float fovDeg, float nearPlane, float farPlane,
                                bool shadedSurface);
    unsigned int applyPostProcessing(const Camera& camera, const std::vector<SceneObject>& sceneObjects, unsigned int sourceTexture, int width, int height, bool allowHistory);
    bool postFxPreserveAlpha = false;
    ResolvedPostFX gatherPostFX(const Camera& camera, const std::vector<SceneObject>& sceneObjects) const;
    unsigned int findFramebufferForTexture(unsigned int texture) const;
    void logPostFxDebug(const PostProcessStats& stats, bool allowHistory) const;

public:
    Renderer() = default;
    ~Renderer();

    void initialize();
    Texture* getTexture(const std::string& path, MaterialProperties::TextureFilter filter = MaterialProperties::TextureFilter::Bilinear);
    unsigned int getDebugWhiteTextureId() const { return debugWhiteTexture; }
    void invalidateTexture(const std::string& path);

    // per-texture GPU format overrides, consulted on first load (Auto = adaptive 16bpp).
    // setting one evicts the cached texture so the next request reloads. the project layer
    // owns persistence, this map is just the live view.
    void setTextureFormatOverride(const std::string& path, TextureFormatPolicy policy);
    TextureFormatPolicy getTextureFormatOverride(const std::string& path) const;
    const std::unordered_map<std::string, TextureFormatPolicy>& getTextureFormatOverrides() const { return textureFormatOverrides; }
    void clearTextureFormatOverrides() { textureFormatOverrides.clear(); }
    // root that relative texture paths resolve against for override keys, so editor absolute
    // paths and runtime paths collapse to the same key. set on project load.
    void setTextureKeyRoot(const std::string& root) { textureKeyRoot = root; }
    std::string normalizeTextureKey(const std::string& path) const;
    Shader* getShader(const std::string& vert, const std::string& frag);
    bool forceReloadShader(const std::string& vert, const std::string& frag);
    void setAmbientColor(const glm::vec3& color) { ambientColor = color; }
    glm::vec3 getAmbientColor() const { return ambientColor; }
    void setShadowMapResolution(int resolution) { shadowMapResolution = std::clamp(resolution, 128, 4096); }
    int getShadowMapResolution() const { return shadowMapResolution; }
    void setMaxRealtimeLights(int count) { maxRealtimeLights = std::clamp(count, 1, kRendererMaxRealtimeLights); }
    int getMaxRealtimeLights() const { return maxRealtimeLights; }
    // Rendering Path policy (see field comments above). budgets past the shader's slot count
    // still matter: they bound how many lights can exist before the editor warns.
    void setSceneLightBudget(int budget, int directionalAllowance) {
        sceneLightBudget = std::max(1, budget);
        directionalLightAllowance = std::clamp(directionalAllowance, 1, 4);
    }
    int getSceneLightBudget() const { return sceneLightBudget; }
    int getDirectionalLightAllowance() const { return directionalLightAllowance; }
    void setMainLightEnabled(bool enabled) { mainLightEnabled = enabled; }
    bool isMainLightEnabled() const { return mainLightEnabled; }
    void setMainLightShadows(bool enabled) { mainLightShadows = enabled; }
    bool areMainLightShadowsEnabled() const { return mainLightShadows; }
    void setAdditionalLightsEnabled(bool enabled) { additionalLightsEnabled = enabled; }
    bool areAdditionalLightsEnabled() const { return additionalLightsEnabled; }
    void setAdditionalLightsShadows(bool enabled) { additionalLightsShadows = enabled; }
    bool areAdditionalLightShadowsEnabled() const { return additionalLightsShadows; }
    void setSpecularEnabled(bool enabled) { specularEnabled = enabled; }
    bool isSpecularEnabled() const { return specularEnabled; }
    void setSoftShadowsAllowed(bool allowed) { softShadowsAllowed = allowed; }
    bool areSoftShadowsAllowed() const { return softShadowsAllowed; }
    void setShadowMaxDistance(float distance) { shadowMaxDistance = std::max(0.0f, distance); }
    float getShadowMaxDistance() const { return shadowMaxDistance; }
    void setColorPrecision(RendererColorPrecision precision);
    RendererColorPrecision getColorPrecision() const { return colorPrecision; }
    void setDefaultTextureFormatPolicy(TextureFormatPolicy policy);
    TextureFormatPolicy getDefaultTextureFormatPolicy() const { return defaultTextureFormatPolicy; }
    void setShaderAutoReload(bool enabled) { autoReloadShaders = enabled; }
    bool isShaderAutoReloadEnabled() const { return autoReloadShaders; }
    void resize(int w, int h);
    int getWidth() const { return currentWidth; }
    int getHeight() const { return currentHeight; }
    void setFrameSerial(uint64_t serial) { frameSerial = serial; capturedSceneFrustums.clear(); }
    bool hasCapturedSceneFrustums() const { return !capturedSceneFrustums.empty(); }
    // True when the object's bounding sphere (inflated by radiusMargin) is inside
    // any frustum captured this frame. Conservative: returns true when no frustums
    // were captured or the object has no computable bounds.
    bool isObjectVisibleInCapturedFrustums(const SceneObject& obj, float radiusMargin = 1.0f) const;

    void beginRender(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos);
    void renderSkybox(const glm::mat4& view, const glm::mat4& proj);
    void renderObject(const SceneObject& obj);
    void renderScene(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int selectedId = -1, float fovDeg = FOV, float nearPlane = NEAR_PLANE, float farPlane = FAR_PLANE, bool drawColliders = false, const std::vector<int>* selectedIds = nullptr, SceneRenderMode renderMode = SceneRenderMode::Normal, bool applyPostFX = true);
    void renderSelectionOutline(const Camera& camera, const std::vector<SceneObject>& sceneObjects, const std::vector<int>& selectedIds, float fovDeg, float nearPlane, float farPlane);
    unsigned int renderScenePreview(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, float fovDeg, float nearPlane, float farPlane, bool applyPostFX = false, int previewSlot = 0, bool transparentBackground = false);
    // Renders the scene into a caller-owned framebuffer using a caller-supplied
    // projection matrix. Added for the OpenXR path, where the render target is a
    // swapchain image the runtime owns and the projection is an asymmetric eye
    // frustum, but deliberately expressed in general terms - it takes a GL
    // framebuffer name and a matrix, and knows nothing about OpenXR.
    //
    // The framebuffer binding and viewport are saved and restored, so this can be
    // called from anywhere in a frame without disturbing the caller's state.
    // `arrayLayer` is the layer to render into for a layered (multiview) target;
    // it is 0 for an ordinary 2D target.
    void renderSceneToTarget(const Camera& camera,
                             const std::vector<SceneObject>& sceneObjects,
                             unsigned int targetFramebuffer,
                             int width, int height,
                             const glm::mat4& projection,
                             float nearPlane, float farPlane,
                             bool clearTarget = true);
    void renderCollisionOverlay(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, float fovDeg, float nearPlane, float farPlane, const std::vector<int>* previewIds = nullptr);
    void endRender();
    PostFXSettings resolvePostFXSettings(const Camera& camera, const std::vector<SceneObject>& sceneObjects) const;
    unsigned int postProcessTexture(const Camera& camera, const std::vector<SceneObject>& sceneObjects, unsigned int sourceTexture, int width, int height, bool allowHistory = false);
    bool postProcessFramebufferRegion(const Camera& camera,
                                      const std::vector<SceneObject>& sceneObjects,
                                      int x, int y, int width, int height,
                                      bool allowHistory = false);

    // Band-scoped ModuVolume, in two halves around a slice of the 2D draw list.
    //
    // begin redirects subsequent drawing into a transparent offscreen target of
    // the same dimensions as the framebuffer being drawn into, so the caller's
    // viewport, scissor and projection keep working untouched. end post-processes
    // just the requested region of that target and composites the result back
    // over the real framebuffer, leaving everything already beneath the band
    // untouched. Returns false if the target could not be prepared, in which case
    // the caller must not call end.
    // The 2D draw-order scope of the volume that would win this frame, so a 2D
    // viewport can decide where in its draw list to insert the post FX instead
    // of always running it last. Inactive when no volume resolves or the winner
    // is unscoped, in which case the caller keeps the full-screen behavior.
    struct PostFX2DScope {
        bool active = false;
        PostFX2DScopeMode mode = PostFX2DScopeMode::AtOrBelow;
        int minOrder = 0;
        int maxOrder = 0;
    };
    PostFX2DScope resolvePostFX2DScope(const Camera& camera,
                                       const std::vector<SceneObject>& sceneObjects) const;

    bool beginBandPostFxRegion(int x, int y, int width, int height,
                               int framebufferWidth, int framebufferHeight);
    bool endBandPostFxRegion(const Camera& camera,
                             const std::vector<SceneObject>& sceneObjects,
                             bool allowHistory = false);
    bool isBandPostFxRegionActive() const { return bandRegionActive; }

    Skybox* getSkybox() { return skybox; }
    unsigned int getViewportTexture() const { return displayTexture ? displayTexture : viewportTexture; }
    unsigned int getRawViewportTexture() const { return viewportTexture; }
    const RenderStats& getLastViewportStats() const { return viewportStats; }
    const RenderStats& getLastPreviewStats() const { return previewStats; }
    const PostProcessStats& getLastViewportPostStats() const { return viewportPostStats; }
    const PostProcessStats& getLastPreviewPostStats() const { return previewPostStats; }
    uint64_t getTextureCacheUsageBytes() const { return static_cast<uint64_t>(textureCacheUsageBytes); }
    uint64_t getTextureCacheBudgetBytes() const { return static_cast<uint64_t>(textureCacheBudgetBytes); }
    size_t getTextureCacheCount() const { return textureCacheBilinear.size() + textureCachePoint.size(); }
    size_t getShaderCacheCount() const { return shaderCache.size(); }
    size_t getExtraPreviewTargetCount() const { return extraPreviewTargets.size(); }
    size_t getReflectionCastTargetCount() const { return reflectionCastTargets.size(); }

    UiTargetInfo ensureUiTarget(int id,
                                int width,
                                int height,
                                int layer,
                                MaterialProperties::TextureFilter filter,
                                bool allowAtlas);
    UiTargetInfo getUiTargetInfo(int id) const;
    void markUiTargetRendered(int id);
    void cleanupUiTargets(const std::unordered_set<int>& active);
};
