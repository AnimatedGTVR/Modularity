#pragma once
#include "Common.h"
#include "SceneObject.h"
#include "Camera.h"
#include "Rendering.h"
#include "Render25D/TMOpenGLRenderer.h"
#include "Render25D/TMSceneBuilder.h"
#include "Render25D/TMRenderer.h"
#include "Lighting2D.h"
#include "AssemblageRuntime.h"
#include "ProjectManager.h"
#include "EditorUI.h"
#include "MeshBuilder.h"
#include "MapCarve.h"
#include "ScriptCompiler.h"
#include "ScriptDiagnostics.h"
#include "ScriptHistory.h"
#include "ScriptLanguageService.h"
#include "ScriptRuntime.h"
#include "PhysicsSystem.h"
#include "PhysicsBackendFactory.h"
#include "AudioSystem.h"
#include "Network/NetworkSession.h"
#include "PackageManager.h"
#include "ModuPak.h"
#include "ManagedScriptRuntime.h"
#include "Profiler.h"
#include "SpritesheetFormat.h"
#include "VideoPlayer.h"
#if !MODULARITY_RUNTIME_ONLY
#include "ThirdParty/ImGuiColorTextEdit/TextEditor.h"
#include "Nebula/NebulaLightmap.h"
#endif
#include "Vulkan/VulkanRenderer.h"
#include "XR/XRSystem.h"
#include "../include/Window/Window.h"
#include <array>
#include <unordered_map>
#include <random>
#include <unordered_set>
#include <atomic>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <cstdint>

void window_size_callback(GLFWwindow* window, int width, int height);
fs::path resolveScriptsConfigPath(const Project& project);

struct PixelSpriteLayerState {
    std::string name = "Layer_0";
    bool visible = true;
    std::vector<unsigned char> pixels;
};

enum class ViewportDisplayMode {
    Stretch = 0,
    Fit = 1,
    Fill = 2,
    IntegerScale = 3
};

enum class ViewportToolbarCorner {
    BottomLeft = 0,
    BottomRight = 1,
    TopLeft = 2,
    TopRight = 3
};

enum class SpriteSheetImportTarget {
    UIImage = 0,
    Sprite25D = 1,
    Sprite2D = 2
};

enum class ScriptScaffoldKind {
    ModuCpp = 0,
    Cpp = 1,
    C = 2,
    CSharp = 3,
    ModuMako = 4
};

class Engine {
    friend void window_size_callback(GLFWwindow* window, int width, int height);
private:
    Window window;
    GLFWwindow* editorWindow = nullptr;
    Modularity::GraphicsBackend graphicsBackend = Modularity::GraphicsBackend::OpenGL;
    Renderer renderer;
    // A ModuVolume scoped to part of the 2D draw order needs its work split
    // around a slice of the draw list rather than run once at the end, so the
    // one request type carries which half of that it is.
    enum class OverlayPostFxKind {
        FullRegion = 0, // process everything drawn into the region so far
        BandBegin = 1,  // redirect subsequent drawing into the isolated band layer
        BandEnd = 2     // process the band layer and composite it back
    };
    struct OverlayPostFxRequest {
        Engine* engine = nullptr;
        Camera camera;
        ImVec2 min = ImVec2(0.0f, 0.0f);
        ImVec2 max = ImVec2(0.0f, 0.0f);
        bool allowHistory = false;
        OverlayPostFxKind kind = OverlayPostFxKind::FullRegion;
    };
    // Deque on purpose: ImGui holds a pointer to each request until the frame is
    // rendered, and deque push_back does not invalidate existing elements.
    std::deque<OverlayPostFxRequest> overlayPostFxRequests;
    static void applyOverlayPostFxCallback(const ImDrawList* parentList,
                                           const ImDrawCmd* command);
    void queueOverlayPostFx(ImDrawList* drawList, const Camera& effectCamera,
                            const ImVec2& min, const ImVec2& max,
                            bool allowHistory,
                            OverlayPostFxKind kind = OverlayPostFxKind::FullRegion);
    Modularity::Render25D::TMRenderer tmRenderer;
    Modularity::Render25D::TMOpenGLRenderer tmOpenGLRenderer;
    Modularity::Render25D::TMSceneBuilder tmSceneBuilder;
    Lighting2DRenderer lighting2DRenderer;
    std::unique_ptr<Modularity::VulkanRenderer> vulkanRenderer;
    // OpenXR. Inert unless the project turns it on: with XR disabled nothing here
    // allocates, no loader is opened, and every render path below behaves exactly
    // as it did before OpenXR existed. Sits beside the renderers rather than
    // inside one because it drives the frame, it does not draw.
    Modularity::XR::XRSystem xrSystem;
    // Startup is attempted once per project load rather than every frame: a
    // machine with no runtime would otherwise pay a failed dlopen every frame.
    bool xrStartupAttempted = false;
    std::string xrStartupError;
    // Set for the frame when the scene was submitted to the XR compositor, so the
    // flat present path knows to stand down.
    bool xrFrameSubmitted = false;
    Camera camera;
    ViewportController viewportController;
    float deltaTime = 0.0f;
    // Gameplay time, i.e. deltaTime scaled by gameTimeScale. Editor UI and the
    // editor camera keep using the unscaled deltaTime so the tool stays
    // responsive while the game runs slowed down or sped up; anything that
    // simulates the game (scripts, physics, animation, video, AI) uses this.
    // Networking is deliberately excluded - slowing it would desync peers.
    float gameDeltaTime = 0.0f;
    float gameTimeScale = 1.0f;
    static constexpr float kMinGameTimeScale = 0.0f;
    static constexpr float kMaxGameTimeScale = 8.0f;
    float lastFrame = 0.0f;
    // Rendering is suspended while the window is minimized or reports a 0x0
    // drawable. Events keep pumping; only GPU work stops. See the suspension
    // block in Engine::run (Windows minimize/restore freeze).
    bool renderSuspended = false;
    bool renderResumeLogPending = false;
    // Env-gated window lifecycle tracing: MODULARITY_WINDOW_LOG=1.
    // Primarily for the manual Windows minimize/restore verification pass.
    void logWindowLifecycle(const char* event, bool iconified, int fbWidth, int fbHeight) const;
    bool cursorLocked = false; // true only while holding right mouse for freelook
    double viewportMoveSpeedHudTime = -1000.0;
    float viewportMoveSpeedHudValue = 5.0f;
    bool viewportFocusActive = false;
    double viewportFocusStartTime = 0.0;
    double viewportFocusDuration = 0.55;
    glm::vec3 viewportFocusStartPosition = glm::vec3(0.0f);
    glm::vec3 viewportFocusTargetPosition = glm::vec3(0.0f);
    glm::vec3 viewportFocusStartFront = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 viewportFocusTargetFront = glm::vec3(0.0f, 0.0f, -1.0f);
    int viewportWidth = 800;
    int viewportHeight = 600;
    bool gizmoHistoryCaptured = false;
    // Baseline for an in-progress gizmo drag. Deltas are measured against the matrix
    // the drag started from and applied to these pristine transforms, so a drag of any
    // length costs exactly one compose/decompose round-trip instead of one per frame
    // (which used to accumulate float error and visibly drift the object).
    struct GizmoDragTransform {
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 rotation = glm::vec3(0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
    };
    std::unordered_map<int, GizmoDragTransform> gizmoDragStartTransforms;
    glm::mat4 gizmoDragStartModel = glm::mat4(1.0f);
    glm::mat4 gizmoDragLiveModel = glm::mat4(1.0f);
    bool gizmoDragActive = false;
    bool worldUiGizmoHistoryCaptured = false;
    bool gameUiGizmoHistoryCaptured = false;
    struct UiRectGizmoSnapshot {
        int objectId = -1;
        glm::vec2 position = glm::vec2(0.0f);
        glm::vec2 size = glm::vec2(0.0f);
        float rotation = 0.0f;
        ImVec2 rectMin = ImVec2(0.0f, 0.0f);
        ImVec2 rectMax = ImVec2(0.0f, 0.0f);
        float textScale = 1.0f;
        float fontSize = 0.0f;
    };
    ImGuizmo::OPERATION worldUiRectGizmoOperation = ImGuizmo::TRANSLATE;
    glm::mat4 worldUiRectGizmoModel = glm::mat4(1.0f);
    std::vector<UiRectGizmoSnapshot> worldUiRectGizmoSnapshots;
    ImVec2 worldUiRectGizmoStartMouse = ImVec2(0.0f, 0.0f);
    ImGuizmo::OPERATION gameUiRectGizmoOperation = ImGuizmo::TRANSLATE;
    glm::mat4 gameUiRectGizmoModel = glm::mat4(1.0f);
    std::vector<UiRectGizmoSnapshot> gameUiRectGizmoSnapshots;
    ImVec2 gameUiRectGizmoStartMouse = ImVec2(0.0f, 0.0f);
    // Standalone material inspection cache
    std::string inspectedMaterialPath;
    MaterialProperties inspectedMaterial;
    std::string inspectedAlbedo;
    std::string inspectedOverlay;
    std::string inspectedNormal;
    std::string inspectedShaderPack;
    std::string inspectedVertShader;
    std::string inspectedFragShader;
    bool inspectedUseOverlay = false;
    bool inspectedMaterialValid = false;
    bool materialColorSamplerActive = false;
    bool materialColorSamplerAwaitMouseRelease = false;
    bool materialColorSamplerHasResult = false;
    std::string materialColorSamplerTargetId;
    glm::vec4 materialColorSamplerResult = glm::vec4(1.0f);
    // One cell changed by a tile edit. Strokes accumulate these so a whole
    // drag is one undo entry holding only the cells it touched.
    struct AssemblageCellDelta {
        int layerId = -1;
        int cellX = 0;
        int cellY = 0;
        uint32_t before = 0;
        uint32_t after = 0;
    };
    struct AssemblageEditRecord {
        std::string assetPath;
        std::string label;
        std::vector<AssemblageCellDelta> cells;
    };
    struct SceneSnapshot {
        // Undo holds two kinds of entry in one stack, so tile edits and object
        // edits undo in the order they were made. A Scene entry deep-copies the
        // object vector as it always has; an AssemblageCells entry leaves that
        // vector empty and carries only the changed cells, which is what keeps a
        // brush stroke from copying an entire map 64 times over.
        enum class Kind { Scene = 0, AssemblageCells = 1 };
        Kind kind = Kind::Scene;
        AssemblageEditRecord assemblageEdit;
        std::vector<SceneObject> objects;
        std::vector<int> selectedIds;
        int nextId = 0;
        bool meshEditMode = false;
        bool meshEditLoaded = false;
        bool meshEditDirty = false;
        bool meshEditExtrudeMode = false;
        bool meshEditAutoUV = true;
        bool meshEditTriangleSelection = false;
        int meshEditAutoObjectId = -1;
        std::string meshEditPath;
        RawMeshAsset meshEditAsset;
        std::vector<int> meshEditSelectedVertices;
        std::vector<int> meshEditSelectedEdges;
        std::vector<int> meshEditSelectedFaces;
        int meshEditActiveMaterialSlot = 0;
        int meshEditSelectionMode = 0;
    };
    struct PlayModeSnapshot {
        SceneSnapshot scene;
        bool hadUnsavedChanges = false;
        bool valid = false;
    };
    std::vector<SceneSnapshot> undoStack;
    std::vector<SceneSnapshot> redoStack;
    PlayModeSnapshot playModeSnapshot;

    std::vector<SceneObject> sceneObjects;
    std::unordered_map<int, std::unique_ptr<VideoPlayer>> videoPlayers;
    // object id -> resolved path whose LoadVideo failed. A failed load is not
    // retried until the path changes (or runtime mode restarts); retrying every
    // frame re-probes the file and floods the log (see syncVideoPlayers).
    std::unordered_map<int, std::string> videoLoadFailedPaths;
    std::unordered_map<int, size_t> sceneObjectIndexById;
    const SceneObject* sceneObjectIndexData = nullptr;
    size_t sceneObjectIndexCount = 0;
    // frame the id->index map was last verified against sceneObjects, so the
    // O(n) validation scan runs once per frame instead of once per lookup
    // (findObjectById is hit per bone per skinned object per frame).
    uint64_t sceneObjectIndexVerifiedFrame = std::numeric_limits<uint64_t>::max();
    struct RuntimeScriptBinding {
        int objectId = -1;
        size_t scriptIndex = 0;
    };
    std::vector<RuntimeScriptBinding> runtimeScriptBindings;
    std::vector<PhysicsCollisionEvent> physicsCollisionEvents;
    uint64_t runtimeScriptBindingsVersion = 1;
    uint64_t runtimeScriptBindingsCachedVersion = 0;
    int selectedObjectId = -1; // primary selection (last)
    std::vector<int> selectedObjectIds; // multi-select
    std::vector<int> hierarchyVisibleOrder;
    int hierarchyRangeAnchorId = -1;
    // Set each frame by renderFileBrowserPanel; routes Delete/Ctrl+C/V/A/D to
    // file operations instead of scene-object operations while the Project
    // panel is focused (read by handleKeyboardShortcuts, one frame behind).
    bool fileBrowserPanelFocused = false;
    std::vector<SceneObject> objectClipboard;
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
    bool consolePanelExpanded = false;
    bool showProjectBrowser = true;  // Now merged into file browser
    bool projectSettingsCompactSidebar = true;
    bool showRegistryPackagesWindow = false;
    bool showModularityDoctorWindow = false;
    bool showMeshBuilder = false;
    bool showBuildSettings = false;
    bool showStyleEditor = false;
    bool showGameProfilerWindow = false;
    bool showScriptingWindow = false;
    bool showModuPakExportDialog = false;
    bool showModuPakImportDialog = false;
    bool showModuObjExportDialog = false;
    bool showModuObjImportDialog = false;
    bool firstFrame = true;
    bool playerMode = false;
    bool autoStartRequested = false;
    bool autoStartPlayerMode = false;
    bool deferInspectorRefresh = false;
    std::string startupProjectPath;
    std::string autoStartBundlePath;
    std::string autoStartProjectPath;
    std::string autoStartSceneName;
    struct ConsoleEntry {
        std::string timestamp;
        std::string message;
        ConsoleMessageType type = ConsoleMessageType::Info;
    };
    std::vector<ConsoleEntry> consoleLog;
    std::string latestErrorMessage;
    std::string latestErrorTimestamp;
    int draggedObjectId = -1;

