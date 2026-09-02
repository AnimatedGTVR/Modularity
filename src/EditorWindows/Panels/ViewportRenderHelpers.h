#pragma once

#include "Engine.h"
#include "AssemblageRuntime.h"
#include "Modu2DStats.h"
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

// True when the current window (between Begin/End) is docked but not the
// selected tab. ImGui still returns true from Begin() and runs the whole window
// body in that state — only draw submission is dropped — so scene-rendering
// panels use this to skip their per-frame work entirely.
inline bool IsCurrentDockTabHidden() {
  const ImGuiWindow *window = ImGui::GetCurrentWindow();
  return window != nullptr && window->DockNode != nullptr &&
         !window->DockTabIsVisible;
}

// Particle helpers

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

// Viewport layout

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

// Profiler stats

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

// Texture / sorting

void ApplyNearestTextureSampling(GLuint textureId);

bool RuntimeUiDrawOrderLess(const SceneObject *a, const SceneObject *b);
void StableSortRuntimeUiDrawList(std::vector<SceneObject *> &drawList);

// Assemblage
//
// An Assemblage layer is an ordinary SceneOBJ that draws a chunked grid instead
// of a single sprite. It rides the same sorted draw list as every 2D sprite, so
// a Freeform object placed between two tile layers sorts between them with no
// special casing - see RuntimeUiDrawOrderLess.

bool IsAssemblageLayerDrawable(const SceneObject &obj);

// Nearest ancestor (including the object itself) carrying an Assemblage
// component. Returns nullptr for a layer that has been detached from its root.
const SceneObject *FindAssemblageRootFor(const std::vector<SceneObject> &objects,
                                         const SceneObject &layerObj);

// One tile, already projected to absolute screen space. Deliberately sink
// neutral: the caller pushes it into whichever emitter that viewport is using
// (the Light2D compositor when it is running, the batched ImGui emitter
// otherwise), so this helper does not need to know about either.
struct AssemblageScreenQuad {
  unsigned int textureId = 0;
  ImVec2 pos[4];
  ImVec2 uv[4];
  glm::vec4 tint = glm::vec4(1.0f);
};

