#pragma once

#include "Common.h"
#include "SceneObject.h"
#include "Camera.h"
#include "Rendering.h"
#include "Render25D/TMOpenGLRenderer.h"
#include "Render25D/TMSceneBuilder.h"
#include "Render25D/TMRenderer.h"
#include "Lighting2D.h"
#include "ProjectManager.h"
#include "EditorUI.h"
#include "MeshBuilder.h"
#include "ScriptCompiler.h"
#include "ScriptDiagnostics.h"
#include "ScriptRuntime.h"
#include "PhysicsSystem.h"
#include "AudioSystem.h"
#include "PackageManager.h"
#include "ManagedScriptRuntime.h"
#include "Profiler.h"
#include "SpritesheetFormat.h"
#if !MODULARITY_RUNTIME_ONLY
#include "ThirdParty/ImGuiColorTextEdit/TextEditor.h"
#endif
#include "Vulkan/VulkanRenderer.h"
#include "../include/Window/Window.h"
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <deque>
#include <future>
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
    CSharp = 3
};

class Engine {
    friend void window_size_callback(GLFWwindow* window, int width, int height);
private:
    Window window;
    GLFWwindow* editorWindow = nullptr;
    Modularity::GraphicsBackend graphicsBackend = Modularity::GraphicsBackend::OpenGL;
    Renderer renderer;
    Modularity::Render25D::TMRenderer tmRenderer;
    Modularity::Render25D::TMOpenGLRenderer tmOpenGLRenderer;
    Modularity::Render25D::TMSceneBuilder tmSceneBuilder;
    Lighting2DRenderer lighting2DRenderer;
    std::unique_ptr<Modularity::VulkanRenderer> vulkanRenderer;
    Camera camera;
    ViewportController viewportController;
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    bool cursorLocked = false; // true only while holding right mouse for freelook
    int viewportWidth = 800;
    int viewportHeight = 600;
    bool gizmoHistoryCaptured = false;
    bool worldUiGizmoHistoryCaptured = false;
    bool gameUiGizmoHistoryCaptured = false;
    struct UiRectGizmoSnapshot {
        int objectId = -1;
        glm::vec2 position = glm::vec2(0.0f);
        glm::vec2 size = glm::vec2(0.0f);
        float rotation = 0.0f;
        ImVec2 rectMin = ImVec2(0.0f, 0.0f);
        ImVec2 rectMax = ImVec2(0.0f, 0.0f);
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
    std::string inspectedVertShader;
    std::string inspectedFragShader;
    bool inspectedUseOverlay = false;
    bool inspectedMaterialValid = false;
    struct SceneSnapshot {
        std::vector<SceneObject> objects;
        std::vector<int> selectedIds;
        int nextId = 0;
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
    std::unordered_map<int, size_t> sceneObjectIndexById;
    const SceneObject* sceneObjectIndexData = nullptr;
    size_t sceneObjectIndexCount = 0;
    struct RuntimeScriptBinding {
        int objectId = -1;
        size_t scriptIndex = 0;
    };
    std::vector<RuntimeScriptBinding> runtimeScriptBindings;
    uint64_t runtimeScriptBindingsVersion = 1;
    uint64_t runtimeScriptBindingsCachedVersion = 0;
    int selectedObjectId = -1; // primary selection (last)
    std::vector<int> selectedObjectIds; // multi-select
    std::vector<int> hierarchyVisibleOrder;
    int hierarchyRangeAnchorId = -1;
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
    bool showProjectBrowser = true;  // Now merged into file browser
    bool showRegistryPackagesWindow = false;
    bool showMeshBuilder = false;
    bool showBuildSettings = false;
    bool showStyleEditor = false;
    bool showScriptingWindow = false;
    bool firstFrame = true;
    bool playerMode = false;
    bool autoStartRequested = false;
    bool autoStartPlayerMode = false;
    bool deferInspectorRefresh = false;
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
    bool termsPopupOpened = false;
    enum class LegacySceneSaveChoice {
        Ask = 0,
        KeepLegacy = 1,
        SaveModular = 2
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
    };
    SceneSerializer::Metadata currentSceneSerialization;
    LegacySceneSaveChoice legacySceneSaveChoice = LegacySceneSaveChoice::SaveModular;
    PendingSceneSaveRequest pendingSceneSaveRequest;
    bool showLegacySceneLayoutDialog = false;
    bool legacySceneLayoutDialogOpened = false;
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
    bool showEnvironmentWindow = true;
    bool showCameraWindow = true;
    bool showAnimationWindow = false;
    bool showAIPathfindingWindow = false;
    bool showPixelSpriteEditorWindow = false;
    char registryPackageSearch[256] = "";
    std::string registryPackageSelectedId;
    int registryPackageView = 0;
    int registryPackageSubsystemFilter = 0;
    int registryPackageTypeFilter = 0;
    int registryPackageSort = 0;
    bool registryPackageLastActionSucceeded = true;
    std::string registryPackageFeedback;
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
    struct AIPathGrid {
        bool baked = false;
        glm::vec2 origin = glm::vec2(0.0f);
        int width = 0;
        int height = 0;
        float cellSize = 1.0f;
        std::vector<uint8_t> walkable;
        std::vector<int> sourceGroundIds;
        std::vector<int> sourceObstacleIds;
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
    bool hierarchyShowTexturePreview = false;
    bool audioPreviewLoop = false;
    bool audioPreviewAutoPlay = false;
    float audioPreviewVolume = 1.0f;
    float audioPreviewBaseVolume = 1.0f;
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
    bool showGameViewport = true;
    int previewCameraId = -1;
    bool gameViewCursorLocked = false;
    bool gameViewportFocused = false;
    bool showGameProfiler = true;
    bool revealDebugSectionsAndMenus = false;
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
    float meshEditUvMoveStep = 0.1f;
    float meshEditUvScaleStep = 1.1f;
    float meshEditUvRotateStep = 15.0f;
    struct UIAnimationState {
        float hover = 0.0f;
        float active = 0.0f;
        float sliderValue = 0.0f;
        float contentExtent = 0.0f;
        bool initialized = false;
    };
    std::unordered_map<int, UIAnimationState> uiAnimationStates;
    std::unordered_map<ImGuiID, UIAnimationState> editorUiAnimationStates;
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
    float light2DLightingBufferScale = 0.75f;
    Light2DPostFXSettings world2DPostFx;
    std::unordered_map<int, std::string> light2DObjectRoutingReasonsLastFrame;
    bool light2DShapeEditMode = false;
    int light2DShapeEditingObjectId = -1;
    int light2DShapeEditingPointIndex = -1;
    struct UiCanvas3DContext {
        ImGuiContext* context = nullptr;
        bool backendReady = false;
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
    enum class MeshEditSelectionMode { Object = 0, Vertex = 1, Edge = 2, Face = 3, UV = 4 };
    MeshEditSelectionMode meshEditSelectionMode = MeshEditSelectionMode::Object;
    ScriptCompiler scriptCompiler;
    ScriptRuntime scriptRuntime;
    ManagedScriptRuntime managedRuntime;
    PhysicsSystem physics;
    AudioSystem audio;
    struct EditorToastState {
        bool visible = false;
        ConsoleMessageType type = ConsoleMessageType::Info;
        std::string message;
        double startTime = 0.0;
        double holdSeconds = 1.8;
    };
    EditorToastState editorToast;
    bool showCompilePopup = false;
    bool compilePopupOpened = false;
    double compilePopupHideTime = 0.0;
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
    struct BuildSceneEntry {
        std::string name;
        bool enabled = true;
    };
    struct BuildSettings {
        BuildPlatform platform = BuildPlatform::Windows;
        std::string architecture = "x86_64";
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
    std::atomic<bool> compileResultReady = false;
    std::thread compileWorker;
    std::mutex compileMutex;
    ScriptCompileJobResult compileResult;
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
    std::unordered_map<std::string, fs::path> scriptAutoCompileBinaryCache;
    std::unordered_set<std::string> scriptAutoCompileDiscoveredSources;
    std::deque<fs::path> autoCompileQueue;
    std::unordered_set<std::string> autoCompileQueued;
    std::unordered_set<std::string> nativeScriptMissingLogged;
    std::unordered_set<std::string> nativeScriptLoadErrorLogged;
    bool managedAutoCompileQueued = false;
    double managedAutoCompileLastScan = 0.0;
    double managedAutoCompileScanInterval = 5.0;
    fs::path managedAutoCompileCachedProjectDir;
    fs::file_time_type managedAutoCompileNewestSource{};
    bool managedAutoCompileHasSource = false;
    double scriptAutoCompileLastCheck = 0.0;
    double scriptAutoCompileInterval = 0.5;
    double scriptAutoCompileLastDirectoryScan = 0.0;
    double scriptAutoCompileDirectoryScanInterval = 5.0;
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
    uint64_t renderFrameSerial = 0;
    struct UIStylePreset {
        std::string name;
        ImGuiStyle style;
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
    static void DecomposeMatrix(const glm::mat4& matrix, glm::vec3& pos, glm::vec3& rot, glm::vec3& scale);
    static glm::mat4 ComposeTransform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale);
    static glm::mat4 ComposeTransform(const glm::vec3& position, const glm::vec3& rotationDeg, const glm::vec3& scale);
    void refreshSceneObjectIndexCache();
    void markRuntimeScriptBindingsDirty() { runtimeScriptBindingsVersion++; }
    void rebuildRuntimeScriptBindings();
    void updateHierarchyWorldTransforms();
    void updateLocalFromWorld(SceneObject& obj, const glm::vec3& parentPos, const glm::quat& parentRot, const glm::vec3& parentScale);
    void initializeLocalTransformsFromWorld(int sceneVersion);
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
    void OpenProjectPath(const std::string& path);
    
