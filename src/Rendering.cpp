#include "Rendering.h"
#include "Camera.h"
#include "ModelLoader.h"
#include <unordered_map>

#define TINYOBJLOADER_IMPLEMENTATION
#include "../include/ThirdParty/tiny_obj_loader.h"

// Global OBJ loader instance
OBJLoader g_objLoader;

// Cube vertex data
float vertices[] = {
    // Back face (z = -0.5f)
    -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f,
     0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f,

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
     0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,

    // Bottom face (y = -0.5f)
    -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 0.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f,

    // Top face (y = 0.5f)
    -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f
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

// Mesh implementation
Mesh::Mesh(const float* vertexData, size_t dataSizeBytes) {
    vertexCount = dataSizeBytes / (8 * sizeof(float));

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

Mesh::~Mesh() {
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
}

void Mesh::draw() const {
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glBindVertexArray(0);
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

            for (int v = 0; v < fv; v++) {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];

                TempVertex tv;
                tv.pos.x = attrib.vertices[3 * size_t(idx.vertex_index) + 0];
                tv.pos.y = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
                tv.pos.z = attrib.vertices[3 * size_t(idx.vertex_index) + 2];

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

                for (int i = 0; i < 3; i++) {
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
    delete shader;
    delete texture1;
    delete texture2;
    delete cubeMesh;
    delete sphereMesh;
    delete capsuleMesh;
    delete skybox;
    if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
    if (viewportTexture) glDeleteTextures(1, &viewportTexture);
    if (rbo) glDeleteRenderbuffers(1, &rbo);
}

Texture* Renderer::getTexture(const std::string& path) {
    if (path.empty()) return nullptr;
    auto it = textureCache.find(path);
    if (it != textureCache.end()) return it->second.get();

    auto tex = std::make_unique<Texture>(path);
    if (!tex->GetID()) {
        return nullptr;
    }
    Texture* raw = tex.get();
    textureCache[path] = std::move(tex);
    return raw;
}

void Renderer::initialize() {
    shader = new Shader("Resources/Shaders/vert.glsl", "Resources/Shaders/frag.glsl");
    if (shader->ID == 0) {
        std::cerr << "Shader compilation failed!\n";
        delete shader;
        shader = nullptr;
        throw std::runtime_error("Shader error");
    }

    texture1 = new Texture("Resources/Textures/container.jpg");
    texture2 = new Texture("Resources/Textures/awesomeface.png");

    cubeMesh = new Mesh(vertices, sizeof(vertices));

    auto sphereVerts = generateSphere();
    sphereMesh = new Mesh(sphereVerts.data(), sphereVerts.size() * sizeof(float));

    auto capsuleVerts = generateCapsule();
    capsuleMesh = new Mesh(capsuleVerts.data(), capsuleVerts.size() * sizeof(float));

    skybox = new Skybox();

    setupFBO();
    glEnable(GL_DEPTH_TEST);
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
}

void Renderer::resize(int w, int h) {
    if (w <= 0 || h <= 0 || (w == currentWidth && h == currentHeight)) return;
    
    currentWidth = w;
    currentHeight = h;
    
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    
    glBindTexture(GL_TEXTURE_2D, viewportTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, currentWidth, currentHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, currentWidth, currentHeight);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer incomplete after resize!\n";
    }
}

void Renderer::beginRender(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos) {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, currentWidth, currentHeight);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader->use();
    shader->setMat4("view", view);
    shader->setMat4("projection", proj);
    shader->setVec3("viewPos", cameraPos);
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

    shader->setMat4("model", model);
    shader->setVec3("materialColor", obj.material.color);
    shader->setFloat("ambientStrength", obj.material.ambientStrength);
    shader->setFloat("specularStrength", obj.material.specularStrength);
    shader->setFloat("shininess", obj.material.shininess);
    shader->setFloat("mixAmount", obj.material.textureMix);

    Texture* baseTex = texture1;
    if (!obj.albedoTexturePath.empty()) {
        if (auto* t = getTexture(obj.albedoTexturePath)) baseTex = t;
    }
    if (baseTex) baseTex->Bind(GL_TEXTURE0);

    bool overlayUsed = false;
    if (obj.useOverlay && !obj.overlayTexturePath.empty()) {
        if (auto* t = getTexture(obj.overlayTexturePath)) {
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
        if (auto* t = getTexture(obj.normalMapPath)) {
            t->Bind(GL_TEXTURE2);
            normalUsed = true;
        }
    }
    shader->setBool("hasNormalMap", normalUsed);

    switch (obj.type) {
        case ObjectType::Cube:
            cubeMesh->draw();
            break;
        case ObjectType::Sphere:
            sphereMesh->draw();
            break;
        case ObjectType::Capsule:
            capsuleMesh->draw();
            break;
        case ObjectType::OBJMesh:
            if (obj.meshId >= 0) {
                Mesh* objMesh = g_objLoader.getMesh(obj.meshId);
                if (objMesh) {
                    objMesh->draw();
                }
            }
            break;
        case ObjectType::Model:
            if (obj.meshId >= 0) {
                Mesh* modelMesh = getModelLoader().getMesh(obj.meshId);
                if (modelMesh) {
                    modelMesh->draw();
                }
            }
            break;
        case ObjectType::PointLight:
        case ObjectType::SpotLight:
        case ObjectType::AreaLight:
            // Lights are not rendered as geometry
            break;
        case ObjectType::DirectionalLight:
            // Not rendered as geometry
            break;
    }
}

void Renderer::renderScene(const Camera& camera, const std::vector<SceneObject>& sceneObjects) {
    if (!shader) return;
    shader->use();
    shader->setMat4("view", camera.getViewMatrix());
    shader->setMat4("projection", glm::perspective(glm::radians(FOV), (float)currentWidth / (float)currentHeight, NEAR_PLANE, FAR_PLANE));
    shader->setVec3("viewPos", camera.position);
    shader->setFloat("ambientStrength", 0.25f);
    shader->setFloat("specularStrength", 0.8f);
    shader->setFloat("shininess", 64.0f);
    shader->setFloat("mixAmount", 0.3f);

    // Collect up to 10 lights
    struct LightUniform {
        int type = 0; // 0 dir,1 point,2 spot
        glm::vec3 dir = glm::vec3(0.0f, -1.0f, 0.0f);
        glm::vec3 pos = glm::vec3(0.0f);
        glm::vec3 color = glm::vec3(1.0f);
        float intensity = 1.0f;
        float range = 10.0f;
        float inner = glm::cos(glm::radians(15.0f));
        float outer = glm::cos(glm::radians(25.0f));
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

    std::vector<LightUniform> lights;
    lights.reserve(10);

    // Add directionals first, then spots, then points
    for (const auto& obj : sceneObjects) {
        if (obj.light.enabled && obj.type == ObjectType::DirectionalLight) {
            LightUniform l;
            l.type = 0;
            l.dir = forwardFromRotation(obj);
            l.color = obj.light.color;
            l.intensity = obj.light.intensity;
            lights.push_back(l);
            if (lights.size() >= 10) break;
        }
    }
    if (lights.size() < 10) {
        for (const auto& obj : sceneObjects) {
            if (obj.light.enabled && obj.type == ObjectType::SpotLight) {
                LightUniform l;
                l.type = 2;
                l.pos = obj.position;
                l.dir = forwardFromRotation(obj);
                l.color = obj.light.color;
                l.intensity = obj.light.intensity;
                l.range = obj.light.range;
                l.inner = glm::cos(glm::radians(obj.light.innerAngle));
                l.outer = glm::cos(glm::radians(obj.light.outerAngle));
                lights.push_back(l);
                if (lights.size() >= 10) break;
            }
        }
    }
    if (lights.size() < 10) {
        for (const auto& obj : sceneObjects) {
            if (obj.light.enabled && obj.type == ObjectType::PointLight) {
                LightUniform l;
                l.type = 1;
                l.pos = obj.position;
                l.color = obj.light.color;
                l.intensity = obj.light.intensity;
                l.range = obj.light.range;
                lights.push_back(l);
                if (lights.size() >= 10) break;
            }
        }
    }
    int count = static_cast<int>(lights.size());
    shader->setInt("lightCount", count);
    for (int i = 0; i < count; ++i) {
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
    }

    // Bind base textures once per frame (used by objects)
    if (texture1) texture1->Bind(0);
    else glBindTexture(GL_TEXTURE_2D, 0);
    if (texture2) texture2->Bind(1);
    else glBindTexture(GL_TEXTURE_2D, 0);

    if (texture1) texture1->Bind(0);
    if (texture2) texture2->Bind(1);

    for (const auto& obj : sceneObjects) {
        // Skip light types in the main render pass (they are handled as gizmos)
        if (obj.type == ObjectType::PointLight || obj.type == ObjectType::SpotLight || obj.type == ObjectType::AreaLight) {
            continue;
        }

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, obj.position);
        model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, obj.scale);

        shader->setMat4("model", model);
        shader->setVec3("materialColor", obj.material.color);
        shader->setFloat("ambientStrength", obj.material.ambientStrength);
        shader->setFloat("specularStrength", obj.material.specularStrength);
        shader->setFloat("shininess", obj.material.shininess);
        shader->setFloat("mixAmount", obj.material.textureMix);

        Texture* baseTex = texture1;
        if (!obj.albedoTexturePath.empty()) {
            if (auto* t = getTexture(obj.albedoTexturePath)) baseTex = t;
        }
        if (baseTex) baseTex->Bind(GL_TEXTURE0);

        bool overlayUsed = false;
        if (obj.useOverlay && !obj.overlayTexturePath.empty()) {
            if (auto* t = getTexture(obj.overlayTexturePath)) {
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
            if (auto* t = getTexture(obj.normalMapPath)) {
                t->Bind(GL_TEXTURE2);
                normalUsed = true;
            }
        }
        shader->setBool("hasNormalMap", normalUsed);

        Mesh* meshToDraw = nullptr;
        if (obj.type == ObjectType::Cube) meshToDraw = cubeMesh;
        else if (obj.type == ObjectType::Sphere) meshToDraw = sphereMesh;
        else if (obj.type == ObjectType::Capsule) meshToDraw = capsuleMesh;
        else if (obj.type == ObjectType::OBJMesh && obj.meshId != -1) {
            meshToDraw = g_objLoader.getMesh(obj.meshId);
        } else if (obj.type == ObjectType::Model && obj.meshId != -1) {
            meshToDraw = getModelLoader().getMesh(obj.meshId);
        }

        if (meshToDraw) {
            meshToDraw->draw();
        }
    }

    if (skybox) {
        glm::mat4 view = camera.getViewMatrix();
        glm::mat4 proj = glm::perspective(glm::radians(FOV), 
                                        (float)currentWidth / currentHeight, 
                                        NEAR_PLANE, FAR_PLANE);

        skybox->draw(glm::value_ptr(view), glm::value_ptr(proj));
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::endRender() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
