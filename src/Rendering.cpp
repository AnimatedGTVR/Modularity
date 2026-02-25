#include "Rendering.h"
#include "Camera.h"
#include "ModelLoader.h"
#include <array>
#include <unordered_map>
#include <unordered_set>

#define TINYOBJLOADER_IMPLEMENTATION
#include "../include/ThirdParty/tiny_obj_loader.h"

// Global OBJ loader instance
OBJLoader g_objLoader;

// Cube vertex data
float vertices[] = {
    // Back face (z = -0.5f)
     0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 0.0f,
    -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f,

    // Front face (z = 0.5f)
    -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f,

    // Left face (x = -0.5f)
    -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
    -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
    -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,

    // Right face (x = 0.5f)
     0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
     0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 0.0f,

    // Bottom face (y = -0.5f)
    -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 0.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f,

    // Top face (y = 0.5f)
    -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 1.0f
};

float mirrorPlaneVertices[] = {
    // positions          // normals        // texcoords
    -0.5f, -0.5f, 0.0f,    0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
     0.5f, -0.5f, 0.0f,    0.0f, 0.0f, 1.0f,  1.0f, 0.0f,
     0.5f,  0.5f, 0.0f,    0.0f, 0.0f, 1.0f,  1.0f, 1.0f,

    -0.5f, -0.5f, 0.0f,    0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
     0.5f,  0.5f, 0.0f,    0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
    -0.5f,  0.5f, 0.0f,    0.0f, 0.0f, 1.0f,  0.0f, 1.0f
};

std::vector<float> generateSphere(int segments, int rings) {
    std::vector<float> vertices;

    for (int ring = 0; ring <= rings; ring++) {
        float theta = ring * PI / rings;
        float sinTheta = sin(theta);
        float cosTheta = cos(theta);

        for (int seg = 0; seg <= segments; seg++) {
            float phi = seg * 2.0f * PI / segments;
            float sinPhi = sin(phi);
            float cosPhi = cos(phi);

            float x = cosPhi * sinTheta;
            float y = cosTheta;
            float z = sinPhi * sinTheta;

            // Position
            vertices.push_back(x * 0.5f);
            vertices.push_back(y * 0.5f);
            vertices.push_back(z * 0.5f);

            // Normal (same as position for unit sphere)
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);

            // Texcoord
            vertices.push_back((float)seg / segments);
            vertices.push_back((float)ring / rings);
        }
    }

    std::vector<float> triangulated;
    int stride = segments + 1;
    for (int ring = 0; ring < rings; ring++) {
        for (int seg = 0; seg < segments; seg++) {
            int current = ring * stride + seg;
            int next = current + stride;

            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[current * 8 + i]);
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[next * 8 + i]);
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[(current + 1) * 8 + i]);

            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[(current + 1) * 8 + i]);
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[next * 8 + i]);
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[(next + 1) * 8 + i]);
        }
    }

    return triangulated;
}

std::vector<float> generateCapsule(int segments, int rings) {
    std::vector<float> vertices;
    float cylinderHeight = 0.5f;
    float radius = 0.25f;

    // Top hemisphere
    for (int ring = 0; ring <= rings / 2; ring++) {
        float theta = ring * PI / rings;
        float sinTheta = sin(theta);
        float cosTheta = cos(theta);

        for (int seg = 0; seg <= segments; seg++) {
            float phi = seg * 2.0f * PI / segments;
            float sinPhi = sin(phi);
            float cosPhi = cos(phi);

            float x = cosPhi * sinTheta * radius;
            float y = cosTheta * radius + cylinderHeight;
            float z = sinPhi * sinTheta * radius;

            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);

            glm::vec3 normal = glm::normalize(glm::vec3(x, y - cylinderHeight, z));
            vertices.push_back(normal.x);
            vertices.push_back(normal.y);
            vertices.push_back(normal.z);

            vertices.push_back((float)seg / segments);
            vertices.push_back((float)ring / (rings / 2));
        }
    }

    // Cylinder body
    for (int i = 0; i <= 1; i++) {
        float y = i == 0 ? cylinderHeight : -cylinderHeight;
        for (int seg = 0; seg <= segments; seg++) {
            float phi = seg * 2.0f * PI / segments;
            float x = cos(phi) * radius;
            float z = sin(phi) * radius;

            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);

            glm::vec3 normal = glm::normalize(glm::vec3(x, 0.0f, z));
            vertices.push_back(normal.x);
            vertices.push_back(normal.y);
            vertices.push_back(normal.z);

            vertices.push_back((float)seg / segments);
            vertices.push_back(0.5f);
        }
    }

    // Bottom hemisphere
    for (int ring = rings / 2; ring <= rings; ring++) {
        float theta = ring * PI / rings;
        float sinTheta = sin(theta);
        float cosTheta = cos(theta);

        for (int seg = 0; seg <= segments; seg++) {
            float phi = seg * 2.0f * PI / segments;
            float sinPhi = sin(phi);
            float cosPhi = cos(phi);

            float x = cosPhi * sinTheta * radius;
            float y = cosTheta * radius - cylinderHeight;
            float z = sinPhi * sinTheta * radius;

            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);

            glm::vec3 normal = glm::normalize(glm::vec3(x, y + cylinderHeight, z));
            vertices.push_back(normal.x);
            vertices.push_back(normal.y);
            vertices.push_back(normal.z);

            vertices.push_back((float)seg / segments);
            vertices.push_back((float)ring / rings);
        }
    }

    std::vector<float> triangulated;
    int stride = segments + 1;
    int totalRings = rings + 3;

    for (int ring = 0; ring < totalRings - 1; ring++) {
        for (int seg = 0; seg < segments; seg++) {
            int current = ring * stride + seg;
            int next = current + stride;

            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[current * 8 + i]);
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[next * 8 + i]);
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[(current + 1) * 8 + i]);

            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[(current + 1) * 8 + i]);
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[next * 8 + i]);
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[(next + 1) * 8 + i]);
        }
    }

    return triangulated;
}

std::vector<float> generateTorus(int segments, int sides) {
    std::vector<float> vertices;
    float majorRadius = 0.35f;
    float minorRadius = 0.15f;

    for (int seg = 0; seg <= segments; ++seg) {
        float u = seg * 2.0f * PI / segments;
        float cosU = cos(u);
        float sinU = sin(u);

        for (int side = 0; side <= sides; ++side) {
            float v = side * 2.0f * PI / sides;
            float cosV = cos(v);
            float sinV = sin(v);

            float x = (majorRadius + minorRadius * cosV) * cosU;
            float y = minorRadius * sinV;
            float z = (majorRadius + minorRadius * cosV) * sinU;

            glm::vec3 normal = glm::normalize(glm::vec3(cosU * cosV, sinV, sinU * cosV));

            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            vertices.push_back(normal.x);
            vertices.push_back(normal.y);
            vertices.push_back(normal.z);
            vertices.push_back((float)seg / segments);
            vertices.push_back((float)side / sides);
        }
    }

    std::vector<float> triangulated;
    int stride = sides + 1;
    for (int seg = 0; seg < segments; ++seg) {
        for (int side = 0; side < sides; ++side) {
            int current = seg * stride + side;
            int next = current + stride;

            for (int i = 0; i < 8; ++i) triangulated.push_back(vertices[current * 8 + i]);
            for (int i = 0; i < 8; ++i) triangulated.push_back(vertices[next * 8 + i]);
            for (int i = 0; i < 8; ++i) triangulated.push_back(vertices[(current + 1) * 8 + i]);

            for (int i = 0; i < 8; ++i) triangulated.push_back(vertices[(current + 1) * 8 + i]);
            for (int i = 0; i < 8; ++i) triangulated.push_back(vertices[next * 8 + i]);
            for (int i = 0; i < 8; ++i) triangulated.push_back(vertices[(next + 1) * 8 + i]);
        }
    }

    return triangulated;
}

