#include "Rendering.h"
#include "Profiler.h"
#include "Modu2DStats.h"
#include "Camera.h"
#include "ModelLoader.h"
#include "../include/Platform/AssetSource.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <future>
#include <limits>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#define TINYOBJLOADER_IMPLEMENTATION
#include "../include/ThirdParty/tiny_obj_loader.h"

extern float vertices[288];
extern float mirrorPlaneVertices[48];

namespace {
struct Runtime2DRenderCounters {
    uint64_t textureBindCount = 0;
    uint64_t stateBindCount = 0;
};

Runtime2DRenderCounters gRuntime2DRenderCounters;

inline void Runtime2DCountTextureBind() {
    ++gRuntime2DRenderCounters.textureBindCount;
}

inline void Runtime2DCountStateBind() {
    ++gRuntime2DRenderCounters.stateBindCount;
}

constexpr int kUiAtlasPadding = 2;

uint64_t BuildUiAtlasKey(int layer, MaterialProperties::TextureFilter filter) {
    const uint64_t layerBits = static_cast<uint64_t>(static_cast<uint32_t>(layer));
    const uint64_t filterBits = static_cast<uint64_t>(filter == MaterialProperties::TextureFilter::Point ? 1u : 0u);
    return (layerBits << 32) | filterBits;
}

GLint ToGLTextureFilter(MaterialProperties::TextureFilter filter) {
    return filter == MaterialProperties::TextureFilter::Point ? GL_NEAREST : GL_LINEAR;
}

int NextPowerOfTwoAtLeast(int value) {
    int result = 1;
    while (result < value && result < 16384) {
        result <<= 1;
    }
    return result;
}

glm::vec4 BuildUiTargetUvTransform(int x, int y, int width, int height, int textureWidth, int textureHeight) {
    const float invW = 1.0f / static_cast<float>(std::max(1, textureWidth));
    const float invH = 1.0f / static_cast<float>(std::max(1, textureHeight));
    const float safeW = static_cast<float>(std::max(1, width));
    const float safeH = static_cast<float>(std::max(1, height));
    const float uOffset = (static_cast<float>(x) + 0.5f) * invW;
    const float vOffset = 1.0f - (static_cast<float>(y) + safeH - 0.5f) * invH;
    const float uScale = std::max(1.0f, safeW - 1.0f) * invW;
    const float vScale = std::max(1.0f, safeH - 1.0f) * invH;
    return glm::vec4(uScale, vScale, uOffset, vOffset);
}

inline void SetDepthOnlyFramebufferDrawBuffer()
{
#if MODULARITY_OPENGL_ES
    const GLenum noColorAttachments = GL_NONE;
    glDrawBuffers(1, &noColorAttachments);
#else
    glDrawBuffer(GL_NONE);
#endif
}

glm::vec4 BuildSpriteUvRect(const SceneObject& obj) {
    if (obj.ui.spriteCustomFramesEnabled &&
        !obj.ui.spriteCustomFrames.empty() &&
        obj.ui.spriteSourceWidth > 0 &&
        obj.ui.spriteSourceHeight > 0) {
        const int frame = std::clamp(obj.ui.spriteSheetFrame, 0, static_cast<int>(obj.ui.spriteCustomFrames.size()) - 1);
        const glm::ivec4 rect = obj.ui.spriteCustomFrames[frame];
        const float invW = 1.0f / static_cast<float>(obj.ui.spriteSourceWidth);
        const float invH = 1.0f / static_cast<float>(obj.ui.spriteSourceHeight);
        const float u0 = rect.x * invW;
        const float vBottom = 1.0f - static_cast<float>(rect.y + rect.w) * invH;
        return glm::vec4(u0, vBottom, rect.z * invW, rect.w * invH);
    }

    if (!obj.ui.spriteSheetEnabled) {
        return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    }

    const int columns = std::max(1, obj.ui.spriteSheetColumns);
    const int rows = std::max(1, obj.ui.spriteSheetRows);
    const int total = std::max(1, columns * rows);
    const int frame = std::clamp(obj.ui.spriteSheetFrame, 0, total - 1);
    const int col = frame % columns;
    const int row = frame / columns;

    const float u0 = static_cast<float>(col) / static_cast<float>(columns);
    const float v0 = 1.0f - static_cast<float>(row + 1) / static_cast<float>(rows);
    const float uSize = 1.0f / static_cast<float>(columns);
    const float vSize = 1.0f / static_cast<float>(rows);
    return glm::vec4(u0, v0, uSize, vSize);
}

bool HasDefaultSpriteUvRect(const SceneObject& obj) {
    const glm::vec4 uvRect = BuildSpriteUvRect(obj);
    return std::abs(uvRect.x) <= 1e-5f &&
           std::abs(uvRect.y) <= 1e-5f &&
           std::abs(uvRect.z - 1.0f) <= 1e-5f &&
           std::abs(uvRect.w - 1.0f) <= 1e-5f;
}

bool HasMeaningfulSpriteFrameScales(const UIElementComponent& ui) {
    if (ui.spriteCustomFrameScales.size() != ui.spriteCustomFrames.size() ||
        ui.spriteCustomFrameScales.empty()) {
        return false;
    }
    for (const glm::vec2& scale : ui.spriteCustomFrameScales) {
        if (std::abs(scale.x - 1.0f) > 0.0001f ||
            std::abs(scale.y - 1.0f) > 0.0001f) {
            return true;
        }
    }
    return false;
}

glm::vec2 ResolveSpriteFrameScale(const UIElementComponent& ui) {
    if (!ui.spriteCustomFramesEnabled || ui.spriteCustomFrames.empty()) {
        return glm::vec2(1.0f);
    }

    const int frameCount = static_cast<int>(ui.spriteCustomFrames.size());
    const int frame = std::clamp(ui.spriteSheetFrame, 0, frameCount - 1);
    if (HasMeaningfulSpriteFrameScales(ui)) {
        const glm::vec2 authored = ui.spriteCustomFrameScales[static_cast<size_t>(frame)];
        return glm::vec2(std::max(0.01f, authored.x), std::max(0.01f, authored.y));
    }

    const glm::ivec4& referenceRect = ui.spriteCustomFrames.front();
    const glm::ivec4& frameRect = ui.spriteCustomFrames[static_cast<size_t>(frame)];
    const float referenceWidth = static_cast<float>(std::max(1, referenceRect.z));
    const float referenceHeight = static_cast<float>(std::max(1, referenceRect.w));
    return glm::vec2(static_cast<float>(std::max(1, frameRect.z)) / referenceWidth,
                     static_cast<float>(std::max(1, frameRect.w)) / referenceHeight);
}

bool IsMaskedSprite25DUiLayerObject(const SceneObject& obj) {
    return obj.type == ObjectType::Sprite25D &&
           obj.hasUI &&
           (obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D);
}

bool ProjectSprite25DProxyHalfSizeNdc(const SceneObject& obj,
                                      const glm::mat4& view,
                                      const glm::mat4& proj,
                                      float viewportWidth,
                                      float viewportHeight,
                                      glm::vec2& outHalfSizeNdc) {
    const glm::mat4 invView = glm::inverse(view);
    glm::vec3 cameraRight = glm::vec3(invView[0]);
    glm::vec3 cameraUp = glm::vec3(invView[1]);
    if (glm::dot(cameraRight, cameraRight) <= 1e-6f ||
        glm::dot(cameraUp, cameraUp) <= 1e-6f) {
        return false;
    }
    cameraRight = glm::normalize(cameraRight);
    cameraUp = glm::normalize(cameraUp);

    const glm::vec2 frameScale = ResolveSpriteFrameScale(obj.ui);
    const glm::vec2 objectScale = glm::max(glm::abs(glm::vec2(obj.scale.x, obj.scale.y)), glm::vec2(0.01f));
    const glm::vec2 baseSize = glm::max(obj.ui.size, glm::vec2(0.01f)) * frameScale * objectScale;
    const glm::vec2 worldHalfExtents = baseSize * 0.005f;

    auto projectNdc = [&](const glm::vec3& point, glm::vec3& outNdc) {
        const glm::vec4 clip = proj * view * glm::vec4(point, 1.0f);
        if (clip.w <= 0.0f || std::abs(clip.w) <= 1e-5f) {
            return false;
        }
        outNdc = glm::vec3(clip) / clip.w;
        return true;
    };

    glm::vec3 centerNdc(0.0f);
    glm::vec3 rightNdc(0.0f);
    glm::vec3 upNdc(0.0f);
    if (!projectNdc(obj.position, centerNdc) ||
        !projectNdc(obj.position + cameraRight * worldHalfExtents.x, rightNdc) ||
        !projectNdc(obj.position + cameraUp * worldHalfExtents.y, upNdc)) {
        return false;
    }

    const float minHalfWidthNdc = 1.0f / std::max(1.0f, viewportWidth);
    const float minHalfHeightNdc = 1.0f / std::max(1.0f, viewportHeight);
    outHalfSizeNdc = glm::vec2(std::max(minHalfWidthNdc, std::abs(rightNdc.x - centerNdc.x)),
                               std::max(minHalfHeightNdc, std::abs(upNdc.y - centerNdc.y)));
    return true;
}

glm::vec4 BuildSurfaceUvTransform(const SceneObject* obj, const MaterialProperties& material) {
    glm::vec2 tiling = material.uvTiling;
    glm::vec2 offset = material.uvOffset;

    if (obj != nullptr &&
        obj->runtimeHasAlbedoTextureOverride &&
        obj->runtimeAlbedoTextureOverrideId != 0 &&
        obj->runtimeAlbedoTextureFlipX) {
        offset.x += tiling.x;
        tiling.x = -tiling.x;
    }
    if (obj != nullptr &&
        obj->runtimeHasAlbedoTextureOverride &&
        obj->runtimeAlbedoTextureOverrideId != 0 &&
        obj->runtimeAlbedoTextureFlipY) {
        offset.y += tiling.y;
        tiling.y = -tiling.y;
    }

    return glm::vec4(tiling, offset);
}

glm::mat4 BuildSceneObjectModelMatrix(const SceneObject& obj, const glm::vec3* cameraPosition = nullptr, const glm::vec3* cameraUp = nullptr) {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, obj.position);

    if (obj.renderType == RenderType::Sprite && obj.faceCamera && cameraPosition != nullptr) {
        glm::vec3 forward = *cameraPosition - obj.position;
        if (glm::dot(forward, forward) < 1e-6f) {
            forward = glm::vec3(0.0f, 0.0f, 1.0f);
        } else {
            forward = glm::normalize(forward);
        }

        glm::vec3 up = (cameraUp != nullptr && glm::dot(*cameraUp, *cameraUp) > 1e-6f)
            ? glm::normalize(*cameraUp)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::cross(up, forward);
        if (glm::dot(right, right) < 1e-6f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
            right = glm::cross(up, forward);
        }
        right = glm::normalize(right);
        up = glm::normalize(glm::cross(forward, right));

        const glm::vec3 signedScale = obj.scale;
        const glm::vec3 absScale = glm::max(glm::abs(signedScale), glm::vec3(0.0001f));
        model[0] = glm::vec4(right * absScale.x * (signedScale.x < 0.0f ? -1.0f : 1.0f), 0.0f);
        model[1] = glm::vec4(up * absScale.y * (signedScale.y < 0.0f ? -1.0f : 1.0f), 0.0f);
        model[2] = glm::vec4(forward * absScale.z * (signedScale.z < 0.0f ? -1.0f : 1.0f), 0.0f);
        return model;
    }

    model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, obj.scale);
    return model;
}

PostFXSettings MakeNeutralPostFXSettings() {
    PostFXSettings settings;
    settings.enabled = false;
    settings.isGlobal = true;
    settings.priority = 0.0f;
    settings.blendWeight = 1.0f;
    settings.blendRadius = 4.0f;
    settings.hdrEnabled = true;
    settings.toneMapper = PostFXToneMapper::ACES;
    settings.whitePoint = 4.0f;
    settings.gamma = 2.2f;
    settings.bloomEnabled = false;
    settings.bloomThreshold = 1.0f;
    settings.bloomSoftKnee = 0.25f;
    settings.bloomIntensity = 0.0f;
    settings.bloomRadius = 1.0f;
    settings.colorAdjustEnabled = false;
    settings.exposure = 0.0f;
    settings.contrast = 1.0f;
    settings.saturation = 1.0f;
    settings.colorFilter = glm::vec3(1.0f);
    settings.motionBlurEnabled = false;
    settings.motionBlurStrength = 0.0f;
    settings.motionBlurThreshold = 0.04f;
    settings.motionBlurClamp = 0.35f;
    settings.vignetteEnabled = false;
    settings.vignetteIntensity = 0.0f;
    settings.vignetteSmoothness = 0.25f;
    settings.chromaticAberrationEnabled = false;
    settings.chromaticAmount = 0.0f;
    settings.sharpenEnabled = false;
    settings.sharpenStrength = 0.0f;
    settings.ambientOcclusionEnabled = false;
    settings.aoRadius = 0.0035f;
    settings.aoStrength = 0.0f;
    settings.ditherEnabled = false;
    settings.ditherIntensity = 0.0f;
    settings.ditherColorBits = 8;
    settings.ditherDarkAdjustment = 0.0f;
    settings.ditherPixelation = 0.0f;
    settings.ditherSize = 1.0f;
    settings.ditherContrast = 0.35f;
    settings.ditherOffset = 0.0f;
    settings.ditherPalette = PostFXDitherPalette::FullColor;
    settings.ditherPattern = PostFXDitherPattern::HybridPS1;
    settings.staticEnabled = false;
    settings.staticIntensity = 0.0f;
    settings.staticGrainScale = 1.0f;
    settings.staticDarkAreaInfluence = 0.0f;
    settings.staticSpeed = 0.0f;
    settings.staticMonochrome = false;
    settings.staticSparkle = 0.0f;
    settings.staticDistortionEnabled = false;
    settings.staticDistortionHorizontalJitterAmount = 0.0f;
    settings.staticDistortionLineDensity = 128.0f;
    settings.staticDistortionGlitchFrequency = 0.0f;
    settings.staticDistortionStrength = 0.0f;
    settings.lensDistortionEnabled = false;
    settings.lensDistortionAmount = 0.0f;
    settings.lensDistortionEdgeFalloff = 0.75f;
    settings.lensDistortionCenterOffset = glm::vec2(0.0f);
    settings.lensDistortionEdgeVignetteEnabled = false;
    settings.lensDistortionEdgeVignetteIntensity = 0.0f;
    settings.lensDistortionEdgeVignetteRadius = 0.9f;
    settings.lensDistortionEdgeVignetteSoftness = 0.25f;
    settings.lensDistortionEdgeVignetteColor = glm::vec3(0.0f);
    settings.pixelationEnabled = false;
    settings.pixelationSize = 1.0f;
    settings.posterizeEnabled = false;
    settings.posterizeLevels = 8;
    settings.scanlinesEnabled = false;
    settings.scanlinesIntensity = 0.0f;
    settings.scanlinesDensity = 1.0f;
    settings.scanlinesSpeed = 0.0f;
    settings.vhsOverlayEnabled = false;
    settings.vhsOverlayOpacity = 0.0f;
    settings.vhsOverlayScanlineStrength = 0.0f;
    settings.vhsOverlayTapeNoise = 0.0f;
    settings.vhsOverlayChromaBleed = 0.0f;
    settings.vhsOverlayBottomNoiseBandHeight = 0.0f;
    settings.vhsOverlayBottomNoiseBandIntensity = 0.0f;
    settings.vhsOverlayDistortionStrength = 0.0f;
    settings.vhsOverlayAnimationSpeed = 0.0f;
    settings.vhsOverlayColorBleed = 0.0f;
    settings.vhsOverlayBanding = 0.0f;
    settings.vhsOverlaySignalMode = PostFXVhsSignalMode::VhsSP;
    settings.vhsOverlayDropouts = 0.0f;
    settings.wavyEnabled = false;
    settings.wavyAmplitude = 0.0f;
    settings.wavyFrequency = 16.0f;
    settings.wavySpeed = 0.0f;
    settings.wavyVertical = false;
    return settings;
}

void AttenuatePostFXStrengths(PostFXSettings& settings, float weight) {
    const float t = glm::clamp(weight, 0.0f, 1.0f);
    settings.bloomIntensity *= t;
    settings.exposure *= t;
    settings.contrast = glm::mix(1.0f, settings.contrast, t);
    settings.saturation = glm::mix(1.0f, settings.saturation, t);
    settings.colorFilter = glm::mix(glm::vec3(1.0f), settings.colorFilter, t);
    settings.motionBlurStrength *= t;
    settings.vignetteIntensity *= t;
    settings.chromaticAmount *= t;
    settings.sharpenStrength *= t;
    settings.aoStrength *= t;

    settings.ditherIntensity *= t;
    settings.ditherColorBits = std::clamp(
        static_cast<int>(std::round(glm::mix(
            8.0f, static_cast<float>(settings.ditherColorBits), t))),
        1, 8);
    settings.ditherDarkAdjustment *= t;
    settings.ditherPixelation *= t;
    settings.ditherSize = glm::mix(1.0f, settings.ditherSize, t);
    settings.ditherContrast *= t;
    settings.ditherOffset *= t;

    settings.staticIntensity *= t;
    settings.staticDarkAreaInfluence *= t;
    settings.staticSparkle *= t;
    settings.staticDistortionHorizontalJitterAmount *= t;
    settings.staticDistortionGlitchFrequency *= t;
    settings.staticDistortionStrength *= t;

    settings.lensDistortionAmount *= t;
    settings.lensDistortionCenterOffset *= t;
    settings.lensDistortionEdgeVignetteIntensity *= t;
    settings.pixelationSize = glm::mix(1.0f, settings.pixelationSize, t);
    settings.posterizeLevels = std::clamp(
        static_cast<int>(std::round(glm::mix(
            64.0f, static_cast<float>(settings.posterizeLevels), t))),
        2, 64);
    settings.scanlinesIntensity *= t;

    settings.vhsOverlayOpacity *= t;
    settings.vhsOverlayScanlineStrength *= t;
    settings.vhsOverlayTapeNoise *= t;
    settings.vhsOverlayChromaBleed *= t;
    settings.vhsOverlayBottomNoiseBandHeight *= t;
    settings.vhsOverlayBottomNoiseBandIntensity *= t;
    settings.vhsOverlayDistortionStrength *= t;
    settings.vhsOverlayColorBleed *= t;
    settings.vhsOverlayBanding *= t;
    settings.vhsOverlayDropouts *= t;
    settings.wavyAmplitude *= t;
}

float ComputePostFXVolumeSpatialWeight(const SceneObject& obj, const glm::vec3& cameraPosition) {
    if (!obj.hasPostFX || !obj.postFx.enabled) return 0.0f;

    if (obj.postFx.isGlobal) return 1.0f;

    SceneObject safeObj = obj;
    safeObj.scale.x = (std::abs(safeObj.scale.x) < 0.001f) ? (safeObj.scale.x < 0.0f ? -0.001f : 0.001f) : safeObj.scale.x;
    safeObj.scale.y = (std::abs(safeObj.scale.y) < 0.001f) ? (safeObj.scale.y < 0.0f ? -0.001f : 0.001f) : safeObj.scale.y;
    safeObj.scale.z = (std::abs(safeObj.scale.z) < 0.001f) ? (safeObj.scale.z < 0.0f ? -0.001f : 0.001f) : safeObj.scale.z;
    glm::mat4 world = BuildSceneObjectModelMatrix(safeObj);
    glm::mat4 invWorld = glm::inverse(world);
    glm::vec3 local = glm::vec3(invWorld * glm::vec4(cameraPosition, 1.0f));
    glm::vec3 halfExtents(0.5f);
    glm::vec3 delta = glm::max(glm::abs(local) - halfExtents, glm::vec3(0.0f));
    float distance = glm::length(delta);
    if (!std::isfinite(distance)) return 0.0f;
    if (distance <= 0.0f) return 1.0f;

    float blendRadius = std::max(obj.postFx.blendRadius, 0.001f);
    float falloff = 1.0f - glm::clamp(distance / blendRadius, 0.0f, 1.0f);
    return falloff;
}

int CountEnabledPostEffects(const PostFXSettings& settings) {
    int count = 0;
    count += settings.hdrEnabled ? 1 : 0;
    count += settings.bloomEnabled ? 1 : 0;
    count += settings.colorAdjustEnabled ? 1 : 0;
    count += settings.motionBlurEnabled ? 1 : 0;
    count += settings.vignetteEnabled ? 1 : 0;
    count += settings.chromaticAberrationEnabled ? 1 : 0;
    count += settings.sharpenEnabled ? 1 : 0;
    count += settings.ambientOcclusionEnabled ? 1 : 0;
    count += settings.ditherEnabled ? 1 : 0;
    count += settings.staticEnabled ? 1 : 0;
    count += settings.staticDistortionEnabled ? 1 : 0;
    count += settings.lensDistortionEnabled ? 1 : 0;
    count += settings.pixelationEnabled ? 1 : 0;
    count += settings.posterizeEnabled ? 1 : 0;
    count += settings.scanlinesEnabled ? 1 : 0;
    count += settings.vhsOverlayEnabled ? 1 : 0;
    count += settings.wavyEnabled ? 1 : 0;
    return count;
}

bool HasMeaningfulToneMapping(const PostFXSettings& settings) {
    constexpr float kWhitePointDefault = 4.0f;
    constexpr float kGammaDefault = 2.2f;
    if (settings.hdrEnabled) {
        return true;
    }
    return settings.toneMapper != PostFXToneMapper::ACES ||
           std::fabs(settings.whitePoint - kWhitePointDefault) > 0.0001f ||
           std::fabs(settings.gamma - kGammaDefault) > 0.0001f;
}

// true when a GL context is current. desktop asks GLFW; on Android the EGL context is
// AndroidRuntime's and GLFW's null backend ALWAYS says nullptr, so render-loop paths must
// treat it as live or render targets / previews silently never allocate. shutdown-only
// paths keep the raw GLFW check.
static inline bool moduRenderContextLive() {
#ifdef __ANDROID__
    return true;
#else
    return glfwGetCurrentContext() != nullptr;
#endif
}

struct FrustumPlanes {
    std::array<glm::vec4, 6> planes;
};

FrustumPlanes BuildFrustumPlanes(const glm::mat4& viewProjection) {
    FrustumPlanes frustum{};
    glm::mat4 rows = glm::transpose(viewProjection);
    frustum.planes[0] = rows[3] + rows[0];
    frustum.planes[1] = rows[3] - rows[0];
    frustum.planes[2] = rows[3] + rows[1];
    frustum.planes[3] = rows[3] - rows[1];
    frustum.planes[4] = rows[3] + rows[2];
    frustum.planes[5] = rows[3] - rows[2];

    for (glm::vec4& plane : frustum.planes) {
        float len = glm::length(glm::vec3(plane));
        if (len > 1e-5f) {
            plane /= len;
        }
    }

    return frustum;
}

bool SphereInsideFrustum(const FrustumPlanes& frustum, const glm::vec3& center, float radius) {
    for (const glm::vec4& plane : frustum.planes) {
        if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) {
            return false;
        }
    }
    return true;
}

bool IsFiniteVec3(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

uint64_t HashStringFNV1a(const std::string& value) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : value) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

bool TryGetObjectLocalBounds(const SceneObject& obj, glm::vec3& outCenter, glm::vec3& outExtents) {
    outCenter = glm::vec3(0.0f);
    switch (obj.renderType) {
        case RenderType::Cube:
            outExtents = glm::vec3(0.5f);
            return true;
        case RenderType::Sphere:
            outExtents = glm::vec3(0.5f);
            return true;
        case RenderType::Capsule:
            outExtents = glm::vec3(0.25f, 0.75f, 0.25f);
            return true;
        case RenderType::Plane:
        case RenderType::Mirror:
        case RenderType::Sprite:
            outExtents = glm::vec3(0.5f, 0.5f, 0.05f);
            return true;
        case RenderType::Torus:
            outExtents = glm::vec3(0.5f, 0.15f, 0.5f);
            return true;
        case RenderType::OBJMesh:
            if (obj.meshId >= 0) {
                if (const auto* meshInfo = g_objLoader.getMeshInfo(obj.meshId)) {
                    if (IsFiniteVec3(meshInfo->boundsMin) && IsFiniteVec3(meshInfo->boundsMax)) {
                        outCenter = (meshInfo->boundsMin + meshInfo->boundsMax) * 0.5f;
                        outExtents = glm::max((meshInfo->boundsMax - meshInfo->boundsMin) * 0.5f,
                                              glm::vec3(0.001f));
                        return true;
                    }
                }
            }
            break;
        case RenderType::Model:
            if (obj.meshId >= 0) {
                if (const auto* meshInfo = getModelLoader().getMeshInfo(obj.meshId)) {
                    if (IsFiniteVec3(meshInfo->boundsMin) && IsFiniteVec3(meshInfo->boundsMax)) {
                        outCenter = (meshInfo->boundsMin + meshInfo->boundsMax) * 0.5f;
                        outExtents = glm::max((meshInfo->boundsMax - meshInfo->boundsMin) * 0.5f,
                                              glm::vec3(0.001f));
                        return true;
                    }
                }
            }
            break;
        case RenderType::None:
            break;
    }

    return false;
}

bool TryComputeObjectCullSphere(const SceneObject& obj, const glm::mat4& model,
                                glm::vec3& outCenter, float& outRadius) {
    glm::vec3 localCenter(0.0f);
    glm::vec3 localExtents(0.0f);
    if (!TryGetObjectLocalBounds(obj, localCenter, localExtents)) {
        return false;
    }

    outCenter = glm::vec3(model * glm::vec4(localCenter, 1.0f));
    glm::vec3 scaledExtents(
        localExtents.x * glm::length(glm::vec3(model[0])),
        localExtents.y * glm::length(glm::vec3(model[1])),
        localExtents.z * glm::length(glm::vec3(model[2])));
    outRadius = glm::length(scaledExtents);
    return std::isfinite(outRadius) && outRadius > 0.0f;
}

bool ProjectPlaneScreenRect(const SceneObject& obj, const glm::mat4& viewProjection,
                            int viewportWidth, int viewportHeight,
                            float& outMinX, float& outMinY, float& outMaxX, float& outMaxY) {
    const std::array<glm::vec3, 4> localCorners = {
        glm::vec3(-0.5f, -0.5f, 0.0f),
        glm::vec3( 0.5f, -0.5f, 0.0f),
        glm::vec3( 0.5f,  0.5f, 0.0f),
        glm::vec3(-0.5f,  0.5f, 0.0f)
    };

    glm::mat4 model = BuildSceneObjectModelMatrix(obj);
    bool anyProjected = false;
    outMinX = static_cast<float>(viewportWidth);
    outMinY = static_cast<float>(viewportHeight);
    outMaxX = 0.0f;
    outMaxY = 0.0f;

    for (const glm::vec3& corner : localCorners) {
        glm::vec4 clip = viewProjection * model * glm::vec4(corner, 1.0f);
        if (clip.w <= 0.0001f) {
            continue;
        }

        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        float screenX = (ndc.x * 0.5f + 0.5f) * static_cast<float>(viewportWidth);
        float screenY = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(viewportHeight);
        outMinX = std::min(outMinX, screenX);
        outMinY = std::min(outMinY, screenY);
        outMaxX = std::max(outMaxX, screenX);
        outMaxY = std::max(outMaxY, screenY);
        anyProjected = true;
    }

    if (!anyProjected) {
        return false;
    }

    outMinX = std::clamp(outMinX, 0.0f, static_cast<float>(viewportWidth));
    outMinY = std::clamp(outMinY, 0.0f, static_cast<float>(viewportHeight));
    outMaxX = std::clamp(outMaxX, 0.0f, static_cast<float>(viewportWidth));
    outMaxY = std::clamp(outMaxY, 0.0f, static_cast<float>(viewportHeight));
    return (outMaxX - outMinX) >= 1.0f && (outMaxY - outMinY) >= 1.0f;
}

