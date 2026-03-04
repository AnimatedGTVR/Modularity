#include "ModelLoader.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <unordered_set>
#include <functional>
#include <assimp/material.h>
#include "ThirdParty/glm/gtc/quaternion.hpp"

ModelLoader& ModelLoader::getInstance() {
    static ModelLoader instance;
    return instance;
}

static void collectRawMeshData(aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform, RawMeshAsset& out);
static bool buildSceneMeshes(const std::string& filepath, const aiScene* scene,
                             std::vector<OBJLoader::LoadedMesh>& loadedMeshes,
                             ModelSceneData& out, std::string& errorMsg);
static void buildSceneNodes(const aiScene* scene,
                            const std::vector<int>& meshIndices,
                            ModelSceneData& out);
static glm::mat4 aiToGlm(const aiMatrix4x4& m);

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
        {".rmesh", "Modularity Raw Mesh", false},
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
    std::string extLower = fs::path(filepath).extension().string();
    std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
    
    // Check if already loaded
    for (size_t i = 0; i < loadedMeshes.size(); i++) {
        if (loadedMeshes[i].path == filepath) {
            result.success = true;
            result.meshIndex = static_cast<int>(i);
            result.meshIndices.push_back(result.meshIndex);
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

    // Handle native raw mesh import without Assimp
    if (extLower == ".rmesh") {
        RawMeshAsset raw;
        std::string error;
        if (!loadRawMesh(filepath, raw, error)) {
            result.errorMessage = error;
            return result;
        }

        // Build interleaved triangle list for GPU upload
        std::vector<float> vertices;
        vertices.reserve(raw.faces.size() * 3 * 8);
        std::vector<glm::vec3> triPositions;
        triPositions.reserve(raw.faces.size() * 3);

        auto getPos = [&](uint32_t idx) -> const glm::vec3& { return raw.positions[idx]; };
        auto getNorm = [&](uint32_t idx) -> glm::vec3 {
            if (idx < raw.normals.size()) return raw.normals[idx];
            return glm::vec3(0.0f);
        };
        auto getUV = [&](uint32_t idx) -> glm::vec2 {
            if (idx < raw.uvs.size()) return raw.uvs[idx];
            return glm::vec2(0.0f);
        };

        for (const auto& face : raw.faces) {
            const uint32_t idx[3] = { face.x, face.y, face.z };
            glm::vec3 faceNormal(0.0f);
            if (!raw.hasNormals) {
                const glm::vec3& a = getPos(idx[0]);
                const glm::vec3& b = getPos(idx[1]);
                const glm::vec3& c = getPos(idx[2]);
                faceNormal = glm::normalize(glm::cross(b - a, c - a));
            }
            for (int i = 0; i < 3; i++) {
                glm::vec3 pos = getPos(idx[i]);
                glm::vec3 n = raw.hasNormals ? getNorm(idx[i]) : faceNormal;
                glm::vec2 uv = raw.hasUVs ? getUV(idx[i]) : glm::vec2(0.0f);

                triPositions.push_back(pos);
                vertices.push_back(pos.x);
                vertices.push_back(pos.y);
                vertices.push_back(pos.z);
                vertices.push_back(n.x);
                vertices.push_back(n.y);
                vertices.push_back(n.z);
                vertices.push_back(uv.x);
                vertices.push_back(uv.y);
            }
        }

        if (vertices.empty()) {
            result.errorMessage = "No triangles found in raw mesh";
            return result;
        }

        OBJLoader::LoadedMesh loaded;
        loaded.path = filepath;
        loaded.name = fs::path(filepath).stem().string();
        loaded.mesh = std::make_unique<Mesh>(vertices.data(), vertices.size() * sizeof(float));
        loaded.vertexCount = static_cast<int>(vertices.size() / 8);
        loaded.faceCount = static_cast<int>(raw.faces.size());
        loaded.hasNormals = raw.hasNormals;
        loaded.hasTexCoords = raw.hasUVs;
        loaded.boundsMin = raw.boundsMin;
        loaded.boundsMax = raw.boundsMax;
        loaded.triangleVertices = std::move(triPositions);
        loaded.positions = raw.positions;
        loaded.triangleIndices.reserve(raw.faces.size() * 3);
        for (const auto& face : raw.faces) {
            loaded.triangleIndices.push_back(face.x);
            loaded.triangleIndices.push_back(face.y);
            loaded.triangleIndices.push_back(face.z);
        }

        loadedMeshes.push_back(std::move(loaded));

        result.success = true;
        result.meshIndex = static_cast<int>(loadedMeshes.size() - 1);
        result.vertexCount = static_cast<int>(raw.positions.size());
        result.faceCount = static_cast<int>(raw.faces.size());
        result.meshCount = 1;
        result.hasNormals = raw.hasNormals;
        result.hasTexCoords = raw.hasUVs;
        result.meshNames.push_back(loadedMeshes.back().name);
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
    
    glm::vec3 boundsMin(FLT_MAX);
    glm::vec3 boundsMax(-FLT_MAX);
    std::vector<glm::vec3> triPositions;
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;

    // Process all meshes in the scene
    std::vector<float> vertices;
    result.meshCount = scene->mNumMeshes;
    result.hasNormals = true;
    result.hasTexCoords = false;
    result.hasTangents = false;
    
    // Process the root node recursively
    processNode(scene->mRootNode, scene, aiMatrix4x4(), vertices, triPositions, positions, indices, boundsMin, boundsMax);
    
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
    
    loaded.boundsMin = boundsMin;
    loaded.boundsMax = boundsMax;
    loaded.triangleVertices = std::move(triPositions);
    loaded.positions = std::move(positions);
    loaded.triangleIndices = std::move(indices);

    loadedMeshes.push_back(std::move(loaded));
    
    result.success = true;
    result.meshIndex = static_cast<int>(loadedMeshes.size() - 1);
    
    std::cerr << "[ModelLoader] Loaded " << filepath << " with " 
              << result.vertexCount << " vertices, " 
              << result.faceCount << " faces, "
              << result.meshCount << " meshes" << std::endl;
    
    return result;
}

bool ModelLoader::loadModelScene(const std::string& filepath, ModelSceneData& out, std::string& errorMsg) {
    out = ModelSceneData();

    if (!isSupported(filepath)) {
        errorMsg = "Unsupported file format: " + fs::path(filepath).extension().string();
        return false;
    }

    auto cached = cachedScenes.find(filepath);
    if (cached != cachedScenes.end()) {
        out = cached->second;
        return true;
    }

    unsigned int importFlags =
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_SortByPType |
        aiProcess_ValidateDataStructure;

    const aiScene* scene = importer.ReadFile(filepath, importFlags);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        errorMsg = "Assimp error: " + std::string(importer.GetErrorString());
        return false;
    }

    if (!buildSceneMeshes(filepath, scene, loadedMeshes, out, errorMsg)) {
        return false;
    }

    buildSceneNodes(scene, out.meshIndices, out);

    out.animations.clear();
    if (scene->mNumAnimations > 0) {
        out.animations.reserve(scene->mNumAnimations);
        for (unsigned int i = 0; i < scene->mNumAnimations; ++i) {
            aiAnimation* anim = scene->mAnimations[i];
            ModelSceneData::AnimationClip clip;
            clip.name = anim->mName.C_Str();
            if (clip.name.empty()) {
                clip.name = "Clip_" + std::to_string(i);
            }
            clip.duration = anim->mDuration;
            clip.ticksPerSecond = anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 25.0;
            clip.channels.reserve(anim->mNumChannels);
            for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
                aiNodeAnim* ch = anim->mChannels[c];
                ModelSceneData::AnimChannel channel;
                channel.nodeName = ch->mNodeName.C_Str();
                channel.positions.reserve(ch->mNumPositionKeys);
                for (unsigned int k = 0; k < ch->mNumPositionKeys; ++k) {
                    const auto& key = ch->mPositionKeys[k];
                    ModelSceneData::AnimVecKey vk;
                    vk.time = static_cast<float>(key.mTime);
                    vk.value = glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z);
                    channel.positions.push_back(vk);
                }
                channel.rotations.reserve(ch->mNumRotationKeys);
                for (unsigned int k = 0; k < ch->mNumRotationKeys; ++k) {
                    const auto& key = ch->mRotationKeys[k];
                    ModelSceneData::AnimQuatKey qk;
                    qk.time = static_cast<float>(key.mTime);
                    qk.value = glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z);
                    channel.rotations.push_back(qk);
                }
                channel.scales.reserve(ch->mNumScalingKeys);
                for (unsigned int k = 0; k < ch->mNumScalingKeys; ++k) {
                    const auto& key = ch->mScalingKeys[k];
                    ModelSceneData::AnimVecKey sk;
                    sk.time = static_cast<float>(key.mTime);
                    sk.value = glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z);
                    channel.scales.push_back(sk);
                }
                clip.channels.push_back(std::move(channel));
            }
            out.animations.push_back(std::move(clip));
        }
    }

    cachedScenes[filepath] = out;
    return true;
}