// Mesh implementation
Mesh::Mesh(const float* vertexData, size_t dataSizeBytes) {
    vertexCount = dataSizeBytes / (8 * sizeof(float));
    strideFloats = 8;

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, dataSizeBytes, vertexData, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

Mesh::Mesh(const float* vertexData, size_t dataSizeBytes, bool dynamicUsage,
           const void* boneData, size_t boneDataBytes) {
    vertexCount = dataSizeBytes / (8 * sizeof(float));
    strideFloats = 8;
    dynamic = dynamicUsage;
    hasBones = boneData && boneDataBytes > 0;

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, dataSizeBytes, vertexData, dynamicUsage ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, strideFloats * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, strideFloats * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, strideFloats * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    if (hasBones) {
        glGenBuffers(1, &boneVBO);
        glBindBuffer(GL_ARRAY_BUFFER, boneVBO);
        glBufferData(GL_ARRAY_BUFFER, boneDataBytes, boneData, GL_STATIC_DRAW);

        glVertexAttribIPointer(3, 4, GL_INT, sizeof(int) * 4 + sizeof(float) * 4, (void*)0);
        glEnableVertexAttribArray(3);

        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(int) * 4 + sizeof(float) * 4,
                              (void*)(sizeof(int) * 4));
        glEnableVertexAttribArray(4);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

Mesh::~Mesh() {
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    if (boneVBO) {
        glDeleteBuffers(1, &boneVBO);
    }
}

void Mesh::draw() const {
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glBindVertexArray(0);
}

void Mesh::updateVertices(const float* vertexData, size_t dataSizeBytes) {
    if (!dynamic) return;
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, dataSizeBytes, vertexData);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    vertexCount = dataSizeBytes / (strideFloats * sizeof(float));
}

static void applyCpuSkinning(OBJLoader::LoadedMesh& meshInfo, const std::vector<glm::mat4>& bones, int maxBones) {
    if (!meshInfo.mesh || !meshInfo.isSkinned) return;
    if (meshInfo.baseVertices.empty() || meshInfo.boneIds.empty() || meshInfo.boneWeights.empty()) return;
    if (!meshInfo.mesh->isDynamic()) return;

    size_t vertexCount = meshInfo.baseVertices.size() / 8;
    if (vertexCount == 0 || meshInfo.boneIds.size() != vertexCount || meshInfo.boneWeights.size() != vertexCount) {
        return;
    }

    std::vector<float> skinned = meshInfo.baseVertices;
    int boneLimit = std::min<int>(static_cast<int>(bones.size()), maxBones);
    for (size_t i = 0; i < vertexCount; ++i) {
        glm::vec3 basePos(skinned[i * 8 + 0], skinned[i * 8 + 1], skinned[i * 8 + 2]);
        glm::vec3 baseNorm(skinned[i * 8 + 3], skinned[i * 8 + 4], skinned[i * 8 + 5]);
        glm::ivec4 ids = meshInfo.boneIds[i];
        glm::vec4 weights = meshInfo.boneWeights[i];

        glm::vec4 skinnedPos(0.0f);
        glm::vec3 skinnedNorm(0.0f);
        for (int k = 0; k < 4; ++k) {
            int id = ids[k];
            float w = weights[k];
            if (w <= 0.0f || id < 0 || id >= boneLimit) continue;
            const glm::mat4& m = bones[id];
            skinnedPos += w * (m * glm::vec4(basePos, 1.0f));
            skinnedNorm += w * glm::mat3(m) * baseNorm;
        }
        skinned[i * 8 + 0] = skinnedPos.x;
        skinned[i * 8 + 1] = skinnedPos.y;
        skinned[i * 8 + 2] = skinnedPos.z;
        if (glm::length(skinnedNorm) > 1e-6f) {
            skinnedNorm = glm::normalize(skinnedNorm);
        }
        skinned[i * 8 + 3] = skinnedNorm.x;
        skinned[i * 8 + 4] = skinnedNorm.y;
        skinned[i * 8 + 5] = skinnedNorm.z;
    }

    meshInfo.mesh->updateVertices(skinned.data(), skinned.size() * sizeof(float));
}

// OBJLoader implementation
int OBJLoader::loadOBJ(const std::string& filepath, std::string& errorMsg) {
    // Check if already loaded
    for (size_t i = 0; i < loadedMeshes.size(); i++) {
        if (loadedMeshes[i].path == filepath) {
            return static_cast<int>(i);
        }
    }
    
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;
    
    std::string baseDir = fs::path(filepath).parent_path().string();
    if (!baseDir.empty()) baseDir += "/";
    
    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, 
                                filepath.c_str(), baseDir.c_str());
    
    if (!warn.empty()) {
        errorMsg += "Warning: " + warn + "\n";
    }
    
    if (!err.empty()) {
        errorMsg += "Error: " + err + "\n";
    }
    
    if (!ret || shapes.empty()) {
        errorMsg += "Failed to load OBJ file: " + filepath;
        return -1;
    }
    
    std::vector<float> vertices;
    bool hasNormalsInFile = !attrib.normals.empty();

    int faceCount = 0;
    for (const auto& shape : shapes) {
        faceCount += static_cast<int>(shape.mesh.num_face_vertices.size());
    }

    glm::vec3 boundsMin(FLT_MAX);
    glm::vec3 boundsMax(-FLT_MAX);
    std::vector<glm::vec3> triPositions;
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> triangleIndices;

    positions.reserve(attrib.vertices.size() / 3);
    for (size_t i = 0; i + 2 < attrib.vertices.size(); i += 3) {
        positions.emplace_back(attrib.vertices[i], attrib.vertices[i + 1], attrib.vertices[i + 2]);
    }

    for (const auto& shape : shapes) {
        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int fv = shape.mesh.num_face_vertices[f];

            struct TempVertex {
                glm::vec3 pos;
                glm::vec2 uv;
                glm::vec3 normal;
                bool hasNormal = false;
            };
            std::vector<TempVertex> faceVerts;
            std::vector<int> facePosIndices;
            facePosIndices.reserve(static_cast<size_t>(fv));

            for (int v = 0; v < fv; v++) {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];

                TempVertex tv;
                tv.pos.x = attrib.vertices[3 * size_t(idx.vertex_index) + 0];
                tv.pos.y = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
                tv.pos.z = attrib.vertices[3 * size_t(idx.vertex_index) + 2];

                boundsMin.x = std::min(boundsMin.x, tv.pos.x);
                boundsMin.y = std::min(boundsMin.y, tv.pos.y);
                boundsMin.z = std::min(boundsMin.z, tv.pos.z);
                boundsMax.x = std::max(boundsMax.x, tv.pos.x);
                boundsMax.y = std::max(boundsMax.y, tv.pos.y);
                boundsMax.z = std::max(boundsMax.z, tv.pos.z);

                if (idx.texcoord_index >= 0 && !attrib.texcoords.empty()) {
                    tv.uv.x = attrib.texcoords[2 * size_t(idx.texcoord_index) + 0];
                    tv.uv.y = attrib.texcoords[2 * size_t(idx.texcoord_index) + 1];
                } else {
                    tv.uv = glm::vec2(0.0f);
                }

                if (idx.normal_index >= 0 && hasNormalsInFile) {
                    tv.normal.x = attrib.normals[3 * size_t(idx.normal_index) + 0];
                    tv.normal.y = attrib.normals[3 * size_t(idx.normal_index) + 1];
                    tv.normal.z = attrib.normals[3 * size_t(idx.normal_index) + 2];
                    tv.hasNormal = true;
                }

                faceVerts.push_back(tv);
                facePosIndices.push_back(idx.vertex_index);
            }

            if (!hasNormalsInFile && fv >= 3) {
                glm::vec3 v0 = faceVerts[0].pos;
                glm::vec3 v1 = faceVerts[1].pos;
                glm::vec3 v2 = faceVerts[2].pos;
                glm::vec3 faceNormal = glm::normalize(glm::cross(v1 - v0, v2 - v0));

                for (auto& tv : faceVerts) {
                    tv.normal = faceNormal;
                    tv.hasNormal = true;
                }
            }

            for (int v = 1; v < fv - 1; v++) {
                const TempVertex* tri[3] = { &faceVerts[0], &faceVerts[v], &faceVerts[v+1] };

                int idx0 = facePosIndices[0];
                int idx1 = facePosIndices[v];
                int idx2 = facePosIndices[v + 1];
                if (idx0 >= 0 && idx1 >= 0 && idx2 >= 0) {
                    triangleIndices.push_back(static_cast<uint32_t>(idx0));
                    triangleIndices.push_back(static_cast<uint32_t>(idx1));
                    triangleIndices.push_back(static_cast<uint32_t>(idx2));
                }

                for (int i = 0; i < 3; i++) {
                    triPositions.push_back(tri[i]->pos);
                    vertices.push_back(tri[i]->pos.x);
                    vertices.push_back(tri[i]->pos.y);
                    vertices.push_back(tri[i]->pos.z);
                    vertices.push_back(tri[i]->normal.x);
                    vertices.push_back(tri[i]->normal.y);
                    vertices.push_back(tri[i]->normal.z);
                    vertices.push_back(tri[i]->uv.x);
                    vertices.push_back(tri[i]->uv.y);
                }
            }

            indexOffset += fv;
        }
    }

    if (vertices.empty()) {
        errorMsg += "No vertices found in OBJ file";
        return -1;
    }

    LoadedMesh loaded;
    loaded.path = filepath;
    loaded.name = fs::path(filepath).stem().string();
    loaded.mesh = std::make_unique<Mesh>(vertices.data(), vertices.size() * sizeof(float));
    loaded.vertexCount = static_cast<int>(vertices.size() / 8);
    loaded.faceCount = faceCount;
    loaded.hasNormals = hasNormalsInFile;
    loaded.hasTexCoords = !attrib.texcoords.empty();
    loaded.boundsMin = boundsMin;
    loaded.boundsMax = boundsMax;
    loaded.triangleVertices = std::move(triPositions);
    loaded.positions = std::move(positions);
    loaded.triangleIndices = std::move(triangleIndices);

    loadedMeshes.push_back(std::move(loaded));
    return static_cast<int>(loadedMeshes.size() - 1);
}

Mesh* OBJLoader::getMesh(int index) {
    if (index < 0 || index >= static_cast<int>(loadedMeshes.size())) {
        return nullptr;
    }
    return loadedMeshes[index].mesh.get();
}

const OBJLoader::LoadedMesh* OBJLoader::getMeshInfo(int index) const {
    if (index < 0 || index >= static_cast<int>(loadedMeshes.size())) {
        return nullptr;
    }
    return &loadedMeshes[index];
}

// Renderer implementation
Renderer::~Renderer() {
    shaderCache.clear();
    shader = nullptr;
    defaultShader = nullptr;
    delete texture1;
    delete texture2;
    delete cubeMesh;
    delete sphereMesh;
    delete capsuleMesh;
    delete planeMesh;
    delete torusMesh;
    delete skybox;
    delete postShader;
    delete brightShader;
    delete blurShader;
    delete shadowDepthShader;
    if (previewTarget.fbo) glDeleteFramebuffers(1, &previewTarget.fbo);
    if (previewTarget.texture) glDeleteTextures(1, &previewTarget.texture);
    if (previewTarget.rbo) glDeleteRenderbuffers(1, &previewTarget.rbo);
    for (auto& entry : extraPreviewTargets) {
        releaseRenderTarget(entry.second);
    }
    extraPreviewTargets.clear();
    if (postTarget.fbo) glDeleteFramebuffers(1, &postTarget.fbo);
    if (postTarget.texture) glDeleteTextures(1, &postTarget.texture);
    if (postTarget.rbo) glDeleteRenderbuffers(1, &postTarget.rbo);
    if (historyTarget.fbo) glDeleteFramebuffers(1, &historyTarget.fbo);
    if (historyTarget.texture) glDeleteTextures(1, &historyTarget.texture);
    if (historyTarget.rbo) glDeleteRenderbuffers(1, &historyTarget.rbo);
    if (bloomTargetA.fbo) glDeleteFramebuffers(1, &bloomTargetA.fbo);
    if (bloomTargetA.texture) glDeleteTextures(1, &bloomTargetA.texture);
    if (bloomTargetA.rbo) glDeleteRenderbuffers(1, &bloomTargetA.rbo);
    if (bloomTargetB.fbo) glDeleteFramebuffers(1, &bloomTargetB.fbo);
    if (bloomTargetB.texture) glDeleteTextures(1, &bloomTargetB.texture);
    if (bloomTargetB.rbo) glDeleteRenderbuffers(1, &bloomTargetB.rbo);
    if (selectionMaskTarget.fbo) glDeleteFramebuffers(1, &selectionMaskTarget.fbo);
    if (selectionMaskTarget.texture) glDeleteTextures(1, &selectionMaskTarget.texture);
    if (selectionMaskTarget.rbo) glDeleteRenderbuffers(1, &selectionMaskTarget.rbo);
    for (auto& entry : shadowCubeMaps) {
        if (entry.second.fbo) glDeleteFramebuffers(1, &entry.second.fbo);
        if (entry.second.depthCube) glDeleteTextures(1, &entry.second.depthCube);
    }
    shadowCubeMaps.clear();
    for (auto& entry : mirrorTargets) {
        releaseRenderTarget(entry.second);
    }
    mirrorTargets.clear();
    for (auto& entry : uiTargets) {
        releaseRenderTarget(entry.second);
    }
    uiTargets.clear();
    if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
    if (viewportTexture) glDeleteTextures(1, &viewportTexture);
    if (rbo) glDeleteRenderbuffers(1, &rbo);
    if (quadVBO) glDeleteBuffers(1, &quadVBO);
    if (quadVAO) glDeleteVertexArrays(1, &quadVAO);
    if (debugWhiteTexture) glDeleteTextures(1, &debugWhiteTexture);
    if (missingMaterialFallbackTexture) glDeleteTextures(1, &missingMaterialFallbackTexture);
}

Texture* Renderer::getTexture(const std::string& path, MaterialProperties::TextureFilter filter) {
    if (path.empty()) return nullptr;
    if (glfwGetCurrentContext() == nullptr) {
        static bool warnedNoContext = false;
        if (!warnedNoContext) {
            std::cerr << "[Renderer] Texture request without an OpenGL context. Returning null texture.\n";
            warnedNoContext = true;
        }
        return nullptr;
    }
    bool point = (filter == MaterialProperties::TextureFilter::Point);
    auto& cache = point ? textureCachePoint : textureCacheBilinear;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second.get();

    GLenum minFilter = point ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
    GLenum magFilter = point ? GL_NEAREST : GL_LINEAR;
    auto tex = std::make_unique<Texture>(path, GL_REPEAT, GL_REPEAT, minFilter, magFilter);
    if (!tex->GetID()) {
        return nullptr;
    }
    Texture* raw = tex.get();
    cache[path] = std::move(tex);
    return raw;
}

