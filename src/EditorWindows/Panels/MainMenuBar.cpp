#include "Engine.h"

namespace {
bool layoutFileNeedsUtilityDockMigration(const fs::path &layoutPath,
                                         bool projectSettingsVisible,
                                         bool modupakVisible) {
  std::ifstream in(layoutPath);
  if (!in.is_open()) {
    return false;
  }

  std::string currentWindow;
  std::string projectDockId;
  std::string projectSettingsDockId;
  bool hasModupakWindow = false;
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("[Window][", 0) == 0) {
      const size_t close = line.find(']', 9);
      currentWindow = (close == std::string::npos) ? std::string()
                                                   : line.substr(9, close - 9);
      if (currentWindow == "Modupak Manager") {
        hasModupakWindow = true;
      }
      continue;
    }

    if (line.rfind("DockId=", 0) != 0) {
      continue;
    }

    const std::string dockId = line.substr(
        7, line.find(',') == std::string::npos ? std::string::npos
                                               : line.find(',') - 7);
    if (currentWindow == "Project" && projectDockId.empty()) {
      projectDockId = dockId;
    } else if (currentWindow == "Project Settings" &&
               projectSettingsDockId.empty()) {
      projectSettingsDockId = dockId;
    }
  }

  if (projectSettingsVisible && !projectDockId.empty() &&
      !projectSettingsDockId.empty() &&
      projectDockId == projectSettingsDockId) {
    return true;
  }

  if (modupakVisible && !hasModupakWindow) {
    return true;
  }

  return false;
}
} // namespace