bool ModelLoader::exportRawMesh(const std::string& inputFile, const std::string& outputFile, std::string& errorMsg) {
    fs::path inPath(inputFile);
    if (!fs::exists(inPath)) {
        errorMsg = "File not found: " + inputFile;
        return false;
    }
    if (!isSupported(inputFile)) {
        errorMsg = "Unsupported file format for raw export";
        return false;
    }

    Assimp::Importer localImporter;
    unsigned int importFlags =
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals |
        aiProcess_FlipUVs;

    const aiScene* scene = localImporter.ReadFile(inputFile, importFlags);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        errorMsg = "Assimp error: " + std::string(localImporter.GetErrorString());
        return false;
    }

    RawMeshAsset raw;
    collectRawMeshData(scene->mRootNode, scene, aiMatrix4x4(), raw);

    if (raw.positions.empty() || raw.faces.empty()) {
        errorMsg = "No geometry found to export";
        return false;
    }

    fs::path outPath(outputFile.empty() ? inPath : fs::path(outputFile));
    if (outPath.extension().empty()) {
        outPath.replace_extension(".rmesh");
    }

    if (!saveRawMesh(raw, outPath.string(), errorMsg)) {
        return false;
    }

    std::cerr << "[ModelLoader] Exported raw mesh to " << outPath << " ("
              << raw.positions.size() << " verts, " << raw.faces.size() << " faces)" << std::endl;
    return true;
}