void Renderer::initialize() {
    shader = new Shader(defaultVertPath.c_str(), defaultFragPath.c_str());
    defaultShader = shader;
    if (shader->ID == 0) {
        std::cerr << "Shader compilation failed!\n";
        delete shader;
        shader = nullptr;
        throw std::runtime_error("Shader error");
    }
    postShader = new Shader(postVertPath.c_str(), postFragPath.c_str());
    if (!postShader || postShader->ID == 0) {
        std::cerr << "PostFX shader compilation failed!\n";
        delete postShader;
        postShader = nullptr;
    } else {
        postShader->use();
        postShader->setInt("sceneTex", 0);
        postShader->setInt("bloomTex", 1);
        postShader->setInt("historyTex", 2);
    }
    brightShader = new Shader(postVertPath.c_str(), postBrightFragPath.c_str());
    if (!brightShader || brightShader->ID == 0) {
        std::cerr << "Bright-pass shader compilation failed!\n";
        delete brightShader;
        brightShader = nullptr;
    } else {
        brightShader->use();
        brightShader->setInt("sceneTex", 0);
    }

    blurShader = new Shader(postVertPath.c_str(), postBlurFragPath.c_str());
    if (!blurShader || blurShader->ID == 0) {
        std::cerr << "Blur shader compilation failed!\n";
        delete blurShader;
        blurShader = nullptr;
    } else {
        blurShader->use();
        blurShader->setInt("image", 0);
    }
    shadowDepthShader = new Shader(shadowDepthVertPath.c_str(), shadowDepthFragPath.c_str());
    if (!shadowDepthShader || shadowDepthShader->ID == 0) {
        std::cerr << "Shadow depth shader compilation failed; shadows will be disabled.\n";
        delete shadowDepthShader;
        shadowDepthShader = nullptr;
    }
    ShaderEntry entry;
    entry.shader.reset(defaultShader);
    entry.vertPath = defaultVertPath;
    entry.fragPath = defaultFragPath;
    if (fs::exists(defaultVertPath)) entry.vertTime = fs::last_write_time(defaultVertPath);
    if (fs::exists(defaultFragPath)) entry.fragTime = fs::last_write_time(defaultFragPath);
    shaderCache[defaultVertPath + "|" + defaultFragPath] = std::move(entry);

    texture1 = new Texture("Resources/Textures/container.jpg");
    texture2 = new Texture("Resources/Textures/awesomeface.png");
    if (debugWhiteTexture == 0) {
        unsigned char white[4] = { 255, 255, 255, 255 };
        glGenTextures(1, &debugWhiteTexture);
        glBindTexture(GL_TEXTURE_2D, debugWhiteTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    if (missingMaterialFallbackTexture == 0) {
        constexpr int kFallbackSize = 64;
        std::vector<unsigned char> pixels(kFallbackSize * kFallbackSize * 4, 255);
        const glm::vec3 centerColor(40.0f, 24.0f, 58.0f);
        const glm::vec3 edgeColor(70.0f, 46.0f, 102.0f);
        const float invHalfDiag = 1.0f / std::sqrt(0.5f * 0.5f + 0.5f * 0.5f);
        for (int y = 0; y < kFallbackSize; ++y) {
            for (int x = 0; x < kFallbackSize; ++x) {
                float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(kFallbackSize) - 0.5f;
                float fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(kFallbackSize) - 0.5f;
                float dist = std::sqrt(fx * fx + fy * fy) * invHalfDiag;
                dist = std::clamp(dist, 0.0f, 1.0f);
                float t = dist * dist * (3.0f - 2.0f * dist);
                glm::vec3 rgb = centerColor * (1.0f - t) + edgeColor * t;
                size_t idx = static_cast<size_t>(y * kFallbackSize + x) * 4;
                pixels[idx + 0] = static_cast<unsigned char>(std::clamp(rgb.r, 0.0f, 255.0f));
                pixels[idx + 1] = static_cast<unsigned char>(std::clamp(rgb.g, 0.0f, 255.0f));
                pixels[idx + 2] = static_cast<unsigned char>(std::clamp(rgb.b, 0.0f, 255.0f));
                pixels[idx + 3] = 255;
            }
        }

        glGenTextures(1, &missingMaterialFallbackTexture);
        glBindTexture(GL_TEXTURE_2D, missingMaterialFallbackTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kFallbackSize, kFallbackSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    cubeMesh = new Mesh(vertices, sizeof(vertices));

    auto sphereVerts = generateSphere();
    sphereMesh = new Mesh(sphereVerts.data(), sphereVerts.size() * sizeof(float));

    auto capsuleVerts = generateCapsule();
    capsuleMesh = new Mesh(capsuleVerts.data(), capsuleVerts.size() * sizeof(float));
    planeMesh = new Mesh(mirrorPlaneVertices, sizeof(mirrorPlaneVertices));
    auto torusVerts = generateTorus();
    torusMesh = new Mesh(torusVerts.data(), torusVerts.size() * sizeof(float));

    skybox = new Skybox();

    setupFBO();
    ensureRenderTarget(postTarget, currentWidth, currentHeight);
    ensureRenderTarget(historyTarget, currentWidth, currentHeight);
    ensureRenderTarget(bloomTargetA, currentWidth, currentHeight);
    ensureRenderTarget(bloomTargetB, currentWidth, currentHeight);
    ensureRenderTarget(selectionMaskTarget, currentWidth, currentHeight);
    ensureQuad();
    clearHistory();
    glEnable(GL_DEPTH_TEST);
}

Shader* Renderer::getShader(const std::string& vert, const std::string& frag) {
    std::string vPath = vert.empty() ? defaultVertPath : vert;
    std::string fPath = frag.empty() ? defaultFragPath : frag;
    std::string key = vPath + "|" + fPath;

    auto reloadEntry = [&](ShaderEntry& e) -> Shader* {
        std::unique_ptr<Shader> newShader = std::make_unique<Shader>(vPath.c_str(), fPath.c_str());
        if (!newShader || newShader->ID == 0) {
            std::cerr << "Shader reload failed for " << key << ", falling back to default\n";
            return defaultShader;
        }
        e.shader = std::move(newShader);
        e.vertPath = vPath;
        e.fragPath = fPath;
        if (fs::exists(vPath)) e.vertTime = fs::last_write_time(vPath);
        if (fs::exists(fPath)) e.fragTime = fs::last_write_time(fPath);
        return e.shader.get();
    };

    auto it = shaderCache.find(key);
    if (it != shaderCache.end()) {
        ShaderEntry& entry = it->second;
        if (autoReloadShaders) {
            bool changed = false;
            if (fs::exists(vPath)) {
                auto t = fs::last_write_time(vPath);
                if (t != entry.vertTime) { changed = true; entry.vertTime = t; }
            }
            if (fs::exists(fPath)) {
                auto t = fs::last_write_time(fPath);
                if (t != entry.fragTime) { changed = true; entry.fragTime = t; }
            }
            if (changed) {
                return reloadEntry(entry);
            }
        }
        return entry.shader ? entry.shader.get() : defaultShader;
    }

    ShaderEntry entry;
    entry.vertPath = vPath;
    entry.fragPath = fPath;
    if (fs::exists(vPath)) entry.vertTime = fs::last_write_time(vPath);
    if (fs::exists(fPath)) entry.fragTime = fs::last_write_time(fPath);
    entry.shader = std::make_unique<Shader>(vPath.c_str(), fPath.c_str());
    if (!entry.shader || entry.shader->ID == 0) {
        std::cerr << "Shader compile failed for " << key << ", using default\n";
        shaderCache[key] = std::move(entry);
        return defaultShader;
    }
    Shader* ptr = entry.shader.get();
    shaderCache[key] = std::move(entry);
    return ptr;
}

bool Renderer::forceReloadShader(const std::string& vert, const std::string& frag) {
    std::string vPath = vert.empty() ? defaultVertPath : vert;
    std::string fPath = frag.empty() ? defaultFragPath : frag;
    std::string key = vPath + "|" + fPath;
    auto it = shaderCache.find(key);
    if (it != shaderCache.end()) {
        shaderCache.erase(it);
    }
    ShaderEntry entry;
    entry.vertPath = vPath;
    entry.fragPath = fPath;
    if (fs::exists(vPath)) entry.vertTime = fs::last_write_time(vPath);
    if (fs::exists(fPath)) entry.fragTime = fs::last_write_time(fPath);
    entry.shader = std::make_unique<Shader>(vPath.c_str(), fPath.c_str());
    if (!entry.shader || entry.shader->ID == 0) {
        std::cerr << "Shader force reload failed for " << key << "\n";
        return false;
    }
    if (vPath == defaultVertPath && fPath == defaultFragPath) {
        defaultShader = entry.shader.get();
    }
    shaderCache[key] = std::move(entry);
    return true;
}

void Renderer::setupFBO() {
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    glGenTextures(1, &viewportTexture);
    glBindTexture(GL_TEXTURE_2D, viewportTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, currentWidth, currentHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, viewportTexture, 0);

    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, currentWidth, currentHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer setup failed!\n";
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    displayTexture = viewportTexture;
}

void Renderer::ensureRenderTarget(RenderTarget& target, int w, int h) {
    ensureRenderTarget(target, w, h, false);
}

void Renderer::ensureRenderTarget(RenderTarget& target, int w, int h, bool alpha) {
    if (w <= 0 || h <= 0) return;

    if (target.fbo == 0) {
        glGenFramebuffers(1, &target.fbo);
        glGenTextures(1, &target.texture);
        glGenRenderbuffers(1, &target.rbo);
    }

    if (target.width == w && target.height == h && target.hasAlpha == alpha) return;

    target.width = w;
    target.height = h;
    target.hasAlpha = alpha;

    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);

    glBindTexture(GL_TEXTURE_2D, target.texture);
    GLenum internalFormat = alpha ? GL_RGBA : GL_RGB;
    GLenum dataFormat = alpha ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, target.width, target.height, 0, dataFormat, GL_UNSIGNED_BYTE, NULL);
    bool isSelectionMask = (&target == &selectionMaskTarget);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, isSelectionMask ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, isSelectionMask ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);

    glBindRenderbuffer(GL_RENDERBUFFER, target.rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, target.width, target.height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target.rbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Preview framebuffer setup failed!\n";
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::releaseRenderTarget(RenderTarget& target) {
    if (target.texture) {
        glDeleteTextures(1, &target.texture);
    }
    if (target.rbo) {
        glDeleteRenderbuffers(1, &target.rbo);
    }
    if (target.fbo) {
        glDeleteFramebuffers(1, &target.fbo);
    }
    target = {};
}

void Renderer::updateMirrorTargets(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, float fovDeg, float nearPlane, float farPlane) {
    if (sceneObjects.empty() || width <= 0 || height <= 0) return;

    std::unordered_set<int> active;
    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    auto planeNormal = [](const SceneObject& obj) {
        glm::quat q = glm::quat(glm::radians(obj.rotation));
        glm::vec3 n = q * glm::vec3(0.0f, 0.0f, 1.0f);
        if (!std::isfinite(n.x) || glm::length(n) < 1e-3f) {
            n = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return glm::normalize(n);
    };

    for (const auto& obj : sceneObjects) {
        if (!obj.enabled || !obj.hasRenderer || obj.renderType != RenderType::Mirror) continue;
        active.insert(obj.id);

        RenderTarget& target = mirrorTargets[obj.id];
        ensureRenderTarget(target, width, height);
        if (target.fbo == 0 || target.texture == 0) continue;

        glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
        glViewport(0, 0, target.width, target.height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::vec3 n = planeNormal(obj);
        glm::vec3 planePoint = obj.position;

        auto reflectPoint = [&](const glm::vec3& p) {
            float dist = glm::dot(p - planePoint, n);
            return p - 2.0f * dist * n;
        };
        auto reflectDir = [&](const glm::vec3& v) {
            float dist = glm::dot(v, n);
            return v - 2.0f * dist * n;
        };

        Camera mirrorCam = camera;
        mirrorCam.position = reflectPoint(camera.position);
        mirrorCam.front = glm::normalize(reflectDir(camera.front));
        mirrorCam.up = glm::normalize(reflectDir(camera.up));
        if (!std::isfinite(mirrorCam.front.x) || glm::length(mirrorCam.front) < 1e-3f) {
            mirrorCam.front = glm::vec3(0.0f, 0.0f, -1.0f);
        }
        if (!std::isfinite(mirrorCam.up.x) || glm::length(mirrorCam.up) < 1e-3f) {
            mirrorCam.up = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        renderSceneInternal(mirrorCam, sceneObjects, target.width, target.height, false, fovDeg, nearPlane, farPlane, false);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);

    for (auto it = mirrorTargets.begin(); it != mirrorTargets.end(); ) {
        if (active.find(it->first) == active.end()) {
            releaseRenderTarget(it->second);
            it = mirrorTargets.erase(it);
        } else {
            ++it;
        }
    }
}

Renderer::UiTargetInfo Renderer::ensureUiTarget(int id, int width, int height) {
    if (glfwGetCurrentContext() == nullptr) {
        return {};
    }
    RenderTarget& target = uiTargets[id];
    ensureRenderTarget(target, width, height, true);
    return { target.fbo, target.texture, target.width, target.height };
}

void Renderer::cleanupUiTargets(const std::unordered_set<int>& active) {
    if (glfwGetCurrentContext() == nullptr) {
        uiTargets.clear();
        return;
    }
    for (auto it = uiTargets.begin(); it != uiTargets.end(); ) {
        if (active.find(it->first) == active.end()) {
            releaseRenderTarget(it->second);
            it = uiTargets.erase(it);
        } else {
            ++it;
        }
    }
}

void Renderer::ensureQuad() {
    if (quadVAO != 0) return;

    float quadVertices[] = {
        // positions   // texcoords
        -1.0f,  1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,

        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f
    };

    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
}

void Renderer::drawFullscreenQuad() {
    recordFullscreenDraw();
    if (quadVAO == 0) ensureQuad();
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void Renderer::resetStats(RenderStats& stats) {
    stats.drawCalls = 0;
    stats.meshDraws = 0;
    stats.fullscreenDraws = 0;
}

void Renderer::recordDrawCall() {
    if (!activeStats) return;
    activeStats->drawCalls += 1;
}

void Renderer::recordMeshDraw() {
    if (!activeStats) return;
    activeStats->drawCalls += 1;
    activeStats->meshDraws += 1;
}

void Renderer::recordFullscreenDraw() {
    if (!activeStats) return;
    activeStats->drawCalls += 1;
    activeStats->fullscreenDraws += 1;
}

void Renderer::clearHistory() {
    historyValid = false;
    if (historyTarget.fbo != 0 && historyTarget.width > 0 && historyTarget.height > 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, historyTarget.fbo);
        glViewport(0, 0, historyTarget.width, historyTarget.height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

void Renderer::clearTarget(RenderTarget& target) {
    if (target.fbo == 0 || target.width <= 0 || target.height <= 0) return;
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.width, target.height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::resize(int w, int h) {
    if (w <= 0 || h <= 0 || (w == currentWidth && h == currentHeight)) return;
    
    currentWidth = w;
    currentHeight = h;
    if (glfwGetCurrentContext() == nullptr) {
        return;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    
    glBindTexture(GL_TEXTURE_2D, viewportTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, currentWidth, currentHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, currentWidth, currentHeight);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer incomplete after resize!\n";
    }

    ensureRenderTarget(postTarget, currentWidth, currentHeight);
    ensureRenderTarget(historyTarget, currentWidth, currentHeight);
    ensureRenderTarget(bloomTargetA, currentWidth, currentHeight);
    ensureRenderTarget(bloomTargetB, currentWidth, currentHeight);
    ensureRenderTarget(selectionMaskTarget, currentWidth, currentHeight);
    clearHistory();
    displayTexture = viewportTexture;
}

void Renderer::beginRender(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos) {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, currentWidth, currentHeight);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    displayTexture = viewportTexture;

    shader->use();
    shader->setMat4("view", view);
    shader->setMat4("projection", proj);
    shader->setVec3("viewPos", cameraPos);
    shader->setFloat("uTime", static_cast<float>(glfwGetTime()));
    texture1->Bind(GL_TEXTURE0);
    texture2->Bind(GL_TEXTURE1);
    shader->setInt("texture1", 0);
    shader->setInt("overlayTex", 1);
    shader->setInt("normalMap", 2);
}

void Renderer::renderSkybox(const glm::mat4& view, const glm::mat4& proj) {
    if (skybox) {
        glDepthFunc(GL_LEQUAL);
        skybox->draw(glm::value_ptr(view), glm::value_ptr(proj));
        glDepthFunc(GL_LESS);
        
        shader->use();
        shader->setMat4("view", view);
        shader->setMat4("projection", proj);
    }
}

void Renderer::renderObject(const SceneObject& obj) {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, obj.position);
    model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1, 0, 0));
    model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0, 0, 1));
    model = glm::scale(model, obj.scale);

    bool hasMaterialAsset = !obj.materialPath.empty();
    bool hasCustomShader = !obj.vertexShaderPath.empty() || !obj.fragmentShaderPath.empty();
    bool hasAnySurfaceInput = !obj.albedoTexturePath.empty() || !obj.overlayTexturePath.empty() || !obj.normalMapPath.empty();
    bool missingMaterialAndShader = !hasMaterialAsset && !hasCustomShader && !hasAnySurfaceInput;

    shader->setMat4("model", model);
    shader->setVec3("materialColor", missingMaterialAndShader ? glm::vec3(1.0f) : obj.material.color);
    shader->setFloat("ambientStrength", obj.material.ambientStrength);
    shader->setFloat("specularStrength", obj.material.specularStrength);
    shader->setFloat("shininess", obj.material.shininess);
    shader->setFloat("mixAmount", obj.material.textureMix);
    shader->setBool("unlit", obj.renderType == RenderType::Mirror || obj.renderType == RenderType::Sprite || missingMaterialAndShader);

    Texture* baseTex = texture1;
    if (missingMaterialAndShader && missingMaterialFallbackTexture != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, missingMaterialFallbackTexture);
    } else {
        if (!obj.albedoTexturePath.empty()) {
            if (auto* t = getTexture(obj.albedoTexturePath, obj.material.textureFilter)) baseTex = t;
        }
        if (baseTex) baseTex->Bind(GL_TEXTURE0);
    }

    bool overlayUsed = false;
    if (obj.renderType == RenderType::Mirror) {
        auto it = mirrorTargets.find(obj.id);
        if (it != mirrorTargets.end() && it->second.texture != 0) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, it->second.texture);
            overlayUsed = true;
        }
    }
    if (!overlayUsed && obj.useOverlay && !obj.overlayTexturePath.empty()) {
        if (auto* t = getTexture(obj.overlayTexturePath, obj.material.textureFilter)) {
            t->Bind(GL_TEXTURE1);
            overlayUsed = true;
        }
    }
    if (!overlayUsed && texture2) {
        texture2->Bind(GL_TEXTURE1);
    }
    shader->setBool("hasOverlay", overlayUsed);

    bool normalUsed = false;
    if (!obj.normalMapPath.empty()) {
        if (auto* t = getTexture(obj.normalMapPath, obj.material.textureFilter)) {
            t->Bind(GL_TEXTURE2);
            normalUsed = true;
        }
    }
    shader->setBool("hasNormalMap", normalUsed);

    switch (obj.renderType) {
        case RenderType::Cube:
            cubeMesh->draw();
            break;
        case RenderType::Sphere:
            sphereMesh->draw();
            break;
        case RenderType::Capsule:
            capsuleMesh->draw();
            break;
        case RenderType::Plane:
            if (planeMesh) planeMesh->draw();
            break;
        case RenderType::Mirror:
            if (planeMesh) planeMesh->draw();
            break;
        case RenderType::Sprite:
            if (planeMesh) planeMesh->draw();
            break;
        case RenderType::Torus:
            if (torusMesh) torusMesh->draw();
            break;
        case RenderType::OBJMesh:
            if (obj.meshId >= 0) {
                Mesh* objMesh = g_objLoader.getMesh(obj.meshId);
                if (objMesh) {
                    objMesh->draw();
                }
            }
            break;
        case RenderType::Model:
            if (obj.meshId >= 0) {
                Mesh* modelMesh = getModelLoader().getMesh(obj.meshId);
                if (modelMesh) {
                    modelMesh->draw();
                }
            }
            break;
        case RenderType::None:
        default:
            break;
    }
}

void Renderer::renderSceneInternal(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, bool unbindFramebuffer, float fovDeg, float nearPlane, float farPlane, bool drawMirrorObjects) {
    if (!defaultShader || width <= 0 || height <= 0) return;

    struct LightUniform {
        int type = 0; // 0 dir,1 point,2 spot,3 area
        int sourceId = -1;
        glm::vec3 dir = glm::vec3(0.0f, -1.0f, 0.0f);
        glm::vec3 pos = glm::vec3(0.0f);
        glm::vec3 color = glm::vec3(1.0f);
        float intensity = 1.0f;
        float range = 10.0f;
        float inner = glm::cos(glm::radians(15.0f));
        float outer = glm::cos(glm::radians(25.0f));
        glm::vec2 areaSize = glm::vec2(1.0f); // width/height for area lights
        float areaFade = 0.0f; // 0 sharp, 1 fully softened
        bool castShadows = false;
        int shadowMode = 0; // 0 off, 1 hard, 2 soft
        float shadowBias = 0.02f;
        float shadowSoftness = 0.04f;
        float shadowFar = 10.0f;
        int shadowMapIndex = -1;
    };
    auto forwardFromRotation = [](const SceneObject& obj) {
        glm::vec3 f = glm::normalize(glm::vec3(
            glm::sin(glm::radians(obj.rotation.y)) * glm::cos(glm::radians(obj.rotation.x)),
            glm::sin(glm::radians(obj.rotation.x)),
            glm::cos(glm::radians(obj.rotation.y)) * glm::cos(glm::radians(obj.rotation.x))
        ));
        if (glm::length(f) < 1e-3f ||
            !std::isfinite(f.x) || !std::isfinite(f.y) || !std::isfinite(f.z)) {
            f = glm::vec3(0.0f, -1.0f, 0.0f);
        }
        return f;
    };
    auto buildModelMatrix = [](const SceneObject& obj) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, obj.position);
        model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, obj.scale);
        return model;
    };
    auto selectMeshForObject = [&](const SceneObject& obj) -> Mesh* {
        if (obj.renderType == RenderType::Cube) return cubeMesh;
        if (obj.renderType == RenderType::Sphere) return sphereMesh;
        if (obj.renderType == RenderType::Capsule) return capsuleMesh;
        if (obj.renderType == RenderType::Plane) return planeMesh;
        if (obj.renderType == RenderType::Mirror) return planeMesh;
        if (obj.renderType == RenderType::Sprite) return planeMesh;
        if (obj.renderType == RenderType::Torus) return torusMesh;
        if (obj.renderType == RenderType::OBJMesh && obj.meshId != -1) {
            return g_objLoader.getMesh(obj.meshId);
        }
        if (obj.renderType == RenderType::Model && obj.meshId != -1) {
            return getModelLoader().getMesh(obj.meshId);
        }
        return nullptr;
    };

    constexpr size_t kMaxLights = 10;
    std::vector<LightUniform> lights;
    lights.reserve(kMaxLights);

    struct LightCandidate {
        LightUniform light;
        float distSq = 0.0f;
        int id = 0;
    };
    std::vector<LightCandidate> candidates;
    candidates.reserve(sceneObjects.size());

    for (const auto& obj : sceneObjects) {
        if (!obj.enabled || !obj.hasLight || !obj.light.enabled) continue;
        if (obj.light.type == LightType::Directional) {
            LightUniform l;
            l.type = 0;
            l.sourceId = obj.id;
            l.dir = forwardFromRotation(obj);
            l.color = obj.light.color;
            l.intensity = obj.light.intensity;
            lights.push_back(l);
            if (lights.size() >= kMaxLights) break;
        } else if (obj.light.type == LightType::Spot) {
            LightUniform l;
            l.type = 2;
            l.sourceId = obj.id;
            l.pos = obj.position;
            l.dir = forwardFromRotation(obj);
            l.color = obj.light.color;
            l.intensity = obj.light.intensity;
            l.range = glm::max(obj.light.range, 1.0f);
            l.inner = glm::cos(glm::radians(obj.light.innerAngle));
            l.outer = glm::cos(glm::radians(obj.light.outerAngle));
            l.castShadows = obj.light.castShadows;
            l.shadowMode = obj.light.castShadows ? (obj.light.softShadows ? 2 : 1) : 0;
            l.shadowBias = glm::clamp(obj.light.shadowBias, 0.0001f, 0.2f);
            l.shadowSoftness = glm::clamp(obj.light.shadowSoftness, 0.0f, 0.2f);
            l.shadowFar = l.range;
            LightCandidate c;
            c.light = l;
            glm::vec3 delta = obj.position - camera.position;
            c.distSq = glm::dot(delta, delta);
            c.id = obj.id;
            candidates.push_back(c);
        } else if (obj.light.type == LightType::Point) {
            LightUniform l;
            l.type = 1;
            l.sourceId = obj.id;
            l.pos = obj.position;
            l.color = obj.light.color;
            l.intensity = obj.light.intensity;
            l.range = glm::max(obj.light.range, 1.0f);
            l.castShadows = obj.light.castShadows;
            l.shadowMode = obj.light.castShadows ? (obj.light.softShadows ? 2 : 1) : 0;
            l.shadowBias = glm::clamp(obj.light.shadowBias, 0.0001f, 0.2f);
            l.shadowSoftness = glm::clamp(obj.light.shadowSoftness, 0.0f, 0.2f);
            l.shadowFar = l.range;
            LightCandidate c;
            c.light = l;
            glm::vec3 delta = obj.position - camera.position;
            c.distSq = glm::dot(delta, delta);
            c.id = obj.id;
            candidates.push_back(c);
        } else if (obj.light.type == LightType::Area) {
            LightUniform l;
            l.type = 3; // area
            l.sourceId = obj.id;
            l.pos = obj.position;
            l.dir = forwardFromRotation(obj); // plane normal
            l.color = obj.light.color;
            l.intensity = obj.light.intensity;
            float sizeHint = glm::max(obj.light.size.x, obj.light.size.y);
            l.range = (obj.light.range > 0.0f) ? obj.light.range : glm::max(sizeHint * 2.0f, 1.0f);
            l.areaSize = obj.light.size;
            l.areaFade = glm::clamp(obj.light.edgeFade, 0.0f, 1.0f);
            l.castShadows = obj.light.castShadows;
            l.shadowMode = obj.light.castShadows ? (obj.light.softShadows ? 2 : 1) : 0;
            l.shadowBias = glm::clamp(obj.light.shadowBias, 0.0001f, 0.2f);
            l.shadowSoftness = glm::clamp(obj.light.shadowSoftness, 0.0f, 0.2f);
            l.shadowFar = glm::max(l.range, 1.0f);
            LightCandidate c;
            c.light = l;
            glm::vec3 delta = obj.position - camera.position;
            c.distSq = glm::dot(delta, delta);
            c.id = obj.id;
            candidates.push_back(c);
        }
    }

    if (lights.size() < kMaxLights && !candidates.empty()) {
        std::sort(candidates.begin(), candidates.end(),
                  [](const LightCandidate& a, const LightCandidate& b) {
                      if (a.distSq != b.distSq) return a.distSq < b.distSq;
                      return a.id < b.id;
                  });
        for (const auto& c : candidates) {
            if (lights.size() >= kMaxLights) break;
            lights.push_back(c.light);
        }
    }

    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 proj = glm::perspective(glm::radians(fovDeg), (float)width / (float)height, nearPlane, farPlane);
    const float timeSeconds = static_cast<float>(glfwGetTime());

    std::array<unsigned int, kMaxShadowMaps> shadowTextures = {0, 0, 0, 0};
    std::unordered_set<int> activeShadowIds;
    auto releaseShadowCubeMap = [](ShadowCubeMap& map) {
        if (map.fbo) glDeleteFramebuffers(1, &map.fbo);
        if (map.depthCube) glDeleteTextures(1, &map.depthCube);
        map.fbo = 0;
        map.depthCube = 0;
        map.resolution = 0;
    };
    if (shadowDepthShader && shadowDepthShader->ID != 0) {
        GLint prevViewport[4] = {0, 0, width, height};
        GLint prevFbo = 0;
        GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
        GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
        GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
        GLboolean polyOffsetWasEnabled = glIsEnabled(GL_POLYGON_OFFSET_FILL);
        GLint cullModeBefore = GL_BACK;
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glGetIntegerv(GL_CULL_FACE_MODE, &cullModeBefore);

        if (blendWasEnabled) glDisable(GL_BLEND);
        if (!depthWasEnabled) glEnable(GL_DEPTH_TEST);
        if (!polyOffsetWasEnabled) glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 4.0f);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        int shadowSlot = 0;
        for (auto& light : lights) {
            light.shadowMapIndex = -1;
            if (shadowSlot >= kMaxShadowMaps) continue;
            if (!light.castShadows || light.type == 0 || light.sourceId < 0) continue;

            ShadowCubeMap& shadowMap = shadowCubeMaps[light.sourceId];
            if (shadowMap.fbo == 0 || shadowMap.depthCube == 0 || shadowMap.resolution != shadowMapResolution) {
                releaseShadowCubeMap(shadowMap);

                glGenFramebuffers(1, &shadowMap.fbo);
                glGenTextures(1, &shadowMap.depthCube);
                shadowMap.resolution = shadowMapResolution;

                glBindTexture(GL_TEXTURE_CUBE_MAP, shadowMap.depthCube);
                for (int face = 0; face < 6; ++face) {
                    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_DEPTH_COMPONENT,
                                 shadowMap.resolution, shadowMap.resolution, 0,
                                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
                }
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

                glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.fbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X, shadowMap.depthCube, 0);
                glDrawBuffer(GL_NONE);
                glReadBuffer(GL_NONE);
                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    std::cerr << "Failed to create shadow cubemap framebuffer for light " << light.sourceId << "\n";
                    releaseShadowCubeMap(shadowMap);
                    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
                    continue;
                }
                glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
                glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            }

            if (shadowMap.fbo == 0 || shadowMap.depthCube == 0) continue;

            activeShadowIds.insert(light.sourceId);
            light.shadowFar = glm::max(light.shadowFar, 1.0f);
            light.shadowMapIndex = shadowSlot;
            shadowTextures[shadowSlot] = shadowMap.depthCube;
            ++shadowSlot;

            const float shadowNearPlane = 0.1f;
            glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, shadowNearPlane, light.shadowFar);
            std::array<glm::mat4, 6> shadowViews = {
                glm::lookAt(light.pos, light.pos + glm::vec3(1.0f, 0.0f, 0.0f),  glm::vec3(0.0f, -1.0f, 0.0f)),
                glm::lookAt(light.pos, light.pos + glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
                glm::lookAt(light.pos, light.pos + glm::vec3(0.0f, 1.0f, 0.0f),  glm::vec3(0.0f, 0.0f, 1.0f)),
                glm::lookAt(light.pos, light.pos + glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
                glm::lookAt(light.pos, light.pos + glm::vec3(0.0f, 0.0f, 1.0f),  glm::vec3(0.0f, -1.0f, 0.0f)),
                glm::lookAt(light.pos, light.pos + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f))
            };

            shadowDepthShader->use();
            shadowDepthShader->setVec3("lightPos", light.pos);
            shadowDepthShader->setFloat("farPlane", light.shadowFar);

            glViewport(0, 0, shadowMap.resolution, shadowMap.resolution);
            glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.fbo);

            for (int face = 0; face < 6; ++face) {
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                       GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, shadowMap.depthCube, 0);
                glDrawBuffer(GL_NONE);
                glReadBuffer(GL_NONE);
                glClear(GL_DEPTH_BUFFER_BIT);
                shadowDepthShader->setMat4("lightSpaceMatrix", shadowProj * shadowViews[face]);

                for (const auto& obj : sceneObjects) {
                    if (!obj.enabled || !HasRendererComponent(obj)) continue;
                    if (obj.renderType == RenderType::Mirror) continue;
                    if (obj.renderType == RenderType::Sprite) continue;
                    if (obj.id == light.sourceId) continue;
                    bool isUiCanvas3D = obj.hasUI && obj.ui.type == UIElementType::Canvas && obj.ui.renderIn3D;
                    if (isUiCanvas3D) continue;
                    if (obj.hasSkeletalAnimation && obj.skeletal.enabled) continue;

                    Mesh* shadowMesh = selectMeshForObject(obj);
                    if (!shadowMesh) continue;

                    shadowDepthShader->setMat4("model", buildModelMatrix(obj));
                    shadowMesh->draw();
                }
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        if (blendWasEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (!depthWasEnabled) glDisable(GL_DEPTH_TEST); else glEnable(GL_DEPTH_TEST);
        if (!polyOffsetWasEnabled) glDisable(GL_POLYGON_OFFSET_FILL);
        if (!cullWasEnabled) glDisable(GL_CULL_FACE);
        else {
            glEnable(GL_CULL_FACE);
            glCullFace(cullModeBefore);
        }
    }

    for (auto it = shadowCubeMaps.begin(); it != shadowCubeMaps.end(); ) {
        if (activeShadowIds.find(it->first) == activeShadowIds.end()) {
            releaseShadowCubeMap(it->second);
            it = shadowCubeMaps.erase(it);
        } else {
            ++it;
        }
    }

    GLboolean cullFace = glIsEnabled(GL_CULL_FACE);
    GLint prevCullMode = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &prevCullMode);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    renderSkybox(view, proj);

    for (const auto& obj : sceneObjects) {
        if (!obj.enabled) continue;
        if (!drawMirrorObjects && obj.hasRenderer && obj.renderType == RenderType::Mirror) continue;
        if (!HasRendererComponent(obj)) continue;

        glm::mat4 model = buildModelMatrix(obj);

        std::string vertPath = obj.vertexShaderPath;
        std::string fragPath = obj.fragmentShaderPath;
        int boneLimit = obj.skeletal.maxBones;
        int availableBones = static_cast<int>(obj.skeletal.finalMatrices.size());
        bool needsFallback = obj.hasSkeletalAnimation && obj.skeletal.enabled &&
                             obj.skeletal.allowCpuFallback &&
                             boneLimit > 0 && availableBones > boneLimit;
        bool wantsGpuSkinning = obj.hasSkeletalAnimation && obj.skeletal.enabled &&
                                obj.skeletal.useGpuSkinning && !needsFallback;
        if (vertPath.empty() && wantsGpuSkinning) {
            vertPath = skinnedVertPath;
        }
        Shader* active = getShader(vertPath, fragPath);
        if (!active) continue;
        shader = active;
        shader->use();

        bool hasMaterialAsset = !obj.materialPath.empty();
        bool hasCustomShader = !obj.vertexShaderPath.empty() || !obj.fragmentShaderPath.empty();
        bool hasAnySurfaceInput = !obj.albedoTexturePath.empty() || !obj.overlayTexturePath.empty() || !obj.normalMapPath.empty();
        bool missingMaterialAndShader = !hasMaterialAsset && !hasCustomShader && !hasAnySurfaceInput;

        shader->setMat4("view", view);
        shader->setMat4("projection", proj);
        shader->setVec3("viewPos", camera.position);
        shader->setFloat("uTime", timeSeconds);
        bool isUiCanvas3D = obj.hasUI && obj.ui.type == UIElementType::Canvas && obj.ui.renderIn3D;
        shader->setBool("unlit", obj.renderType == RenderType::Mirror || obj.renderType == RenderType::Sprite || isUiCanvas3D || missingMaterialAndShader);
        shader->setVec3("ambientColor", ambientColor);
        shader->setInt("texture1", 0);
        shader->setInt("overlayTex", 1);
        shader->setInt("normalMap", 2);
        shader->setInt("shadowCube0", 3);
        shader->setInt("shadowCube1", 4);
        shader->setInt("shadowCube2", 5);
        shader->setInt("shadowCube3", 6);
        for (int slot = 0; slot < kMaxShadowMaps; ++slot) {
            glActiveTexture(GL_TEXTURE3 + slot);
            glBindTexture(GL_TEXTURE_CUBE_MAP, shadowTextures[slot]);
        }

        shader->setInt("lightCount", static_cast<int>(lights.size()));
        for (size_t i = 0; i < lights.size() && i < kMaxLights; ++i) {
            const auto& l = lights[i];
            std::string idx = "[" + std::to_string(i) + "]";
            shader->setInt("lightTypeArr" + idx, l.type);
            shader->setVec3("lightDirArr" + idx, l.dir);
            shader->setVec3("lightPosArr" + idx, l.pos);
            shader->setVec3("lightColorArr" + idx, l.color);
            shader->setFloat("lightIntensityArr" + idx, l.intensity);
            shader->setFloat("lightRangeArr" + idx, l.range);
            shader->setFloat("lightInnerCosArr" + idx, l.inner);
            shader->setFloat("lightOuterCosArr" + idx, l.outer);
            shader->setVec2("lightAreaSizeArr" + idx, l.areaSize);
            shader->setFloat("lightAreaFadeArr" + idx, l.areaFade);
            shader->setInt("lightShadowMapArr" + idx, l.shadowMapIndex);
            shader->setInt("lightShadowModeArr" + idx, (l.shadowMapIndex >= 0) ? l.shadowMode : 0);
            shader->setFloat("lightShadowBiasArr" + idx, l.shadowBias);
            shader->setFloat("lightShadowSoftnessArr" + idx, l.shadowSoftness);
            shader->setFloat("lightShadowFarArr" + idx, l.shadowFar);
        }

        shader->setMat4("model", model);
        shader->setVec3("materialColor", missingMaterialAndShader ? glm::vec3(1.0f) : obj.material.color);
        shader->setFloat("ambientStrength", obj.material.ambientStrength);
        shader->setFloat("specularStrength", obj.material.specularStrength);
        shader->setFloat("shininess", obj.material.shininess);
        shader->setFloat("mixAmount", obj.material.textureMix);

        if (obj.hasSkeletalAnimation && obj.skeletal.enabled) {
            int safeLimit = std::max(0, boneLimit);
            int boneCount = std::min<int>(availableBones, safeLimit);
            if (wantsGpuSkinning && boneCount > 0) {
                shader->setInt("boneCount", boneCount);
                shader->setMat4Array("bones", obj.skeletal.finalMatrices.data(), boneCount);
                shader->setBool("useSkinning", true);
            } else {
                shader->setBool("useSkinning", false);
            }
        } else {
            shader->setBool("useSkinning", false);
        }

        bool usingUiTargetTex = false;
        if (isUiCanvas3D) {
            auto it = uiTargets.find(obj.id);
            if (it != uiTargets.end() && it->second.texture != 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, it->second.texture);
                usingUiTargetTex = true;
            }
        }
        Texture* baseTex = texture1;
        if (!usingUiTargetTex) {
            if (missingMaterialAndShader && missingMaterialFallbackTexture != 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, missingMaterialFallbackTexture);
            } else {
                if (!obj.albedoTexturePath.empty()) {
                    if (auto* t = getTexture(obj.albedoTexturePath, obj.material.textureFilter)) baseTex = t;
                }
                if (baseTex) baseTex->Bind(GL_TEXTURE0);
            }
        }

        bool overlayUsed = false;
        if (obj.renderType == RenderType::Mirror) {
            auto it = mirrorTargets.find(obj.id);
            if (it != mirrorTargets.end() && it->second.texture != 0) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, it->second.texture);
                overlayUsed = true;
            }
        }
        if (!overlayUsed && obj.useOverlay && !obj.overlayTexturePath.empty()) {
            if (auto* t = getTexture(obj.overlayTexturePath, obj.material.textureFilter)) {
                t->Bind(GL_TEXTURE1);
                overlayUsed = true;
            }
        }
        if (!overlayUsed && texture2) {
            texture2->Bind(GL_TEXTURE1);
        }
        shader->setBool("hasOverlay", overlayUsed);

        bool normalUsed = false;
        if (!obj.normalMapPath.empty()) {
            if (auto* t = getTexture(obj.normalMapPath, obj.material.textureFilter)) {
                t->Bind(GL_TEXTURE2);
                normalUsed = true;
            }
        }
        shader->setBool("hasNormalMap", normalUsed);

        Mesh* meshToDraw = selectMeshForObject(obj);

        if (obj.renderType == RenderType::Model && obj.meshId != -1 &&
            obj.hasSkeletalAnimation && obj.skeletal.enabled && !wantsGpuSkinning) {
            const auto* meshInfo = getModelLoader().getMeshInfo(obj.meshId);
            if (meshInfo) {
                applyCpuSkinning(*const_cast<OBJLoader::LoadedMesh*>(meshInfo),
                                 obj.skeletal.finalMatrices,
                                 obj.skeletal.maxBones);
            }
        }

        bool doubleSided = (obj.renderType == RenderType::Sprite || obj.renderType == RenderType::Mirror);
        if (doubleSided) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }

        if (meshToDraw) {
            bool wantsBlend = (obj.renderType == RenderType::Sprite) || isUiCanvas3D;
            GLboolean blendWasEnabled = GL_FALSE;
            GLboolean depthMaskWasEnabled = GL_TRUE;
            if (wantsBlend) {
                blendWasEnabled = glIsEnabled(GL_BLEND);
                if (!blendWasEnabled) glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }
            if (isUiCanvas3D) {
                glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWasEnabled);
                glDepthMask(GL_FALSE);
            }
            recordMeshDraw();
            meshToDraw->draw();
            if (isUiCanvas3D) {
                glDepthMask(depthMaskWasEnabled);
            }
            if (wantsBlend && !blendWasEnabled) {
                glDisable(GL_BLEND);
            }
        }
    }

    if (!cullFace) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(prevCullMode);
    }

    if (unbindFramebuffer) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

