#pragma once

#include "Common.h"
#include "../include/Shaders/Shader.h"
#include <array>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

class Renderer;

enum class Light2DType {
    Point = 0,
    Spot = 1,
    Freeform = 2,
    Sprite = 3,
    Global = 4
};

enum class Light2DBlendMode {
    Additive = 0,
    Multiply = 1,
    Subtractive = 2
};

enum class Light2DOverlapOperation {
    Additive = 0,
    Max = 1,
    AlphaBlend = 2
};

enum class Light2DNormalMapQuality {
    Disabled = 0,
    Fast = 1,
    Accurate = 2
};

struct Light2DBlendStyleDefinition {
    std::string name = "Additive";
    Light2DBlendMode mode = Light2DBlendMode::Additive;
    glm::vec4 modulate = glm::vec4(1.0f);
    float intensityScale = 1.0f;
};

struct Light2DFlickerSettings {
    bool enabled = false;
    float speed = 9.0f;
    float amount = 0.12f;
    float seed = 0.0f;
};

struct Light2DComponent {
    bool enabled = true;
    Light2DType type = Light2DType::Point;
    glm::vec4 color = glm::vec4(1.0f);
    float intensity = 1.0f;
    float radius = 5.0f;
    float innerRadius = 0.0f;
    float outerRadius = 5.0f;
    float falloffStrength = 1.0f;
    float innerSpotAngle = 24.0f;
    float outerSpotAngle = 42.0f;
    int blendStyle = 0;
    int lightOrder = 0;
    Light2DOverlapOperation overlapOperation = Light2DOverlapOperation::Additive;
    float shadowStrength = 0.75f;
    bool volumetricEnabled = false;
    bool castsShadows = false;
    bool targetAllLayers = true;
    uint32_t targetLayerMask = 0xFFFFFFFFu;
    Light2DNormalMapQuality normalMapQuality = Light2DNormalMapQuality::Disabled;
    float normalMapDistance = 2.0f;
    bool useDistanceExponent = false;
    float distanceExponent = 1.0f;
    std::string cookieTexturePath;
    glm::vec2 cookieScale = glm::vec2(1.0f);
    float cookieRotation = 0.0f;
    float freeformFeather = 0.6f;
    float freeformEdgeFalloff = 1.0f;
    std::vector<glm::vec2> shapePoints;
    Light2DFlickerSettings flicker;
};

struct ShadowCaster2DComponent {
    bool enabled = true;
    bool castsSelfShadow = false;
    bool targetAllLayers = true;
    uint32_t targetLayerMask = 0xFFFFFFFFu;
    float shadowStrength = 1.0f;
    std::vector<glm::vec2> points;
};

struct Light2DPolygonCache {
    uint64_t version = 0;
    bool valid = false;
    bool selfIntersecting = false;
    glm::vec2 boundsMin = glm::vec2(0.0f);
    glm::vec2 boundsMax = glm::vec2(0.0f);
    std::vector<glm::vec2> vertices;
    std::vector<unsigned int> indices;
    std::string error;
};

struct Light2DScreenSprite {
    int objectId = -1;
    int layer = 0;
    int drawOrder = 0;
    unsigned int textureId = 0;
    unsigned int normalMapTextureId = 0;
    glm::vec2 positions[4] = {};
    glm::vec2 uvs[4] = {};
    glm::vec4 tint = glm::vec4(1.0f);
    bool receiveLighting = true;
    bool unlit = false;
    float emissiveIntensity = 0.0f;
};

struct Light2DScreenLight {
    int objectId = -1;
    bool enabled = true;
    Light2DType type = Light2DType::Point;
    int blendStyle = 0;
    int lightOrder = 0;
    Light2DOverlapOperation overlapOperation = Light2DOverlapOperation::Additive;
    bool targetAllLayers = true;
    uint32_t targetLayerMask = 0xFFFFFFFFu;
    glm::vec4 color = glm::vec4(1.0f);
    float intensity = 1.0f;
    float radius = 5.0f;
    float innerRadius = 0.0f;
    float outerRadius = 5.0f;
    float falloffStrength = 1.0f;
    float innerSpotAngle = 24.0f;
    float outerSpotAngle = 42.0f;
    float shadowStrength = 0.75f;
    bool volumetricEnabled = false;
    bool castsShadows = false;
    glm::vec2 position = glm::vec2(0.0f);
    float rotationRad = 0.0f;
    glm::vec2 boundsMin = glm::vec2(0.0f);
    glm::vec2 boundsMax = glm::vec2(0.0f);
    float freeformFeatherPx = 24.0f;
    float freeformEdgeFalloff = 1.0f;
    unsigned int cookieTextureId = 0;
    glm::vec2 cookieScale = glm::vec2(1.0f);
    float cookieRotationRad = 0.0f;
    std::vector<glm::vec2> polygon;
};

struct Light2DScreenShadowCaster {
    int objectId = -1;
    bool enabled = true;
    bool targetAllLayers = true;
    uint32_t targetLayerMask = 0xFFFFFFFFu;
    float shadowStrength = 1.0f;
    std::vector<glm::vec2> polygon;
};

