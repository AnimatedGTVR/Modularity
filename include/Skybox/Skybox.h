#ifndef SKYBOX_H
#define SKYBOX_H

#include <string>
#include "ThirdParty/glm/glm.hpp"

class Shader;
class Texture;

enum class SkyboxMode {
    Procedural = 0,
    Scrolling = 1
};

struct SkyboxSettings {
    SkyboxMode mode = SkyboxMode::Procedural;
    // Render the procedural sky into a cached cubemap (refreshed a face at a
    // time on a short interval) instead of evaluating it per screen pixel.
    bool cachedSky = true;
    std::string sunTexturePath = "Resources/Engine-Root/Skybox/Sun Skybox.png";
    std::string moonTexturePath = "Resources/Engine-Root/Skybox/Moon Skybox.png";
    std::string scrollingTexturePath;
    float scrollingRepeatX = 2.0f;
    float scrollingRepeatY = 1.0f;
    float scrollingLookSensitivity = 1.0f;
    float scrollingVerticalInfluence = 0.18f;
    bool environmentReflections = false;
    float environmentReflectionIntensity = 0.5f;
    float reflectionDistanceFadeStart = 4.0f;
    float reflectionDistanceFadeEnd = 24.0f;
    bool fogEnabled = false;
    int fogMode = 0; // 0 linear, 1 exponential, 2 exponential squared
    float fogStart = 20.0f;
    float fogEnd = 120.0f;
    float fogDensity = 0.015f;
    float fogHeight = 0.0f;
    float fogHeightFalloff = 0.0f;
    glm::vec3 fogColor = glm::vec3(0.65f, 0.72f, 0.78f);
};

class Skybox {
private:
    unsigned int VAO, VBO;
    Shader* skyboxShader;
    Texture* sunTexture = nullptr;
    Texture* moonTexture = nullptr;
    Texture* scrollingTexture = nullptr;
    std::string vertPath = "Resources/Shaders/skybox_vert.glsl";
    std::string fragPath = "Resources/Shaders/skybox_frag.glsl";
    float timeOfDay = 0.5f; // 0.0 = night, 0.25 = sunrise, 0.5 = day, 0.75 = sunset, 1.0 = midnight
    float animationTime = 0.0f;
    SkyboxSettings settings;

    // Baked-sky cache: the procedural sky is rendered into this cubemap a face
    // at a time so fullscreen sky draws collapse to one cube fetch per pixel.
    unsigned int bakedCube = 0;
    unsigned int bakedFbo = 0;
    int bakedDirtyMask = 0;          // bit per cube face
    bool bakedHasFullCapture = false;
    bool bakedAllocFailed = false;
    float bakedLastRefreshSec = -1.0f;

    void setupMesh();
    bool reloadShader();
    void reloadTextures();
    void releaseBakedSky();
    bool ensureBakedSkyTargets();
    void bakeDirtyFaces(int maxFaces);
    bool cachedSkyUsable() const;
    void applySkyUniforms(const glm::mat4& viewMat, const glm::mat4& projMat,
                          float viewportW, float viewportH,
                          const glm::vec2& cameraAngles, bool sampleBaked);
    void drawSkyTriangle();

public:
    Skybox();
    ~Skybox();
    
    void draw(const float* view, const float* projection);
    void setTimeOfDay(float time); // 0.0 to 1.0
    float getTimeOfDay() const { return timeOfDay; }
    void setShaderPaths(const std::string& vert, const std::string& frag);
    std::string getVertPath() const { return vertPath; }
    std::string getFragPath() const { return fragPath; }
    void setSettings(const SkyboxSettings& newSettings);
    const SkyboxSettings& getSettings() const { return settings; }
};

#endif