PostFXSettings Renderer::gatherPostFX(const std::vector<SceneObject>& sceneObjects) const {
    PostFXSettings combined;
    combined.enabled = false;
    for (const auto& obj : sceneObjects) {
        if (!obj.hasPostFX) continue;
        if (!obj.postFx.enabled) continue;
        combined = obj.postFx; // Last enabled node wins for now
        combined.enabled = true;
    }
    return combined;
}

unsigned int Renderer::applyPostProcessing(const std::vector<SceneObject>& sceneObjects, unsigned int sourceTexture, int width, int height, bool allowHistory) {
    PostFXSettings settings = gatherPostFX(sceneObjects);
    GLint polygonMode[2] = { GL_FILL, GL_FILL };
#ifdef GL_POLYGON_MODE
    glGetIntegerv(GL_POLYGON_MODE, polygonMode);
#endif
    bool wireframe = (polygonMode[0] == GL_LINE || polygonMode[1] == GL_LINE);

    bool wantsEffects = settings.enabled &&
        (settings.bloomEnabled || settings.colorAdjustEnabled || settings.motionBlurEnabled ||
         settings.vignetteEnabled || settings.chromaticAberrationEnabled || settings.ambientOcclusionEnabled);

    if (wireframe) {
        wantsEffects = false;
    }

    if (!wantsEffects || !postShader || width <= 0 || height <= 0 || sourceTexture == 0) {
        if (allowHistory) {
            displayTexture = sourceTexture;
            clearHistory();
        }
        return sourceTexture;
    }

    RenderTarget& target = allowHistory ? postTarget : previewPostTarget;
    ensureRenderTarget(target, width, height);
    if (allowHistory) {
        ensureRenderTarget(historyTarget, width, height);
    }
    ensureRenderTarget(bloomTargetA, width, height);
    ensureRenderTarget(bloomTargetB, width, height);
    if (target.fbo == 0 || target.texture == 0) {
        if (allowHistory) {
            displayTexture = sourceTexture;
            clearHistory();
        }
        return sourceTexture;
    }

    // --- Bloom using bright pass + separable blur (inspired by ProcessingPostFX) ---
    unsigned int bloomTex = 0;
    if (settings.bloomEnabled && brightShader && blurShader) {
        // Bright pass
        glDisable(GL_DEPTH_TEST);
        brightShader->use();
        brightShader->setFloat("threshold", settings.bloomThreshold);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sourceTexture);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomTargetA.fbo);
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);
        drawFullscreenQuad();

        // Blur ping-pong
        blurShader->use();
        float sigma = glm::max(settings.bloomRadius * 2.5f, 0.1f);
        int radius = static_cast<int>(glm::clamp(settings.bloomRadius * 4.0f, 2.0f, 12.0f));
        blurShader->setFloat("sigma", sigma);
        blurShader->setInt("radius", radius);

        bool horizontal = true;
        unsigned int pingTex = bloomTargetA.texture;
        RenderTarget* writeTarget = &bloomTargetB;
        for (int i = 0; i < 4; ++i) {
            blurShader->setBool("horizontal", horizontal);
            blurShader->setVec2("texelSize", glm::vec2(1.0f / width, 1.0f / height));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, pingTex);
            glBindFramebuffer(GL_FRAMEBUFFER, writeTarget->fbo);
            glViewport(0, 0, width, height);
            glClear(GL_COLOR_BUFFER_BIT);
            drawFullscreenQuad();

            // swap
            pingTex = writeTarget->texture;
            writeTarget = (writeTarget == &bloomTargetA) ? &bloomTargetB : &bloomTargetA;
            horizontal = !horizontal;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glEnable(GL_DEPTH_TEST);
        bloomTex = pingTex;
    } else {
        bloomTex = 0;
        clearTarget(bloomTargetA);
        clearTarget(bloomTargetB);
    }

    glDisable(GL_DEPTH_TEST);
    postShader->use();
    postShader->setBool("enableBloom", settings.bloomEnabled && bloomTex != 0);
    postShader->setFloat("bloomIntensity", settings.bloomIntensity);
    postShader->setBool("enableColorAdjust", settings.colorAdjustEnabled);
    postShader->setFloat("exposure", settings.exposure);
    postShader->setFloat("contrast", settings.contrast);
    postShader->setFloat("saturation", settings.saturation);
    postShader->setVec3("colorFilter", settings.colorFilter);
    postShader->setBool("enableMotionBlur", settings.motionBlurEnabled);
    postShader->setFloat("motionBlurStrength", settings.motionBlurStrength);
    postShader->setBool("hasHistory", allowHistory && historyValid);
    postShader->setBool("enableVignette", settings.vignetteEnabled);
    postShader->setFloat("vignetteIntensity", settings.vignetteIntensity);
    postShader->setFloat("vignetteSmoothness", settings.vignetteSmoothness);
    postShader->setBool("enableChromatic", settings.chromaticAberrationEnabled);
    postShader->setFloat("chromaticAmount", settings.chromaticAmount);
    postShader->setBool("enableAO", settings.ambientOcclusionEnabled);
    postShader->setFloat("aoRadius", settings.aoRadius);
    postShader->setFloat("aoStrength", settings.aoStrength);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTex ? bloomTex : sourceTexture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, allowHistory ? historyTarget.texture : 0);

    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    drawFullscreenQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);

    if (allowHistory) {
        displayTexture = target.texture;
    }

    if (settings.motionBlurEnabled && allowHistory && historyTarget.fbo != 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, historyTarget.fbo);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        historyValid = true;
    } else if (allowHistory) {
        clearHistory();
    }

    return target.texture;
}

