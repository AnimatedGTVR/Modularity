#pragma once

#include "Common.h"
#include "SceneObject.h"
#include "../include/Skybox/Skybox.h"

struct RecentProject {
    std::string name;
    std::string path;
    std::string lastOpened;
};

enum class ProjectPipeline {
    Pipeline3D = 0,
    Pipeline2D = 1,
    Pipeline25D = 2
};

inline const char* ProjectPipelineLabel(ProjectPipeline pipeline) {
    switch (pipeline) {
        case ProjectPipeline::Pipeline2D: return "2D Pipeline";
        case ProjectPipeline::Pipeline25D: return "2.5D Pipeline";
        case ProjectPipeline::Pipeline3D:
        default:
            return "3D Pipeline";
    }
}

inline const char* SerializeProjectPipeline(ProjectPipeline pipeline) {
    switch (pipeline) {
        case ProjectPipeline::Pipeline2D: return "2D";
        case ProjectPipeline::Pipeline25D: return "2.5D";
        case ProjectPipeline::Pipeline3D:
        default:
            return "3D";
    }
}

inline ProjectPipeline ParseProjectPipeline(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "2d" || value == "pipeline2d" || value == "1") {
        return ProjectPipeline::Pipeline2D;
    }
    if (value == "2.5d" || value == "25d" || value == "pipeline25d" ||
        value == "pipeline2.5d" || value == "2") {
        return ProjectPipeline::Pipeline25D;
    }
    return ProjectPipeline::Pipeline3D;
}

inline int ProjectPipelineToUiIndex(ProjectPipeline pipeline) {
    switch (pipeline) {
        case ProjectPipeline::Pipeline25D: return 1;
        case ProjectPipeline::Pipeline2D: return 2;
        case ProjectPipeline::Pipeline3D:
        default:
            return 0;
    }
}

inline ProjectPipeline ProjectPipelineFromUiIndex(int index) {
    switch (index) {
        case 1: return ProjectPipeline::Pipeline25D;
        case 2: return ProjectPipeline::Pipeline2D;
        case 0:
        default:
            return ProjectPipeline::Pipeline3D;
    }
}

enum class ProjectMassUnit {
    Kilograms = 0,
    Grams = 1,
    Pounds = 2,
    Ounces = 3
};

inline float ProjectMassUnitToKilograms(ProjectMassUnit unit) {
    switch (unit) {
        case ProjectMassUnit::Grams: return 0.001f;
        case ProjectMassUnit::Pounds: return 0.45359237f;
        case ProjectMassUnit::Ounces: return 0.0283495231f;
        case ProjectMassUnit::Kilograms:
        default:
            return 1.0f;
    }
}

inline const char* ProjectMassUnitLabel(ProjectMassUnit unit) {
    switch (unit) {
        case ProjectMassUnit::Grams: return "Grams";
        case ProjectMassUnit::Pounds: return "Pounds";
        case ProjectMassUnit::Ounces: return "Ounces";
        case ProjectMassUnit::Kilograms:
        default:
            return "Kilograms";
    }
}

inline const char* ProjectMassUnitSuffix(ProjectMassUnit unit) {
    switch (unit) {
        case ProjectMassUnit::Grams: return "g";
        case ProjectMassUnit::Pounds: return "lb";
        case ProjectMassUnit::Ounces: return "oz";
        case ProjectMassUnit::Kilograms:
        default:
            return "kg";
    }
}

struct ProjectPhysicsSettings {
    ProjectMassUnit massUnit = ProjectMassUnit::Kilograms;
    float globalGravityScale = 1.0f;
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
    ProjectPhysicsSettings physicsSettings;

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
    std::string acceptedTermsVersion;
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
                         float timeOfDay,
                         const SkyboxSettings& skyboxSettings = SkyboxSettings{});

    static bool loadScene(const fs::path& filePath,
                         std::vector<SceneObject>& objects,
                         int& nextId,
                         int& outVersion,
                         float* outTimeOfDay = nullptr,
                         SkyboxSettings* outSkyboxSettings = nullptr);

    static bool loadSceneDeferred(const fs::path& filePath,
                         std::vector<SceneObject>& objects,
                         int& nextId,
                         int& outVersion,
                         float* outTimeOfDay = nullptr,
                         SkyboxSettings* outSkyboxSettings = nullptr);
};