    ProjectManager projectManager;
    PackageManager packageManager;
    bool showLauncher = true;
    bool launcherIntroStarted = false;
    bool launcherIntroFinished = false;
    bool launcherIntroSoundPlayed = false;
    double launcherIntroStartTime = 0.0;
    bool launcherTransitionActive = false;
    bool launcherTransitionPendingHide = false;
    double launcherTransitionStartTime = 0.0;
    ImVec2 launcherTransitionFocus = ImVec2(0.0f, 0.0f);
    std::string launcherLoadingPreviewPath;
    std::string launcherTransitionProjectName;
    bool launcherWindSoundActive = false;
    bool termsPopupOpened = false;
    enum class LegacySceneSaveChoice {
        Ask = 0,
        KeepLegacy = 1,
        SaveModular = 2
    };
    // Answer to "you are saving while play mode is running". Ask prompts every time;
    // the other two are what a ticked "Don't ask again" remembers. Persisted per
    // project in the editor user settings.
    enum class PlayModeSaveChoice {
        Ask = 0,
        SaveAnyway = 1,
        ExitPlayMode = 2
    };
    enum class PendingScenePostAction {
        None = 0,
        LoadScene = 1,
        CreateNewScene = 2,
        CloseProject = 3
    };
    struct PendingSceneSaveRequest {
        bool active = false;
        std::string destinationSceneName;
        PendingScenePostAction postAction = PendingScenePostAction::None;
        std::string postActionPayload;
        bool allowLegacyUpgradePrompt = true;
    };
    SceneSerializer::Metadata currentSceneSerialization;
    LegacySceneSaveChoice legacySceneSaveChoice = LegacySceneSaveChoice::SaveModular;
    PlayModeSaveChoice playModeSaveChoice = PlayModeSaveChoice::Ask;
    PendingSceneSaveRequest pendingSceneSaveRequest;
    bool showLegacySceneLayoutDialog = false;
    bool legacySceneLayoutDialogOpened = false;
    bool showPlayModeSaveDialog = false;
    bool playModeSaveDialogOpened = false;
    bool playModeSaveDontAskAgain = false;
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
    bool vulkanRendererInitialized = false;
    bool vulkanMaterialFeatureWarningShown = false;
    
    bool showImportOBJDialog = false;
    bool showImportModelDialog = false;  // For Assimp models
    bool showImportSpriteSheetDialog = false;
    std::string pendingOBJPath;
    std::string pendingModelPath;  // For Assimp models
    std::string pendingSpriteSheetPath;
    char importOBJName[128] = "";
    char importModelName[128] = "";  // For Assimp models
    char importSpriteSheetName[128] = "";
    int importSpriteSheetColumns = 4;
    int importSpriteSheetRows = 4;
    float importSpriteSheetFps = 12.0f;
    SpriteSheetImportTarget importSpriteSheetTarget = SpriteSheetImportTarget::Sprite2D;
    
    char fileBrowserSearch[256] = "";
    float fileBrowserIconScale = 1.0f;  // 0.5 to 2.0 range
    float fileBrowserSidebarWidth = 220.0f;
    bool showFileBrowserSidebar = true;
    std::vector<fs::path> fileBrowserFavorites;
    struct ExternalFileDropEvent {
        fs::path path;
        double mouseX = 0.0;
        double mouseY = 0.0;
    };
    std::vector<ExternalFileDropEvent> pendingExternalFileDrops;
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
    EditorChromeScale uiChromeScale = EditorChromeScale::Default;
    static constexpr int kWorkspaceLayoutVersion = 2;
    UIAnimationMode uiAnimationMode = UIAnimationMode::Fluid;
    WorkspaceMode currentWorkspace = WorkspaceMode::Default;
    int loadedWorkspaceLayoutVersion = 0;
    std::array<bool, 3> workspaceTabVisible = { true, true, true };
    bool workspaceLayoutDirty = false;
    bool pendingWorkspaceReload = false;
    bool workspaceLayoutSavePending = false;
    bool workspaceLayoutAutoRepairPending = false;
    bool workspaceLayoutSettlingFrame = false;
    double workspaceLayoutStabilizeUntil = 0.0;
    double workspaceSwitchLockUntil = 0.0;
    fs::path pendingWorkspaceIniPath;
    ImGuiID mainDockspaceId = 0;
    bool editorSettingsDirty = false;
    bool windowsDisclaimerPopupOpened = false;
    bool lowSpecBenchmarkPopupOpened = false;
    bool androidStorageAccessPopupOpened = false;
    bool moduCppNvimWarningChecked = false;
    bool moduCppNvimWarningDetected = false;
    bool moduCppNvimWarningPopupOpened = false;
    bool showModuCppNvimRemovalSteps = false;
    bool moduCppNvimRemovalStepsOpened = false;
    bool showEnvironmentWindow = true;
    bool showCameraWindow = true;
    bool showAnimationWindow = false;
    bool showAIPathfindingWindow = false;
    bool showPixelSpriteEditorWindow = false;
    bool showVisualScriptingWindow = false;
    bool showSectorMapWindow = false;
    bool showLightmappingWindow = false;
#if !MODULARITY_RUNTIME_ONLY
    // Lightmapping window (Nebula bakes). Settings persist with the editor user
    // settings; the preview texture is owned here so it survives tab switches.
    struct LightmappingWindowState {
        int activeTab = 1;     // 0 = Object, 1 = Bake, 2 = Maps
        int requestedTab = -1; // one-shot programmatic tab change, -1 = none
        Nebula::LightmapSettings settings;
        // Bake target selection is derived from the scene each frame, but the
        // per-object opt-out lives here so it is not lost on reselection.
        std::unordered_set<int> excludedObjectIds;
        bool bakeSelectedOnly = false;
        // Result preview
        unsigned int previewTexture = 0;
        int previewWidth = 0;
        int previewHeight = 0;
        float previewExposure = 1.0f;
        bool previewDirty = false;
        // Last completed bake, for the status line at the bottom of the window.
        std::string lastBakeSummary;
        std::string lastBakeError;
        double lastBakeSeconds = 0.0;
        double bakeStartTime = 0.0;
        size_t lastBakeTriangles = 0;
        std::string pendingOutputPath;
        // Footer stats (triangle count / derived atlas size). Recomputed on a
        // timer rather than per frame: the full scene collection copies every
        // vertex, which is far too expensive for a label that only needs to
        // stay roughly current.
        double statsRefreshTime = -1.0;
        size_t statsMeshCount = 0;
        size_t statsTriangleCount = 0;
        float statsSurfaceArea = 0.0f;
        glm::vec3 statsBoundsMin = glm::vec3(0.0f);
        glm::vec3 statsBoundsMax = glm::vec3(0.0f);
    };
    LightmappingWindowState lightmapping;
#endif
    char registryPackageSearch[256] = "";
    std::string registryPackageSelectedId;
    int registryPackageView = 0;
    int registryPackageSubsystemFilter = 0;
    int registryPackageTypeFilter = 0;
    int registryPackageSort = 0;
    bool registryPackageLastActionSucceeded = true;
    std::string registryPackageFeedback;
    char moduPakPackageName[128] = "";
    char moduPakAuthorName[128] = "";
    char moduPakDescription[512] = "";
    char moduPakVersion[32] = "1.0.0";
    char moduPakOutputPath[512] = "";
    char moduPakAddPath[512] = "";
    char moduPakSearch[128] = "";
    bool moduPakRecursiveFolders = true;
    std::vector<fs::path> moduPakExportInputs;
    std::vector<uint8_t> moduPakExportSelections;
    char moduPakImportPath[512] = "";
    std::vector<ModuPakFileEntry> moduPakImportEntries;
    ModuPakManifest moduPakImportManifest;
    std::string moduPakImportFeedback;
    char moduObjName[128] = "";
    char moduObjAuthor[128] = "";
    char moduObjDescription[512] = "";
    char moduObjVersion[32] = "1.0.0";
    char moduObjOutputPath[512] = "";
    char moduObjImportPath[512] = "";
    bool pixelSpriteOpenImagePopupOpen = false;
    bool pixelSpriteOpenImagePopupTrigger = false;
    FileBrowser pixelSpriteOpenImageBrowser;
    char pixelSpriteOpenImageSearch[256] = "";
    int animationTargetId = -1;
    std::vector<int> animationEditTargetIds;
    bool animationApplyToSelection = true;
    int animationSelectedKey = -1;
    int animationSelectedEvent = -1;
    bool animationLogEvents = false;
    bool animationRecordMode = false;
    float animationCurrentTime = 0.0f;
    bool animationIsPlaying = false;
    float animationLastAppliedTime = -1.0f;
    struct AIPathBakeSettings {
        float cellSize = 0.75f;
        float obstaclePadding = 0.2f;
        bool allowDiagonal = true;
        bool autoRebake = false;
        int maxGridResolution = 512;
    };
    struct AIOffMeshLinkBaked {
        int sourceId = -1;
        int fromCell = -1;
        int toCell = -1;
        glm::vec3 startPoint = glm::vec3(0.0f);
        glm::vec3 endPoint = glm::vec3(0.0f);
        bool bidirectional = true;
        float cost = 1.0f;
    };
    struct AIPathGrid {
        bool baked = false;
        glm::vec2 origin = glm::vec2(0.0f);
        int width = 0;
        int height = 0;
        float cellSize = 1.0f;
        std::vector<uint8_t> walkable;
        std::vector<float> cellCost;     // per-cell traversal multiplier (1.0 default)
        std::vector<float> groundTop;    // per-cell ground Y (FLT_MAX if unknown)
        std::vector<int> sourceGroundIds;
        std::vector<int> sourceObstacleIds;
        std::vector<AIOffMeshLinkBaked> links;
    };
    struct AIAgentRuntimeState {
        std::vector<glm::vec3> path;
        int nextIndex = 0;
        float repathTimer = 0.0f;
        glm::vec3 lastGoal = glm::vec3(0.0f);
        bool hasLastGoal = false;
    };
    AIPathBakeSettings aiPathBakeSettings;
    AIPathGrid aiPathGrid;
    std::unordered_map<int, AIAgentRuntimeState> aiAgentRuntimeStates;
    int aiPreviewAgentId = -1;
    int aiPreviewTargetId = -1;
    bool aiPathDrawGrid = true;
    bool aiPathDrawPath = true;
    uint64_t aiPathLastSourceHash = 0;
    bool hierarchyShowTexturePreview = true;
    bool audioPreviewLoop = false;
    bool audioPreviewAutoPlay = false;
    float audioPreviewVolume = 1.0f;
    float audioPreviewBaseVolume = 1.0f;
    bool videoAssetPreviewLoop = false;
    std::unique_ptr<VideoPlayer> videoAssetPreviewPlayer;
    std::string videoAssetPreviewPath;
    enum class EditorFeedbackSoundCategory {
        Click = 0,
        Error = 1,
        Other = 2,
        Boot = 3
    };
    bool feedbackSoundsEnabled = true;
    bool feedbackClickSoundsEnabled = true;
    bool feedbackErrorSoundsEnabled = true;
    bool feedbackOtherSoundsEnabled = true;
    enum class AudioPreviewContext {
        None = 0,
        AssetBrowser,
        AudioSourceComponent
    };
    AudioPreviewContext audioPreviewContext = AudioPreviewContext::None;
    std::string audioPreviewSelectedPath;
    bool isPlaying = false;
    bool isPaused = false;
    bool showViewOutput = true;
    bool showSceneGizmos = true;
    bool gizmoShowCameraOverlays = true;
    bool gizmoShowCameraFrustumLabels = true;
    bool gizmoShowLightOverlays = true;
    bool gizmoShowLightIntensityLabels = true;
    bool showViewportHintOverlay = true;
    bool showLight2DStatsOverlay = true;
    bool gizmoShowLight2DBounds = true;
    bool gizmoShowLight2DShapes = true;
    bool gizmoShowShadowCaster2DBounds = true;
    float sceneGizmoIconScale = 1.0f;
    float sceneGizmoOverlayScale = 1.0f;
    SceneRenderMode sceneViewportRenderMode = SceneRenderMode::Normal;
    struct PlayerControllerGroundProbeDebug {
        int playerId = -1;
        glm::vec3 rayStart = glm::vec3(0.0f);
        glm::vec3 rayEnd = glm::vec3(0.0f);
        glm::vec3 hitPos = glm::vec3(0.0f);
        bool hasHit = false;
    };
    struct PlayerControllerRuntimeState {
        glm::vec2 localVelocity = glm::vec2(0.0f);
        glm::vec3 slideVelocity = glm::vec3(0.0f);
        glm::vec3 lastGroundHitPos = glm::vec3(0.0f);
        bool hasGroundSample = false;