void Renderer::renderScene(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int selectedId, float fovDeg, float nearPlane, float farPlane, bool drawColliders, const std::vector<int>* selectedIds) {
    resetStats(viewportStats);
    activeStats = &viewportStats;
    updateMirrorTargets(camera, sceneObjects, currentWidth, currentHeight, fovDeg, nearPlane, farPlane);
    renderSceneInternal(camera, sceneObjects, currentWidth, currentHeight, true, fovDeg, nearPlane, farPlane, true);
    if (drawColliders) {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, currentWidth, currentHeight);
        renderCollisionOverlay(camera, sceneObjects, currentWidth, currentHeight, fovDeg, nearPlane, farPlane);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    std::vector<int> effectiveSelection;
    if (selectedIds && !selectedIds->empty()) {
        effectiveSelection = *selectedIds;
    } else if (selectedId >= 0) {
        effectiveSelection.push_back(selectedId);
    }
    renderSelectionOutline(camera, sceneObjects, effectiveSelection, fovDeg, nearPlane, farPlane);
    unsigned int result = applyPostProcessing(sceneObjects, viewportTexture, currentWidth, currentHeight, true);
    displayTexture = result ? result : viewportTexture;
    activeStats = nullptr;
}

unsigned int Renderer::renderScenePreview(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, float fovDeg, float nearPlane, float farPlane, bool applyPostFX, int previewSlot) {
    if (glfwGetCurrentContext() == nullptr) {
        return 0;
    }
    resetStats(previewStats);
    activeStats = &previewStats;
    RenderTarget& target = (previewSlot == 0) ? previewTarget : extraPreviewTargets[previewSlot];
    ensureRenderTarget(target, width, height);
    if (target.fbo == 0) {
        activeStats = nullptr;
        return 0;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, width, height);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    updateMirrorTargets(camera, sceneObjects, width, height, fovDeg, nearPlane, farPlane);
    renderSceneInternal(camera, sceneObjects, width, height, true, fovDeg, nearPlane, farPlane, true);
    if (!applyPostFX) {
        activeStats = nullptr;
        return target.texture;
    }
    unsigned int processed = applyPostProcessing(sceneObjects, target.texture, width, height, false);
    activeStats = nullptr;
    return processed ? processed : target.texture;
}