void Engine::renderMainMenuBar() {
  refreshScriptEditorWindows();

  // Help > About fires this, the popup itself is rendered after the menu bar
  // closes because OpenPopup inside a BeginMenu attaches to the menu's ID
  // stack and the modal would never find it out here.
  static bool triggerAboutModularityPopup = false;

  if (ImGui::BeginMainMenuBar()) {
    const EditorChromeMetrics &chrome = getEditorChromeMetrics(uiChromeScale);
    ImVec2 menuItemSpacing = chrome.menuItemSpacing;
    ImVec2 menuFramePadding = chrome.menuFramePadding;
    float menuFontScale = chrome.fontScale;
    // On touch, fatten the menu bar + dropdown hit areas (the pushed FramePadding
    // also governs the BeginMenu popup's MenuItem height) so they're finger-sized.
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_IsTouchScreen) {
      menuItemSpacing.x *= 1.6f;
      menuFramePadding.x *= 1.8f;
      menuFramePadding.y *= 2.6f;
      if (menuFontScale < 1.0f) menuFontScale = 1.0f;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, menuItemSpacing);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, menuFramePadding);
    ImGui::SetWindowFontScale(menuFontScale);

    if (ImGui::BeginMenu("Engine")) {
      if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
        showNewSceneDialog = true;
        memset(newSceneName, 0, sizeof(newSceneName));
      }
      if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
        saveCurrentScene();
      }
      if (ImGui::MenuItem("Save Scene As...")) {
        showSaveSceneAsDialog = true;
        strncpy(saveSceneAsName,
                projectManager.currentProject.currentSceneName.c_str(),
                sizeof(saveSceneAsName) - 1);
      }
      if (ImGui::BeginMenu("Load Scene",
                           projectManager.currentProject.isLoaded)) {
        const auto scenes = projectManager.currentProject.getSceneList();
        if (scenes.empty()) {
          ImGui::TextDisabled("No scenes");
        }
        for (const auto &scene : scenes) {
          const bool isCurrentScene =
              scene == projectManager.currentProject.currentSceneName;
          if (ImGui::MenuItem(scene.c_str(), nullptr, isCurrentScene,
                              !isCurrentScene)) {
            loadScene(scene);
          }
        }
        ImGui::EndMenu();
      }
      if (ImGui::MenuItem("Build Settings...")) {
        showBuildSettings = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Return to Project Manager")) {
        if (projectManager.currentProject.hasUnsavedChanges) {
          requestSceneSave(projectManager.currentProject.currentSceneName,
                           PendingScenePostAction::CloseProject,
                           "",
                           true);
        } else {
          performCloseProject();
        }
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Exit")) {
        glfwSetWindowShouldClose(editorWindow, true);
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Actions")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undoStack.empty())) {
        undo();
      }
      if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redoStack.empty())) {
        redo();
      }
      ImGui::Separator();
      const bool hasSelection =
          !selectedObjectIds.empty() || selectedObjectId >= 0;
      if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSelection)) {
        copySelected();
      }
      if (ImGui::MenuItem("Paste", "Ctrl+V", false, !objectClipboard.empty())) {
        pasteClipboard();
      }
      if (ImGui::MenuItem("Duplicate", nullptr, false, hasSelection)) {
        duplicateSelected();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Select All", "Ctrl+A", false,
                          !sceneObjects.empty())) {
        selectAllObjects();
      }
      if (ImGui::MenuItem("Clear Selection", nullptr, false, hasSelection)) {
        clearSelection();
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Workflow")) {
      if (ImGui::BeginMenu("Workspace")) {
        const bool workspaceTransitionActive =
            pendingWorkspaceReload || workspaceLayoutDirty ||
            glfwGetTime() < workspaceLayoutStabilizeUntil;
        const bool workspaceSwitchLocked =
            glfwGetTime() < workspaceSwitchLockUntil;

        auto switchWorkspace = [&](WorkspaceMode mode) {
          if (currentWorkspace == mode) {
            return;
          }
          if (workspaceTransitionActive || workspaceSwitchLocked) {
            return;
          }
          saveWorkspaceLayout(currentWorkspace);
          applyWorkspacePreset(mode, true);
          workspaceSwitchLockUntil = glfwGetTime() + 0.22;
        };

        const bool canSwitchWorkspace =
            !workspaceTransitionActive && !workspaceSwitchLocked;
        if (ImGui::MenuItem("Default", nullptr,
                            currentWorkspace == WorkspaceMode::Default,
                            canSwitchWorkspace)) {
          switchWorkspace(WorkspaceMode::Default);
        }
        if (ImGui::MenuItem("Animation", nullptr,
                            currentWorkspace == WorkspaceMode::Animation,
                            canSwitchWorkspace)) {
          switchWorkspace(WorkspaceMode::Animation);
        }
        if (hasScriptingWindowPackage()) {
          if (ImGui::MenuItem("Scripting", nullptr,
                              currentWorkspace == WorkspaceMode::Scripting,
                              canSwitchWorkspace)) {
            switchWorkspace(WorkspaceMode::Scripting);
          }
        }
        ImGui::EndMenu();
      }

      ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy);
      ImGui::MenuItem("Inspector", nullptr, &showInspector);
      ImGui::MenuItem("File Browser", nullptr, &showFileBrowser);
      ImGui::MenuItem("Console", nullptr, &showConsole);
      if (hasScriptingWindowPackage()) {
        ImGui::MenuItem("Scripting", nullptr, &showScriptingWindow);
      }
      bool prevProjectBrowser = showProjectBrowser;
      ImGui::MenuItem("Project Settings", nullptr, &showProjectBrowser);
      if (prevProjectBrowser != showProjectBrowser) {
        saveEditorUserSettings();
      }
      /*if (hasMeshBuilderPackage()) {
        ImGui::MenuItem("Mesh Builder (Legacy Window)", nullptr,
                        &showMeshBuilder);
      }*/
      ImGui::MenuItem("Environment", nullptr, &showEnvironmentWindow);
      ImGui::MenuItem("Camera", nullptr, &showCameraWindow);
      bool prevAnimationWindow = showAnimationWindow;
      ImGui::MenuItem("Animation", nullptr, &showAnimationWindow);
      if (prevAnimationWindow != showAnimationWindow) {
        saveEditorUserSettings();
      }
      bool prevAIPathWindow = showAIPathfindingWindow;
      ImGui::MenuItem("AI Pathfinding", nullptr, &showAIPathfindingWindow);
      if (prevAIPathWindow != showAIPathfindingWindow) {
        saveEditorUserSettings();
      }
      if (hasSpriteEditorPackage()) {
        bool prevPixelSpriteEditor = showPixelSpriteEditorWindow;
        ImGui::MenuItem("Pixel Sprite Editor", nullptr,
                        &showPixelSpriteEditorWindow);
        if (prevPixelSpriteEditor != showPixelSpriteEditorWindow) {
          saveEditorUserSettings();
        }
      }
      ImGui::MenuItem("View Output", nullptr, &showViewOutput);
      ImGui::Separator();
      if (isProject2DPipeline()) {
        bool forced2DOverlay = true;
        ImGui::BeginDisabled();
        ImGui::MenuItem("UI World Overlay", nullptr, &forced2DOverlay);
        ImGui::EndDisabled();
      } else {
        ImGui::MenuItem("UI World Overlay", nullptr, &uiWorldMode);
      }
      ImGui::MenuItem("3D Grid", nullptr, &showSceneGrid3D);
      ImGui::Separator();
      if (ImGui::MenuItem("Fullscreen Viewport", "F11", viewportFullscreen)) {
        viewportFullscreen = !viewportFullscreen;
      }
      ImGui::Separator();
      if (ImGui::BeginMenu("Style")) {
        ImGui::TextDisabled("Editor Styles");
        for (size_t i = 0; i < uiStylePresets.size(); ++i) {
          bool selected = static_cast<int>(i) == uiStylePresetIndex;
          if (ImGui::MenuItem(uiStylePresets[i].name.c_str(), nullptr,
                              selected)) {
            applyUIStylePresetByName(uiStylePresets[i].name);
            saveEditorUserSettings();
          }
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("UI Scale")) {
          const auto selectScale = [&](EditorChromeScale scale) {
            if (uiChromeScale == scale) {
              return;
            }
            uiChromeScale = scale;
            saveEditorUserSettings();
          };
          if (ImGui::MenuItem("Big", nullptr,
                              uiChromeScale == EditorChromeScale::Big)) {
            selectScale(EditorChromeScale::Big);
          }
          if (ImGui::MenuItem("Default", nullptr,
                              uiChromeScale == EditorChromeScale::Default)) {
            selectScale(EditorChromeScale::Default);
          }
          if (ImGui::MenuItem("Compact", nullptr,
                              uiChromeScale == EditorChromeScale::Compact)) {
            selectScale(EditorChromeScale::Compact);
          }
          ImGui::EndMenu();
        }
        ImGui::TextDisabled("UI Animations");
        if (ImGui::MenuItem("Fluid", nullptr,
                            uiAnimationMode == UIAnimationMode::Fluid)) {
          uiAnimationMode = UIAnimationMode::Fluid;
          saveEditorUserSettings();
        }
        if (ImGui::MenuItem("Snappy", nullptr,
                            uiAnimationMode == UIAnimationMode::Snappy)) {
          uiAnimationMode = UIAnimationMode::Snappy;
          saveEditorUserSettings();
        }
        if (ImGui::MenuItem("Off", nullptr,
                            uiAnimationMode == UIAnimationMode::Off)) {
          uiAnimationMode = UIAnimationMode::Off;
          saveEditorUserSettings();
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Feedback Sounds")) {
          bool feedbackSettingsChanged = false;
          feedbackSettingsChanged |= ImGui::MenuItem("Enable All Feedback Sounds", nullptr, &feedbackSoundsEnabled);
          ImGui::Separator();
          feedbackSettingsChanged |= ImGui::MenuItem("Click Sounds", nullptr, &feedbackClickSoundsEnabled);
          feedbackSettingsChanged |= ImGui::MenuItem("Error Sounds", nullptr, &feedbackErrorSoundsEnabled);
          feedbackSettingsChanged |= ImGui::MenuItem("Other Feedback Sounds", nullptr, &feedbackOtherSoundsEnabled);
          ImGui::Separator();
          ImGui::TextDisabled("Boot intro sound is always enabled.");
          if (feedbackSettingsChanged) {
            saveEditorUserSettings();
          }
          ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::MenuItem("Style Editor", nullptr, &showStyleEditor);
        if (ImGui::MenuItem("Export Theme + Layout")) {
          exportEditorThemeLayout();
        }
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Packages")) {
      bool prevRegistryPackages = showRegistryPackagesWindow;
      ImGui::MenuItem("ModuPAK Manager", nullptr, &showRegistryPackagesWindow);
      if (prevRegistryPackages != showRegistryPackagesWindow) {
        saveEditorUserSettings();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Export Into ModuPAK...")) {
        openModuPakExportDialog(fileBrowser.selectedFiles);
      }
      if (ImGui::MenuItem("Import ModuPAK...")) {
        openModuPakImportDialog();
      }
      if (ImGui::MenuItem("Import ModuOBJ...")) {
        openModuObjImportDialog();
      }
      if (!scriptEditorWindows.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("Scripted Windows");
        for (auto &window : scriptEditorWindows) {
          ImGui::MenuItem(window.label.c_str(), nullptr, &window.open);
        }
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
      ImGui::MenuItem("Modularity Doctor", nullptr, &showModularityDoctorWindow);
      bool prevProfilerWindow = showGameProfilerWindow;
      ImGui::MenuItem("Profiler", nullptr, &showGameProfilerWindow);
      if (prevProfilerWindow != showGameProfilerWindow) {
        saveEditorUserSettings();
      }
      ImGui::Separator();
      ImGui::TextDisabled("Script Modes");
      auto toggleSpec = [&](bool enabled) {
        if (specMode == enabled)
          return;
        if (enabled && !physics->isReady() && !physics->init()) {
          addConsoleMessage("PhysX failed to initialize; spec mode disabled",
                            ConsoleMessageType::Warning);
          specMode = false;
          return;
        }
        deferInspectorRefresh = true;
        resetScriptRuntimeStateForReload(false);
        specMode = enabled;
        if (!isPlaying) {
          if (specMode) {
            physics->onPlayStart(sceneObjects);
            audio.setPrefer2DSpatialAudio(isProject2DPipeline() || uiWorldMode);
            audio.onPlayStart(sceneObjects);
          } else {
            physics->onPlayStop();
            audio.onPlayStop();
          }
        }
      };
      bool specValue = specMode;
      if (ImGui::MenuItem("Spec Mode (run Script_Spec)", nullptr, &specValue)) {
        toggleSpec(specValue);
      }
      bool testValue = testMode;
      if (ImGui::MenuItem("Test Mode (run Script_TestEditor)", nullptr,
                          &testValue)) {
        if (testMode != testValue) {
          deferInspectorRefresh = true;
          resetScriptRuntimeStateForReload(false);
        }
        testMode = testValue;
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("SceneOBJ")) {
      renderSceneObjectCreateMenu();
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("About")) {
        triggerAboutModularityPopup = true;
      }
      ImGui::EndMenu();
    }

    // On touch with the Mobile layout, the Play/Spec/Pause controls live in the
    // Quick Tools popup instead, so keep the menu bar uncluttered. Desktop (and
    // touch in Desktop layout) keeps the bar here.
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_IsTouchScreen) ||
        !mobileEditorLayout) {
      renderPlayControlsBar();
    }

    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleVar(2);
    ImGui::EndMainMenuBar();
  }

  // the About Modularity card, same design system as the Delete/Rename cards
  if (triggerAboutModularityPopup) {
    playEditorFeedbackPreview("Resources/Sounds/Info.mp3", 0.95f, false,
                              EditorFeedbackSoundCategory::Other);
    ImGui::OpenPopup("Modularity Engine##AboutModularity");
    triggerAboutModularityPopup = false;
  }
  if (ImGui::IsPopupOpen("Modularity Engine##AboutModularity")) {
    CardModalIcon moduLogo;
    if (rendererInitialized) {
      Texture *logo =
          renderer.getTexture("Resources/Engine-Root/Modu-Logo.png");
      if (logo && logo->GetID()) {
        moduLogo = {static_cast<ImTextureID>(logo->GetID()), true};
      }
    }
    if (moduLogo.id == static_cast<ImTextureID>(0) && usingVulkan() &&
        vulkanRendererInitialized && vulkanRenderer) {
      moduLogo = {vulkanRenderer->getOrCreateUIImage(
                      "Resources/Engine-Root/Modu-Logo.png"),
                  false};
    }
    if (beginCardModal("Modularity Engine##AboutModularity", 0.0f, nullptr,
                       moduLogo)) {
      ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.90f);
      cardModalText("aka ModuEngine");
      ImGui::PopFont();
      ImGui::Spacing();
      cardModalText("Modularity™ is a trademark of Tareno Labs™");
      cardModalText("© 2025-2026 Tareno Labs™");
      if (cardModalButton("Okay", CardButtonKind::Primary, 0, 1)) {
        ImGui::CloseCurrentPopup();
      }
      endCardModal();
    }
  }

  auto layoutFileHasDockNodesForDockspace = [](const fs::path &layoutPath,
                                               ImGuiID dockspaceId) {
    if (dockspaceId == 0) {
      return false;
    }
    std::ifstream in(layoutPath);
    if (!in.is_open()) {
      return false;
    }

    char dockspaceIdHex[16];
    std::snprintf(dockspaceIdHex, sizeof(dockspaceIdHex), "0x%08X",
                  dockspaceId);

    bool hasDockingData = false;
    bool hasDockNodes = false;
    bool hasMatchingDockspace = false;
    std::string line;
    while (std::getline(in, line)) {
      if (line == "[Docking][Data]") {
        hasDockingData = true;
        continue;
      }
      if (!hasDockingData) {
        continue;
      }
      if (!line.empty() && line.front() == '[') {
        break;
      }
      if (line.find("DockNode") != std::string::npos) {
        hasDockNodes = true;
      }
      if (line.find("DockSpace") != std::string::npos &&
          line.find(dockspaceIdHex) != std::string::npos) {
        hasMatchingDockspace = true;
      }
    }
    return hasDockingData && hasDockNodes && hasMatchingDockspace;
  };

  if (pendingWorkspaceReload) {
    if (mainDockspaceId != 0) {
      const bool hasLayoutFile = !pendingWorkspaceIniPath.empty() &&
                                 fs::exists(pendingWorkspaceIniPath);
      const bool hasMatchingDockspace =
          hasLayoutFile && layoutFileHasDockNodesForDockspace(
                               pendingWorkspaceIniPath, mainDockspaceId);
      if (hasMatchingDockspace) {
        ImGui::ClearIniSettings();
        ImGui::LoadIniSettingsFromDisk(
            pendingWorkspaceIniPath.string().c_str());
        workspaceLayoutSettlingFrame = true;
        if (ImGui::DockBuilderGetNode(mainDockspaceId) == nullptr) {
          ImGui::DockBuilderRemoveNode(mainDockspaceId);
          workspaceLayoutDirty = true;
          workspaceLayoutAutoRepairPending = true;
        } else {
          // A valid persisted layout should win over the default workspace
          // repair path instead of being rebuilt a frame later.
          workspaceLayoutAutoRepairPending = false;
        }
      } else {
        // No persisted layout to load (or stale DockSpace ID): force
        // deterministic rebuild.
        ImGui::ClearIniSettings();
        ImGui::DockBuilderRemoveNode(mainDockspaceId);
        workspaceLayoutDirty = true;
        workspaceLayoutAutoRepairPending = true;
        workspaceLayoutSettlingFrame = true;
      }
      pendingWorkspaceReload = false;
      workspaceLayoutSavePending = false;
      workspaceLayoutStabilizeUntil = glfwGetTime() + 0.75;
    }
  }

  if (!pendingWorkspaceReload && workspaceLayoutDirty) {
    ImGui::ClearIniSettings();
    buildWorkspaceLayout(currentWorkspace);
    workspaceLayoutSavePending = true;
    workspaceLayoutSettlingFrame = true;
  }

  if (showStyleEditor) {
    if (ImGui::Begin("Style Editor", &showStyleEditor)) {
      static char newPresetName[128] = "";
      const auto &fontCatalog = getUIFontCatalog();
      std::string currentFontLabel = "ImGui Default";
      const int currentFontIndex = findUIFontCatalogIndex(uiEditorFontAsset);
      if (currentFontIndex >= 0) {
        currentFontLabel = fontCatalog[static_cast<size_t>(currentFontIndex)].label;
      }

      if (ImGui::BeginCombo("Preset", uiStylePresetName.c_str())) {
        for (size_t i = 0; i < uiStylePresets.size(); ++i) {
          const bool selected = (uiStylePresets[i].name == uiStylePresetName);
          if (ImGui::Selectable(uiStylePresets[i].name.c_str(), selected)) {
            applyUIStylePresetByName(uiStylePresets[i].name);
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
      if (ImGui::BeginCombo("Editor Font", currentFontLabel.c_str())) {
        for (size_t i = 0; i < fontCatalog.size(); ++i) {
          const bool selected = (fontCatalog[i].id == uiEditorFontAsset);
          if (ImGui::Selectable(fontCatalog[i].label.c_str(), selected)) {
            applyEditorUIFontById(fontCatalog[i].id);
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      // glass knobs live up here because ShowStyleEditor below doesn't know they exist
      ImGui::Checkbox("Glass Blur", &ImGui::GetStyle().GlassBlur);
      ImGui::SameLine();
      ImGui::Checkbox("Pill Switches", &ImGui::GetStyle().CheckboxSwitch);
      ImGui::SameLine();
      ImGui::Checkbox("Pill Sliders", &ImGui::GetStyle().SliderPill);

      if (ImGui::Button("Save Current Preset")) {
        saveCurrentUIStyleToPreset(uiStylePresetName, true);
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(180.0f);
      ImGui::InputTextWithHint("##NewStylePresetName", "New preset name", newPresetName, sizeof(newPresetName));
      ImGui::SameLine();
      if (ImGui::Button("Save As New") && newPresetName[0] != '\0') {
        if (saveCurrentUIStyleToPreset(newPresetName, false)) {
          std::snprintf(newPresetName, sizeof(newPresetName), "%s", "");
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Save UI Settings")) {
        saveEditorUserSettings();
      }
      ImGui::SameLine();
      if (ImGui::Button("Export Theme + Layout")) {
        exportEditorThemeLayout();
      }
      ImGui::Separator();
      ImGuiStyle &style = ImGui::GetStyle();
      ImGui::ShowStyleEditor(&style);
    }
    ImGui::End();
  }
}

void Engine::applyWorkspacePreset(WorkspaceMode mode, bool rebuildLayout) {
  currentWorkspace = mode;
  workspaceLayoutSavePending = false;
  workspaceLayoutAutoRepairPending = true;
  workspaceLayoutStabilizeUntil = glfwGetTime() + 0.75;
  switch (mode) {
  case WorkspaceMode::Default:
    showHierarchy = true;
    showInspector = true;
    showFileBrowser = true;
    showConsole = true;
    showScriptingWindow = false;
    showAnimationWindow = false;
    showAIPathfindingWindow = false;
    showEnvironmentWindow = true;
    showCameraWindow = true;
    showGameViewport = true;
    break;
  case WorkspaceMode::Animation:
    showHierarchy = true;
    showInspector = true;
    showFileBrowser = false;
    showConsole = true;
    showScriptingWindow = false;
    showAnimationWindow = true;
    showAIPathfindingWindow = true;
    showEnvironmentWindow = false;
    showCameraWindow = false;
    showGameViewport = true;
    break;
  case WorkspaceMode::Scripting:
    showHierarchy = true;
    showInspector = true;
    showFileBrowser = true;
    showConsole = true;
    showScriptingWindow = hasScriptingWindowPackage();
    showAnimationWindow = false;
    showAIPathfindingWindow = false;
    showEnvironmentWindow = false;
    showCameraWindow = false;
    showGameViewport = true;
    break;
  }

  if (rebuildLayout) {
    // Explicit workspace switches should rebuild from the preset instead of
    // reloading stale persisted dock data that can fight the new layout.
    pendingWorkspaceIniPath.clear();
    pendingWorkspaceReload = false;
    buildWorkspaceLayout(mode);
    if (workspaceLayoutDirty) {
      pendingWorkspaceReload = true;
    }
    workspaceLayoutStabilizeUntil = glfwGetTime() + 0.75;
    return;
  }

  auto layoutFileIsUsable = [&](const fs::path &layoutPath) {
    std::ifstream in(layoutPath);
    if (!in.is_open())
      return false;

    bool hasDockingData = false;
    bool hasDockNodes = false;
    bool hasDockspace = false;
    std::string line;
    while (std::getline(in, line)) {
      if (line == "[Docking][Data]") {
        hasDockingData = true;
        continue;
      }
      if (hasDockingData && line.find("DockNode") != std::string::npos) {
        hasDockNodes = true;
      }
      if (hasDockingData && line.find("DockSpace") != std::string::npos) {
        hasDockspace = true;
      }
    }
    if (!(hasDockingData && hasDockNodes && hasDockspace)) {
      return false;
    }

    if (loadedWorkspaceLayoutVersion < kWorkspaceLayoutVersion &&
        layoutFileNeedsUtilityDockMigration(layoutPath, showProjectBrowser,
                                            showRegistryPackagesWindow)) {
      return false;
    }

    return true;
  };

  fs::path layoutPath = getWorkspaceLayoutPath(mode);
  if (!layoutPath.empty() && fs::exists(layoutPath) &&
      layoutFileIsUsable(layoutPath)) {
    pendingWorkspaceIniPath = layoutPath;
    pendingWorkspaceReload = true;
    workspaceLayoutDirty = false;
    workspaceLayoutStabilizeUntil = glfwGetTime() + 0.75;
    return;
  }

  // applyWorkspacePreset() is also called during project/settings load, where
  // there may be no active ImGui window for ImGui::GetID() yet.
  // Defer dock layout rebuild to the normal UI frame path.
  pendingWorkspaceIniPath.clear();
  pendingWorkspaceReload = true;
  workspaceLayoutDirty = true;
  workspaceLayoutStabilizeUntil = glfwGetTime() + 0.75;
}

void Engine::buildWorkspaceLayout(WorkspaceMode mode) {
  if (!ImGui::GetCurrentContext()) {
    workspaceLayoutDirty = true;
    return;
  }

  if (mainDockspaceId == 0) {
    workspaceLayoutDirty = true;
    return;
  }
  const ImGuiID dockspaceId = mainDockspaceId;

  ImGuiViewport *viewport = ImGui::GetMainViewport();
  if (!viewport) {
    workspaceLayoutDirty = true;
    return;
  }

  ImGui::DockBuilderRemoveNode(dockspaceId);
  ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
  ImVec2 dockspaceSize = viewport->WorkSize;
  if (ImGuiWindow *dockHost = ImGui::FindWindowByName("DockSpace")) {
    dockspaceSize = dockHost->Size;
  }
  dockspaceSize.y =
      ImMax(0.0f, dockspaceSize.y - getEditorBottomStatusReserveHeight());
  ImGui::DockBuilderSetNodeSize(dockspaceId, dockspaceSize);

  ImGuiID dockMain = dockspaceId;
  if (mode == WorkspaceMode::Default) {
    ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left,
                                                   0.20f, nullptr, &dockMain);
    ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right,
                                                    0.28f, nullptr, &dockMain);
    ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down,
                                                     0.28f, nullptr, &dockMain);
    ImGuiID dockUtility = ImGui::DockBuilderSplitNode(
        dockRight, ImGuiDir_Down, 0.42f, nullptr, &dockRight);

    ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
    ImGui::DockBuilderDockWindow("Camera", dockLeft);
    ImGui::DockBuilderDockWindow("Inspector", dockRight);
    ImGui::DockBuilderDockWindow("Environment", dockRight);
    ImGui::DockBuilderDockWindow("Project", dockBottom);
    ImGui::DockBuilderDockWindow("Project Settings", dockUtility);
    ImGui::DockBuilderDockWindow("Modupak Manager", dockUtility);
    if (hasSpriteEditorPackage()) {
      ImGui::DockBuilderDockWindow("Pixel Sprite Editor", dockMain);
    }
    ImGui::DockBuilderDockWindow("Viewport", dockMain);
    ImGui::DockBuilderDockWindow("Game Viewport", dockMain);
  } else if (mode == WorkspaceMode::Animation) {
    ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left,
                                                   0.20f, nullptr, &dockMain);
    ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right,
                                                    0.27f, nullptr, &dockMain);
    ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down,
                                                     0.35f, nullptr, &dockMain);
    ImGuiID dockUtility = ImGui::DockBuilderSplitNode(
        dockRight, ImGuiDir_Down, 0.42f, nullptr, &dockRight);

    ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
    ImGui::DockBuilderDockWindow("Camera", dockLeft);
    ImGui::DockBuilderDockWindow("Inspector", dockRight);
    ImGui::DockBuilderDockWindow("Environment", dockRight);
    ImGui::DockBuilderDockWindow("Animation", dockBottom);
    ImGui::DockBuilderDockWindow("AI Pathfinding", dockBottom);
    ImGui::DockBuilderDockWindow("Project", dockBottom);
    ImGui::DockBuilderDockWindow("Project Settings", dockUtility);
    ImGui::DockBuilderDockWindow("Modupak Manager", dockUtility);
    if (hasSpriteEditorPackage()) {
      ImGui::DockBuilderDockWindow("Pixel Sprite Editor", dockMain);
    }
    ImGui::DockBuilderDockWindow("Viewport", dockMain);
  } else {
    ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left,
                                                   0.25f, nullptr, &dockMain);
    ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right,
                                                    0.35f, nullptr, &dockMain);
    ImGuiID dockUtility = ImGui::DockBuilderSplitNode(
        dockRight, ImGuiDir_Down, 0.42f, nullptr, &dockRight);

    ImGui::DockBuilderDockWindow("Project", dockLeft);
    ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
    ImGui::DockBuilderDockWindow("Camera", dockLeft);
    if (hasScriptingWindowPackage()) {
      ImGui::DockBuilderDockWindow("Scripting", dockRight);
    }
    ImGui::DockBuilderDockWindow("Inspector", dockRight);
    ImGui::DockBuilderDockWindow("Environment", dockRight);
    ImGui::DockBuilderDockWindow("Project Settings", dockUtility);
    ImGui::DockBuilderDockWindow("Modupak Manager", dockUtility);
    if (hasSpriteEditorPackage()) {
      ImGui::DockBuilderDockWindow("Pixel Sprite Editor", dockMain);
    }
    ImGui::DockBuilderDockWindow("Viewport", dockMain);
    ImGui::DockBuilderDockWindow("Game Viewport", dockMain);
  }

  ImGui::DockBuilderFinish(dockspaceId);
  workspaceLayoutDirty = false;
  workspaceLayoutSavePending = false;
  workspaceLayoutStabilizeUntil = glfwGetTime() + 0.75;
}