        // View motion. All of this is presentation state rebuilt every frame from
        // movement; none of it feeds back into the capsule or the physics pose.
        float bobPhase = 0.0f;          // stride position, radians
        // Eased stand-in for the real planar speed. Every gait-driven value reads
        // this, so amplitude, stride rate and lean all change together.
        float gaitSpeed = 0.0f;
        // Footfall accumulator, in whole steps. Starts at 0.25 because the vertical
        // bob bottoms out a quarter of a footfall into the cycle - that offset is
        // what puts the sound on the camera dip instead of a beat early.
        float footPhase = 0.25f;
        int lastFootstepClip = -1;      // so a random pick never repeats back to back
        float idleWeight = 1.0f;        // 0..1 breathing weight, on its own clock
        float idlePhase = 0.0f;         // seconds, drives the breathing sines
        glm::vec2 lookSwayOffset = glm::vec2(0.0f);   // degrees (pitch, yaw)
        glm::vec2 lookSwayVelocity = glm::vec2(0.0f);
        glm::vec2 attachedSwayOffset = glm::vec2(0.0f);  // degrees, child lag
        glm::vec2 attachedSwayVelocity = glm::vec2(0.0f);
        float roll = 0.0f;              // degrees, smoothed strafe + turn lean
        float landingDip = 0.0f;        // units, negative while compressing
        float landingDipVelocity = 0.0f;
        float previousYaw = 0.0f;
        bool hasPreviousYaw = false;
        bool wasGrounded = true;
        float jumpCrouch = 0.0f;        // units, negative while winding up

        // Look smoothing carry-over: mouse input that has arrived but not been
        // applied yet. Drained rather than damped, so no input is ever lost.
        glm::vec2 pendingLook = glm::vec2(0.0f);
        // Charged jump.
        float jumpCharge = 0.0f;        // seconds held
        bool jumpCharging = false;
        // Authored local rotation of each swayed child, captured the first time it is
        // touched so the spring is applied as an offset rather than accumulating onto
        // itself frame after frame.
        std::unordered_map<int, glm::vec3> attachedBaseRotation;
    };
    PlayerControllerGroundProbeDebug playerControllerGroundProbeDebug;
    std::unordered_map<int, PlayerControllerRuntimeState> playerControllerRuntimeStates;
    // Head-bob translation for the active player camera. Applied in
    // makeCameraFromObject (the one choke point every camera build goes through)
    // instead of on the object, because on this rig the camera *is* the player
    // object and writing it into position would drag the collider along.
    // Offset is in the camera's own basis; id is -1 whenever it should not apply.
    int playerViewMotionCameraId = -1;
    glm::vec3 playerViewMotionOffset = glm::vec3(0.0f);
    bool showGameViewport = true;
    int previewCameraId = -1;
    bool gameViewCursorLocked = false;
    bool gameViewportFocused = false;
    bool showGameProfiler = true;
    bool revealDebugSectionsAndMenus = false;
    bool uiCanvasPreviewEnabled = true;
    bool showCanvasOverlay = false;
    bool showUIWorldGrid = true;
    bool showSceneGrid3D = false;
    bool pixelGridSnapEnabled = true;
    int pixelGridSnapStep = 1;
    enum class PixelSpriteEditorMode {
        Edit = 0,
        SpriteSheet = 1
    };
    enum class PixelSpriteTool {
        Pencil = 0,
        Eraser = 1,
        Bucket = 2,
        ColorPicker = 3,
        MagicSelect = 4,
        Lasso = 5,
        LineCurve = 6,
        MoveSelectedArea = 7,
        Rectangle = 8,
        RoundedRectangle = 9,
        Circle = 10,
        SelectArea = 11
    };
    enum class PixelSpriteCheckerTheme {
        Light = 0,
        Dark = 1
    };
    struct PixelSpriteDocument {
        fs::path imagePath;
        fs::path sidecarPath;
        std::string name = "Untitled";
        int width = 16;
        int height = 16;
        std::vector<unsigned char> pixels;
        bool dirty = false;
        bool loaded = false;
        bool selectionActive = false;
        glm::ivec2 selectionStart = glm::ivec2(0);
        glm::ivec2 selectionEnd = glm::ivec2(0);
        std::string expectedMinimumModuEngineVersionOrHigher;
        bool strictValidation = false;
        std::vector<glm::ivec4> spriteFrames;
        std::vector<std::string> spriteFrameNames;
        std::vector<glm::vec2> spriteFrameScales;
        std::vector<PixelSpriteLayerState> layers;
        int activeLayer = 0;
        int activeFrame = 0;
    };
    struct PixelSpriteHistoryState {
        std::string label = "Edit";
        int width = 16;
        int height = 16;
        std::vector<unsigned char> pixels;
        bool selectionActive = false;
        glm::ivec2 selectionStart = glm::ivec2(0);
        glm::ivec2 selectionEnd = glm::ivec2(0);
        std::string expectedMinimumModuEngineVersionOrHigher;
        bool strictValidation = false;
        std::vector<glm::ivec4> spriteFrames;
        std::vector<std::string> spriteFrameNames;
        std::vector<glm::vec2> spriteFrameScales;
        std::vector<PixelSpriteLayerState> layers;
        int activeLayer = 0;
        int activeFrame = 0;
    };
    PixelSpriteDocument pixelSpriteDocument;
    std::vector<PixelSpriteHistoryState> pixelSpriteUndoStack;
    std::vector<PixelSpriteHistoryState> pixelSpriteRedoStack;
    PixelSpriteEditorMode pixelSpriteEditorMode = PixelSpriteEditorMode::Edit;
    PixelSpriteTool pixelSpriteTool = PixelSpriteTool::Pencil;
    float pixelSpriteZoom = 18.0f;
    float pixelSpriteTargetZoom = 18.0f;
    ImVec2 pixelSpriteCanvasPan = ImVec2(0.0f, 0.0f);
    ImVec2 pixelSpriteCanvasTargetPan = ImVec2(0.0f, 0.0f);
    bool pixelSpriteCanvasStateInitialized = false;
    bool pixelSpriteCanvasCenterPending = false;
    glm::vec4 pixelSpritePrimaryColor = glm::vec4(0.12f, 0.12f, 0.12f, 1.0f);
    glm::vec4 pixelSpriteSecondaryColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    std::vector<glm::vec4> pixelSpriteRecentColors;
    int pixelSpriteBrushSize = 1;
    bool pixelSpriteShowGrid = true;
    bool pixelSpritePixelPerfect = true;
    PixelSpriteCheckerTheme pixelSpriteCheckerTheme = PixelSpriteCheckerTheme::Light;
    bool pixelSpriteRightPanelCollapsed = false;
    bool pixelSpriteFloatingSelectionActive = false;
    std::vector<unsigned char> pixelSpriteFloatingSelectionPixels;
    glm::ivec2 pixelSpriteFloatingSelectionSize = glm::ivec2(0);
    glm::ivec2 pixelSpriteFloatingSelectionPosition = glm::ivec2(0);
    int pixelSpriteFloatingSelectionLayer = -1;
    int gameViewportResolutionIndex = 0;
    int gameViewportCustomWidth = 1920;
    int gameViewportCustomHeight = 1080;
    float gameViewportZoom = 1.0f;
    bool gameViewportAutoFit = true;
    int sceneViewportRenderWidth = 1600;
    int sceneViewportRenderHeight = 900;
    ViewportDisplayMode sceneViewportDisplayMode = ViewportDisplayMode::Stretch;
    ViewportDisplayMode gameViewportDisplayMode = ViewportDisplayMode::Fit;
    ViewportToolbarCorner sceneViewportToolbarCorner = ViewportToolbarCorner::BottomLeft;
    // Touch (Android): on-screen twin-stick camera nav in the Scene viewport.
    // Toggled from the Quick Tools toolbar; radius is in pre-DPI pixels.
    bool showTouchSticks = true;
    float touchStickRadius = 44.0f;
    // Right-stick look feel (saved). Sensitivity is the look rate; invert flips Y.
    float touchStickSensitivity = 6.0f;
    bool touchStickInvertY = false;
    // Quick Tools popup (Android touch toolbar). quickToolsOpen is runtime-only
    // (popup expanded this session); the rest are saved to editor settings.
    bool quickToolsOpen = false;
    bool quickToolsPinned = false;
    // Mobile layout hides the top play-controls bar (Play/Spec/Pause live in the
    // Quick Tools popup instead). Desktop layout keeps the bar. Touch-only effect.
    bool mobileEditorLayout = true;
    int gameViewportLastRenderWidth = 0;
    int gameViewportLastRenderHeight = 0;
    int activePlayerId = -1;
    MeshBuilder meshBuilder;
    char meshBuilderPath[260] = "";
    char meshBuilderFaceInput[128] = "";
    bool meshEditMode = false;
    bool meshEditLoaded = false;
    bool meshEditDirty = false;
    bool meshEditExtrudeMode = false;
    bool meshEditAutoUV = true;
    bool meshEditTriangleSelection = false;
    int meshEditAutoObjectId = -1;
    std::string meshEditPath;
    RawMeshAsset meshEditAsset;
    std::vector<int> meshEditSelectedVertices;
    std::vector<int> meshEditSelectedEdges; // indices into generated edge list
    std::vector<int> meshEditSelectedFaces; // indices into mesh faces
    int meshEditActiveMaterialSlot = 0;
    float meshEditInsetAmount = 0.2f;
    float meshEditExtrudeAmount = 0.3f;
    float meshEditBevelAmount = 0.1f;
    float meshEditGridSnap = 0.1f;
    float meshEditWeldDistance = 0.001f;
    float meshEditSmoothFactor = 0.5f;
    int meshEditSmoothIterations = 1;
    float meshEditSimilarFaceAngle = 1.0f;
    int meshEditPivotMode = 0; // 0 median, 1 bounds center, 2 active element
    bool meshEditProportionalEditing = false;
    float meshEditProportionalRadius = 1.0f;
    float meshEditProportionalFalloff = 2.0f;
    float meshEditUvMoveStep = 0.1f;
    float meshEditUvScaleStep = 1.1f;
    float meshEditUvRotateStep = 15.0f;
    struct UIAnimationState {
        float hover = 0.0f;
        float active = 0.0f;
        float sliderValue = 0.0f;
        float contentExtent = 0.0f;
        // 0 = collapsed, 1 = open. Drives the inspector foldout arrow's rotation
        // so one sprite covers both states by turning between them.
        float foldOpen = 0.0f;
        bool initialized = false;
    };
    std::unordered_map<int, UIAnimationState> uiAnimationStates;
    std::unordered_map<ImGuiID, UIAnimationState> editorUiAnimationStates;
    // Handoff from a component header to the body scope opened immediately after
    // it, so the expand/collapse reveal needs no extra argument at the ~44
    // inspector body call sites.
    ImGuiID inspectorPendingRevealId = 0;
    bool inspectorPendingRevealOpen = false;
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
    // -- Assemblage -----------------------------------------------------------
    // Which way the user is editing the 2D world. Freeform is the existing
    // place-anything-anywhere workflow; Assemblage is grid painting. The switch
    // changes the active tools only - both kinds of content are always loaded,
    // always drawn, and always in the same scene.
    enum class World2DEditMode { Freeform = 0, Assemblage = 1 };
    enum class AssemblageTool {
        Paint = 0,
        Erase = 1,
        Rectangle = 2,
        Fill = 3,
        Line = 4,
        Picker = 5,
        Select = 6
    };
    // The runtime owns every loaded Assemblage. Editor and player share it, so
    // there is no separate edit copy to keep in sync.
    AssemblageRuntime assemblageRuntime;
    World2DEditMode world2DEditMode = World2DEditMode::Freeform;
    AssemblageTool assemblageTool = AssemblageTool::Paint;
    bool showAssemblageWindow = false;
    int assemblageActiveObjectId = -1;   // Assemblage root being edited
    int assemblageActiveLayerId = -1;
    uint32_t assemblageActiveTile = 1;
    bool assemblageShowGridOverlay = true;
    bool assemblageRandomVariants = false;
    // In-progress stroke. Cells accumulate here until the mouse is released,
    // then the whole stroke lands as a single undo entry.
    bool assemblageStrokeActive = false;
    AssemblageEditRecord assemblageStroke;
    bool assemblageDragActive = false;
    glm::ivec2 assemblageDragStart = glm::ivec2(0);
    glm::ivec2 assemblageDragCurrent = glm::ivec2(0);
    bool assemblageHasSelection = false;
    glm::ivec2 assemblageSelectionMin = glm::ivec2(0);
    glm::ivec2 assemblageSelectionMax = glm::ivec2(0);
    // Clipboard for copy/paste, stored as cell offsets from the selection
    // origin so a paste lands wherever the cursor is.
    struct AssemblageClipboardCell {
        int dx = 0;
        int dy = 0;
        uint32_t cell = 0;
    };
    std::vector<AssemblageClipboardCell> assemblageClipboard;
    glm::ivec2 assemblageClipboardSize = glm::ivec2(0);
    // Per-frame telemetry for the 2D stats overlay.
    int assemblageTileQuadsLastFrame = 0;
    std::array<Light2DBlendStyleDefinition, 4> light2DBlendStyles = {{
        { "Additive", Light2DBlendMode::Additive, glm::vec4(1.0f), 1.0f },
        { "Multiply", Light2DBlendMode::Multiply, glm::vec4(1.0f), 1.0f },
        { "Subtractive", Light2DBlendMode::Subtractive, glm::vec4(1.0f), 1.0f },
        { "Soft Additive", Light2DBlendMode::Additive, glm::vec4(0.85f, 0.85f, 0.85f, 1.0f), 0.65f }
    }};
    bool light2DCompositorRanLastFrame = false;
    bool light2DLightBufferHadContentLastFrame = false;
    int light2DActiveCountLastFrame = 0;
    int light2DLitSprite2DCountLastFrame = 0;
    int light2DLitWorldImageCountLastFrame = 0;
    Light2DPostFXSettings world2DPostFx;
    std::unordered_map<int, std::string> light2DObjectRoutingReasonsLastFrame;
    bool light2DShapeEditMode = false;
    int light2DShapeEditingObjectId = -1;
    int light2DShapeEditingPointIndex = -1;
    struct UiCanvas3DContext {
        ImGuiContext* context = nullptr;
        bool backendReady = false;
        // input was routed to this canvas last frame; redraw-skipping waits one
        // extra frame after input stops so ImGui can release hover/active state.
        bool hadInputLastFrame = false;
        uint64_t lastContentSignature = 0;
        bool hasRenderedTarget = false;
    };
    struct UiCanvas3DInput {
        ImVec2 mousePos = ImVec2(-FLT_MAX, -FLT_MAX);
        bool mouseDown[3] = { false, false, false };
        float mouseWheel = 0.0f;
        bool hasInput = false;
        float hitT = FLT_MAX;
    };
    std::unordered_map<int, UiCanvas3DContext> uiCanvas3DContexts;
    std::unordered_map<int, UiCanvas3DInput> uiCanvas3DInputs;
    bool consoleWrapText = true;
    enum class MeshEditSelectionMode { Object = 0, Vertex = 1, Edge = 2, Face = 3, UV = 4, Carve = 5 };
    MeshEditSelectionMode meshEditSelectionMode = MeshEditSelectionMode::Object;
    // Carve tool state (MeshEditSelectionMode::Carve). Transient preview data,
    // intentionally not serialized and not part of undo snapshots.
    int carveFaceIndex = -1;               // picked face defining the carve plane
    bool carveHasRect = false;             // an opening has been placed
    glm::vec2 carveRectCenter = glm::vec2(0.0f); // in the picked face's plane UV
    glm::vec2 carveRectSize = glm::vec2(1.2f, 2.2f); // full width/height
    bool carveDragActive = false;
    glm::vec2 carveDragStartUV = glm::vec2(0.0f);
    bool carveShapeValid = false;
    std::string carveInvalidReason;
    bool carvePreviewDirty = true;
    bool carveCutThrough = true;
    float carveDepth = 0.25f;              // pocket depth when not cutting through
    bool carveCreateFrame = false;
    float carveFrameThickness = 0.08f;
    int carveShapeSegments = 0;            // 0 = rectangle, >=3 = polygon/circle
    bool carveSnapToGrid = true;
    float carveSurfaceOffset = 0.0f;
    bool carveCreateConnectedSector = false;
    int carveConnectedPreset = 1;          // room preset for the generated sector
    void resetCarveToolState() {
        carveFaceIndex = -1;
        carveHasRect = false;
        carveDragActive = false;
        carveShapeValid = false;
        carveInvalidReason.clear();
        carvePreviewDirty = true;
    }
    ScriptCompiler scriptCompiler;
    ScriptRuntime scriptRuntime;
    ManagedScriptRuntime managedRuntime;
    // Default to Jolt; the actual backend the project wants is swapped in
    // when a project loads or the user changes the dropdown in settings.
    std::unique_ptr<IPhysicsBackend> physics = CreatePhysicsBackend(PhysicsBackendType::Jolt);
    AudioSystem audio;
    // Footstep clip choice. One generator on the engine rather than a static inside
    // the audio call, so it cannot be shared across threads by accident.
    std::mt19937 movementAudioRng{std::random_device{}()};
    struct EditorToastState {
        bool visible = false;
        ConsoleMessageType type = ConsoleMessageType::Info;
        std::string message;
        double startTime = 0.0;
        double holdSeconds = 1.8;
        // Progress mode turns the corner toast into a small bake/progress card
        // (title + detail line + bar) anchored bottom-right instead of the
        // one-line centered message. Sticky progress toasts never time out;
        // the owner clears them via finishEditorProgressToast().
        bool progressMode = false;
        bool sticky = false;
        float progress = 0.0f;   // 0..1; negative = indeterminate
        std::string detail;      // secondary line under the title
    };
    EditorToastState editorToast;
    bool showCompilePopup = false;
    bool compilePopupOpened = false;
    double compilePopupHideTime = 0.0;
    // current compile UI is a non-blocking corner toast (auto-compile on save) rather
    // than the centered modal (manual / batch compiles).
    bool compileUiToast = false;
    // set true for the duration of an auto-triggered compile call so the open points
    // can capture toast-vs-modal, then immediately cleared. manual compiles leave it
    // false and get the modal.
    bool nextCompileFromAuto = false;
    double compileCompletionStart = 0.0;
    bool lastCompileSuccess = false;
    std::string lastCompileStatus;
    std::string lastCompileLog;
    std::vector<Modularity::ScriptDiagnostic> lastCompileDiagnostics;
    float compileProgress = 0.0f;
    std::string compileStage;
    enum class BuildPlatform {
        Windows = 0,
        Linux = 1,
        Android = 2
    };

