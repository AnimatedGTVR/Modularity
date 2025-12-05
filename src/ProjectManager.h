#pragma once

#include "Common.h"
#include "SceneObject.h"

struct RecentProject {
    std::string name;
    std::string path;
    std::string lastOpened;
};

class Project {
public:
    std::string name;
    fs::path projectPath;
    fs::path scenesPath;
    fs::path assetsPath;
    fs::path scriptsPath;
    std::string currentSceneName;
    bool isLoaded = false;
    bool hasUnsavedChanges = false;

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
    char openProjectPath[512] = "";
    bool showNewProjectDialog = false;
    bool showOpenProjectDialog = false;
    std::string errorMessage;
    Project currentProject;

    ProjectManager();

    void loadRecentProjects();
    void saveRecentProjects();
    void addToRecentProjects(const std::string& name, const std::string& path);
    bool loadProject(const std::string& path);
};

class SceneSerializer {
public:
    static bool saveScene(const fs::path& filePath,
                         const std::vector<SceneObject>& objects,
                         int nextId);

    static bool loadScene(const fs::path& filePath,
                         std::vector<SceneObject>& objects,
                         int& nextId);
};
