#pragma once

#include "Common.h"
#include "SceneObject.h"
#include "Camera.h"
#include "Rendering.h"
#include "ProjectManager.h"
#include "EditorUI.h"
#include "MeshBuilder.h"
#include "ScriptCompiler.h"
#include "ScriptRuntime.h"
#include "PhysicsSystem.h"
#include "AudioSystem.h"
#include "PackageManager.h"
#include "ManagedScriptRuntime.h"
#include "ThirdParty/ImGuiColorTextEdit/TextEditor.h"
#include "../include/Window/Window.h"
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

void window_size_callback(GLFWwindow* window, int width, int height);
fs::path resolveScriptsConfigPath(const Project& project);

class Engine {
private:
    Window window;
    GLFWwindow* editorWindow = nullptr;
    Renderer renderer;
    Camera camera;
    ViewportController viewportController;
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    bool cursorLocked = false; // true only while holding right mouse for freelook
    int viewportWidth = 800;
    int viewportHeight = 600;
    bool gizmoHistoryCaptured = false;
    // Standalone material inspection cache
    std::string inspectedMaterialPath;
    MaterialProperties inspectedMaterial;
    std::string inspectedAlbedo;
    std::string inspectedOverlay;
    std::string inspectedNormal;
    std::string inspectedVertShader;
    std::string inspectedFragShader;
    bool inspectedUseOverlay = false;
    bool inspectedMaterialValid = false;
    struct SceneSnapshot {
        std::vector<SceneObject> objects;
        std::vector<int> selectedIds;
        int nextId = 0;
    };
    std::vector<SceneSnapshot> undoStack;
    std::vector<SceneSnapshot> redoStack;

    std::vector<SceneObject> sceneObjects;
    int selectedObjectId = -1; // primary selection (last)
    std::vector<int> selectedObjectIds; // multi-select
    int nextObjectId = 0;

    // Gizmo state
    ImGuizmo::OPERATION mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE mCurrentGizmoMode = ImGuizmo::LOCAL;
    bool useSnap = false;
    float snapValue[3] = { 0.5f, 0.5f, 0.5f };
    float rotationSnapValue = 15.0f;

    FileBrowser fileBrowser;
    bool viewportFullscreen = false;
    bool showHierarchy = true;
    bool showInspector = true;
    bool showFileBrowser = true;
    bool showConsole = true;
    bool showProjectBrowser = true;  // Now merged into file browser
    bool showMeshBuilder = false;
    bool showBuildSettings = false;
    bool showStyleEditor = false;
    bool showScriptingWindow = false;
    bool firstFrame = true;
    bool playerMode = false;
    bool autoStartRequested = false;
    bool autoStartPlayerMode = false;
    std::string autoStartProjectPath;
    std::string autoStartSceneName;
    std::vector<std::string> consoleLog;
    int draggedObjectId = -1;

    ProjectManager projectManager;
    PackageManager packageManager;
    bool showLauncher = true;
    bool showNewSceneDialog = false;
    bool showSaveSceneAsDialog = false;
    char newSceneName[128] = "";
    char saveSceneAsName[128] = "";
    struct ScriptEditorWindowEntry {
        fs::path binaryPath;
        std::string label;
        bool open = false;
    };
    std::vector<ScriptEditorWindowEntry> scriptEditorWindows;
    bool scriptEditorWindowsDirty = true;
    bool rendererInitialized = false;
    
    bool showImportOBJDialog = false;
    bool showImportModelDialog = false;  // For Assimp models
    std::string pendingOBJPath;
    std::string pendingModelPath;  // For Assimp models
    char importOBJName[128] = "";
    char importModelName[128] = "";  // For Assimp models
    
