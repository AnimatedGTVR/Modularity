#ifndef SKYBOX_H
#define SKYBOX_H

#include <string>

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
