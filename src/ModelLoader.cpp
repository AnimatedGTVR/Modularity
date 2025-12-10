#include "ModelLoader.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstring>

ModelLoader& ModelLoader::getInstance() {
    static ModelLoader instance;
    return instance;
}

static void collectRawMeshData(aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform, RawMeshAsset& out);

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

    // Process all meshes in the scene
    std::vector<float> vertices;
    result.meshCount = scene->mNumMeshes;
    result.hasNormals = true;
    result.hasTexCoords = false;
    result.hasTangents = false;
    
    // Process the root node recursively
    processNode(scene->mRootNode, scene, aiMatrix4x4(), vertices, triPositions, boundsMin, boundsMax);
    
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

    loadedMeshes.push_back(std::move(loaded));
    
    result.success = true;
    result.meshIndex = static_cast<int>(loadedMeshes.size() - 1);
    
    std::cerr << "[ModelLoader] Loaded " << filepath << " with " 
              << result.vertexCount << " vertices, " 
              << result.faceCount << " faces, "
              << result.meshCount << " meshes" << std::endl;
    
    return result;
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

    in.read(reinterpret_cast<char*>(&out.boundsMin.x), sizeof(float) * 3);
    in.read(reinterpret_cast<char*>(&out.boundsMax.x), sizeof(float) * 3);

    out.positions.resize(header.vertexCount);
    out.normals.resize(header.vertexCount);
    out.uvs.resize(header.vertexCount);
    out.faces.resize(header.faceCount);

    in.read(reinterpret_cast<char*>(out.positions.data()), sizeof(glm::vec3) * out.positions.size());
    in.read(reinterpret_cast<char*>(out.normals.data()), sizeof(glm::vec3) * out.normals.size());
    in.read(reinterpret_cast<char*>(out.uvs.data()), sizeof(glm::vec2) * out.uvs.size());
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
    out.write(reinterpret_cast<const char*>(asset.normals.data()), sizeof(glm::vec3) * asset.normals.size());
    out.write(reinterpret_cast<const char*>(asset.uvs.data()), sizeof(glm::vec2) * asset.uvs.size());
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

    return true;
}

static glm::mat4 aiToGlm(const aiMatrix4x4& m) {
    return glm::mat4(
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4
    );
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

void ModelLoader::processNode(aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform, std::vector<float>& vertices, std::vector<glm::vec3>& triPositions, glm::vec3& boundsMin, glm::vec3& boundsMax) {
    aiMatrix4x4 currentTransform = parentTransform * node->mTransformation;
    // Process all meshes in this node
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        processMesh(mesh, currentTransform, vertices, triPositions, boundsMin, boundsMax);
    }
    
    // Process children nodes
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i], scene, currentTransform, vertices, triPositions, boundsMin, boundsMax);
    }
}

void ModelLoader::processMesh(aiMesh* mesh, const aiMatrix4x4& transform, std::vector<float>& vertices, std::vector<glm::vec3>& triPositions, glm::vec3& boundsMin, glm::vec3& boundsMax) {
    glm::mat4 gTransform = aiToGlm(transform);
    glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(gTransform)));

    // Process each face
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        
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

            boundsMin.x = std::min(boundsMin.x, finalPos.x);
            boundsMin.y = std::min(boundsMin.y, finalPos.y);
            boundsMin.z = std::min(boundsMin.z, finalPos.z);
            boundsMax.x = std::max(boundsMax.x, finalPos.x);
            boundsMax.y = std::max(boundsMax.y, finalPos.y);
            boundsMax.z = std::max(boundsMax.z, finalPos.z);
            
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
}

size_t ModelLoader::getMeshCount() const {
    return loadedMeshes.size();
}
