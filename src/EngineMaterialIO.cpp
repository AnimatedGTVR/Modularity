#include "EngineMaterialIO.h"
#include "MaterialAssetUtils.h"
#include <algorithm>
#include <cstdio>
bool readMaterialFile(const std::string& path, MaterialFileData& outData) {
    std::ifstream f(path); if (!f.is_open()) {return false;} std::string line;
    while (std::getline(f, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        if (key == "color") {sscanf(val.c_str(), "%f,%f,%f", &outData.props.color.r, &outData.props.color.g, &outData.props.color.b);}
        else if (key == "opacity" || key == "alpha") {outData.props.alpha = std::clamp(std::stof(val), 0.0f, 1.0f);}
        else if (key == "ambient")          {outData.props.ambientStrength = std::stof(val);}
        else if (key == "specular")         {outData.props.specularStrength = std::stof(val);}
        else if (key == "shininess")        {outData.props.shininess = std::stof(val);}
        else if (key == "normalIntensity" || key == "normalMapIntensity") {outData.props.normalMapIntensity = std::clamp(std::stof(val), 0.0f, 2.0f);}
        else if (key == "textureMix")       {outData.props.textureMix = std::stof(val);}
        else if (key == "uvTiling")         {sscanf(val.c_str(), "%f,%f", &outData.props.uvTiling.x, &outData.props.uvTiling.y);}
        else if (key == "uvOffset")         {sscanf(val.c_str(), "%f,%f", &outData.props.uvOffset.x, &outData.props.uvOffset.y);}
        else if (key == "scrollSpeed")      {outData.props.scrollSpeed = std::max(0.0f, std::stof(val));}
        else if (key == "scrollDirection")  {sscanf(val.c_str(), "%f,%f", &outData.props.scrollDirection.x, &outData.props.scrollDirection.y);}
        else if (key == "uvScrollEnabled") {outData.props.uvScrollEnabled = (val == "1" || val == "true");}
        else if (key == "cloudColor")       {sscanf(val.c_str(), "%f,%f,%f", &outData.props.cloudColor.r, &outData.props.cloudColor.g, &outData.props.cloudColor.b);}
        else if (key == "cloudSkyColor")    {sscanf(val.c_str(), "%f,%f,%f", &outData.props.cloudSkyColor.r, &outData.props.cloudSkyColor.g, &outData.props.cloudSkyColor.b);}
        else if (key == "cloudScale")       {outData.props.cloudScale = std::max(0.0001f, std::stof(val));}
        else if (key == "cloudCoverage")    {outData.props.cloudCoverage = std::clamp(std::stof(val), 0.0f, 1.0f);}
        else if (key == "cloudSoftness")    {outData.props.cloudSoftness = std::clamp(std::stof(val), 0.001f, 1.0f);}
        else if (key == "cloudDetail")      {outData.props.cloudDetail = std::clamp(std::stoi(val), 1, 8);}
        else if (key == "cloudSpeed")       {outData.props.cloudSpeed = std::stof(val);}
        else if (key == "cloudWarp")        {outData.props.cloudWarp = std::max(0.0f, std::stof(val));}
        else if (key == "cloudHighlight")   {outData.props.cloudHighlight = std::max(0.0f, std::stof(val));}
        else if (key == "cloudStars")       {outData.props.cloudStars = std::clamp(std::stof(val), 0.0f, 1.0f);}
        else if (key == "cloudHorizon")     {outData.props.cloudHorizon = std::clamp(std::stof(val), 0.0f, 1.0f);}
        else if (key == "textureFilter") {
            std::string lower = val;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {return static_cast<char>(std::tolower(c));});
            if (lower == "1" || lower == "point" || lower == "nearest") {outData.props.textureFilter = MaterialProperties::TextureFilter::Point;}
            else                            {outData.props.textureFilter = MaterialProperties::TextureFilter::Bilinear;}}
        else if (key == "albedo")           {outData.albedo = val;}
        else if (key == "overlay")          {outData.overlay = val;}
        else if (key == "normal")           {outData.normal = val;}
        else if (key == "useOverlay")       {outData.useOverlay = std::stoi(val) != 0;}
        else if (key == "shaderPack")       {outData.shaderPack = ResolveShaderPackReferencedPath(fs::path(path), val).string();}
        else if (key == "vertexShader")     {outData.vertexShader = val;}
        else if (key == "fragmentShader")   {outData.fragmentShader = val;} }
    if (!outData.shaderPack.empty()) {
        ShaderPackAssetData shaderPackData;
        if (ReadShaderPackFile(outData.shaderPack, shaderPackData)) {outData.vertexShader = shaderPackData.vertexShaderPath; outData.fragmentShader = shaderPackData.fragmentShaderPath;}}
    return true;
}
bool writeMaterialFile(const MaterialFileData& data, const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "# Material\n";
    f << "color=" << data.props.color.r << "," << data.props.color.g << "," << data.props.color.b << "\n";
    f << "opacity=" << data.props.alpha << "\n";
    f << "ambient=" << data.props.ambientStrength << "\n";
    f << "specular=" << data.props.specularStrength << "\n";
    f << "shininess=" << data.props.shininess << "\n";
    f << "normalMapIntensity=" << data.props.normalMapIntensity << "\n";
    f << "textureMix=" << data.props.textureMix << "\n";
    f << "uvTiling=" << data.props.uvTiling.x << "," << data.props.uvTiling.y << "\n";
    f << "uvOffset=" << data.props.uvOffset.x << "," << data.props.uvOffset.y << "\n";
    f << "scrollSpeed=" << data.props.scrollSpeed << "\n";
    f << "scrollDirection=" << data.props.scrollDirection.x << "," << data.props.scrollDirection.y << "\n";
    f << "uvScrollEnabled=" << (data.props.uvScrollEnabled ? 1 : 0) << "\n";
    f << "cloudColor=" << data.props.cloudColor.r << "," << data.props.cloudColor.g << "," << data.props.cloudColor.b << "\n";
    f << "cloudSkyColor=" << data.props.cloudSkyColor.r << "," << data.props.cloudSkyColor.g << "," << data.props.cloudSkyColor.b << "\n";
    f << "cloudScale=" << data.props.cloudScale << "\n";
    f << "cloudCoverage=" << data.props.cloudCoverage << "\n";
    f << "cloudSoftness=" << data.props.cloudSoftness << "\n";
    f << "cloudDetail=" << data.props.cloudDetail << "\n";
    f << "cloudSpeed=" << data.props.cloudSpeed << "\n";
    f << "cloudWarp=" << data.props.cloudWarp << "\n";
    f << "cloudHighlight=" << data.props.cloudHighlight << "\n";
    f << "cloudStars=" << data.props.cloudStars << "\n";
    f << "cloudHorizon=" << data.props.cloudHorizon << "\n";
    f << "textureFilter=" << static_cast<int>(data.props.textureFilter) << "\n";
    f << "useOverlay=" << (data.useOverlay ? 1 : 0) << "\n";
    f << "albedo=" << data.albedo << "\n";
    f << "overlay=" << data.overlay << "\n";
    f << "normal=" << data.normal << "\n";
    f << "shaderPack=" << data.shaderPack << "\n";
    f << "vertexShader=" << data.vertexShader << "\n";
    f << "fragmentShader=" << data.fragmentShader << "\n";
    return true;}
