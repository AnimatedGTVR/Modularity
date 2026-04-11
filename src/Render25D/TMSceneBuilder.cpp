#include "Render25D/TMSceneBuilder.h"

#include <array>
#include <cctype>

namespace Modularity::Render25D {

namespace {

std::string ToLowerCopy(const std::string& value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

bool IsPseudo3DOverlayObject(const SceneObject& obj) {
    return obj.type == ObjectType::Sprite25D;
}

bool IsWorldRenderableObject(const SceneObject& obj) {
    if (!IsObjectEnabledInHierarchy(obj)) {
        return false;
    }
    if (HasUIComponent(obj) && !IsPseudo3DOverlayObject(obj)) {
        return false;
    }
    return true;
}

bool IsMMeshObject(const SceneObject& obj) {
    if (!IsWorldRenderableObject(obj) || !obj.hasRenderer) {
        return false;
    }
    if (obj.renderType != RenderType::Model && obj.renderType != RenderType::OBJMesh) {
        return false;
    }
    return ToLowerCopy(fs::path(obj.meshPath).extension().string()) == ".mmesh";
}

bool IsFloorPlaneObject(const SceneObject& obj) {
    return IsWorldRenderableObject(obj) &&
           obj.hasRenderer &&
           obj.renderType == RenderType::Plane;
}

glm::vec3 ResolveObjectHalfExtents(const SceneObject& obj) {
    const glm::vec3 absoluteScale = glm::abs(obj.scale);
    return glm::max(absoluteScale * 0.5f, glm::vec3(0.5f));
}

glm::vec2 ResolvePlaneHalfExtents(const SceneObject& obj) {
    std::array<float, 3> components = {
        std::abs(obj.scale.x),
        std::abs(obj.scale.y),
        std::abs(obj.scale.z)
    };
    std::sort(components.begin(), components.end(), std::greater<float>());
    return glm::vec2(std::max(0.5f, components[0] * 0.5f),
                     std::max(0.5f, components[1] * 0.5f));
}

TMTextureFilter ToTMTextureFilter(MaterialProperties::TextureFilter filter) {
    return (filter == MaterialProperties::TextureFilter::Point)
               ? TMTextureFilter::Point
               : TMTextureFilter::Bilinear;
}

float BuildStableWobbleSeed(const SceneObject& obj) {
    const uint32_t id = static_cast<uint32_t>(std::max(0, obj.id));
    return static_cast<float>((id * 747796405u) ^ 2891336453u) * 0.0000001f;
}

} // namespace

void TMSceneBuilder::buildFromSceneObjects(const std::vector<SceneObject>& sceneObjects,
                                           TMScene& outScene,
                                           BuildStats& outStats) const {
    outScene = TMScene();
    outStats = BuildStats();

    TMSegment segment;
    segment.id = 0;
    segment.name = "DefaultSegment";
    segment.floorSurface.enabled = false;
    segment.floorSurface.height = 0.0f;
    segment.floorSurface.uvScale = glm::vec2(0.125f);
    segment.floorSurface.maxDistance = 128.0f;
    segment.floorSurface.perspectiveStrength = 1.0f;

    glm::vec3 boundsMin(FLT_MAX);
    glm::vec3 boundsMax(-FLT_MAX);
    bool hasBounds = false;

    for (const SceneObject& obj : sceneObjects) {
        if (!IsWorldRenderableObject(obj)) {
            continue;
        }

        const glm::vec3 halfExtents = ResolveObjectHalfExtents(obj);
        boundsMin = glm::min(boundsMin, obj.position - halfExtents - glm::vec3(2.0f, 0.0f, 2.0f));
        boundsMax = glm::max(boundsMax, obj.position + halfExtents + glm::vec3(2.0f, 2.0f, 2.0f));
        hasBounds = true;

        if (!IsFloorPlaneObject(obj)) {
            continue;
        }

        outStats.hasExplicitFloor = true;
        segment.floorSurface.height = obj.position.y;
        if (segment.floorSurface.texturePath.empty() && !obj.albedoTexturePath.empty()) {
            segment.floorSurface.texturePath = obj.albedoTexturePath;
        }

        segment.floorSurface.enabled = true;
        const glm::vec2 planeHalfExtents = ResolvePlaneHalfExtents(obj);
        boundsMin.x = std::min(boundsMin.x, obj.position.x - planeHalfExtents.x);
        boundsMin.z = std::min(boundsMin.z, obj.position.z - planeHalfExtents.y);
        boundsMax.x = std::max(boundsMax.x, obj.position.x + planeHalfExtents.x);
        boundsMax.z = std::max(boundsMax.z, obj.position.z + planeHalfExtents.y);
        segment.floorSurface.uvScale = glm::vec2(
            1.0f / std::max(1.0f, planeHalfExtents.x * 2.0f),
            1.0f / std::max(1.0f, planeHalfExtents.y * 2.0f));
    }

    if (!hasBounds) {
        boundsMin = glm::vec3(-16.0f, -2.0f, -16.0f);
        boundsMax = glm::vec3(16.0f, 8.0f, 16.0f);
    }

    segment.boundsMin = glm::vec3(boundsMin.x, std::min(boundsMin.y, segment.floorSurface.height - 2.0f), boundsMin.z);
    segment.boundsMax = glm::vec3(boundsMax.x, std::max(boundsMax.y, segment.floorSurface.height + 8.0f), boundsMax.z);
    outScene.segments.push_back(segment);
    outStats.segmentCount = 1;

    for (const SceneObject& obj : sceneObjects) {
        if (!IsMMeshObject(obj)) {
            continue;
        }

        TMSectorModelInstance instance;
        instance.segmentIndex = 0;
        instance.name = obj.name.empty() ? "SectorModel" : obj.name;
        instance.meshAssetPath = obj.meshPath;
        instance.textureOverridePath = obj.albedoTexturePath;
        instance.position = obj.position;
        instance.rotationEuler = obj.rotation;
        instance.scale = glm::max(glm::abs(obj.scale), glm::vec3(0.0001f));
        instance.baseHeight = 0.0f;
        instance.enabled = true;
        instance.presentation.colorTint = glm::vec4(obj.material.color, obj.material.alpha);
        instance.presentation.textureFilter = ToTMTextureFilter(obj.material.textureFilter);
        instance.wobble.seed = BuildStableWobbleSeed(obj);
        instance.wobble.offset = obj.position;
        outScene.sectorModels.push_back(std::move(instance));
    }

    outStats.modelCount = static_cast<uint32_t>(outScene.sectorModels.size());
}

} // namespace Modularity::Render25D