int QuantizeMirrorTargetDimension(int requested, int maxDimension) {
    constexpr int kMirrorTileSize = 64;
    int clamped = std::clamp(requested, 256, maxDimension);
    int quantized = ((clamped + kMirrorTileSize - 1) / kMirrorTileSize) * kMirrorTileSize;
    return std::clamp(quantized, 256, maxDimension);
}

uint64_t HashCombine64(uint64_t seed, uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

uint64_t HashString64(const std::string& value) {
    return std::hash<std::string>{}(value);
}

uint64_t HashFloat64(float value) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return static_cast<uint64_t>(bits);
}

const std::vector<float>& GetPrimitiveTriangleVertices(RenderType type) {
    static const std::vector<float> cubeVerts(std::begin(vertices), std::end(vertices));
    static const std::vector<float> planeVerts(std::begin(mirrorPlaneVertices), std::end(mirrorPlaneVertices));
    static const std::vector<float> sphereVerts = generateSphere();
    static const std::vector<float> capsuleVerts = generateCapsule();
    static const std::vector<float> torusVerts = generateTorus();

    switch (type) {
        case RenderType::Cube: return cubeVerts;
        case RenderType::Sphere: return sphereVerts;
        case RenderType::Capsule: return capsuleVerts;
        case RenderType::Plane: return planeVerts;
        case RenderType::Torus: return torusVerts;
        default: return planeVerts;
    }
}

bool IsStaticMergeCandidate(const SceneObject& obj) {
    if (!IsObjectEnabledInHierarchy(obj) || !HasRendererComponent(obj)) return false;
    if (obj.hasUI || obj.hasLight || obj.hasCamera || obj.hasPostFX) return false;
    if (obj.hasRigidbody || obj.hasRigidbody2D || obj.hasPlayerController) return false;
    if (obj.hasVideoPlayer) return false;
    if (obj.hasAnimation || obj.hasSkeletalAnimation) return false;
    if (!obj.scripts.empty()) return false;
    if (obj.faceCamera) return false;

    switch (obj.renderType) {
        case RenderType::Cube:
        case RenderType::Sphere:
        case RenderType::Capsule:
        case RenderType::OBJMesh:
        case RenderType::Model:
        case RenderType::Plane:
        case RenderType::Sprite:
        case RenderType::Torus:
            return true;
        case RenderType::None:
        case RenderType::Mirror:
            return false;
    }

    return false;
}

bool TryGetObjectTriangleVertexStream(const SceneObject& obj, const std::vector<float>*& outVertices) {
    outVertices = nullptr;
    switch (obj.renderType) {
        case RenderType::Cube:
        case RenderType::Sphere:
        case RenderType::Capsule:
        case RenderType::Plane:
        case RenderType::Torus: {
            outVertices = &GetPrimitiveTriangleVertices(obj.renderType);
            return true;
        }
        case RenderType::OBJMesh:
            if (obj.meshId >= 0) {
                if (const auto* meshInfo = g_objLoader.getMeshInfo(obj.meshId)) {
                    if (!meshInfo->baseVertices.empty()) {
                        outVertices = &meshInfo->baseVertices;
                        return true;
                    }
                }
            }
            return false;
        case RenderType::Model:
            if (obj.meshId >= 0) {
                if (const auto* meshInfo = getModelLoader().getMeshInfo(obj.meshId)) {
                    if (!meshInfo->baseVertices.empty()) {
                        outVertices = &meshInfo->baseVertices;
                        return true;
                    }
                }
            }
            return false;
        case RenderType::None:
        case RenderType::Mirror:
        case RenderType::Sprite:
            return false;
    }

    return false;
}

void AppendTransformedTriangleVertices(const std::vector<float>& src, const glm::mat4& model, std::vector<float>& dst,
                                       glm::vec3& boundsMin, glm::vec3& boundsMax) {
    if (src.empty()) return;
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
    const size_t baseSize = dst.size();
    dst.resize(baseSize + src.size());

    for (size_t i = 0; i + 7 < src.size(); i += 8) {
        glm::vec3 pos(src[i + 0], src[i + 1], src[i + 2]);
        glm::vec3 normal(src[i + 3], src[i + 4], src[i + 5]);
        glm::vec2 uv(src[i + 6], src[i + 7]);

        glm::vec3 worldPos = glm::vec3(model * glm::vec4(pos, 1.0f));
        glm::vec3 worldNormal = glm::normalize(normalMatrix * normal);
        if (!std::isfinite(worldNormal.x) || glm::length(worldNormal) < 1e-6f) {
            worldNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        boundsMin = glm::min(boundsMin, worldPos);
        boundsMax = glm::max(boundsMax, worldPos);

        size_t out = baseSize + i;
        dst[out + 0] = worldPos.x;
        dst[out + 1] = worldPos.y;
        dst[out + 2] = worldPos.z;
        dst[out + 3] = worldNormal.x;
        dst[out + 4] = worldNormal.y;
        dst[out + 5] = worldNormal.z;
        dst[out + 6] = uv.x;
        dst[out + 7] = uv.y;
    }
}
} // namespace

// Public wrapper over the file-local stream lookup above, so consumers outside
// Rendering.cpp (the Nebula lightmap baker) read exactly the same geometry the
// static-merge path does instead of re-deriving it.
const std::vector<float>* GetObjectTriangleVertexStream(const SceneObject& obj) {
    const std::vector<float>* stream = nullptr;
    if (!TryGetObjectTriangleVertexStream(obj, stream)) return nullptr;
    return stream;
}

// Global (declared in Rendering.h) so viewport panels can build matching
// projections; lives outside the anonymous namespace on purpose.
glm::mat4 BuildCameraProjection(const Camera& camera, int width, int height,
                                float fovDeg, float nearPlane, float farPlane) {
    const float safeWidth = static_cast<float>(std::max(1, width));
    const float safeHeight = static_cast<float>(std::max(1, height));
    const float aspect = safeWidth / safeHeight;
    if (camera.orthographic) {
        // 3D ortho cameras carry a world-unit half-height (Unity's "Size"); legacy 2D leaves
        // orthoSize at 0 and derives it from pixels-per-unit.
        const float halfHeight = (camera.orthoSize > 0.0f)
            ? camera.orthoSize
            : safeHeight / (2.0f * std::max(1.0f, camera.pixelsPerUnit));
        const float halfWidth = halfHeight * aspect;
        return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
    }
    return glm::perspective(glm::radians(fovDeg), aspect, nearPlane, farPlane);
}

void ModuRuntime2DRenderCounters_Reset() {
    gRuntime2DRenderCounters = Runtime2DRenderCounters{};
}

void ModuRuntime2DRenderCounters_Read(uint64_t* outTextureBindCount,
                                      uint64_t* outStateBindCount) {
    if (outTextureBindCount) {
        *outTextureBindCount = gRuntime2DRenderCounters.textureBindCount;
    }
    if (outStateBindCount) {
        *outStateBindCount = gRuntime2DRenderCounters.stateBindCount;
    }
}

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
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[(current + 1) * 8 + i]);
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[next * 8 + i]);

            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[(current + 1) * 8 + i]);
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[(next + 1) * 8 + i]);
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[next * 8 + i]);
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
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[(current + 1) * 8 + i]);
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[next * 8 + i]);

            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[(current + 1) * 8 + i]);
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[(next + 1) * 8 + i]);
            for (int i = 0; i < 8; i++) triangulated.push_back(vertices[next * 8 + i]);
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
            for (int i = 0; i < 8; ++i) triangulated.push_back(vertices[(current + 1) * 8 + i]);
            for (int i = 0; i < 8; ++i) triangulated.push_back(vertices[next * 8 + i]);

            for (int i = 0; i < 8; ++i) triangulated.push_back(vertices[(current + 1) * 8 + i]);
            for (int i = 0; i < 8; ++i) triangulated.push_back(vertices[(next + 1) * 8 + i]);
            for (int i = 0; i < 8; ++i) triangulated.push_back(vertices[next * 8 + i]);
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
    loaded.baseVertices = vertices;

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
    // Any resize racing teardown must become a no-op rather than reallocating
    // targets we are about to delete.
    rendererShuttingDown = true;
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
    delete compositeShader;
    delete brightShader;
    delete blurShader;
    delete shadowDepthShader;
    delete directionalShadowDepthShader;
    if (previewTarget.fbo) glDeleteFramebuffers(1, &previewTarget.fbo);
    if (previewTarget.texture) glDeleteTextures(1, &previewTarget.texture);
    if (previewTarget.rbo) glDeleteRenderbuffers(1, &previewTarget.rbo);
    for (auto& entry : extraPreviewTargets) {
        releaseRenderTarget(entry.second);
    }
    extraPreviewTargets.clear();
    extraPreviewTargetLastUsed.clear();
    if (postTarget.fbo) glDeleteFramebuffers(1, &postTarget.fbo);
    if (postTarget.texture) glDeleteTextures(1, &postTarget.texture);
    if (postTarget.rbo) glDeleteRenderbuffers(1, &postTarget.rbo);
    releaseRenderTarget(polarizationTarget);
    releaseRenderTarget(overlayCaptureTarget);
    releaseRenderTarget(bandLayerTarget);
    releaseRenderTarget(bandCaptureTarget);
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
    for (auto& entry : shadowDirectionalMaps) {
        if (entry.second.fbo) glDeleteFramebuffers(1, &entry.second.fbo);
        if (entry.second.depthTexture) glDeleteTextures(1, &entry.second.depthTexture);
    }
    shadowDirectionalMaps.clear();
    for (auto& entry : mirrorTargets) {
        releaseRenderTarget(entry.second);
    }
    mirrorTargets.clear();
    mirrorUpdateStates.clear();
    for (auto& entry : reflectionCastTargets) {
        releaseReflectionCastTarget(entry.second);
    }
    reflectionCastTargets.clear();
    releaseSkyboxReflectionTarget(skyboxReflectionTarget);
    for (auto& entry : uiTargets) {
        releaseRenderTarget(entry.second);
    }
    uiTargets.clear();
    uiTargetViews.clear();
    for (auto& entry : uiTargetAtlases) {
        releaseRenderTarget(entry.second.target);
    }
    uiTargetAtlases.clear();
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
    if (!moduRenderContextLive()) {
        static bool warnedNoContext = false;
        if (!warnedNoContext) {
            std::cerr << "[Renderer] Texture request without an OpenGL context. Returning null texture.\n";
            warnedNoContext = true;
        }
        return nullptr;
    }
    bool point = (filter == MaterialProperties::TextureFilter::Point);
    auto& cache = point ? textureCachePoint : textureCacheBilinear;
    const double nowSec = glfwGetTime();
    auto missingIt = missingTextureRetryAfter.find(path);
    if (missingIt != missingTextureRetryAfter.end()) {
        if (nowSec < missingIt->second) {
            return nullptr;
        }
        missingTextureRetryAfter.erase(missingIt);
    }
    auto it = cache.find(path);
    if (it != cache.end()) {
        it->second.lastUsedTime = nowSec;
        return it->second.texture.get();
    }

    GLenum minFilter = point ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
    GLenum magFilter = point ? GL_NEAREST : GL_LINEAR;
    // Per-texture overrides win; otherwise the project-wide default from
    // Graphics Manager applies (Auto keeps the adaptive behavior).
    TextureFormatPolicy policy = getTextureFormatOverride(path);
    if (policy == TextureFormatPolicy::Auto) policy = defaultTextureFormatPolicy;
    auto tex = std::make_unique<Texture>(path, GL_REPEAT, GL_REPEAT, minFilter, magFilter, policy);
    if (!tex->GetID()) {
        missingTextureRetryAfter[path] = nowSec + 1.0;
        return nullptr;
    }
    CachedTextureEntry entry;
    entry.approxBytes = tex->GetApproxMemoryBytes();
    entry.lastUsedTime = nowSec;
    entry.texture = std::move(tex);
    Texture* raw = entry.texture.get();
    textureCacheUsageBytes += entry.approxBytes;
    cache[path] = std::move(entry);
    purgeTextureCacheIfNeeded();
    return raw;
}

void Renderer::invalidateTexture(const std::string& path) {
    if (path.empty()) return;
    missingTextureRetryAfter.erase(path);
    auto eraseFromCache = [this, &path](auto& cache) {
        auto it = cache.find(path);
        if (it == cache.end()) return;
        textureCacheUsageBytes = (textureCacheUsageBytes > it->second.approxBytes)
            ? (textureCacheUsageBytes - it->second.approxBytes)
            : 0;
        cache.erase(it);
    };
    eraseFromCache(textureCacheBilinear);
    eraseFromCache(textureCachePoint);
}

std::string Renderer::normalizeTextureKey(const std::string& path) const {
    if (path.empty()) return path;
    std::error_code ec;
    fs::path p(path);
    if (p.is_relative() && !textureKeyRoot.empty()) {
        p = fs::path(textureKeyRoot) / p;
    }
    fs::path canon = fs::weakly_canonical(p, ec);
    if (ec) canon = p.lexically_normal();
    return canon.generic_string();
}

void Renderer::setTextureFormatOverride(const std::string& path, TextureFormatPolicy policy) {
    if (path.empty()) return;
    const std::string key = normalizeTextureKey(path);
    if (policy == TextureFormatPolicy::Auto) {
        textureFormatOverrides.erase(key);
    } else {
        textureFormatOverrides[key] = policy;
    }
    // evict every cache entry whose normalized key matches (entries are keyed by the original
    // request string, which can differ from the caller's path) so the next request reloads
    // at the new format.
    auto evictMatching = [this, &key](auto& cache) {
        for (auto it = cache.begin(); it != cache.end();) {
            if (normalizeTextureKey(it->first) == key) {
                textureCacheUsageBytes = (textureCacheUsageBytes > it->second.approxBytes)
                    ? (textureCacheUsageBytes - it->second.approxBytes) : 0;
                it = cache.erase(it);
            } else {
                ++it;
            }
        }
    };
    evictMatching(textureCacheBilinear);
    evictMatching(textureCachePoint);
}

TextureFormatPolicy Renderer::getTextureFormatOverride(const std::string& path) const {
    auto it = textureFormatOverrides.find(normalizeTextureKey(path));
    return it == textureFormatOverrides.end() ? TextureFormatPolicy::Auto : it->second;
}

void Renderer::setDefaultTextureFormatPolicy(TextureFormatPolicy policy) {
    if (defaultTextureFormatPolicy == policy) return;
    defaultTextureFormatPolicy = policy;
    // everything loaded under the old default is the wrong format now. dropping the whole cache
    // is heavy but this only happens when the user flips the setting; textures stream back lazily.
    textureCacheBilinear.clear();
    textureCachePoint.clear();
    textureCacheUsageBytes = 0;
    missingTextureRetryAfter.clear();
}

void Renderer::setColorPrecision(RendererColorPrecision precision) {
    if (colorPrecision == precision) return;
    colorPrecision = precision;
    // re-allocate the main viewport color buffer at the new precision. offscreen RenderTargets
    // rebuild lazily since ensureRenderTarget stores the effective hdr flag.
    if (framebuffer != 0 && viewportTexture != 0 && moduRenderContextLive()) {
        const bool sdr = (colorPrecision == RendererColorPrecision::SDR8);
        glBindTexture(GL_TEXTURE_2D, viewportTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, sdr ? GL_RGBA8 : GL_RGBA16F, currentWidth, currentHeight,
                     0, GL_RGBA, sdr ? GL_UNSIGNED_BYTE : GL_FLOAT, NULL);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void Renderer::purgeTextureCacheIfNeeded() {
    if (textureCacheUsageBytes <= textureCacheBudgetBytes) {
        return;
    }

    struct EvictionCandidate {
        bool point = false;
        double lastUsedTime = 0.0;
        size_t approxBytes = 0;
        std::string path;
    };

    std::vector<EvictionCandidate> candidates;
    candidates.reserve(textureCacheBilinear.size() + textureCachePoint.size());

    auto collect = [&candidates](const auto& cache, bool point) {
        for (const auto& [path, entry] : cache) {
            candidates.push_back({point, entry.lastUsedTime, entry.approxBytes, path});
        }
    };
    collect(textureCacheBilinear, false);
    collect(textureCachePoint, true);

    std::sort(candidates.begin(), candidates.end(),
              [](const EvictionCandidate& a, const EvictionCandidate& b) {
                  if (a.lastUsedTime != b.lastUsedTime) return a.lastUsedTime < b.lastUsedTime;
                  return a.path < b.path;
              });

    const double nowSec = glfwGetTime();
    for (const auto& candidate : candidates) {
        if (textureCacheUsageBytes <= textureCacheBudgetBytes) {
            break;
        }

        if ((nowSec - candidate.lastUsedTime) < 2.0) {
            continue;
        }

        auto& cache = candidate.point ? textureCachePoint : textureCacheBilinear;
        auto it = cache.find(candidate.path);
        if (it == cache.end()) continue;

        textureCacheUsageBytes = (textureCacheUsageBytes > it->second.approxBytes)
            ? (textureCacheUsageBytes - it->second.approxBytes)
            : 0;
        cache.erase(it);
    }
}

void Renderer::purgePreviewTargetsIfNeeded(int keepSlot) {
    constexpr size_t kMaxExtraPreviewTargets = 192;
    if (extraPreviewTargets.size() <= kMaxExtraPreviewTargets) {
        return;
    }

    struct PreviewTargetCandidate {
        int slot = 0;
        uint64_t lastUsed = 0;
    };

    std::vector<PreviewTargetCandidate> candidates;
    candidates.reserve(extraPreviewTargets.size());
    for (const auto& [slot, target] : extraPreviewTargets) {
        (void)target;
        if (slot == keepSlot) continue;
        auto usedIt = extraPreviewTargetLastUsed.find(slot);
        candidates.push_back({slot, usedIt != extraPreviewTargetLastUsed.end() ? usedIt->second : 0});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const PreviewTargetCandidate& a, const PreviewTargetCandidate& b) {
                  if (a.lastUsed != b.lastUsed) return a.lastUsed < b.lastUsed;
                  return a.slot < b.slot;
              });

    for (const PreviewTargetCandidate& candidate : candidates) {
        if (extraPreviewTargets.size() <= kMaxExtraPreviewTargets) {
            break;
        }
        auto targetIt = extraPreviewTargets.find(candidate.slot);
        if (targetIt == extraPreviewTargets.end()) continue;
        releaseRenderTarget(targetIt->second);
        extraPreviewTargets.erase(targetIt);
        extraPreviewTargetLastUsed.erase(candidate.slot);
    }
}

void Renderer::rebuildStaticMergeBatches(const std::vector<SceneObject>& sceneObjects) {
    if (lastStaticMergeCheckFrameSerial == frameSerial) {
        return;
    }
    lastStaticMergeCheckFrameSerial = frameSerial;

    constexpr size_t kMinSceneObjectsForStaticMerge = 8;
    if (sceneObjects.size() < kMinSceneObjectsForStaticMerge) {
        staticMergeSceneSignature = 0;
        staticMergeBatches.clear();
        staticMergeSourceIds.clear();
        return;
    }

    auto isStaticMergeBatchable = [&](const SceneObject& obj) {
        if (!IsStaticMergeCandidate(obj)) return false;
        if (obj.material.alpha < 0.999f) return false;
        if (obj.renderType == RenderType::Sprite && !HasDefaultSpriteUvRect(obj)) {
            return false;
        }
        if (!obj.albedoTexturePath.empty()) {
            if (Texture* tex = getTexture(obj.albedoTexturePath, obj.material.textureFilter)) {
                if (tex->UsesAlphaBlending()) {
                    return false;
                }
            }
        }
        if (obj.useOverlay && !obj.overlayTexturePath.empty()) {
            if (Texture* tex = getTexture(obj.overlayTexturePath, obj.material.textureFilter)) {
                if (tex->UsesAlphaBlending()) {
                    return false;
                }
            }
        }
        return true;
    };

    uint64_t signature = 1469598103934665603ull;
    size_t candidateCount = 0;
    for (const auto& obj : sceneObjects) {
        if (!isStaticMergeBatchable(obj)) continue;
        ++candidateCount;
        signature = HashCombine64(signature, static_cast<uint64_t>(obj.id));
        signature = HashCombine64(signature, static_cast<uint64_t>(obj.renderType));
        signature = HashCombine64(signature, static_cast<uint64_t>(obj.meshId + 1));
        signature = HashCombine64(signature, HashString64(obj.vertexShaderPath));
        signature = HashCombine64(signature, HashString64(obj.fragmentShaderPath));
        signature = HashCombine64(signature, HashString64(obj.materialPath));
        signature = HashCombine64(signature, HashString64(obj.albedoTexturePath));
        signature = HashCombine64(signature, HashString64(obj.overlayTexturePath));
        signature = HashCombine64(signature, HashString64(obj.normalMapPath));
        signature = HashCombine64(signature, HashFloat64(obj.position.x));
        signature = HashCombine64(signature, HashFloat64(obj.position.y));
        signature = HashCombine64(signature, HashFloat64(obj.position.z));
        signature = HashCombine64(signature, HashFloat64(obj.rotation.x));
        signature = HashCombine64(signature, HashFloat64(obj.rotation.y));
        signature = HashCombine64(signature, HashFloat64(obj.rotation.z));
        signature = HashCombine64(signature, HashFloat64(obj.scale.x));
        signature = HashCombine64(signature, HashFloat64(obj.scale.y));
        signature = HashCombine64(signature, HashFloat64(obj.scale.z));
        signature = HashCombine64(signature, HashFloat64(obj.material.color.r));
        signature = HashCombine64(signature, HashFloat64(obj.material.color.g));
        signature = HashCombine64(signature, HashFloat64(obj.material.color.b));
        signature = HashCombine64(signature, HashFloat64(obj.material.alpha));
        signature = HashCombine64(signature, HashFloat64(obj.material.ambientStrength));
        signature = HashCombine64(signature, HashFloat64(obj.material.specularStrength));
        signature = HashCombine64(signature, HashFloat64(obj.material.shininess));
        signature = HashCombine64(signature, HashFloat64(obj.material.normalMapIntensity));
        signature = HashCombine64(signature, HashFloat64(obj.material.textureMix));
        signature = HashCombine64(signature, HashFloat64(obj.material.scrollSpeed));
        signature = HashCombine64(signature, HashFloat64(obj.material.scrollDirection.x));
        signature = HashCombine64(signature, HashFloat64(obj.material.scrollDirection.y));
        signature = HashCombine64(signature, HashFloat64(obj.material.cloudColor.r));
        signature = HashCombine64(signature, HashFloat64(obj.material.cloudColor.g));
        signature = HashCombine64(signature, HashFloat64(obj.material.cloudColor.b));
        signature = HashCombine64(signature, HashFloat64(obj.material.cloudSkyColor.r));
        signature = HashCombine64(signature, HashFloat64(obj.material.cloudSkyColor.g));
        signature = HashCombine64(signature, HashFloat64(obj.material.cloudSkyColor.b));
        signature = HashCombine64(signature, HashFloat64(obj.material.cloudScale));
        signature = HashCombine64(signature, HashFloat64(obj.material.cloudCoverage));
        signature = HashCombine64(signature, HashFloat64(obj.material.cloudSoftness));
        signature = HashCombine64(signature, static_cast<uint64_t>(obj.material.cloudDetail));
        signature = HashCombine64(signature, HashFloat64(obj.material.cloudSpeed));
        signature = HashCombine64(signature, HashFloat64(obj.material.cloudWarp));
        signature = HashCombine64(signature, HashFloat64(obj.material.cloudHighlight));
        signature = HashCombine64(signature, HashFloat64(obj.material.cloudStars));
        signature = HashCombine64(signature, HashFloat64(obj.material.cloudHorizon));
        signature = HashCombine64(signature, static_cast<uint64_t>(obj.material.textureFilter));
        signature = HashCombine64(signature, static_cast<uint64_t>(obj.useOverlay ? 1 : 0));
    }
    signature = HashCombine64(signature, static_cast<uint64_t>(candidateCount));

    if (candidateCount < 2) {
        staticMergeSceneSignature = 0;
        staticMergeBatches.clear();
        staticMergeSourceIds.clear();
        return;
    }

    if (signature == staticMergeSceneSignature) {
        return;
    }

    staticMergeSceneSignature = signature;
    staticMergeBatches.clear();
    staticMergeSourceIds.clear();

    struct BatchAccumulator {
        Renderer::StaticMergeBatch batch;
        std::vector<float> vertices;
        glm::vec3 boundsMin = glm::vec3(FLT_MAX);
        glm::vec3 boundsMax = glm::vec3(-FLT_MAX);
    };

    std::unordered_map<std::string, BatchAccumulator> accumulators;
    accumulators.reserve(candidateCount);

    for (const auto& obj : sceneObjects) {
        if (!isStaticMergeBatchable(obj)) continue;

        const std::vector<float>* sourceVertices = nullptr;
        if (!TryGetObjectTriangleVertexStream(obj, sourceVertices) || !sourceVertices || sourceVertices->empty()) {
            continue;
        }

        std::string batchKey =
            std::to_string(static_cast<int>(obj.renderType)) + "|" +
            obj.vertexShaderPath + "|" + obj.fragmentShaderPath + "|" + obj.materialPath + "|" +
            obj.albedoTexturePath + "|" + obj.overlayTexturePath + "|" + obj.normalMapPath + "|" +
            std::to_string(static_cast<int>(obj.material.textureFilter)) + "|" +
            std::to_string(obj.useOverlay ? 1 : 0) + "|" +
            std::to_string(obj.material.color.r) + "|" +
            std::to_string(obj.material.color.g) + "|" +
            std::to_string(obj.material.color.b) + "|" +
            std::to_string(obj.material.alpha) + "|" +
            std::to_string(obj.material.ambientStrength) + "|" +
            std::to_string(obj.material.specularStrength) + "|" +
            std::to_string(obj.material.shininess) + "|" +
            std::to_string(obj.material.normalMapIntensity) + "|" +
            std::to_string(obj.material.textureMix);

        BatchAccumulator& acc = accumulators[batchKey];
        if (acc.batch.albedoTexturePath.empty() && acc.batch.normalMapPath.empty() &&
            acc.batch.overlayTexturePath.empty() && acc.batch.materialPath.empty() &&
            acc.batch.vertPath.empty() && acc.batch.fragPath.empty() && acc.vertices.empty()) {
            acc.batch.vertPath = obj.vertexShaderPath;
            acc.batch.fragPath = obj.fragmentShaderPath;
            acc.batch.material = obj.material;
            acc.batch.materialPath = obj.materialPath;
            acc.batch.albedoTexturePath = obj.albedoTexturePath;
            acc.batch.overlayTexturePath = obj.overlayTexturePath;
            acc.batch.normalMapPath = obj.normalMapPath;
            acc.batch.useOverlay = obj.useOverlay;
            acc.batch.unlit = (obj.renderType == RenderType::Sprite);
            acc.batch.doubleSided = (obj.renderType == RenderType::Sprite);
        }

        AppendTransformedTriangleVertices(*sourceVertices, BuildSceneObjectModelMatrix(obj),
                                         acc.vertices, acc.boundsMin, acc.boundsMax);
        staticMergeSourceIds.insert(obj.id);
    }

    for (auto& [key, acc] : accumulators) {
        (void)key;
        if (acc.vertices.empty()) continue;
        StaticMergeBatch batch = std::move(acc.batch);
        batch.mesh = std::make_unique<Mesh>(acc.vertices.data(), acc.vertices.size() * sizeof(float));
        batch.boundsCenter = (acc.boundsMin + acc.boundsMax) * 0.5f;
        batch.boundsRadius = glm::length(glm::max(acc.boundsMax - batch.boundsCenter, glm::vec3(0.001f)));
        staticMergeBatches.push_back(std::move(batch));
    }
}

void Renderer::initialize() {
#if MODULARITY_RUNTIME_ONLY
    autoReloadShaders = false;
#endif
    auto requireFile = [](const std::string& path, const char* label) {
        // check via AssetSource so shaders are found inside the APK on Android, not just the
        // filesystem. desktop behaves like the old fs::exists.
        if (!Modularity::Platform::GetAssetSource().Exists(path)) {
            throw std::runtime_error(std::string(label) + " not found: " + path);
        }
    };
    requireFile(defaultVertPath, "Default vertex shader");
    requireFile(defaultFragPath, "Default fragment shader");

    GLint maxTextureSize = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    uiAtlasMaxSize = std::clamp(static_cast<int>(maxTextureSize), 1024, 4096);

    shader = new Shader(defaultVertPath.c_str(), defaultFragPath.c_str());
    defaultShader = shader;
    if (shader->ID == 0) {
        std::cerr << "Shader compilation failed: " << defaultVertPath
                  << " + " << defaultFragPath << "\n";
        delete shader;
        shader = nullptr;
        defaultShader = nullptr;
        throw std::runtime_error("Default shader compilation failed: " +
                                 defaultVertPath + " + " + defaultFragPath);
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
    compositeShader = new Shader(postVertPath.c_str(), postCompositeFragPath.c_str());
    if (!compositeShader || compositeShader->ID == 0) {
        std::cerr << "PostFX composite shader compilation failed!\n";
        delete compositeShader;
        compositeShader = nullptr;
    } else {
        compositeShader->use();
        compositeShader->setInt("sourceTex", 0);
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
    directionalShadowDepthShader = new Shader(directionalShadowDepthVertPath.c_str(), directionalShadowDepthFragPath.c_str());
    if (!directionalShadowDepthShader || directionalShadowDepthShader->ID == 0) {
        std::cerr << "Directional shadow depth shader compilation failed; directional shadows will be disabled.\n";
        delete directionalShadowDepthShader;
        directionalShadowDepthShader = nullptr;
    }
    ShaderEntry entry;
    entry.shader.reset(defaultShader);
    entry.vertPath = defaultVertPath;
    entry.fragPath = defaultFragPath;
    entry.nextReloadCheckTime = glfwGetTime() + 0.5;
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
    ensureRenderTarget(postTarget, currentWidth, currentHeight, false, true);
    ensureRenderTarget(historyTarget, currentWidth, currentHeight, false, true);
    ensureRenderTarget(bloomTargetA, currentWidth, currentHeight, false, true);
    ensureRenderTarget(bloomTargetB, currentWidth, currentHeight, false, true);
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
            const double nowSec = glfwGetTime();
            if (nowSec >= entry.nextReloadCheckTime) {
                entry.nextReloadCheckTime = nowSec + 0.5;
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
        }
        return entry.shader ? entry.shader.get() : defaultShader;
    }

    ShaderEntry entry;
    entry.vertPath = vPath;
    entry.fragPath = fPath;
    entry.nextReloadCheckTime = glfwGetTime() + 0.5;
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
    entry.nextReloadCheckTime = glfwGetTime() + 0.5;
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
    {
        const bool sdr = (colorPrecision == RendererColorPrecision::SDR8);
        glTexImage2D(GL_TEXTURE_2D, 0, sdr ? GL_RGBA8 : GL_RGBA16F, currentWidth, currentHeight,
                     0, GL_RGBA, sdr ? GL_UNSIGNED_BYTE : GL_FLOAT, NULL);
    }
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
    ensureRenderTarget(target, w, h, false, false);
}

void Renderer::ensureRenderTarget(RenderTarget& target, int w, int h, bool alpha) {
    ensureRenderTarget(target, w, h, alpha, false);
}

void Renderer::ensureRenderTarget(RenderTarget& target, int w, int h, bool alpha, bool hdr) {
    ensureRenderTarget(target, w, h, alpha, hdr, true);
}

void Renderer::ensureRenderTarget(RenderTarget& target, int w, int h, bool alpha, bool hdr, bool depth) {
    if (w <= 0 || h <= 0) return;

    // project color precision can demote HDR to 8-bit. storing the effective flag means a
    // precision switch invalidates targets on their next ensure, no teardown pass needed.
    if (colorPrecision == RendererColorPrecision::SDR8) hdr = false;

    if (target.fbo == 0) {
        glGenFramebuffers(1, &target.fbo);
        glGenTextures(1, &target.texture);
    }
    if (depth && target.rbo == 0) {
        glGenRenderbuffers(1, &target.rbo);
    }

    if (target.width == w && target.height == h && target.hasAlpha == alpha && target.hdr == hdr &&
        target.hasDepth == depth) return;

    target.width = w;
    target.height = h;
    target.hasAlpha = alpha;
    target.hdr = hdr;
    target.hasDepth = depth;

    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);

    glBindTexture(GL_TEXTURE_2D, target.texture);
    GLenum internalFormat = GL_RGB8;
    GLenum dataFormat = GL_RGB;
    GLenum dataType = GL_UNSIGNED_BYTE;
    if (hdr) {
        internalFormat = alpha ? GL_RGBA16F : GL_RGBA16F;
        dataFormat = alpha ? GL_RGBA : GL_RGBA;
        dataType = GL_FLOAT;
    } else if (alpha) {
        internalFormat = GL_RGBA8;
        dataFormat = GL_RGBA;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, target.width, target.height, 0, dataFormat, dataType, NULL);
    bool isSelectionMask = (&target == &selectionMaskTarget);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, isSelectionMask ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, isSelectionMask ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    target.textureFilter = isSelectionMask ? MaterialProperties::TextureFilter::Point
                                           : MaterialProperties::TextureFilter::Bilinear;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);

    if (depth) {
        glBindRenderbuffer(GL_RENDERBUFFER, target.rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, target.width, target.height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target.rbo);
    } else {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
        if (target.rbo != 0) {
            glDeleteRenderbuffers(1, &target.rbo);
            target.rbo = 0;
        }
    }

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

void Renderer::releaseReflectionCastTarget(ReflectionCastTarget& target) {
    if (target.depth) {
        glDeleteRenderbuffers(1, &target.depth);
    }
    if (target.cube) {
        glDeleteTextures(1, &target.cube);
    }
    if (target.fbo) {
        glDeleteFramebuffers(1, &target.fbo);
    }
    target = {};
}

void Renderer::releaseSkyboxReflectionTarget(SkyboxReflectionTarget& target) {
    if (target.depth) {
        glDeleteRenderbuffers(1, &target.depth);
    }
    if (target.cube) {
        glDeleteTextures(1, &target.cube);
    }
    if (target.fbo) {
        glDeleteFramebuffers(1, &target.fbo);
    }
    target = {};
}

void Renderer::updateSkyboxReflectionTarget() {
    if (!skybox) return;
    const SkyboxSettings& settings = skybox->getSettings();
    if (!settings.environmentReflections || settings.environmentReflectionIntensity <= 0.001f) {
        if (skyboxReflectionTarget.fbo || skyboxReflectionTarget.cube || skyboxReflectionTarget.depth) {
            releaseSkyboxReflectionTarget(skyboxReflectionTarget);
        }
        return;
    }

    uint64_t signature = 1469598103934665603ull;
    auto mix = [&](uint64_t v) {
        signature ^= v;
        signature *= 1099511628211ull;
    };
    auto mixFloat = [&](float value) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        mix(bits);
    };
    mix(static_cast<uint64_t>(settings.mode));
    mixFloat(skybox->getTimeOfDay());
    mixFloat(settings.scrollingRepeatX);
    mixFloat(settings.scrollingRepeatY);
    mixFloat(settings.scrollingLookSensitivity);
    mixFloat(settings.scrollingVerticalInfluence);
    for (char c : settings.sunTexturePath) mix(static_cast<unsigned char>(c));
    for (char c : settings.moonTexturePath) mix(static_cast<unsigned char>(c));
    for (char c : settings.scrollingTexturePath) mix(static_cast<unsigned char>(c));

    constexpr int resolution = 128;
    const bool targetMissing = skyboxReflectionTarget.fbo == 0 || skyboxReflectionTarget.cube == 0 || skyboxReflectionTarget.depth == 0;
    const bool mustCapture = targetMissing ||
        skyboxReflectionTarget.resolution != resolution ||
        !skyboxReflectionTarget.hasCapture;
    if (!mustCapture && skyboxReflectionTarget.signature == signature) return;

    // A day/night cycle changes timeOfDay (and therefore the signature) every
    // frame, which used to re-render all six sky faces per frame. The capture
    // is a 128px cubemap of a slowly moving sky, so refreshing it a few times
    // a second is visually identical and caps the cost.
    constexpr double kMinRecaptureIntervalSec = 0.25;
    const double nowSec = glfwGetTime();
    if (!mustCapture &&
        skyboxReflectionTarget.lastCaptureSec >= 0.0 &&
        nowSec - skyboxReflectionTarget.lastCaptureSec < kMinRecaptureIntervalSec) {
        return;
    }

    if (targetMissing || skyboxReflectionTarget.resolution != resolution) {
        releaseSkyboxReflectionTarget(skyboxReflectionTarget);
        skyboxReflectionTarget.resolution = resolution;
        glGenFramebuffers(1, &skyboxReflectionTarget.fbo);
        glGenTextures(1, &skyboxReflectionTarget.cube);
        glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxReflectionTarget.cube);
        for (int face = 0; face < 6; ++face) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB16F,
                         resolution, resolution, 0, GL_RGB, GL_FLOAT, nullptr);
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glGenRenderbuffers(1, &skyboxReflectionTarget.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, skyboxReflectionTarget.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, resolution, resolution);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    GLint prevFBO = 0;
    GLint prevViewport[4] = {0, 0, currentWidth, currentHeight};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    const glm::vec3 faceDirs[6] = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
    };
    const glm::vec3 faceUps[6] = {
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
    };
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    for (int face = 0; face < 6; ++face) {
        glBindFramebuffer(GL_FRAMEBUFFER, skyboxReflectionTarget.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                               skyboxReflectionTarget.cube, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, skyboxReflectionTarget.depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            break;
        }
        glViewport(0, 0, resolution, resolution);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), faceDirs[face], faceUps[face]);
        renderSkybox(view, proj);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    skyboxReflectionTarget.signature = signature;
    skyboxReflectionTarget.hasCapture = true;
    skyboxReflectionTarget.lastCaptureSec = nowSec;
}