void Renderer::renderCollisionOverlay(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, float fovDeg, float nearPlane, float farPlane) {
    if (!defaultShader || width <= 0 || height <= 0) return;

    GLint prevPoly[2] = { GL_FILL, GL_FILL };
    glGetIntegerv(GL_POLYGON_MODE, prevPoly);
    GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    GLboolean cullFace = glIsEnabled(GL_CULL_FACE);
    GLint prevCullMode = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &prevCullMode);
    GLboolean polyOffsetLine = glIsEnabled(GL_POLYGON_OFFSET_LINE);

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1.0f, -1.0f);

    Shader* active = defaultShader;
    active->use();
    active->setMat4("view", camera.getViewMatrix());
    active->setMat4("projection", glm::perspective(glm::radians(fovDeg), (float)width / (float)height, nearPlane, farPlane));
    active->setVec3("viewPos", camera.position);
    active->setBool("unlit", true);
    active->setBool("hasOverlay", false);
    active->setBool("hasNormalMap", false);
    active->setInt("lightCount", 0);
    active->setFloat("mixAmount", 0.0f);
    active->setVec3("materialColor", glm::vec3(0.2f, 1.0f, 0.2f));
    active->setFloat("ambientStrength", 1.0f);
    active->setFloat("specularStrength", 0.0f);
    active->setFloat("shininess", 1.0f);
    active->setInt("texture1", 0);
    active->setInt("overlayTex", 1);
    active->setInt("normalMap", 2);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, debugWhiteTexture ? debugWhiteTexture : (texture1 ? texture1->GetID() : 0));

    for (const auto& obj : sceneObjects) {
        if (!obj.enabled) continue;
        if (!(obj.hasCollider && obj.collider.enabled) && !(obj.hasRigidbody && obj.rigidbody.enabled)) continue;

        Mesh* meshToDraw = nullptr;
        glm::vec3 scale = obj.scale;
        glm::vec3 position = obj.position;
        glm::vec3 rotation = obj.rotation;

        if (obj.hasCollider && obj.collider.enabled) {
            switch (obj.collider.type) {
                case ColliderType::Box:
                    meshToDraw = cubeMesh;
                    scale = obj.collider.boxSize;
                    break;
                case ColliderType::Capsule:
                    meshToDraw = capsuleMesh;
                    scale = obj.collider.boxSize;
                    break;
                case ColliderType::Mesh:
                case ColliderType::ConvexMesh:
                    if (obj.hasRenderer && obj.renderType == RenderType::OBJMesh && obj.meshId >= 0) {
                        meshToDraw = g_objLoader.getMesh(obj.meshId);
                    } else if (obj.hasRenderer && obj.renderType == RenderType::Model && obj.meshId >= 0) {
                        meshToDraw = getModelLoader().getMesh(obj.meshId);
                    } else {
                        meshToDraw = nullptr;
                    }
                    scale = obj.scale;
                    break;
            }
        } else {
            switch (obj.renderType) {
                case RenderType::Cube:
                    meshToDraw = cubeMesh;
                    break;
                case RenderType::Sphere:
                    meshToDraw = sphereMesh;
                    break;
                case RenderType::Capsule:
                    meshToDraw = capsuleMesh;
                    break;
                case RenderType::Plane:
                    meshToDraw = planeMesh;
                    break;
                case RenderType::Sprite:
                    meshToDraw = planeMesh;
                    break;
                case RenderType::Torus:
                    meshToDraw = sphereMesh;
                    break;
                case RenderType::OBJMesh:
                    if (obj.meshId >= 0) meshToDraw = g_objLoader.getMesh(obj.meshId);
                    break;
                case RenderType::Model:
                    if (obj.meshId >= 0) meshToDraw = getModelLoader().getMesh(obj.meshId);
                    break;
                default:
                    break;
            }
        }

        if (!meshToDraw) continue;

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, scale);
        active->setMat4("model", model);

        meshToDraw->draw();
    }

    if (!polyOffsetLine) glDisable(GL_POLYGON_OFFSET_LINE);
    if (cullFace) glEnable(GL_CULL_FACE);
    if (depthTest) glEnable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, prevPoly[0]);
}