    // single source of truth for build target metadata; UI combos, serialization and export
    // all derive from these tables so a new platform only gets added here.
    static constexpr int kBuildPlatformCount = 3;
    static constexpr const char* kBuildPlatformLabels[kBuildPlatformCount] = {
        "Windows",
        "Linux",
        "Android (Experimental)"
    };
    static constexpr const char* kBuildPlatformSerializedNames[kBuildPlatformCount] = {
        "Windows",
        "Linux",
        "Android"
    };
    static constexpr bool kBuildPlatformExperimental[kBuildPlatformCount] = {
        false,
        false,
        true
    };

    struct BuildArchitectureInfo {
        const char* label;
        const char* serializedName;
    };
    static constexpr int kDesktopArchitectureCount = 2;
    static constexpr BuildArchitectureInfo kDesktopArchitectures[kDesktopArchitectureCount] = {
        {"x86_64", "x86_64"},
        {"x86",    "x86"},
    };
    static constexpr int kAndroidArchitectureCount = 2;
    static constexpr BuildArchitectureInfo kAndroidArchitectures[kAndroidArchitectureCount] = {
        {"arm64-v8a",   "arm64-v8a"},
        {"armeabi-v7a", "armeabi-v7a"},
    };

    static int buildPlatformIndex(BuildPlatform p) {
        int idx = static_cast<int>(p);
        if (idx < 0 || idx >= kBuildPlatformCount) idx = 0;
        return idx;
    }
    static BuildPlatform buildPlatformFromIndex(int index) {
        if (index < 0 || index >= kBuildPlatformCount) index = 0;
        return static_cast<BuildPlatform>(index);
    }
    static const char* buildPlatformLabel(BuildPlatform p) {
        return kBuildPlatformLabels[buildPlatformIndex(p)];
    }
    static const char* buildPlatformSerializedName(BuildPlatform p) {
        return kBuildPlatformSerializedNames[buildPlatformIndex(p)];
    }
    static bool buildPlatformIsExperimental(BuildPlatform p) {
        return kBuildPlatformExperimental[buildPlatformIndex(p)];
    }
    static BuildPlatform buildPlatformFromSerializedName(const std::string& name, BuildPlatform fallback) {
        for (int i = 0; i < kBuildPlatformCount; ++i) {
            if (name == kBuildPlatformSerializedNames[i]) return static_cast<BuildPlatform>(i);
        }
        return fallback;
    }
    static const BuildArchitectureInfo* buildArchitecturesForPlatform(BuildPlatform p, int& outCount) {
        if (p == BuildPlatform::Android) {
            outCount = kAndroidArchitectureCount;
            return kAndroidArchitectures;
        }
        outCount = kDesktopArchitectureCount;
        return kDesktopArchitectures;
    }
    static const char* defaultArchitectureForPlatform(BuildPlatform p) {
        int count = 0;
        const BuildArchitectureInfo* list = buildArchitecturesForPlatform(p, count);
        return list[0].serializedName;
    }

    // Defined in AndroidExport.cpp. Returns an empty string if no usable NDK
    // can be located via ANDROID_NDK_ROOT / ANDROID_NDK_HOME / ANDROID_NDK.
    static std::string resolveAndroidNdkPath();

    // defined in AndroidExport.cpp. cross-compiles every project script for the ABI with the
    // NDK clang, stages them by soname into apk/lib/<abi>/, and drops 0-byte placeholders at
    // the bundle paths so the runtime's binary-present gate passes. false + error on first failure.
    bool crossCompileAndroidScripts(const ScriptBuildConfig& scriptConfig,
                                    const fs::path& projectRoot,
                                    const fs::path& ndkRoot,
                                    const std::string& androidAbi,
                                    const fs::path& libDir,
                                    const fs::path& runtimeStageRoot,
                                    const fs::path& compiledScriptsRel,
                                    const std::function<void(const std::string&)>& appendLog,
                                    std::string& error);

    // on-device (Android editor) script compile. ensureOnDeviceToolchain extracts the bundled
    // clang to the files dir on first use; configureOnDeviceScriptCompile points a
    // ScriptBuildConfig at it so the normal compileScriptFile path makes a loadable .so on the
    // phone. no-op stubs off Android. see src/AndroidScriptCompile.cpp.
    fs::path ensureOnDeviceToolchain(std::string& error);
    bool configureOnDeviceScriptCompile(ScriptBuildConfig& config, std::string& error);