bool ModelLoader::loadRawMesh(const std::string& filepath, RawMeshAsset& out, std::string& errorMsg) {
    out = RawMeshAsset();

    std::ifstream in(filepath, std::ios::binary);
    if (!in) {
        errorMsg = "Unable to open raw mesh: " + filepath;
        return false;
    }

    struct Header {
        char magic[6];
        uint32_t version;
        uint32_t vertexCount;
        uint32_t faceCount;
    } header{};

    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (std::strncmp(header.magic, "RMESH", 5) != 0) {
        errorMsg = "Invalid raw mesh header";
        return false;
    }
    if (header.version != 1) {
        errorMsg = "Unsupported raw mesh version";
        return false;
    }

    if (header.vertexCount == 0 || header.faceCount == 0) {
        errorMsg = "Raw mesh contains no geometry";
        return false;
    }

    in.seekg(0, std::ios::end);
    std::streamoff fileSize = in.tellg();
    in.seekg(sizeof(header), std::ios::beg);

    in.read(reinterpret_cast<char*>(&out.boundsMin.x), sizeof(float) * 3);
    in.read(reinterpret_cast<char*>(&out.boundsMax.x), sizeof(float) * 3);

    const std::streamoff payloadSize = fileSize - sizeof(header) - sizeof(float) * 6;
    const std::streamoff positionsSize = static_cast<std::streamoff>(sizeof(glm::vec3)) * header.vertexCount;
    const std::streamoff normalsSize = static_cast<std::streamoff>(sizeof(glm::vec3)) * header.vertexCount;
    const std::streamoff uvsSize = static_cast<std::streamoff>(sizeof(glm::vec2)) * header.vertexCount;
    const std::streamoff facesSize = static_cast<std::streamoff>(sizeof(glm::u32vec3)) * header.faceCount;

    bool hasNormals = false;
    bool hasUVs = false;
    if (payloadSize == positionsSize + normalsSize + uvsSize + facesSize) {
        hasNormals = true;
        hasUVs = true;
    } else if (payloadSize == positionsSize + normalsSize + facesSize) {
        hasNormals = true;
    } else if (payloadSize == positionsSize + uvsSize + facesSize) {
        hasUVs = true;
    } else if (payloadSize == positionsSize + facesSize) {
        // legacy raw meshes without normals/uvs
    } else if (payloadSize < positionsSize + facesSize) {
        errorMsg = "Raw mesh data is truncated";
        return false;
    }

    out.positions.resize(header.vertexCount);
    out.faces.resize(header.faceCount);

    in.read(reinterpret_cast<char*>(out.positions.data()), sizeof(glm::vec3) * out.positions.size());
    if (hasNormals) {
        out.normals.resize(header.vertexCount);
        in.read(reinterpret_cast<char*>(out.normals.data()), sizeof(glm::vec3) * out.normals.size());
    } else {
        out.normals.assign(header.vertexCount, glm::vec3(0.0f));
    }
    if (hasUVs) {
        out.uvs.resize(header.vertexCount);
        in.read(reinterpret_cast<char*>(out.uvs.data()), sizeof(glm::vec2) * out.uvs.size());
    } else {
        out.uvs.assign(header.vertexCount, glm::vec2(0.0f));
    }
    in.read(reinterpret_cast<char*>(out.faces.data()), sizeof(glm::u32vec3) * out.faces.size());

    if (!in.good()) {
        errorMsg = "Unexpected EOF while reading raw mesh";
        return false;
    }

    auto validIndex = [&](uint32_t idx) { return idx < out.positions.size(); };
    for (const auto& face : out.faces) {
        if (!validIndex(face.x) || !validIndex(face.y) || !validIndex(face.z)) {
            errorMsg = "Raw mesh contains invalid face indices";
            return false;
        }
    }

    out.hasNormals = false;
    for (const auto& n : out.normals) {
        if (glm::length(n) > 1e-4f) { out.hasNormals = true; break; }
    }

    out.hasUVs = false;
    for (const auto& uv : out.uvs) {
        if (std::abs(uv.x) > 1e-6f || std::abs(uv.y) > 1e-6f) { out.hasUVs = true; break; }
    }

    if (!out.hasNormals) {
        out.normals.assign(out.positions.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> accum(out.positions.size(), glm::vec3(0.0f));

        for (const auto& face : out.faces) {
            const glm::vec3& a = out.positions[face.x];
            const glm::vec3& b = out.positions[face.y];
            const glm::vec3& c = out.positions[face.z];
            glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
            accum[face.x] += n;
            accum[face.y] += n;
            accum[face.z] += n;
        }
        for (size_t i = 0; i < accum.size(); i++) {
            if (glm::length(accum[i]) > 1e-6f) {
                out.normals[i] = glm::normalize(accum[i]);
            }
        }
        out.hasNormals = true;
    }

    // Recompute bounds if file stored invalid values
    if (!std::isfinite(out.boundsMin.x) || !std::isfinite(out.boundsMax.x)) {
        out.boundsMin = glm::vec3(FLT_MAX);
        out.boundsMax = glm::vec3(-FLT_MAX);
        for (const auto& p : out.positions) {
            out.boundsMin.x = std::min(out.boundsMin.x, p.x);
            out.boundsMin.y = std::min(out.boundsMin.y, p.y);
            out.boundsMin.z = std::min(out.boundsMin.z, p.z);
            out.boundsMax.x = std::max(out.boundsMax.x, p.x);
            out.boundsMax.y = std::max(out.boundsMax.y, p.y);
            out.boundsMax.z = std::max(out.boundsMax.z, p.z);
        }
    }

    return true;
}

bool ModelLoader::saveRawMesh(const RawMeshAsset& asset, const std::string& filepath, std::string& errorMsg) {
    if (asset.positions.empty() || asset.faces.empty()) {
        errorMsg = "Raw mesh is empty";
        return false;
    }

    fs::path outPath(filepath);
    if (outPath.extension().empty()) {
        outPath.replace_extension(".rmesh");
    }

    std::vector<glm::vec3> normalsData;
    normalsData.resize(asset.positions.size(), glm::vec3(0.0f));
    if (asset.normals.size() == asset.positions.size()) {
        normalsData = asset.normals;
    }

    std::vector<glm::vec2> uvsData;
    uvsData.resize(asset.positions.size(), glm::vec2(0.0f));
    if (asset.uvs.size() == asset.positions.size()) {
        uvsData = asset.uvs;
    }

    struct Header {
        char magic[6] = {'R','M','E','S','H','\0'};
        uint32_t version = 1;
        uint32_t vertexCount = 0;
        uint32_t faceCount = 0;
    } header;

    header.vertexCount = static_cast<uint32_t>(asset.positions.size());
    header.faceCount = static_cast<uint32_t>(asset.faces.size());

    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        errorMsg = "Unable to open file for writing: " + outPath.string();
        return false;
    }

    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&asset.boundsMin.x), sizeof(float) * 3);
    out.write(reinterpret_cast<const char*>(&asset.boundsMax.x), sizeof(float) * 3);
    out.write(reinterpret_cast<const char*>(asset.positions.data()), sizeof(glm::vec3) * asset.positions.size());
    out.write(reinterpret_cast<const char*>(normalsData.data()), sizeof(glm::vec3) * normalsData.size());
    out.write(reinterpret_cast<const char*>(uvsData.data()), sizeof(glm::vec2) * uvsData.size());
    out.write(reinterpret_cast<const char*>(asset.faces.data()), sizeof(glm::u32vec3) * asset.faces.size());

    if (!out.good()) {
        errorMsg = "Failed while writing raw mesh file";
        return false;
    }

    return true;
}

