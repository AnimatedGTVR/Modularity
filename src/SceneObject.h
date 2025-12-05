#pragma once

#include "Common.h"

enum class ObjectType {
    Cube,
    Sphere,
    Capsule,
    OBJMesh,
    Model  // New type for Assimp-loaded models (FBX, GLTF, etc.)
};

enum class ConsoleMessageType {
    Info,
    Warning,
    Error,
    Success
};

class SceneObject {
public:
    std::string name;
    ObjectType type;
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
    int id;
    int parentId = -1;
    std::vector<int> childIds;
    bool isExpanded = true;
    std::string meshPath;  // Path to OBJ file (for OBJMesh type)
    int meshId = -1;       // Index into loaded meshes cache

    SceneObject(const std::string& name, ObjectType type, int id)
        : name(name), type(type), position(0.0f), rotation(0.0f), scale(1.0f), id(id) {}
};