    char fileBrowserSearch[256] = "";
    float fileBrowserIconScale = 1.0f;  // 0.5 to 2.0 range
    float fileBrowserSidebarWidth = 220.0f;
    bool showFileBrowserSidebar = true;
    std::vector<fs::path> fileBrowserFavorites;
    std::string uiStylePresetName = "Current";
    enum class UIAnimationMode {
        Off = 0,
        Snappy = 1,
        Fluid = 2
    };
    enum class WorkspaceMode {
        Default = 0,
        Animation = 1,
        Scripting = 2
    };
    UIAnimationMode uiAnimationMode = UIAnimationMode::Off;
    WorkspaceMode currentWorkspace = WorkspaceMode::Default;
    bool workspaceLayoutDirty = false;
    bool pendingWorkspaceReload = false;
    fs::path pendingWorkspaceIniPath;
    bool editorSettingsDirty = false;
    bool showEnvironmentWindow = true;
    bool showCameraWindow = true;
    bool showAnimationWindow = false;
    int animationTargetId = -1;
    int animationSelectedKey = -1;
    float animationCurrentTime = 0.0f;
    bool animationIsPlaying = false;
    float animationLastAppliedTime = -1.0f;
    bool hierarchyShowTexturePreview = false;
    bool hierarchyPreviewNearest = false;
    std::unordered_map<std::string, bool> texturePreviewFilterOverrides;
    bool audioPreviewLoop = false;
    bool audioPreviewAutoPlay = false;
    std::string audioPreviewSelectedPath;
    bool isPlaying = false;
    bool isPaused = false;
    bool showViewOutput = true;
    bool showSceneGizmos = true;
    bool showGameViewport = true;
    int previewCameraId = -1;
    bool gameViewCursorLocked = false;
    bool gameViewportFocused = false;
    bool showGameProfiler = true;
    bool showCanvasOverlay = false;
    bool showUIWorldGrid = true;
    bool showSceneGrid3D = false;
    int gameViewportResolutionIndex = 0;
    int gameViewportCustomWidth = 1920;
    int gameViewportCustomHeight = 1080;
    float gameViewportZoom = 1.0f;
    bool gameViewportAutoFit = true;
    int activePlayerId = -1;
    MeshBuilder meshBuilder;
    char meshBuilderPath[260] = "";
    char meshBuilderFaceInput[128] = "";
    bool meshEditMode = false;
    bool meshEditLoaded = false;
    bool meshEditDirty = false;
    bool meshEditExtrudeMode = false;
    std::string meshEditPath;
    RawMeshAsset meshEditAsset;
    std::vector<int> meshEditSelectedVertices;
    std::vector<int> meshEditSelectedEdges; // indices into generated edge list
    std::vector<int> meshEditSelectedFaces; // indices into mesh faces
    struct UIAnimationState {
        float hover = 0.0f;
        float active = 0.0f;
        float sliderValue = 0.0f;
        bool initialized = false;
    };
    std::unordered_map<int, UIAnimationState> uiAnimationStates;
    struct UIWorldCamera2D {
        glm::vec2 position = glm::vec2(0.0f);
        float zoom = 100.0f; // pixels per world unit
        glm::vec2 viewportSize = glm::vec2(0.0f);

        glm::vec2 WorldToScreen(const glm::vec2& world) const {
            return glm::vec2(
                (world.x - position.x) * zoom + viewportSize.x * 0.5f,
                (position.y - world.y) * zoom + viewportSize.y * 0.5f
            );
        }