bool ModelLoader::updateRawMesh(int meshIndex, const RawMeshAsset& asset, std::string& errorMsg) {
    if (meshIndex < 0 || meshIndex >= static_cast<int>(loadedMeshes.size())) {
        errorMsg = "Invalid mesh index";
        return false;
    }
    if (asset.positions.empty() || asset.faces.empty()) {
        errorMsg = "Raw mesh is empty";
        return false;
    }

    std::vector<float> vertices;
    vertices.reserve(asset.faces.size() * 3 * 8);
    std::vector<glm::vec3> triPositions;
    triPositions.reserve(asset.faces.size() * 3);

    auto getPos = [&](uint32_t idx) -> const glm::vec3& { return asset.positions[idx]; };
    auto getNorm = [&](uint32_t idx) -> glm::vec3 {
        if (idx < asset.normals.size()) return asset.normals[idx];
        return glm::vec3(0.0f);
    };
    auto getUV = [&](uint32_t idx) -> glm::vec2 {
        if (idx < asset.uvs.size()) return asset.uvs[idx];
        return glm::vec2(0.0f);
    };

    for (const auto& face : asset.faces) {
        const uint32_t idx[3] = { face.x, face.y, face.z };
        glm::vec3 faceNormal(0.0f);
        if (!asset.hasNormals) {
            const glm::vec3& a = getPos(idx[0]);
            const glm::vec3& b = getPos(idx[1]);
            const glm::vec3& c = getPos(idx[2]);
            faceNormal = glm::normalize(glm::cross(b - a, c - a));
        }
        for (int i = 0; i < 3; i++) {
            glm::vec3 pos = getPos(idx[i]);
            glm::vec3 n = asset.hasNormals ? getNorm(idx[i]) : faceNormal;
            glm::vec2 uv = asset.hasUVs ? getUV(idx[i]) : glm::vec2(0.0f);

            triPositions.push_back(pos);
            vertices.push_back(pos.x);
            vertices.push_back(pos.y);
            vertices.push_back(pos.z);
            vertices.push_back(n.x);
            vertices.push_back(n.y);
            vertices.push_back(n.z);
            vertices.push_back(uv.x);
            vertices.push_back(uv.y);
        }
    }

    if (vertices.empty()) {
        errorMsg = "No vertices generated for GPU upload";
        return false;
    }

    OBJLoader::LoadedMesh& loaded = loadedMeshes[meshIndex];
    loaded.mesh = std::make_unique<Mesh>(vertices.data(), vertices.size() * sizeof(float));
    loaded.vertexCount = static_cast<int>(vertices.size() / 8);
    loaded.faceCount = static_cast<int>(asset.faces.size());
    loaded.hasNormals = asset.hasNormals;
    loaded.hasTexCoords = asset.hasUVs;
    loaded.boundsMin = asset.boundsMin;
    loaded.boundsMax = asset.boundsMax;
    loaded.triangleVertices = std::move(triPositions);
    loaded.positions = asset.positions;
    loaded.triangleIndices.clear();
    loaded.triangleIndices.reserve(asset.faces.size() * 3);
    for (const auto& face : asset.faces) {
        loaded.triangleIndices.push_back(face.x);
        loaded.triangleIndices.push_back(face.y);
        loaded.triangleIndices.push_back(face.z);
    }

    return true;
}

