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

  if (ImGui::BeginMainMenuBar()) {
    const EditorChromeMetrics &chrome = getEditorChromeMetrics(uiChromeScale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, chrome.menuItemSpacing);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, chrome.menuFramePadding);
    ImGui::SetWindowFontScale(chrome.fontScale);
    const bool project25DPipeline = isProject25DPipeline();
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
      if (!project25DPipeline) {
        return;
      }
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
        created->rig25DNode.nodeId =
            isRoot ? -1 : allocateRig25DNodeId();
        created->rig25DNode.nodeName = isRoot ? std::string() : created->name;
        created->localPosition = created->position;
        created->localRotation = NormalizeEulerDegrees(created->rotation);
        created->localScale = created->scale;
        created->localInitialized = true;
        EnsureInspectorComponentMetadata(*created);
        projectManager.currentProject.hasUnsavedChanges = true;
      }
    };

    if (ImGui::BeginMenu("File")) {
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
      if (ImGui::MenuItem("Build Settings...")) {
        showBuildSettings = true;
      }
      if (ImGui::BeginMenu("ModuPAK")) {
        if (ImGui::MenuItem("Export Into ModuPAK...")) {
          openModuPakExportDialog(fileBrowser.selectedFiles);
        }
        if (ImGui::MenuItem("Import ModuPAK...")) {
          openModuPakImportDialog();
        }
        if (ImGui::MenuItem("Import ModuOBJ...")) {
          openModuObjImportDialog();
        }
        ImGui::EndMenu();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Close Project")) {
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

    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z", false, false)) {
      }
      if (ImGui::MenuItem("Redo", "Ctrl+Y", false, false)) {
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
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
      bool prevRegistryPackages = showRegistryPackagesWindow;
      ImGui::MenuItem("Modupak Manager", nullptr, &showRegistryPackagesWindow);
      if (prevRegistryPackages != showRegistryPackagesWindow) {
        saveEditorUserSettings();
      }
      bool prevProfilerWindow = showGameProfilerWindow;
      ImGui::MenuItem("Profiler", nullptr, &showGameProfilerWindow);
      if (prevProfilerWindow != showGameProfilerWindow) {
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
      if (!scriptEditorWindows.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("Scripted Windows");
        for (auto &window : scriptEditorWindows) {
          ImGui::MenuItem(window.label.c_str(), nullptr, &window.open);
        }
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Fullscreen Viewport", "F11", viewportFullscreen)) {
        viewportFullscreen = !viewportFullscreen;
      }
      ImGui::EndMenu();
    }

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

    if (ImGui::BeginMenu("Scripts")) {
      auto toggleSpec = [&](bool enabled) {
        if (specMode == enabled)
          return;
        if (enabled && !physics.isReady() && !physics.init()) {
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
            physics.onPlayStart(sceneObjects);
            audio.setPrefer2DSpatialAudio(isProject2DPipeline() || uiWorldMode);
            audio.onPlayStart(sceneObjects);
          } else {
            physics.onPlayStop();
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

    if (ImGui::BeginMenu("Create")) {
      if (ImGui::MenuItem("Empty"))
        addObject(ObjectType::Empty, "Empty");
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
      if (ImGui::MenuItem("2.5D Sprite"))
        addObject(ObjectType::Sprite25D, "2.5D Sprite");
      if (ImGui::MenuItem("Mirror"))
        addObject(ObjectType::Mirror, "Mirror");
      if (project25DPipeline && ImGui::BeginMenu("2.5D Rig")) {
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
      if (ImGui::MenuItem("Camera"))
        addObject(ObjectType::Camera, "Camera");
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
      if (ImGui::MenuItem("ModuVolume"))
        addObject(ObjectType::PostFXNode, "ModuVolume");
      if (ImGui::MenuItem("Audio Reverb Zone")) {
        addObject(ObjectType::Empty, "Reverb Zone");
        if (!sceneObjects.empty()) {
          sceneObjects.back().hasReverbZone = true;
          sceneObjects.back().reverbZone = ReverbZoneComponent{};
          sceneObjects.back().reverbZone.boxSize =
              glm::max(sceneObjects.back().scale, glm::vec3(1.0f));
        }
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("About")) {
        logToConsole("Modularity Engine - V6.8\nThis build may still have "
                     "issues,\n\nif you'd like to report any bugs or missing "
                     "features, feel free to contact us!");
      }
      ImGui::EndMenu();
    }

    renderPlayControlsBar();

    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleVar(2);
    ImGui::EndMainMenuBar();
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

#pragma endregion