// Projects the visible chunks of one layer and calls emit() per tile. Returns
// the number of quads emitted.
//
// Culling is per chunk against the world-space view rect, so an off-screen
// region costs one AABB test rather than one per cell. Chunk geometry itself is
// cached in world space by AssemblageRuntime and rebuilt only when its cells
// change, so a static map costs a transform per visible tile and nothing else.
template <typename WorldToScreenFn, typename EmitFn>
int AppendAssemblageLayerQuads(const std::vector<SceneObject> &sceneObjects,
                               const SceneObject &layerObj,
                               AssemblageRuntime &runtime, Renderer &renderer,
                               double timeSeconds,
                               const glm::vec2 &worldViewMin,
                               const glm::vec2 &worldViewMax,
                               WorldToScreenFn worldToScreen, EmitFn emit) {
  if (!IsAssemblageLayerDrawable(layerObj))
    return 0;

  const SceneObject *root = FindAssemblageRootFor(sceneObjects, layerObj);
  if (root == nullptr || !root->assemblage.enabled)
    return 0;

  const std::string &assetPath = root->assemblage.assetPath;
  if (assetPath.empty())
    return 0;

  AssemblageRuntime::Entry *entry = runtime.acquire(assetPath);
  if (entry == nullptr)
    return 0;

  const Assemblage::Layer *layer =
      entry->asset.findLayer(layerObj.assemblageLayer.layerId);
  if (layer == nullptr || layer->chunks.empty())
    return 0;

  // The layer object's own world transform places the grid. Using position
  // (rather than ui.position) keeps Assemblage on the same footing as Light2D
  // and ShadowCaster2D, which already read the plain transform in 2D scenes.
  const glm::vec2 layerOffset(layerObj.position.x, layerObj.position.y);
  const glm::vec2 localViewMin = worldViewMin - layerOffset;
  const glm::vec2 localViewMax = worldViewMax - layerOffset;

  const float layerAlpha = std::max(0.0f, std::min(1.0f, layerObj.assemblageLayer.opacity));
  if (layerAlpha <= 0.001f)
    return 0;
  const glm::vec4 layerTint = layerObj.assemblageLayer.tint;

  int emitted = 0;
  for (const auto &chunkEntry : layer->chunks) {
    const AssemblageRuntime::ChunkGeometry *geo =
        runtime.chunkGeometry(assetPath, layer->id, chunkEntry.first, renderer);
    if (geo == nullptr || geo->quads.empty())
      continue;

    // Per-chunk cull. One AABB test stands in for up to chunkSize^2 cells.
    if (geo->boundsMax.x < localViewMin.x || geo->boundsMin.x > localViewMax.x ||
        geo->boundsMax.y < localViewMin.y || geo->boundsMin.y > localViewMax.y)
      continue;

    for (const AssemblageRuntime::TileQuad &quad : geo->quads) {
      glm::vec2 uvMin = quad.uvMin;
      glm::vec2 uvMax = quad.uvMax;
      if (quad.animatedTile != Assemblage::kEmptyTile) {
        // Animation is resolved from the tile definition and the clock alone, so
        // every cell sharing this tile lands on the same frame with no per-cell
        // state anywhere in this loop.
        if (const Assemblage::TileDef *tile = entry->tileset.find(quad.animatedTile)) {
          glm::vec2 animMin, animMax;
          if (ResolveTileUv(*tile, quad.sheetWidth, quad.sheetHeight, timeSeconds,
                            animMin, animMax)) {
            uvMin = animMin;
            uvMax = animMax;
            if ((quad.cellFlags & Assemblage::CellFlag_FlipX) != 0)
              std::swap(uvMin.x, uvMax.x);
            if ((quad.cellFlags & Assemblage::CellFlag_FlipY) != 0)
              std::swap(uvMin.y, uvMax.y);
          }
        }
      }

      const glm::vec2 worldMin = quad.worldMin + layerOffset;
      const glm::vec2 worldMax = quad.worldMax + layerOffset;
      const ImVec2 a = worldToScreen(worldMin);
      const ImVec2 b = worldToScreen(worldMax);
      const ImVec2 screenMin(std::min(a.x, b.x), std::min(a.y, b.y));
      const ImVec2 screenMax(std::max(a.x, b.x), std::max(a.y, b.y));

      AssemblageScreenQuad out;
      out.textureId = quad.textureId;
      out.pos[0] = ImVec2(screenMin.x, screenMin.y);
      out.pos[1] = ImVec2(screenMax.x, screenMin.y);
      out.pos[2] = ImVec2(screenMax.x, screenMax.y);
      out.pos[3] = ImVec2(screenMin.x, screenMax.y);
      out.uv[0] = ImVec2(uvMin.x, uvMin.y);
      out.uv[1] = ImVec2(uvMax.x, uvMin.y);
      out.uv[2] = ImVec2(uvMax.x, uvMax.y);
      out.uv[3] = ImVec2(uvMin.x, uvMax.y);
      out.tint = quad.tint * layerTint;
      out.tint.a *= layerAlpha;
      emit(out);
      ++emitted;
    }
  }
  return emitted;
}

// Projection

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

// Frosted backdrop for runtime UI (ui.backdropBlur). Blurs whatever has already been
// drawn behind `obj` inside this viewport and paints it over the element's rect, so the
// element itself still draws sharp on top. `drawList` must already hold every sprite that
// should be blurred - flush any pending sprite batch before calling. No-op when the
// element does not ask for a backdrop or the capture is unavailable (Vulkan / kill switch).
void EmitRuntimeUiBackdropBlur(ImDrawList *drawList, const SceneObject &obj,
                               const ImVec2 &rectMin, const ImVec2 &rectMax);

