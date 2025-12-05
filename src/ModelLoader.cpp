#include "ModelLoader.h"
#include <algorithm>
#include <iostream>

ModelLoader& ModelLoader::getInstance() {
    static ModelLoader instance;
    return instance;
}

ModelLoader& getModelLoader() {
    return ModelLoader::getInstance();
}

std::vector<ModelFormat> ModelLoader::getSupportedFormats() {
    return {
        {".fbx", "Autodesk FBX", true},
        {".obj", "Wavefront OBJ", false},
        {".gltf", "glTF 2.0", true},
        {".glb", "glTF Binary", true},
        {".dae", "Collada", true},
        {".blend", "Blender", true},
        {".3ds", "3D Studio Max", false},
        {".ase", "3D Studio ASE", false},
        {".ifc", "IFC-STEP", false},
        {".xgl", "XGL", false},
        {".zgl", "ZGL", false},
        {".ply", "Stanford PLY", false},
        {".dxf", "AutoCAD DXF", false},
        {".lwo", "LightWave", false},
        {".lws", "LightWave Scene", false},
        {".lxo", "Modo", false},
        {".stl", "Stereolithography", false},
        {".x", "DirectX X", true},
        {".ac", "AC3D", false},
        {".ms3d", "Milkshape 3D", true},
        {".cob", "TrueSpace", false},
        {".scn", "TrueSpace Scene", false},
        {".bvh", "Biovision BVH", true},
        {".csm", "CharacterStudio Motion", true},
        {".irrmesh", "Irrlicht Mesh", false},
        {".irr", "Irrlicht Scene", false},
        {".mdl", "Quake MDL", true},
        {".md2", "Quake II MD2", true},
        {".md3", "Quake III MD3", true},
        {".pk3", "Quake III BSP", false},
        {".mdc", "RtCW MDC", true},
        {".md5mesh", "Doom 3 MD5", true},
        {".md5anim", "Doom 3 MD5 Anim", true},
        {".smd", "Valve SMD", true},
        {".vta", "Valve VTA", false},
        {".ogex", "Open Game Engine Exchange", true},
        {".3d", "Unreal 3D", false},
        {".b3d", "BlitzBasic 3D", true},
        {".q3d", "Quick3D", false},
        {".q3s", "Quick3D Scene", false},
        {".nff", "Neutral File Format", false},
        {".off", "Object File Format", false},
        {".raw", "Raw Triangles", false},
        {".ter", "Terragen Terrain", false},
        {".hmp", "3D GameStudio HMP", false},
        {".ndo", "Nendo", false},
    };
}

bool ModelLoader::isSupported(const std::string& filepath) const {
    std::string ext = fs::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    auto formats = getSupportedFormats();
    for (const auto& format : formats) {
        if (format.extension == ext) {
            return true;
        }
    }
    return false;
}

