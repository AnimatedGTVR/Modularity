#include "Engine.h"
#include "ViewportRenderHelpers.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

using namespace ViewportRenderHelpers;

#pragma region 3D UI Canvas Targets
void Engine::renderUiCanvas3DTargets() {
  if (!rendererInitialized || !projectManager.currentProject.isLoaded)
    return;

  ImGuiContext *mainContext = ImGui::GetCurrentContext();
  if (!mainContext)
    return;

  ImGuiStyle mainStyle = ImGui::GetStyle();
  ImGuiIO &mainIo = ImGui::GetIO();
  refreshSceneObjectIndexCache();

  auto findSceneObject = [&](int id) -> SceneObject * {
    auto it = sceneObjectIndexById.find(id);
    if (it == sceneObjectIndexById.end() || it->second >= sceneObjects.size()) {
      return nullptr;
    }
    return &sceneObjects[it->second];
  };

  auto isUiType = [](const SceneObject &target) {
    return target.hasUI && target.ui.type != UIElementType::None;
  };

  for (auto &obj : sceneObjects) {
    if (obj.hasUI && obj.ui.type == UIElementType::Canvas &&
        !obj.ui.renderIn3D) {
      if (obj.hasRenderer && obj.renderType == RenderType::Sprite) {
        obj.hasRenderer = false;
        obj.renderType = RenderType::None;
      }
    }
  }

  auto findCanvasRoot = [&](const SceneObject &obj) -> const SceneObject * {
    const SceneObject *current = &obj;
    const SceneObject *found = nullptr;
    while (current) {
      if (current->hasUI && current->ui.type == UIElementType::Canvas) {
        found = current;
      }
      if (current->parentId < 0)
        break;
      current = findSceneObject(current->parentId);
      if (!current)
        break;
    }
    return found;
  };

  auto isOffscreenCanvas = [](const SceneObject &canvas) {
    return canvas.enabled && canvas.hasUI &&
           canvas.ui.type == UIElementType::Canvas &&
           (canvas.ui.renderIn3D || (canvas.ui.pseudo3DEnabled &&
                                     canvas.ui.pseudo3DUseOffscreenSurface));
  };

  std::unordered_set<int> activeCanvasIds;
  for (auto &canvas : sceneObjects) {
    if (!isOffscreenCanvas(canvas))
      continue;
    activeCanvasIds.insert(canvas.id);

    if (canvas.ui.renderIn3D) {
      canvas.hasRenderer = true;
      canvas.renderType = RenderType::Sprite;
      canvas.material.textureMix = 1.0f;
    } else if (canvas.hasRenderer && canvas.renderType == RenderType::Sprite) {
      canvas.hasRenderer = false;
      canvas.renderType = RenderType::None;
    }
    if (canvas.ui.pseudo3DEnabled && !canvas.ui.renderIn3D) {
      canvas.faceCamera = false;
    }

    const glm::vec2 pseudoLayout = ResolvePseudo3DLayoutSize(canvas);
    const bool pseudoCanvas =
        !canvas.ui.renderIn3D && canvas.ui.pseudo3DEnabled;
    const float layoutWidth =
        pseudoCanvas ? pseudoLayout.x : std::max(1.0f, canvas.ui.size.x);
    const float layoutHeight =
        pseudoCanvas ? pseudoLayout.y : std::max(1.0f, canvas.ui.size.y);
    int targetWidth = (canvas.ui.renderTargetSize.x > 0)
                          ? canvas.ui.renderTargetSize.x
                          : static_cast<int>(layoutWidth);
    int targetHeight = (canvas.ui.renderTargetSize.y > 0)
                           ? canvas.ui.renderTargetSize.y
                           : static_cast<int>(layoutHeight);
    targetWidth = std::clamp(targetWidth, 16, 4096);
    targetHeight = std::clamp(targetHeight, 16, 4096);

    Renderer::UiTargetInfo target =
        renderer.ensureUiTarget(canvas.id, targetWidth, targetHeight);
    if (target.fbo == 0 || target.texture == 0)
      continue;

    UiCanvas3DContext &ctxEntry = uiCanvas3DContexts[canvas.id];
    if (!ctxEntry.context) {
      ctxEntry.context = ImGui::CreateContext();
      ImGui::SetCurrentContext(ctxEntry.context);
      ImGuiIO &io = ImGui::GetIO();
      preloadUIFontCatalogForContext(ctxEntry.context);
      ImFont *canvasFont = getUIFontForContext(uiEditorFontAsset, ctxEntry.context);
      if (!canvasFont) {
        canvasFont = getUIFontForContext("builtin:imgui-default", ctxEntry.context);
      }
      if (canvasFont) {
        io.FontDefault = canvasFont;
      }
      ImGui_ImplOpenGL3_Init("#version 330");
      ctxEntry.backendReady = true;
    }

    ImGui::SetCurrentContext(ctxEntry.context);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(layoutWidth, layoutHeight);
    io.DisplayFramebufferScale = ImVec2(
        (layoutWidth > 0.0f) ? (static_cast<float>(targetWidth) / layoutWidth)
                             : 1.0f,
        (layoutHeight > 0.0f)
            ? (static_cast<float>(targetHeight) / layoutHeight)
            : 1.0f);
    io.DeltaTime =
        (mainIo.DeltaTime > 0.0f) ? mainIo.DeltaTime : (1.0f / 60.0f);
    auto inputIt = uiCanvas3DInputs.find(canvas.id);
    if (inputIt != uiCanvas3DInputs.end() && inputIt->second.hasInput) {
      io.MousePos = inputIt->second.mousePos;
      io.MouseDown[0] = inputIt->second.mouseDown[0];
      io.MouseDown[1] = inputIt->second.mouseDown[1];
      io.MouseDown[2] = inputIt->second.mouseDown[2];
      io.MouseWheel = inputIt->second.mouseWheel;
    } else {
      io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
      io.MouseDown[0] = false;
      io.MouseDown[1] = false;
      io.MouseDown[2] = false;
      io.MouseWheel = 0.0f;
    }

    ImGui::GetStyle() = mainStyle;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(layoutWidth, layoutHeight));
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##Canvas3D", nullptr, flags);

    auto anchorToPivot = [](UIAnchor anchor, const ImVec2 &size) {
      switch (anchor) {
      case UIAnchor::Center:
        return ImVec2(size.x * 0.5f, size.y * 0.5f);
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
    };
    auto anchorToPoint = [](UIAnchor anchor, const ImVec2 &min,
                            const ImVec2 &max) {
      switch (anchor) {
      case UIAnchor::Center:
        return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
      case UIAnchor::TopLeft:
        return min;
      case UIAnchor::TopRight:
        return ImVec2(max.x, min.y);
      case UIAnchor::BottomLeft:
        return ImVec2(min.x, max.y);
      case UIAnchor::BottomRight:
        return max;
      default:
        return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
      }
    };
    auto resolveUIRect = [&](const SceneObject &obj, ImVec2 &outMin,
                             ImVec2 &outMax) {
      std::vector<const SceneObject *> chain;
      const SceneObject *current = &obj;
      while (current) {
        if (isUiType(*current) && current->id != canvas.id) {
          chain.push_back(current);
        }
        if (current->parentId < 0)
          break;
        current = findSceneObject(current->parentId);
        if (!current)
          break;
        if (current->id == canvas.id)
          break;
      }
      std::reverse(chain.begin(), chain.end());

      ImVec2 regionMin = ImGui::GetWindowPos();
      ImVec2 regionMax =
          ImVec2(regionMin.x + layoutWidth, regionMin.y + layoutHeight);
      for (const SceneObject *node : chain) {
        glm::vec2 nodeSize = getSpriteDisplaySize(*node);
        ImVec2 size =
            ImVec2(std::max(1.0f, nodeSize.x), std::max(1.0f, nodeSize.y));
        ImVec2 anchorPoint =
            anchorToPoint(node->ui.anchor, regionMin, regionMax);
        ImVec2 pivot(anchorPoint.x + node->ui.position.x,
                     anchorPoint.y + node->ui.position.y);
        ImVec2 pivotOffset = anchorToPivot(node->ui.anchor, size);
        regionMin = ImVec2(pivot.x - pivotOffset.x, pivot.y - pivotOffset.y);
        regionMax = ImVec2(regionMin.x + size.x, regionMin.y + size.y);
      }
      outMin = regionMin;
      outMax = regionMax;
    };

    auto brighten = [](const ImVec4 &c, float k) {
      return ImVec4(std::clamp(c.x * k, 0.0f, 1.0f),
                    std::clamp(c.y * k, 0.0f, 1.0f),
                    std::clamp(c.z * k, 0.0f, 1.0f), c.w);
    };
    float animSpeed = 0.0f;
    if (uiAnimationMode == UIAnimationMode::Fluid) {
      animSpeed = 8.0f;
    } else if (uiAnimationMode == UIAnimationMode::Snappy) {
      animSpeed = 18.0f;
    }
    float animStep =
        (uiAnimationMode == UIAnimationMode::Off)
            ? 1.0f
            : (1.0f - std::exp(-animSpeed * ImGui::GetIO().DeltaTime));
    auto animateValue = [&](float &current, float target, bool immediate) {
      if (uiAnimationMode == UIAnimationMode::Off || immediate) {
        current = target;
      } else {
        current += (target - current) * animStep;
      }
      return current;
    };
    BatchedSpriteEmitter spriteBatch(ImGui::GetWindowDrawList());
    auto resolveCanvasMaskRectForObject =
        [&](const SceneObject &obj, ImVec2 &outMin, ImVec2 &outMax) -> bool {
      bool hasMask = false;
      ImVec2 maskMin(0.0f, 0.0f);
      ImVec2 maskMax(0.0f, 0.0f);
      const SceneObject *current = &obj;
      while (current && current->parentId >= 0) {
        current = findSceneObject(current->parentId);
        if (!current)
          break;
        if (!(current->hasUI && current->ui.type == UIElementType::Canvas &&
              current->ui.maskChildren)) {
          continue;
        }

        ImVec2 canvasMin, canvasMax;
        resolveUIRect(*current, canvasMin, canvasMax);
        if (!hasMask) {
          maskMin = canvasMin;
          maskMax = canvasMax;
          hasMask = true;
        } else {
          maskMin.x = std::max(maskMin.x, canvasMin.x);
          maskMin.y = std::max(maskMin.y, canvasMin.y);
          maskMax.x = std::min(maskMax.x, canvasMax.x);
          maskMax.y = std::min(maskMax.y, canvasMax.y);
        }
      }
      if (!hasMask)
        return false;
      outMin = maskMin;
      outMax = maskMax;
      return (outMax.x > outMin.x) && (outMax.y > outMin.y);
    };

    for (auto &obj : sceneObjects) {
      if (!IsObjectEnabledInHierarchy(obj) || !isUiType(obj))
        continue;
      const SceneObject *root = findCanvasRoot(obj);
      if (!root || root->id != canvas.id)
        continue;
      if (obj.ui.type == UIElementType::Canvas)
        continue;

      ImVec2 rectMin, rectMax;
      resolveUIRect(obj, rectMin, rectMax);
      ImVec2 rectSize(rectMax.x - rectMin.x, rectMax.y - rectMin.y);
      if (rectSize.x <= 1.0f || rectSize.y <= 1.0f)
        continue;

      ImGuiStyle savedStyle;
      bool styleApplied = false;
      bool fontApplied = false;
      if (!obj.ui.stylePreset.empty()) {
        if (const auto *preset = getUIStylePreset(obj.ui.stylePreset)) {
          savedStyle = ImGui::GetStyle();
          ImGui::GetStyle() = preset->style;
          styleApplied = true;
          if (ImFont *presetFont =
                  getUIFontForContext(preset->fontAsset, ImGui::GetCurrentContext())) {
            ImGui::PushFont(presetFont, preset->style.FontSizeBase);
            fontApplied = true;
          }
        }
      }

      ImVec2 drawMin = rectMin;
      ImVec2 drawMax = rectMax;
      ImVec2 drawSize(drawMax.x - drawMin.x, drawMax.y - drawMin.y);
      ImVec2 localMin(drawMin.x - ImGui::GetWindowPos().x,
                      drawMin.y - ImGui::GetWindowPos().y);
      bool pushedCanvasMask = false;
      if (obj.ui.type != UIElementType::Canvas) {
        ImVec2 maskMin, maskMax;
        if (resolveCanvasMaskRectForObject(obj, maskMin, maskMax)) {
          maskMin.x = std::max(maskMin.x, ImGui::GetWindowPos().x);
          maskMin.y = std::max(maskMin.y, ImGui::GetWindowPos().y);
          maskMax.x =
              std::min(maskMax.x, ImGui::GetWindowPos().x + layoutWidth);
          maskMax.y =
              std::min(maskMax.y, ImGui::GetWindowPos().y + layoutHeight);
          if (maskMax.x <= maskMin.x || maskMax.y <= maskMin.y) {
            if (fontApplied)
              ImGui::PopFont();
            if (styleApplied)
              ImGui::GetStyle() = savedStyle;
            continue;
          }
          if (drawMax.x <= maskMin.x || drawMin.x >= maskMax.x ||
              drawMax.y <= maskMin.y || drawMin.y >= maskMax.y) {
            if (fontApplied)
              ImGui::PopFont();
            if (styleApplied)
              ImGui::GetStyle() = savedStyle;
            continue;
          }
          spriteBatch.flush();
          ImGui::PushClipRect(maskMin, maskMax, true);
          pushedCanvasMask = true;
        }
      }

      ImGui::PushID(obj.id);
      UIAnimationState &animState = uiAnimationStates[obj.id];
      if (!animState.initialized) {
        animState.sliderValue = obj.ui.sliderValue;
        animState.initialized = true;
      }
      if (obj.ui.type == UIElementType::Image ||
          obj.ui.type == UIElementType::Sprite2D) {
        Texture *spriteTex = nullptr;
        unsigned int texId = 0;
        if (rendererInitialized && !obj.albedoTexturePath.empty()) {
          spriteTex = renderer.getTexture(
              obj.albedoTexturePath, MaterialProperties::TextureFilter::Point);
          if (spriteTex != nullptr) {
            texId = spriteTex->GetID();
          }
        }
        std::array<ImVec2, 4> uvQuad = buildSpriteSheetUvs(obj);
        const int frame = resolveSpriteSheetFrame(obj);
        const ImVec2 sourceFrameSizePx =
            ResolveUiSourceFrameSizePx(obj, frame, spriteTex);
        ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b,
                    obj.ui.color.a);
        const ImU32 tintColor = ImGui::GetColorU32(tint);
        float angle = glm::radians(obj.ui.rotation);
        if (DrawNineSliceSprite(spriteBatch, (ImTextureID)(intptr_t)texId, obj,
                                drawMin, drawMax, uvQuad, sourceFrameSizePx,
                                angle, tintColor)) {
        } else if (std::abs(angle) > 1e-4f) {
          ImDrawList *dl = ImGui::GetWindowDrawList();
          ImVec2 center = ImVec2((drawMin.x + drawMax.x) * 0.5f,
                                 (drawMin.y + drawMax.y) * 0.5f);
          ImVec2 half = ImVec2(drawSize.x * 0.5f, drawSize.y * 0.5f);
          float c = std::cos(angle);
          float s = std::sin(angle);
          auto rotPt = [&](float x, float y) {
            return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
          };
          ImVec2 p0 = rotPt(-half.x, -half.y);
          ImVec2 p1 = rotPt(half.x, -half.y);
          ImVec2 p2 = rotPt(half.x, half.y);
          ImVec2 p3 = rotPt(-half.x, half.y);
          if (texId != 0) {
            spriteBatch.push((ImTextureID)(intptr_t)texId, p0, p1, p2, p3,
                             uvQuad[0], uvQuad[1], uvQuad[2], uvQuad[3],
                             tintColor);
          } else {
            spriteBatch.flush();
            ImU32 fill = tintColor;
            ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
            dl->AddQuadFilled(p0, p1, p2, p3, fill);
            dl->AddQuad(p0, p1, p2, p3, border, 2.0f);
            ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
            ImVec2 textPos(center.x - textSize.x * 0.5f,
                           center.y - textSize.y * 0.5f);
            dl->AddText(textPos, IM_COL32(210, 210, 220, 220),
                        obj.ui.label.c_str());
          }
        } else {
          ImDrawList *dl = ImGui::GetWindowDrawList();
          if (texId != 0) {
            spriteBatch.push((ImTextureID)(intptr_t)texId, drawMin,
                             ImVec2(drawMax.x, drawMin.y), drawMax,
                             ImVec2(drawMin.x, drawMax.y), uvQuad[0],
                             ImVec2(uvQuad[2].x, uvQuad[0].y), uvQuad[2],
                             ImVec2(uvQuad[0].x, uvQuad[2].y), tintColor);
          } else {
            spriteBatch.flush();
            ImU32 fill = tintColor;
            ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
            dl->AddRectFilled(drawMin, drawMax, fill, 6.0f);
            dl->AddRect(drawMin, drawMax, border, 6.0f);
            ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
            ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                           drawMin.y + (drawSize.y - textSize.y) * 0.5f);
            dl->AddText(textPos, IM_COL32(210, 210, 220, 220),
                        obj.ui.label.c_str());
          }
        }
      } else if (obj.ui.type == UIElementType::Slider) {
        spriteBatch.flush();
        ImGui::SetCursorPos(localMin);
        ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b,
                    obj.ui.color.a);
        const ImU32 s3SliderBg     = (obj.ui.backgroundColor.a > 0.0f) ? ImGui::GetColorU32(ImVec4(obj.ui.backgroundColor.r, obj.ui.backgroundColor.g, obj.ui.backgroundColor.b, obj.ui.backgroundColor.a)) : ImGui::GetColorU32(ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
        const ImU32 s3SliderFill   = (obj.ui.fillColor.a > 0.0f)       ? ImGui::GetColorU32(ImVec4(obj.ui.fillColor.r, obj.ui.fillColor.g, obj.ui.fillColor.b, obj.ui.fillColor.a))             : ImGui::GetColorU32(tint);
        const ImU32 s3SliderBorder = (obj.ui.borderColor.a > 0.0f)     ? ImGui::GetColorU32(ImVec4(obj.ui.borderColor.r, obj.ui.borderColor.g, obj.ui.borderColor.b, obj.ui.borderColor.a))     : ImGui::GetColorU32(brighten(tint, 0.85f));
        const ImU32 s3SliderText   = (obj.ui.textColor.a > 0.0f)       ? ImGui::GetColorU32(ImVec4(obj.ui.textColor.r, obj.ui.textColor.g, obj.ui.textColor.b, obj.ui.textColor.a))             : IM_COL32(240, 240, 245, 220);
        if (obj.ui.sliderStyle == UISliderStyle::ImGui) {
          ImGui::PushItemWidth(drawSize.x);
          ImGui::BeginDisabled(!obj.ui.interactable);
          ImGui::PushStyleColor(ImGuiCol_FrameBg,      ImGui::ColorConvertU32ToFloat4(s3SliderBg));
          ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, brighten(tint, 0.5f));
          ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  brighten(tint, 0.7f));
          ImGui::PushStyleColor(ImGuiCol_SliderGrab,     brighten(tint, 0.9f));
          ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, brighten(tint, 1.1f));
          ImGui::SliderFloat(obj.ui.label.c_str(), &obj.ui.sliderValue,
                             obj.ui.sliderMin, obj.ui.sliderMax);
          ImGui::PopStyleColor(5);
          ImGui::EndDisabled();
          ImGui::PopItemWidth();
        } else {
          ImDrawList *dl = ImGui::GetWindowDrawList();
          ImU32 bg     = s3SliderBg;
          ImU32 fill   = s3SliderFill;
          ImU32 border = s3SliderBorder;
          float minValue = obj.ui.sliderMin;
          float maxValue = obj.ui.sliderMax;
          float range = (maxValue - minValue);
          if (range <= 1e-6f)
            range = 1.0f;
          ImGui::BeginDisabled(!obj.ui.interactable);
          ImGui::InvisibleButton("##UISlider", drawSize);
          bool held = obj.ui.interactable && ImGui::IsItemActive();
          if (held && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
              drawSize.x > 1.0f) {
            float mouseT = (ImGui::GetIO().MousePos.x - drawMin.x) / drawSize.x;
            mouseT = std::clamp(mouseT, 0.0f, 1.0f);
            float newValue = minValue + mouseT * range;
            obj.ui.sliderValue = newValue;
          }
          ImGui::EndDisabled();

          animateValue(animState.sliderValue, obj.ui.sliderValue, held);
          float displayValue = (uiAnimationMode == UIAnimationMode::Off)
                                   ? obj.ui.sliderValue
                                   : animState.sliderValue;
          float t = (displayValue - minValue) / range;
          t = std::clamp(t, 0.0f, 1.0f);

          ViewportRenderHelpers::RenderUISliderStyle(
              dl, obj.ui.sliderStyle, drawMin, drawMax, drawSize, bg, fill,
              border, s3SliderText, t, minValue, maxValue,
              obj.ui.label.c_str());
        }
      } else if (obj.ui.type == UIElementType::Button) {
        spriteBatch.flush();
        ImGui::SetCursorPos(localMin);
        ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b,
                    obj.ui.color.a);
        obj.ui.buttonPressed = false;
        obj.ui.uiHovered = false;
        obj.ui.uiActive = false;
        const ImU32 s3BtnText = (obj.ui.textColor.a > 0.0f) ? ImGui::GetColorU32(ImVec4(obj.ui.textColor.r, obj.ui.textColor.g, obj.ui.textColor.b, obj.ui.textColor.a)) : IM_COL32(240, 240, 245, 220);
        if (obj.ui.buttonStyle == UIButtonStyle::ImGui) {
          ImGui::PushStyleColor(ImGuiCol_Button, tint);
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brighten(tint, 1.1f));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive, brighten(tint, 1.2f));
          ImGui::BeginDisabled(!obj.ui.interactable);
          obj.ui.buttonPressed = ImGui::Button(obj.ui.label.c_str(), drawSize);
          obj.ui.uiHovered = ImGui::IsItemHovered();
          obj.ui.uiActive  = ImGui::IsItemActive();
          ImGui::EndDisabled();
          ImGui::PopStyleColor(3);
        } else if (obj.ui.buttonStyle == UIButtonStyle::Outline) {
          ImDrawList *dl = ImGui::GetWindowDrawList();
          ImU32 border = (obj.ui.borderColor.a > 0.0f) ? ImGui::GetColorU32(ImVec4(obj.ui.borderColor.r, obj.ui.borderColor.g, obj.ui.borderColor.b, obj.ui.borderColor.a)) : ImGui::GetColorU32(tint);
          ImGui::BeginDisabled(!obj.ui.interactable);
          if (ImGui::InvisibleButton("##UIButton", drawSize)) {
            obj.ui.buttonPressed = obj.ui.interactable;
          }
          bool hovered = ImGui::IsItemHovered();
          bool active = ImGui::IsItemActive();
          obj.ui.uiHovered = hovered;
          obj.ui.uiActive  = active;
          ImGui::EndDisabled();
          float hoverT =
              animateValue(animState.hover, hovered ? 1.0f : 0.0f, false);
          float activeT =
              animateValue(animState.active, active ? 1.0f : 0.0f, false);
          if (hoverT > 0.001f) {
            ImVec4 hoverCol = brighten(tint, 0.45f);
            hoverCol.w *= std::clamp(hoverT, 0.0f, 1.0f);
            dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(hoverCol),
                              6.0f);
          }
          if (activeT > 0.001f) {
            ImVec4 activeCol = brighten(tint, 0.65f);
            activeCol.w *= std::clamp(activeT, 0.0f, 1.0f);
            dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(activeCol),
                              6.0f);
          }
          dl->AddRect(drawMin, drawMax, border, 6.0f, 0, 2.0f);
          ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
          ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                         drawMin.y + (drawSize.y - textSize.y) * 0.5f);
          dl->AddText(textPos, s3BtnText, obj.ui.label.c_str());
        }
      } else if (obj.ui.type == UIElementType::Text) {
        spriteBatch.flush();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b,
                    obj.ui.color.a);
        ImFont *textFont =
            obj.ui.textFont.empty()
                ? ImGui::GetFont()
                : getUIFontForContext(obj.ui.textFont, ImGui::GetCurrentContext());
        if (!textFont) {
          textFont = ImGui::GetFont();
        }
        float scale = std::max(0.1f, obj.ui.textScale);
        float fontSize = std::max(1.0f, ImGui::GetFontSize() * scale);
        ImGui::PushClipRect(drawMin, drawMax, true);
        AddUITextWithFilter(dl, obj.material.textureFilter, textFont,
                            fontSize, drawMin, drawMax,
                            ImGui::GetColorU32(tint), obj.ui.label.c_str(),
                            obj.ui.textAutoWrap, obj.ui.textHAlign,
                            obj.ui.textVAlign, obj.ui.textEffectFlags,
                            obj.ui.textEffectSpeed, obj.ui.textEffectIntensity);
        ImGui::PopClipRect();
      }
      if (pushedCanvasMask) {
        spriteBatch.flush();
        ImGui::PopClipRect();
      }
      ImGui::PopID();
      if (fontApplied)
        ImGui::PopFont();
      if (styleApplied)
        ImGui::GetStyle() = savedStyle;
    }
    spriteBatch.flush();

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::Render();

    GLint prevFbo = 0;
    GLint prevViewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.width, target.height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2],
               prevViewport[3]);
  }

  for (auto it = uiCanvas3DContexts.begin(); it != uiCanvas3DContexts.end();) {
    if (activeCanvasIds.find(it->first) == activeCanvasIds.end()) {
      if (it->second.context) {
        ImGui::SetCurrentContext(it->second.context);
        if (it->second.backendReady) {
          ImGui_ImplOpenGL3_Shutdown();
        }
        ImGui::DestroyContext(it->second.context);
      }
      it = uiCanvas3DContexts.erase(it);
    } else {
      ++it;
    }
  }
  renderer.cleanupUiTargets(activeCanvasIds);

  ImGui::SetCurrentContext(mainContext);
}
#pragma endregion