void Renderer::renderSelectionOutline(const Camera& camera, const std::vector<SceneObject>& sceneObjects, const std::vector<int>& selectedIds, float fovDeg, float nearPlane, float farPlane) {
    const double nowSec = glfwGetTime();
    if (selectionOutlineLastUpdateSec <= 0.0 || nowSec < selectionOutlineLastUpdateSec) {
        selectionOutlineLastUpdateSec = nowSec;
    }
    float dt = static_cast<float>(nowSec - selectionOutlineLastUpdateSec);
    selectionOutlineLastUpdateSec = nowSec;
    dt = std::clamp(dt, 0.0f, 0.25f);

    auto normalizeSelectionIds = [](const std::vector<int>& ids) {
        std::vector<int> out;
        out.reserve(ids.size());
        for (int id : ids) {
            if (id >= 0) out.push_back(id);
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    };

    std::vector<int> normalizedSelection = normalizeSelectionIds(selectedIds);

    constexpr float kFadeInSeconds = 0.05f;
    constexpr float kFadeOutSeconds = 0.05f;
    constexpr float kSwapSeconds = 0.10f;
    const bool hasSelection = !normalizedSelection.empty();
    if (hasSelection) {
        if (selectionOutlineVisualIds.empty()) {
            selectionOutlineVisualIds = normalizedSelection;
            selectionOutlinePrevIds.clear();
            selectionOutlineSwapBlend = 1.0f;
        } else if (selectionOutlineVisualIds != normalizedSelection) {
            selectionOutlinePrevIds = selectionOutlineVisualIds;
            selectionOutlineVisualIds = normalizedSelection;
            selectionOutlineSwapBlend = 0.0f;
        }
    }

    const float targetBlend = hasSelection ? 1.0f : 0.0f;
    if (selectionOutlineBlend < targetBlend) {
        selectionOutlineBlend = std::min(targetBlend, selectionOutlineBlend + dt / kFadeInSeconds);
    } else if (selectionOutlineBlend > targetBlend) {
        selectionOutlineBlend = std::max(targetBlend, selectionOutlineBlend - dt / kFadeOutSeconds);
    }

    if (!hasSelection) {
        selectionOutlinePrevIds.clear();
        selectionOutlineSwapBlend = 1.0f;
    } else if (!selectionOutlinePrevIds.empty()) {
        selectionOutlineSwapBlend = std::min(1.0f, selectionOutlineSwapBlend + dt / kSwapSeconds);
        if (selectionOutlineSwapBlend >= 0.999f) {
            selectionOutlineSwapBlend = 1.0f;
            selectionOutlinePrevIds.clear();
        }
    } else {
        selectionOutlineSwapBlend = 1.0f;
    }

    if (!hasSelection && selectionOutlineBlend <= 0.001f) {
        selectionOutlineBlend = 0.0f;
        selectionOutlineVisualIds.clear();
        selectionOutlinePrevIds.clear();
        selectionOutlineSwapBlend = 1.0f;
        return;
    }
    if (!defaultShader || currentWidth <= 0 || currentHeight <= 0) return;
    if (selectionOutlineVisualIds.empty() || selectionOutlineBlend <= 0.001f) return;

    struct MaskDrawItem {
        const SceneObject* obj = nullptr;
        Mesh* mesh = nullptr;
        bool wantsGpuSkinning = false;
        bool doubleSided = false;
    };

    auto resolveMesh = [this](const SceneObject& obj) -> Mesh* {
        if (obj.renderType == RenderType::Cube) return cubeMesh;
        if (obj.renderType == RenderType::Sphere) return sphereMesh;
        if (obj.renderType == RenderType::Capsule) return capsuleMesh;
        if (obj.renderType == RenderType::Plane) return planeMesh;
        if (obj.renderType == RenderType::Mirror) return planeMesh;
        if (obj.renderType == RenderType::Sprite) return planeMesh;
        if (obj.renderType == RenderType::Torus) return torusMesh;
        if (obj.renderType == RenderType::OBJMesh && obj.meshId >= 0) return g_objLoader.getMesh(obj.meshId);
        if (obj.renderType == RenderType::Model && obj.meshId >= 0) return getModelLoader().getMesh(obj.meshId);
        return nullptr;
    };

    auto buildDrawItems = [&](const std::vector<int>& ids) {
        std::unordered_set<int> selectedSet(ids.begin(), ids.end());
        std::vector<MaskDrawItem> drawItems;
        drawItems.reserve(selectedSet.size());

        for (const auto& obj : sceneObjects) {
            if (!obj.enabled) continue;
            if (selectedSet.find(obj.id) == selectedSet.end()) continue;
            if (!HasRendererComponent(obj)) continue;

            Mesh* meshToDraw = resolveMesh(obj);
            if (!meshToDraw) continue;

            bool wantsGpuSkinning = obj.hasSkeletalAnimation && obj.skeletal.enabled && obj.skeletal.useGpuSkinning;
            int boneLimit = obj.skeletal.maxBones;
            int availableBones = static_cast<int>(obj.skeletal.finalMatrices.size());
            if (obj.hasSkeletalAnimation && obj.skeletal.enabled &&
                obj.skeletal.allowCpuFallback && boneLimit > 0 && availableBones > boneLimit) {
                wantsGpuSkinning = false;
            }

            if (obj.renderType == RenderType::Model && obj.meshId >= 0 &&
                obj.hasSkeletalAnimation && obj.skeletal.enabled && !wantsGpuSkinning) {
                const auto* meshInfo = getModelLoader().getMeshInfo(obj.meshId);
                if (meshInfo) {
                    applyCpuSkinning(*const_cast<OBJLoader::LoadedMesh*>(meshInfo),
                                     obj.skeletal.finalMatrices,
                                     obj.skeletal.maxBones);
                }
            }

            drawItems.push_back({
                &obj,
                meshToDraw,
                wantsGpuSkinning,
                (obj.renderType == RenderType::Sprite || obj.renderType == RenderType::Mirror)
            });
        }

        return drawItems;
    };

    std::vector<MaskDrawItem> currentDrawItems = buildDrawItems(selectionOutlineVisualIds);
    std::vector<MaskDrawItem> previousDrawItems;
    if (!selectionOutlinePrevIds.empty() && selectionOutlineSwapBlend < 0.999f) {
        previousDrawItems = buildDrawItems(selectionOutlinePrevIds);
    }
    if (previousDrawItems.empty()) {
        selectionOutlinePrevIds.clear();
        selectionOutlineSwapBlend = 1.0f;
    }
    if (currentDrawItems.empty() && previousDrawItems.empty()) return;

    ensureRenderTarget(selectionMaskTarget, currentWidth, currentHeight);
    if (selectionMaskTarget.fbo == 0 || selectionMaskTarget.texture == 0) return;

    Shader* staticMaskShader = getShader(selectionMaskVertPath, selectionMaskFragPath);
    Shader* skinnedMaskShader = getShader(skinnedVertPath, selectionMaskFragPath);
    Shader* outlineShader = getShader(postVertPath, selectionOutlineFragPath);
    if (!staticMaskShader || !skinnedMaskShader || !outlineShader) return;
    const bool staticMaskShaderFallbackToDefault = (staticMaskShader == defaultShader);
    const bool skinnedMaskShaderFallbackToDefault = (skinnedMaskShader == defaultShader);
    const bool outlineShaderFallbackToDefault = (outlineShader == defaultShader);
    if (outlineShaderFallbackToDefault) return;

    GLint prevDrawFbo = 0;
    GLint prevReadFbo = 0;
    GLint prevViewport[4] = { 0, 0, currentWidth, currentHeight };
    GLint prevActiveTex = GL_TEXTURE0;
    GLint prevBlendSrcRGB = GL_ONE;
    GLint prevBlendDstRGB = GL_ZERO;
    GLint prevBlendSrcAlpha = GL_ONE;
    GLint prevBlendDstAlpha = GL_ZERO;
    GLint prevDepthFunc = GL_LESS;
    GLint prevPoly[2] = { GL_FILL, GL_FILL };
    GLfloat prevPolyOffsetFactor = 0.0f;
    GLfloat prevPolyOffsetUnits = 0.0f;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstAlpha);
    glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
    glGetIntegerv(GL_POLYGON_MODE, prevPoly);
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &prevPolyOffsetFactor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &prevPolyOffsetUnits);

    GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    GLboolean cullFace = glIsEnabled(GL_CULL_FACE);
    GLint prevCullMode = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &prevCullMode);
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLboolean stencilEnabled = glIsEnabled(GL_STENCIL_TEST);
    GLboolean polyOffsetFill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    GLboolean prevColorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    glGetBooleanv(GL_COLOR_WRITEMASK, prevColorMask);

    const glm::mat4 view = camera.getViewMatrix();
    const glm::mat4 projection = glm::perspective(glm::radians(fovDeg), (float)currentWidth / (float)currentHeight, nearPlane, farPlane);
    auto drawOutlinePass = [&](const std::vector<MaskDrawItem>& drawItems, float passWeight) {
        if (drawItems.empty() || passWeight <= 0.001f) return;

        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, selectionMaskTarget.fbo);
        glBlitFramebuffer(0, 0, currentWidth, currentHeight,
                          0, 0, currentWidth, currentHeight,
                          GL_DEPTH_BUFFER_BIT, GL_NEAREST);

        glBindFramebuffer(GL_FRAMEBUFFER, selectionMaskTarget.fbo);
        glViewport(0, 0, currentWidth, currentHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_FALSE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_BLEND);
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -1.0f);

        for (const auto& drawItem : drawItems) {
            Shader* maskShader = drawItem.wantsGpuSkinning ? skinnedMaskShader : staticMaskShader;
            if (!maskShader) continue;

            if (drawItem.doubleSided) {
                glDisable(GL_CULL_FACE);
            } else {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, drawItem.obj->position);
            model = glm::rotate(model, glm::radians(drawItem.obj->rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, glm::radians(drawItem.obj->rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, glm::radians(drawItem.obj->rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::scale(model, drawItem.obj->scale);

            maskShader->use();
            maskShader->setMat4("view", view);
            maskShader->setMat4("projection", projection);
            maskShader->setMat4("model", model);

            const bool maskShaderFallbackToDefault =
                drawItem.wantsGpuSkinning ? skinnedMaskShaderFallbackToDefault : staticMaskShaderFallbackToDefault;
            if (maskShaderFallbackToDefault) {
                maskShader->setVec3("viewPos", camera.position);
                maskShader->setBool("unlit", true);
                maskShader->setBool("hasOverlay", false);
                maskShader->setBool("hasNormalMap", false);
                maskShader->setInt("lightCount", 0);
                maskShader->setFloat("mixAmount", 0.0f);
                maskShader->setVec3("materialColor", glm::vec3(1.0f));
                maskShader->setFloat("ambientStrength", 1.0f);
                maskShader->setFloat("specularStrength", 0.0f);
                maskShader->setFloat("shininess", 1.0f);
                maskShader->setInt("texture1", 0);
                maskShader->setInt("overlayTex", 1);
                maskShader->setInt("normalMap", 2);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, debugWhiteTexture ? debugWhiteTexture : (texture1 ? texture1->GetID() : 0));
            }

            if (drawItem.wantsGpuSkinning && drawItem.obj->hasSkeletalAnimation && drawItem.obj->skeletal.enabled) {
                int boneCount = std::min<int>(static_cast<int>(drawItem.obj->skeletal.finalMatrices.size()),
                                              std::max(0, drawItem.obj->skeletal.maxBones));
                if (boneCount > 0) {
                    maskShader->setInt("boneCount", boneCount);
                    maskShader->setMat4Array("bones", drawItem.obj->skeletal.finalMatrices.data(), boneCount);
                    maskShader->setBool("useSkinning", true);
                } else {
                    maskShader->setBool("useSkinning", false);
                }
            } else {
                maskShader->setBool("useSkinning", false);
            }

            drawItem.mesh->draw();
        }

        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, currentWidth, currentHeight);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        outlineShader->use();
        outlineShader->setInt("maskTex", 0);
        outlineShader->setVec2("texelSize", glm::vec2(1.0f / (float)currentWidth, 1.0f / (float)currentHeight));
        outlineShader->setVec3("outlineColor", glm::vec3(0.48f, 0.43f, 1.0f));
        outlineShader->setFloat("outlineRadiusPx", 2.7f);
        outlineShader->setFloat("outlineSoftnessPx", 1.15f);
        outlineShader->setFloat("outlineOpacity", 0.95f * selectionOutlineBlend * passWeight);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, selectionMaskTarget.texture);
        drawFullscreenQuad();
    };

    float currentPassWeight = 1.0f;
    float previousPassWeight = 0.0f;
    if (!previousDrawItems.empty() && selectionOutlineSwapBlend < 0.999f) {
        previousPassWeight = 1.0f - selectionOutlineSwapBlend;
        currentPassWeight = selectionOutlineSwapBlend;
    }

    drawOutlinePass(previousDrawItems, previousPassWeight);
    drawOutlinePass(currentDrawItems, currentPassWeight);

    glColorMask(prevColorMask[0], prevColorMask[1], prevColorMask[2], prevColorMask[3]);
    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(depthMask);
    if (cullFace) {
        glEnable(GL_CULL_FACE);
        glCullFace(prevCullMode);
    } else {
        glDisable(GL_CULL_FACE);
    }
    if (blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFuncSeparate(prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrcAlpha, prevBlendDstAlpha);
    if (stencilEnabled) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    if (polyOffsetFill) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(prevPolyOffsetFactor, prevPolyOffsetUnits);
    glDepthFunc(prevDepthFunc);
    glPolygonMode(GL_FRONT, prevPoly[0]);
    glPolygonMode(GL_BACK, prevPoly[1]);

    glActiveTexture(prevActiveTex);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, prevDrawFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

void Renderer::endRender() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