void Renderer::updateReflectionCastTargets(const std::vector<SceneObject>& sceneObjects, float nearPlane, float farPlane) {
    if (sceneObjects.empty() || reflectionCapturePass) return;

    std::unordered_set<int> active;
    GLint prevFBO = 0;
    GLint prevViewport[4] = {0, 0, currentWidth, currentHeight};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    const glm::vec3 faceDirs[6] = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
    };
    const glm::vec3 faceUps[6] = {
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
    };

    for (const SceneObject& obj : sceneObjects) {
        if (!IsObjectEnabledInHierarchy(obj) || !obj.hasReflectionCast || !obj.reflectionCast.enabled) continue;
        active.insert(obj.id);

        ReflectionCastTarget& target = reflectionCastTargets[obj.id];
        const int resolution = std::clamp(obj.reflectionCast.resolution, 32, 1024);
        const bool targetMissing = target.fbo == 0 || target.cube == 0 || target.depth == 0;
        const bool sizeChanged = target.resolution != resolution;
        const bool shouldCapture = targetMissing ||
            sizeChanged ||
            obj.reflectionCast.updateMode == ReflectionCastUpdateMode::EveryFrame ||
            !obj.reflectionCast.baked ||
            !target.hasCapture;
        if (!shouldCapture) continue;

        if (targetMissing || sizeChanged) {
            releaseReflectionCastTarget(target);
            target.resolution = resolution;
            glGenFramebuffers(1, &target.fbo);
            glGenTextures(1, &target.cube);
            glBindTexture(GL_TEXTURE_CUBE_MAP, target.cube);
            for (int face = 0; face < 6; ++face) {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB16F,
                             resolution, resolution, 0, GL_RGB, GL_FLOAT, nullptr);
            }
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glGenRenderbuffers(1, &target.depth);
            glBindRenderbuffer(GL_RENDERBUFFER, target.depth);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, resolution, resolution);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
        }
        if (target.fbo == 0 || target.cube == 0 || target.depth == 0) continue;

        reflectionCapturePass = true;
        for (int face = 0; face < 6; ++face) {
            glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, target.cube, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target.depth);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                break;
            }
            glViewport(0, 0, resolution, resolution);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            Camera captureCamera;
            captureCamera.position = obj.position;
            captureCamera.front = faceDirs[face];
            captureCamera.up = faceUps[face];
            renderSceneInternal(captureCamera, sceneObjects, resolution, resolution, false, 90.0f, nearPlane, farPlane, false, true);
        }
        reflectionCapturePass = false;
        target.hasCapture = true;
        const_cast<SceneObject&>(obj).reflectionCast.baked = true;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    for (auto it = reflectionCastTargets.begin(); it != reflectionCastTargets.end(); ) {
        if (active.find(it->first) == active.end()) {
            releaseReflectionCastTarget(it->second);
            it = reflectionCastTargets.erase(it);
        } else {
            ++it;
        }
    }
}

void Renderer::updateMirrorTargets(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, float fovDeg, float nearPlane, float farPlane) {
    if (camera.orthographic || sceneObjects.empty() || width <= 0 || height <= 0) return;

    bool hasEnabledMirror = false;
    for (const auto& obj : sceneObjects) {
        if (IsObjectEnabledInHierarchy(obj) && obj.hasRenderer && obj.renderType == RenderType::Mirror) {
            hasEnabledMirror = true;
            break;
        }
    }
    if (!hasEnabledMirror) {
        if (!mirrorTargets.empty()) {
            for (auto& entry : mirrorTargets) {
                releaseRenderTarget(entry.second);
            }
            mirrorTargets.clear();
            mirrorUpdateStates.clear();
        }
        return;
    }

    std::unordered_set<int> active;
    active.reserve(mirrorTargets.size() + 4);
    const double nowSec = glfwGetTime();
    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    const glm::mat4 viewProjection =
        BuildCameraProjection(camera, width, height, fovDeg, nearPlane, farPlane) *
        camera.getViewMatrix();
    const FrustumPlanes frustum = BuildFrustumPlanes(viewProjection);

    auto planeNormal = [](const SceneObject& obj) {
        glm::quat q = glm::quat(glm::radians(obj.rotation));
        glm::vec3 n = q * glm::vec3(0.0f, 0.0f, 1.0f);
        if (!std::isfinite(n.x) || glm::length(n) < 1e-3f) {
            n = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return glm::normalize(n);
    };

    for (const auto& obj : sceneObjects) {
        if (!IsObjectEnabledInHierarchy(obj) || !obj.hasRenderer || obj.renderType != RenderType::Mirror) continue;
        active.insert(obj.id);

        glm::vec3 n = planeNormal(obj);
        glm::vec3 planePoint = obj.position;
        const glm::vec3 toCamera = camera.position - planePoint;
        if (glm::dot(n, toCamera) <= 0.001f) {
            continue;
        }

        glm::mat4 mirrorModel = BuildSceneObjectModelMatrix(obj);
        glm::vec3 boundsCenter(0.0f);
        float boundsRadius = 0.0f;
        if (TryComputeObjectCullSphere(obj, mirrorModel, boundsCenter, boundsRadius) &&
            !SphereInsideFrustum(frustum, boundsCenter, boundsRadius)) {
            continue;
        }

        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        if (!ProjectPlaneScreenRect(obj, viewProjection, width, height, minX, minY, maxX, maxY)) {
            continue;
        }

        const int targetWidth = QuantizeMirrorTargetDimension(static_cast<int>(std::ceil(maxX - minX)), width);
        const int targetHeight = QuantizeMirrorTargetDimension(static_cast<int>(std::ceil(maxY - minY)), height);

        RenderTarget& target = mirrorTargets[obj.id];
        MirrorUpdateState& state = mirrorUpdateStates[obj.id];
        const glm::vec3 cameraDelta = camera.position - state.lastCameraPos;
        const glm::vec3 mirrorPosDelta = obj.position - state.lastMirrorPos;
        const glm::vec3 mirrorRotDelta = obj.rotation - state.lastMirrorRot;
        const glm::vec3 mirrorScaleDelta = obj.scale - state.lastMirrorScale;
        const bool sizeChanged = target.width != targetWidth || target.height != targetHeight;
        const bool targetMissing = target.fbo == 0 || target.texture == 0;
        const bool cameraMoved = !std::isfinite(state.lastCameraPos.x) ||
            glm::dot(cameraDelta, cameraDelta) > (0.02f * 0.02f);
        const bool cameraRotated = !std::isfinite(state.lastCameraFront.x) ||
            glm::dot(camera.front, state.lastCameraFront) < 0.9995f ||
            glm::dot(camera.up, state.lastCameraUp) < 0.9995f;
        const bool mirrorMoved =
            glm::dot(mirrorPosDelta, mirrorPosDelta) > (0.001f * 0.001f) ||
            glm::dot(mirrorRotDelta, mirrorRotDelta) > (0.05f * 0.05f) ||
            glm::dot(mirrorScaleDelta, mirrorScaleDelta) > (0.001f * 0.001f);
        const bool transformChanged = cameraMoved || cameraRotated || mirrorMoved;
        const double secondsSinceUpdate = (state.lastUpdateTime >= 0.0) ? (nowSec - state.lastUpdateTime) : DBL_MAX;
        const bool shouldRefresh =
            targetMissing ||
            sizeChanged ||
            state.lastUpdateTime < 0.0 ||
            (transformChanged && secondsSinceUpdate >= (1.0 / 45.0)) ||
            secondsSinceUpdate >= 0.25;

        if (!shouldRefresh) {
            continue;
        }

        ensureRenderTarget(target, targetWidth, targetHeight, false, true);
        if (target.fbo == 0 || target.texture == 0) continue;

        glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
        glViewport(0, 0, target.width, target.height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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

        renderSceneInternal(mirrorCam, sceneObjects, target.width, target.height, false, fovDeg, nearPlane, farPlane, false, true);

        state.lastCameraPos = camera.position;
        state.lastCameraFront = camera.front;
        state.lastCameraUp = camera.up;
        state.lastMirrorPos = obj.position;
        state.lastMirrorRot = obj.rotation;
        state.lastMirrorScale = obj.scale;
        state.lastUpdateTime = nowSec;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);

    for (auto it = mirrorTargets.begin(); it != mirrorTargets.end(); ) {
        if (active.find(it->first) == active.end()) {
            releaseRenderTarget(it->second);
            mirrorUpdateStates.erase(it->first);
            it = mirrorTargets.erase(it);
        } else {
            ++it;
        }
    }
}

Renderer::UiTargetInfo Renderer::ensureUiTarget(int id,
                                                int width,
                                                int height,
                                                int layer,
                                                MaterialProperties::TextureFilter filter,
                                                bool allowAtlas) {
    if (!moduRenderContextLive()) {
        return {};
    }

    width = std::clamp(width, 16, 4096);
    height = std::clamp(height, 16, 4096);
    const GLint glFilter = ToGLTextureFilter(filter);

    auto setTargetFilter = [&](RenderTarget& target, bool force = false) {
        if (target.texture == 0) return;
        if (!force && target.textureFilter == filter) return;
        glBindTexture(GL_TEXTURE_2D, target.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        target.textureFilter = filter;
    };

    auto makeView = [&](RenderTarget& target, bool invalidated) {
        UiTargetInfo view;
        view.fbo = target.fbo;
        view.texture = target.texture;
        view.width = width;
        view.height = height;
        view.framebufferWidth = target.width;
        view.framebufferHeight = target.height;
        view.clearWidth = width;
        view.clearHeight = height;
        view.uvTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
        view.invalidated = invalidated;
        return view;
    };

    auto ensureDedicated = [&](bool preserveInvalidated) {
        RenderTarget& target = uiTargets[id];
        const bool targetInvalidated =
            target.fbo == 0 || target.texture == 0 || target.width != width ||
            target.height != height || !target.hasAlpha || target.hasDepth || target.hdr;
        // UI canvas surfaces are pure ImGui color composition; a depth/stencil
        // renderbuffer would only be allocated and cleared, never sampled.
        ensureRenderTarget(target, width, height, true, false, false);
        setTargetFilter(target, targetInvalidated);

        UiTargetInfo view = makeView(target, targetInvalidated);
        view.framebufferWidth = width;
        view.framebufferHeight = height;
        view.clearX = 0;
        view.clearY = 0;
        view.usesAtlas = false;
        auto prev = uiTargetViews.find(id);
        if (prev != uiTargetViews.end()) {
            view.invalidated = view.invalidated || preserveInvalidated ||
                               prev->second.invalidated ||
                               prev->second.texture != view.texture ||
                               prev->second.usesAtlas;
        } else {
            view.invalidated = true;
        }
        uiTargetViews[id] = view;
        return view;
    };

    const int clearWidth = width + kUiAtlasPadding * 2;
    const int clearHeight = height + kUiAtlasPadding * 2;
    if (!allowAtlas || clearWidth > uiAtlasMaxSize || clearHeight > uiAtlasMaxSize) {
        return ensureDedicated(false);
    }

    const uint64_t atlasKey = BuildUiAtlasKey(layer, filter);
    UiAtlasTarget& atlas = uiTargetAtlases[atlasKey];
    bool atlasInvalidated = false;

    auto allocateSlot = [&](UiAtlasSlot& slot) -> bool {
        if (atlas.size <= 0) return false;
        if (slot.clearWidth > atlas.size || slot.clearHeight > atlas.size) return false;
        if (atlas.cursorX + slot.clearWidth > atlas.size) {
            atlas.cursorX = 0;
            atlas.cursorY += atlas.rowHeight;
            atlas.rowHeight = 0;
        }
        if (atlas.cursorY + slot.clearHeight > atlas.size) return false;
        slot.clearX = atlas.cursorX;
        slot.clearY = atlas.cursorY;
        slot.x = slot.clearX + kUiAtlasPadding;
        slot.y = slot.clearY + kUiAtlasPadding;
        atlas.cursorX += slot.clearWidth;
        atlas.rowHeight = std::max(atlas.rowHeight, slot.clearHeight);
        return true;
    };

    auto repackAtlas = [&](int requestedSize) -> bool {
        std::vector<std::pair<int, UiAtlasSlot>> slots;
        slots.reserve(atlas.slots.size());
        for (const auto& entry : atlas.slots) {
            slots.push_back(entry);
        }
        std::sort(slots.begin(), slots.end(),
                  [](const auto& a, const auto& b) {
                      if (a.second.clearHeight != b.second.clearHeight) {
                          return a.second.clearHeight > b.second.clearHeight;
                      }
                      return a.first < b.first;
                  });

        int size = std::max(1024, NextPowerOfTwoAtLeast(requestedSize));
        size = std::max(size, atlas.size);
        while (size <= uiAtlasMaxSize) {
            UiAtlasTarget packed;
            packed.size = size;
            bool fits = true;
            for (auto& entry : slots) {
                UiAtlasSlot slot = entry.second;
                if (slot.clearWidth > packed.size || slot.clearHeight > packed.size) {
                    fits = false;
                    break;
                }
                if (packed.cursorX + slot.clearWidth > packed.size) {
                    packed.cursorX = 0;
                    packed.cursorY += packed.rowHeight;
                    packed.rowHeight = 0;
                }
                if (packed.cursorY + slot.clearHeight > packed.size) {
                    fits = false;
                    break;
                }
                slot.clearX = packed.cursorX;
                slot.clearY = packed.cursorY;
                slot.x = slot.clearX + kUiAtlasPadding;
                slot.y = slot.clearY + kUiAtlasPadding;
                packed.cursorX += slot.clearWidth;
                packed.rowHeight = std::max(packed.rowHeight, slot.clearHeight);
                packed.slots[entry.first] = slot;
            }
            if (fits) {
                atlas.size = packed.size;
                atlas.cursorX = packed.cursorX;
                atlas.cursorY = packed.cursorY;
                atlas.rowHeight = packed.rowHeight;
                atlas.slots = std::move(packed.slots);
                atlasInvalidated = true;
                return true;
            }
            if (size >= uiAtlasMaxSize) break;
            size = std::min(uiAtlasMaxSize, size * 2);
        }
        return false;
    };

    bool slotInvalidated = false;
    auto slotIt = atlas.slots.find(id);
    if (slotIt == atlas.slots.end()) {
        UiAtlasSlot slot;
        slot.contentWidth = width;
        slot.contentHeight = height;
        slot.clearWidth = clearWidth;
        slot.clearHeight = clearHeight;

        if (atlas.size <= 0) {
            atlas.size = std::min(uiAtlasMaxSize, std::max(1024, NextPowerOfTwoAtLeast(std::max(clearWidth, clearHeight))));
        }
        if (!allocateSlot(slot)) {
            auto oldSlots = atlas.slots;
            atlas.slots[id] = slot;
            if (!repackAtlas(std::max(atlas.size * 2, std::max(clearWidth, clearHeight)))) {
                atlas.slots = std::move(oldSlots);
                return ensureDedicated(false);
            }
        } else {
            atlas.slots[id] = slot;
        }
        slotInvalidated = true;
    } else if (slotIt->second.contentWidth != width ||
               slotIt->second.contentHeight != height) {
        auto oldSlots = atlas.slots;
        slotIt->second.contentWidth = width;
        slotIt->second.contentHeight = height;
        slotIt->second.clearWidth = clearWidth;
        slotIt->second.clearHeight = clearHeight;
        if (!repackAtlas(std::max(atlas.size, std::max(clearWidth, clearHeight)))) {
            atlas.slots = std::move(oldSlots);
            atlas.slots.erase(id);
            repackAtlas(std::max(1024, atlas.size));
            return ensureDedicated(false);
        }
        slotInvalidated = true;
    }

    const bool targetInvalidated =
        atlas.target.fbo == 0 || atlas.target.texture == 0 ||
        atlas.target.width != atlas.size || atlas.target.height != atlas.size ||
        !atlas.target.hasAlpha || atlas.target.hasDepth || atlas.target.hdr;
    ensureRenderTarget(atlas.target, atlas.size, atlas.size, true, false, false);
    setTargetFilter(atlas.target, targetInvalidated);
    atlasInvalidated = atlasInvalidated || targetInvalidated;

    if (atlasInvalidated) {
        for (const auto& entry : atlas.slots) {
            auto viewIt = uiTargetViews.find(entry.first);
            if (viewIt != uiTargetViews.end()) {
                viewIt->second.invalidated = true;
            }
        }
    }

    const UiAtlasSlot& slot = atlas.slots[id];
    UiTargetInfo view;
    view.fbo = atlas.target.fbo;
    view.texture = atlas.target.texture;
    view.width = width;
    view.height = height;
    view.framebufferWidth = atlas.target.width;
    view.framebufferHeight = atlas.target.height;
    view.x = slot.x;
    view.y = slot.y;
    view.clearX = slot.clearX;
    view.clearY = slot.clearY;
    view.clearWidth = slot.clearWidth;
    view.clearHeight = slot.clearHeight;
    view.uvTransform = BuildUiTargetUvTransform(slot.x, slot.y, width, height,
                                                atlas.target.width, atlas.target.height);
    view.usesAtlas = true;
    view.atlasKey = atlasKey;
    auto prev = uiTargetViews.find(id);
    view.invalidated = slotInvalidated || atlasInvalidated;
    if (prev != uiTargetViews.end()) {
        view.invalidated = view.invalidated || prev->second.invalidated ||
                           prev->second.texture != view.texture ||
                           !prev->second.usesAtlas ||
                           prev->second.atlasKey != atlasKey ||
                           prev->second.x != view.x ||
                           prev->second.y != view.y;
    } else {
        view.invalidated = true;
    }
    uiTargetViews[id] = view;
    return view;
}

Renderer::UiTargetInfo Renderer::getUiTargetInfo(int id) const {
    auto it = uiTargetViews.find(id);
    return (it != uiTargetViews.end()) ? it->second : UiTargetInfo{};
}

void Renderer::markUiTargetRendered(int id) {
    auto it = uiTargetViews.find(id);
    if (it != uiTargetViews.end()) {
        it->second.invalidated = false;
    }
}

void Renderer::cleanupUiTargets(const std::unordered_set<int>& active) {
    if (!moduRenderContextLive()) {
        uiTargets.clear();
        uiTargetViews.clear();
        uiTargetAtlases.clear();
        return;
    }

    for (auto it = uiTargetViews.begin(); it != uiTargetViews.end();) {
        if (active.find(it->first) == active.end()) {
            it = uiTargetViews.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = uiTargets.begin(); it != uiTargets.end();) {
        auto viewIt = uiTargetViews.find(it->first);
        if (active.find(it->first) == active.end() ||
            viewIt == uiTargetViews.end() ||
            viewIt->second.usesAtlas) {
            releaseRenderTarget(it->second);
            it = uiTargets.erase(it);
        } else {
            ++it;
        }
    }

    for (auto atlasIt = uiTargetAtlases.begin(); atlasIt != uiTargetAtlases.end();) {
        UiAtlasTarget& atlas = atlasIt->second;
        for (auto slotIt = atlas.slots.begin(); slotIt != atlas.slots.end();) {
            auto viewIt = uiTargetViews.find(slotIt->first);
            if (active.find(slotIt->first) == active.end() ||
                viewIt == uiTargetViews.end() ||
                !viewIt->second.usesAtlas ||
                viewIt->second.atlasKey != atlasIt->first) {
                slotIt = atlas.slots.erase(slotIt);
            } else {
                ++slotIt;
            }
        }
        if (atlas.slots.empty()) {
            releaseRenderTarget(atlas.target);
            atlasIt = uiTargetAtlases.erase(atlasIt);
        } else {
            ++atlasIt;
        }
    }
}

bool Renderer::isObjectVisibleInCapturedFrustums(const SceneObject& obj, float radiusMargin) const {
    if (capturedSceneFrustums.empty()) {
        return true;
    }
    glm::vec3 center(0.0f);
    float radius = 0.0f;
    if (!TryComputeObjectCullSphere(obj, BuildSceneObjectModelMatrix(obj), center, radius)) {
        return true;
    }
    radius *= std::max(1.0f, radiusMargin);
    for (const auto& planes : capturedSceneFrustums) {
        FrustumPlanes frustum{planes};
        if (SphereInsideFrustum(frustum, center, radius)) {
            return true;
        }
    }
    return false;
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

unsigned int Renderer::findFramebufferForTexture(unsigned int texture) const {
    if (texture == 0) return 0;
    if (texture == viewportTexture) return framebuffer;

    auto findTargetMatch = [texture](const RenderTarget& target) -> unsigned int {
        return (target.texture == texture) ? target.fbo : 0u;
    };

    if (unsigned int match = findTargetMatch(previewTarget)) return match;
    if (unsigned int match = findTargetMatch(postTarget)) return match;
    if (unsigned int match = findTargetMatch(previewPostTarget)) return match;
    if (unsigned int match = findTargetMatch(historyTarget)) return match;
    if (unsigned int match = findTargetMatch(bloomTargetA)) return match;
    if (unsigned int match = findTargetMatch(bloomTargetB)) return match;
    if (unsigned int match = findTargetMatch(selectionMaskTarget)) return match;

    for (const auto& [id, target] : extraPreviewTargets) {
        (void)id;
        if (unsigned int match = findTargetMatch(target)) return match;
    }
    for (const auto& [id, target] : mirrorTargets) {
        (void)id;
        if (unsigned int match = findTargetMatch(target)) return match;
    }
    for (const auto& [id, target] : uiTargets) {
        (void)id;
        if (unsigned int match = findTargetMatch(target)) return match;
    }
    for (const auto& [key, atlas] : uiTargetAtlases) {
        (void)key;
        if (unsigned int match = findTargetMatch(atlas.target)) return match;
    }

    return 0;
}

void Renderer::logPostFxDebug(const PostProcessStats& stats, bool allowHistory) const {
    (void)stats;
    (void)allowHistory;
}

void Renderer::resize(int w, int h) {
    // Defensive validation, all platforms. See RendererResizePolicy.h; the policy
    // is pure so tools/resize-selftest can exercise it headlessly.
    {
        ModuRenderer::ResizeRequest request;
        request.width = w;
        request.height = h;
        request.currentWidth = currentWidth;
        request.currentHeight = currentHeight;
        request.initialized = (framebuffer != 0 && viewportTexture != 0 && rbo != 0);
        request.resizeInProgress = resizeInProgress;
        request.shuttingDown = rendererShuttingDown;

        if (ModuRenderer::EvaluateResizeRequest(request) != ModuRenderer::ResizeDecision::Accept) {
            return;
        }
    }

#ifndef __ANDROID__
    // desktop: if GLFW's context isn't current yet (resize can fire before the window exists),
    // bail WITHOUT touching currentWidth/Height so a later resize still reallocates.
    // on Android GLFW's null backend ALWAYS says nullptr here, and running this guard there
    // gave us the squashed black-barred viewport (resize updated sizes but never reallocated).
    // EGL is current throughout the render loop, so skipping the check there is safe.
    if (glfwGetCurrentContext() == nullptr) {
        return;
    }
#endif

    // Clamp to what the driver will actually accept, then re-check that the
    // clamped size is still a real change.
    {
        GLint maxTexture = 0;
        GLint maxRenderbuffer = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
        glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderbuffer);
        const GLint limit = std::min(maxTexture > 0 ? maxTexture : 16384,
                                     maxRenderbuffer > 0 ? maxRenderbuffer : 16384);
        ModuRenderer::ClampResizeToLimit(w, h, static_cast<int>(limit));
        if (w == currentWidth && h == currentHeight) return;
    }

    // Cleared on every exit path below.
    struct ResizeGuard {
        bool& flag;
        explicit ResizeGuard(bool& f) : flag(f) { flag = true; }
        ~ResizeGuard() { flag = false; }
    } resizeGuard(resizeInProgress);

    currentWidth = w;
    currentHeight = h;

    GLint previousFramebuffer = 0;
    GLint previousTexture = 0;
    GLint previousRenderbuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    glBindTexture(GL_TEXTURE_2D, viewportTexture);
    {
        const bool sdr = (colorPrecision == RendererColorPrecision::SDR8);
        glTexImage2D(GL_TEXTURE_2D, 0, sdr ? GL_RGBA8 : GL_RGBA16F, currentWidth, currentHeight,
                     0, GL_RGBA, sdr ? GL_UNSIGNED_BYTE : GL_FLOAT, NULL);
    }
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, currentWidth, currentHeight);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer incomplete after resize!\n";
    }

    ensureRenderTarget(postTarget, currentWidth, currentHeight, false, true);
    ensureRenderTarget(historyTarget, currentWidth, currentHeight, false, true);
    ensureRenderTarget(bloomTargetA, currentWidth, currentHeight, false, true);
    ensureRenderTarget(bloomTargetB, currentWidth, currentHeight, false, true);
    ensureRenderTarget(selectionMaskTarget, currentWidth, currentHeight);
    clearHistory();
    displayTexture = viewportTexture;

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previousRenderbuffer));
}

