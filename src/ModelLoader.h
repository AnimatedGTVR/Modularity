#pragma once

#include "Common.h"
#include "Rendering.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// Supported file extensions for model import
struct ModelFormat {
    std::string extension;
    std::string description;
    bool supportsAnimation;
};

// Model loading result with detailed info
struct ModelLoadResult {
    bool success = false;
    int meshIndex = -1;
    std::string errorMessage;
    int vertexCount = 0;
    int faceCount = 0;
    int meshCount = 0;
    bool hasNormals = false;
    bool hasTexCoords = false;
    bool hasTangents = false;
    std::vector<std::string> meshNames;
};

class ModelLoader {
public:
    // Singleton access
    static ModelLoader& getInstance();
    
    // Load a model file (FBX, OBJ, GLTF, etc.)
    ModelLoadResult loadModel(const std::string& filepath);
    
    // Get mesh by index
    Mesh* getMesh(int index);
    
    // Get mesh info
    const OBJLoader::LoadedMesh* getMeshInfo(int index) const;
    
    // Get all loaded meshes
    const std::vector<OBJLoader::LoadedMesh>& getAllMeshes() const;
    
    // Check if file extension is supported
    bool isSupported(const std::string& filepath) const;
    
    // Get list of supported formats
    static std::vector<ModelFormat> getSupportedFormats();
    
    // Clear all loaded meshes
    void clear();
    
    // Get mesh count
    size_t getMeshCount() const;

private:
    ModelLoader() = default;
    ~ModelLoader() = default;
    ModelLoader(const ModelLoader&) = delete;
    ModelLoader& operator=(const ModelLoader&) = delete;
    
    // Process Assimp scene
    void processNode(aiNode* node, const aiScene* scene, std::vector<float>& vertices, glm::vec3& boundsMin, glm::vec3& boundsMax);
    void processMesh(aiMesh* mesh, const aiScene* scene, std::vector<float>& vertices, glm::vec3& boundsMin, glm::vec3& boundsMax);
    
    // Storage for loaded meshes (reusing OBJLoader::LoadedMesh structure)
    std::vector<OBJLoader::LoadedMesh> loadedMeshes;
    
    // Assimp importer (kept for resource management)
    Assimp::Importer importer;
};

// Global accessor
ModelLoader& getModelLoader();