        glm::vec2 ScreenToWorld(const glm::vec2& screen) const {
            return glm::vec2(
                (screen.x - viewportSize.x * 0.5f) / zoom + position.x,
                position.y - (screen.y - viewportSize.y * 0.5f) / zoom
            );
        }
    };
    bool uiWorldMode = false;
    bool uiWorldPanning = false;
    UIWorldCamera2D uiWorldCamera;
    bool consoleWrapText = true;
    enum class MeshEditSelectionMode { Vertex = 0, Edge = 1, Face = 2 };
    MeshEditSelectionMode meshEditSelectionMode = MeshEditSelectionMode::Vertex;
    ScriptCompiler scriptCompiler;
    ScriptRuntime scriptRuntime;
    ManagedScriptRuntime managedRuntime;
    PhysicsSystem physics;
    AudioSystem audio;
    bool showCompilePopup = false;
    bool compilePopupOpened = false;
    double compilePopupHideTime = 0.0;
    bool lastCompileSuccess = false;
    std::string lastCompileStatus;
    std::string lastCompileLog;
    float compileProgress = 0.0f;
    std::string compileStage;
    enum class BuildPlatform {
        Windows = 0,
        Linux = 1,
        Android = 2
    };
    struct BuildSceneEntry {
        std::string name;
        bool enabled = true;
    };
    struct BuildSettings {
        BuildPlatform platform = BuildPlatform::Windows;
        std::string architecture = "x86_64";
        bool developmentBuild = false;
        bool autoConnectProfiler = false;
        bool scriptDebugging = false;
        bool deepProfiling = false;
        bool scriptsOnlyBuild = false;
        bool serverBuild = false;
        std::string compressionMethod = "Default";
        std::vector<BuildSceneEntry> scenes;
    };
    BuildSettings buildSettings;
    int buildSettingsSelectedIndex = -1;
    bool buildSettingsDirty = false;
    struct ExportJobResult {
        bool success = false;
        std::string message;
        fs::path outputDir;
    };
    struct ExportJobState {
        bool active = false;
        bool done = false;
        bool success = false;
        bool cancelled = false;
        float progress = 0.0f;
        std::string status;
        std::string log;
        fs::path outputDir;
        bool runAfter = false;
        std::future<ExportJobResult> future;
    };
    ExportJobState exportJob;
    std::atomic<bool> exportCancelRequested = false;
    std::mutex exportMutex;
    bool showExportDialog = false;
    bool exportRunAfter = false;
    char exportOutputPath[512] = "";
    struct ScriptCompileJobResult {
        bool success = false;
        bool isManaged = false;
        fs::path scriptPath;
        fs::path binaryPath;
        std::string compiledSource;
        std::string compileLog;
        std::string linkLog;
        std::string error;
    };
    std::atomic<bool> compileInProgress = false;
    std::atomic<bool> compileResultReady = false;
    std::thread compileWorker;
    std::mutex compileMutex;
    ScriptCompileJobResult compileResult;
    std::unordered_map<std::string, fs::file_time_type> scriptLastAutoCompileTime;
    std::deque<fs::path> autoCompileQueue;
    std::unordered_set<std::string> autoCompileQueued;
    bool managedAutoCompileQueued = false;
    double scriptAutoCompileLastCheck = 0.0;
    double scriptAutoCompileInterval = 0.5;
    struct ProjectLoadResult {
        bool success = false;
        Project project;
        std::string error;
        std::string path;
    };
    bool projectLoadInProgress = false;
    double projectLoadStartTime = 0.0;
    std::string projectLoadPath;
    std::future<ProjectLoadResult> projectLoadFuture;
    bool sceneLoadInProgress = false;
    float sceneLoadProgress = 0.0f;
    std::string sceneLoadStatus;
    std::string sceneLoadSceneName;
    std::vector<SceneObject> sceneLoadObjects;
    std::vector<size_t> sceneLoadAssetIndices;
    size_t sceneLoadAssetsDone = 0;
    int sceneLoadNextId = 0;
    int sceneLoadVersion = 9;
    float sceneLoadTimeOfDay = -1.0f;
    bool specMode = false;
    bool testMode = false;
    bool collisionWireframe = false;
    bool fpsCapEnabled = false;
    float fpsCap = 120.0f;
    struct UIStylePreset {
        std::string name;
        ImGuiStyle style;
        bool builtin = false;
    };
    std::vector<UIStylePreset> uiStylePresets;
    int uiStylePresetIndex = 0;
    struct ScriptEditorState {
        fs::path filePath;
        std::string buffer;
        bool dirty = false;
        bool autoCompileOnSave = true;
        bool hasWriteTime = false;
        fs::file_time_type lastWriteTime;
    };
    ScriptEditorState scriptEditorState;
    std::vector<fs::path> scriptingFileList;
    std::vector<std::string> scriptingCompletions;
    TextEditor scriptTextEditor;
    bool scriptTextEditorReady = false;
    char scriptingFilter[128] = "";
    bool scriptingFilesDirty = true;
    // Private methods
    SceneObject* getSelectedObject();
    glm::vec3 getSelectionCenterWorld(bool worldSpace) const;
    void setPrimarySelection(int id, bool additive = false);
    void clearSelection();
    static void DecomposeMatrix(const glm::mat4& matrix, glm::vec3& pos, glm::vec3& rot, glm::vec3& scale);
    static glm::mat4 ComposeTransform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale);
    static glm::mat4 ComposeTransform(const glm::vec3& position, const glm::vec3& rotationDeg, const glm::vec3& scale);
    void updateHierarchyWorldTransforms();
    void updateLocalFromWorld(SceneObject& obj, const glm::vec3& parentPos, const glm::quat& parentRot, const glm::vec3& parentScale);
    void initializeLocalTransformsFromWorld(int sceneVersion);
    
    void importOBJToScene(const std::string& filepath, const std::string& objectName);
    void importModelToScene(const std::string& filepath, const std::string& objectName);  // Assimp import
    void convertModelToRawMesh(const std::string& filepath);
    void createRMeshPrimitive(const std::string& primitiveName);
    bool ensureMeshEditTarget(SceneObject* obj);
    bool syncMeshEditToGPU(SceneObject* obj);
    bool saveMeshEditAsset(std::string& error);
    void handleKeyboardShortcuts();
    void OpenProjectPath(const std::string& path);
    
    // UI rendering methods
    void renderLauncher();
    void renderNewProjectDialog();
    void renderOpenProjectDialog();
    void renderMainMenuBar();
    void renderPlayControlsBar();
    void renderEnvironmentWindow();
    void renderCameraWindow();
    void renderAnimationWindow();
    void renderHierarchyPanel();
    void renderObjectNode(SceneObject& obj, const std::string& filter,
                          std::vector<bool>& ancestorHasNext, bool isLast, int depth);
    void renderFileBrowserPanel();
    void renderMeshBuilderPanel();
    void renderInspectorPanel();
    void renderConsolePanel();
    void renderViewport();
    void renderPlayerViewport();
    void renderGameViewportWindow();
    void renderBuildSettingsWindow();
    void renderScriptingWindow();
    void renderDialogs();
    void updateCompileJob();
    void renderProjectBrowserPanel();
    void renderScriptEditorWindows();
    void refreshScriptEditorWindows();
    void refreshScriptingFileList();
    Camera makeCameraFromObject(const SceneObject& obj) const;
    void compileScriptFile(const fs::path& scriptPath);
    void updateAutoCompileScripts();
    void processAutoCompileQueue();
    void queueAutoCompile(const fs::path& scriptPath, const fs::file_time_type& sourceTime);
    void startProjectLoad(const std::string& path);
    void pollProjectLoad();
    void finishProjectLoad(ProjectLoadResult& result);
    void beginDeferredSceneLoad(const std::string& sceneName);
    void pollSceneLoad();
    void finalizeDeferredSceneLoad();
    void syncPlayerCamera();
    void updateScripts(float delta);
    void updatePlayerController(float delta);
    void updateRigidbody2D(float delta);
    void updateCameraFollow2D(float delta);
    void updateSkeletalAnimations(float delta);
    void updateSkinningMatrices();
    void rebuildSkeletalBindings();
    void initUIStylePresets();
    int findUIStylePreset(const std::string& name) const;
    const UIStylePreset* getUIStylePreset(const std::string& name) const;
    void registerUIStylePreset(const std::string& name, const ImGuiStyle& style, bool replace);
    bool applyUIStylePresetByName(const std::string& name);
    void applyWorkspacePreset(WorkspaceMode mode, bool rebuildLayout);
    void buildWorkspaceLayout(WorkspaceMode mode);
    fs::path getEditorUserSettingsPath() const;
    fs::path getEditorLayoutPath() const;
    fs::path getWorkspaceLayoutPath(WorkspaceMode mode) const;
    void loadEditorUserSettings();
    void saveEditorUserSettings() const;
    void exportEditorThemeLayout();
    void resetBuildSettings();
    void loadBuildSettings();
    void saveBuildSettings();
    bool addSceneToBuildSettings(const std::string& sceneName, bool enabled);
    void loadAutoStartConfig();
    void applyAutoStartMode();
    void startExportBuild(const fs::path& outputDir, bool runAfter);
    void pollExportBuild();
    
    void renderFileBrowserToolbar();
    void renderFileBrowserBreadcrumb();
    void renderFileBrowserGridView();
    void renderFileBrowserListView();
    void renderFileContextMenu(const fs::directory_entry& entry);
    void handleFileDoubleClick(const fs::directory_entry& entry);
    void openScriptInEditor(const fs::path& path);
    ImVec4 getFileCategoryColor(FileCategory category) const;
    const char* getFileCategoryIconText(FileCategory category) const;
    
    // Project/scene management
    void createNewProject(const char* name, const char* location);
    void loadRecentScenes();
    void saveCurrentScene();
    void loadScene(const std::string& sceneName);
    void createNewScene(const std::string& sceneName);
    
    // Scene object management
    void addObject(ObjectType type, const std::string& baseName);
    void duplicateSelected();
    void deleteSelected();
    void setParent(int childId, int parentId);
    void loadMaterialFromFile(SceneObject& obj);
    void saveMaterialToFile(const SceneObject& obj);
    void recordState(const char* reason = "");
    void undo();
    void redo();
    
    // Console/logging
    void addConsoleMessage(const std::string& message, ConsoleMessageType type);
    void logToConsole(const std::string& message);

    // Material helpers
    bool loadMaterialData(const std::string& path, MaterialProperties& props,
                          std::string& albedo, std::string& overlay,
                          std::string& normal, bool& useOverlay,
                          std::string* vertexShaderOut = nullptr,
                          std::string* fragmentShaderOut = nullptr);
    bool saveMaterialData(const std::string& path, const MaterialProperties& props,
                          const std::string& albedo, const std::string& overlay,
                          const std::string& normal, bool useOverlay,
                          const std::string& vertexShader,
                          const std::string& fragmentShader);
    
    // ImGui setup
    void setupImGui();
    bool initRenderer();