// UI text rendering

void ApplyUIFontFilterCallback(const ImDrawList *, const ImDrawCmd *cmd);

void AppendWrappedTextSegment(ImFont *font, float fontSize, const char *start,
                              const char *end, float wrapWidth, bool autoWrap,
                              std::vector<std::string> &outLines);

std::vector<std::string> BuildWrappedTextLines(ImFont *font, float fontSize,
                                               const char *text,
                                               float wrapWidth, bool autoWrap);

// Typewriter reveal parameters. revealChars < 0 disables the reveal entirely and
// the line draws in full, which is what every non-scripted label does.
// glyphOffset carries the running glyph index across wrapped lines so the reveal
// head keeps counting through a multi-line label; it is advanced in place.
struct UITextRevealParams {
  float revealChars = -1.0f;
  float popScale = 1.0f;
  float softness = 1.0f;
  bool active() const { return revealChars >= 0.0f; }
};

void DrawUITextLineWithEffects(ImDrawList *drawList, ImFont *font,
                               float fontSize, const ImVec2 &linePos,
                               ImU32 baseColor, const char *lineText,
                               int effectFlags, float effectSpeed,
                               float effectIntensity,
                               const UITextRevealParams &reveal = {},
                               int *glyphOffset = nullptr);

// stableRasterFontSize keeps viewport zoom from rebuilding glyphs at every
// rounded pixel size; the generated text vertices are scaled to fontSize.
void AddUITextWithFilter(ImDrawList *drawList,
                         MaterialProperties::TextureFilter filter, ImFont *font,
                         float fontSize, const ImVec2 &drawMin,
                         const ImVec2 &drawMax, ImU32 color, const char *text,
                         bool autoWrap, UITextHAlign hAlign,
                         UITextVAlign vAlign, int effectFlags,
                         float effectSpeed, float effectIntensity,
                         float stableRasterFontSize = 0.0f,
                         const UITextRevealParams &reveal = {});

// non-ImGui slider styles (Fill / Vertical / Stepped / Circle / Ring). the Circle fan-fill
// works at every t ( the old PathFillConvex version broke past 180 degrees and at t=0 ).
void RenderUISliderStyle(ImDrawList *dl, UISliderStyle style,
                         const ImVec2 &drawMin, const ImVec2 &drawMax,
                         const ImVec2 &drawSize,
                         ImU32 bg, ImU32 fillColor, ImU32 border, ImU32 textColor,
                         float t, float minValue, float maxValue,
                         const char *label);

// rotate the vertices in a draw-list range around pivot. use right after adding
// text/glyphs so only the snapshotted range rotates.
void RotateDrawListVertices(ImDrawList *dl, int firstVertex, int lastVertex,
                            const ImVec2 &pivot, float angleRadians);

void ScaleDrawListVertices(ImDrawList *dl, int firstVertex, int lastVertex,
                           const ImVec2 &pivot, const ImVec2 &scale);

// viewportRenderScale = on-screen size / render resolution (1.5 = upscaled 1.5x).
// screen-space UI text multiplies font size by it so text tracks the upscale instead of
// staying fixed-pixel. defaults to 1 for old call sites.
float ComputeViewportTextFontSize(float baseFontSize, float textScale,
                                  bool useWorldUi, float worldUiZoom,
                                  float viewportRenderScale = 1.0f);

float ResolveViewportUITextAuthoredFontSize(float baseFontSize,
                                            float textScale,
                                            float explicitFontSize);

float ResolveViewportUITextFontSize(float baseFontSize, float textScale,
                                    float explicitFontSize, bool useWorldUi,
                                    float worldUiZoom,
                                    float viewportRenderScale = 1.0f,
                                    float elementScaleY = 1.0f);

