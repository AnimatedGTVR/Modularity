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
        std::vector<glm::vec3> triangleVertices; // positions duplicated per-triangle for picking
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
    struct RenderTarget {
        unsigned int fbo = 0;
        unsigned int texture = 0;
        unsigned int rbo = 0;
        int width = 0;
        int height = 0;
    };
    RenderTarget previewTarget;
    RenderTarget postTarget;
    RenderTarget previewPostTarget;
    RenderTarget historyTarget;
    RenderTarget bloomTargetA;
    RenderTarget bloomTargetB;
    struct MirrorSmoothing {
        glm::vec2 planar = glm::vec2(0.0f);
        bool initialized = false;
    };
    std::unordered_map<int, MirrorSmoothing> mirrorSmooth;
    Shader* shader = nullptr;
    Shader* defaultShader = nullptr;
    Shader* postShader = nullptr;
    Shader* brightShader = nullptr;
    Shader* blurShader = nullptr;
    Texture* texture1 = nullptr;
    Texture* texture2 = nullptr;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textureCache;
    std::unordered_map<std::string, std::unique_ptr<Texture>> previewTextureCacheLinear;
    std::unordered_map<std::string, std::unique_ptr<Texture>> previewTextureCacheNearest;
    struct ShaderEntry {
        std::unique_ptr<Shader> shader;
        fs::file_time_type vertTime;
        fs::file_time_type fragTime;
        std::string vertPath;
        std::string fragPath;
    };
    std::unordered_map<std::string, ShaderEntry> shaderCache;
    std::string defaultVertPath = "Resources/Shaders/vert.glsl";
    std::string defaultFragPath = "Resources/Shaders/frag.glsl";
    std::string postVertPath = "Resources/Shaders/postfx_vert.glsl";
    std::string postFragPath = "Resources/Shaders/postfx_frag.glsl";
    std::string postBrightFragPath = "Resources/Shaders/postfx_bright_frag.glsl";
    std::string postBlurFragPath = "Resources/Shaders/postfx_blur_frag.glsl";
    bool autoReloadShaders = true;
    glm::vec3 ambientColor = glm::vec3(0.2f, 0.2f, 0.2f);
    Mesh* cubeMesh = nullptr;
    Mesh* sphereMesh = nullptr;
    Mesh* capsuleMesh = nullptr;
    Mesh* planeMesh = nullptr;
    Skybox* skybox = nullptr;
    unsigned int quadVAO = 0;
    unsigned int quadVBO = 0;
    unsigned int displayTexture = 0;
    bool historyValid = false;
    std::unordered_map<int, RenderTarget> mirrorTargets;

    void setupFBO();
    void ensureRenderTarget(RenderTarget& target, int w, int h);
    void releaseRenderTarget(RenderTarget& target);
    void updateMirrorTargets(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, float fovDeg, float nearPlane, float farPlane);
    void ensureQuad();
    void drawFullscreenQuad();
    void clearHistory();
    void clearTarget(RenderTarget& target);
    void renderSceneInternal(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, bool unbindFramebuffer, float fovDeg, float nearPlane, float farPlane, bool drawMirrorObjects);
    unsigned int applyPostProcessing(const std::vector<SceneObject>& sceneObjects, unsigned int sourceTexture, int width, int height, bool allowHistory);
    PostFXSettings gatherPostFX(const std::vector<SceneObject>& sceneObjects) const;

public:
    Renderer() = default;
    ~Renderer();

    void initialize();
    Texture* getTexture(const std::string& path);
    Texture* getTexturePreview(const std::string& path, bool nearest);
    Shader* getShader(const std::string& vert, const std::string& frag);
    bool forceReloadShader(const std::string& vert, const std::string& frag);
    void setAmbientColor(const glm::vec3& color) { ambientColor = color; }
    glm::vec3 getAmbientColor() const { return ambientColor; }
    void resize(int w, int h);
    int getWidth() const { return currentWidth; }
    int getHeight() const { return currentHeight; }

    void beginRender(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos);
    void renderSkybox(const glm::mat4& view, const glm::mat4& proj);
    void renderObject(const SceneObject& obj);
    void renderScene(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int selectedId = -1, float fovDeg = FOV, float nearPlane = NEAR_PLANE, float farPlane = FAR_PLANE);
    unsigned int renderScenePreview(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, float fovDeg, float nearPlane, float farPlane, bool applyPostFX = false);
    void endRender();

    Skybox* getSkybox() { return skybox; }
    unsigned int getViewportTexture() const { return displayTexture ? displayTexture : viewportTexture; }
};