public:
    Engine() = default;

    bool init();
    void run();
    void shutdown();
    SceneObject* findObjectByName(const std::string& name);
    SceneObject* findObjectById(int id);
    fs::path resolveScriptBinary(const fs::path& sourcePath);
    fs::path resolveManagedAssembly(const fs::path& sourcePath);
    fs::path getManagedProjectPath() const;
    fs::path getManagedOutputDll() const;
    void compileManagedScripts();
    void markProjectDirty();
    // Script-accessible logging wrapper
    void addConsoleMessageFromScript(const std::string& message, ConsoleMessageType type);
    // Script-accessible physics helpers
    bool setRigidbodyVelocityFromScript(int id, const glm::vec3& velocity);
    bool getRigidbodyVelocityFromScript(int id, glm::vec3& outVelocity);
    bool setRigidbodyAngularVelocityFromScript(int id, const glm::vec3& velocity);
    bool getRigidbodyAngularVelocityFromScript(int id, glm::vec3& outVelocity);
    bool teleportPhysicsActorFromScript(int id, const glm::vec3& position, const glm::vec3& rotationDeg);
    bool addRigidbodyForceFromScript(int id, const glm::vec3& force);
    bool addRigidbodyImpulseFromScript(int id, const glm::vec3& impulse);
    bool addRigidbodyTorqueFromScript(int id, const glm::vec3& torque);
    bool addRigidbodyAngularImpulseFromScript(int id, const glm::vec3& impulse);
    bool setRigidbodyYawFromScript(int id, float yawDegrees);
    bool raycastClosestFromScript(const glm::vec3& origin, const glm::vec3& dir, float distance,
                                  int ignoreId, glm::vec3* hitPos, glm::vec3* hitNormal,
                                  float* hitDistance) const;
    // Audio control exposed to scripts
    bool playAudioFromScript(int id);
    bool stopAudioFromScript(int id);
    bool setAudioLoopFromScript(int id, bool loop);
    bool setAudioVolumeFromScript(int id, float volume);
    bool setAudioClipFromScript(int id, const std::string& path);
    void syncLocalTransform(SceneObject& obj);
    const std::vector<UIStylePreset>& getUIStylePresets() const { return uiStylePresets; }
    void registerUIStylePresetFromScript(const std::string& name, const ImGuiStyle& style, bool replace = false);
    void setFrameRateCapFromScript(bool enabled, float cap);
};