void Renderer::beginRender(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos) {
    (void)view;
    (void)proj;
    (void)cameraPos;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    Runtime2DCountStateBind();
    glViewport(0, 0, currentWidth, currentHeight);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    displayTexture = viewportTexture;
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
    glm::mat4 model = BuildSceneObjectModelMatrix(obj);

    // Mirrored transforms reverse triangle winding; flip the front face for them so a
    // negative scale reads as a flipped object instead of an inside-out one.
    const bool mirroredTransform = glm::determinant(glm::mat3(model)) < 0.0f;
    GLint restoreFrontFace = GL_CCW;
    if (mirroredTransform) {
        glGetIntegerv(GL_FRONT_FACE, &restoreFrontFace);
        glFrontFace(restoreFrontFace == GL_CCW ? GL_CW : GL_CCW);
    }
    struct FrontFaceRestore {
        bool active;
        GLint value;
        ~FrontFaceRestore() { if (active) glFrontFace(value); }
    } frontFaceRestore{mirroredTransform, restoreFrontFace};

    const bool hasRuntimeAlbedoOverride =
        obj.runtimeHasAlbedoTextureOverride && obj.runtimeAlbedoTextureOverrideId != 0;
    bool hasMaterialAsset = !obj.materialPath.empty();
    bool hasCustomShader = !obj.vertexShaderPath.empty() || !obj.fragmentShaderPath.empty();
    bool hasAnySurfaceInput = hasRuntimeAlbedoOverride ||
                              !obj.albedoTexturePath.empty() ||
                              !obj.overlayTexturePath.empty() ||
                              !obj.normalMapPath.empty();
    bool missingMaterialAndShader = !hasMaterialAsset && !hasCustomShader && !hasAnySurfaceInput;

    shader->setMat4("model", model);
    shader->setVec3("materialColor", missingMaterialAndShader ? glm::vec3(1.0f) : obj.material.color);
    shader->setFloat("materialAlpha", missingMaterialAndShader ? 1.0f : obj.material.alpha);
    shader->setFloat("ambientStrength", obj.material.ambientStrength);
    shader->setFloat("specularStrength", obj.material.specularStrength);
    shader->setBool("specularEnabled", specularEnabled);
    shader->setFloat("shininess", obj.material.shininess);
    shader->setFloat("normalMapIntensity", obj.material.normalMapIntensity);
    shader->setFloat("mixAmount", obj.material.textureMix);
    shader->setFloat("uScrollSpeed", obj.material.scrollSpeed);
    shader->setVec2("uScrollDir", obj.material.scrollDirection);
    shader->setVec3("uCloudColor", obj.material.cloudColor);
    shader->setVec3("uCloudSkyColor", obj.material.cloudSkyColor);
    shader->setFloat("uCloudScale", obj.material.cloudScale);
    shader->setFloat("uCloudCoverage", obj.material.cloudCoverage);
    shader->setFloat("uCloudSoftness", obj.material.cloudSoftness);
    shader->setInt("uCloudDetail", obj.material.cloudDetail);
    shader->setFloat("uCloudSpeed", obj.material.cloudSpeed);
    shader->setFloat("uCloudWarp", obj.material.cloudWarp);
    shader->setFloat("uCloudHighlight", obj.material.cloudHighlight);
    shader->setFloat("uCloudStars", obj.material.cloudStars);
    shader->setFloat("uCloudHorizon", obj.material.cloudHorizon);
    shader->setVec4("uvTransform", BuildSurfaceUvTransform(&obj, obj.material));
    shader->setVec4("uvRect", BuildSpriteUvRect(obj));
    shader->setBool("unlit", obj.renderType == RenderType::Mirror || obj.renderType == RenderType::Sprite || missingMaterialAndShader);

    Texture* baseTex = texture1;
    if (missingMaterialAndShader && missingMaterialFallbackTexture != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, missingMaterialFallbackTexture);
    } else if (hasRuntimeAlbedoOverride) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, obj.runtimeAlbedoTextureOverrideId);
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
    if (skybox) {
        const SkyboxSettings& skySettings = skybox->getSettings();
        shader->setFloat("reflectionFadeStart", skySettings.reflectionDistanceFadeStart);
        shader->setFloat("reflectionFadeEnd", skySettings.reflectionDistanceFadeEnd);
        shader->setBool("fogEnabled", skySettings.fogEnabled);
        shader->setInt("fogMode", skySettings.fogMode);
        shader->setVec3("fogColor", skySettings.fogColor);
        shader->setFloat("fogStart", skySettings.fogStart);
        shader->setFloat("fogEnd", skySettings.fogEnd);
        shader->setFloat("fogDensity", skySettings.fogDensity);
        shader->setFloat("fogHeight", skySettings.fogHeight);
        shader->setFloat("fogHeightFalloff", skySettings.fogHeightFalloff);
    } else {
        shader->setBool("fogEnabled", false);
    }

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