ModelLoadResult ModelLoader::loadModel(const std::string& filepath) {
    ModelLoadResult result;
    
    // Check if already loaded
    for (size_t i = 0; i < loadedMeshes.size(); i++) {
        if (loadedMeshes[i].path == filepath) {
            result.success = true;
            result.meshIndex = static_cast<int>(i);
            const auto& mesh = loadedMeshes[i];
            result.vertexCount = mesh.vertexCount;
            result.faceCount = mesh.faceCount;
            result.hasNormals = mesh.hasNormals;
            result.hasTexCoords = mesh.hasTexCoords;
            return result;
        }
    }
    
    // Check if format is supported
    if (!isSupported(filepath)) {
        result.errorMessage = "Unsupported file format: " + fs::path(filepath).extension().string();
        return result;
    }
    
    // Configure import flags
    unsigned int importFlags = 
        aiProcess_Triangulate |           // Convert all faces to triangles
        aiProcess_GenSmoothNormals |      // Generate smooth normals if missing
        aiProcess_FlipUVs |               // Flip UV coordinates for OpenGL
        aiProcess_CalcTangentSpace |      // Calculate tangents and bitangents
        aiProcess_JoinIdenticalVertices | // Optimize vertex count
        aiProcess_SortByPType |           // Sort by primitive type
        aiProcess_OptimizeMeshes |        // Reduce number of meshes
        aiProcess_ValidateDataStructure;  // Validate the imported data
    
    // Load the model
    const aiScene* scene = importer.ReadFile(filepath, importFlags);
    
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        result.errorMessage = "Assimp error: " + std::string(importer.GetErrorString());
        return result;
    }
    
    // Process all meshes in the scene
    std::vector<float> vertices;
    result.meshCount = scene->mNumMeshes;
    result.hasNormals = true;
    result.hasTexCoords = false;
    result.hasTangents = false;
    
    // Process the root node recursively
    processNode(scene->mRootNode, scene, vertices);
    
    // Check mesh properties
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
        result.meshNames.push_back(mesh->mName.C_Str());
        result.vertexCount += mesh->mNumVertices;
        result.faceCount += mesh->mNumFaces;
        if (mesh->mTextureCoords[0]) result.hasTexCoords = true;
        if (mesh->mTangents) result.hasTangents = true;
    }
    
    if (vertices.empty()) {
        result.errorMessage = "No vertices found in model file";
        return result;
    }
    
    // Create the mesh
    OBJLoader::LoadedMesh loaded;
    loaded.path = filepath;
    loaded.name = fs::path(filepath).stem().string();
    loaded.mesh = std::make_unique<Mesh>(vertices.data(), vertices.size() * sizeof(float));
    loaded.vertexCount = result.vertexCount;
    loaded.faceCount = result.faceCount;
    loaded.hasNormals = result.hasNormals;
    loaded.hasTexCoords = result.hasTexCoords;
    
    loadedMeshes.push_back(std::move(loaded));
    
    result.success = true;
    result.meshIndex = static_cast<int>(loadedMeshes.size() - 1);
    
    std::cerr << "[ModelLoader] Loaded " << filepath << " with " 
              << result.vertexCount << " vertices, " 
              << result.faceCount << " faces, "
              << result.meshCount << " meshes" << std::endl;
    
    return result;
}

void ModelLoader::processNode(aiNode* node, const aiScene* scene, std::vector<float>& vertices) {
    // Process all meshes in this node
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        processMesh(mesh, scene, vertices);
    }
    
    // Process children nodes
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i], scene, vertices);
    }
}

void ModelLoader::processMesh(aiMesh* mesh, const aiScene* scene, std::vector<float>& vertices) {
    // Process each face
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        
        // Process each vertex of the face
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            unsigned int index = face.mIndices[j];
            
            // Position
            vertices.push_back(mesh->mVertices[index].x);
            vertices.push_back(mesh->mVertices[index].y);
            vertices.push_back(mesh->mVertices[index].z);
            
            // Normal
            if (mesh->mNormals) {
                vertices.push_back(mesh->mNormals[index].x);
                vertices.push_back(mesh->mNormals[index].y);
                vertices.push_back(mesh->mNormals[index].z);
            } else {
                vertices.push_back(0.0f);
                vertices.push_back(1.0f);
                vertices.push_back(0.0f);
            }
            
            // Texture coordinates
            if (mesh->mTextureCoords[0]) {
                vertices.push_back(mesh->mTextureCoords[0][index].x);
                vertices.push_back(mesh->mTextureCoords[0][index].y);
            } else {
                vertices.push_back(0.0f);
                vertices.push_back(0.0f);
            }
        }
    }
}

Mesh* ModelLoader::getMesh(int index) {
    if (index < 0 || index >= static_cast<int>(loadedMeshes.size())) {
        return nullptr;
    }
    return loadedMeshes[index].mesh.get();
}

const OBJLoader::LoadedMesh* ModelLoader::getMeshInfo(int index) const {
    if (index < 0 || index >= static_cast<int>(loadedMeshes.size())) {
        return nullptr;
    }
    return &loadedMeshes[index];
}

const std::vector<OBJLoader::LoadedMesh>& ModelLoader::getAllMeshes() const {
    return loadedMeshes;
}

void ModelLoader::clear() {
    loadedMeshes.clear();
}

size_t ModelLoader::getMeshCount() const {
    return loadedMeshes.size();
}