int ModelLoader::addRawMesh(const RawMeshAsset& asset, const std::string& sourcePath,
                            const std::string& name, std::string& errorMsg) {
    if (asset.positions.empty() || asset.faces.empty()) {
        errorMsg = "Raw mesh is empty";
        return -1;
    }

    std::vector<float> vertices;
    vertices.reserve(asset.faces.size() * 3 * 8);
    std::vector<glm::vec3> triPositions;
    triPositions.reserve(asset.faces.size() * 3);

    auto getPos = [&](uint32_t idx) -> const glm::vec3& { return asset.positions[idx]; };
    auto getNorm = [&](uint32_t idx) -> glm::vec3 {
        if (idx < asset.normals.size()) return asset.normals[idx];
        return glm::vec3(0.0f);
    };
    auto getUV = [&](uint32_t idx) -> glm::vec2 {
        if (idx < asset.uvs.size()) return asset.uvs[idx];
        return glm::vec2(0.0f);
    };

    for (const auto& face : asset.faces) {
        const uint32_t idx[3] = { face.x, face.y, face.z };
        glm::vec3 faceNormal(0.0f);
        if (!asset.hasNormals) {
            const glm::vec3& a = getPos(idx[0]);
            const glm::vec3& b = getPos(idx[1]);
            const glm::vec3& c = getPos(idx[2]);
            faceNormal = glm::normalize(glm::cross(b - a, c - a));
        }
        for (int i = 0; i < 3; i++) {
            glm::vec3 pos = getPos(idx[i]);
            glm::vec3 n = asset.hasNormals ? getNorm(idx[i]) : faceNormal;
            glm::vec2 uv = asset.hasUVs ? getUV(idx[i]) : glm::vec2(0.0f);

            triPositions.push_back(pos);
            vertices.push_back(pos.x);
            vertices.push_back(pos.y);
            vertices.push_back(pos.z);
            vertices.push_back(n.x);
            vertices.push_back(n.y);
            vertices.push_back(n.z);
            vertices.push_back(uv.x);
            vertices.push_back(uv.y);
        }
    }

    if (vertices.empty()) {
        errorMsg = "No vertices generated for GPU upload";
        return -1;
    }

    OBJLoader::LoadedMesh loaded;
    loaded.path = sourcePath;
    loaded.name = name.empty() ? "StaticBatch" : name;
    loaded.mesh = std::make_unique<Mesh>(vertices.data(), vertices.size() * sizeof(float));
    loaded.vertexCount = static_cast<int>(vertices.size() / 8);
    loaded.faceCount = static_cast<int>(asset.faces.size());
    loaded.hasNormals = asset.hasNormals;
    loaded.hasTexCoords = asset.hasUVs;
    loaded.boundsMin = asset.boundsMin;
    loaded.boundsMax = asset.boundsMax;
    loaded.triangleVertices = std::move(triPositions);
    loaded.positions = asset.positions;
    loaded.triangleIndices.clear();
    loaded.triangleIndices.reserve(asset.faces.size() * 3);
    for (const auto& face : asset.faces) {
        loaded.triangleIndices.push_back(face.x);
        loaded.triangleIndices.push_back(face.y);
        loaded.triangleIndices.push_back(face.z);
    }

    int newIndex = static_cast<int>(loadedMeshes.size());
    loadedMeshes.push_back(std::move(loaded));
    return newIndex;
}

static glm::mat4 aiToGlm(const aiMatrix4x4& m) {
    return glm::mat4(
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4
    );
}

static glm::vec3 quatToEulerDegrees(const aiQuaternion& q) {
    glm::quat gq(q.w, q.x, q.y, q.z);
    glm::vec3 euler = glm::degrees(glm::eulerAngles(gq));
    return euler;
}

