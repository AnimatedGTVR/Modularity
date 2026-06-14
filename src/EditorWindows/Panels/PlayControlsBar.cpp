#include "Engine.h"
#include <chrono>
#include <cstdio>

void Engine::renderPlayControlsBar() {
  const EditorChromeMetrics &chrome = getEditorChromeMetrics(uiChromeScale);
  const char *playTooltip = isPlaying ? "Stop Play Mode" : "Play Mode";
  const char *specTooltip = specMode ? "Disable Spec Mode" : "Spec Mode";
  const char *pauseTooltip = isPaused ? "Resume" : "Pause";
  const bool hasVulkanSceneTexture =
      usingVulkan() && vulkanRendererInitialized && (vulkanRenderer != nullptr);
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

  struct ToolbarIcon {
    ImTextureID id = static_cast<ImTextureID>(0);
    bool flipY = false;
  };

  auto resolveToolbarIcon = [&](const char *iconPath) -> ToolbarIcon {
    if (!iconPath || !*iconPath) {
      return {};
    }
    if (rendererInitialized) {
      if (Texture *icon = renderer.getTexture(
              iconPath, MaterialProperties::TextureFilter::Bilinear);
          icon && icon->GetID()) {
        return {static_cast<ImTextureID>(icon->GetID()), true};
      }
      return {};
    }
    if (hasVulkanSceneTexture && vulkanRenderer) {
      ImTextureID vkIcon = vulkanRenderer->getOrCreateUIImage(iconPath);
      if (vkIcon != static_cast<ImTextureID>(0)) {
        return {vkIcon, false};
      }
    }
    return {};
  };

  const float buttonSide = chrome.buttonSize;
  const float spacing = chrome.buttonSpacing;

  const float regionMinX = ImGui::GetWindowContentRegionMin().x;
  const float regionMaxX = ImGui::GetWindowContentRegionMax().x;
  const float totalWidth = buttonSide * 3.0f + spacing * 2.0f;
  float startX = regionMaxX - totalWidth;
  if (startX < regionMinX)
    startX = regionMinX;

  ImVec2 cursor = ImGui::GetCursorPos();
  ImGui::SetCursorPos(ImVec2(startX, cursor.y));

  auto iconButton = [&](const char *id, const char *iconPathColored,
                        const char *iconPathGray, const char *fallbackText,
                        const char *tooltip, bool toggled) -> bool {
    const ImVec2 slotSize(buttonSide, buttonSide);
    const ImVec2 slotPos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, slotSize);
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    ImGuiID buttonId = ImGui::GetID(id);
    UIAnimationState &st = editorUiAnimationStates[buttonId];
    if (uiAnimationMode == UIAnimationMode::Off) {
      st.hover = hovered ? 1.0f : 0.0f;
      st.active = active ? 1.0f : 0.0f;
    } else {
      const float hoverTarget = hovered ? 1.0f : 0.0f;
      const float activeTarget = active ? 1.0f : 0.0f;
      st.hover += (hoverTarget - st.hover) * animStep;
      st.active += (activeTarget - st.active) * animStep;
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 slotMax(slotPos.x + slotSize.x, slotPos.y + slotSize.y);
    const float zoom = 1.0f + st.hover * 0.08f + st.active * 0.14f;
    const float drawSide = slotSize.x * zoom;
    const ImVec2 iconCenter(slotPos.x + slotSize.x * 0.5f,
                            slotPos.y + slotSize.y * 0.5f);
    const ImVec2 iconMin(iconCenter.x - drawSide * 0.5f,
                         iconCenter.y - drawSide * 0.5f);
    const ImVec2 iconMax(iconCenter.x + drawSide * 0.5f,
                         iconCenter.y + drawSide * 0.5f);

    const char *iconPath = toggled ? iconPathColored : iconPathGray;
    ToolbarIcon icon = resolveToolbarIcon(iconPath);
    int alpha = toggled ? 255 : 192;
    if (hovered && !toggled)
      alpha = 218;
    if (active && !toggled)
      alpha = 236;
    const ImU32 iconTint = IM_COL32(255, 255, 255, alpha);

    const float highlightStrength = std::max(st.hover, st.active);
    if (highlightStrength > 0.01f) {
      const int a = static_cast<int>(24.0f + 68.0f * highlightStrength);
      dl->AddRect(slotPos, slotMax, IM_COL32(255, 255, 255, a), 3.0f, 0, 1.0f);
    }
    if (toggled) {
      dl->AddRect(slotPos, slotMax, IM_COL32(255, 255, 255, 146), 3.0f, 0,
                  1.2f);
    }

    if (icon.id != static_cast<ImTextureID>(0)) {
      // Inset UVs slightly to avoid transparent-edge bleed artifacts between
      // icons.
      const float uvInset = 1.0f / 32.0f;
      const ImVec2 uvMin = icon.flipY ? ImVec2(uvInset, 1.0f - uvInset)
                                      : ImVec2(uvInset, uvInset);
      const ImVec2 uvMax = icon.flipY ? ImVec2(1.0f - uvInset, uvInset)
                                      : ImVec2(1.0f - uvInset, 1.0f - uvInset);
      dl->AddImage(icon.id, iconMin, iconMax, uvMin, uvMax, iconTint);
    } else {
      ImVec2 textSize = ImGui::CalcTextSize(fallbackText);
      ImVec2 textPos(iconCenter.x - textSize.x * 0.5f,
                     iconCenter.y - textSize.y * 0.5f);
      dl->AddText(textPos, iconTint, fallbackText);
    }
    if (hovered && tooltip && *tooltip) {
      ImGui::SetTooltip("%s", tooltip);
    }
    return pressed;
  };

  bool playPressed = iconButton(
      "##PlayModeToolbarButton", "Resources/Engine-Root/Editor/Play Button.png",
      "Resources/Engine-Root/Editor/Play Button Gray.png", "P", playTooltip,
      isPlaying);
  // Debug helper: MODULARITY_DEBUG_AUTOPLAY=<frames> presses Play once that
  // many frames after this bar starts rendering with a loaded project.
  {
    static int autoplayCountdown = []() {
      const char *v = std::getenv("MODULARITY_DEBUG_AUTOPLAY");
      const int frames = v ? std::atoi(v) : 0;
      return frames > 0 ? frames : -1;
    }();
    if (autoplayCountdown > 0 && !isPlaying &&
        projectManager.currentProject.isLoaded) {
      if (--autoplayCountdown == 0) {
        playPressed = true;
        std::fprintf(stderr, "[Debug] MODULARITY_DEBUG_AUTOPLAY entering play mode\n");
      }
    }
  }
  ImGui::SameLine(0.0f, spacing);
  bool specPressed =
      iconButton("##SpecModeToolbarButton",
                 "Resources/Engine-Root/Editor/Spec Mode Button.png",
                 "Resources/Engine-Root/Editor/Spec Mode Button Gray.png", "S",
                 specTooltip, specMode);
  ImGui::SameLine(0.0f, spacing);
  bool pausePressed = iconButton(
      "##PauseToolbarButton", "Resources/Engine-Root/Editor/Pause Button.png",
      "Resources/Engine-Root/Editor/Pause Button Gray.png", "||", pauseTooltip,
      isPaused);

    if (playPressed) {
      ImGui::ClearActiveID();
      bool newState = !isPlaying;
      if (newState) {
        const auto __playStart = std::chrono::steady_clock::now();
        auto __markPlayStep = [&](const char *label, const auto &stepStart) {
          const double ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - stepStart)
                                .count();
          if (ms > 10.0) {
            std::fprintf(stderr, "[ModuTimer] play enter %s %.2f ms\n", label,
                         ms);
          }
        };
        auto __stepStart = std::chrono::steady_clock::now();
        clearVideoPlayers();
        videoAssetPreviewPlayer.reset();
        videoAssetPreviewPath.clear();
        __markPlayStep("clearPreviews", __stepStart);
        // Reset script module state so Begin/static script state is fresh each
        // play session.
        __stepStart = std::chrono::steady_clock::now();
        resetScriptRuntimeStateForReload(false);
        __markPlayStep("scriptReset", __stepStart);
        __stepStart = std::chrono::steady_clock::now();
        capturePlayModeSnapshot();
        __markPlayStep("snapshot", __stepStart);
        deferInspectorRefresh = true;
        __stepStart = std::chrono::steady_clock::now();
        for (SceneObject &obj : sceneObjects) {
          if (!obj.hasAnimation)
            continue;
          obj.animation.runtimePlaying = false;
          obj.animation.runtimePaused = false;
          obj.animation.runtimeTime = 0.0f;
          obj.animation.runtimeDirection = 1.0f;
          obj.animation.runtimeInitialized = false;
          obj.animation.runtimeClipPath.clear();
        }
        __markPlayStep("animations", __stepStart);
        __stepStart = std::chrono::steady_clock::now();
        if (physics->isReady() || physics->init()) {
          physics->onPlayStart(sceneObjects);
        } else {
          addConsoleMessage(
              "PhysX failed to initialize; physics disabled for play mode",
              ConsoleMessageType::Warning);
        }
        __markPlayStep("physics", __stepStart);
        __stepStart = std::chrono::steady_clock::now();
        audio.setPrefer2DSpatialAudio(isProject2DPipeline() || uiWorldMode);
        audio.onPlayStart(sceneObjects);
        __markPlayStep("audio", __stepStart);
        bool hasPlayerController = false;
        for (const auto &obj : sceneObjects) {
          if (IsObjectEnabledInHierarchy(obj) && obj.hasPlayerController &&
              obj.playerController.enabled) {
            hasPlayerController = true;
            break;
          }
        }
        if (hasPlayerController && showGameViewport) {
          gameViewCursorLocked = true;
          gameViewportFocused = true;
        }
        const double __playMs = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() -
                                    __playStart)
                                    .count();
        std::fprintf(stderr,
                     "[ModuTimer] play enter total %.2f ms objects=%zu scripts=%zu textures=%zu shaders=%zu previews=%zu reflections=%zu\n",
                     __playMs, sceneObjects.size(), runtimeScriptBindings.size(),
                     rendererInitialized ? renderer.getTextureCacheCount() : 0,
                     rendererInitialized ? renderer.getShaderCacheCount() : 0,
                     rendererInitialized ? renderer.getExtraPreviewTargetCount() : 0,
                     rendererInitialized ? renderer.getReflectionCastTargetCount() : 0);
      } else {
        const auto __playStop = std::chrono::steady_clock::now();
        clearVideoPlayers();
        videoAssetPreviewPlayer.reset();
        videoAssetPreviewPath.clear();
        physics->onPlayStop();
        audio.onPlayStop();
        restorePlayModeSnapshot();
        deferInspectorRefresh = true;
        resetScriptRuntimeStateForReload(false);
        isPaused = false;
        if (specMode && (physics->isReady() || physics->init())) {
          physics->onPlayStart(sceneObjects);
        }
        const double __stopMs = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() -
                                    __playStop)
                                    .count();
        std::fprintf(stderr, "[ModuTimer] play exit total %.2f ms\n", __stopMs);
      }
      isPlaying = newState;
    }
  if (pausePressed) {
    isPaused = !isPaused;
    if (isPaused)
      isPlaying = true; // placeholder: pausing implies we're in play mode
  }
  if (specPressed) {
    ImGui::ClearActiveID();
    bool enable = !specMode;
    if (enable && !physics->isReady() && !physics->init()) {
      addConsoleMessage("PhysX failed to initialize; spec mode disabled",
                        ConsoleMessageType::Warning);
      enable = false;
    }
    if (specMode != enable) {
      deferInspectorRefresh = true;
      resetScriptRuntimeStateForReload(false);
    }
    specMode = enable;
    if (!isPlaying) {
      if (specMode) {
        clearVideoPlayers();
        videoAssetPreviewPlayer.reset();
        videoAssetPreviewPath.clear();
        physics->onPlayStart(sceneObjects);
        audio.setPrefer2DSpatialAudio(isProject2DPipeline() || uiWorldMode);
        audio.onPlayStart(sceneObjects);
      } else {
        clearVideoPlayers();
        videoAssetPreviewPlayer.reset();
        videoAssetPreviewPath.clear();
        physics->onPlayStop();
        audio.onPlayStop();
      }
    }
  }
}

