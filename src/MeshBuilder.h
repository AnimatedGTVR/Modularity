#pragma once

#include "Common.h"
#include "ModelLoader.h"

#pragma region Mesh Builder State
// Lightweight mesh editing state used by the MeshBuilder panel.
class MeshBuilder {
public:
    bool hasMesh = false;
    RawMeshAsset mesh;
    std::string loadedPath;
    bool dirty = false;
    int selectedVertex = -1;
#pragma endregion

#pragma region Mesh Builder API
    bool load(const std::string& path, std::string& error);
    bool save(const std::string& path, std::string& error);
    void clear();
    void recomputeNormals();

    // Add a new face defined by vertex indices (3 = triangle, 4 = quad fan).
    bool addFace(const std::vector<uint32_t>& indices, std::string& error);
};
#pragma endregion