static bool buildSceneMeshes(const std::string& filepath, const aiScene* scene,
                             std::vector<OBJLoader::LoadedMesh>& loadedMeshes,
                             ModelSceneData& out, std::string& errorMsg) {
    out.meshIndices.assign(scene->mNumMeshes, -1);
    out.meshMaterialIndices.assign(scene->mNumMeshes, -1);

    out.materials.clear();
    out.materials.reserve(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial* mat = scene->mMaterials[i];
        ModelMaterialInfo info;
        info.name = mat->GetName().C_Str();
        if (info.name.empty()) {
            info.name = "Material_" + std::to_string(i);
        }

        aiColor3D diffuse(1.0f, 1.0f, 1.0f);
        if (AI_SUCCESS == mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse)) {
            info.props.color = glm::vec3(diffuse.r, diffuse.g, diffuse.b);
        }

        aiColor3D specular(0.0f, 0.0f, 0.0f);
        if (AI_SUCCESS == mat->Get(AI_MATKEY_COLOR_SPECULAR, specular)) {
            float avg = (specular.r + specular.g + specular.b) / 3.0f;
            info.props.specularStrength = avg;
        }

        float shininess = info.props.shininess;
        if (AI_SUCCESS == mat->Get(AI_MATKEY_SHININESS, shininess)) {
            info.props.shininess = shininess;
        }

        aiString tex;
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_DIFFUSE, 0, &tex)) {
            info.albedoPath = tex.C_Str();
        }
        if (AI_SUCCESS == mat->GetTexture(aiTextureType_NORMALS, 0, &tex)) {
            info.normalPath = tex.C_Str();
        } else if (AI_SUCCESS == mat->GetTexture(aiTextureType_HEIGHT, 0, &tex)) {
            info.normalPath = tex.C_Str();
        }

        if (!info.albedoPath.empty()) {
            info.props.textureMix = 1.0f;
        }

        out.materials.push_back(info);
    }

    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        aiMesh* mesh = scene->mMeshes[i];
        if (!mesh || mesh->mNumVertices == 0 || mesh->mNumFaces == 0) {
            continue;
        }

        std::vector<float> vertices;
        struct BoneVertex {
            int ids[4];
            float weights[4];
        };
        std::vector<BoneVertex> boneVertices;
        std::vector<glm::ivec4> vertexBoneIds(mesh->mNumVertices, glm::ivec4(0));
        std::vector<glm::vec4> vertexBoneWeights(mesh->mNumVertices, glm::vec4(0.0f));
        std::vector<glm::vec3> triPositions;
        std::vector<glm::vec3> positions;
        std::vector<uint32_t> triangleIndices;
        vertices.reserve(mesh->mNumFaces * 3 * 8);
        boneVertices.reserve(mesh->mNumFaces * 3);
        triPositions.reserve(mesh->mNumFaces * 3);
        positions.reserve(mesh->mNumVertices);
        triangleIndices.reserve(mesh->mNumFaces * 3);

        glm::vec3 boundsMin(FLT_MAX);
        glm::vec3 boundsMax(-FLT_MAX);

        std::vector<std::string> boneNames;
        std::vector<glm::mat4> inverseBindMatrices;
        if (mesh->mNumBones > 0) {
            boneNames.reserve(mesh->mNumBones);
            inverseBindMatrices.reserve(mesh->mNumBones);
            for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
                aiBone* bone = mesh->mBones[b];
                int boneIndex = static_cast<int>(boneNames.size());
                boneNames.push_back(bone->mName.C_Str());
                inverseBindMatrices.push_back(aiToGlm(bone->mOffsetMatrix));

                for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
                    unsigned int vId = bone->mWeights[w].mVertexId;
                    float weight = bone->mWeights[w].mWeight;
                    if (vId >= vertexBoneWeights.size()) continue;

                    glm::vec4& weights = vertexBoneWeights[vId];
                    glm::ivec4& ids = vertexBoneIds[vId];
                    int replaceIndex = -1;
                    float minWeight = weight;
                    for (int k = 0; k < 4; ++k) {
                        if (weights[k] == 0.0f) {
                            replaceIndex = k;
                            break;
                        }
                        if (weights[k] < minWeight) {
                            minWeight = weights[k];
                            replaceIndex = k;
                        }
                    }
                    if (replaceIndex >= 0) {
                        weights[replaceIndex] = weight;
                        ids[replaceIndex] = boneIndex;
                    }
                }
            }
        }

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            glm::vec3 pos(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
            positions.push_back(pos);
            boundsMin.x = std::min(boundsMin.x, pos.x);
            boundsMin.y = std::min(boundsMin.y, pos.y);
            boundsMin.z = std::min(boundsMin.z, pos.z);
            boundsMax.x = std::max(boundsMax.x, pos.x);
            boundsMax.y = std::max(boundsMax.y, pos.y);
            boundsMax.z = std::max(boundsMax.z, pos.z);
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue;

            triangleIndices.push_back(static_cast<uint32_t>(face.mIndices[0]));
            triangleIndices.push_back(static_cast<uint32_t>(face.mIndices[1]));
            triangleIndices.push_back(static_cast<uint32_t>(face.mIndices[2]));

            for (unsigned int j = 0; j < 3; ++j) {
                unsigned int index = face.mIndices[j];
                glm::vec3 pos(mesh->mVertices[index].x,
                              mesh->mVertices[index].y,
                              mesh->mVertices[index].z);
                vertices.push_back(pos.x);
                vertices.push_back(pos.y);
                vertices.push_back(pos.z);
                triPositions.push_back(pos);

                if (mesh->mNormals) {
                    glm::vec3 n(mesh->mNormals[index].x,
                                mesh->mNormals[index].y,
                                mesh->mNormals[index].z);
                    vertices.push_back(n.x);
                    vertices.push_back(n.y);
                    vertices.push_back(n.z);
                } else {
                    vertices.push_back(0.0f);
                    vertices.push_back(1.0f);
                    vertices.push_back(0.0f);
                }

                if (mesh->mTextureCoords[0]) {
                    vertices.push_back(mesh->mTextureCoords[0][index].x);
                    vertices.push_back(mesh->mTextureCoords[0][index].y);
                } else {
                    vertices.push_back(0.0f);
                    vertices.push_back(0.0f);
                }

                BoneVertex bv{};
                glm::ivec4 ids = vertexBoneIds[index];
                glm::vec4 weights = vertexBoneWeights[index];
                float weightSum = weights.x + weights.y + weights.z + weights.w;
                if (weightSum > 0.0f) {
                    weights /= weightSum;
                }
                bv.ids[0] = ids.x;
                bv.ids[1] = ids.y;
                bv.ids[2] = ids.z;
                bv.ids[3] = ids.w;
                bv.weights[0] = weights.x;
                bv.weights[1] = weights.y;
                bv.weights[2] = weights.z;
                bv.weights[3] = weights.w;
                boneVertices.push_back(bv);
            }
        }

        if (vertices.empty()) {
            continue;
        }

        OBJLoader::LoadedMesh loaded;
        loaded.path = filepath;
        loaded.name = mesh->mName.C_Str();
        if (loaded.name.empty()) {
            loaded.name = fs::path(filepath).stem().string() + "_mesh" + std::to_string(i);
        }
        bool isSkinned = mesh->mNumBones > 0 && boneVertices.size() == vertices.size() / 8;
        if (isSkinned) {
            loaded.mesh = std::make_unique<Mesh>(vertices.data(), vertices.size() * sizeof(float), true,
                                                 boneVertices.data(), boneVertices.size() * sizeof(BoneVertex));
        } else {
            loaded.mesh = std::make_unique<Mesh>(vertices.data(), vertices.size() * sizeof(float));
        }
        loaded.vertexCount = static_cast<int>(vertices.size() / 8);
        loaded.faceCount = static_cast<int>(mesh->mNumFaces);
        loaded.hasNormals = mesh->mNormals != nullptr;
        loaded.hasTexCoords = mesh->mTextureCoords[0] != nullptr;
        loaded.boundsMin = boundsMin;
        loaded.boundsMax = boundsMax;
        loaded.triangleVertices = std::move(triPositions);
        loaded.positions = std::move(positions);
        loaded.triangleIndices = std::move(triangleIndices);
        loaded.isSkinned = isSkinned;
        loaded.boneNames = std::move(boneNames);
        loaded.inverseBindMatrices = std::move(inverseBindMatrices);
        loaded.baseVertices = vertices;
        if (isSkinned) {
            loaded.boneIds.reserve(boneVertices.size());
            loaded.boneWeights.reserve(boneVertices.size());
            for (const auto& bv : boneVertices) {
                loaded.boneIds.emplace_back(bv.ids[0], bv.ids[1], bv.ids[2], bv.ids[3]);
                loaded.boneWeights.emplace_back(bv.weights[0], bv.weights[1], bv.weights[2], bv.weights[3]);
            }
        }

        out.meshMaterialIndices[i] = mesh->mMaterialIndex < (int)out.materials.size()
            ? static_cast<int>(mesh->mMaterialIndex)
            : -1;

        out.meshIndices[i] = static_cast<int>(loadedMeshes.size());
        loadedMeshes.push_back(std::move(loaded));
    }

    bool anyMesh = false;
    for (int idx : out.meshIndices) {
        if (idx >= 0) { anyMesh = true; break; }
    }
    if (!anyMesh) {
        errorMsg = "No meshes found in model file";
        return false;
    }

    return true;
}

