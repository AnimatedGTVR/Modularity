#pragma once

#include "Common.h"
#include "Rendering.h"
#include <cstdint>

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

// Raw mesh asset for editable geometry (.rmesh)
struct RawMeshAsset {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::u32vec3> faces;
    glm::vec3 boundsMin = glm::vec3(FLT_MAX);
    glm::vec3 boundsMax = glm::vec3(-FLT_MAX);
    bool hasNormals = false;
    bool hasUVs = false;
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
    
    // Export a model file into a raw editable mesh asset (.rmesh)
    bool exportRawMesh(const std::string& inputFile, const std::string& outputFile, std::string& errorMsg);

    // Load a raw mesh asset from disk
    bool loadRawMesh(const std::string& filepath, RawMeshAsset& out, std::string& errorMsg);

    // Save a raw mesh asset to disk
    bool saveRawMesh(const RawMeshAsset& asset, const std::string& filepath, std::string& errorMsg);

    // Update an already-loaded raw mesh in GPU memory
    bool updateRawMesh(int meshIndex, const RawMeshAsset& asset, std::string& errorMsg);
    
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
    void processNode(aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform, std::vector<float>& vertices, std::vector<glm::vec3>& triPositions, glm::vec3& boundsMin, glm::vec3& boundsMax);
    void processMesh(aiMesh* mesh, const aiMatrix4x4& transform, std::vector<float>& vertices, std::vector<glm::vec3>& triPositions, glm::vec3& boundsMin, glm::vec3& boundsMax);
    
    // Storage for loaded meshes (reusing OBJLoader::LoadedMesh structure)
    std::vector<OBJLoader::LoadedMesh> loadedMeshes;
    
    // Assimp importer (kept for resource management)
    Assimp::Importer importer;
};

// Global accessor
ModelLoader& getModelLoader();
