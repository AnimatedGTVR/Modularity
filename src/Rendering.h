#pragma once

#include "Common.h"
#include "SceneObject.h"
#include "../include/Shaders/Shader.h"
#include "../include/Textures/Texture.h"
#include "../include/Skybox/Skybox.h"
#include <unordered_map>

// Cube vertex data (position + normal + texcoord)
extern float vertices[];

// Primitive generation functions
std::vector<float> generateSphere(int segments = 32, int rings = 16);
std::vector<float> generateCapsule(int segments = 16, int rings = 8);

class Mesh {
private:
    unsigned int VAO, VBO;
    int vertexCount;

public:
    Mesh(const float* vertexData, size_t dataSizeBytes);
    ~Mesh();
    
    void draw() const;
    int getVertexCount() const { return vertexCount; }
};

class OBJLoader {
public:
    struct LoadedMesh {
        std::string path;
        std::unique_ptr<Mesh> mesh;
        std::string name;
        int vertexCount = 0;
        int faceCount = 0;
        bool hasNormals = false;
        bool hasTexCoords = false;
        glm::vec3 boundsMin = glm::vec3(FLT_MAX);
        glm::vec3 boundsMax = glm::vec3(-FLT_MAX);
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
private:
    unsigned int framebuffer = 0, viewportTexture = 0, rbo = 0;
    int currentWidth = 800, currentHeight = 600;
    Shader* shader = nullptr;
    Texture* texture1 = nullptr;
    Texture* texture2 = nullptr;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textureCache;
    Mesh* cubeMesh = nullptr;
    Mesh* sphereMesh = nullptr;
    Mesh* capsuleMesh = nullptr;
    Skybox* skybox = nullptr;

    void setupFBO();

public:
    Renderer() = default;
    ~Renderer();

    void initialize();
    Texture* getTexture(const std::string& path);
    void resize(int w, int h);
    int getWidth() const { return currentWidth; }
    int getHeight() const { return currentHeight; }

    void beginRender(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos);
    void renderSkybox(const glm::mat4& view, const glm::mat4& proj);
    void renderObject(const SceneObject& obj);
    void renderScene(const Camera& camera, const std::vector<SceneObject>& sceneObjects);
    void endRender();

    Skybox* getSkybox() { return skybox; }
    unsigned int getViewportTexture() const { return viewportTexture; }
};
