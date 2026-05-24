#pragma once

#include "Engine.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

void ModuRuntime2DProfiler_RecordUiRuntime(double uiRuntimeMs,
                                           double spriteBatchBuildMs,
                                           uint32_t visibleObjectCount);

namespace ViewportRenderHelpers {

constexpr int kRuntimeInternalWidth = 1280;
constexpr int kRuntimeInternalHeight = 720;

// ---------- Particle helpers ----------

uint32_t ParticleHash(uint32_t x);
float ParticleUnit(uint32_t &state);
float ParticleRange(ParticleSystem2DComponent::MinMaxFloat range, uint32_t &state);
void RestartParticleSystem2D(ParticleSystem2DComponent &ps, double now);
void EmitParticle2D(SceneObject &obj, uint32_t emissionIndex);
void SimulateParticleSystem2D(SceneObject &obj, double now);

template <typename WorldToScreenFn, typename WorldToRenderLocalFn,
          typename ParentOffsetFn, typename RectOutsideFn>
void AppendParticleSystem2DSprites(std::vector<SceneObject> &sceneObjects,
                                   Renderer &renderer, double now,
                                   WorldToScreenFn worldToScreen,
                                   WorldToRenderLocalFn worldToRenderLocal,
                                   ParentOffsetFn getParentOffset,
                                   RectOutsideFn rectOutsideOverlay,
                                   Light2DRenderRequest &lightRequest,
                                   int &drawOrder) {
  const unsigned int fallbackTextureId = renderer.getDebugWhiteTextureId();
  for (SceneObject &obj : sceneObjects) {
    if (!IsObjectEnabledInHierarchy(obj) || !obj.hasParticleSystem2D) continue;

    SimulateParticleSystem2D(obj, now);

    ParticleSystem2DComponent &ps = obj.particleSystem2D;
    if (!ps.enabled || ps.particles.empty()) continue;

    unsigned int textureId = 0;
    const std::string texturePath =
        !ps.texturePath.empty() ? ps.texturePath : obj.albedoTexturePath;
    if (!texturePath.empty()) {
      if (Texture *particleTex =
              renderer.getTexture(texturePath, obj.material.textureFilter)) {
        textureId = particleTex->GetID();
      }
    }
    if (textureId == 0) {
      textureId = fallbackTextureId;
    }
    if (textureId == 0) continue;

    const glm::vec2 emitterWorld =
        getParentOffset(obj) + glm::vec2(obj.position.x, obj.position.y);
    for (const auto &particle : ps.particles) {
      if (!particle.alive) continue;
      const float t = std::clamp(
          particle.age / std::max(0.01f, particle.lifetime), 0.0f, 1.0f);
      const glm::vec2 worldCenter = emitterWorld + particle.position;
      const float size = ps.sizeOverLifetimeEnabled
                             ? glm::mix(particle.size, ps.sizeOverLifetime, t)
                             : particle.size;
      const glm::vec2 half(std::max(0.001f, size) * 0.5f);
      const glm::vec2 worldMin = worldCenter - half;
      const glm::vec2 worldMax = worldCenter + half;
      ImVec2 s0 = worldToScreen(worldMin);
      ImVec2 s1 = worldToScreen(worldMax);
      ImVec2 rectMin(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
      ImVec2 rectMax(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
      if (rectOutsideOverlay(rectMin, rectMax)) continue;

      glm::vec2 r0 = worldToRenderLocal(worldMin);
      glm::vec2 r1 = worldToRenderLocal(worldMax);
      glm::vec2 center = worldToRenderLocal(worldCenter);
      const glm::vec2 renderHalf(std::abs(r1.x - r0.x) * 0.5f,
                                 std::abs(r1.y - r0.y) * 0.5f);
      const float angle = glm::radians(particle.rotation);
      const float c = std::cos(angle);
      const float s = std::sin(angle);
      auto rotatePoint = [&](float x, float y) {
        return glm::vec2(center.x + x * c - y * s,
                         center.y + x * s + y * c);
      };

      Light2DScreenSprite sprite;
      sprite.objectId = obj.id;
      sprite.layer = obj.layer;
      sprite.drawOrder = drawOrder++;
      sprite.textureId = textureId;
      sprite.tint = ps.colorOverLifetimeEnabled
                        ? glm::mix(particle.startColor, ps.colorOverLifetime, t)
                        : particle.startColor;
      sprite.receiveLighting = ps.receiveLighting2D;
      sprite.unlit = ps.unlitLighting2D;
      sprite.emissiveIntensity = ps.emissiveLighting2D;
      sprite.positions[0] = rotatePoint(-renderHalf.x, -renderHalf.y);
      sprite.positions[1] = rotatePoint(renderHalf.x, -renderHalf.y);
      sprite.positions[2] = rotatePoint(renderHalf.x, renderHalf.y);
      sprite.positions[3] = rotatePoint(-renderHalf.x, renderHalf.y);
      sprite.uvs[0] = glm::vec2(0.0f, 0.0f);
      sprite.uvs[1] = glm::vec2(1.0f, 0.0f);
      sprite.uvs[2] = glm::vec2(1.0f, 1.0f);
      sprite.uvs[3] = glm::vec2(0.0f, 1.0f);
      lightRequest.sprites.push_back(sprite);
    }
  }
}

template <typename RectOutsideFn>
void AppendProjectedParticleSystem2DSprites(
    std::vector<SceneObject> &sceneObjects, Renderer &renderer, double now,
    const glm::mat4 &view, const glm::mat4 &proj, const ImVec2 &overlayPos,
    const ImVec2 &overlaySize, const ImVec2 &renderLocalOrigin,
    RectOutsideFn rectOutsideRenderLocal, Light2DRenderRequest &lightRequest,
    int &drawOrder);

// ---------- Viewport layout ----------

struct EmbeddedViewportLayout {
  ImVec2 panelMin = ImVec2(0.0f, 0.0f);
  ImVec2 panelMax = ImVec2(0.0f, 0.0f);
  ImVec2 panelSize = ImVec2(0.0f, 0.0f);
  ImVec2 displayMin = ImVec2(0.0f, 0.0f);
  ImVec2 displayMax = ImVec2(0.0f, 0.0f);
  ImVec2 displaySize = ImVec2(0.0f, 0.0f);
  ImVec2 uvMin = ImVec2(0.0f, 0.0f);
  ImVec2 uvMax = ImVec2(1.0f, 1.0f);
};

ImVec2 ComputeAspectFitSize(const ImVec2 &available, float aspect);

EmbeddedViewportLayout BuildEmbeddedViewportLayout(
    const ImVec2 &panelMin, const ImVec2 &panelSize, int renderWidth,
    int renderHeight, ViewportDisplayMode displayMode, float zoom = 1.0f);

bool TryMapScreenPointToRenderPixel(const EmbeddedViewportLayout &layout,
                                    const ImVec2 &screenPoint, int renderWidth,
                                    int renderHeight, glm::vec2 &outRenderPixel,
                                    glm::vec2 *outNormalized = nullptr);

// ---------- Profiler stats ----------

struct Runtime2DWorldProfilerStats {
  bool useWorldUi = false;
  double uiRuntimeMs = 0.0;
  double spriteBatchBuildMs = 0.0;
  uint32_t visibleObjectCount = 0;
  float light2DBuildMs = 0.0f;
  int light2DVisibleLights = 0;
  int light2DVisibleSprites = 0;
};

void SubmitRuntime2DWorldProfilerStats(const Runtime2DWorldProfilerStats &stats);

// ---------- Texture / sorting ----------

void ApplyNearestTextureSampling(GLuint textureId);

bool RuntimeUiDrawOrderLess(const SceneObject *a, const SceneObject *b);
void StableSortRuntimeUiDrawList(std::vector<SceneObject *> &drawList);

// ---------- Projection ----------

bool ProjectWorldToOverlayPoint(const glm::vec3 &worldPos,
                                const glm::mat4 &view, const glm::mat4 &proj,
                                const ImVec2 &overlayPos,
                                const ImVec2 &overlaySize, ImVec2 &outScreen);

bool HasMeaningfulSpriteFrameScales(const UIElementComponent &ui);
glm::vec2 ResolveSpriteFrameScale(const UIElementComponent &ui);

bool ResolveProjectedSprite25DRect(const SceneObject &obj,
                                   const glm::mat4 &view, const glm::mat4 &proj,
                                   const ImVec2 &overlayPos,
                                   const ImVec2 &overlaySize, ImVec2 &outMin,
                                   ImVec2 &outMax);

// ---------- UI text rendering ----------

void ApplyUIFontFilterCallback(const ImDrawList *, const ImDrawCmd *cmd);

void AppendWrappedTextSegment(ImFont *font, float fontSize, const char *start,
                              const char *end, float wrapWidth, bool autoWrap,
                              std::vector<std::string> &outLines);

std::vector<std::string> BuildWrappedTextLines(ImFont *font, float fontSize,
                                               const char *text,
                                               float wrapWidth, bool autoWrap);

void DrawUITextLineWithEffects(ImDrawList *drawList, ImFont *font,
                               float fontSize, const ImVec2 &linePos,
                               ImU32 baseColor, const char *lineText,
                               int effectFlags, float effectSpeed,
                               float effectIntensity);

void AddUITextWithFilter(ImDrawList *drawList,
                         MaterialProperties::TextureFilter filter, ImFont *font,
                         float fontSize, const ImVec2 &drawMin,
                         const ImVec2 &drawMax, ImU32 color, const char *text,
                         bool autoWrap, UITextHAlign hAlign,
                         UITextVAlign vAlign, int effectFlags,
                         float effectSpeed, float effectIntensity);

// Renders a non-ImGui slider style (Fill / Vertical / Stepped / Circle / Ring).
// The Circle fan-fill is robust at all `t` values (the previous PathFillConvex
// implementation broke for arcs > 180° and at t=0).
void RenderUISliderStyle(ImDrawList *dl, UISliderStyle style,
                         const ImVec2 &drawMin, const ImVec2 &drawMax,
                         const ImVec2 &drawSize,
                         ImU32 bg, ImU32 fillColor, ImU32 border, ImU32 textColor,
                         float t, float minValue, float maxValue,
                         const char *label);

// Rotates the vertices in a draw-list range around `pivot`. Use after adding
// text/glyphs to a draw list and before any subsequent commands so only the
// snapshotted range gets rotated.
void RotateDrawListVertices(ImDrawList *dl, int firstVertex, int lastVertex,
                            const ImVec2 &pivot, float angleRadians);

void ScaleDrawListVertices(ImDrawList *dl, int firstVertex, int lastVertex,
                           const ImVec2 &pivot, const ImVec2 &scale);

// `viewportRenderScale` is the on-screen size of the viewport divided by the
// resolution it's rendered at (e.g. 1.5 means the rendered image is being
// upscaled 1.5x). Screen-space UI text multiplies font size by it so text
// scales 1:1 with the rendered images instead of staying a fixed pixel size.
// Defaults to 1.0 to preserve existing call-sites.
float ComputeViewportTextFontSize(float baseFontSize, float textScale,
                                  bool useWorldUi, float worldUiZoom,
                                  float viewportRenderScale = 1.0f);

float ResolveViewportUITextFontSize(float baseFontSize, float textScale,
                                    float explicitFontSize, bool useWorldUi,
                                    float worldUiZoom,
                                    float viewportRenderScale = 1.0f,
                                    float elementScaleY = 1.0f);

void ApplyUITextScaleFromRectResize(SceneObject &obj, float startTextScale,
                                    float startFontSize,
                                    const ImVec2 &startScreenSize,
                                    const ImVec2 &newScreenSize);

// ---------- Scene lookup cache ----------

struct UiSceneLookupCache {
  explicit UiSceneLookupCache(const std::vector<SceneObject> &objects) {
    byId.reserve(objects.size());
    parentOffsetCache.reserve(objects.size());
    canvas3DIdCache.reserve(objects.size());
    pseudoCanvasIdCache.reserve(objects.size());
    for (const SceneObject &obj : objects) {
      byId.emplace(obj.id, &obj);
    }
  }

  const SceneObject *find(int id) const {
    auto it = byId.find(id);
    return (it != byId.end()) ? it->second : nullptr;
  }

  glm::vec2 getWorldParentOffset(const SceneObject &obj) {
    auto cached = parentOffsetCache.find(obj.id);
    if (cached != parentOffsetCache.end()) {
      return cached->second;
    }

    glm::vec2 offset(0.0f);
    if (const SceneObject *parent = find(obj.parentId)) {
      offset = getWorldParentOffset(*parent);
      if (parent->type == ObjectType::Sprite25D) {
        offset += glm::vec2(parent->position.x, parent->position.y);
      } else if (parent->hasUI && parent->ui.type != UIElementType::None) {
        offset += glm::vec2(parent->ui.position.x, parent->ui.position.y);
      } else {
        offset += glm::vec2(parent->position.x, parent->position.y);
      }
    }

    parentOffsetCache.emplace(obj.id, offset);
    return offset;
  }

  int find3DCanvasId(const SceneObject &obj) {
    auto cached = canvas3DIdCache.find(obj.id);
    if (cached != canvas3DIdCache.end()) {
      return cached->second;
    }

    int canvasId = -1;
    if (obj.hasUI && obj.ui.type == UIElementType::Canvas &&
        obj.ui.renderIn3D) {
      canvasId = obj.id;
    } else if (const SceneObject *parent = find(obj.parentId)) {
      canvasId = find3DCanvasId(*parent);
    }

    canvas3DIdCache.emplace(obj.id, canvasId);
    return canvasId;
  }

  int findPseudo3DCanvasId(const SceneObject &obj) {
    auto cached = pseudoCanvasIdCache.find(obj.id);
    if (cached != pseudoCanvasIdCache.end()) {
      return cached->second;
    }

    int canvasId = -1;
    if (obj.hasUI && obj.ui.type == UIElementType::Canvas &&
        !obj.ui.renderIn3D && obj.ui.pseudo3DEnabled &&
        obj.ui.pseudo3DUseOffscreenSurface) {
      canvasId = obj.id;
    } else if (const SceneObject *parent = find(obj.parentId)) {
      canvasId = findPseudo3DCanvasId(*parent);
    }

    pseudoCanvasIdCache.emplace(obj.id, canvasId);
    return canvasId;
  }

private:
  std::unordered_map<int, const SceneObject *> byId;
  std::unordered_map<int, glm::vec2> parentOffsetCache;
  std::unordered_map<int, int> canvas3DIdCache;
  std::unordered_map<int, int> pseudoCanvasIdCache;
};

// ---------- Sprite texture resolver ----------

class SpriteTextureResolver {
public:
  explicit SpriteTextureResolver(Renderer *renderer) : renderer(renderer) {}

  Texture *resolveTexture(const SceneObject &obj) {
    if (renderer == nullptr || obj.albedoTexturePath.empty()) {
      return nullptr;
    }

    auto cached = textureIdCache.find(obj.albedoTexturePath);
    if (cached != textureIdCache.end()) {
      return cached->second.texture;
    }

    Texture *texture = renderer->getTexture(
        obj.albedoTexturePath, MaterialProperties::TextureFilter::Point);
    CachedTexture cachedTexture;
    cachedTexture.texture = texture;
    cachedTexture.textureId = (texture != nullptr) ? texture->GetID() : 0;
    textureIdCache.emplace(obj.albedoTexturePath, cachedTexture);
    return texture;
  }

  unsigned int resolve(const SceneObject &obj) {
    Texture *texture = resolveTexture(obj);
    if (texture == nullptr) {
      return 0;
    }
    auto cached = textureIdCache.find(obj.albedoTexturePath);
    return (cached != textureIdCache.end()) ? cached->second.textureId
                                            : texture->GetID();
  }

private:
  struct CachedTexture {
    Texture *texture = nullptr;
    unsigned int textureId = 0;
  };

  Renderer *renderer = nullptr;
  std::unordered_map<std::string, CachedTexture> textureIdCache;
};

// ---------- Batched sprite emitter ----------

struct BatchedSpriteQuad {
  ImTextureID textureId = 0;
  ImVec2 pos[4];
  ImVec2 uv[4];
  ImU32 color = 0;
};

class BatchedSpriteEmitter {
public:
  explicit BatchedSpriteEmitter(ImDrawList *drawList) : drawList(drawList) {}

  void reserve(size_t quadCount) { quads.reserve(quadCount); }

  void push(ImTextureID textureId, const ImVec2 &p0, const ImVec2 &p1,
            const ImVec2 &p2, const ImVec2 &p3, const ImVec2 &uv0,
            const ImVec2 &uv1, const ImVec2 &uv2, const ImVec2 &uv3,
            ImU32 color) {
    if (textureId == 0) {
      flush();
      return;
    }
    if (!quads.empty() && textureId != currentTextureId) {
      flush();
    }
    currentTextureId = textureId;
    BatchedSpriteQuad &quad = quads.emplace_back();
    quad.textureId = textureId;
    quad.pos[0] = p0;
    quad.pos[1] = p1;
    quad.pos[2] = p2;
    quad.pos[3] = p3;
    quad.uv[0] = uv0;
    quad.uv[1] = uv1;
    quad.uv[2] = uv2;
    quad.uv[3] = uv3;
    quad.color = color;
  }

  void flush() {
    if (quads.empty() || drawList == nullptr || currentTextureId == 0) {
      quads.clear();
      currentTextureId = 0;
      return;
    }

    drawList->PushTextureID(currentTextureId);
    drawList->PrimReserve(static_cast<int>(quads.size()) * 6,
                          static_cast<int>(quads.size()) * 4);
    for (const BatchedSpriteQuad &quad : quads) {
      unsigned int idx = drawList->_VtxCurrentIdx;
      drawList->PrimWriteIdx(static_cast<ImDrawIdx>(idx));
      drawList->PrimWriteIdx(static_cast<ImDrawIdx>(idx + 1));
      drawList->PrimWriteIdx(static_cast<ImDrawIdx>(idx + 2));
      drawList->PrimWriteIdx(static_cast<ImDrawIdx>(idx));
      drawList->PrimWriteIdx(static_cast<ImDrawIdx>(idx + 2));
      drawList->PrimWriteIdx(static_cast<ImDrawIdx>(idx + 3));
      drawList->PrimWriteVtx(quad.pos[0], quad.uv[0], quad.color);
      drawList->PrimWriteVtx(quad.pos[1], quad.uv[1], quad.color);
      drawList->PrimWriteVtx(quad.pos[2], quad.uv[2], quad.color);
      drawList->PrimWriteVtx(quad.pos[3], quad.uv[3], quad.color);
    }
    drawList->PopTextureID();
    quads.clear();
    currentTextureId = 0;
  }

private:
  ImDrawList *drawList = nullptr;
  ImTextureID currentTextureId = 0;
  std::vector<BatchedSpriteQuad> quads;
};

// ---------- Sprite frame / nine-slice ----------

ImVec2 ResolveUiSourceFrameSizePx(const SceneObject &obj, int frame,
                                  const Texture *texture);

bool DrawNineSliceSprite(BatchedSpriteEmitter &spriteBatch,
                         ImTextureID textureId, const SceneObject &obj,
                         const ImVec2 &drawMin, const ImVec2 &drawMax,
                         const std::array<ImVec2, 4> &uvQuad,
                         const ImVec2 &sourceFrameSizePx, float angleRad,
                         ImU32 color);

// ---------- Pseudo-3D canvas helpers ----------

glm::vec2 ResolvePseudo3DLayoutSize(const SceneObject &canvas);

float Cross2D(const ImVec2 &a, const ImVec2 &b);

bool PointInTriangleBarycentric(const ImVec2 &p, const ImVec2 &a,
                                const ImVec2 &b, const ImVec2 &c, float &wa,
                                float &wb, float &wc);

bool MapPointToPseudo3DQuadUV(const std::array<ImVec2, 4> &corners,
                              const ImVec2 &point, ImVec2 &outUv);

std::array<ImVec2, 4>
BuildPseudo3DPanelCorners(const ImVec2 &panelMin, const ImVec2 &panelMax,
                          const UIElementComponent &ui, float distanceScale,
                          float perspectiveDistanceFactor,
                          const std::array<ImVec2, 4> *baseCorners = nullptr);

// Project a transform-driven canvas's world-space quad to screen-space.
// The canvas is treated as a unit quad in its local XY plane, scaled by
// canvas.scale.xy, transformed by canvas.position and canvas.rotation (Euler XYZ
// degrees). Corners are emitted in pseudo-3D order: 0=TL, 1=TR, 2=BR, 3=BL,
// matching BuildPseudo3DPanelCorners. Returns false if any corner is behind the
// camera near plane (caller should skip rendering in that case).
bool ProjectTransformDrivenCanvasCorners(const SceneObject &canvas,
                                         const glm::mat4 &view,
                                         const glm::mat4 &proj,
                                         const ImVec2 &overlayPos,
                                         const ImVec2 &overlaySize,
                                         std::array<ImVec2, 4> &outCorners);

void ResolvePseudo3DDistanceState(const UIElementComponent &ui, float distance,
                                  float &outScale, float &outPerspectiveFactor,
                                  bool &outAllowInteraction);

// ---------- AppendProjectedParticleSystem2DSprites implementation ----------

template <typename RectOutsideFn>
void AppendProjectedParticleSystem2DSprites(
    std::vector<SceneObject> &sceneObjects, Renderer &renderer, double now,
    const glm::mat4 &view, const glm::mat4 &proj, const ImVec2 &overlayPos,
    const ImVec2 &overlaySize, const ImVec2 &renderLocalOrigin,
    RectOutsideFn rectOutsideRenderLocal, Light2DRenderRequest &lightRequest,
    int &drawOrder) {
  const unsigned int fallbackTextureId = renderer.getDebugWhiteTextureId();
  const glm::mat4 invView = glm::inverse(view);
  const glm::vec3 cameraRight = glm::normalize(glm::vec3(invView[0]));
  const glm::vec3 cameraUp = glm::normalize(glm::vec3(invView[1]));

  for (SceneObject &obj : sceneObjects) {
    if (!IsObjectEnabledInHierarchy(obj) || !obj.hasParticleSystem2D) continue;

    SimulateParticleSystem2D(obj, now);

    ParticleSystem2DComponent &ps = obj.particleSystem2D;
    if (!ps.enabled || ps.particles.empty()) continue;

    unsigned int textureId = 0;
    const std::string texturePath =
        !ps.texturePath.empty() ? ps.texturePath : obj.albedoTexturePath;
    if (!texturePath.empty()) {
      if (Texture *particleTex =
              renderer.getTexture(texturePath, obj.material.textureFilter)) {
        textureId = particleTex->GetID();
      }
    }
    if (textureId == 0) textureId = fallbackTextureId;
    if (textureId == 0) continue;

    glm::mat4 objectRotation(1.0f);
    objectRotation = glm::rotate(objectRotation, glm::radians(obj.rotation.x),
                                 glm::vec3(1.0f, 0.0f, 0.0f));
    objectRotation = glm::rotate(objectRotation, glm::radians(obj.rotation.y),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    objectRotation = glm::rotate(objectRotation, glm::radians(obj.rotation.z),
                                 glm::vec3(0.0f, 0.0f, 1.0f));

    for (const auto &particle : ps.particles) {
      if (!particle.alive) continue;
      const float t = std::clamp(
          particle.age / std::max(0.01f, particle.lifetime), 0.0f, 1.0f);
      const float size = ps.sizeOverLifetimeEnabled
                             ? glm::mix(particle.size, ps.sizeOverLifetime, t)
                             : particle.size;
      const float halfSize = std::max(0.001f, size) * 0.5f;
      const float angle = glm::radians(particle.rotation);
      const float c = std::cos(angle);
      const float s = std::sin(angle);
      const glm::vec3 particleWorldCenter =
          obj.position + glm::vec3(objectRotation *
                                   glm::vec4(particle.position.x * obj.scale.x,
                                             particle.position.y * obj.scale.y,
                                             0.0f, 0.0f));
      auto localToWorld = [&](float x, float y) {
        const float rx = x * c - y * s;
        const float ry = x * s + y * c;
        return particleWorldCenter + cameraRight * rx + cameraUp * ry;
      };

      std::array<ImVec2, 4> projected;
      const std::array<glm::vec3, 4> corners = {
          localToWorld(-halfSize, -halfSize),
          localToWorld(halfSize, -halfSize),
          localToWorld(halfSize, halfSize),
          localToWorld(-halfSize, halfSize),
      };
      bool valid = true;
      for (size_t i = 0; i < corners.size(); ++i) {
        if (!ProjectWorldToOverlayPoint(corners[i], view, proj, overlayPos,
                                        overlaySize, projected[i])) {
          valid = false;
          break;
        }
        projected[i].x -= renderLocalOrigin.x;
        projected[i].y -= renderLocalOrigin.y;
      }
      if (!valid) continue;

      ImVec2 rectMin(projected[0].x, projected[0].y);
      ImVec2 rectMax(projected[0].x, projected[0].y);
      for (const ImVec2 &point : projected) {
        rectMin.x = std::min(rectMin.x, point.x);
        rectMin.y = std::min(rectMin.y, point.y);
        rectMax.x = std::max(rectMax.x, point.x);
        rectMax.y = std::max(rectMax.y, point.y);
      }
      if (rectOutsideRenderLocal(rectMin, rectMax)) continue;

      Light2DScreenSprite sprite;
      sprite.objectId = obj.id;
      sprite.layer = obj.layer;
      sprite.drawOrder = drawOrder++;
      sprite.textureId = textureId;
      sprite.tint = ps.colorOverLifetimeEnabled
                        ? glm::mix(particle.startColor, ps.colorOverLifetime, t)
                        : particle.startColor;
      sprite.receiveLighting = ps.receiveLighting2D;
      sprite.unlit = ps.unlitLighting2D;
      sprite.emissiveIntensity = ps.emissiveLighting2D;
      sprite.positions[0] = glm::vec2(projected[0].x, projected[0].y);
      sprite.positions[1] = glm::vec2(projected[1].x, projected[1].y);
      sprite.positions[2] = glm::vec2(projected[2].x, projected[2].y);
      sprite.positions[3] = glm::vec2(projected[3].x, projected[3].y);
      sprite.uvs[0] = glm::vec2(0.0f, 0.0f);
      sprite.uvs[1] = glm::vec2(1.0f, 0.0f);
      sprite.uvs[2] = glm::vec2(1.0f, 1.0f);
      sprite.uvs[3] = glm::vec2(0.0f, 1.0f);
      lightRequest.sprites.push_back(sprite);
    }
  }
}

} // namespace ViewportRenderHelpers
