#ifndef SKYBOX_H
#define SKYBOX_H

#include <string>

class Shader;

class Skybox {
private:
    unsigned int VAO, VBO;
    Shader* skyboxShader;
    std::string vertPath = "Resources/Shaders/skybox_vert.glsl";
    std::string fragPath = "Resources/Shaders/skybox_frag.glsl";
    float timeOfDay = 0.5f; // 0.0 = night, 0.25 = sunrise, 0.5 = day, 0.75 = sunset, 1.0 = midnight

    void setupMesh();
    bool reloadShader();

public:
    Skybox();
    ~Skybox();
    
    void draw(const float* view, const float* projection);
    void setTimeOfDay(float time); // 0.0 to 1.0
    float getTimeOfDay() const { return timeOfDay; }
    void setShaderPaths(const std::string& vert, const std::string& frag);
    std::string getVertPath() const { return vertPath; }
    std::string getFragPath() const { return fragPath; }
};

#endif