static void buildSceneNodes(const aiScene* scene,
                            const std::vector<int>& meshIndices,
                            ModelSceneData& out) {
    std::unordered_set<std::string> boneNames;
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        aiMesh* mesh = scene->mMeshes[i];
        for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
            boneNames.insert(mesh->mBones[b]->mName.C_Str());
        }
    }

    std::function<void(aiNode*, int)> walk = [&](aiNode* node, int parentIndex) {
        ModelNodeInfo info;
        info.name = node->mName.C_Str();
        if (info.name.empty()) {
            info.name = "Node_" + std::to_string(out.nodes.size());
        }
        info.parentIndex = parentIndex;
        info.isBone = boneNames.find(info.name) != boneNames.end();

        aiVector3D scaling(1.0f, 1.0f, 1.0f);
        aiVector3D position(0.0f, 0.0f, 0.0f);
        aiQuaternion rotation;
        node->mTransformation.Decompose(scaling, rotation, position);

        info.localPosition = glm::vec3(position.x, position.y, position.z);
        info.localScale = glm::vec3(scaling.x, scaling.y, scaling.z);
        info.localRotation = quatToEulerDegrees(rotation);

        for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
            unsigned int meshIndex = node->mMeshes[i];
            if (meshIndex < meshIndices.size()) {
                info.meshIndices.push_back(static_cast<int>(meshIndex));
            }
        }

        int thisIndex = static_cast<int>(out.nodes.size());
        out.nodes.push_back(info);

        for (unsigned int c = 0; c < node->mNumChildren; ++c) {
            walk(node->mChildren[c], thisIndex);
        }
    };

    out.nodes.clear();
    if (scene->mRootNode) {
        walk(scene->mRootNode, -1);
    }
}

static void collectRawMeshData(aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform, RawMeshAsset& out) {
    aiMatrix4x4 current = parentTransform * node->mTransformation;
    glm::mat4 gTransform = aiToGlm(current);
    glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(gTransform)));

    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        size_t baseIndex = out.positions.size();

        // Vertices
        for (unsigned int v = 0; v < mesh->mNumVertices; v++) {
            glm::vec3 pos(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
            glm::vec4 transformed = gTransform * glm::vec4(pos, 1.0f);
            glm::vec3 finalPos = glm::vec3(transformed) / (transformed.w == 0.0f ? 1.0f : transformed.w);
            out.positions.push_back(finalPos);

            out.boundsMin.x = std::min(out.boundsMin.x, finalPos.x);
            out.boundsMin.y = std::min(out.boundsMin.y, finalPos.y);
            out.boundsMin.z = std::min(out.boundsMin.z, finalPos.z);
            out.boundsMax.x = std::max(out.boundsMax.x, finalPos.x);
            out.boundsMax.y = std::max(out.boundsMax.y, finalPos.y);
            out.boundsMax.z = std::max(out.boundsMax.z, finalPos.z);

            glm::vec3 normal(0.0f);
            if (mesh->mNormals) {
                normal = glm::normalize(normalMat * glm::vec3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z));
            }
            out.normals.push_back(normal);

            glm::vec2 uv(0.0f);
            if (mesh->mTextureCoords[0]) {
                uv.x = mesh->mTextureCoords[0][v].x;
                uv.y = mesh->mTextureCoords[0][v].y;
            }
            out.uvs.push_back(uv);
        }

        // Faces (triangles)
        for (unsigned int f = 0; f < mesh->mNumFaces; f++) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue;
            glm::u32vec3 tri(
                static_cast<uint32_t>(baseIndex + face.mIndices[0]),
                static_cast<uint32_t>(baseIndex + face.mIndices[1]),
                static_cast<uint32_t>(baseIndex + face.mIndices[2])
            );
            out.faces.push_back(tri);
        }
    }

    for (unsigned int c = 0; c < node->mNumChildren; c++) {
        collectRawMeshData(node->mChildren[c], scene, current, out);
    }
}

