#pragma once

#include "Common.h"
#include "SceneObject.h"
#include "Camera.h"
#include "Rendering.h"
#include "ProjectManager.h"
#include "EditorUI.h"
#include "../include/Window/Window.h"

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
    bool cursorLocked = false;
    int viewportWidth = 800;
    int viewportHeight = 600;

    std::vector<SceneObject> sceneObjects;
    int selectedObjectId = -1;
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
    bool firstFrame = true;
    std::vector<std::string> consoleLog;
    int draggedObjectId = -1;

    ProjectManager projectManager;
    bool showLauncher = true;
    bool showNewSceneDialog = false;
    bool showSaveSceneAsDialog = false;
    char newSceneName[128] = "";
    char saveSceneAsName[128] = "";
    bool rendererInitialized = false;
    
    bool showImportOBJDialog = false;
    bool showImportModelDialog = false;  // For Assimp models
    std::string pendingOBJPath;
    std::string pendingModelPath;  // For Assimp models
    char importOBJName[128] = "";
    char importModelName[128] = "";  // For Assimp models
    
    char fileBrowserSearch[256] = "";

    // Private methods
    SceneObject* getSelectedObject();
    static void DecomposeMatrix(const glm::mat4& matrix, glm::vec3& pos, glm::vec3& rot, glm::vec3& scale);
    
    void importOBJToScene(const std::string& filepath, const std::string& objectName);
    void importModelToScene(const std::string& filepath, const std::string& objectName);  // Assimp import
    void handleKeyboardShortcuts();
    void OpenProjectPath(const std::string& path);
    
    // UI rendering methods
    void renderLauncher();
    void renderNewProjectDialog();
    void renderOpenProjectDialog();
    void renderMainMenuBar();
    void renderHierarchyPanel();
    void renderObjectNode(SceneObject& obj, const std::string& filter);
    void renderFileBrowserPanel();
    void renderInspectorPanel();
    void renderConsolePanel();
    void renderViewport();
    void renderDialogs();
    void renderProjectBrowserPanel();
    
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
    
    // Console/logging
    void addConsoleMessage(const std::string& message, ConsoleMessageType type);
    void logToConsole(const std::string& message);
    
    // ImGui setup
    void setupImGui();
    bool initRenderer();

public:
    Engine() = default;

    bool init();
    void run();
    void shutdown();
};
