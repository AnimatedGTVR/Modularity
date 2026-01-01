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
#include "../include/Window/Window.h"
#include <unordered_map>

void window_size_callback(GLFWwindow* window, int width, int height);

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
    bool firstFrame = true;
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
    bool showEnvironmentWindow = true;
    bool showCameraWindow = true;
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
    bool showUITextOverlay = false;
    bool showCanvasOverlay = false;
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
    enum class MeshEditSelectionMode { Vertex = 0, Edge = 1, Face = 2 };
    MeshEditSelectionMode meshEditSelectionMode = MeshEditSelectionMode::Vertex;
    ScriptCompiler scriptCompiler;
    ScriptRuntime scriptRuntime;
    PhysicsSystem physics;
    AudioSystem audio;
    bool showCompilePopup = false;
    bool lastCompileSuccess = false;
    std::string lastCompileStatus;
    std::string lastCompileLog;
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
    void renderHierarchyPanel();
    void renderObjectNode(SceneObject& obj, const std::string& filter,
                          std::vector<bool>& ancestorHasNext, bool isLast, int depth);
    void renderFileBrowserPanel();
    void renderMeshBuilderPanel();
    void renderInspectorPanel();
    void renderConsolePanel();
    void renderViewport();
    void renderGameViewportWindow();
    void renderDialogs();
    void renderProjectBrowserPanel();
    void renderScriptEditorWindows();
    void refreshScriptEditorWindows();
    Camera makeCameraFromObject(const SceneObject& obj) const;
    void compileScriptFile(const fs::path& scriptPath);
    void updateScripts(float delta);
    void updatePlayerController(float delta);
    void updateRigidbody2D(float delta);
    void initUIStylePresets();
    int findUIStylePreset(const std::string& name) const;
    const UIStylePreset* getUIStylePreset(const std::string& name) const;
    void registerUIStylePreset(const std::string& name, const ImGuiStyle& style, bool replace);
    
    void renderFileBrowserToolbar();
    void renderFileBrowserBreadcrumb();
    void renderFileBrowserGridView();
    void renderFileBrowserListView();
    void renderFileContextMenu(const fs::directory_entry& entry);
    void handleFileDoubleClick(const fs::directory_entry& entry);
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