    struct BuildSceneEntry {
        std::string name;
        bool enabled = true;
    };
    struct BuildSettings {
        std::string profileName = "Default";
        std::string activeProfilePath = "BuildProfiles/Default.modubuild";
        BuildPlatform platform = BuildPlatform::Windows;
        std::string architecture = "x86_64";
        std::string configuration = "Release";
        bool includeEditor = false;
        bool runtimeOnly = true;
        bool leanRuntimeExport = false;
        bool shipScriptSdk = false;
        std::string outputFolder = "Builds";
        std::string outputName = "{buildName}-{version}-{platform}";
        std::string moduleAssimp = "auto";
        std::string modulePhysX = "auto";
        std::string moduleJolt = "auto";
        std::string moduleVulkan = "auto";
        std::string moduleSndfile = "auto";
        std::string moduleOpusfile = "auto";
        std::string moduleMono = "auto";
        std::string moduleOpenGLES = "auto";
        std::string companyName = "DefaultCompany";
        std::string buildName = "MyProject";
        std::string version = "0.1.0";
        std::string splashImagePath;
        bool splashEnabled = false;
        float splashDurationSeconds = 2.5f;
        bool packageStandaloneArchive = true;
        bool developmentBuild = false;
        bool autoConnectProfiler = false;
        bool scriptDebugging = false;
        bool deepProfiling = false;
        bool scriptsOnlyBuild = false;
        bool serverBuild = false;
        std::string compressionMethod = "Default";
        glm::vec3 rendererAmbientColor = glm::vec3(0.2f, 0.2f, 0.2f);
        int rendererShadowResolution = 512;
        bool rendererAutoReloadShaders = true;
        float light2DLightingResolution = 0.75f;    // internal render scale for 2D light buffers (ships to player)
        bool light2DLightingResolutionAuto = true;  // true = adaptive downscale, false = honor the scale exactly
        bool light2DLightingPixelFilter = false;    // false = bilinear (smooth), true = nearest (crisp pixel light, skips bilinear taps)
        bool light2DLightingSDR = false;            // false = HDR RGBA16F, true = SDR RGBA8 (half the 2D-lighting bandwidth; clamps overbright light)
        float editorCameraFov = FOV;
        float editorCameraNear = NEAR_PLANE;
        float editorCameraFar = FAR_PLANE;
        std::vector<BuildSceneEntry> scenes;
    };
    BuildSettings buildSettings;
    int buildSettingsSelectedIndex = -1;
    bool buildSettingsDirty = false;
    struct ExportJobResult {
        bool success = false;
        std::string message;
        fs::path outputDir;
        std::string executableName;
        fs::path archivePath;
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
        std::string executableName;
        fs::path archivePath;
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
        fs::path stagedBinaryPath;
        std::string compiledSource;
        std::string compileLog;
        std::string linkLog;
        std::string error;
        std::vector<Modularity::ScriptDiagnostic> diagnostics;
    };
    struct ScriptCompileQueueItem {
        fs::path scriptPath;
        std::string displayLabel;
        bool managed = false;
    };
    struct ScriptCompileHistoryItem {
        fs::path scriptPath;
        std::string displayLabel;
        std::string statusLabel;
        std::string summary;
        std::string outputLog;
        std::vector<Modularity::ScriptDiagnostic> diagnostics;
        bool success = false;
        bool warning = false;
        double completedAt = 0.0;
    };
    std::atomic<bool> compileInProgress = false;
    // Pool of in-flight script compile jobs. Sized up to compileMaxParallelJobs()
    // (CPU core count) so independent .moducpp scripts build concurrently instead
    // of one at a time; a managed (dotnet) build claims the whole pool by itself.
    std::vector<std::future<ScriptCompileJobResult>> compileWorkers;
    std::mutex compileMutex;
    std::deque<ScriptCompileQueueItem> compileRequestQueue;
    std::unordered_set<std::string> compileRequestKeys;
    std::vector<ScriptCompileHistoryItem> compileHistory;
    std::string compileCurrentLabel;
    fs::path compileCurrentPath;
    bool compileCurrentManaged = false;
    int compileBatchTotal = 0;
    int compileBatchCompleted = 0;
    std::unordered_map<std::string, fs::file_time_type> scriptLastAutoCompileTime;
    std::unordered_map<std::string, fs::file_time_type> scriptAutoCompileCheckedSourceTime;
    // last source revision the inspector kicked a "stale binary" background recompile
    // for. without this, a script that keeps failing to compile re-queues a build every
    // single inspector frame and machine-guns the compile-start sound.
    std::unordered_map<std::string, fs::file_time_type> inspectorStaleRecompileAttempted;
    std::unordered_map<std::string, fs::path> scriptAutoCompileBinaryCache;
    std::unordered_set<std::string> scriptAutoCompileDiscoveredSources;
    struct ScriptBinaryResolveCacheEntry {
        fs::file_time_type sourceWriteTime{};
        fs::file_time_type configWriteTime{};
        fs::path binaryPath;
        bool valid = false;
    };
    std::unordered_map<std::string, ScriptBinaryResolveCacheEntry> scriptBinaryResolveCache;
    // Parsed scripts config, reused across scripts. loadConfig probes the
    // filesystem hard (engine roots, bundled SDK include roots, host import
    // library), and resolveScriptBinary calls it once per script - so loading a
    // project with N scripts repeated that whole probe N times for a result that
    // only changes when scripts.modu itself does.
    struct ScriptBuildConfigCacheEntry {
        fs::file_time_type configWriteTime{};
        ScriptBuildConfig config;
        bool valid = false;
        bool haveConfig = false;
        std::string error;
    };
    std::unordered_map<std::string, ScriptBuildConfigCacheEntry> scriptBuildConfigCache;
    // "Does this compiled script export an editor window?" keyed by binary path +
    // mtime. Answering it calls ScriptRuntime::hasEditorWindow, which LoadLibrary's
    // the module - so the output-directory sweep used to load every DLL in the
    // project just to ask, and on Windows each first load is an antivirus scan.
    // A recompile changes the mtime and re-probes, so a newly added editor window
    // still appears.
    struct ScriptEditorWindowProbe {
        fs::file_time_type writeTime{};
        bool hasEditorWindow = false;
    };
    std::unordered_map<std::string, ScriptEditorWindowProbe> scriptEditorWindowProbeCache;
    bool scriptEditorWindowProbeCacheLoaded = false;
    bool scriptEditorWindowProbeCacheDirty = false;
    fs::path scriptEditorWindowProbeCachePath() const;
    void loadScriptEditorWindowProbeCache();
    void saveScriptEditorWindowProbeCache();
    std::deque<fs::path> autoCompileQueue;
    std::unordered_set<std::string> autoCompileQueued;
    std::unordered_set<std::string> nativeScriptMissingLogged;
    std::unordered_set<std::string> nativeScriptLoadErrorLogged;
    struct ScriptHttpRequestState {
        int id = 0;
        std::mutex mutex;
        std::deque<std::string> pendingChunks;
        std::string bufferedResponse;
        bool done = false;
        bool success = false;
        bool cancelled = false;
        bool stream = false;
    };
    std::mutex scriptHttpRequestsMutex;
    std::unordered_map<int, std::shared_ptr<ScriptHttpRequestState>> scriptHttpRequests;
    std::atomic<int> nextScriptHttpRequestId = 1;
    struct ScriptProcessState {
        int id = 0;
        std::mutex mutex;               // guards pendingChunks/done/success/exitCode
        std::deque<std::string> pendingChunks;
        bool done = false;
        bool success = false;
        bool cancelled = false;
        int exitCode = -1;
        std::mutex stdinMutex;          // guards stdinFd writes
        int stdinFd = -1;               // parent write end (interactive; -1 otherwise). POSIX only.
        long long pid = -1;             // child pid (POSIX) for signalling; -1 if unknown
        std::string lineBuffer;         // ReadProcessLine reassembly buffer (single consumer)
    };
    std::mutex scriptProcessesMutex;
    std::unordered_map<int, std::shared_ptr<ScriptProcessState>> scriptProcesses;
    std::atomic<int> nextScriptProcessId = 1;
    bool managedAutoCompileQueued = false;
    double managedAutoCompileLastScan = 0.0;
    double managedAutoCompileScanInterval = 5.0;
    fs::path managedAutoCompileCachedProjectDir;
    fs::file_time_type managedAutoCompileNewestSource{};
    bool managedAutoCompileHasSource = false;
    // last managed source state we actually kicked an auto-compile for. without this a
    // relocated/stale managed output path fails the up-to-date check on every scan and
    // we recompile C# forever.
    fs::file_time_type managedAutoCompileCompiledSource{};
    bool managedAutoCompileHasCompiled = false;
    // Master switch for watch-and-rebuild. Off means scripts only compile when
    // asked (save, Compile button, build), which is what you want on a laptop
    // or a very large Scripts tree. Global preference, see EditorGlobalPreferences.
    bool scriptAutoCompileEnabled = true;
    double scriptAutoCompileLastCheck = 0.0;
    double scriptAutoCompileInterval = 0.5;
    double scriptAutoCompileLastDirectoryScan = 0.0;
    double scriptAutoCompileDirectoryScanInterval = 5.0;
    // Auto-compile source discovery runs off the main thread. The recursive
    // project scan plus a stat per script was the dominant cost in the editor's
    // per-frame "Asset Loading" block, and on a slow disk it lands as a visible
    // frame hitch every few seconds. The main thread now only builds a request,
    // hands it to a worker, and merges the result back when it's ready.
    struct ScriptAutoCompileScanRequest {
        fs::path projectRoot;
        fs::path scriptsDir;
        fs::path outDir;
        // Script paths pulled off sceneObjects on the main thread; the worker
        // never touches the scene.
        std::vector<fs::path> sceneScriptPaths;
        bool runDirectoryScan = false;
        // Carried forward verbatim when runDirectoryScan is false.
        std::unordered_set<std::string> discoveredSources;
        std::unordered_map<std::string, fs::file_time_type> checkedSourceTime;
        std::unordered_map<std::string, fs::path> binaryCache;
        std::unordered_map<std::string, fs::file_time_type> lastAutoCompileTime;
        // Provenance half of the staleness check: what this engine builds against now,
        // and what each binary on disk was built against. Copied (not moved) because the
        // main thread keeps recording into the live history while a scan is in flight.
        ScriptToolchainStamp toolchainStamp;
        std::unordered_map<std::string, ScriptHistoryEntry> historyEntries;
        // Sources already force-queued once for a provenance mismatch. A script that
        // fails to compile keeps its stale history entry, and without this it would be
        // re-queued by every single scan.
        std::unordered_set<std::string> historyForcedKeys;
        bool historyMigrating = false;
        bool scanManaged = false;
        bool runManagedDirectoryScan = false;
        fs::path managedProject;
        fs::path managedOutput;
        bool managedHasSource = false;
        fs::file_time_type managedNewestSource{};
        fs::file_time_type managedCompiledSource{};
        bool managedHasCompiled = false;
        uint64_t generation = 0;
    };
    struct ScriptAutoCompileScanResult {
        std::unordered_set<std::string> discoveredSources;
        std::unordered_map<std::string, fs::file_time_type> checkedSourceTime;
        std::unordered_map<std::string, fs::path> binaryCache;
        // Every source key the scan considered; used to prune caches the worker
        // doesn't own (scriptLastAutoCompileTime).
        std::unordered_set<std::string> knownSources;
        // Sources whose binary is missing or older than the source, in the order
        // the main thread should queue them.
        struct OutdatedSource {
            fs::path path;
            fs::file_time_type sourceTime{};
            // Queued because the *engine* moved, not the source. The source mtime hasn't
            // changed, so the "did we already build this revision?" guards would swallow
            // it; they're skipped for these.
            bool forcedByHistory = false;
        };
        std::vector<OutdatedSource> outdatedSources;
        // Binaries with no history entry, seen while the manifest is still migrating.
        // Adopted at the current stamp rather than rebuilt, so adding this system to an
        // existing project doesn't force one full rebuild on first launch.
        std::vector<std::pair<std::string, ScriptHistoryEntry>> adoptedHistoryEntries;
        // How many sources the history condemned, for a single summary line instead of
        // one console message per script.
        int historyStaleCount = 0;
        // One line per set of sources that compile to the same binary. Only the winner
        // is queued; the rest are skipped, so without this they would vanish silently.
        std::vector<std::string> duplicateArtifactWarnings;
        bool ranDirectoryScan = false;
        bool managedScanned = false;
        bool managedHasSource = false;
        fs::file_time_type managedNewestSource{};
        bool managedNeedsCompile = false;
        uint64_t generation = 0;
    };
    std::future<ScriptAutoCompileScanResult> scriptAutoCompileScanFuture;
    bool scriptAutoCompileScanInFlight = false;
    // Bumped whenever the auto-compile caches are cleared (project/scene swap) so
    // a scan launched against the old project can't repopulate them.
    uint64_t scriptAutoCompileScanGeneration = 0;
    // The scripts config is re-resolved only when its file timestamp moves, so the
    // common tick costs one stat instead of a parse. Resolving it stays on the main
    // thread because packageManager.applyToBuildConfig reads editor-owned state.
    ScriptBuildConfig scriptAutoCompileConfig;
    bool scriptAutoCompileConfigValid = false;
    fs::path scriptAutoCompileConfigPath;
    fs::file_time_type scriptAutoCompileConfigTime{};
    // Which engine build produced the binaries sitting in outDir. See ScriptHistory.h -
    // this is what turns an ABI bump into "recompiling 12 scripts" instead of a wall of
    // "ABI mismatch, recompile scripts" errors the first time the user hits Play.
    ScriptHistory scriptHistory;
    fs::path scriptHistoryPath;
    ScriptToolchainStamp scriptToolchainStamp;
    // Sources already re-queued after the loader rejected their binary. Without it a
    // script that fails to compile re-queues on every frame it's ticked.
    std::unordered_set<std::string> scriptLoadFailureRecompileRequested;
    // Same idea for the scan-side provenance check; see ScanRequest::historyForcedKeys.
    std::unordered_set<std::string> scriptHistoryForcedRecompile;
    // Load-failure rebuilds that arrived mid-play; queued once the session ends.
    std::vector<fs::path> scriptLoadFailurePendingSources;
    // Deferred so a reload asked for from a context menu doesn't tear the script runtime
    // down in the middle of the ImGui pass that asked for it.
    bool scriptDomainReloadPending = false;
    bool scriptDomainReloadClearsCache = false;
    struct ProjectLoadResult {
        bool success = false;
        Project project;
        std::string error;
        std::string path;
    };
    bool projectLoadInProgress = false;
    double projectLoadStartTime = 0.0;
    // Wall-clock trace of the click -> project-open path. Offsets are measured
    // from the click, so a gap between two consecutive lines is blocking work
    // that nothing has instrumented yet - which is exactly what a freeze looks
    // like from the outside.
    std::chrono::steady_clock::time_point projectLoadTraceOrigin{};
    bool projectLoadTraceActive = false;
    void traceProjectLoad(const char* tag);
    std::string projectLoadPath;
    std::future<ProjectLoadResult> projectLoadFuture;
    double startupSplashStartTime = -1.0;
    bool sceneLoadInProgress = false;
    float sceneLoadProgress = 0.0f;
    std::string sceneLoadStatus;
    std::string sceneLoadSceneName;
    std::vector<SceneObject> sceneLoadObjects;
    std::vector<size_t> sceneLoadAssetIndices;
    size_t sceneLoadAssetsDone = 0;
    int sceneLoadNextId = 0;
    int sceneLoadVersion = 9;
    SceneSerializer::Metadata sceneLoadMetadata;
    float sceneLoadTimeOfDay = -1.0f;
    float sceneTimeOfDay = 0.5f;
    SkyboxSettings sceneLoadSkyboxSettings;
    SkyboxSettings sceneSkyboxSettings;
    bool specMode = false;
    bool testMode = false;
    bool collisionWireframe = false;
    bool fpsCapEnabled = false;
    float fpsCap = 120.0f;
    // Editor-window vsync. The project's graphicsSettings.vsync is a *player*
    // setting (Runtime Graphics), so the editor gets its own knob rather than
    // inheriting one that is documented as shipping behavior. On by default:
    // free-running the editor at ~1000 FPS is what makes glfwSwapBuffers block
    // erratically on driver back-pressure, which reads as stutter.
    bool editorVSyncEnabled = true;
    // -1 = nothing applied yet, so the first apply always reaches glfwSwapInterval.
    int presentSwapIntervalApplied = -1;
    // Absolute deadline the pacer is steering toward, carried forward frame to
    // frame so scheduler overshoot cancels out instead of accumulating.
    double framePacingDeadline = 0.0;
    double framePacingInterval = 0.0;
    // Adaptive estimate (ms) of how far past its requested duration an OS sleep
    // actually lands. The pacer sleeps short by this much and yields the rest.
    double framePacingSleepOvershootMs = 1.0;
    uint64_t renderFrameSerial = 0;
    struct UIStylePreset {
        std::string name;
        ImGuiStyle style;
        // Depth/shading half of the theme (gradients, bevels, per-edge borders). Default
        // constructs to "disabled", so a preset saved before shading existed keeps rendering flat.
        ImGuiShadeTheme shade;
        std::string fontAsset = "builtin:imgui-default";
        bool builtin = false;
    };
    struct UIFontCatalogEntry {
        std::string id;
        std::string label;
        fs::path path;
        bool builtin = false;
        bool isDefault = false;
    };
    struct UIFontContextState {
        std::unordered_map<std::string, ImFont*> loadedFonts;
    };
    std::vector<UIStylePreset> uiStylePresets;
    std::vector<UIFontCatalogEntry> uiFontCatalog;
    std::unordered_map<ImGuiContext*, UIFontContextState> uiFontContexts;
    int uiStylePresetIndex = 0;
    std::string uiEditorFontAsset = "builtin:imgui-default";
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
    ScriptLanguageServiceDocumentData scriptLanguageDocument;
#if !MODULARITY_RUNTIME_ONLY
    TextEditor scriptTextEditor;
    bool scriptTextEditorReady = false;
#endif
    char scriptingFilter[128] = "";
    bool scriptingFilesDirty = true;
    struct RuntimeAnimKey {
        float time = 0.0f;
        float value = 0.0f;
        float inTangent = 0.0f;
        float outTangent = 0.0f;
        int interpolation = 1; // 0=constant, 1=linear, 2=cubic
    };
    struct RuntimeAnimTrack {
        std::string propertyId;
        std::vector<RuntimeAnimKey> keys;
    };
    struct RuntimeAnimBinding {
        std::string path;
        std::vector<RuntimeAnimTrack> tracks;
    };
    struct RuntimeAnimationClip {
        std::string name;
        int rootObjectId = -1;
        float duration = 2.0f;
        float sampleRate = 30.0f;
        std::vector<RuntimeAnimBinding> bindings;
    };
    struct RuntimeClipCacheEntry {
        RuntimeAnimationClip clip;
        fs::file_time_type lastWriteTime{};
        bool hasWriteTime = false;
        bool valid = false;
    };
    std::unordered_map<std::string, RuntimeClipCacheEntry> runtimeAnimationClipCache;
    // Private methods
    SceneObject* getSelectedObject();
    glm::vec3 getSelectionCenterWorld(bool worldSpace) const;
    void setPrimarySelection(int id, bool additive = false);
    void clearSelection();
    // referenceScale (optional): the transform's previous scale. Used only to decide
    // which axis carries a mirror when the matrix has a negative determinant, so a
    // negative scale stays on the axis the user set it on.
    // referenceRotationDeg (optional): the transform's previous euler angles. Picks
    // between the two equivalent XYZ spellings so Y rotates continuously through +-90
    // instead of folding back while X and Z jump by 180.
    static void DecomposeMatrix(const glm::mat4& matrix, glm::vec3& pos, glm::vec3& rot, glm::vec3& scale,
                                const glm::vec3* referenceScale = nullptr,
                                const glm::vec3* referenceRotationDeg = nullptr);
    static glm::mat4 ComposeTransform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale);
    static glm::mat4 ComposeTransform(const glm::vec3& position, const glm::vec3& rotationDeg, const glm::vec3& scale);
    void refreshSceneObjectIndexCache();
    void markRuntimeScriptBindingsDirty() { runtimeScriptBindingsVersion++; }
    void rebuildRuntimeScriptBindings();
    // rebuildWorldFromLocal: derive every object's world transform from its local one,
    // including the physics-driven objects that normally own their world transform. Only
    // the scene loader needs it - see initializeLocalTransformsFromWorld.
    void updateHierarchyWorldTransforms(bool rebuildWorldFromLocal = false);
    void updateLocalFromWorld(SceneObject& obj, const glm::vec3& parentPos, const glm::quat& parentRot, const glm::vec3& parentScale);
    void initializeLocalTransformsFromWorld(int sceneVersion);
    static bool isScriptScanExcludedDir(const std::string& name, bool atScanRoot);
    void queueScriptCompile(const fs::path& scriptPath);
    void queueScriptCompileBatch(const std::vector<fs::path>& scriptPaths);
    void playCompileStartSound();
    bool isEditorFeedbackSoundEnabled(EditorFeedbackSoundCategory category) const;
    bool playEditorFeedbackPreview(const std::string& path,
                                   float volume,
                                   bool loop,
                                   EditorFeedbackSoundCategory category);
    bool playEditorFeedbackOneShot(const std::string& path,
                                   float volume,
                                   EditorFeedbackSoundCategory category);
    
