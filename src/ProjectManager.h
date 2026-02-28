#pragma once

#include "Common.h"
#include "SceneObject.h"

struct RecentProject {
    std::string name;
    std::string path;
    std::string lastOpened;
};

enum class ProjectPipeline {
    Pipeline3D = 0,
    Pipeline2D = 1
};

class Project {
public:
    std::string name;
    fs::path projectPath;
    fs::path scenesPath;
    fs::path assetsPath;
    fs::path scriptsPath;
    fs::path scriptsConfigPath;
    std::string currentSceneName;
    bool isLoaded = false;
    bool hasUnsavedChanges = false;
    bool usesNewLayout = false;
    ProjectPipeline pipeline = ProjectPipeline::Pipeline3D;
    Modularity::GraphicsBackend rendererBackend = Modularity::GraphicsBackend::OpenGL;

    Project() = default;
    Project(const std::string& projectName, const fs::path& basePath);

    bool create();
    bool load(const fs::path& projectFilePath);
    void saveProjectFile() const;
    std::vector<std::string> getSceneList() const;
    fs::path getSceneFilePath(const std::string& sceneName) const;
};

class ProjectManager {
public:
    std::vector<RecentProject> recentProjects;
    fs::path appDataPath;
    char newProjectName[128] = "";
    char newProjectLocation[512] = "";
    char defaultProjectLocation[512] = "";
    char openProjectPath[512] = "";
    bool showNewProjectDialog = false;
    bool showOpenProjectDialog = false;
    int newProjectPipelineMode = 0;
    int newProjectRendererMode = 0;
    bool newProjectImportLastPackages = true;
    std::string newProjectTemplatePath;
    std::string newProjectTemplateName;
    std::string errorMessage;
    Project currentProject;

    ProjectManager();

    void loadRecentProjects();
    void saveRecentProjects();
    void loadLauncherSettings();
    void saveLauncherSettings() const;
    void addToRecentProjects(const std::string& name, const std::string& path);
    bool loadProject(const std::string& path);
};

class SceneSerializer {
public:
    static bool saveScene(const fs::path& filePath,
                         const std::vector<SceneObject>& objects,
                         int nextId,
                         float timeOfDay);

    static bool loadScene(const fs::path& filePath,
                         std::vector<SceneObject>& objects,
                         int& nextId,
                         int& outVersion,
                         float* outTimeOfDay = nullptr);

    static bool loadSceneDeferred(const fs::path& filePath,
                         std::vector<SceneObject>& objects,
                         int& nextId,
                         int& outVersion,
                         float* outTimeOfDay = nullptr);
};
