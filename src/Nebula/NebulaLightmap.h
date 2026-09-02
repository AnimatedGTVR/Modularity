#pragma once

// Editor-side bridge to the Nebula baking framework (../../Nebula), reached
// through the C ABI in ../../NebulaBindings.
//
// The shared library is resolved lazily with dlopen/LoadLibrary rather than
// linked, so a Modularity build without a Rust toolchain still compiles and
// runs; baking simply reports itself unavailable. Nothing here includes
// Engine.h, so the Lightmapping window owns all scene translation.

#include <cstdint>
#include <string>
#include <vector>

// The vendored glm, matching every other engine header. Including <glm/glm.hpp>
// here would pull in a system-wide glm and collide with this one.
#include "../ThirdParty/glm/glm.hpp"

namespace Nebula {

// ── Bake settings (mirrors the Lightmapping window's Bake tab) ───────────────

enum class LightmapMode {
    SingleLightmaps = 0,
    DualLightmaps = 1,
    Directional = 2
};

enum class LightmapQuality {
    Low = 0,
    Medium = 1,
    High = 2
};

struct LightmapSettings {
    LightmapMode mode = LightmapMode::DualLightmaps;
    bool useInForwardRendering = false;
    LightmapQuality quality = LightmapQuality::High;
    int bounces = 1;
    glm::vec3 skyLightColor = glm::vec3(0.83f, 0.89f, 0.98f);
    float skyLightIntensity = 0.0f;
    float bounceBoost = 1.0f;
    float bounceIntensity = 1.0f;
    int finalGatherRays = 1000;
    float contrastThreshold = 0.05f;
    float interpolation = 0.0f;
    int interpolationPoints = 15;
    float ambientOcclusion = 0.0f;
    float lodSurfaceDistance = 1.0f;
    bool lockAtlas = false;
    // Texels per world unit; the atlas side is derived from this and the baked
    // scene's extent unless lockAtlas pins it to lockedAtlasSize.
    float resolution = 50.0f;
    float padding = 0.0f;
    int lockedAtlasSize = 1024;
};

// ── Scene payload handed to the baker ───────────────────────────────────────

struct BakeMaterialData {
    glm::vec4 albedo = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    float roughness = 0.5f;
    float metallic = 0.0f;
    glm::vec3 emissive = glm::vec3(0.0f);
    bool castsShadows = true;
};

struct BakeMeshData {
    std::vector<float> positions;   // 3 per vertex, object space
    std::vector<float> normals;     // 3 per vertex (may be empty)
    std::vector<float> uvs;         // 2 per vertex (may be empty)
    std::vector<uint32_t> indices;  // triangle list
    glm::mat4 worldTransform = glm::mat4(1.0f);
    uint32_t materialId = 0;
    // Editor-side provenance, used for reporting only.
    int sceneObjectId = -1;
    std::string name;
};

struct BakeLightData {
    int kind = 1;  // matches LightType / NB_LIGHT_*
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    float range = 10.0f;
    float innerAngleRadians = 0.26f;
    float outerAngleRadians = 0.44f;
    glm::vec2 halfExtents = glm::vec2(0.5f, 0.5f);
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    bool castsShadows = true;
};

struct BakeSceneData {
    std::vector<BakeMeshData> meshes;
    std::vector<BakeMaterialData> materials;
    std::vector<BakeLightData> lights;
    glm::vec3 boundsMin = glm::vec3(0.0f);
    glm::vec3 boundsMax = glm::vec3(0.0f);
    // Total world-space area of all baked triangles. This, not the bounding
    // box, is what the atlas has to cover, so it drives the derived size.
    float surfaceArea = 0.0f;

    size_t triangleCount() const;
    bool empty() const { return meshes.empty(); }
};

// ── Baker ───────────────────────────────────────────────────────────────────

enum class BakeStatus {
    Idle = 0,
    Running = 1,
    Done = 2,
    Failed = 3,
    Cancelled = 4
};

struct BakeProgress {
    BakeStatus status = BakeStatus::Idle;
    float fraction = -1.0f;  // negative = indeterminate
    uint32_t step = 0;
    uint32_t totalSteps = 0;
    std::string message;
};

// Single owner of the loaded library and the in-flight job. Not thread safe;
// every method is expected to be called from the editor thread.
class LightmapBaker {
public:
    static LightmapBaker& instance();

    // Attempts to load the bindings library once, caching the outcome.
    // `reason` receives a user-facing explanation when unavailable.
    bool available(std::string* reason = nullptr);

    // Resolved path of the loaded library (empty when unavailable).
    const std::string& libraryPath() const { return libraryPath_; }

    // Starts a bake. Returns false and fills `error` if the scene is empty, the
    // library is missing, or a bake is already running.
    bool start(const BakeSceneData& scene,
               const LightmapSettings& settings,
               const std::string& outputPath,
               std::string& error);

    // Pumps progress from the worker. Cheap; call once per frame.
    void update();

    void cancel();

    // Drops any finished job and its result, returning to Idle.
    void reset();

    bool isRunning() const { return progress_.status == BakeStatus::Running; }
    const BakeProgress& progress() const { return progress_; }
    const std::string& outputPath() const { return outputPath_; }

    // Atlas size of the last completed bake (0 when there is none).
    uint32_t resultWidth() const { return resultWidth_; }
    uint32_t resultHeight() const { return resultHeight_; }
    bool hasResult() const { return resultWidth_ > 0 && resultHeight_ > 0; }

    // Fills `dst` with width*height*4 tone-mapped RGBA8 bytes.
    bool copyPreviewRGBA8(std::vector<unsigned char>& dst, int width, int height,
                          float exposure);

    // Writes the completed bake to `outputPath()`. Returns false and fills
    // `error` on failure.
    bool writeResult(std::string& error);

    // Atlas side the given settings would produce for the given amount of
    // baked surface. Exposed so the window can show it before a bake starts.
    static uint32_t resolveAtlasSize(const LightmapSettings& settings,
                                     float worldSurfaceArea);

    ~LightmapBaker();

private:
    LightmapBaker() = default;
    LightmapBaker(const LightmapBaker&) = delete;
    LightmapBaker& operator=(const LightmapBaker&) = delete;

    bool ensureLoaded();

    void* handle_ = nullptr;
    bool loadAttempted_ = false;
    std::string loadError_;
    std::string libraryPath_;

    void* job_ = nullptr;  // NbBakeJob*
    BakeProgress progress_;
    std::string outputPath_;
    uint32_t resultWidth_ = 0;
    uint32_t resultHeight_ = 0;
};

const char* toString(LightmapMode mode);
const char* toString(LightmapQuality quality);

}  // namespace Nebula
