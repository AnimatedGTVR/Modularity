#pragma once // future me: if you add a new material key, touch BOTH read and write.
#include "SceneObject.h"
#include <string>
struct MaterialFileData {
    MaterialProperties props;
    std::string albedo;
    std::string overlay;
    std::string normal;
    bool useOverlay = false;
    std::string shaderPack;
    std::string vertexShader;
    std::string fragmentShader;
};
bool readMaterialFile(const std::string& path, MaterialFileData& outData);
bool writeMaterialFile(const MaterialFileData& data, const std::string& path);