    void importOBJToScene(const std::string& filepath, const std::string& objectName);
    void importModelToScene(const std::string& filepath, const std::string& objectName);  // Assimp import
    void convertModelToRawMesh(const std::string& filepath);
    void createRMeshPrimitive(const std::string& primitiveName);
    void createMMeshPrimitive(const std::string& primitiveName);
    void createPipelineDefaultSceneObjects();
    bool ensureMeshEditTarget(SceneObject* obj);
    bool syncMeshEditToGPU(SceneObject* obj);
    bool saveMeshEditAsset(std::string& error);
    void handleKeyboardShortcuts();
    void focusViewportOnSelection();
    void OpenProjectPath(const std::string& path);
    
    // UI rendering methods
    void renderLauncher();
    bool requiresTermsOfServiceAcceptance() const;
    void renderTermsOfServiceModal();
    void renderLegacySceneLayoutModal();
    void renderPlayModeSaveModal();
    void renderNewProjectDialog();
    void renderOpenProjectDialog();
    void renderMainMenuBar();
    // Hands ModuGUI the per-window artwork its dock tabs and menu entries draw.
    // Cheap and idempotent, so it just runs once a frame ahead of the editor UI.
    void refreshEditorWindowIcons();
    void renderSceneObjectCreateMenu();
    void renderPlayControlsBar();
    // Play-control actions, shared by the desktop play bar and the touch Quick
    // Tools toolbar so both drive identical play/spec/pause behavior.
    void togglePlayMode();
    void toggleSpecMode();
    void togglePause();
    void renderEnvironmentWindow();
    void renderCameraWindow();
    void renderAnimationWindow();
    void renderAIPathfindingWindow();
    void renderPixelSpriteEditorWindow();
    void renderVisualScriptingWindow();
    // Sector Map graph window + map creation helpers (SectorMapWindow.cpp)
    void renderSectorMapWindow();
#if !MODULARITY_RUNTIME_ONLY
    // Lightmapping (Nebula). EditorWindows/LightmappingWindow.cpp
    void renderLightmappingWindow();
    // Collects the static geometry + lights a bake should cover. Copies all
    // vertex data, so this is only called when a bake actually starts.
    Nebula::BakeSceneData collectLightmapBakeScene() const;
    // Cheap counterpart for the window footer: counts triangles and world
    // bounds without copying geometry.
    void refreshLightmapStats();
    // Drives the running bake and mirrors it into the corner progress toast.
    void updateLightmapBake();
    void startLightmapBake();
    void releaseLightmapPreviewTexture();
#endif
    SceneObject* ensureMapRootObject(bool recordUndo);
    SceneObject* createMapSectorObject(const std::string& name, const glm::vec2& graphPos,
                                       bool recordUndo);
    SceneObject* createMapTransitionObject(const std::string& sourceSectorId,
                                           const std::string& destinationSectorId,
                                           bool recordUndo);
    void focusMapSector(int sectorObjectId);
    // Room generation + carve-connect workflow (MapMakerTools.cpp).
    // Presets: 0 Small, 1 Medium, 2 Large, 3 Corridor, 4 Empty Sector,
    // 5 Vertical Shaft, 6 Stair Room.
    bool mapRoomPresetDimensions(int preset, glm::vec3& outInnerSize) const;
    SceneObject* createRoomMeshObject(RawMeshAsset&& roomMesh, const std::string& name,
                                      const glm::vec3& position, float yawDeg,
                                      std::string& error);
    // Creates sector + generated room shell at position (undoable as one step
    // when recordUndo is true). Returns the sector object, or null on failure.
    SceneObject* createMapRoomSectorAt(int preset, const glm::vec3& floorPosition,
                                       float yawDeg, const glm::vec2& graphPos,
                                       bool recordUndo);
    // After a successful through-carve: builds the destination room, carves
    // its matching opening, and links portals + transition + graph node.
    void createConnectedSectorThroughCarve(SceneObject* wallObject,
                                           const MapCarve::CarveOutcome& carve,
                                           const MapCarve::PlaneBasis& basisLocal,
                                           const glm::mat4& wallModelMatrix);
    // Recomputes the transient editorSectorHidden flags from the Map Root's
    // sector visibility mode + active sector. Cleared entirely while playing.
    void applyMapSectorVisibility();
    void renderHierarchyPanel();
    void renderObjectNode(SceneObject& obj, const std::string& filter,
                          std::vector<bool>& ancestorHasNext, std::unordered_set<int>& renderPath,
                          bool isLast, int depth, float animStep);
    void renderFileBrowserPanel();
    void renderMeshBuilderPanel();
    void renderInspectorPanel();
    void renderConsolePanel();
    void renderLatestErrorBar();
    void renderEditorToast();
    void renderViewport();
    void renderPlayerViewport();
    // runtime "lite" dev overlay (player only, dev builds): touch-friendly inspector for live
    // tweaking on device. RuntimeDevOverlay.cpp; compiled into the player too.
    void renderRuntimeDevOverlay();
    // movable touch toolbar (Undo/Redo/Save) for the editor on touch screens, so those don't
    // need tiny menu pokes. TouchEditToolbar.cpp; no-op in the player.
    void renderTouchEditToolbar();
    // finger drag-to-scroll for ImGui windows (Android; ImGui has no native touch scrolling).
    // returns true if the press should still hit ImGui as a click, false if it got swallowed
    // as a scroll drag. TouchScroll.cpp.
    bool updateAndroidTouchScroll(float px, float py, bool active);
    bool devOverlayOpen_ = false;
    int devOverlaySelectedId_ = -1;
    // global UI scale from setupImGui (display density on Android, monitor scale on desktop),
    // shared so launcher/touch overlays don't each guess their own.
    float uiDpiScale = 1.0f;
    // --play / "dev play": once the startup project + scene load, boot straight into the
    // running game with the dev overlay on and the cursor usable.
    bool autoPlayOnLoad_ = false;
    bool devToolsForced_ = false;

public:
    void setAutoPlayDevMode(bool on) { autoPlayOnLoad_ = on; devToolsForced_ = on; }
private:
    void renderGameViewportWindow();
    void drawGameProfilerContent();
    void renderGameProfilerWindow();
    void renderUiCanvas3DTargets();
    void renderBuildSettingsWindow();
    void renderModularityDoctorWindow();
    void renderScriptingWindow();
    void renderModuPakExportDialog();
    void renderModuPakImportDialog();
    void renderModuObjExportDialog();
    void renderModuObjImportDialog();
    void openModuPakExportDialog(const std::vector<fs::path>& seedPaths);
    void openModuPakImportDialog(const fs::path& packagePath = {});
    void openModuObjExportDialog();
    void openModuObjImportDialog(const fs::path& packagePath = {});
    void renderDialogs();
    void updateCompileJob();
    void renderProjectBrowserPanel();
    void renderRegistryPackagesWindow();
    void renderScriptEditorWindows();
    void refreshScriptEditorWindows();
    void refreshScriptingFileList();
    Camera makeCameraFromObject(const SceneObject& obj) const;
    const SceneObject* findPlayerCameraObject() const;
    Light2DPostFXSettings resolveWorld2DPostFx(const Camera& effectCamera) const;
    Light2DPostFXSettings resolveWorld2DPostFx(const UIWorldCamera2D& effectCamera) const;
    void compileScriptFile(const fs::path& scriptPath);
    // Tops up compileWorkers from compileRequestQueue up to the parallel job cap.
    void startQueuedCompileJobs();
    void updateAutoCompileScripts();
    // Pure filesystem half of updateAutoCompileScripts, run on a worker thread.
    // Static on purpose: it must not reach engine state.
    static ScriptAutoCompileScanResult runScriptAutoCompileScan(ScriptAutoCompileScanRequest request);
    void applyScriptAutoCompileScanResult(ScriptAutoCompileScanResult& result);
    void processAutoCompileQueue();
    void queueAutoCompile(const fs::path& scriptPath, const fs::file_time_type& sourceTime);
    void resetScriptRuntimeStateForReload(bool clearBinaryPaths);
    // Script history (compiled-binary provenance). See ScriptHistory.h.
    void ensureScriptHistoryLoaded();
    void recordScriptHistoryEntry(const fs::path& sourcePath, const fs::path& binaryPath);
    void flushScriptHistory();
    // Re-queues a script whose compiled binary the loader refused for an ABI/layout
    // reason. Anything else (missing file, broken .so) is left to the existing logging.
    void requestRecompileForScriptLoadFailure(const fs::path& sourcePath,
                                              ScriptLoadFailure failure);
    // Unity-style domain reload: drop every loaded script module and its state, forget
    // the cached binary bindings, and rebuild whatever is stale. `clearCompiledScripts`
    // additionally wipes outDir first, which forces a from-scratch rebuild of everything.
    // Deferred to the top of the next frame via requestScriptDomainReload.
    void requestScriptDomainReload(bool clearCompiledScripts);
    void updateScriptReloadRequests();
    void reloadScriptDomain(bool clearCompiledScripts);
    void getSceneViewportInternalResolution(int& outWidth, int& outHeight) const;
    float getSceneViewportInternalAspect() const;
    void getRuntimeInternalResolution(int& outWidth, int& outHeight) const;
    float getRuntimeInternalAspect() const;
    // pushes the project's Graphics/Lighting settings (light budget, shadows, color precision,
    // texture default) into the live renderer. runs on project load + settings changes.
    void applyProjectGraphicsToRenderer();
    // Pushes vsync into the swap chain: editorVSyncEnabled drives the editor
    // window, the project's graphicsSettings.vsync drives the player.
    void applyPresentationSettings();
    // Sleeps off whatever is left of the frame budget after presenting.
    void paceFrame(double frameStart);
    void startProjectLoad(const std::string& path);
    void pollProjectLoad();
    void finishProjectLoad(ProjectLoadResult& result);
    void beginDeferredSceneLoad(const std::string& sceneName);
    void pollSceneLoad();
    void finalizeDeferredSceneLoad();
    void syncPlayerCamera();
    void updateScripts(float delta);
    void dispatchPhysicsCollisionEvents(float delta);
    void updatePlayerController(float delta);
    // The gait model: one eased speed and one stride phase, shared by the head bob
    // and the footstep audio so the two cannot drift apart. Runs whether or not View
    // Motion is on, because footsteps are useful without a bobbing camera.
    // Returns how many footfalls landed this frame (normally 0 or 1).
    int updatePlayerGait(SceneObject& player,
                         PlayerControllerRuntimeState& runtime,
                         float planarSpeed,
                         bool grounded,
                         float delta);
    void updatePlayerMovementAudio(SceneObject& player,
                                   PlayerControllerRuntimeState& runtime,
                                   int footfalls,
                                   bool jumped,
                                   bool landed,
                                   float landingImpactSpeed);
    // Cosmetic view motion layered on top of the finished movement step. Reads the
    // frame's movement result, writes the camera bob offset and the extra view angles
    // (returned via outAngleOffsetDeg so the caller folds them into one rotation
    // compose rather than stacking two Euler conversions).
    void updatePlayerViewMotion(SceneObject& player,
                                PlayerControllerRuntimeState& runtime,
                                const glm::vec2& lookAngleDelta,
                                float landingImpactSpeed,
                                bool landed,
                                float jumpChargeRatio,
                                bool jumpLaunched,
                                float delta,
                                glm::vec3& outAngleOffsetDeg);
    void releasePlayerViewMotionChildren(PlayerControllerRuntimeState& runtime);
    void updateRigidbody2D(float delta);
    void updateCameraFollow2D(float delta);
    void syncVideoPlayers(float delta);
    void clearVideoPlayers();
    fs::path resolveProjectAssetPath(const std::string& rawPath) const;
    void updateRuntimeAnimations(float delta);
    void updateAIAgents(float delta);
    void updateSkeletalAnimations(float delta);
    void updateSkinningMatrices();
    fs::path resolveAnimationClipPath(const std::string& storedPath) const;
    bool loadRuntimeAnimationClipFile(const fs::path& path, RuntimeAnimationClip& outClip) const;
    const RuntimeAnimationClip* getRuntimeAnimationClip(const std::string& storedPath);
    float getAnimationDurationForObject(const SceneObject& obj) const;
    bool applyRuntimeAnimatedProperty(SceneObject& obj, const std::string& propertyId, float value);
    void evaluateRuntimeAnimationClip(const RuntimeAnimationClip& clip, float time, int rootObjectId);
    void rebuildSkeletalBindings();
    void initUIStylePresets();
    int findUIStylePreset(const std::string& name) const;
    const UIStylePreset* getUIStylePreset(const std::string& name) const;
    void registerUIStylePreset(const std::string& name, const ImGuiStyle& style, bool replace);
    // 'shade' is optional: pass nullptr to keep an existing preset's shading, or to register a
    // flat preset (scripts registering a style they built by hand hit this path).
    void upsertUIStylePreset(const std::string& name, const ImGuiStyle& style, const std::string& fontAsset, bool replace, const ImGuiShadeTheme* shade = nullptr);
    bool applyUIStylePresetByName(const std::string& name);
    bool saveCurrentUIStyleToPreset(const std::string& name, bool replaceExisting);
    // Optional live theme file, see the Theme Hot Reload region in Engine.cpp.
    fs::path getEditorThemeFilePath() const;
    bool loadEditorThemeFile(const fs::path& path, std::string* outError = nullptr);
    bool saveEditorThemeFile(std::string* outError = nullptr);
    void pollEditorThemeFile();
    double editorThemeFileNextCheck = 0.0;
    fs::file_time_type editorThemeFileLastWrite{};
    bool editorThemeFileTracked = false;
    void refreshUIFontCatalog();
    int findUIFontCatalogIndex(const std::string& id) const;
    fs::path resolveUIFontPath(const std::string& id) const;
    void preloadUIFontCatalogForContext(ImGuiContext* context);
    ImFont* getUIFontForContext(const std::string& fontAsset, ImGuiContext* context);
    bool applyEditorUIFontById(const std::string& fontAsset);
    std::string getDefaultEditorUIFontAsset() const;
    void applyWorkspacePreset(WorkspaceMode mode, bool rebuildLayout);
    void buildWorkspaceLayout(WorkspaceMode mode);
    void updateDockDrawerInteractions();
    void renderWindowsDisclaimerPopup();
    // Offers the hardware benchmark, once, on a machine that looks weak enough to
    // be worth measuring. Answering it either way is what sets
    // preferences.lowSpecPromptAnswered, so a decline sticks.
    void renderLowSpecBenchmarkPopup();
    void renderAndroidStorageAccessPopup();
    void renderModuCppNvimWarningPopup();
    // First-launch only: offers to match the editor language to the OS locale.
    // Never switches without an explicit answer.
    void renderEditorLanguagePromptPopup();
    void checkModuCppNvimUsage();
    bool bakeAIPathGrid(bool logResult);
    bool findAIPath(const glm::vec3& start, const glm::vec3& goal, std::vector<glm::vec3>& outPath, float clearancePadding = 0.0f) const;
    void autosaveWorkspaceLayout();
    void saveWorkspaceLayout(WorkspaceMode mode) const;
    fs::path getEditorUserSettingsPath() const;
    fs::path getEditorLayoutPath() const;
    // XR scene components (src/XR/XRComponents.cpp)
    // Applies tracked poses to the rig, resolves the XR Origin transform, and
    // runs the interaction pass. A no-op when no XR session is live.
    void updateXRComponents(float deltaTime);
    void updateXRInteraction(float deltaTime);
    // The single enabled XR Origin / XR Camera in the scene, or nullptr.
    const SceneObject* findXROriginObject() const;
    const SceneObject* findXRCameraObject() const;
    // Grab lifecycle. beginXRGrab returns false when the interactable refuses the
    // hand (wrong hand allowed, or already held).
    bool beginXRGrab(SceneObject& interactable, SceneObject& interactor,
                     XRActionBasedControllerComponent* controller);
    void updateXRGrabFollow(SceneObject& interactable, SceneObject& interactor, float deltaTime);
    void endXRGrab(SceneObject& interactable, SceneObject& interactor);
    // Interactable ids gathered once per interaction pass, so N interactors cost
    // one scene walk rather than N. Member rather than local to keep the
    // allocation out of the frame (section 42).
    std::vector<int> xrInteractableScratch;