// Shared SceneOBJ creation entries used by the "SceneOBJ" menu bar entry and
// the hierarchy create popups. Category names are ModuPAK extension points
// (packages inject entries by category name, especially "Lights"), so keep them
// stable. Future you renaming a category WILL break someone's ModuPAK.
void Engine::renderSceneObjectCreateMenu() {
  // TODO: "Search SceneOBJ..." quick-create filter, once the editor has a
  // reusable in-menu search pattern.
  auto lastCreated = [&]() -> SceneObject * {
    return sceneObjects.empty() ? nullptr : &sceneObjects.back();
  };
  auto allocateRig25DNodeId = [&]() {
    int nextNodeId = 0;
    for (const SceneObject &obj : sceneObjects) {
      if (!obj.hasRig25DNode || obj.rig25DNode.nodeId < 0) {
        continue;
      }
      nextNodeId = std::max(nextNodeId, obj.rig25DNode.nodeId + 1);
    }
    return nextNodeId;
  };
  auto createRig25DObject = [&](bool isRoot) {
    addObject(ObjectType::Empty, isRoot ? "Rig Root" : "Rig Node");
    if (SceneObject *created = getSelectedObject()) {
      created->type = ObjectType::Empty;
      created->hasRenderer = false;
      created->renderType = RenderType::None;
      created->faceCamera = false;
      created->hasRig25DRoot = isRoot;
      created->rig25DRoot.enabled = isRoot;
      created->hasRig25DNode = !isRoot;
      created->rig25DNode.enabled = !isRoot;
      created->rig25DNode.nodeId = isRoot ? -1 : allocateRig25DNodeId();
      created->rig25DNode.nodeName = isRoot ? std::string() : created->name;
      created->localPosition = created->position;
      created->localRotation = NormalizeEulerDegrees(created->rotation);
      created->localScale = created->scale;
      created->localInitialized = true;
      EnsureInspectorComponentMetadata(*created);
      projectManager.currentProject.hasUnsavedChanges = true;
    }
  };
  auto createTaggedMarker = [&](const std::string &label,
                                const std::string &tag) {
    addObject(ObjectType::Empty, label);
    if (SceneObject *created = lastCreated()) {
      created->tag = tag;
    }
  };
  auto createUIWithCanvas = [&](ObjectType type, const std::string &baseName) {
    int canvasId = -1;
    for (const auto &obj : sceneObjects) {
      if (obj.hasUI && obj.ui.type == UIElementType::Canvas) {
        canvasId = obj.id;
        break;
      }
    }
    if (canvasId < 0) {
      addObject(ObjectType::Canvas, "Canvas");
      if (SceneObject *created = lastCreated()) {
        canvasId = created->id;
      }
    }
    addObject(type, baseName);
    if (!sceneObjects.empty() && canvasId >= 0) {
      setParent(sceneObjects.back().id, canvasId);
    }
  };

  if (ImGui::MenuItem("Empty SceneOBJ"))
    addObject(ObjectType::Empty, "Empty");
  ImGui::Separator();

  if (ImGui::BeginMenu("3D Primitives")) {
    if (ImGui::MenuItem("Cube"))
      addObject(ObjectType::Cube, "Cube");
    if (ImGui::MenuItem("Sphere"))
      addObject(ObjectType::Sphere, "Sphere");
    if (ImGui::MenuItem("Capsule"))
      addObject(ObjectType::Capsule, "Capsule");
    if (ImGui::MenuItem("Plane"))
      addObject(ObjectType::Plane, "Plane");
    if (ImGui::MenuItem("Torus"))
      addObject(ObjectType::Torus, "Torus");
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("2D Objects")) {
    if (ImGui::MenuItem("Sprite (Quad)"))
      addObject(ObjectType::Sprite, "Sprite");
    if (ImGui::MenuItem("2.5D Sprite"))
      addObject(ObjectType::Sprite25D, "2.5D Sprite");
    if (ImGui::MenuItem("Particle System 2D"))
      addObject(ObjectType::ParticleSystem2D, "Particle System 2D");
    if (ImGui::MenuItem("Mirror"))
      addObject(ObjectType::Mirror, "Mirror");
    ImGui::EndMenu();
  }

  if (isProject25DPipeline() && ImGui::BeginMenu("2.5D Rig")) {
    ImGui::TextDisabled("Rig nodes are Empty objects.");
    ImGui::TextDisabled("Animate them with the normal transform tracks.");
    ImGui::Separator();
    if (ImGui::MenuItem("Create Rig Root (Empty)")) {
      createRig25DObject(true);
    }
    if (ImGui::MenuItem("Create Rig Node (Empty)")) {
      createRig25DObject(false);
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("MMesh")) {
    if (ImGui::MenuItem("Cube"))
      createMMeshPrimitive("Cube");
    if (ImGui::MenuItem("Sphere"))
      createMMeshPrimitive("Sphere");
    if (ImGui::MenuItem("Plane"))
      createMMeshPrimitive("Plane");
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("RMesh")) {
    if (ImGui::MenuItem("Cube"))
      createRMeshPrimitive("Cube");
    if (ImGui::MenuItem("Sphere"))
      createRMeshPrimitive("Sphere");
    if (ImGui::MenuItem("Plane"))
      createRMeshPrimitive("Plane");
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Cameras")) {
    if (ImGui::MenuItem("Camera"))
      addObject(ObjectType::Camera, "Camera");
    if (ImGui::MenuItem("2D Camera")) {
      addObject(ObjectType::Camera, "2D Camera");
      if (SceneObject *created = lastCreated()) {
        created->camera.use2D = true;
      }
    }
    ImGui::EndMenu();
  }

  // "Lights" is a public create-menu category that ModuPAKs (e.g. the 2D
  // world package) extend, so do not rename it.
  if (ImGui::BeginMenu("Lights")) {
    if (ImGui::MenuItem("Directional Light"))
      addObject(ObjectType::DirectionalLight, "Directional Light");
    if (ImGui::MenuItem("Point Light"))
      addObject(ObjectType::PointLight, "Point Light");
    if (ImGui::MenuItem("Spot Light"))
      addObject(ObjectType::SpotLight, "Spot Light");
    if (ImGui::MenuItem("Area Light"))
      addObject(ObjectType::AreaLight, "Area Light");
    if (ImGui::MenuItem("Reflection Cast"))
      addObject(ObjectType::ReflectionCast, "Reflection Cast");
    if (has2DWorldPackage()) {
      ImGui::Separator();
      if (ImGui::MenuItem("2D Point Light"))
        addObject(ObjectType::Light2D, "2D Point Light");
      if (ImGui::MenuItem("2D Spot Light")) {
        addObject(ObjectType::Light2D, "2D Spot Light");
        if (SceneObject *created = lastCreated()) {
          created->light2D.type = Light2DType::Spot;
        }
      }
      if (ImGui::MenuItem("2D Freeform Light")) {
        addObject(ObjectType::Light2D, "2D Freeform Light");
        if (SceneObject *created = lastCreated()) {
          created->light2D.type = Light2DType::Freeform;
          created->light2D.shapePoints = {
              glm::vec2(-2.0f, -1.5f), glm::vec2(2.0f, -1.5f),
              glm::vec2(2.5f, 1.0f), glm::vec2(0.0f, 2.5f),
              glm::vec2(-2.5f, 1.0f)};
        }
      }
      if (ImGui::MenuItem("2D Global Light")) {
        addObject(ObjectType::Light2D, "2D Global Light");
        if (SceneObject *created = lastCreated()) {
          created->light2D.type = Light2DType::Global;
          created->light2D.intensity = 0.35f;
          created->light2D.color = glm::vec4(0.45f, 0.52f, 0.72f, 1.0f);
        }
      }
      if (ImGui::MenuItem("2D Shadow Caster"))
        addObject(ObjectType::ShadowCaster2D, "2D Shadow Caster");
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Audio")) {
    if (ImGui::MenuItem("Audio Source")) {
      addObject(ObjectType::Empty, "Audio Source");
      if (SceneObject *created = lastCreated()) {
        created->hasAudioSource = true;
        created->audioSource = AudioSourceComponent{};
      }
    }
    if (ImGui::MenuItem("Audio Reverb Zone")) {
      addObject(ObjectType::Empty, "Reverb Zone");
      if (SceneObject *created = lastCreated()) {
        created->hasReverbZone = true;
        created->reverbZone = ReverbZoneComponent{};
        created->reverbZone.boxSize = glm::max(created->scale, glm::vec3(1.0f));
      }
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Gameplay")) {
    if (ImGui::MenuItem("Player Controller")) {
      addObject(ObjectType::Capsule, "Player Controller");
      if (SceneObject *created = lastCreated()) {
        created->hasPlayerController = true;
        created->playerController = PlayerControllerComponent{};
        created->hasCollider = true;
        created->collider.type = ColliderType::Capsule;
        created->collider.boxSize =
            glm::vec3(created->playerController.radius * 2.0f,
                      created->playerController.height,
                      created->playerController.radius * 2.0f);
        created->collider.convex = true;
        created->hasRigidbody = true;
        created->rigidbody.enabled = true;
        created->rigidbody.useGravity = true;
        created->rigidbody.isKinematic = false;
        created->rigidbody.lockRotationX = true;
        created->rigidbody.lockRotationY = false;
        created->rigidbody.lockRotationZ = true;
        created->scale = glm::vec3(created->playerController.radius * 2.0f,
                                   created->playerController.height,
                                   created->playerController.radius * 2.0f);
        syncLocalTransform(*created);
      }
    }
    if (ImGui::MenuItem("Spawn Point"))
      createTaggedMarker("Spawn Point", "SpawnPoint");
    if (ImGui::MenuItem("Trigger Zone"))
      createTaggedMarker("Trigger Zone", "TriggerZone");
    if (ImGui::MenuItem("Checkpoint"))
      createTaggedMarker("Checkpoint", "Checkpoint");
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("AI")) {
    if (ImGui::MenuItem("AI Agent")) {
      addObject(ObjectType::Capsule, "AI Agent");
      if (SceneObject *created = lastCreated()) {
        created->hasAIAgent = true;
        created->aiAgent = AIAgentComponent{};
        created->aiAgent.destination = created->position;
      }
    }
    if (ImGui::MenuItem("Nav Area")) {
      addObject(ObjectType::Plane, "Nav Area");
      if (SceneObject *created = lastCreated()) {
        created->hasGroundBakedType = true;
        created->groundBakedType = GroundBakedTypeComponent{};
      }
    }
    if (ImGui::MenuItem("Off-Mesh Link")) {
      addObject(ObjectType::Empty, "Off-Mesh Link");
      if (SceneObject *created = lastCreated()) {
        created->hasOffMeshLink = true;
        created->offMeshLink = OffMeshLinkComponent{};
        created->offMeshLink.startPoint = created->position;
        created->offMeshLink.endPoint =
            created->position + glm::vec3(2.0f, 0.0f, 0.0f);
      }
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Video")) {
    if (ImGui::MenuItem("Video Player Plane")) {
      addObject(ObjectType::Plane, "Video Player Plane");
      if (SceneObject *created = lastCreated()) {
        created->hasVideoPlayer = true;
        created->videoPlayer = VideoPlayerComponent{};
      }
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Environment / Volumes")) {
    if (ImGui::MenuItem("ModuVolume"))
      addObject(ObjectType::PostFXNode, "ModuVolume");
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("UI")) {
    if (ImGui::MenuItem("Canvas"))
      addObject(ObjectType::Canvas, "Canvas");
    if (ImGui::MenuItem("UI Image"))
      createUIWithCanvas(ObjectType::UIImage, "UI Image");
    if (ImGui::MenuItem("UI Slider"))
      createUIWithCanvas(ObjectType::UISlider, "UI Slider");
    if (ImGui::MenuItem("UI Button"))
      createUIWithCanvas(ObjectType::UIButton, "UI Button");
    if (ImGui::MenuItem("UI Text"))
      createUIWithCanvas(ObjectType::UIText, "UI Text");
    if (has2DWorldPackage() && ImGui::MenuItem("Sprite2D"))
      createUIWithCanvas(ObjectType::Sprite2D, "Sprite2D");
    ImGui::EndMenu();
  }
}

#pragma endregion