struct Light2DRenderRequest {
    int width = 0;
    int height = 0;
    glm::vec4 clearColor = glm::vec4(0.0f);
    glm::vec3 baseAmbient = glm::vec3(0.15f);
    float lightingBufferScale = 0.0f;
    std::array<Light2DBlendStyleDefinition, 4> blendStyles = {};
    std::vector<Light2DScreenSprite> sprites;
    std::vector<Light2DScreenLight> lights;
    std::vector<Light2DScreenShadowCaster> shadowCasters;
};

struct Light2DDebugStats {
    int renderedLayers = 0;
    int visibleSprites = 0;
    int visibleLights = 0;
    int freeformLights = 0;
    int shadowCasters = 0;
    int polygonCacheHits = 0;
    int polygonCacheMisses = 0;
    int triangulationFailures = 0;
    int drawCalls = 0;
    float cpuBuildMs = 0.0f;
};

uint32_t Light2DLayerBit(int layer);
bool Light2DLayerMaskContains(uint32_t mask, int layer);
uint64_t Light2DHashShape(const std::vector<glm::vec2>& points);
bool Light2DPolygonSelfIntersects(const std::vector<glm::vec2>& points);
bool Light2DValidatePolygon(const std::vector<glm::vec2>& points, std::string* outError = nullptr);
bool Light2DTriangulatePolygon(const std::vector<glm::vec2>& points,
                               std::vector<unsigned int>& outIndices,
                               std::string* outError = nullptr);

class Lighting2DRenderer {
public:
    Lighting2DRenderer() = default;
    ~Lighting2DRenderer();

    void shutdown();
    unsigned int render(const Light2DRenderRequest& request, Renderer& renderer);
    void setLightingBufferScale(float scale) { lightingBufferScale = std::clamp(scale, 0.25f, 1.0f); }
    float getLightingBufferScale() const { return lightingBufferScale; }

    const Light2DDebugStats& getLastStats() const { return lastStats; }
    const Light2DPolygonCache* getPolygonCache(int objectId) const;
    const Light2DPolygonCache& updatePolygonCache(int objectId, const Light2DComponent& component);
    void clearPolygonCache(int objectId);

private:
    struct RenderTarget {
        unsigned int fbo = 0;
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
    };

    struct LayerBatch {
        int layer = 0;
        size_t spriteBegin = 0;
        size_t spriteEnd = 0;
        std::vector<const Light2DScreenLight*> lights;
    };

    struct SpriteGpuVertex {
        glm::vec2 position = glm::vec2(0.0f);
        glm::vec2 uv = glm::vec2(0.0f);
        glm::vec4 color = glm::vec4(1.0f);
        glm::vec4 params = glm::vec4(0.0f);
    };

    bool initialized = false;
    RenderTarget finalTarget;
    RenderTarget additiveTarget;
    RenderTarget multiplyTarget;
    RenderTarget subtractiveTarget;
    unsigned int quadVao = 0;
    unsigned int quadVbo = 0;
    unsigned int spriteVao = 0;
    unsigned int spriteVbo = 0;
    unsigned int polygonVao = 0;
    unsigned int polygonVbo = 0;
    unsigned int polygonEbo = 0;
    size_t spriteVboCapacity = 0;
    std::unique_ptr<Shader> lightQuadShader;
    std::unique_ptr<Shader> lightFreeformShader;
    std::unique_ptr<Shader> spriteShader;
    float lightingBufferScale = 1.0f;
    std::vector<const Light2DScreenSprite*> orderedSpritesScratch;
    std::vector<const Light2DScreenLight*> orderedLightsScratch;
    std::vector<LayerBatch> layerBatchesScratch;
    std::unordered_map<int, size_t> layerToBatchIndexScratch;
    std::vector<SpriteGpuVertex> spriteVertices;
    std::vector<glm::vec2> polygonVerticesScratch;
    std::unordered_map<int, Light2DPolygonCache> polygonCache;
    Light2DDebugStats lastStats;

    void initializeIfNeeded();
    void destroyTarget(RenderTarget& target);
    void ensureTarget(RenderTarget& target, int width, int height);
    void ensureGeometry();
    void ensureShaders();
    void clearTarget(RenderTarget& target, const glm::vec4& color) const;
    float resolveLightingBufferScale(const Light2DRenderRequest& request) const;
    void renderLayer(const Light2DRenderRequest& request,
                     Renderer& renderer,
                     const LayerBatch& batch,
                     float lightingScale);
    void clearLightTargets();
    void renderLightPass(const Light2DRenderRequest& request,
                         Renderer& renderer,
                         int layer,
                         const std::vector<const Light2DScreenLight*>& lights,
                         float lightingScale);
    void renderSpritePass(const Light2DRenderRequest& request,
                          int layer,
                          size_t spriteBegin,
                          size_t spriteEnd);
    void drawFullscreenQuad() const;
    void drawUnitQuad() const;
};