    // OpenXR (src/XRRenderPath.cpp)
    // True when the loaded project asks for XR and this build can provide it.
    bool isXRRequestedByProject() const;
    // Brings XR up on first use and pumps its event loop. Safe and cheap to call
    // every frame; a no-op when the project does not want XR.
    void updateXRSystem();
    // Renders the scene once per eye into the OpenXR swapchains and submits the
    // frame. Returns true when a frame was actually submitted to the compositor.
    bool renderXRFrame();
    void shutdownXRSystem();

    fs::path getWorkspaceLayoutPath(WorkspaceMode mode) const;
    void loadEditorUserSettings();
    void saveEditorUserSettings() const;
    // Global (per-user) preferences from launcher_settings.modu. Applied once
    // at boot before any project exists, and again whenever the launcher's
    // Settings tab changes something, so the editor never has to be restarted
    // to see a preference take effect. Per-project editor settings are loaded
    // after this and still win where the two overlap.
    void applyGlobalEditorPreferences(bool applyAppearance = true);
    // Inverse: snapshot the live editor state back into the preference struct
    // so "what the editor is doing now" is what gets written out.
    void captureGlobalEditorPreferences();
    // Registers or tears down the frosted-glass renderer to match the current
    // hardware tier. Called from applyGlobalEditorPreferences so a change in the
    // Settings tab takes effect immediately instead of needing a restart.
    void syncGlassBlurToHardwareTier();
    // Installs the launcher's default ModuPAK set into a freshly created
    // project. Returns the ids that actually landed.
    std::vector<std::string> installDefaultPackagesIntoProject();
    void exportEditorThemeLayout();
    bool isProject2DPipeline() const;
    bool isProject25DPipeline() const;
    bool is2DWorldEditingEnabled() const;
    void applyProjectPipelineDefaults(bool force = false);
    bool renderTMViewportPass(const Camera& renderCamera,
                              int width,
                              int height,
                              float fovDeg,
                              float nearPlane,
                              float farPlane,
                              unsigned int* outTexture = nullptr,
                              Modularity::Render25D::TMRenderer::RenderStats* outStats = nullptr,
                              std::string* outError = nullptr,
                              int previewSlot = -1);
    unsigned int getActiveSceneTexture() const;
    std::string buildTMOverlayLabel(const Modularity::Render25D::TMRenderer::RenderStats& stats,
                                    const std::string& error) const;
    int resolveSpriteSheetFrame(const SceneObject& obj) const;
    glm::vec2 getSpriteDisplaySize(const SceneObject& obj) const;
    std::array<ImVec2, 4> buildSpriteSheetUvs(const SceneObject& obj) const;
    void resetBuildSettings();
    void loadBuildSettings();
    void saveBuildSettings();
    void selectBuildSettingsProfile(const fs::path& relativeProfilePath);
    void saveBuildSettingsProfileAs(const std::string& profileName);
    void duplicateBuildSettingsProfile(const std::string& profileName);
    bool deleteBuildSettingsProfile(const std::string& profileName);
    bool addSceneToBuildSettings(const std::string& sceneName, bool enabled);
    fs::path resolveSplashImagePath() const;
    void applyBuildWindowTitle();
    void loadAutoStartConfig();
    void applyAutoStartMode();
    void startExportBuild(const fs::path& outputDir, bool runAfter);
    void pollExportBuild();

public:
    // headless Android APK build (./build.sh --Android -> --build-android). no window/GL:
    // construct an Engine, call this, return the exit code. with a project it reuses the full
    // export pipeline; empty projectPath = bare player APK. 0 on success.
    struct AndroidBuildRequest {
        std::string projectPath; // empty => bare player APK
        std::string abi = "arm64-v8a";
        std::string outputApk;   // -o destination; copied from the produced APK
        std::string outputDir;   // staging/output root; defaults beside the APK
        bool debug = false;
        bool editor = false;     // build the Modularity editor APK instead of the player
    };
    int buildAndroidApkHeadless(const AndroidBuildRequest& request, std::string& error);
    int buildBareAndroidApk(const AndroidBuildRequest& request, std::string& error);
private:

    void renderFileBrowserToolbar();
    void renderFileBrowserBreadcrumb();
    void renderFileBrowserGridView();
    void renderFileBrowserListView();
    void renderFileContextMenu(const fs::directory_entry& entry);
    void handleFileDoubleClick(const fs::directory_entry& entry);
    void openScriptInEditor(const fs::path& path);
    bool createScriptAsset(ScriptScaffoldKind kind,
                           const std::string& requestedName,
                           const fs::path& preferredDirectory,
                           fs::path& outPath,
                           ScriptLanguage& outLanguage,
                           std::string& outManagedType,
                           std::string& error);
    ImVec4 getFileCategoryColor(FileCategory category) const;
    const char* getFileCategoryIconText(FileCategory category) const;
    
    // Project/scene management
    void createNewProject(const char* name, const char* location);
    void loadRecentScenes();
    bool saveCurrentScene(bool allowLegacyUpgradePrompt = true);
    void saveProjectPreview();
    fs::path getProjectPreviewPath(const fs::path& projectPathOrFile) const;
    void loadScene(const std::string& sceneName);
    void createNewScene(const std::string& sceneName);
    void applySceneTimeOfDay(float timeOfDay);
    float getSceneTimeOfDay();
    void applySceneSkyboxSettings(const SkyboxSettings& settings);
    SkyboxSettings getSceneSkyboxSettings() const;
    bool requestSceneSave(const std::string& destinationSceneName,
                          PendingScenePostAction postAction,
                          const std::string& postActionPayload,
                          bool allowLegacyUpgradePrompt = true);
    // requestSceneSave minus the play-mode prompt. The prompt's buttons call this
    // directly once the user has answered, so answering cannot re-open the prompt.
    bool performSceneSaveRequest(const std::string& destinationSceneName,
                                 PendingScenePostAction postAction,
                                 const std::string& postActionPayload,
                                 bool allowLegacyUpgradePrompt);
    bool executeSceneSave(const std::string& destinationSceneName,
                          SceneSerializer::SavePreference preference,
                          bool moveLegacySourceToCompatibility);
    void continuePendingScenePostAction();
    void resetPendingSceneSaveRequest();
    void initializeNewSceneSerializationState(const fs::path& sourcePath = {});
    void performLoadScene(const std::string& sceneName);
    void performCreateNewScene(const std::string& sceneName);
    void performCloseProject();
    
