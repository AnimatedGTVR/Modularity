#pragma once

#include "Common.h"
#include <cstdint>

namespace Modularity::Render25D {

enum class TMTextureFilter : uint32_t {
    Bilinear = 0,
    Point = 1
};

struct TMSurfacePresentation {
    glm::vec4 colorTint = glm::vec4(1.0f);
    TMTextureFilter textureFilter = TMTextureFilter::Bilinear;
    bool affineTextureWarpEnabled = false;
    float affineTextureWarpStrength = 0.35f;
    glm::vec2 uvTiling = glm::vec2(1.0f);
    glm::vec2 uvOffset = glm::vec2(0.0f);
};

struct TMWobbleSettings {
    bool enabled = false;
    float strength = 0.0125f;
    float speed = 1.0f;
    float seed = 0.0f;
    glm::vec3 offset = glm::vec3(0.0f);
};

struct TMPresentationSettings {
    // Glue3D-style "2dray: tile+seg+m7,tsm" fake-3D presentation
    bool fake3DEnabled = true;
    int fake3DInternalHeight = 240;   // low-res buffer lines, width follows aspect
    bool fake3DPointSampling = true;  // force nearest filtering on all TM textures
    bool fake3DFlatShading = true;    // one light level per face (software look)
    int fake3DShadeLevels = 5;        // quantized light bands, 0 = smooth
    bool fake3DAffineTextures = true;  // affine (non-perspective-correct) mapping
    float fake3DAffineStrength = 1.0f; // 0 = perspective-correct, 1 = fully affine
    bool vertexWobbleEnabled = false;
    float wobbleStrength = 0.0125f;
    float wobbleSpeed = 1.0f;
    bool textureWarpEnabled = false;
    bool lookPitchStretchEnabled = true;
    float lookPitchStretchStrength = 0.28f;
    float lookPitchCompressStrength = 0.18f;
    float lookPitchShearStrength = 0.16f;
    float lookPitchCurve = 1.0f;       // response exponent on normalized pitch
    float lookPitchDepthRange = 24.0f; // view depth over which pitch shear ramps in
    float presentationPitchMinDegrees = -65.0f;
    float presentationPitchMaxDegrees = 65.0f;
    bool presentationSnapEnabled = true;
    float presentationSnapStep = 0.125f;
    bool cameraRelativeSnapEnabled = true;
    float cameraRelativeSnapStep = 0.125f;
    bool vertexSnapEnabled = true;
    float vertexSnapStep = 0.0625f;
    bool screenSnapEnabled = true;
    float screenSnapStep = 2.0f;
    bool presentationAwareCullingEnabled = true;
    bool skipFrustumCullingForDeformedObjects = false;
    bool inflatePresentationBounds = true;
    float presentationBoundsPadding = 0.35f;
    float presentationBoundsScale = 1.15f;
};

} // namespace Modularity::Render25D