void Renderer::renderSceneInternal(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, bool unbindFramebuffer, float fovDeg, float nearPlane, float farPlane, bool drawMirrorObjects, bool drawSkybox) {
    if (!defaultShader || width <= 0 || height <= 0) return;
    // legacy 2D cameras (orthoSize == 0) go through the 2D pipeline, not here; true 3D ortho
    // (orthoSize > 0) takes the full 3D path below.
    if (camera.orthographic && camera.orthoSize <= 0.0f) {
        glViewport(0, 0, width, height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (unbindFramebuffer) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        return;
    }
    if (!reflectionCapturePass) {
        updateSkyboxReflectionTarget();
        updateReflectionCastTargets(sceneObjects, nearPlane, farPlane);
    }

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
        int shadowKind = 0; // 0 off, 1 cube, 2 directional
        int shadowMode = 0; // 0 off, 1 hard, 2 soft
        float shadowBias = 0.02f;
        float shadowSoftness = 0.04f;
        float shadowFar = 10.0f;
        int shadowResolution = 512;
        int shadowMapIndex = -1;
        glm::mat4 shadowMatrix = glm::mat4(1.0f);
    };
    auto forwardFromRotation = [](const SceneObject& obj) {
        glm::mat4 rotation(1.0f);
        rotation = glm::rotate(rotation, glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        rotation = glm::rotate(rotation, glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        rotation = glm::rotate(rotation, glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        glm::vec3 f = glm::normalize(glm::vec3(rotation * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
        if (glm::length(f) < 1e-3f ||
            !std::isfinite(f.x) || !std::isfinite(f.y) || !std::isfinite(f.z)) {
            f = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return f;
    };
    auto buildModelMatrix = [&](const SceneObject& obj) {
        return BuildSceneObjectModelMatrix(obj, &camera.position, &camera.up);
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

    constexpr size_t kMaxLights = static_cast<size_t>(kRendererMaxRealtimeLights);
    // the Rendering Path budget caps how many lights compete for shader slots; the directional
    // allowance rides on top so the sun never loses its slot to point-light spam.
    const size_t activeLightLimit = static_cast<size_t>(std::clamp(
        std::min(maxRealtimeLights, sceneLightBudget + directionalLightAllowance),
        1, kRendererMaxRealtimeLights));
    const size_t guaranteedDirectionals = static_cast<size_t>(std::clamp(directionalLightAllowance, 1, 4));
    std::vector<LightUniform> lights;
    lights.reserve(activeLightLimit);
    size_t directionalCount = 0;

    struct LightCandidate {
        LightUniform light;
        float distSq = 0.0f;
        int id = 0;
    };
    std::vector<LightCandidate> candidates;
    candidates.reserve(sceneObjects.size());

    for (const auto& obj : sceneObjects) {
        if (!IsObjectEnabledInHierarchy(obj) || !obj.hasLight || !obj.light.enabled) continue;
        if (obj.light.type == LightType::Directional && !mainLightEnabled) continue;
        if (obj.light.type != LightType::Directional && !additionalLightsEnabled) continue;
        if (obj.light.type == LightType::Directional) {
            LightUniform l;
            l.type = 0;
            l.sourceId = obj.id;
            l.pos = obj.position;
            l.dir = forwardFromRotation(obj);
            l.color = obj.light.color;
            l.intensity = obj.light.intensity;
            l.castShadows = obj.light.castShadows;
            l.shadowMode = obj.light.castShadows ? (obj.light.softShadows ? 2 : 1) : 0;
            l.shadowBias = glm::clamp(obj.light.shadowBias, 0.0001f, 0.2f);
            l.shadowSoftness = glm::clamp(obj.light.shadowSoftness, 0.0f, 0.2f);
            l.shadowFar = glm::max(farPlane, nearPlane + 1.0f);
            l.shadowResolution = (obj.light.shadowResolution > 0)
                ? std::clamp(obj.light.shadowResolution, 128, 8192)
                : shadowMapResolution;
            if (directionalCount < guaranteedDirectionals) {
                lights.push_back(l);
                ++directionalCount;
                if (lights.size() >= activeLightLimit) break;
            } else {
                // Suns beyond the Rendering Path's allowance lose their
                // guaranteed slot and compete with the other lights.
                LightCandidate c;
                c.light = l;
                glm::vec3 delta = obj.position - camera.position;
                c.distSq = glm::dot(delta, delta);
                c.id = obj.id;
                candidates.push_back(c);
            }
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
            l.shadowResolution = (obj.light.shadowResolution > 0)
                ? std::clamp(obj.light.shadowResolution, 128, 8192)
                : shadowMapResolution;
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
            l.shadowResolution = (obj.light.shadowResolution > 0)
                ? std::clamp(obj.light.shadowResolution, 128, 8192)
                : shadowMapResolution;
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
            l.shadowResolution = (obj.light.shadowResolution > 0)
                ? std::clamp(obj.light.shadowResolution, 128, 8192)
                : shadowMapResolution;
            LightCandidate c;
            c.light = l;
            glm::vec3 delta = obj.position - camera.position;
            c.distSq = glm::dot(delta, delta);
            c.id = obj.id;
            candidates.push_back(c);
        }
    }

    if (lights.size() < activeLightLimit && !candidates.empty()) {
        const auto candidateLess = [](const LightCandidate& a, const LightCandidate& b) {
            if (a.distSq != b.distSq) return a.distSq < b.distSq;
            return a.id < b.id;
        };
        const size_t remainingSlots = activeLightLimit - lights.size();
        if (candidates.size() > remainingSlots) {
            std::nth_element(candidates.begin(),
                             candidates.begin() + remainingSlots,
                             candidates.end(),
                             candidateLess);
            candidates.resize(remainingSlots);
        }
        std::sort(candidates.begin(), candidates.end(), candidateLess);
        for (const auto& c : candidates) {
            if (lights.size() >= activeLightLimit) break;
            lights.push_back(c.light);
        }
    }

    // shadow policy gates: the camera can opt out entirely, the Lighting Manager can kill
    // main/additional casting or force hard shadows. shadowMode 0 also skips the map pass.
    for (auto& l : lights) {
        const bool isDirectional = (l.type == 0);
        bool allowShadows = camera.renderShadows &&
                            (isDirectional ? mainLightShadows : additionalLightsShadows);
        if (!allowShadows) {
            l.castShadows = false;
            l.shadowMode = 0;
        } else if (!softShadowsAllowed && l.shadowMode == 2) {
            l.shadowMode = 1;
        }
    }

    glm::mat4 view = camera.getViewMatrix();
    // The override is the XR eye frustum; see the member's comment in Rendering.h.
    // Everything downstream (frustum culling, shadow fitting, uniforms) then works
    // off the real projection rather than an approximation of it.
    glm::mat4 proj = sceneProjectionOverride
        ? *sceneProjectionOverride
        : BuildCameraProjection(camera, width, height, fovDeg, nearPlane, farPlane);
    FrustumPlanes frustum = BuildFrustumPlanes(proj * view);
    capturedSceneFrustums.push_back(frustum.planes);
    const float timeSeconds = static_cast<float>(glfwGetTime());

    auto buildDirectionalShadowMatrix = [&](const glm::vec3& lightDir) {
        // fit the map to a distance-capped frustum when a shadow max distance is set, otherwise a
        // huge far plane smears the directional map across texels nobody samples.
        glm::mat4 shadowFitProj = proj;
        // Skipped under an XR projection override: rebuilding the fit frustum with
        // BuildCameraProjection would silently swap the asymmetric eye frustum for
        // a symmetric one and fit the shadow map to the wrong volume. Using the
        // full-range eye projection instead costs some shadow texel density and is
        // always correct.
        if (!sceneProjectionOverride && shadowMaxDistance > 0.0f && shadowMaxDistance < farPlane) {
            const float shadowFar = std::max(nearPlane + 0.01f, shadowMaxDistance);
            shadowFitProj = BuildCameraProjection(camera, width, height, fovDeg, nearPlane, shadowFar);
        }
        const glm::mat4 invViewProj = glm::inverse(shadowFitProj * view);
        std::array<glm::vec3, 8> corners;
        int cornerIndex = 0;
        for (int x = 0; x <= 1; ++x) {
            for (int y = 0; y <= 1; ++y) {
                for (int z = 0; z <= 1; ++z) {
                    glm::vec4 corner = invViewProj * glm::vec4(
                        x == 0 ? -1.0f : 1.0f,
                        y == 0 ? -1.0f : 1.0f,
                        z == 0 ? -1.0f : 1.0f,
                        1.0f);
                    corners[cornerIndex++] = glm::vec3(corner) / std::max(corner.w, 0.0001f);
                }
            }
        }

        glm::vec3 center(0.0f);
        for (const glm::vec3& corner : corners) {
            center += corner;
        }
        center /= static_cast<float>(corners.size());

        glm::vec3 dir = glm::normalize(lightDir);
        if (glm::length(dir) < 1e-4f || !std::isfinite(dir.x) || !std::isfinite(dir.y) || !std::isfinite(dir.z)) {
            dir = glm::vec3(0.0f, -1.0f, 0.0f);
        }

        float radius = 0.0f;
        for (const glm::vec3& corner : corners) {
            radius = glm::max(radius, glm::length(corner - center));
        }
        radius = glm::max(radius, 5.0f);

        glm::vec3 up = (std::abs(dir.y) > 0.95f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::mat4 lightView = glm::lookAt(center - dir * (radius * 2.0f), center, up);

        glm::vec3 minBounds(std::numeric_limits<float>::max());
        glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
        for (const glm::vec3& corner : corners) {
            glm::vec3 lightSpaceCorner = glm::vec3(lightView * glm::vec4(corner, 1.0f));
            minBounds = glm::min(minBounds, lightSpaceCorner);
            maxBounds = glm::max(maxBounds, lightSpaceCorner);
        }

        const float xyPadding = glm::max(radius * 0.15f, 2.0f);
        const float zPadding = glm::max(radius * 0.5f, 10.0f);
        minBounds.x -= xyPadding;
        minBounds.y -= xyPadding;
        minBounds.z -= zPadding;
        maxBounds.x += xyPadding;
        maxBounds.y += xyPadding;
        maxBounds.z += zPadding;

        const float nearPlane = glm::max(0.1f, -maxBounds.z);
        const float farPlane = glm::max(nearPlane + 0.1f, -minBounds.z);
        glm::mat4 lightProj = glm::ortho(
            minBounds.x, maxBounds.x,
            minBounds.y, maxBounds.y,
            nearPlane, farPlane);
        return lightProj * lightView;
    };

    std::array<unsigned int, kMaxShadowMaps> shadowTextures = {0, 0, 0, 0};
    std::array<unsigned int, kMaxShadowMaps> shadowDirectionalTextures = {0, 0, 0, 0};
    std::unordered_set<int> activeCubeShadowIds;
    std::unordered_set<int> activeDirectionalShadowIds;
    auto releaseShadowCubeMap = [](ShadowCubeMap& map) {
        if (map.fbo) glDeleteFramebuffers(1, &map.fbo);
        if (map.depthCube) glDeleteTextures(1, &map.depthCube);
        map.fbo = 0;
        map.depthCube = 0;
        map.resolution = 0;
    };
    auto releaseDirectionalShadowMap = [](ShadowDirectionalMap& map) {
        if (map.fbo) glDeleteFramebuffers(1, &map.fbo);
        if (map.depthTexture) glDeleteTextures(1, &map.depthTexture);
        map.fbo = 0;
        map.depthTexture = 0;
        map.resolution = 0;
    };
    const bool hasShadowCasters = std::any_of(lights.begin(), lights.end(), [](const LightUniform& light) {
        return light.castShadows && light.sourceId >= 0;
    });
    const bool canRenderAnyShadowMaps =
        (shadowDepthShader && shadowDepthShader->ID != 0) ||
        (directionalShadowDepthShader && directionalShadowDepthShader->ID != 0);
    if (canRenderAnyShadowMaps && hasShadowCasters) {
        MODU_PROFILE_SCOPE("Shadow Pass", ProfilerSampleCategory::RenderDetail);
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
            light.shadowKind = 0;
            light.shadowMatrix = glm::mat4(1.0f);
            if (shadowSlot >= kMaxShadowMaps) continue;
            if (!light.castShadows || light.sourceId < 0) continue;

            if (light.type == 0) {
                if (!directionalShadowDepthShader || directionalShadowDepthShader->ID == 0) {
                    continue;
                }

                ShadowDirectionalMap& shadowMap = shadowDirectionalMaps[light.sourceId];
                if (shadowMap.fbo == 0 || shadowMap.depthTexture == 0 || shadowMap.resolution != light.shadowResolution) {
                    releaseDirectionalShadowMap(shadowMap);

                    glGenFramebuffers(1, &shadowMap.fbo);
                    glGenTextures(1, &shadowMap.depthTexture);
                    shadowMap.resolution = light.shadowResolution;

                    glBindTexture(GL_TEXTURE_2D, shadowMap.depthTexture);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
                                 shadowMap.resolution, shadowMap.resolution, 0,
                                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#if MODULARITY_OPENGL_ES
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#else
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
                    const float borderColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
#endif

                    glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.fbo);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap.depthTexture, 0);
                    SetDepthOnlyFramebufferDrawBuffer();
                    glReadBuffer(GL_NONE);
                    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                        std::cerr << "Failed to create directional shadow framebuffer for light " << light.sourceId << "\n";
                        releaseDirectionalShadowMap(shadowMap);
                        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
                        continue;
                    }
                    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
                    glBindTexture(GL_TEXTURE_2D, 0);
                }

                if (shadowMap.fbo == 0 || shadowMap.depthTexture == 0) continue;

                activeDirectionalShadowIds.insert(light.sourceId);
                light.shadowMapIndex = shadowSlot;
                light.shadowKind = 2;
                light.shadowFar = glm::max(light.shadowFar, 1.0f);
                light.shadowMatrix = buildDirectionalShadowMatrix(light.dir);
                shadowDirectionalTextures[shadowSlot] = shadowMap.depthTexture;
                ++shadowSlot;

                directionalShadowDepthShader->use();
                directionalShadowDepthShader->setMat4("lightSpaceMatrix", light.shadowMatrix);

                glViewport(0, 0, shadowMap.resolution, shadowMap.resolution);
                glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.fbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap.depthTexture, 0);
                SetDepthOnlyFramebufferDrawBuffer();
                glReadBuffer(GL_NONE);
                glClear(GL_DEPTH_BUFFER_BIT);

                // The shadow matrix is the full light-space ortho view-projection,
                // so its frustum is exactly the volume the GPU clips this pass to:
                // culling against it drops draws without changing the map.
                const FrustumPlanes shadowFrustum = BuildFrustumPlanes(light.shadowMatrix);
                for (const auto& obj : sceneObjects) {
                    if (!IsObjectEnabledInHierarchy(obj) || !HasRendererComponent(obj)) continue;
                    if (obj.renderType == RenderType::Mirror) continue;
                    if (obj.renderType == RenderType::Sprite) continue;
                    if (obj.id == light.sourceId) continue;
                    bool isUiCanvas3D = obj.hasUI && obj.ui.type == UIElementType::Canvas && obj.ui.renderIn3D;
                    if (isUiCanvas3D) continue;
                    if (obj.hasSkeletalAnimation && obj.skeletal.enabled) continue;

                    Mesh* shadowMesh = selectMeshForObject(obj);
                    if (!shadowMesh) continue;
                    glm::mat4 model = buildModelMatrix(obj);
                    glm::vec3 boundsCenter(0.0f);
                    float boundsRadius = 0.0f;
                    if (TryComputeObjectCullSphere(obj, model, boundsCenter, boundsRadius) &&
                        !SphereInsideFrustum(shadowFrustum, boundsCenter, boundsRadius)) {
                        continue;
                    }
                    directionalShadowDepthShader->setMat4("model", model);
                    shadowMesh->draw();
                }
                continue;
            }

            ShadowCubeMap& shadowMap = shadowCubeMaps[light.sourceId];
            if (shadowMap.fbo == 0 || shadowMap.depthCube == 0 || shadowMap.resolution != light.shadowResolution) {
                releaseShadowCubeMap(shadowMap);

                glGenFramebuffers(1, &shadowMap.fbo);
                glGenTextures(1, &shadowMap.depthCube);
                shadowMap.resolution = light.shadowResolution;

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
                SetDepthOnlyFramebufferDrawBuffer();
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

            activeCubeShadowIds.insert(light.sourceId);
            light.shadowFar = glm::max(light.shadowFar, 1.0f);
            light.shadowMapIndex = shadowSlot;
            light.shadowKind = 1;
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
                SetDepthOnlyFramebufferDrawBuffer();
                glReadBuffer(GL_NONE);
                glClear(GL_DEPTH_BUFFER_BIT);
                shadowDepthShader->setMat4("lightSpaceMatrix", shadowProj * shadowViews[face]);

                for (const auto& obj : sceneObjects) {
                    if (!IsObjectEnabledInHierarchy(obj) || !HasRendererComponent(obj)) continue;
                    if (obj.renderType == RenderType::Mirror) continue;
                    if (obj.renderType == RenderType::Sprite) continue;
                    if (obj.id == light.sourceId) continue;
                    bool isUiCanvas3D = obj.hasUI && obj.ui.type == UIElementType::Canvas && obj.ui.renderIn3D;
                    if (isUiCanvas3D) continue;
                    if (obj.hasSkeletalAnimation && obj.skeletal.enabled) continue;

                    Mesh* shadowMesh = selectMeshForObject(obj);
                    if (!shadowMesh) continue;
                    glm::mat4 model = buildModelMatrix(obj);
                    glm::vec3 boundsCenter(0.0f);
                    float boundsRadius = 0.0f;
                    if (TryComputeObjectCullSphere(obj, model, boundsCenter, boundsRadius)) {
                        glm::vec3 lightDelta = boundsCenter - light.pos;
                        float maxDistance = light.shadowFar + boundsRadius;
                        if (glm::dot(lightDelta, lightDelta) > maxDistance * maxDistance) {
                            continue;
                        }
                    }

                    shadowDepthShader->setMat4("model", model);
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
        if (activeCubeShadowIds.find(it->first) == activeCubeShadowIds.end()) {
            releaseShadowCubeMap(it->second);
            it = shadowCubeMaps.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = shadowDirectionalMaps.begin(); it != shadowDirectionalMaps.end(); ) {
        if (activeDirectionalShadowIds.find(it->first) == activeDirectionalShadowIds.end()) {
            releaseDirectionalShadowMap(it->second);
            it = shadowDirectionalMaps.erase(it);
        } else {
            ++it;
        }
    }

    GLboolean cullFace = glIsEnabled(GL_CULL_FACE);
    GLint prevCullMode = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &prevCullMode);
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    bool currentBlendEnabled = (blendEnabled == GL_TRUE);
    bool currentDepthMask = (depthMask == GL_TRUE);
    GLint prevFrontFace = GL_CCW;
    glGetIntegerv(GL_FRONT_FACE, &prevFrontFace);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    bool currentCullFaceEnabled = true;
    // Mirrored transforms (negative scale on an odd number of axes) reverse triangle
    // winding. Without flipping the front face they cull the wrong side and the mesh
    // reads as inside out, so track winding the same way culling is tracked.
    bool currentFrontFaceCCW = true;

    for (int slot = 0; slot < kMaxShadowMaps; ++slot) {
        glActiveTexture(GL_TEXTURE3 + slot);
        glBindTexture(GL_TEXTURE_CUBE_MAP, shadowTextures[slot]);
    }
    for (int slot = 0; slot < kMaxShadowMaps; ++slot) {
        glActiveTexture(GL_TEXTURE7 + slot);
        glBindTexture(GL_TEXTURE_2D, shadowDirectionalTextures[slot]);
    }
    glActiveTexture(GL_TEXTURE0);

    bool skyboxPending = false;
    if (drawSkybox && camera.solidBackground) {
        // Solid Color background: replace the skybox with the camera's clear
        // color (the caller already cleared depth).
        glClearColor(camera.backgroundColor.r, camera.backgroundColor.g,
                     camera.backgroundColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    } else if (drawSkybox) {
        // The skybox fullscreen triangle sits at depth 1.0 and draws with
        // GL_LEQUAL, so it is deferred until after the opaque items: early-Z
        // then rejects every sky pixel hidden behind geometry instead of
        // shading the whole screen with the procedural sky and overdrawing it.
        skyboxPending = true;
    }
    rebuildStaticMergeBatches(sceneObjects);

    const std::string emptyPath;
    auto combineHash = [](uint64_t seed, uint64_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        return seed;
    };
    auto buildOpaqueSortKey = [&](const std::string& vert,
                                  const std::string& frag,
                                  const std::string& material,
                                  const std::string& albedo,
                                  const std::string& overlay,
                                  const std::string& normal) {
        uint64_t key = 1469598103934665603ull;
        key = combineHash(key, HashStringFNV1a(vert));
        key = combineHash(key, HashStringFNV1a(frag));
        key = combineHash(key, HashStringFNV1a(material));
        key = combineHash(key, HashStringFNV1a(albedo));
        key = combineHash(key, HashStringFNV1a(overlay));
        key = combineHash(key, HashStringFNV1a(normal));
        return key;
    };

    using RenderPathRef = const std::string*;

    struct RenderItem {
        const SceneObject* obj = nullptr;
        const StaticMergeBatch* staticBatch = nullptr;
        Mesh* mesh = nullptr;
        glm::mat4 model = glm::mat4(1.0f);
        glm::vec3 sortCenter = glm::vec3(0.0f);
        // Transparent draw order is decided by this point, so it is the middle of the
        // geometry, not the pivot. sortCenter above stays on the pivot because reflection
        // cast selection keys off it and re-homing that would move probe assignments in
        // already-tuned scenes.
        glm::vec3 depthSortCenter = glm::vec3(0.0f);
        RenderPathRef vertPath = nullptr;
        RenderPathRef fragPath = nullptr;
        MaterialProperties material = MaterialProperties{};
        RenderPathRef materialPath = nullptr;
        RenderPathRef albedoTexturePath = nullptr;
        RenderPathRef overlayTexturePath = nullptr;
        RenderPathRef normalMapPath = nullptr;
        bool useOverlay = false;
        int boneLimit = 0;
        int availableBones = 0;
        bool wantsGpuSkinning = false;
        bool isUiCanvas3D = false;
        int uiSortingOrder = 0;
        bool requiresBlending = false;
        bool sortOpaque = false;
        bool unlit = false;
        bool doubleSided = false;
        float cameraDepth = 0.0f;
        float cameraDistanceSq = 0.0f;
        uint64_t opaqueSortKey = 0;
    };

    std::vector<RenderItem> drawItems;
    drawItems.reserve(sceneObjects.size() + staticMergeBatches.size());

    // static merge batches fold arbitrary layers into one mesh, so a camera with a filtered
    // culling mask draws the sources individually instead ( correctness over the speedup ).
    const bool cullingMaskAllowsAll = (camera.cullingMask == 0xFFFFFFFFu);

    auto gatherRenderItemsRange = [&](size_t beginIndex, size_t endIndex) {
        std::vector<RenderItem> localItems;
        localItems.reserve(endIndex > beginIndex ? (endIndex - beginIndex) : 0);

        for (size_t objIndex = beginIndex; objIndex < endIndex; ++objIndex) {
            const auto& obj = sceneObjects[objIndex];
            if (!IsObjectEnabledInHierarchy(obj)) continue;
            if (!cullingMaskAllowsAll &&
                (camera.cullingMask & (1u << (static_cast<uint32_t>(obj.layer) & 31u))) == 0u) continue;
            if (!drawMirrorObjects && obj.hasRenderer && obj.renderType == RenderType::Mirror) continue;
            if (!HasRendererComponent(obj)) continue;
            if (cullingMaskAllowsAll &&
                staticMergeSourceIds.find(obj.id) != staticMergeSourceIds.end()) continue;

            glm::mat4 model = buildModelMatrix(obj);
            glm::vec3 boundsCenter(0.0f);
            float boundsRadius = 0.0f;
            const bool hasBounds = TryComputeObjectCullSphere(obj, model, boundsCenter, boundsRadius);
            if (hasBounds && !SphereInsideFrustum(frustum, boundsCenter, boundsRadius)) {
                continue;
            }

            Mesh* mesh = selectMeshForObject(obj);
            if (!mesh) continue;

            RenderItem item;
            item.obj = &obj;
            item.mesh = mesh;
            item.model = model;
            item.sortCenter = obj.position;
            // A model authored with its origin at the base ( or at the parent's origin, which
            // is what a room's worth of imported props share ) sorts as much nearer or further
            // than it actually draws. That is what let a transparent mesh standing behind a
            // Render-In-3D canvas paint over it. The cull sphere above already has the world
            // space centre, so this costs nothing extra.
            item.depthSortCenter = hasBounds ? boundsCenter : obj.position;
            item.vertPath = &obj.vertexShaderPath;
            item.fragPath = &obj.fragmentShaderPath;
            item.material = obj.material;
            item.materialPath = &obj.materialPath;
            item.albedoTexturePath = &obj.albedoTexturePath;
            item.overlayTexturePath = &obj.overlayTexturePath;
            item.normalMapPath = &obj.normalMapPath;
            item.useOverlay = obj.useOverlay;
            item.boneLimit = obj.skeletal.maxBones;
            item.availableBones = static_cast<int>(obj.skeletal.finalMatrices.size());
            bool needsFallback = obj.hasSkeletalAnimation && obj.skeletal.enabled &&
                                 obj.skeletal.allowCpuFallback &&
                                 item.boneLimit > 0 && item.availableBones > item.boneLimit;
            item.wantsGpuSkinning = obj.hasSkeletalAnimation && obj.skeletal.enabled &&
                                    obj.skeletal.useGpuSkinning && !needsFallback;
            if (item.wantsGpuSkinning && item.vertPath->empty()) {
                item.vertPath = &skinnedVertPath;
            }
            item.isUiCanvas3D = obj.hasUI && obj.ui.type == UIElementType::Canvas && obj.ui.renderIn3D;
            item.uiSortingOrder = item.isUiCanvas3D ? obj.ui.sortingOrder : 0;
            item.requiresBlending = item.material.alpha < 0.999f || item.isUiCanvas3D;
            item.unlit = obj.renderType == RenderType::Mirror || obj.renderType == RenderType::Sprite || item.isUiCanvas3D;
            item.doubleSided =
                obj.renderType == RenderType::Sprite ||
                obj.renderType == RenderType::Mirror;
            glm::vec3 viewSpaceCenter = glm::vec3(view * glm::vec4(item.depthSortCenter, 1.0f));
            item.cameraDepth = -viewSpaceCenter.z;
            glm::vec3 toCamera = item.depthSortCenter - camera.position;
            item.cameraDistanceSq = glm::dot(toCamera, toCamera);
            localItems.push_back(std::move(item));
        }

        return localItems;
    };

    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const size_t minItemsPerTask = 2048;
    const size_t maxTaskCountByWork = std::max<size_t>(1, sceneObjects.size() / minItemsPerTask);
    const size_t taskCount = (hardwareThreads > 4 && sceneObjects.size() >= minItemsPerTask * 2)
        ? std::min<size_t>(hardwareThreads, maxTaskCountByWork)
        : 1;

    if (taskCount <= 1) {
        drawItems = gatherRenderItemsRange(0, sceneObjects.size());
    } else {
        std::vector<std::future<std::vector<RenderItem>>> gatherFutures;
        gatherFutures.reserve(taskCount);
        const size_t chunkSize = (sceneObjects.size() + taskCount - 1) / taskCount;

        for (size_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
            const size_t beginIndex = taskIndex * chunkSize;
            if (beginIndex >= sceneObjects.size()) {
                break;
            }
            const size_t endIndex = std::min(sceneObjects.size(), beginIndex + chunkSize);
            gatherFutures.push_back(std::async(std::launch::async,
                [&, beginIndex, endIndex]() {
                    return gatherRenderItemsRange(beginIndex, endIndex);
                }));
        }

        for (auto& future : gatherFutures) {
            std::vector<RenderItem> chunkItems = future.get();
            drawItems.insert(drawItems.end(),
                             std::make_move_iterator(chunkItems.begin()),
                             std::make_move_iterator(chunkItems.end()));
        }
    }

    for (const auto& batch : staticMergeBatches) {
        if (!cullingMaskAllowsAll) break; // masked cameras drew the sources individually
        if (!batch.mesh) continue;
        if (batch.boundsRadius > 0.0f &&
            !SphereInsideFrustum(frustum, batch.boundsCenter, batch.boundsRadius)) {
            continue;
        }

        RenderItem item;
        item.staticBatch = &batch;
        item.mesh = batch.mesh.get();
        item.model = glm::mat4(1.0f);
        item.sortCenter = batch.boundsCenter;
        item.depthSortCenter = batch.boundsCenter;
        item.vertPath = &batch.vertPath;
        item.fragPath = &batch.fragPath;
        item.material = batch.material;
        item.materialPath = &batch.materialPath;
        item.albedoTexturePath = &batch.albedoTexturePath;
        item.overlayTexturePath = &batch.overlayTexturePath;
        item.normalMapPath = &batch.normalMapPath;
        item.useOverlay = batch.useOverlay;
        item.requiresBlending = item.material.alpha < 0.999f;
        item.unlit = batch.unlit;
        item.doubleSided = batch.doubleSided;
        glm::vec3 viewSpaceCenter = glm::vec3(view * glm::vec4(item.depthSortCenter, 1.0f));
        item.cameraDepth = -viewSpaceCenter.z;
        glm::vec3 toCamera = item.depthSortCenter - camera.position;
        item.cameraDistanceSq = glm::dot(toCamera, toCamera);
        drawItems.push_back(std::move(item));
    }

    auto classifyAlphaBehavior = [&](RenderItem& item) {
        if (item.requiresBlending) {
            item.sortOpaque = false;
            return;
        }

        if (item.albedoTexturePath && !item.albedoTexturePath->empty()) {
            if (Texture* tex = getTexture(*item.albedoTexturePath, item.material.textureFilter)) {
                if (tex->UsesAlphaBlending()) {
                    item.requiresBlending = true;
                }
            }
        }

        item.sortOpaque = !item.requiresBlending;
        if (item.sortOpaque) {
            item.opaqueSortKey = buildOpaqueSortKey(
                item.vertPath ? *item.vertPath : emptyPath,
                item.fragPath ? *item.fragPath : emptyPath,
                item.materialPath ? *item.materialPath : emptyPath,
                item.albedoTexturePath ? *item.albedoTexturePath : emptyPath,
                item.overlayTexturePath ? *item.overlayTexturePath : emptyPath,
                item.normalMapPath ? *item.normalMapPath : emptyPath);
        }
    };

    for (RenderItem& item : drawItems) {
        classifyAlphaBehavior(item);
    }

    if (drawItems.size() > 1) {
        std::stable_sort(drawItems.begin(), drawItems.end(),
                         [](const RenderItem& a, const RenderItem& b) {
                             if (a.sortOpaque != b.sortOpaque) return a.sortOpaque > b.sortOpaque;
                             if (!a.sortOpaque) {
                                 if (a.cameraDepth != b.cameraDepth) {
                                     return a.cameraDepth > b.cameraDepth;
                                 }
                                 if (a.cameraDistanceSq != b.cameraDistanceSq) {
                                     return a.cameraDistanceSq > b.cameraDistanceSq;
                                 }
                                 int aId = a.obj ? a.obj->id : -1;
                                 int bId = b.obj ? b.obj->id : -1;
                                 return aId < bId;
                             }
                             if (a.opaqueSortKey != b.opaqueSortKey) return a.opaqueSortKey < b.opaqueSortKey;
                             int aId = a.obj ? a.obj->id : -1;
                             int bId = b.obj ? b.obj->id : -1;
                             return aId < bId;
                         });

        // future me: Render-In-3D UI canvases are flat quads that overlap from the camera's
        // view, so pure depth sorting ignores the Sort Order you set in the inspector (that's
        // why a button on Sort Order 10 still drew under a circle on -5). Sort Order only
        // ranks canvases against each other though, and a rule that narrow cannot live inside
        // the comparator: it made the ordering intransitive. Canvas A (Sort Order 10, far)
        // sorted before mesh M, M sorted before canvas B (Sort Order 0, near), and B sorted
        // before A -- a cycle. std::stable_sort on a comparator that is not a strict weak
        // ordering is undefined, and in practice it shuffles the ENTIRE transparent queue, so
        // one canvas plus one transparent mesh anywhere in the scene was enough to drop a
        // canvas underneath geometry standing behind it.
        //
        // Apply Sort Order as a second pass instead. The canvases keep the exact slots depth
        // sorting handed them and only their order within those slots follows Sort Order
        // (higher value later -> painted on top), which is the behavior the comparator was
        // reaching for, minus the cycle. Regular transparent geometry keeps camera-depth
        // ordering untouched.
        std::vector<size_t> canvasSlots;
        for (size_t i = 0; i < drawItems.size(); ++i) {
            if (!drawItems[i].sortOpaque && drawItems[i].isUiCanvas3D) {
                canvasSlots.push_back(i);
            }
        }
        if (canvasSlots.size() > 1) {
            std::vector<RenderItem> canvasItems;
            canvasItems.reserve(canvasSlots.size());
            for (size_t slot : canvasSlots) {
                canvasItems.push_back(drawItems[slot]);
            }
            std::stable_sort(canvasItems.begin(), canvasItems.end(),
                             [](const RenderItem& a, const RenderItem& b) {
                                 return a.uiSortingOrder < b.uiSortingOrder;
                             });
            for (size_t i = 0; i < canvasSlots.size(); ++i) {
                drawItems[canvasSlots[i]] = canvasItems[i];
            }
        }
    }

    GLint currentActiveTexture = GL_TEXTURE0;
    std::array<GLuint, 8> boundTexture2D = {
        std::numeric_limits<GLuint>::max(), std::numeric_limits<GLuint>::max(),
        std::numeric_limits<GLuint>::max(), std::numeric_limits<GLuint>::max(),
        std::numeric_limits<GLuint>::max(), std::numeric_limits<GLuint>::max(),
        std::numeric_limits<GLuint>::max(), std::numeric_limits<GLuint>::max()
    };
    auto bindTexture2D = [&](GLenum unit, GLuint textureId) {
        if (currentActiveTexture != static_cast<GLint>(unit)) {
            glActiveTexture(unit);
            currentActiveTexture = static_cast<GLint>(unit);
            Runtime2DCountStateBind();
        }
        int slot = static_cast<int>(unit - GL_TEXTURE0);
        if (slot >= 0 && slot < static_cast<int>(boundTexture2D.size()) &&
            boundTexture2D[slot] == textureId) {
            return;
        }
        glBindTexture(GL_TEXTURE_2D, textureId);
        Runtime2DCountTextureBind();
        if (slot >= 0 && slot < static_cast<int>(boundTexture2D.size())) {
            boundTexture2D[slot] = textureId;
        }
    };

    auto flushPendingSkybox = [&]() {
        if (!skyboxPending) return;
        skyboxPending = false;
        if (camera.orthographic) {
            // An orthographic projection has no vanishing point for the skybox
            // to project through, so give it a perspective matrix like Unity does.
            const float aspect = static_cast<float>(std::max(1, width)) /
                                 static_cast<float>(std::max(1, height));
            renderSkybox(view, glm::perspective(glm::radians(glm::clamp(fovDeg, 1.0f, 179.0f)),
                                                aspect, nearPlane, farPlane));
        } else {
            renderSkybox(view, proj);
        }
        // Skybox::draw leaves texture units 0-2 unbound with unit 0 active;
        // sync the loop's bind caches with that state.
        boundTexture2D[0] = 0;
        boundTexture2D[1] = 0;
        boundTexture2D[2] = 0;
        currentActiveTexture = GL_TEXTURE0;
    };

    struct LightUniformNameCache {
        std::array<std::string, kMaxLights> type;
        std::array<std::string, kMaxLights> dir;
        std::array<std::string, kMaxLights> pos;
        std::array<std::string, kMaxLights> color;
        std::array<std::string, kMaxLights> intensity;
        std::array<std::string, kMaxLights> range;
        std::array<std::string, kMaxLights> innerCos;
        std::array<std::string, kMaxLights> outerCos;
        std::array<std::string, kMaxLights> areaSize;
        std::array<std::string, kMaxLights> areaFade;
        std::array<std::string, kMaxLights> shadowMap;
        std::array<std::string, kMaxLights> shadowKind;
        std::array<std::string, kMaxLights> shadowMode;
        std::array<std::string, kMaxLights> shadowBias;
        std::array<std::string, kMaxLights> shadowSoftness;
        std::array<std::string, kMaxLights> shadowFar;
        std::array<std::string, kMaxLights> shadowMatrix;
    };
    static const LightUniformNameCache kLightNames = []() {
        LightUniformNameCache names;
        for (size_t i = 0; i < kMaxLights; ++i) {
            const std::string idx = "[" + std::to_string(i) + "]";
            names.type[i] = "lightTypeArr" + idx;
            names.dir[i] = "lightDirArr" + idx;
            names.pos[i] = "lightPosArr" + idx;
            names.color[i] = "lightColorArr" + idx;
            names.intensity[i] = "lightIntensityArr" + idx;
            names.range[i] = "lightRangeArr" + idx;
            names.innerCos[i] = "lightInnerCosArr" + idx;
            names.outerCos[i] = "lightOuterCosArr" + idx;
            names.areaSize[i] = "lightAreaSizeArr" + idx;
            names.areaFade[i] = "lightAreaFadeArr" + idx;
            names.shadowMap[i] = "lightShadowMapArr" + idx;
            names.shadowKind[i] = "lightShadowKindArr" + idx;
            names.shadowMode[i] = "lightShadowModeArr" + idx;
            names.shadowBias[i] = "lightShadowBiasArr" + idx;
            names.shadowSoftness[i] = "lightShadowSoftnessArr" + idx;
            names.shadowFar[i] = "lightShadowFarArr" + idx;
            names.shadowMatrix[i] = "lightShadowMatrixArr" + idx;
        }
        return names;
    }();

    Shader* currentShader = nullptr;
    auto findReflectionCastForItem = [&](const RenderItem& item, float& outIntensity) -> unsigned int {
        outIntensity = 0.0f;
        if (reflectionCastTargets.empty()) return 0;
        float bestDistSq = std::numeric_limits<float>::max();
        unsigned int bestCube = 0;
        float bestIntensity = 0.0f;
        for (const SceneObject& probeObj : sceneObjects) {
            if (!IsObjectEnabledInHierarchy(probeObj) || !probeObj.hasReflectionCast || !probeObj.reflectionCast.enabled) continue;
            auto targetIt = reflectionCastTargets.find(probeObj.id);
            if (targetIt == reflectionCastTargets.end() || targetIt->second.cube == 0 || !targetIt->second.hasCapture) continue;
            const glm::vec3 delta = item.sortCenter - probeObj.position;
            const float distSq = glm::dot(delta, delta);
            const float range = std::max(0.01f, probeObj.reflectionCast.blendDistance);
            if (distSq > range * range || distSq >= bestDistSq) continue;
            bestDistSq = distSq;
            bestCube = targetIt->second.cube;
            const float distance = std::sqrt(std::max(0.0f, distSq));
            bestIntensity = probeObj.reflectionCast.intensity * glm::clamp(1.0f - distance / range, 0.0f, 1.0f);
        }
        outIntensity = bestIntensity;
        return bestCube;
    };

    struct MaterialUniformCache {
        bool valid = false;
        bool unlit = false;
        glm::vec3 materialColor{0.0f};
        float materialAlpha = 0.0f;
        float ambientStrength = 0.0f;
        float specularStrength = 0.0f;
        float shininess = 0.0f;
        float normalMapIntensity = 0.0f;
        float mixAmount = 0.0f;
        float scrollSpeed = -1.0f;
        glm::vec2 scrollDirection{0.0f};
        glm::vec3 cloudColor{-1.0f};
        glm::vec3 cloudSkyColor{-1.0f};
        float cloudScale = -1.0f;
        float cloudCoverage = -1.0f;
        float cloudSoftness = -1.0f;
        int cloudDetail = -1;
        float cloudSpeed = -1.0f;
        float cloudWarp = -1.0f;
        float cloudHighlight = -1.0f;
        float cloudStars = -1.0f;
        float cloudHorizon = -1.0f;
        glm::vec4 uvTransform{0.0f};
        glm::vec4 uvRect{0.0f};
        bool useSkinning = false;
        int boneCount = -1;
        bool hasOverlay = false;
        bool hasNormalMap = false;
        bool hasReflectionCast = false;
        float reflectionIntensity = 0.0f;
        float reflectionFadeStart = -1.0f;
        float reflectionFadeEnd = -1.0f;
        bool fogEnabled = false;
        int fogMode = -1;
        glm::vec3 fogColor{0.0f};
        float fogStart = -1.0f;
        float fogEnd = -1.0f;
        float fogDensity = -1.0f;
        float fogHeight = 0.0f;
        float fogHeightFalloff = -1.0f;
    } matCache;
    auto invalidateMatCache = [&]() { matCache = MaterialUniformCache{}; };
    for (const RenderItem& item : drawItems) {
        // Items are sorted opaque-first; the sky has to be in place before the
        // first blended item so transparency composites over it.
        if (skyboxPending && !item.sortOpaque) {
            flushPendingSkybox();
        }
        const SceneObject* objPtr = item.obj;
        const std::string& vertPath = item.vertPath ? *item.vertPath : emptyPath;
        const std::string& fragPath = item.fragPath ? *item.fragPath : emptyPath;
        const std::string& materialPath = item.materialPath ? *item.materialPath : emptyPath;
        const std::string& albedoTexturePath = item.albedoTexturePath ? *item.albedoTexturePath : emptyPath;
        const std::string& overlayTexturePath = item.overlayTexturePath ? *item.overlayTexturePath : emptyPath;
        const std::string& normalMapPath = item.normalMapPath ? *item.normalMapPath : emptyPath;

        Shader* active = getShader(vertPath, fragPath);
        if (!active) continue;
        shader = active;
        if (currentShader != shader) {
            currentShader = shader;
            shader->use();
            invalidateMatCache();
            Runtime2DCountStateBind();
            shader->setMat4("view", view);
            shader->setMat4("projection", proj);
            shader->setVec3("viewPos", camera.position);
            shader->setFloat("uTime", timeSeconds);
            shader->setVec3("ambientColor", ambientColor);
            shader->setBool("specularEnabled", specularEnabled);
            shader->setInt("texture1", 0);
            shader->setInt("overlayTex", 1);
            shader->setInt("normalMap", 2);
            shader->setInt("shadowCube0", 3);
            shader->setInt("shadowCube1", 4);
            shader->setInt("shadowCube2", 5);
            shader->setInt("shadowCube3", 6);
            shader->setInt("dirShadow0", 7);
            shader->setInt("dirShadow1", 8);
            shader->setInt("dirShadow2", 9);
            shader->setInt("dirShadow3", 10);
            shader->setInt("reflectionCube", 11);
            const SkyboxSettings skySettings = skybox ? skybox->getSettings() : SkyboxSettings{};
            shader->setFloat("reflectionFadeStart", skySettings.reflectionDistanceFadeStart);
            shader->setFloat("reflectionFadeEnd", skySettings.reflectionDistanceFadeEnd);
            shader->setBool("fogEnabled", skybox != nullptr && skySettings.fogEnabled);
            shader->setInt("fogMode", skySettings.fogMode);
            shader->setVec3("fogColor", skySettings.fogColor);
            shader->setFloat("fogStart", skySettings.fogStart);
            shader->setFloat("fogEnd", skySettings.fogEnd);
            shader->setFloat("fogDensity", skySettings.fogDensity);
            shader->setFloat("fogHeight", skySettings.fogHeight);
            shader->setFloat("fogHeightFalloff", skySettings.fogHeightFalloff);
            shader->setInt("lightCount", static_cast<int>(lights.size()));
            for (size_t i = 0; i < lights.size() && i < kMaxLights; ++i) {
                const auto& l = lights[i];
                shader->setInt(kLightNames.type[i], l.type);
                shader->setVec3(kLightNames.dir[i], l.dir);
                shader->setVec3(kLightNames.pos[i], l.pos);
                shader->setVec3(kLightNames.color[i], l.color);
                shader->setFloat(kLightNames.intensity[i], l.intensity);
                shader->setFloat(kLightNames.range[i], l.range);
                shader->setFloat(kLightNames.innerCos[i], l.inner);
                shader->setFloat(kLightNames.outerCos[i], l.outer);
                shader->setVec2(kLightNames.areaSize[i], l.areaSize);
                shader->setFloat(kLightNames.areaFade[i], l.areaFade);
                shader->setInt(kLightNames.shadowMap[i], l.shadowMapIndex);
                shader->setInt(kLightNames.shadowKind[i], l.shadowKind);
                shader->setInt(kLightNames.shadowMode[i], (l.shadowMapIndex >= 0) ? l.shadowMode : 0);
                shader->setFloat(kLightNames.shadowBias[i], l.shadowBias);
                shader->setFloat(kLightNames.shadowSoftness[i], l.shadowSoftness);
                shader->setFloat(kLightNames.shadowFar[i], l.shadowFar);
                shader->setMat4(kLightNames.shadowMatrix[i], l.shadowMatrix);
            }
        }

        const SkyboxSettings skySettings = skybox ? skybox->getSettings() : SkyboxSettings{};
        const bool wantFogEnabled = skybox != nullptr && skySettings.fogEnabled;
        if (!matCache.valid || matCache.reflectionFadeStart != skySettings.reflectionDistanceFadeStart) {
            shader->setFloat("reflectionFadeStart", skySettings.reflectionDistanceFadeStart);
            matCache.reflectionFadeStart = skySettings.reflectionDistanceFadeStart;
        }
        if (!matCache.valid || matCache.reflectionFadeEnd != skySettings.reflectionDistanceFadeEnd) {
            shader->setFloat("reflectionFadeEnd", skySettings.reflectionDistanceFadeEnd);
            matCache.reflectionFadeEnd = skySettings.reflectionDistanceFadeEnd;
        }
        if (!matCache.valid || matCache.fogEnabled != wantFogEnabled) {
            shader->setBool("fogEnabled", wantFogEnabled);
            matCache.fogEnabled = wantFogEnabled;
        }
        if (!matCache.valid || matCache.fogMode != skySettings.fogMode) {
            shader->setInt("fogMode", skySettings.fogMode);
            matCache.fogMode = skySettings.fogMode;
        }
        if (!matCache.valid || matCache.fogColor != skySettings.fogColor) {
            shader->setVec3("fogColor", skySettings.fogColor);
            matCache.fogColor = skySettings.fogColor;
        }
        if (!matCache.valid || matCache.fogStart != skySettings.fogStart) {
            shader->setFloat("fogStart", skySettings.fogStart);
            matCache.fogStart = skySettings.fogStart;
        }
        if (!matCache.valid || matCache.fogEnd != skySettings.fogEnd) {
            shader->setFloat("fogEnd", skySettings.fogEnd);
            matCache.fogEnd = skySettings.fogEnd;
        }
        if (!matCache.valid || matCache.fogDensity != skySettings.fogDensity) {
            shader->setFloat("fogDensity", skySettings.fogDensity);
            matCache.fogDensity = skySettings.fogDensity;
        }
        if (!matCache.valid || matCache.fogHeight != skySettings.fogHeight) {
            shader->setFloat("fogHeight", skySettings.fogHeight);
            matCache.fogHeight = skySettings.fogHeight;
        }
        if (!matCache.valid || matCache.fogHeightFalloff != skySettings.fogHeightFalloff) {
            shader->setFloat("fogHeightFalloff", skySettings.fogHeightFalloff);
            matCache.fogHeightFalloff = skySettings.fogHeightFalloff;
        }

        const bool hasRuntimeAlbedoOverride =
            objPtr != nullptr &&
            objPtr->runtimeHasAlbedoTextureOverride &&
            objPtr->runtimeAlbedoTextureOverrideId != 0;
        const UiTargetInfo* uiTarget = nullptr;
        if (item.isUiCanvas3D && objPtr != nullptr) {
            auto uiIt = uiTargetViews.find(objPtr->id);
            if (uiIt != uiTargetViews.end() && uiIt->second.texture != 0) {
                uiTarget = &uiIt->second;
            }
        }
        bool hasMaterialAsset = !materialPath.empty();
        bool hasCustomShader = !vertPath.empty() || !fragPath.empty();
        bool hasAnySurfaceInput = hasRuntimeAlbedoOverride ||
                                  !albedoTexturePath.empty() ||
                                  !overlayTexturePath.empty() ||
                                  !normalMapPath.empty();
        bool missingMaterialAndShader = !hasMaterialAsset && !hasCustomShader && !hasAnySurfaceInput;

        bool wantUnlit = item.unlit || missingMaterialAndShader;
        if (!matCache.valid || matCache.unlit != wantUnlit) {
            shader->setBool("unlit", wantUnlit);
            matCache.unlit = wantUnlit;
        }

        shader->setMat4("model", item.model);

        glm::vec3 wantColor = missingMaterialAndShader ? glm::vec3(1.0f) : item.material.color;
        if (!matCache.valid || matCache.materialColor != wantColor) {
            shader->setVec3("materialColor", wantColor);
            matCache.materialColor = wantColor;
        }
        float wantAlpha = missingMaterialAndShader ? 1.0f : item.material.alpha;
        if (!matCache.valid || matCache.materialAlpha != wantAlpha) {
            shader->setFloat("materialAlpha", wantAlpha);
            matCache.materialAlpha = wantAlpha;
        }
        if (!matCache.valid || matCache.ambientStrength != item.material.ambientStrength) {
            shader->setFloat("ambientStrength", item.material.ambientStrength);
            matCache.ambientStrength = item.material.ambientStrength;
        }
        if (!matCache.valid || matCache.specularStrength != item.material.specularStrength) {
            shader->setFloat("specularStrength", item.material.specularStrength);
            matCache.specularStrength = item.material.specularStrength;
        }
        if (!matCache.valid || matCache.shininess != item.material.shininess) {
            shader->setFloat("shininess", item.material.shininess);
            matCache.shininess = item.material.shininess;
        }
        if (!matCache.valid || matCache.normalMapIntensity != item.material.normalMapIntensity) {
            shader->setFloat("normalMapIntensity", item.material.normalMapIntensity);
            matCache.normalMapIntensity = item.material.normalMapIntensity;
        }
        if (!matCache.valid || matCache.mixAmount != item.material.textureMix) {
            shader->setFloat("mixAmount", item.material.textureMix);
            matCache.mixAmount = item.material.textureMix;
        }
        if (!matCache.valid || matCache.scrollSpeed != item.material.scrollSpeed) {
            shader->setFloat("uScrollSpeed", item.material.scrollSpeed);
            matCache.scrollSpeed = item.material.scrollSpeed;
        }
        if (!matCache.valid || matCache.scrollDirection != item.material.scrollDirection) {
            shader->setVec2("uScrollDir", item.material.scrollDirection);
            matCache.scrollDirection = item.material.scrollDirection;
        }
        if (!matCache.valid || matCache.cloudColor != item.material.cloudColor) {
            shader->setVec3("uCloudColor", item.material.cloudColor);
            matCache.cloudColor = item.material.cloudColor;
        }
        if (!matCache.valid || matCache.cloudSkyColor != item.material.cloudSkyColor) {
            shader->setVec3("uCloudSkyColor", item.material.cloudSkyColor);
            matCache.cloudSkyColor = item.material.cloudSkyColor;
        }
        if (!matCache.valid || matCache.cloudScale != item.material.cloudScale) {
            shader->setFloat("uCloudScale", item.material.cloudScale);
            matCache.cloudScale = item.material.cloudScale;
        }
        if (!matCache.valid || matCache.cloudCoverage != item.material.cloudCoverage) {
            shader->setFloat("uCloudCoverage", item.material.cloudCoverage);
            matCache.cloudCoverage = item.material.cloudCoverage;
        }
        if (!matCache.valid || matCache.cloudSoftness != item.material.cloudSoftness) {
            shader->setFloat("uCloudSoftness", item.material.cloudSoftness);
            matCache.cloudSoftness = item.material.cloudSoftness;
        }
        if (!matCache.valid || matCache.cloudDetail != item.material.cloudDetail) {
            shader->setInt("uCloudDetail", item.material.cloudDetail);
            matCache.cloudDetail = item.material.cloudDetail;
        }
        if (!matCache.valid || matCache.cloudSpeed != item.material.cloudSpeed) {
            shader->setFloat("uCloudSpeed", item.material.cloudSpeed);
            matCache.cloudSpeed = item.material.cloudSpeed;
        }
        if (!matCache.valid || matCache.cloudWarp != item.material.cloudWarp) {
            shader->setFloat("uCloudWarp", item.material.cloudWarp);
            matCache.cloudWarp = item.material.cloudWarp;
        }
        if (!matCache.valid || matCache.cloudHighlight != item.material.cloudHighlight) {
            shader->setFloat("uCloudHighlight", item.material.cloudHighlight);
            matCache.cloudHighlight = item.material.cloudHighlight;
        }
        if (!matCache.valid || matCache.cloudStars != item.material.cloudStars) {
            shader->setFloat("uCloudStars", item.material.cloudStars);
            matCache.cloudStars = item.material.cloudStars;
        }
        if (!matCache.valid || matCache.cloudHorizon != item.material.cloudHorizon) {
            shader->setFloat("uCloudHorizon", item.material.cloudHorizon);
            matCache.cloudHorizon = item.material.cloudHorizon;
        }
        glm::vec4 wantUvT = uiTarget ? uiTarget->uvTransform
                                      : BuildSurfaceUvTransform(objPtr, item.material);
        if (!matCache.valid || matCache.uvTransform != wantUvT) {
            shader->setVec4("uvTransform", wantUvT);
            matCache.uvTransform = wantUvT;
        }
        glm::vec4 wantUvR = uiTarget ? glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)
                                     : (objPtr ? BuildSpriteUvRect(*objPtr) : glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
        if (!matCache.valid || matCache.uvRect != wantUvR) {
            shader->setVec4("uvRect", wantUvR);
            matCache.uvRect = wantUvR;
        }

        bool wantSkin = false;
        if (objPtr && objPtr->hasSkeletalAnimation && objPtr->skeletal.enabled) {
            int safeLimit = std::max(0, item.boneLimit);
            int boneCount = std::min<int>(item.availableBones, safeLimit);
            if (item.wantsGpuSkinning && boneCount > 0) {
                // Bones are per-object, so always upload; can't safely cache the mat4 array.
                shader->setInt("boneCount", boneCount);
                shader->setMat4Array("bones", objPtr->skeletal.finalMatrices.data(), boneCount);
                matCache.boneCount = boneCount;
                wantSkin = true;
            }
        }
        if (!matCache.valid || matCache.useSkinning != wantSkin) {
            shader->setBool("useSkinning", wantSkin);
            matCache.useSkinning = wantSkin;
        }

        bool usingUiTargetTex = false;
        if (item.isUiCanvas3D && uiTarget != nullptr) {
            bindTexture2D(GL_TEXTURE0, uiTarget->texture);
            usingUiTargetTex = true;
        }
        Texture* baseTex = texture1;
        if (!usingUiTargetTex) {
            if (missingMaterialAndShader && missingMaterialFallbackTexture != 0) {
                bindTexture2D(GL_TEXTURE0, missingMaterialFallbackTexture);
            } else if (hasRuntimeAlbedoOverride) {
                bindTexture2D(GL_TEXTURE0, objPtr->runtimeAlbedoTextureOverrideId);
            } else {
                if (!albedoTexturePath.empty()) {
                    if (auto* t = getTexture(albedoTexturePath, item.material.textureFilter)) baseTex = t;
                }
                if (baseTex) bindTexture2D(GL_TEXTURE0, baseTex->GetID());
            }
        }

        bool overlayUsed = false;
        if (objPtr && objPtr->renderType == RenderType::Mirror) {
            auto it = mirrorTargets.find(objPtr->id);
            if (it != mirrorTargets.end() && it->second.texture != 0) {
                bindTexture2D(GL_TEXTURE1, it->second.texture);
                overlayUsed = true;
            }
        }
        if (!overlayUsed && item.useOverlay && !overlayTexturePath.empty()) {
            if (auto* t = getTexture(overlayTexturePath, item.material.textureFilter)) {
                bindTexture2D(GL_TEXTURE1, t->GetID());
                overlayUsed = true;
            }
        }
        if (!overlayUsed && texture2) {
            bindTexture2D(GL_TEXTURE1, texture2->GetID());
        }
        if (!matCache.valid || matCache.hasOverlay != overlayUsed) {
            shader->setBool("hasOverlay", overlayUsed);
            matCache.hasOverlay = overlayUsed;
        }

        float reflectionIntensity = 0.0f;
        unsigned int reflectionCube = findReflectionCastForItem(item, reflectionIntensity);
        if (reflectionCube == 0 && skybox && skyboxReflectionTarget.cube != 0 && skyboxReflectionTarget.hasCapture) {
            const SkyboxSettings& skySettings = skybox->getSettings();
            if (skySettings.environmentReflections && skySettings.environmentReflectionIntensity > 0.001f) {
                reflectionCube = skyboxReflectionTarget.cube;
                reflectionIntensity = skySettings.environmentReflectionIntensity;
            }
        }
        const bool reflectionUsed = reflectionCube != 0 && reflectionIntensity > 0.001f;
        if (reflectionUsed) {
            glActiveTexture(GL_TEXTURE11);
            glBindTexture(GL_TEXTURE_CUBE_MAP, reflectionCube);
            glActiveTexture(GL_TEXTURE0);
        }
        if (!matCache.valid || matCache.hasReflectionCast != reflectionUsed) {
            shader->setBool("hasReflectionCast", reflectionUsed);
            matCache.hasReflectionCast = reflectionUsed;
        }
        if (!matCache.valid || std::abs(matCache.reflectionIntensity - reflectionIntensity) > 0.0001f) {
            shader->setFloat("reflectionIntensity", reflectionIntensity);
            matCache.reflectionIntensity = reflectionIntensity;
        }

        bool normalUsed = false;
        if (!normalMapPath.empty()) {
            if (auto* t = getTexture(normalMapPath, item.material.textureFilter)) {
                bindTexture2D(GL_TEXTURE2, t->GetID());
                normalUsed = true;
            }
        } else {
            bindTexture2D(GL_TEXTURE2, 0);
        }
        if (!matCache.valid || matCache.hasNormalMap != normalUsed) {
            shader->setBool("hasNormalMap", normalUsed);
            matCache.hasNormalMap = normalUsed;
        }
        matCache.valid = true;

        if (objPtr && objPtr->renderType == RenderType::Model && objPtr->meshId != -1 &&
            objPtr->hasSkeletalAnimation && objPtr->skeletal.enabled && !item.wantsGpuSkinning) {
            const auto* meshInfo = getModelLoader().getMeshInfo(objPtr->meshId);
            if (meshInfo) {
                applyCpuSkinning(*const_cast<OBJLoader::LoadedMesh*>(meshInfo),
                                 objPtr->skeletal.finalMatrices,
                                 objPtr->skeletal.maxBones);
            }
        }

        const bool wantFrontFaceCCW = glm::determinant(glm::mat3(item.model)) >= 0.0f;
        if (wantFrontFaceCCW != currentFrontFaceCCW) {
            glFrontFace(wantFrontFaceCCW ? GL_CCW : GL_CW);
            currentFrontFaceCCW = wantFrontFaceCCW;
            Runtime2DCountStateBind();
        }

        if (item.doubleSided) {
            if (currentCullFaceEnabled) {
                glDisable(GL_CULL_FACE);
                currentCullFaceEnabled = false;
                Runtime2DCountStateBind();
            }
        } else if (!currentCullFaceEnabled) {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            currentCullFaceEnabled = true;
            Runtime2DCountStateBind();
        }

        bool wantsTransparency = !item.sortOpaque;
        bool wantsBlend = wantsTransparency || item.isUiCanvas3D;
        if (wantsBlend) {
            if (!currentBlendEnabled) {
                glEnable(GL_BLEND);
                currentBlendEnabled = true;
                Runtime2DCountStateBind();
            }
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            Runtime2DCountStateBind();
        } else if (currentBlendEnabled) {
            glDisable(GL_BLEND);
            currentBlendEnabled = false;
            Runtime2DCountStateBind();
        }
        if ((wantsTransparency || item.isUiCanvas3D) && currentDepthMask) {
            glDepthMask(GL_FALSE);
            currentDepthMask = false;
            Runtime2DCountStateBind();
        } else if (!(wantsTransparency || item.isUiCanvas3D) && !currentDepthMask) {
            glDepthMask(GL_TRUE);
            currentDepthMask = true;
            Runtime2DCountStateBind();
        }
        recordMeshDraw();
        item.mesh->draw();
    }
    flushPendingSkybox();

    auto drawMaskedSprite25DUiLayer = [&]() {
        ensureQuad();
        if (quadVAO == 0) {
            return;
        }

        const float viewportWidth = static_cast<float>(std::max(1, width));
        const float viewportHeight = static_cast<float>(std::max(1, height));

        std::vector<const SceneObject*> spriteItems;
        spriteItems.reserve(sceneObjects.size());
        for (const SceneObject& obj : sceneObjects) {
            if (!IsObjectEnabledInHierarchy(obj) || !IsMaskedSprite25DUiLayerObject(obj)) {
                continue;
            }

            const glm::vec4 clipCenter = proj * view * glm::vec4(obj.position, 1.0f);
            if (std::abs(clipCenter.w) <= 1e-5f) {
                continue;
            }
            const glm::vec3 ndcCenter = glm::vec3(clipCenter) / clipCenter.w;
            if (clipCenter.w <= 0.0f || ndcCenter.z < -1.0f || ndcCenter.z > 1.0f) {
                continue;
            }
            spriteItems.push_back(&obj);
        }
        if (spriteItems.empty()) {
            return;
        }

        std::stable_sort(spriteItems.begin(), spriteItems.end(),
                         [&](const SceneObject* a, const SceneObject* b) {
                             if (a->ui.pseudo3DDepthSort != b->ui.pseudo3DDepthSort) {
                                 return a->ui.pseudo3DDepthSort < b->ui.pseudo3DDepthSort;
                             }
                             const float aDepth = -glm::vec3(view * glm::vec4(a->position, 1.0f)).z;
                             const float bDepth = -glm::vec3(view * glm::vec4(b->position, 1.0f)).z;
                             if (std::abs(aDepth - bDepth) > 0.001f) {
                                 return aDepth > bDepth;
                             }
                             return a->id < b->id;
                         });

        static const std::string maskedSpriteVert = "Resources/Shaders/masked_sprite25d_ui_vert.glsl";
        static const std::string maskedSpriteFrag = "Resources/Shaders/masked_sprite25d_ui_frag.glsl";
        Shader* spriteShader = getShader(maskedSpriteVert, maskedSpriteFrag);
        if (spriteShader == nullptr) {
            return;
        }

        GLint previousDepthFunc = GL_LESS;
        glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);

        if (currentCullFaceEnabled) {
            glDisable(GL_CULL_FACE);
            currentCullFaceEnabled = false;
            Runtime2DCountStateBind();
        }
        if (!currentBlendEnabled) {
            glEnable(GL_BLEND);
            currentBlendEnabled = true;
            Runtime2DCountStateBind();
        }
        if (currentDepthMask) {
            glDepthMask(GL_FALSE);
            currentDepthMask = false;
            Runtime2DCountStateBind();
        }
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        shader = spriteShader;
        currentShader = spriteShader;
        shader->use();
        Runtime2DCountStateBind();
        shader->setInt("spriteTexture", 0);

        for (const SceneObject* obj : spriteItems) {
            const glm::vec4 clipCenter = proj * view * glm::vec4(obj->position, 1.0f);
            if (std::abs(clipCenter.w) <= 1e-5f) {
                continue;
            }
            const glm::vec3 ndcCenter = glm::vec3(clipCenter) / clipCenter.w;
            if (clipCenter.w <= 0.0f || ndcCenter.z < -1.0f || ndcCenter.z > 1.0f) {
                continue;
            }

            glm::vec2 halfSizeNdc(0.0f);
            if (!ProjectSprite25DProxyHalfSizeNdc(*obj, view, proj, viewportWidth, viewportHeight, halfSizeNdc)) {
                continue;
            }

            GLuint textureId = 0;
            if (obj->runtimeHasAlbedoTextureOverride && obj->runtimeAlbedoTextureOverrideId != 0) {
                textureId = obj->runtimeAlbedoTextureOverrideId;
            } else if (!obj->albedoTexturePath.empty()) {
                if (Texture* tex = getTexture(obj->albedoTexturePath, MaterialProperties::TextureFilter::Point)) {
                    textureId = tex->GetID();
                }
            }
            if (textureId == 0) {
                textureId = missingMaterialFallbackTexture;
            }
            if (textureId == 0) {
                continue;
            }

            shader->setVec2("centerNdc", glm::vec2(ndcCenter.x, ndcCenter.y));
            shader->setVec2("halfSizeNdc", halfSizeNdc);
            shader->setFloat("depthNdc", ndcCenter.z);
            shader->setFloat("rotationRadians", glm::radians(obj->ui.rotation));
            shader->setVec4("tint", obj->ui.color);
            shader->setVec4("uvRect", BuildSpriteUvRect(*obj));
            bindTexture2D(GL_TEXTURE0, textureId);

            recordDrawCall();
            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        glBindVertexArray(0);

        glDepthFunc(previousDepthFunc);
    };
    drawMaskedSprite25DUiLayer();

    if (currentBlendEnabled != (blendEnabled == GL_TRUE)) {
        if (blendEnabled) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
    }
    if (currentDepthMask != (depthMask == GL_TRUE)) {
        glDepthMask(depthMask);
    }
    if (!cullFace) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(prevCullMode);
    }
    glFrontFace(prevFrontFace);

    if (unbindFramebuffer) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

Renderer::ResolvedPostFX Renderer::gatherPostFX(const Camera& camera, const std::vector<SceneObject>& sceneObjects) const {
    ResolvedPostFX result;
    result.settings = MakeNeutralPostFXSettings();
    result.settings.hdrEnabled = false;

    const SceneObject* resolvedVolume = nullptr;
    float resolvedPriority = -FLT_MAX;
    float resolvedSpatialWeight = 0.0f;
    int resolvedIndex = -1;

    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        const SceneObject& obj = sceneObjects[i];
        if (!IsObjectEnabledInHierarchy(obj) || !obj.hasPostFX || !obj.postFx.enabled) continue;

        const float spatialWeight =
            ComputePostFXVolumeSpatialWeight(obj, camera.position);
        const float blendWeight =
            glm::clamp(obj.postFx.blendWeight, 0.0f, 1.0f);
        const float polarization =
            glm::clamp(obj.postFx.distortionPolarization, 0.0f, 1.0f);
        const float polarizationVisibility =
            (polarization > 0.0001f && polarization < 0.9999f)
                ? 1.0f - polarization
                : 0.0f;
        if (spatialWeight *
                std::max(blendWeight, polarizationVisibility) <=
            0.0001f) {
            continue;
        }

        result.activeVolumeCount += 1;
        if (!resolvedVolume ||
            obj.postFx.priority > resolvedPriority ||
            (obj.postFx.priority == resolvedPriority && static_cast<int>(i) >= resolvedIndex)) {
            resolvedVolume = &obj;
            resolvedPriority = obj.postFx.priority;
            resolvedSpatialWeight = spatialWeight;
            resolvedIndex = static_cast<int>(i);
        }
    }

    if (!resolvedVolume) {
        return result;
    }

    PostFXSettings settings = MakeNeutralPostFXSettings();
    const float polarization =
        glm::clamp(resolvedVolume->postFx.distortionPolarization, 0.0f, 1.0f);
    const float weight =
        glm::clamp(resolvedSpatialWeight * polarization, 0.0f, 1.0f);
    settings.enabled = true;
    settings.isGlobal = resolvedVolume->postFx.isGlobal;
    settings.priority = resolvedVolume->postFx.priority;
    settings.blendWeight = weight;
    settings.blendRadius = resolvedVolume->postFx.blendRadius;
    settings.hdrEnabled = resolvedVolume->postFx.hdrEnabled;
    settings.toneMapper = resolvedVolume->postFx.toneMapper;
    settings.whitePoint = glm::mix(settings.whitePoint, resolvedVolume->postFx.whitePoint, weight);
    settings.gamma = glm::mix(settings.gamma, resolvedVolume->postFx.gamma, weight);

    if (resolvedVolume->postFx.bloomEnabled) {
        settings.bloomEnabled = true;
        settings.bloomThreshold = glm::mix(settings.bloomThreshold, resolvedVolume->postFx.bloomThreshold, weight);
        settings.bloomSoftKnee = glm::mix(settings.bloomSoftKnee, resolvedVolume->postFx.bloomSoftKnee, weight);
        settings.bloomIntensity = glm::mix(0.0f, resolvedVolume->postFx.bloomIntensity, weight);
        settings.bloomRadius = glm::mix(settings.bloomRadius, resolvedVolume->postFx.bloomRadius, weight);
    }
    if (resolvedVolume->postFx.colorAdjustEnabled) {
        settings.colorAdjustEnabled = true;
        settings.exposure = glm::mix(settings.exposure, resolvedVolume->postFx.exposure, weight);
        settings.contrast = glm::mix(settings.contrast, resolvedVolume->postFx.contrast, weight);
        settings.saturation = glm::mix(settings.saturation, resolvedVolume->postFx.saturation, weight);
        settings.colorFilter = glm::mix(settings.colorFilter, resolvedVolume->postFx.colorFilter, weight);
    }
    if (resolvedVolume->postFx.motionBlurEnabled) {
        settings.motionBlurEnabled = true;
        settings.motionBlurStrength = glm::mix(0.0f, resolvedVolume->postFx.motionBlurStrength, weight);
        settings.motionBlurThreshold = glm::mix(settings.motionBlurThreshold, resolvedVolume->postFx.motionBlurThreshold, weight);
        settings.motionBlurClamp = glm::mix(settings.motionBlurClamp, resolvedVolume->postFx.motionBlurClamp, weight);
    }
    if (resolvedVolume->postFx.vignetteEnabled) {
        settings.vignetteEnabled = true;
        settings.vignetteIntensity = glm::mix(0.0f, resolvedVolume->postFx.vignetteIntensity, weight);
        settings.vignetteSmoothness = glm::mix(settings.vignetteSmoothness, resolvedVolume->postFx.vignetteSmoothness, weight);
    }
    if (resolvedVolume->postFx.chromaticAberrationEnabled) {
        settings.chromaticAberrationEnabled = true;
        settings.chromaticAmount = glm::mix(0.0f, resolvedVolume->postFx.chromaticAmount, weight);
    }
    if (resolvedVolume->postFx.sharpenEnabled) {
        settings.sharpenEnabled = true;
        settings.sharpenStrength = glm::mix(0.0f, resolvedVolume->postFx.sharpenStrength, weight);
    }
    if (resolvedVolume->postFx.ambientOcclusionEnabled) {
        settings.ambientOcclusionEnabled = true;
        settings.aoRadius = glm::mix(settings.aoRadius, resolvedVolume->postFx.aoRadius, weight);
        settings.aoStrength = glm::mix(0.0f, resolvedVolume->postFx.aoStrength, weight);
    }
    if (resolvedVolume->postFx.ditherEnabled) {
        settings.ditherEnabled = true;
        settings.ditherIntensity = glm::mix(0.0f, resolvedVolume->postFx.ditherIntensity, weight);
        settings.ditherColorBits = resolvedVolume->postFx.ditherColorBits;
        settings.ditherDarkAdjustment = glm::mix(0.0f, resolvedVolume->postFx.ditherDarkAdjustment, weight);
        settings.ditherPixelation = glm::mix(0.0f, resolvedVolume->postFx.ditherPixelation, weight);
        settings.ditherSize = glm::mix(1.0f, resolvedVolume->postFx.ditherSize, weight);
        settings.ditherContrast = glm::mix(0.0f, resolvedVolume->postFx.ditherContrast, weight);
        settings.ditherOffset = glm::mix(0.0f, resolvedVolume->postFx.ditherOffset, weight);
        settings.ditherPalette = resolvedVolume->postFx.ditherPalette;
        settings.ditherPattern = resolvedVolume->postFx.ditherPattern;
    }
    if (resolvedVolume->postFx.staticEnabled) {
        settings.staticEnabled = true;
        settings.staticIntensity = glm::mix(0.0f, resolvedVolume->postFx.staticIntensity, weight);
        settings.staticGrainScale = glm::mix(settings.staticGrainScale, resolvedVolume->postFx.staticGrainScale, weight);
        settings.staticDarkAreaInfluence = glm::mix(0.0f, resolvedVolume->postFx.staticDarkAreaInfluence, weight);
        settings.staticSpeed = glm::mix(0.0f, resolvedVolume->postFx.staticSpeed, weight);
        settings.staticMonochrome = resolvedVolume->postFx.staticMonochrome;
        settings.staticSparkle = glm::mix(0.0f, resolvedVolume->postFx.staticSparkle, weight);
    }
    if (resolvedVolume->postFx.staticDistortionEnabled) {
        settings.staticDistortionEnabled = true;
        settings.staticDistortionHorizontalJitterAmount =
            glm::mix(0.0f, resolvedVolume->postFx.staticDistortionHorizontalJitterAmount, weight);
        settings.staticDistortionLineDensity =
            glm::mix(settings.staticDistortionLineDensity, resolvedVolume->postFx.staticDistortionLineDensity, weight);
        settings.staticDistortionGlitchFrequency =
            glm::mix(0.0f, resolvedVolume->postFx.staticDistortionGlitchFrequency, weight);
        settings.staticDistortionStrength =
            glm::mix(0.0f, resolvedVolume->postFx.staticDistortionStrength, weight);
    }
    if (resolvedVolume->postFx.lensDistortionEnabled) {
        settings.lensDistortionEnabled = true;
        settings.lensDistortionAmount = glm::mix(0.0f, resolvedVolume->postFx.lensDistortionAmount, weight);
        settings.lensDistortionEdgeFalloff =
            glm::mix(settings.lensDistortionEdgeFalloff, resolvedVolume->postFx.lensDistortionEdgeFalloff, weight);
        settings.lensDistortionCenterOffset =
            glm::mix(settings.lensDistortionCenterOffset, resolvedVolume->postFx.lensDistortionCenterOffset, weight);
        settings.lensDistortionEdgeVignetteEnabled =
            resolvedVolume->postFx.lensDistortionEdgeVignetteEnabled;
        settings.lensDistortionEdgeVignetteIntensity =
            glm::mix(0.0f, resolvedVolume->postFx.lensDistortionEdgeVignetteIntensity, weight);
        settings.lensDistortionEdgeVignetteRadius =
            glm::mix(settings.lensDistortionEdgeVignetteRadius,
                     resolvedVolume->postFx.lensDistortionEdgeVignetteRadius, weight);
        settings.lensDistortionEdgeVignetteSoftness =
            glm::mix(settings.lensDistortionEdgeVignetteSoftness,
                     resolvedVolume->postFx.lensDistortionEdgeVignetteSoftness, weight);
        settings.lensDistortionEdgeVignetteColor =
            glm::mix(settings.lensDistortionEdgeVignetteColor,
                     resolvedVolume->postFx.lensDistortionEdgeVignetteColor, weight);
    }
    if (resolvedVolume->postFx.pixelationEnabled) {
        settings.pixelationEnabled = true;
        settings.pixelationSize =
            glm::mix(settings.pixelationSize, resolvedVolume->postFx.pixelationSize, weight);
    }
    if (resolvedVolume->postFx.posterizeEnabled) {
        settings.posterizeEnabled = true;
        settings.posterizeLevels = resolvedVolume->postFx.posterizeLevels;
    }
    if (resolvedVolume->postFx.scanlinesEnabled) {
        settings.scanlinesEnabled = true;
        settings.scanlinesIntensity =
            glm::mix(0.0f, resolvedVolume->postFx.scanlinesIntensity, weight);
        settings.scanlinesDensity =
            glm::mix(settings.scanlinesDensity, resolvedVolume->postFx.scanlinesDensity, weight);
        settings.scanlinesSpeed =
            glm::mix(0.0f, resolvedVolume->postFx.scanlinesSpeed, weight);
    }
    if (resolvedVolume->postFx.vhsOverlayEnabled) {
        settings.vhsOverlayEnabled = true;
        settings.vhsOverlayOpacity = glm::mix(0.0f, resolvedVolume->postFx.vhsOverlayOpacity, weight);
        settings.vhsOverlayScanlineStrength =
            glm::mix(0.0f, resolvedVolume->postFx.vhsOverlayScanlineStrength, weight);
        settings.vhsOverlayTapeNoise = glm::mix(0.0f, resolvedVolume->postFx.vhsOverlayTapeNoise, weight);
        settings.vhsOverlayChromaBleed = glm::mix(0.0f, resolvedVolume->postFx.vhsOverlayChromaBleed, weight);
        settings.vhsOverlayBottomNoiseBandHeight =
            glm::mix(0.0f, resolvedVolume->postFx.vhsOverlayBottomNoiseBandHeight, weight);
        settings.vhsOverlayBottomNoiseBandIntensity =
            glm::mix(0.0f, resolvedVolume->postFx.vhsOverlayBottomNoiseBandIntensity, weight);
        settings.vhsOverlayDistortionStrength =
            glm::mix(0.0f, resolvedVolume->postFx.vhsOverlayDistortionStrength, weight);
        settings.vhsOverlayAnimationSpeed =
            glm::mix(0.0f, resolvedVolume->postFx.vhsOverlayAnimationSpeed, weight);
        settings.vhsOverlayColorBleed =
            glm::mix(0.0f, resolvedVolume->postFx.vhsOverlayColorBleed, weight);
        settings.vhsOverlayBanding =
            glm::mix(0.0f, resolvedVolume->postFx.vhsOverlayBanding, weight);
        settings.vhsOverlaySignalMode = resolvedVolume->postFx.vhsOverlaySignalMode;
        settings.vhsOverlayDropouts =
            glm::mix(0.0f, resolvedVolume->postFx.vhsOverlayDropouts, weight);
    }
    if (resolvedVolume->postFx.wavyEnabled) {
        settings.wavyEnabled = true;
        settings.wavyAmplitude = glm::mix(0.0f, resolvedVolume->postFx.wavyAmplitude, weight);
        settings.wavyFrequency = glm::mix(settings.wavyFrequency, resolvedVolume->postFx.wavyFrequency, weight);
        settings.wavySpeed = glm::mix(0.0f, resolvedVolume->postFx.wavySpeed, weight);
        settings.wavyVertical = resolvedVolume->postFx.wavyVertical;
    }

    const float polarizationVisibility =
        (polarization > 0.0001f && polarization < 0.9999f)
            ? (1.0f - polarization) * resolvedSpatialWeight
            : 0.0f;
    settings.blendWeight =
        glm::clamp(polarizationVisibility, 0.0f, 1.0f);
    result.polarizationSettings = settings;
    result.hasPolarizationLayer = settings.blendWeight > 0.0001f;

    PostFXSettings fullSettings = resolvedVolume->postFx;
    fullSettings.blendWeight =
        glm::clamp(resolvedVolume->postFx.blendWeight *
                       resolvedSpatialWeight,
                   0.0f, 1.0f);
    AttenuatePostFXStrengths(fullSettings, fullSettings.blendWeight);
    result.settings = fullSettings;
    result.resolvedVolumeId = resolvedVolume->id;
    result.resolvedVolumeName = resolvedVolume->name;
    result.resolvedBlend = fullSettings.blendWeight;
    return result;
}

PostFXSettings Renderer::resolvePostFXSettings(const Camera& camera, const std::vector<SceneObject>& sceneObjects) const {
    return gatherPostFX(camera, sceneObjects).settings;
}

Renderer::PostFX2DScope Renderer::resolvePostFX2DScope(
    const Camera& camera, const std::vector<SceneObject>& sceneObjects) const {
    PostFX2DScope scope;
    const ResolvedPostFX resolved = gatherPostFX(camera, sceneObjects);
    if (!resolved.settings.enabled || resolved.activeVolumeCount <= 0 ||
        !resolved.settings.scope2DEnabled) {
        return scope;
    }
    scope.active = true;
    scope.mode = resolved.settings.scope2DMode;
    if (scope.mode == PostFX2DScopeMode::Band) {
        // Authored bounds can be dragged past each other; take them as a range.
        scope.minOrder = std::min(resolved.settings.scope2DMinOrder,
                                  resolved.settings.scope2DMaxOrder);
        scope.maxOrder = std::max(resolved.settings.scope2DMinOrder,
                                  resolved.settings.scope2DMaxOrder);
    } else {
        // At-or-below has no lower bound; only the cutoff means anything.
        scope.minOrder = std::numeric_limits<int>::min();
        scope.maxOrder = resolved.settings.scope2DMaxOrder;
    }
    return scope;
}

unsigned int Renderer::postProcessTexture(const Camera& camera,
                                          const std::vector<SceneObject>& sceneObjects,
                                          unsigned int sourceTexture,
                                          int width,
                                          int height,
                                          bool allowHistory) {
    return applyPostProcessing(camera, sceneObjects, sourceTexture, width, height, allowHistory);
}

bool Renderer::postProcessFramebufferRegion(
    const Camera& camera, const std::vector<SceneObject>& sceneObjects,
    int x, int y, int width, int height, bool allowHistory) {
    if (!moduRenderContextLive() || width <= 0 || height <= 0) {
        return false;
    }

    const ResolvedPostFX resolved = gatherPostFX(camera, sceneObjects);
    if (!resolved.settings.enabled || resolved.activeVolumeCount <= 0) {
        return false;
    }

    GLint previousReadFramebuffer = 0;
    GLint previousDrawFramebuffer = 0;
    GLint previousViewport[4] = {0, 0, 0, 0};
    GLint previousScissorBox[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
    const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);

    ensureRenderTarget(overlayCaptureTarget, width, height, false, false, false);
    if (overlayCaptureTarget.fbo == 0 || overlayCaptureTarget.texture == 0) {
        return false;
    }

    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, previousReadFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, overlayCaptureTarget.fbo);
    glBlitFramebuffer(x, y, x + width, y + height,
                      0, 0, width, height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    const unsigned int processed =
        applyPostProcessing(camera, sceneObjects, overlayCaptureTarget.texture,
                            width, height, allowHistory);
    const unsigned int presentedTexture =
        processed != 0 ? processed : overlayCaptureTarget.texture;
    const unsigned int presentedFramebuffer =
        findFramebufferForTexture(presentedTexture);
    if (presentedFramebuffer != 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, presentedFramebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previousDrawFramebuffer);
        glBlitFramebuffer(0, 0, width, height,
                          x, y, x + width, y + height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, previousReadFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previousDrawFramebuffer);
    glViewport(previousViewport[0], previousViewport[1],
               previousViewport[2], previousViewport[3]);
    glScissor(previousScissorBox[0], previousScissorBox[1],
              previousScissorBox[2], previousScissorBox[3]);
    if (scissorEnabled) {
        glEnable(GL_SCISSOR_TEST);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    return presentedFramebuffer != 0;
}

bool Renderer::beginBandPostFxRegion(int x, int y, int width, int height,
                                     int framebufferWidth, int framebufferHeight) {
    if (!moduRenderContextLive() || bandRegionActive ||
        width <= 0 || height <= 0 ||
        framebufferWidth <= 0 || framebufferHeight <= 0) {
        return false;
    }

    // Read the binding first: ensureRenderTarget unbinds to 0 on the frames
    // where it actually allocates, so asking afterwards would report 0 rather
    // than whatever the caller was drawing into.
    GLint previousDrawFramebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);

    // Same dimensions as the framebuffer being drawn into, so the caller's
    // viewport, scissor rects and projection all keep mapping 1:1 and the band
    // draws exactly where it would have.
    ensureRenderTarget(bandLayerTarget, framebufferWidth, framebufferHeight,
                       true, false, false);
    if (bandLayerTarget.fbo == 0 || bandLayerTarget.texture == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
        return false;
    }

    const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint previousScissorBox[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, bandLayerTarget.fbo);
    // Transparent, because alpha is what carries the band's coverage: it is the
    // only thing that later tells the composite which pixels the band actually
    // painted and which must be left to whatever is underneath.
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    GLfloat previousClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(previousClearColor[0], previousClearColor[1],
                 previousClearColor[2], previousClearColor[3]);
    glScissor(previousScissorBox[0], previousScissorBox[1],
              previousScissorBox[2], previousScissorBox[3]);
    if (scissorEnabled) {
        glEnable(GL_SCISSOR_TEST);
    }

    bandRegionX = x;
    bandRegionY = y;
    bandRegionWidth = width;
    bandRegionHeight = height;
    bandPreviousFramebuffer = static_cast<int>(previousDrawFramebuffer);
    bandRegionActive = true;
    return true;
}

bool Renderer::endBandPostFxRegion(const Camera& camera,
                                   const std::vector<SceneObject>& sceneObjects,
                                   bool allowHistory) {
    if (!bandRegionActive) {
        return false;
    }
    // Cleared up front: every early-out below still has to hand the framebuffer
    // back, or the rest of the frame draws into the band target.
    bandRegionActive = false;

    const int x = bandRegionX;
    const int y = bandRegionY;
    const int width = bandRegionWidth;
    const int height = bandRegionHeight;
    const GLuint previousFramebuffer = static_cast<GLuint>(bandPreviousFramebuffer);

    if (!moduRenderContextLive()) {
        return false;
    }

    GLint previousViewport[4] = {0, 0, 0, 0};
    GLint previousScissorBox[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
    const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);

    glDisable(GL_SCISSOR_TEST);

    // Post FX is authored against the visible game view, so vignette, lens
    // distortion and friends have to be centred on the region rather than on
    // the whole framebuffer the band layer spans.
    ensureRenderTarget(bandCaptureTarget, width, height, true, false, false);
    if (bandCaptureTarget.fbo == 0 || bandCaptureTarget.texture == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
        return false;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, bandLayerTarget.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, bandCaptureTarget.fbo);
    glBlitFramebuffer(x, y, x + width, y + height,
                      0, 0, width, height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    postFxPreserveAlpha = true;
    const unsigned int processed =
        applyPostProcessing(camera, sceneObjects, bandCaptureTarget.texture,
                            width, height, allowHistory);
    postFxPreserveAlpha = false;
    // applyPostProcessing hands the source straight back when nothing is
    // visible; the band still has to be composited either way or it vanishes.
    const unsigned int presentedTexture =
        processed != 0 ? processed : bandCaptureTarget.texture;

    glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
    bool composited = false;
    if (compositeShader && compositeShader->ID != 0) {
        glViewport(x, y, width, height);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        // The band layer is premultiplied: it was drawn over a cleared
        // transparent target with ImGui's separate-alpha blend, and the post
        // shader re-premultiplies on the way out.
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        compositeShader->use();
        compositeShader->setInt("sourceTex", 0);
        compositeShader->setFloat("opacity", 1.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, presentedTexture);
        drawFullscreenQuad();
        composited = true;
    }

    glViewport(previousViewport[0], previousViewport[1],
               previousViewport[2], previousViewport[3]);
    glScissor(previousScissorBox[0], previousScissorBox[1],
              previousScissorBox[2], previousScissorBox[3]);
    if (scissorEnabled) {
        glEnable(GL_SCISSOR_TEST);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    if (blendEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    if (depthEnabled) {
        glEnable(GL_DEPTH_TEST);
    }
    return composited;
}

unsigned int Renderer::applyPostProcessing(const Camera& camera, const std::vector<SceneObject>& sceneObjects, unsigned int sourceTexture, int width, int height, bool allowHistory) {
    MODU_PROFILE_SCOPE("Post Processing", ProfilerSampleCategory::RenderDetail);
    using Clock = std::chrono::steady_clock;

    const auto totalStart = Clock::now();
    ResolvedPostFX resolved;
    PostFXSettings settings;
    if (postFxSettingsOverrideActive) {
        settings = postFxSettingsOverride;
        resolved.settings = settings;
        resolved.activeVolumeCount = 1;
        resolved.resolvedBlend = settings.blendWeight;
    } else {
        MODU_PROFILE_SCOPE("PostFX Resolve", ProfilerSampleCategory::RenderDetail);
        resolved = gatherPostFX(camera, sceneObjects);
        settings = resolved.settings;
        if (resolved.hasPolarizationLayer && sourceTexture != 0 &&
            width > 0 && height > 0) {
            postFxSettingsOverrideActive = true;
            postFxSettingsOverride = resolved.polarizationSettings;
            postFxTargetOverride = &polarizationTarget;
            const unsigned int polarizedTexture =
                applyPostProcessing(camera, sceneObjects, sourceTexture,
                                    width, height, false);
            postFxTargetOverride = nullptr;
            postFxSettingsOverrideActive = false;
            if (polarizedTexture != 0) {
                sourceTexture = polarizedTexture;
            }
        }
    }

    PostProcessStats& postStats = allowHistory ? viewportPostStats : previewPostStats;
    postStats = {};
    postStats.sourceTextureId = sourceTexture;
    postStats.sourceFramebufferId = findFramebufferForTexture(sourceTexture);
    postStats.activeVolumeCount = resolved.activeVolumeCount;
    postStats.resolvedVolumeId = resolved.resolvedVolumeId;
    postStats.resolvedVolumeName = resolved.resolvedVolumeName;
    postStats.resolvedBlend = resolved.resolvedBlend;
    postStats.activeEffectCount = CountEnabledPostEffects(settings);
    postStats.hdrEnabled = settings.hdrEnabled;

    bool wantsEffects = settings.enabled &&
        (HasMeaningfulToneMapping(settings) || settings.bloomEnabled || settings.colorAdjustEnabled ||
         settings.motionBlurEnabled || settings.vignetteEnabled ||
         settings.chromaticAberrationEnabled || settings.sharpenEnabled ||
         settings.ambientOcclusionEnabled || settings.ditherEnabled ||
         settings.staticEnabled || settings.staticDistortionEnabled ||
         settings.lensDistortionEnabled || settings.pixelationEnabled ||
         settings.posterizeEnabled || settings.scanlinesEnabled ||
         settings.vhsOverlayEnabled ||
         settings.wavyEnabled);

    if (wantsEffects) {
#if MODULARITY_OPENGL_ES
        bool wireframe = false;
#else
        GLint polygonMode[2] = { GL_FILL, GL_FILL };
#ifdef GL_POLYGON_MODE
        glGetIntegerv(GL_POLYGON_MODE, polygonMode);
#endif
        bool wireframe = (polygonMode[0] == GL_LINE || polygonMode[1] == GL_LINE);
#endif
        if (wireframe) {
            wantsEffects = false;
            postStats.activeEffectCount = 0;
            postStats.hdrEnabled = false;
            postStats.skipReason = "wireframe_or_line_mode";
        }
    }

    if (!wantsEffects || !postShader || width <= 0 || height <= 0 || sourceTexture == 0) {
        if (postStats.skipReason.empty()) {
            if (!wantsEffects) {
                postStats.skipReason = "no_visible_effects";
            } else if (!postShader) {
                postStats.skipReason = "post_shader_unavailable";
            } else if (width <= 0 || height <= 0) {
                postStats.skipReason = "invalid_target_size";
            } else {
                postStats.skipReason = "missing_source_texture";
            }
        }
        postStats.totalMs = std::chrono::duration<float, std::milli>(Clock::now() - totalStart).count();
        postStats.finalPresentedTextureId = sourceTexture;
        postStats.finalPresentedFramebufferId = postStats.sourceFramebufferId;
        postStats.finalTextureDiffersFromSource = false;
        if (allowHistory) {
            displayTexture = sourceTexture;
            clearHistory();
        }
        logPostFxDebug(postStats, allowHistory);
        return sourceTexture;
    }

    RenderTarget& target = postFxTargetOverride
                               ? *postFxTargetOverride
                               : (allowHistory ? postTarget : previewPostTarget);
    ensureRenderTarget(target, width, height, true, true);
    if (allowHistory) {
        ensureRenderTarget(historyTarget, width, height, false, true);
    }
    ensureRenderTarget(bloomTargetA, width, height, false, true);
    ensureRenderTarget(bloomTargetB, width, height, false, true);
    if (target.fbo == 0 || target.texture == 0) {
        postStats.skipReason = "post_target_unavailable";
        postStats.totalMs = std::chrono::duration<float, std::milli>(Clock::now() - totalStart).count();
        postStats.finalPresentedTextureId = sourceTexture;
        postStats.finalPresentedFramebufferId = postStats.sourceFramebufferId;
        postStats.finalTextureDiffersFromSource = false;
        if (allowHistory) {
            displayTexture = sourceTexture;
            clearHistory();
        }
        logPostFxDebug(postStats, allowHistory);
        return sourceTexture;
    }

    postStats.executionBegan = true;
    unsigned int bloomTex = 0;
    if (settings.bloomEnabled && brightShader && blurShader) {
        {
            MODU_PROFILE_SCOPE("Bloom Extract", ProfilerSampleCategory::RenderDetail);
            const auto bloomExtractStart = Clock::now();
            glDisable(GL_DEPTH_TEST);
            brightShader->use();
            brightShader->setFloat("threshold", settings.bloomThreshold);
            brightShader->setFloat("softKnee", settings.bloomSoftKnee);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sourceTexture);
            glBindFramebuffer(GL_FRAMEBUFFER, bloomTargetA.fbo);
            glViewport(0, 0, width, height);
            glClear(GL_COLOR_BUFFER_BIT);
            drawFullscreenQuad();
            Modu2DStats::CountPostFxPass();
            postStats.bloomExtractDestinationTextureId = bloomTargetA.texture;
            postStats.bloomExtractDestinationFramebufferId = bloomTargetA.fbo;
            postStats.bloomExtractMs = std::chrono::duration<float, std::milli>(Clock::now() - bloomExtractStart).count();
        }

        unsigned int pingTex = bloomTargetA.texture;
        {
            MODU_PROFILE_SCOPE("Bloom Blur", ProfilerSampleCategory::RenderDetail);
            const auto bloomBlurStart = Clock::now();
            blurShader->use();
            float sigma = glm::max(settings.bloomRadius * 2.5f, 0.1f);
            int radius = static_cast<int>(glm::clamp(settings.bloomRadius * 4.0f, 2.0f, 12.0f));
            blurShader->setFloat("sigma", sigma);
            blurShader->setInt("radius", radius);

            bool horizontal = true;
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
                Modu2DStats::CountPostFxPass();

                pingTex = writeTarget->texture;
                writeTarget = (writeTarget == &bloomTargetA) ? &bloomTargetB : &bloomTargetA;
                horizontal = !horizontal;
            }
            postStats.bloomBlurMs = std::chrono::duration<float, std::milli>(Clock::now() - bloomBlurStart).count();
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glEnable(GL_DEPTH_TEST);
        bloomTex = pingTex;
        postStats.bloomBlurTextureId = bloomTex;
        postStats.bloomBlurFramebufferId = findFramebufferForTexture(bloomTex);
        postStats.bloomUsed = true;
    } else {
        bloomTex = 0;
        clearTarget(bloomTargetA);
        clearTarget(bloomTargetB);
    }

    const bool hasHistory = allowHistory && historyValid;
    postStats.motionBlurUsed = settings.motionBlurEnabled && hasHistory;

    {
        MODU_PROFILE_SCOPE("PostFX Composite", ProfilerSampleCategory::RenderDetail);
        const auto compositeStart = Clock::now();
        glDisable(GL_DEPTH_TEST);
        postShader->use();
        postShader->setFloat("effectBlendWeight",
                             std::clamp(settings.blendWeight, 0.0f, 1.0f));
        postShader->setBool("preserveAlpha", postFxPreserveAlpha);
        postShader->setBool("enableHDR", settings.hdrEnabled);
        postShader->setInt("toneMapper", static_cast<int>(settings.toneMapper));
        postShader->setFloat("whitePoint", settings.whitePoint);
        postShader->setFloat("gamma", settings.gamma);
        postShader->setBool("enableBloom", settings.bloomEnabled && bloomTex != 0);
        postShader->setFloat("bloomIntensity", settings.bloomIntensity);
        postShader->setBool("enableColorAdjust", settings.colorAdjustEnabled);
        postShader->setFloat("exposure", settings.exposure);
        postShader->setFloat("contrast", settings.contrast);
        postShader->setFloat("saturation", settings.saturation);
        postShader->setVec3("colorFilter", settings.colorFilter);
        postShader->setBool("enableMotionBlur", settings.motionBlurEnabled);
        postShader->setFloat("motionBlurStrength", settings.motionBlurStrength);
        postShader->setFloat("motionBlurThreshold", settings.motionBlurThreshold);
        postShader->setFloat("motionBlurClamp", settings.motionBlurClamp);
        postShader->setBool("hasHistory", hasHistory);
        postShader->setBool("enableVignette", settings.vignetteEnabled);
        postShader->setFloat("vignetteIntensity", settings.vignetteIntensity);
        postShader->setFloat("vignetteSmoothness", settings.vignetteSmoothness);
        postShader->setBool("enableChromatic", settings.chromaticAberrationEnabled);
        postShader->setFloat("chromaticAmount", settings.chromaticAmount);
        postShader->setBool("enableSharpen", settings.sharpenEnabled);
        postShader->setFloat("sharpenStrength", settings.sharpenStrength);
        postShader->setBool("enableAO", settings.ambientOcclusionEnabled);
        postShader->setFloat("aoRadius", settings.aoRadius);
        postShader->setFloat("aoStrength", settings.aoStrength);
        postShader->setBool("enableDither", settings.ditherEnabled);
        postShader->setFloat("ditherIntensity", settings.ditherIntensity);
        postShader->setInt("ditherColorBits", std::clamp(settings.ditherColorBits, 1, 8));
        postShader->setFloat("ditherDarkAdjustment", settings.ditherDarkAdjustment);
        postShader->setFloat("ditherPixelation", settings.ditherPixelation);
        postShader->setFloat("ditherSize", std::clamp(settings.ditherSize, 1.0f, 8.0f));
        postShader->setFloat("ditherContrast", std::clamp(settings.ditherContrast, -1.0f, 1.0f));
        postShader->setFloat("ditherOffset", std::clamp(settings.ditherOffset, -1.0f, 1.0f));
        postShader->setInt("ditherPalette", static_cast<int>(settings.ditherPalette));
        postShader->setInt("ditherPattern", static_cast<int>(settings.ditherPattern));
        postShader->setBool("enableStatic", settings.staticEnabled);
        postShader->setFloat("staticIntensity", settings.staticIntensity);
        postShader->setFloat("staticGrainScale", std::max(0.01f, settings.staticGrainScale));
        postShader->setFloat("staticDarkAreaInfluence", std::clamp(settings.staticDarkAreaInfluence, 0.0f, 4.0f));
        postShader->setFloat("staticSpeed", settings.staticSpeed);
        postShader->setBool("staticMonochrome", settings.staticMonochrome);
        postShader->setFloat("staticSparkle", std::clamp(settings.staticSparkle, 0.0f, 1.0f));
        postShader->setBool("enableStaticDistortion", settings.staticDistortionEnabled);
        postShader->setFloat("staticDistortionHorizontalJitterAmount",
                             std::max(0.0f, settings.staticDistortionHorizontalJitterAmount));
        postShader->setFloat("staticDistortionLineDensity",
                             std::max(1.0f, settings.staticDistortionLineDensity));
        postShader->setFloat("staticDistortionGlitchFrequency", std::max(0.0f, settings.staticDistortionGlitchFrequency));
        postShader->setFloat("staticDistortionStrength", std::max(0.0f, settings.staticDistortionStrength));
        postShader->setBool("enableLensDistortion", settings.lensDistortionEnabled);
        postShader->setFloat("lensDistortionAmount", settings.lensDistortionAmount);
        postShader->setFloat("lensDistortionEdgeFalloff", std::clamp(settings.lensDistortionEdgeFalloff, 0.0f, 1.0f));
        postShader->setVec2("lensDistortionCenterOffset",
                            glm::clamp(settings.lensDistortionCenterOffset, glm::vec2(-0.5f), glm::vec2(0.5f)));
        postShader->setBool("enableLensEdgeVignette",
                            settings.lensDistortionEdgeVignetteEnabled);
        postShader->setFloat("lensEdgeVignetteIntensity",
                             std::clamp(settings.lensDistortionEdgeVignetteIntensity, 0.0f, 1.0f));
        postShader->setFloat("lensEdgeVignetteRadius",
                             std::clamp(settings.lensDistortionEdgeVignetteRadius, 0.0f, 1.4142f));
        postShader->setFloat("lensEdgeVignetteSoftness",
                             std::clamp(settings.lensDistortionEdgeVignetteSoftness, 0.001f, 1.0f));
        postShader->setVec3("lensEdgeVignetteColor",
                            glm::clamp(settings.lensDistortionEdgeVignetteColor,
                                       glm::vec3(0.0f), glm::vec3(1.0f)));
        postShader->setBool("enablePixelation", settings.pixelationEnabled);
        postShader->setFloat("pixelationSize",
                             std::clamp(settings.pixelationSize, 1.0f, 64.0f));
        postShader->setBool("enablePosterize", settings.posterizeEnabled);
        postShader->setInt("posterizeLevels",
                           std::clamp(settings.posterizeLevels, 2, 64));
        postShader->setBool("enableScanlines", settings.scanlinesEnabled);
        postShader->setFloat("scanlinesIntensity",
                             std::clamp(settings.scanlinesIntensity, 0.0f, 1.0f));
        postShader->setFloat("scanlinesDensity",
                             std::clamp(settings.scanlinesDensity, 0.25f, 8.0f));
        postShader->setFloat("scanlinesSpeed", settings.scanlinesSpeed);
        postShader->setBool("enableVHSOverlay", settings.vhsOverlayEnabled);
        postShader->setFloat("vhsOverlayOpacity", std::clamp(settings.vhsOverlayOpacity, 0.0f, 1.0f));
        postShader->setFloat("vhsOverlayScanlineStrength", std::clamp(settings.vhsOverlayScanlineStrength, 0.0f, 1.0f));
        postShader->setFloat("vhsOverlayTapeNoise", std::clamp(settings.vhsOverlayTapeNoise, 0.0f, 1.0f));
        postShader->setFloat("vhsOverlayChromaBleed", std::clamp(settings.vhsOverlayChromaBleed, 0.0f, 1.0f));
        postShader->setFloat("vhsOverlayBottomNoiseBandHeight",
                             std::clamp(settings.vhsOverlayBottomNoiseBandHeight, 0.0f, 1.0f));
        postShader->setFloat("vhsOverlayBottomNoiseBandIntensity",
                             std::clamp(settings.vhsOverlayBottomNoiseBandIntensity, 0.0f, 2.0f));
        postShader->setFloat("vhsOverlayDistortionStrength",
                             std::clamp(settings.vhsOverlayDistortionStrength, 0.0f, 2.0f));
        postShader->setFloat("vhsOverlayAnimationSpeed",
                             std::max(0.0f, settings.vhsOverlayAnimationSpeed));
        postShader->setFloat("vhsOverlayColorBleed",
                             std::clamp(settings.vhsOverlayColorBleed, 0.0f, 1.0f));
        postShader->setFloat("vhsOverlayBanding",
                             std::clamp(settings.vhsOverlayBanding, 0.0f, 1.0f));
        postShader->setInt("vhsOverlaySignalMode",
                           std::clamp(static_cast<int>(settings.vhsOverlaySignalMode), 0, 5));
        postShader->setFloat("vhsOverlayDropouts",
                             std::clamp(settings.vhsOverlayDropouts, 0.0f, 1.0f));
        postShader->setBool("enableWavyEffect", settings.wavyEnabled);
        postShader->setFloat("wavyAmplitude", std::max(0.0f, settings.wavyAmplitude));
        postShader->setFloat("wavyFrequency", std::max(0.0f, settings.wavyFrequency));
        postShader->setFloat("wavySpeed", settings.wavySpeed);
        postShader->setBool("wavyVertical", settings.wavyVertical);
        postShader->setVec2("texelSize", glm::vec2(1.0f / width, 1.0f / height));
        postShader->setFloat("u_time", static_cast<float>(glfwGetTime()));

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
        Modu2DStats::CountPostFxPass();
        postStats.compositeExecuted = true;
        postStats.compositeDestinationTextureId = target.texture;
        postStats.compositeDestinationFramebufferId = target.fbo;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glEnable(GL_DEPTH_TEST);
        postStats.compositeMs = std::chrono::duration<float, std::milli>(Clock::now() - compositeStart).count();
    }

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

    postStats.totalMs = std::chrono::duration<float, std::milli>(Clock::now() - totalStart).count();
    postStats.finalPresentedTextureId = target.texture;
    postStats.finalPresentedFramebufferId = target.fbo;
    postStats.finalTextureDiffersFromSource = (target.texture != sourceTexture);
    logPostFxDebug(postStats, allowHistory);
    return target.texture;
}

void Renderer::renderWireframeOverlay(
    const Camera &camera, const std::vector<SceneObject> &sceneObjects,
    float fovDeg, float nearPlane, float farPlane, bool shadedSurface) {
#if MODULARITY_OPENGL_ES
  (void)camera;
  (void)sceneObjects;
  (void)fovDeg;
  (void)nearPlane;
  (void)farPlane;
  (void)shadedSurface;
  return;
#else
  if (camera.orthographic || currentWidth <= 0 || currentHeight <= 0)
    return;

  struct WireframeDrawItem {
    const SceneObject *obj = nullptr;
    Mesh *mesh = nullptr;
    glm::mat4 model = glm::mat4(1.0f);
    bool wantsGpuSkinning = false;
    bool doubleSided = false;
  };

  auto resolveMesh = [this](const SceneObject &obj) -> Mesh * {
    if (obj.renderType == RenderType::Cube)
      return cubeMesh;
    if (obj.renderType == RenderType::Sphere)
      return sphereMesh;
    if (obj.renderType == RenderType::Capsule)
      return capsuleMesh;
    if (obj.renderType == RenderType::Plane)
      return planeMesh;
    if (obj.renderType == RenderType::Mirror)
      return planeMesh;
    if (obj.renderType == RenderType::Sprite)
      return planeMesh;
    if (obj.renderType == RenderType::Torus)
      return torusMesh;
    if (obj.renderType == RenderType::OBJMesh && obj.meshId >= 0) {
      return g_objLoader.getMesh(obj.meshId);
    }
    if (obj.renderType == RenderType::Model && obj.meshId >= 0) {
      return getModelLoader().getMesh(obj.meshId);
    }
    return nullptr;
  };

  rebuildStaticMergeBatches(sceneObjects);
  std::vector<WireframeDrawItem> drawItems;
  drawItems.reserve(sceneObjects.size() + staticMergeBatches.size());
  for (const SceneObject &obj : sceneObjects) {
    if (!IsObjectEnabledInHierarchy(obj) || !HasRendererComponent(obj))
      continue;
    if (staticMergeSourceIds.find(obj.id) != staticMergeSourceIds.end())
      continue;

    Mesh *mesh = resolveMesh(obj);
    if (!mesh)
      continue;

    bool wantsGpuSkinning = obj.hasSkeletalAnimation && obj.skeletal.enabled &&
                            obj.skeletal.useGpuSkinning;
    const int boneLimit = std::max(0, obj.skeletal.maxBones);
    const int availableBones =
        static_cast<int>(obj.skeletal.finalMatrices.size());
    if (obj.hasSkeletalAnimation && obj.skeletal.enabled &&
        obj.skeletal.allowCpuFallback && boneLimit > 0 &&
        availableBones > boneLimit) {
      wantsGpuSkinning = false;
    }

    if (obj.renderType == RenderType::Model && obj.meshId >= 0 &&
        obj.hasSkeletalAnimation && obj.skeletal.enabled && !wantsGpuSkinning) {
      const auto *meshInfo = getModelLoader().getMeshInfo(obj.meshId);
      if (meshInfo) {
        applyCpuSkinning(*const_cast<OBJLoader::LoadedMesh *>(meshInfo),
                         obj.skeletal.finalMatrices, obj.skeletal.maxBones);
      }
    }

    drawItems.push_back({
        &obj,
        mesh,
        BuildSceneObjectModelMatrix(obj, &camera.position, &camera.up),
        wantsGpuSkinning,
        obj.renderType == RenderType::Sprite ||
            obj.renderType == RenderType::Mirror,
    });
  }
  for (const StaticMergeBatch &batch : staticMergeBatches) {
    if (!batch.mesh)
      continue;
    drawItems.push_back(
        {nullptr, batch.mesh.get(), glm::mat4(1.0f), false, batch.doubleSided});
  }
  if (drawItems.empty())
    return;

  Shader *staticWireShader =
      getShader(selectionMaskVertPath, wireframeFragPath);
  Shader *skinnedWireShader = getShader(skinnedVertPath, wireframeFragPath);
  if (!staticWireShader || !skinnedWireShader)
    return;

  GLint previousFramebuffer = 0;
  GLint previousViewport[4] = {0, 0, currentWidth, currentHeight};
  GLint previousDepthFunc = GL_LESS;
  GLint previousCullMode = GL_BACK;
  GLint previousPolygonMode[2] = {GL_FILL, GL_FILL};
  GLfloat previousLineWidth = 1.0f;
  GLfloat previousPolygonOffsetFactor = 0.0f;
  GLfloat previousPolygonOffsetUnits = 0.0f;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
  glGetIntegerv(GL_VIEWPORT, previousViewport);
  glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
  glGetIntegerv(GL_CULL_FACE_MODE, &previousCullMode);
  glGetIntegerv(GL_POLYGON_MODE, previousPolygonMode);
  glGetFloatv(GL_LINE_WIDTH, &previousLineWidth);
  glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &previousPolygonOffsetFactor);
  glGetFloatv(GL_POLYGON_OFFSET_UNITS, &previousPolygonOffsetUnits);

  const GLboolean previousDepthTest = glIsEnabled(GL_DEPTH_TEST);
  const GLboolean previousCullFace = glIsEnabled(GL_CULL_FACE);
  const GLboolean previousBlend = glIsEnabled(GL_BLEND);
  const GLboolean previousStencil = glIsEnabled(GL_STENCIL_TEST);
  const GLboolean previousPolygonOffsetLine =
      glIsEnabled(GL_POLYGON_OFFSET_LINE);
  GLboolean previousDepthMask = GL_TRUE;
  GLboolean previousColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
  glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
  glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);

  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glViewport(0, 0, currentWidth, currentHeight);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(shadedSurface ? GL_LEQUAL : GL_LESS);
  glDepthMask(shadedSurface ? GL_FALSE : GL_TRUE);
  glDisable(GL_BLEND);
  glDisable(GL_STENCIL_TEST);
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  glLineWidth(1.0f);
  if (shadedSurface) {
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1.0f, -1.0f);
  } else {
    glDisable(GL_POLYGON_OFFSET_LINE);
  }

  const glm::mat4 view = camera.getViewMatrix();
  const glm::mat4 projection = BuildCameraProjection(
      camera, currentWidth, currentHeight, fovDeg, nearPlane, farPlane);
  const glm::vec4 wireColor = shadedSurface
                                  ? glm::vec4(0.035f, 0.045f, 0.065f, 1.0f)
                                  : glm::vec4(0.72f, 0.78f, 0.88f, 1.0f);

  Shader *activeShader = nullptr;
  for (const WireframeDrawItem &item : drawItems) {
    Shader *wireShader =
        item.wantsGpuSkinning ? skinnedWireShader : staticWireShader;
    if (wireShader != activeShader) {
      activeShader = wireShader;
      activeShader->use();
      activeShader->setMat4("view", view);
      activeShader->setMat4("projection", projection);
      activeShader->setVec4("wireColor", wireColor);
    }

    if (item.doubleSided) {
      glDisable(GL_CULL_FACE);
    } else {
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
    }

    activeShader->setMat4("model", item.model);
    if (item.wantsGpuSkinning && item.obj) {
      const int boneCount = std::min<int>(
          static_cast<int>(item.obj->skeletal.finalMatrices.size()),
          std::max(0, item.obj->skeletal.maxBones));
      activeShader->setInt("boneCount", boneCount);
      activeShader->setBool("useSkinning", boneCount > 0);
      activeShader->setVec4("uvRect", glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
      if (boneCount > 0) {
        activeShader->setMat4Array(
            "bones", item.obj->skeletal.finalMatrices.data(), boneCount);
      }
    }

    recordMeshDraw();
    item.mesh->draw();
  }

  glColorMask(previousColorMask[0], previousColorMask[1], previousColorMask[2],
              previousColorMask[3]);
  if (previousDepthTest)
    glEnable(GL_DEPTH_TEST);
  else
    glDisable(GL_DEPTH_TEST);
  glDepthMask(previousDepthMask);
  glDepthFunc(previousDepthFunc);
  if (previousCullFace) {
    glEnable(GL_CULL_FACE);
    glCullFace(previousCullMode);
  } else {
    glDisable(GL_CULL_FACE);
  }
  if (previousBlend)
    glEnable(GL_BLEND);
  else
    glDisable(GL_BLEND);
  if (previousStencil)
    glEnable(GL_STENCIL_TEST);
  else
    glDisable(GL_STENCIL_TEST);
  if (previousPolygonOffsetLine) {
    glEnable(GL_POLYGON_OFFSET_LINE);
  } else {
    glDisable(GL_POLYGON_OFFSET_LINE);
  }
  glPolygonOffset(previousPolygonOffsetFactor, previousPolygonOffsetUnits);
  glPolygonMode(GL_FRONT, previousPolygonMode[0]);
  glPolygonMode(GL_BACK, previousPolygonMode[1]);
  glLineWidth(previousLineWidth);
  glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
  glViewport(previousViewport[0], previousViewport[1], previousViewport[2],
             previousViewport[3]);
#endif
}

void Renderer::renderScene(const Camera &camera,
                           const std::vector<SceneObject> &sceneObjects,
                           int selectedId, float fovDeg, float nearPlane,
                           float farPlane, bool drawColliders,
                           const std::vector<int> *selectedIds,
                           SceneRenderMode renderMode, bool applyPostFX) {
  resetStats(viewportStats);
  activeStats = &viewportStats;
#if MODULARITY_OPENGL_ES
  renderMode = SceneRenderMode::Normal;
#endif
  const bool drawShadedScene = renderMode != SceneRenderMode::Wireframe;
  if (drawShadedScene && !camera.orthographic) {
    MODU_PROFILE_SCOPE("Mirror Targets", ProfilerSampleCategory::RenderDetail);
    updateMirrorTargets(camera, sceneObjects, currentWidth, currentHeight,
                        fovDeg, nearPlane, farPlane);
  }
  if (drawShadedScene) {
    MODU_PROFILE_SCOPE("Scene Draw", ProfilerSampleCategory::RenderDetail);
    renderSceneInternal(camera, sceneObjects, currentWidth, currentHeight, true,
                        fovDeg, nearPlane, farPlane, true, true);
  } else if (!camera.orthographic) {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, currentWidth, currentHeight);
    renderSkybox(camera.getViewMatrix(),
                 BuildCameraProjection(camera, currentWidth, currentHeight,
                                       fovDeg, nearPlane, farPlane));
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  if (renderMode != SceneRenderMode::Normal) {
    MODU_PROFILE_SCOPE("Wireframe Overlay",
                       ProfilerSampleCategory::RenderDetail);
    renderWireframeOverlay(camera, sceneObjects, fovDeg, nearPlane, farPlane,
                           renderMode == SceneRenderMode::ShadedWireframe);
  }
  selectionOutlineSourceScratch.clear();
  if (selectedIds && !selectedIds->empty()) {
    selectionOutlineSourceScratch.insert(selectionOutlineSourceScratch.end(),
                                         selectedIds->begin(),
                                         selectedIds->end());
  } else if (selectedId >= 0) {
    selectionOutlineSourceScratch.push_back(selectedId);
  }
  if (!camera.orthographic && drawColliders) {
    MODU_PROFILE_SCOPE("Collision Overlay",
                       ProfilerSampleCategory::RenderDetail);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, currentWidth, currentHeight);
    renderCollisionOverlay(camera, sceneObjects, currentWidth, currentHeight,
                           fovDeg, nearPlane, farPlane,
                           selectionOutlineSourceScratch.empty()
                               ? nullptr
                               : &selectionOutlineSourceScratch);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  if (!camera.orthographic) {
    {
      MODU_PROFILE_SCOPE("Selection Outline",
                         ProfilerSampleCategory::RenderDetail);
      renderSelectionOutline(camera, sceneObjects,
                             selectionOutlineSourceScratch, fovDeg, nearPlane,
                             farPlane);
    }
  }
  unsigned int result = 0;
  if (renderMode == SceneRenderMode::Normal && applyPostFX) {
    result = applyPostProcessing(camera, sceneObjects, viewportTexture,
                                 currentWidth, currentHeight, true);
  } else {
    viewportPostStats = {};
    clearHistory();
  }
  if (result) {
    displayTexture = result;
  } else {
    displayTexture = viewportTexture;
    viewportPostStats = {};
    clearHistory();
  }
  activeStats = nullptr;
}

void Renderer::renderSceneToTarget(const Camera& camera,
                                   const std::vector<SceneObject>& sceneObjects,
                                   unsigned int targetFramebuffer,
                                   int width, int height,
                                   const glm::mat4& projection,
                                   float nearPlane, float farPlane,
                                   bool clearTarget) {
    if (!moduRenderContextLive() || width <= 0 || height <= 0) {
        return;
    }

    // Same state discipline as renderScenePreview: capture what we are about to
    // disturb and put it back on every exit path, including the early returns
    // inside renderSceneInternal.
    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    struct TargetRenderStateRestore {
        GLint framebuffer = 0;
        GLint viewport[4] = {0, 0, 0, 0};
        Renderer* renderer = nullptr;

        ~TargetRenderStateRestore() {
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
            if (renderer) {
                // Cleared here rather than at the end of the function body so an
                // early return can never leave the override pointing at a matrix
                // that has gone out of scope.
                renderer->sceneProjectionOverride = nullptr;
                renderer->activeStats = nullptr;
            }
        }
    } restore{previousFramebuffer,
              {previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]},
              this};

    resetStats(viewportStats);
    activeStats = &viewportStats;
    sceneProjectionOverride = &projection;

    glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
    glViewport(0, 0, width, height);
    if (clearTarget) {
        if (camera.solidBackground) {
            glClearColor(camera.backgroundColor.r, camera.backgroundColor.g,
                         camera.backgroundColor.b, 1.0f);
        } else {
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    if (!camera.orthographic) {
        MODU_PROFILE_SCOPE("Mirror Targets", ProfilerSampleCategory::RenderDetail);
        updateMirrorTargets(camera, sceneObjects, width, height, FOV, nearPlane, farPlane);
        // Mirror updates bind their own targets; come back to ours before drawing.
        glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
        glViewport(0, 0, width, height);
    }
    {
        MODU_PROFILE_SCOPE("Scene Draw", ProfilerSampleCategory::RenderDetail);
        // unbindFramebuffer = false: the target belongs to the caller (for XR, to
        // the OpenXR runtime), so this must not bind 0 behind their back.
        renderSceneInternal(camera, sceneObjects, width, height, false, FOV, nearPlane, farPlane,
                            true, true);
    }
}

unsigned int Renderer::renderScenePreview(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, float fovDeg, float nearPlane, float farPlane, bool applyPostFX, int previewSlot, bool transparentBackground) {
    if (!moduRenderContextLive()) {
        return 0;
    }
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        return 0;
    }

    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    struct PreviewRenderStateRestore {
        GLint framebuffer = 0;
        GLint viewport[4] = {0, 0, 0, 0};
        Renderer* renderer = nullptr;

        ~PreviewRenderStateRestore() {
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
            if (renderer) {
                renderer->activeStats = nullptr;
            }
        }
    } restore{previousFramebuffer,
              {previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]},
              this};

    resetStats(previewStats);
    activeStats = &previewStats;
    RenderTarget& target = (previewSlot == 0) ? previewTarget : extraPreviewTargets[previewSlot];
    if (previewSlot != 0) {
        extraPreviewTargetLastUsed[previewSlot] = ++previewTargetUseSerial;
        purgePreviewTargetsIfNeeded(previewSlot);
    }
    ensureRenderTarget(target, width, height, transparentBackground, true);
    if (target.fbo == 0) {
        previewPostStats = {};
        return 0;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, width, height);
    if (transparentBackground) {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    } else if (camera.solidBackground) {
        glClearColor(camera.backgroundColor.r, camera.backgroundColor.g,
                     camera.backgroundColor.b, 1.0f);
    } else {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!camera.orthographic) {
        MODU_PROFILE_SCOPE("Mirror Targets", ProfilerSampleCategory::RenderDetail);
        updateMirrorTargets(camera, sceneObjects, width, height, fovDeg, nearPlane, farPlane);
    }
    {
        MODU_PROFILE_SCOPE("Scene Draw", ProfilerSampleCategory::RenderDetail);
        renderSceneInternal(camera, sceneObjects, width, height, true, fovDeg, nearPlane, farPlane, true, !transparentBackground);
    }
    if (!applyPostFX) {
        previewPostStats = {};
        return target.texture;
    }
    unsigned int processed = applyPostProcessing(camera, sceneObjects, target.texture, width, height, false);
    return processed ? processed : target.texture;
}

void Renderer::renderCollisionOverlay(const Camera& camera, const std::vector<SceneObject>& sceneObjects, int width, int height, float fovDeg, float nearPlane, float farPlane, const std::vector<int>* previewIds) {
#if MODULARITY_OPENGL_ES
    (void)camera;
    (void)sceneObjects;
    (void)width;
    (void)height;
    (void)fovDeg;
    (void)nearPlane;
    (void)farPlane;
    (void)previewIds;
    return;
#else
    if (camera.orthographic || !defaultShader || width <= 0 || height <= 0) return;
    std::unordered_set<int> previewSet;
    if (previewIds && !previewIds->empty()) {
        previewSet.insert(previewIds->begin(), previewIds->end());
    }

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
    active->setMat4("projection", BuildCameraProjection(camera, width, height, fovDeg, nearPlane, farPlane));
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
        if (!previewSet.empty() && previewSet.find(obj.id) == previewSet.end()) continue;
        if (!IsObjectEnabledInHierarchy(obj)) continue;
        if (!(obj.hasCollider && obj.collider.enabled) && !(obj.hasRigidbody && obj.rigidbody.enabled)) continue;

        Mesh* meshToDraw = nullptr;
        glm::vec3 scale = obj.scale;
        glm::vec3 colliderOffset(0.0f);

        if (obj.hasCollider && obj.collider.enabled) {
            colliderOffset = obj.collider.offset;
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

        glm::mat4 model(1.0f);
        model = glm::translate(model, obj.position);
        model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::translate(model, colliderOffset);
        model = glm::scale(model, scale);
        active->setMat4("model", model);

        meshToDraw->draw();
    }

    if (!polyOffsetLine) glDisable(GL_POLYGON_OFFSET_LINE);
    if (cullFace) glEnable(GL_CULL_FACE);
    if (depthTest) glEnable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, prevPoly[0]);
#endif
}

void Renderer::renderSelectionOutline(const Camera& camera, const std::vector<SceneObject>& sceneObjects, const std::vector<int>& selectedIds, float fovDeg, float nearPlane, float farPlane) {
    if (camera.orthographic) {
        return;
    }
    const double nowSec = glfwGetTime();
    if (selectionOutlineLastUpdateSec <= 0.0 || nowSec < selectionOutlineLastUpdateSec) {
        selectionOutlineLastUpdateSec = nowSec;
    }
    float dt = static_cast<float>(nowSec - selectionOutlineLastUpdateSec);
    selectionOutlineLastUpdateSec = nowSec;
    dt = std::clamp(dt, 0.0f, 0.25f);

    selectionOutlineInputScratch.clear();
    selectionOutlineInputScratch.reserve(selectedIds.size());
    for (int id : selectedIds) {
        if (id >= 0) selectionOutlineInputScratch.push_back(id);
    }
    std::sort(selectionOutlineInputScratch.begin(), selectionOutlineInputScratch.end());
    selectionOutlineInputScratch.erase(
        std::unique(selectionOutlineInputScratch.begin(), selectionOutlineInputScratch.end()),
        selectionOutlineInputScratch.end());

    constexpr float kFadeInSeconds = 0.05f;
    constexpr float kFadeOutSeconds = 0.05f;
    constexpr float kSwapSeconds = 0.10f;
    const bool hasSelection = !selectionOutlineInputScratch.empty();
    if (hasSelection) {
        if (selectionOutlineVisualIds.empty()) {
            selectionOutlineVisualIds = selectionOutlineInputScratch;
            selectionOutlinePrevIds.clear();
            selectionOutlineSwapBlend = 1.0f;
        } else if (selectionOutlineVisualIds != selectionOutlineInputScratch) {
            selectionOutlinePrevIds = selectionOutlineVisualIds;
            selectionOutlineVisualIds = selectionOutlineInputScratch;
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

    auto buildDrawItems = [&](const std::vector<int>& ids, std::vector<MaskDrawItem>& drawItems) {
        selectionOutlineSelectedScratch.clear();
        selectionOutlineSelectedScratch.reserve(ids.size());
        for (int id : ids) {
            selectionOutlineSelectedScratch.insert(id);
        }

        drawItems.clear();
        drawItems.reserve(selectionOutlineSelectedScratch.size());

        for (const auto& obj : sceneObjects) {
            if (!IsObjectEnabledInHierarchy(obj)) continue;
            if (selectionOutlineSelectedScratch.find(obj.id) == selectionOutlineSelectedScratch.end()) continue;
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
    };

    buildDrawItems(selectionOutlineVisualIds, selectionOutlineCurrentDrawScratch);
    selectionOutlinePreviousDrawScratch.clear();
    if (!selectionOutlinePrevIds.empty() && selectionOutlineSwapBlend < 0.999f) {
        buildDrawItems(selectionOutlinePrevIds, selectionOutlinePreviousDrawScratch);
    }
    if (selectionOutlinePreviousDrawScratch.empty()) {
        selectionOutlinePrevIds.clear();
        selectionOutlineSwapBlend = 1.0f;
    }
    if (selectionOutlineCurrentDrawScratch.empty() && selectionOutlinePreviousDrawScratch.empty()) return;

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
    GLfloat prevPolyOffsetFactor = 0.0f;
    GLfloat prevPolyOffsetUnits = 0.0f;
#if !MODULARITY_OPENGL_ES
    GLint prevPoly[2] = { GL_FILL, GL_FILL };
#endif
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstAlpha);
    glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
#if !MODULARITY_OPENGL_ES
    glGetIntegerv(GL_POLYGON_MODE, prevPoly);
#endif
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
    const glm::mat4 projection = BuildCameraProjection(camera, currentWidth, currentHeight, fovDeg, nearPlane, farPlane);
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
#if !MODULARITY_OPENGL_ES
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
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

            glm::mat4 model = BuildSceneObjectModelMatrix(*drawItem.obj, &camera.position, &camera.up);

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
    if (!selectionOutlinePreviousDrawScratch.empty() && selectionOutlineSwapBlend < 0.999f) {
        previousPassWeight = 1.0f - selectionOutlineSwapBlend;
        currentPassWeight = selectionOutlineSwapBlend;
    }

    drawOutlinePass(selectionOutlinePreviousDrawScratch, previousPassWeight);
    drawOutlinePass(selectionOutlineCurrentDrawScratch, currentPassWeight);

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
#if !MODULARITY_OPENGL_ES
    glPolygonMode(GL_FRONT, prevPoly[0]);
    glPolygonMode(GL_BACK, prevPoly[1]);
#endif

    glActiveTexture(prevActiveTex);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, prevDrawFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

void Renderer::endRender() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    Runtime2DCountStateBind();
}