void ApplyUITextScaleFromRectResize(SceneObject &obj, float startTextScale,
                                    float startFontSize,
                                    const ImVec2 &startScreenSize,
                                    const ImVec2 &newScreenSize);

// Rotate a 2D vector by an angle in radians (screen/UI space, +angle = the same
// winding the per-element image/text rotation uses).
inline glm::vec2 RotateUiVec2(const glm::vec2 &v, float angleRad) {
  if (std::abs(angleRad) < 1e-6f) return v;
  const float c = std::cos(angleRad);
  const float s = std::sin(angleRad);
  return glm::vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

// Offset of the anchor point inside a size-sized box. Matches the anchorToPivot
// lambdas the viewports use for placement.
inline ImVec2 UiAnchorPivot(UIAnchor anchor, const ImVec2 &size) {
  switch (anchor) {
  case UIAnchor::TopLeft:
    return ImVec2(0.0f, 0.0f);
  case UIAnchor::TopRight:
    return ImVec2(size.x, 0.0f);
  case UIAnchor::BottomLeft:
    return ImVec2(0.0f, size.y);
  case UIAnchor::BottomRight:
    return ImVec2(size.x, size.y);
  default:
    return ImVec2(size.x * 0.5f, size.y * 0.5f);
  }
}

// Corners of an element rotated about its ANCHOR rather than its centre.
// ui.position pins the anchor, so that is the point users expect to stay put
// when they spin something -- anchor Bottom Left should hinge like a jaw.
//
// center/half keep each call site's existing meaning, and for UIAnchor::Center
// the pivot lands exactly on the centre, so centred elements are bit-identical
// to the previous behaviour. Only non-centred anchors change.
inline void UiRotatedCornersAboutAnchor(UIAnchor anchor, const ImVec2 &center,
                                        const ImVec2 &half, float angleRad,
                                        ImVec2 &p0, ImVec2 &p1, ImVec2 &p2,
                                        ImVec2 &p3) {
  const ImVec2 pivot = UiAnchorPivot(anchor, ImVec2(half.x * 2.0f, half.y * 2.0f));
  // Pivot relative to the centre the corner offsets are measured from.
  const float px = pivot.x - half.x;
  const float py = pivot.y - half.y;
  const float c = std::cos(angleRad);
  const float s = std::sin(angleRad);
  auto rot = [&](float x, float y) {
    const float dx = x - px;
    const float dy = y - py;
    return ImVec2(center.x + px + dx * c - dy * s,
                  center.y + py + dx * s + dy * c);
  };
  p0 = rot(-half.x, -half.y);
  p1 = rot(half.x, -half.y);
  p2 = rot(half.x, half.y);
  p3 = rot(-half.x, half.y);
}

// Scene lookup cache

struct UiSceneLookupCache {
  explicit UiSceneLookupCache(const std::vector<SceneObject> &objects) {
    byId.reserve(objects.size());
    parentOffsetCache.reserve(objects.size());
    parentRotationCache.reserve(objects.size());
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

  // World-space pivot of obj's PARENT (accumulated up the chain). Rotation-aware:
  // each ancestor's local offset is rotated by everything above it, so a rotated
  // parent orbits its descendants around the parent's pivot. Callers add the
  // object's own (rotated) ui.position on top; see getWorldParentRotationRad.
  glm::vec2 getWorldParentOffset(const SceneObject &obj) {
    auto cached = parentOffsetCache.find(obj.id);
    if (cached != parentOffsetCache.end()) {
      return cached->second;
    }

    glm::vec2 offset(0.0f);
    if (const SceneObject *parent = find(obj.parentId)) {
      const glm::vec2 grandOffset = getWorldParentOffset(*parent);
      const float grandRotRad = getWorldParentRotationRad(*parent);
      glm::vec2 parentLocal;
      if (parent->type == ObjectType::Sprite25D) {
        parentLocal = glm::vec2(parent->position.x, parent->position.y);
      } else if (parent->hasUI && parent->ui.type != UIElementType::None) {
        parentLocal = glm::vec2(parent->ui.position.x, parent->ui.position.y);
      } else {
        parentLocal = glm::vec2(parent->position.x, parent->position.y);
      }
      offset = grandOffset + RotateUiVec2(parentLocal, grandRotRad);
    }

    parentOffsetCache.emplace(obj.id, offset);
    return offset;
  }

  // Accumulated UI rotation (radians) contributed by the parent chain, NOT
  // including obj's own rotation. Only UI ancestors contribute (via ui.rotation),
  // matching how getWorldParentOffset only accumulates ancestors' 2D positions;
  // non-UI ancestors (Empty/Sprite) contribute no 2D rotation.
  float getWorldParentRotationRad(const SceneObject &obj) {
    auto cached = parentRotationCache.find(obj.id);
    if (cached != parentRotationCache.end()) {
      return cached->second;
    }

    float rot = 0.0f;
    if (const SceneObject *parent = find(obj.parentId)) {
      rot = getWorldParentRotationRad(*parent);
      if (parent->hasUI && parent->ui.type != UIElementType::None) {
        rot += glm::radians(parent->ui.rotation);
      }
    }

    parentRotationCache.emplace(obj.id, rot);
    return rot;
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
  std::unordered_map<int, float> parentRotationCache;
  std::unordered_map<int, int> canvas3DIdCache;
  std::unordered_map<int, int> pseudoCanvasIdCache;
};

// Sprite texture resolver

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

// Batched sprite emitter

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
    Modu2DStats::CountSpriteQuads(1);
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

    Modu2DStats::CountSpriteBatch();
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

// Sprite frame / nine-slice

ImVec2 ResolveUiSourceFrameSizePx(const SceneObject &obj, int frame,
                                  const Texture *texture);

bool DrawNineSliceSprite(BatchedSpriteEmitter &spriteBatch,
                         ImTextureID textureId, const SceneObject &obj,
                         const ImVec2 &drawMin, const ImVec2 &drawMax,
                         const std::array<ImVec2, 4> &uvQuad,
                         const ImVec2 &sourceFrameSizePx, float angleRad,
                         ImU32 color);

// Separate draw path for material.uvScrollEnabled sprites. Returns false when
// scrolling is off, so callers fall through to the normal path and the static
// and sprite-sheet rendering is left exactly as it was.
//
// Textures are created GL_CLAMP_TO_EDGE, so offsetting UVs past 0..1 would smear
// the edge instead of tiling. This splits the element at the UV seam and emits
// up to four quads, each sampling strictly inside 0..1, which loops seamlessly
// without touching the texture cache or its wrap mode.
bool DrawScrollingUiSprite(BatchedSpriteEmitter &spriteBatch,
                           ImTextureID textureId, const SceneObject &obj,
                           const ImVec2 &drawMin, const ImVec2 &drawMax,
                           float angleRad, ImU32 color, double timeSeconds);

// Pseudo-3D canvas helpers

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

// project a transform-driven canvas quad to screen space (unit quad in local XY, scaled by
// canvas.scale.xy, placed by position/rotation). corners come out pseudo-3D order
// (TL,TR,BR,BL) matching BuildPseudo3DPanelCorners. false if any corner sits behind the
// near plane (caller should skip rendering).
bool ProjectTransformDrivenCanvasCorners(const SceneObject &canvas,
                                         const glm::mat4 &view,
                                         const glm::mat4 &proj,
                                         const ImVec2 &overlayPos,
                                         const ImVec2 &overlaySize,
                                         std::array<ImVec2, 4> &outCorners);

void ResolvePseudo3DDistanceState(const UIElementComponent &ui, float distance,
                                  float &outScale, float &outPerspectiveFactor,
                                  bool &outAllowInteraction);

// AppendProjectedParticleSystem2DSprites implementation

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
