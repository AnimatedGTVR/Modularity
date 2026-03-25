#pragma once

#include "Common.h"
#include "SceneObject.h"
#include "../include/Shaders/Shader.h"
#include "../include/Textures/Texture.h"
#include "../include/Skybox/Skybox.h"
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <limits>

// Cube vertex data (position + normal + texcoord)
extern float vertices[];

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
    };

private:
    unsigned int framebuffer = 0, viewportTexture = 0, rbo = 0;
    int currentWidth = 800, currentHeight = 600;
    struct RenderTarget {
        unsigned int fbo = 0;
        unsigned int texture = 0;
        unsigned int rbo = 0;
        int width = 0;
        int height = 0;
        bool hasAlpha = false;
        bool hdr = false;
    };
    struct ResolvedPostFX {
        PostFXSettings settings;
        int activeVolumeCount = 0;
        int resolvedVolumeId = -1;
        std::string resolvedVolumeName;
        float resolvedBlend = 0.0f;
    };
    RenderTarget previewTarget;
    std::unordered_map<int, RenderTarget> extraPreviewTargets;
    RenderTarget postTarget;
    RenderTarget previewPostTarget;
    RenderTarget historyTarget;
    RenderTarget bloomTargetA;
    RenderTarget bloomTargetB;
    RenderTarget selectionMaskTarget;
    static constexpr int kMaxShadowMaps = 4;
    struct ShadowCubeMap {
        unsigned int fbo = 0;
        unsigned int depthCube = 0;
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
    Shader* shader = nullptr;
    Shader* defaultShader = nullptr;
    Shader* postShader = nullptr;
    Shader* brightShader = nullptr;
    Shader* blurShader = nullptr;
    Shader* shadowDepthShader = nullptr;
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
    std::string postBrightFragPath = "Resources/Shaders/postfx_bright_frag.glsl";
    std::string postBlurFragPath = "Resources/Shaders/postfx_blur_frag.glsl";
    std::string selectionMaskVertPath = "Resources/Shaders/selection_mask_vert.glsl";
    std::string selectionMaskFragPath = "Resources/Shaders/selection_mask_frag.glsl";
    std::string selectionOutlineFragPath = "Resources/Shaders/selection_outline_frag.glsl";
    std::string shadowDepthVertPath = "Resources/Shaders/shadow_depth_vert.glsl";
    std::string shadowDepthFragPath = "Resources/Shaders/shadow_depth_frag.glsl";
    bool autoReloadShaders = true;
    int shadowMapResolution = 512;
    glm::vec3 ambientColor = glm::vec3(0.2f, 0.2f, 0.2f);
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
    uint64_t lastStaticMergeCheckFrameSerial = std::numeric_limits<uint64_t>::max();
    std::unordered_map<int, ShadowCubeMap> shadowCubeMaps;
    std::unordered_map<int, RenderTarget> mirrorTargets;
    std::unordered_map<int, MirrorUpdateState> mirrorUpdateStates;
    std::unordered_map<int, RenderTarget> uiTargets;
    std::vector<StaticMergeBatch> staticMergeBatches;
    std::unordered_set<int> staticMergeSourceIds;
    RenderStats viewportStats;
    RenderStats previewStats;
    PostProcessStats viewportPostStats;
    PostProcessStats previewPostStats;
    RenderStats* activeStats = nullptr;
    float selectionOutlineBlend = 0.0f;
    double selectionOutlineLastUpdateSec = 0.0;
    std::vector<int> selectionOutlineVisualIds;
    std::vector<int> selectionOutlinePrevIds;
    float selectionOutlineSwapBlend = 1.0f;

    void setupFBO();
    void ensureRenderTarget(RenderTarget& target, int w, int h);
    void ensureRenderTarget(RenderTarget& target, int w, int h, bool alpha);
    void ensureRenderTarget(RenderTarget& target, int w, int h, bool alpha, bool hdr);
    void releaseRenderTarget(RenderTarget& target);
    void updateMirrorTargets(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, float fovDeg, float nearPlane, float farPlane);
    void ensureQuad();
    void drawFullscreenQuad();
    void purgeTextureCacheIfNeeded();
    void rebuildStaticMergeBatches(const std::vector<SceneObject>& sceneObjects);
    void resetStats(RenderStats& stats);
    void recordDrawCall();
    void recordMeshDraw();
    void recordFullscreenDraw();
    void clearHistory();
    void clearTarget(RenderTarget& target);
    void renderSceneInternal(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, bool unbindFramebuffer, float fovDeg, float nearPlane, float farPlane, bool drawMirrorObjects);
    unsigned int applyPostProcessing(const Camera& camera, const std::vector<SceneObject>& sceneObjects, unsigned int sourceTexture, int width, int height, bool allowHistory);
    ResolvedPostFX gatherPostFX(const Camera& camera, const std::vector<SceneObject>& sceneObjects) const;

public:
    Renderer() = default;
    ~Renderer();

    void initialize();
    Texture* getTexture(const std::string& path, MaterialProperties::TextureFilter filter = MaterialProperties::TextureFilter::Bilinear);
    void invalidateTexture(const std::string& path);
    Shader* getShader(const std::string& vert, const std::string& frag);
    bool forceReloadShader(const std::string& vert, const std::string& frag);
    void setAmbientColor(const glm::vec3& color) { ambientColor = color; }
    glm::vec3 getAmbientColor() const { return ambientColor; }
    void setShadowMapResolution(int resolution) { shadowMapResolution = std::clamp(resolution, 128, 4096); }
    int getShadowMapResolution() const { return shadowMapResolution; }
    void setShaderAutoReload(bool enabled) { autoReloadShaders = enabled; }
    bool isShaderAutoReloadEnabled() const { return autoReloadShaders; }
    void resize(int w, int h);
    int getWidth() const { return currentWidth; }
    int getHeight() const { return currentHeight; }
    void setFrameSerial(uint64_t serial) { frameSerial = serial; }

    void beginRender(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos);
    void renderSkybox(const glm::mat4& view, const glm::mat4& proj);
    void renderObject(const SceneObject& obj);
    void renderScene(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int selectedId = -1, float fovDeg = FOV, float nearPlane = NEAR_PLANE, float farPlane = FAR_PLANE, bool drawColliders = false, const std::vector<int>* selectedIds = nullptr);
    void renderSelectionOutline(const Camera& camera, const std::vector<SceneObject>& sceneObjects, const std::vector<int>& selectedIds, float fovDeg, float nearPlane, float farPlane);
    unsigned int renderScenePreview(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, float fovDeg, float nearPlane, float farPlane, bool applyPostFX = false, int previewSlot = 0);
    void renderCollisionOverlay(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, float fovDeg, float nearPlane, float farPlane, const std::vector<int>* previewIds = nullptr);
    void endRender();

    Skybox* getSkybox() { return skybox; }
    unsigned int getViewportTexture() const { return displayTexture ? displayTexture : viewportTexture; }
    const RenderStats& getLastViewportStats() const { return viewportStats; }
    const RenderStats& getLastPreviewStats() const { return previewStats; }
    const PostProcessStats& getLastViewportPostStats() const { return viewportPostStats; }
    const PostProcessStats& getLastPreviewPostStats() const { return previewPostStats; }

    struct UiTargetInfo {
        unsigned int fbo = 0;
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
    };
    UiTargetInfo ensureUiTarget(int id, int width, int height);
    void cleanupUiTargets(const std::unordered_set<int>& active);
};
