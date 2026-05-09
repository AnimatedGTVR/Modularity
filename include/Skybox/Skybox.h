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

    void setupMesh();
    bool reloadShader();
    void reloadTextures();

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