    // Scene object management
    void addObject(ObjectType type, const std::string& baseName);
    void duplicateSelected();
    void copySelected();
    void pasteClipboard();
    void selectAllObjects();
    void deleteSelected();
    void setParent(int childId, int parentId, int beforeSiblingId = -1);
    void loadMaterialFromFile(SceneObject& obj);
    void saveMaterialToFile(const SceneObject& obj);
    SceneSnapshot captureSceneSnapshot() const;
    void restoreSceneSnapshot(SceneSnapshot snap);
    void pushUndoSnapshot(SceneSnapshot snap, const char* reason = "");
    // Assemblage editing. All of these address cells, never SceneObjects, and
    // never hold a pointer into the scene vector across a frame.
    //
    // Editor-only: defined in EditorWindows/AssemblageWindow.cpp and
    // AssemblageViewport.cpp, both excluded from the player build. Declaring
    // them behind the same guard means runtime code that reaches for one fails
    // to compile here rather than failing to link the player much later.
    // Loading and drawing an Assemblage is NOT in here - that is
    // AssemblageRuntime and ViewportRenderHelpers, which the player does build.
#if !MODULARITY_RUNTIME_ONLY
    void renderAssemblageWindow();
    // Grid, cell cursor and the painting tools. Called from the 2D viewport
    // once the scene has been drawn, so the overlay sits on top of the tiles.
    void renderAssemblageViewportOverlay(const ImVec2& overlayPos,
                                         const ImVec2& overlaySize,
                                         bool viewportHovered);
    SceneObject* getActiveAssemblageRoot();
    // Resolves the current edit target. False when no Assemblage/layer is
    // selected or the asset failed to load.
    bool resolveAssemblageEditTarget(std::string& outAssetPath,
                                     Assemblage::Asset*& outAsset,
                                     int& outLayerId);
    void beginAssemblageStroke(const char* label);
    bool applyAssemblageCell(int cellX, int cellY, uint32_t value);
    void endAssemblageStroke();
    void applyAssemblageEdit(const AssemblageEditRecord& record, bool forward);
    void assemblageFloodFill(int cellX, int cellY, uint32_t value);
    void assemblageFillRect(glm::ivec2 a, glm::ivec2 b, uint32_t value);
    void assemblageDrawLine(glm::ivec2 a, glm::ivec2 b, uint32_t value);
    void assemblageCopySelection();
    void assemblagePasteAt(glm::ivec2 origin);
    uint32_t resolveAssemblageBrushCell();
    // Turns a dropped image into tiles: one per .spritesheet region when the
    // sheet has a sidecar, otherwise a single whole-image tile.
    void addAssemblageTilesFromImage(const std::string& droppedPath);
    // Creates an Assemblage root plus a first layer, and the backing asset.
    // Returns the root object id, or -1 on failure.
    int createAssemblageInScene(const std::string& name);
    // Adds/removes a layer on the active Assemblage, keeping the layer objects
    // in the hierarchy in step with the asset's layer table.
    int addAssemblageLayer(const std::string& name);
    void removeAssemblageLayer(int layerId);
    void syncAssemblageLayerObjects(int rootObjectId);
    bool saveDirtyAssemblages(std::string& outError);
#endif  // !MODULARITY_RUNTIME_ONLY
    // Rooting the runtime at the project is runtime work: the player resolves
    // the same project-relative asset paths when it loads a scene.
    void syncAssemblageProjectRoot();
    void recordState(const char* reason = "");
    bool saveDirtyMeshEditAssetForSceneSave();
    void capturePlayModeSnapshot();
    void restorePlayModeSnapshot();
    void undo();
    void redo();
    
    // Console/logging
    void addConsoleMessage(const std::string& message, ConsoleMessageType type);

    // Networking. The session is started when a running mode begins and a scene
    // NetworkManager exists, ticked each frame, and shut down when play stops, so
    // no network state survives a play session. Its log sink is routed into the
    // editor console.
    void updateNetworking(float delta);
    Net::NetworkSession networkSession;
    bool networkSessionActive = false;
    // assetId -> .moduobj path, rebuilt when the project changes. Networking
    // spawns by asset id, and the engine has no general asset database yet.
    std::unordered_map<std::string, fs::path> moduObjAssetIndex;
    std::string moduObjAssetIndexRoot;
    ModuObj::AssetCache moduObjAssetCache;
    void rebuildModuObjAssetIndex();
    void logToConsole(const std::string& message);
    void showEditorToast(const std::string& message,
                         ConsoleMessageType type = ConsoleMessageType::Info,
                         double holdSeconds = 1.8);
    // Corner progress card, reusing the toast surface. Call repeatedly to
    // update; progress01 < 0 renders an indeterminate sweep.
    void showEditorProgressToast(const std::string& title,
                                 const std::string& detail,
                                 float progress01,
                                 ConsoleMessageType type = ConsoleMessageType::Info);
    // Swaps the progress card back to a normal auto-fading message toast.
    void finishEditorProgressToast(const std::string& message,
                                   ConsoleMessageType type,
                                   double holdSeconds = 2.6);

    // Material helpers
    bool loadMaterialData(const std::string& path, MaterialProperties& props,
                          std::string& albedo, std::string& overlay,
                          std::string& normal, bool& useOverlay,
                          std::string* shaderPackOut = nullptr,
                          std::string* vertexShaderOut = nullptr,
                          std::string* fragmentShaderOut = nullptr);
    bool saveMaterialData(const std::string& path, const MaterialProperties& props,
                          const std::string& albedo, const std::string& overlay,
                          const std::string& normal, bool useOverlay,
                          const std::string& shaderPack,
                          const std::string& vertexShader,
                          const std::string& fragmentShader);
    bool applyTextureAssetToObject(SceneObject& obj, const fs::path& texturePath);
    bool hasInstalledPackage(const char* id) const;
    bool hasSpriteEditorPackage() const;
    bool hasSpritesheetPackage() const;
    bool has2DWorldPackage() const;
    bool hasMeshBuilderPackage() const;
    bool hasScriptingWindowPackage() const;
    bool hasVulkanPipelinePackage() const;
    void clampOptionalPackageState(bool logMissingRenderer = false);
    
    // ImGui setup
    void setupImGui();
    bool initRenderer();
    bool initVulkanRenderer();
    Modularity::GraphicsBackend resolveRequestedBackend() const;
    bool usingVulkan() const { return graphicsBackend == Modularity::GraphicsBackend::Vulkan; }
    void onWindowResized(int width, int height);

public:
    Engine() = default;

    void setStartupProjectPath(const std::string& path);
    bool init();
    void run();
    void shutdown();
    SceneObject* findObjectByName(const std::string& name);
    SceneObject* findObjectById(int id);
    bool propagateObjectRenameReferences(const std::string& oldName,
                                         const std::string& newName,
                                         int renamedObjectId = -1);
    bool loadPixelSpriteDocument(const fs::path& imagePath);
    bool savePixelSpriteDocument();
    fs::path resolveScriptBinary(const fs::path& sourcePath);
    fs::path resolveManagedAssembly(const fs::path& sourcePath);
    fs::path getManagedProjectPath() const;
    fs::path getManagedOutputDll() const;
    void compileManagedScripts();
    void markProjectDirty();
    // Script-accessible logging wrapper
    void addConsoleMessageFromScript(const std::string& message, ConsoleMessageType type);
    fs::path resolveProjectPathFromScript(const std::string& rawPath) const;
    fs::path getProgramRootPathFromScript() const;
    fs::path getEngineDocsRootPathFromScript() const;
    fs::path getPersistentDataPathFromScript(const std::string& subFolder) const;
    std::string getSelectedFilePathFromScript() const;
    std::string getSelectedObjectInfoFromScript() const;
    std::string getSceneHierarchyFromScript(int maxObjects = 0) const;
    // Scene editing for AI/agent tooling (addressed by object id).
    int  createSceneObjectFromScript(const std::string& type, const std::string& name, int parentId);
    bool deleteSceneObjectFromScript(int objectId);
    bool renameSceneObjectFromScript(int objectId, const std::string& name);
    bool setSceneObjectParentFromScript(int objectId, int parentId);
    bool setSceneObjectTransformFromScript(int objectId, const glm::vec3& position,
                                           const glm::vec3& rotationDeg, const glm::vec3& scale);
    bool setSceneObjectEnabledFromScript(int objectId, bool enabled);
    bool addObjectComponentFromScript(int objectId, const std::string& component);
    bool attachObjectScriptFromScript(int objectId, const std::string& scriptPath);
    std::string getProjectNameFromScript() const;
    std::string getCurrentSceneNameFromScript() const;
    std::string httpPostFromScript(const std::string& url, const std::string& contentType,
                                   const std::string& body, const std::string& headers);
    int startHttpPostFromScript(const std::string& url, const std::string& contentType,
                                const std::string& body, const std::string& headers, bool stream);
    bool pollHttpPostFromScript(int requestId, std::string& outChunk, bool& outDone, bool& outSuccess);
    void cancelHttpPostFromScript(int requestId);
    int startProcessFromScript(const std::string& command, const std::string& workingDir,
                               bool interactive);
    bool writeProcessStdinFromScript(int processId, const std::string& data);
    bool pollProcessFromScript(int processId, std::string& outChunk, bool& outDone,
                               bool& outSuccess, int& outExitCode);
    void cancelProcessFromScript(int processId);
    // Blocking helpers used by the synchronous script tool pipeline (the script's
    // own clock is frame-based and can't advance inside a tool call, so the wait
    // and timeout happen here on the engine thread).
    std::string runProcessBlockingFromScript(const std::string& command,
                                             const std::string& workingDir,
                                             int timeoutMs, int* exitCode);
    bool readProcessLineFromScript(int processId, std::string& outLine, int timeoutMs);
    std::string readFileTextFromScript(const std::string& path) const;
    std::string readFileBase64FromScript(const std::string& path, size_t maxBytes = 16 * 1024 * 1024) const;
    bool writeFileTextFromScript(const std::string& path, const std::string& content);
    bool deleteFileFromScript(const std::string& path);
    std::string listFilesFromScript(const std::string& path, bool recursive, int maxEntries) const;
    std::string searchFilesFromScript(const std::string& root, const std::string& query, int maxResults) const;
    ImTextureID getUIImageTextureFromScript(const std::string& path, int* outWidth = nullptr, int* outHeight = nullptr);
    bool saveProjectFromScript();
    // Runtime input queries for script helpers.
    bool isRuntimeKeyDown(int key) const;
    bool isRuntimeMouseDown(int button) const;
    glm::vec2 getRuntimeMouseDelta() const;
    int getSelectedObjectId() const;
    // Script-accessible physics helpers
    bool setRigidbodyVelocityFromScript(int id, const glm::vec3& velocity);
    bool getRigidbodyVelocityFromScript(int id, glm::vec3& outVelocity);
    bool setRigidbodyAngularVelocityFromScript(int id, const glm::vec3& velocity);
    bool getRigidbodyAngularVelocityFromScript(int id, glm::vec3& outVelocity);
    bool teleportPhysicsActorFromScript(int id, const glm::vec3& position, const glm::vec3& rotationDeg);
    float getProjectGravityScaleFromScript() const;
    void setProjectGravityScaleFromScript(float scale);
    bool addRigidbodyForceFromScript(int id, const glm::vec3& force);
    bool addRigidbodyImpulseFromScript(int id, const glm::vec3& impulse);
    bool addRigidbodyTorqueFromScript(int id, const glm::vec3& torque);
    bool addRigidbodyAngularImpulseFromScript(int id, const glm::vec3& impulse);
    bool setRigidbodyYawFromScript(int id, float yawDegrees);
    bool raycastClosestFromScript(const glm::vec3& origin, const glm::vec3& dir, float distance,
                                  int ignoreId, glm::vec3* hitPos, glm::vec3* hitNormal,
                                  float* hitDistance, int* hitActorId = nullptr,
                                  glm::vec3* hitActorVelocity = nullptr,
                                  float* hitStaticFriction = nullptr,
                                  float* hitDynamicFriction = nullptr) const;
    // Audio control exposed to scripts
    bool playAudioFromScript(int id);
    bool stopAudioFromScript(int id);
    bool setAudioLoopFromScript(int id, bool loop);
    bool setAudioVolumeFromScript(int id, float volume);
    bool setAudioClipFromScript(int id, const std::string& path);
    bool playAudioOneShotFromScript(int id, const std::string& clipPath, float volumeScale = 1.0f);
    bool hasAnimationFromScript(int id) const;
    bool playAnimationFromScript(int id, bool restart = true);
    bool stopAnimationFromScript(int id, bool resetTime = true);
    bool pauseAnimationFromScript(int id, bool pause);
    bool reverseAnimationFromScript(int id, bool restartIfStopped = true);
    bool setAnimationTimeFromScript(int id, float timeSeconds);
    float getAnimationTimeFromScript(int id) const;
    bool isAnimationPlayingFromScript(int id) const;
    bool setAnimationLoopFromScript(int id, bool loop);
    bool setAnimationPlaySpeedFromScript(int id, float speed);
    bool setAnimationPlayOnAwakeFromScript(int id, bool playOnAwake);
    void syncLocalTransform(SceneObject& obj);
    const std::vector<SceneObject>& getSceneObjects() const { return sceneObjects; }
    const std::vector<UIStylePreset>& getUIStylePresets() const { return uiStylePresets; }
    const std::vector<UIFontCatalogEntry>& getUIFontCatalog() const { return uiFontCatalog; }
    const std::string& getEditorUIFontAsset() const { return uiEditorFontAsset; }
    void registerUIStylePresetFromScript(const std::string& name, const ImGuiStyle& style, bool replace = false);
    void setFrameRateCapFromScript(bool enabled, float cap);
};