    // UI rendering methods
    void renderLauncher();
    bool requiresTermsOfServiceAcceptance() const;
    void renderTermsOfServiceModal();
    void renderLegacySceneLayoutModal();
    void renderNewProjectDialog();
    void renderOpenProjectDialog();
    void renderMainMenuBar();
    void renderPlayControlsBar();
    void renderEnvironmentWindow();
    void renderCameraWindow();
    void renderAnimationWindow();
    void renderAIPathfindingWindow();
    void renderPixelSpriteEditorWindow();
    void renderHierarchyPanel();
    void renderObjectNode(SceneObject& obj, const std::string& filter,
                          std::vector<bool>& ancestorHasNext, bool isLast, int depth, float animStep);
    void renderFileBrowserPanel();
    void renderMeshBuilderPanel();
    void renderInspectorPanel();
    void renderConsolePanel();
    void renderLatestErrorBar();
    void renderEditorToast();
    void renderViewport();
    void renderPlayerViewport();
    void renderGameViewportWindow();
    void drawGameProfilerContent();
    void renderUiCanvas3DTargets();
    void renderBuildSettingsWindow();
    void renderScriptingWindow();
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
    void updateAutoCompileScripts();
    void processAutoCompileQueue();
    void queueAutoCompile(const fs::path& scriptPath, const fs::file_time_type& sourceTime);
    void resetScriptRuntimeStateForReload(bool clearBinaryPaths);
    void getSceneViewportInternalResolution(int& outWidth, int& outHeight) const;
    float getSceneViewportInternalAspect() const;
    void getRuntimeInternalResolution(int& outWidth, int& outHeight) const;
    float getRuntimeInternalAspect() const;
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
    void upsertUIStylePreset(const std::string& name, const ImGuiStyle& style, const std::string& fontAsset, bool replace);
    bool applyUIStylePresetByName(const std::string& name);
    bool saveCurrentUIStyleToPreset(const std::string& name, bool replaceExisting);
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
    bool bakeAIPathGrid(bool logResult);
    bool findAIPath(const glm::vec3& start, const glm::vec3& goal, std::vector<glm::vec3>& outPath) const;
    void autosaveWorkspaceLayout();
    void saveWorkspaceLayout(WorkspaceMode mode) const;
    fs::path getEditorUserSettingsPath() const;
    fs::path getEditorLayoutPath() const;
    fs::path getWorkspaceLayoutPath(WorkspaceMode mode) const;
    void loadEditorUserSettings();
    void saveEditorUserSettings() const;
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
    bool addSceneToBuildSettings(const std::string& sceneName, bool enabled);
    fs::path resolveSplashImagePath() const;
    void applyBuildWindowTitle();
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
    void recordState(const char* reason = "");
    void capturePlayModeSnapshot();
    void restorePlayModeSnapshot();
    void undo();
    void redo();
    
    // Console/logging
    void addConsoleMessage(const std::string& message, ConsoleMessageType type);
    void logToConsole(const std::string& message);
    void showEditorToast(const std::string& message,
                         ConsoleMessageType type = ConsoleMessageType::Info,
                         double holdSeconds = 1.8);

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