bool ModelLoader::buildRawMeshFromScene(const std::string& filepath, RawMeshAsset& out, std::string& errorMsg,
                                        glm::vec3* outRootPos, glm::vec3* outRootRot, glm::vec3* outRootScale) {
    out = RawMeshAsset();

    fs::path inPath(filepath);
    if (!fs::exists(inPath)) {
        errorMsg = "File not found: " + filepath;
        return false;
    }
    if (!isSupported(filepath)) {
        errorMsg = "Unsupported file format for raw mesh build";
        return false;
    }

    Assimp::Importer localImporter;
    unsigned int importFlags =
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals |
        aiProcess_FlipUVs;

    const aiScene* scene = localImporter.ReadFile(filepath, importFlags);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        errorMsg = "Assimp error: " + std::string(localImporter.GetErrorString());
        return false;
    }

    aiMatrix4x4 parent;
    parent = aiMatrix4x4();
    if (scene->mRootNode && (outRootPos || outRootRot || outRootScale)) {
        aiVector3D scaling(1.0f, 1.0f, 1.0f);
        aiVector3D position(0.0f, 0.0f, 0.0f);
        aiQuaternion rotation;
        scene->mRootNode->mTransformation.Decompose(scaling, rotation, position);
        if (outRootPos) *outRootPos = glm::vec3(position.x, position.y, position.z);
        if (outRootScale) *outRootScale = glm::vec3(scaling.x, scaling.y, scaling.z);
        if (outRootRot) *outRootRot = quatToEulerDegrees(rotation);

        aiMatrix4x4 rootTransform = scene->mRootNode->mTransformation;
        rootTransform.Inverse();
        parent = rootTransform;
    }

    collectRawMeshData(scene->mRootNode, scene, parent, out);

    if (out.positions.empty() || out.faces.empty()) {
        errorMsg = "No geometry found to build raw mesh";
        return false;
    }

    out.hasNormals = false;
    for (const auto& n : out.normals) {
        if (glm::length(n) > 1e-4f) { out.hasNormals = true; break; }
    }

    out.hasUVs = false;
    for (const auto& uv : out.uvs) {
        if (std::abs(uv.x) > 1e-6f || std::abs(uv.y) > 1e-6f) { out.hasUVs = true; break; }
    }

    if (!out.hasNormals) {
        out.normals.assign(out.positions.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> accum(out.positions.size(), glm::vec3(0.0f));
        for (const auto& face : out.faces) {
            const glm::vec3& a = out.positions[face.x];
            const glm::vec3& b = out.positions[face.y];
            const glm::vec3& c = out.positions[face.z];
            glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
            accum[face.x] += n;
            accum[face.y] += n;
            accum[face.z] += n;
        }
        for (size_t i = 0; i < accum.size(); i++) {
            if (glm::length(accum[i]) > 1e-6f) {
                out.normals[i] = glm::normalize(accum[i]);
            }
        }
        out.hasNormals = true;
    }

    return true;
}

void ModelLoader::processNode(aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform,
                              std::vector<float>& vertices, std::vector<glm::vec3>& triPositions,
                              std::vector<glm::vec3>& positions, std::vector<uint32_t>& indices,
                              glm::vec3& boundsMin, glm::vec3& boundsMax) {
    aiMatrix4x4 currentTransform = parentTransform * node->mTransformation;
    // Process all meshes in this node
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        processMesh(mesh, currentTransform, vertices, triPositions, positions, indices, boundsMin, boundsMax);
    }
    
    // Process children nodes
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i], scene, currentTransform, vertices, triPositions, positions, indices, boundsMin, boundsMax);
    }
}

void ModelLoader::processMesh(aiMesh* mesh, const aiMatrix4x4& transform,
                              std::vector<float>& vertices, std::vector<glm::vec3>& triPositions,
                              std::vector<glm::vec3>& positions, std::vector<uint32_t>& indices,
                              glm::vec3& boundsMin, glm::vec3& boundsMax) {
    glm::mat4 gTransform = aiToGlm(transform);
    glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(gTransform)));

    size_t baseIndex = positions.size();
    positions.reserve(baseIndex + mesh->mNumVertices);
    for (unsigned int v = 0; v < mesh->mNumVertices; v++) {
        glm::vec3 pos(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
        glm::vec4 transformed = gTransform * glm::vec4(pos, 1.0f);
        glm::vec3 finalPos = glm::vec3(transformed) / (transformed.w == 0.0f ? 1.0f : transformed.w);
        positions.push_back(finalPos);

        boundsMin.x = std::min(boundsMin.x, finalPos.x);
        boundsMin.y = std::min(boundsMin.y, finalPos.y);
        boundsMin.z = std::min(boundsMin.z, finalPos.z);
        boundsMax.x = std::max(boundsMax.x, finalPos.x);
        boundsMax.y = std::max(boundsMax.y, finalPos.y);
        boundsMax.z = std::max(boundsMax.z, finalPos.z);
    }

    // Process each face
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        if (face.mNumIndices == 3) {
            indices.push_back(static_cast<uint32_t>(baseIndex + face.mIndices[0]));
            indices.push_back(static_cast<uint32_t>(baseIndex + face.mIndices[1]));
            indices.push_back(static_cast<uint32_t>(baseIndex + face.mIndices[2]));
        }
        
        // Process each vertex of the face
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            unsigned int index = face.mIndices[j];
            
            glm::vec3 pos(mesh->mVertices[index].x,
                          mesh->mVertices[index].y,
                          mesh->mVertices[index].z);
            glm::vec4 transformed = gTransform * glm::vec4(pos, 1.0f);
            glm::vec3 finalPos = glm::vec3(transformed) / (transformed.w == 0.0f ? 1.0f : transformed.w);

            vertices.push_back(finalPos.x);
            vertices.push_back(finalPos.y);
            vertices.push_back(finalPos.z);

            triPositions.push_back(finalPos);

            // Normal
            if (mesh->mNormals) {
                glm::vec3 n(mesh->mNormals[index].x,
                            mesh->mNormals[index].y,
                            mesh->mNormals[index].z);
                n = glm::normalize(normalMat * n);
                vertices.push_back(n.x);
                vertices.push_back(n.y);
                vertices.push_back(n.z);
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
    cachedScenes.clear();
}

size_t ModelLoader::getMeshCount() const {
    return loadedMeshes.size();
}
