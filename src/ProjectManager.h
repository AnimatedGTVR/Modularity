#pragma once

#include "Common.h"
#include "SceneObject.h"
#include "../include/Skybox/Skybox.h"
#include <map>
#include <string>

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

// Which physics engine the project's 3D simulation runs on. Jolt is the
// default — it ships on every platform Modularity targets (including
// Android). PhysX remains available on desktop as a familiar fallback,
// but is unavailable on Android.
enum class PhysicsBackendType {
    Jolt = 0,
    PhysX = 1
};

inline const char* PhysicsBackendLabel(PhysicsBackendType t) {
    switch (t) {
        case PhysicsBackendType::PhysX: return "PhysX";
        case PhysicsBackendType::Jolt:
        default:
            return "Jolt";
    }
}

struct ProjectPhysicsSettings {
    PhysicsBackendType backend = PhysicsBackendType::Jolt;
    ProjectMassUnit massUnit = ProjectMassUnit::Kilograms;
    float globalGravityScale = 1.0f;
    float fixedTimestep = 1.0f / 60.0f;
    int solverIterations = 8;
    bool enable3DPhysics = true;
    bool enable2DPhysics = true;
    float defaultRaycastDistance = 1000.0f;
    bool raycastHitTriggers = false;
    float defaultRigidbodyMass = 1.0f;
    float defaultRigidbodyDrag = 0.0f;
    float defaultMaterialFriction = 0.5f;
    float defaultMaterialBounciness = 0.0f;
    std::vector<std::string> collisionLayers = { "Default" };
};

enum class ProjectTextureFiltering {
    Bilinear = 0,
    Point = 1,
    Trilinear = 2
};

enum class ProjectAntiAliasing {
    Off = 0,
    MSAA2x = 1,
    MSAA4x = 2,
    MSAA8x = 3
};

struct ProjectGraphicsSettings {
    bool vsync = true;
    int targetFps = 60;
    int shadowQuality = 2;
    float renderResolutionScale = 1.0f;
    ProjectTextureFiltering textureFiltering = ProjectTextureFiltering::Bilinear;
    ProjectAntiAliasing antiAliasing = ProjectAntiAliasing::MSAA4x;
    bool fullscreenStartup = false;
    bool editorPreviewOverrides = true;
    bool gamePreviewOverrides = false;
};

enum class ProjectConsoleMode {
    DockedMiniButton = 0,
    FloatingWindow = 1
};

enum class ProjectConsoleTone {
    Fun = 0,
    Concise = 1
};

struct ProjectConsoleSettings {
    ProjectConsoleMode mode = ProjectConsoleMode::DockedMiniButton;
    bool alwaysOpenOnLaunch = false;
    bool openOnlyOnErrors = true;
    ProjectConsoleTone tone = ProjectConsoleTone::Fun;
};

struct ProjectPlayerSettings {
    std::string productName;
    std::string companyName = "DefaultCompany";
    std::string defaultScene;
    int startupWidth = 1280;
    int startupHeight = 720;
    bool fullscreenStartup = false;
    // When true the player ignores startupWidth/startupHeight and renders
    // at the actual display surface size. On Android that's the EGL
    // surface size (typically the tablet's native resolution); on desktop
    // it's the GLFW window framebuffer size. Use this when you want the
    // game to look crisp on whatever device runs it without manually
    // configuring resolutions per target.
    bool nativeDisplayResolution = false;
    bool cursorLocked = false;
    bool cursorVisible = true;
    std::string buildTarget = "Windows";
    std::string applicationIconPath;
    std::string saveDataPathBehavior = "CompanyAndProduct";
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
    ProjectGraphicsSettings graphicsSettings;
    std::vector<std::string> tags = { "Untagged" };
    ProjectConsoleSettings consoleSettings;
    ProjectPlayerSettings playerSettings;
    // Per-texture GPU storage-format overrides: asset-relative path -> format
    // name (see TextureFormatPolicy ToString). Absent entries use Auto.
    std::map<std::string, std::string> textureFormatOverrides;

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
    bool windowsDisclaimerAcknowledgedV68 = false;
    bool moduCppNvimWarningDismissedV1 = false;
    int newProjectPipelineMode = 0;
    int newProjectRendererMode = 0;
    bool newProjectImportLastPackages = true;
    std::string newProjectTemplatePath;
    std::string newProjectTemplateName;
    std::string newProjectPresetId = "empty";
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
    enum class FileFormat {
        LegacyFlat = 0,
        ModularNodes = 1
    };

    enum class SavePreference {
        PreferModular = 0,
        ForceLegacyFlat = 1
    };

    struct Metadata {
        int version = 0;
        FileFormat fileFormat = FileFormat::ModularNodes;
        bool loadedFromLegacyLayout = false;
        bool upgradedToModularLayout = false;
        fs::path sourcePath;
    };

    struct SaveOptions {
        SavePreference preference = SavePreference::PreferModular;
        bool moveLegacySourceToCompatibility = false;
        Metadata* metadata = nullptr;
    };

    static bool saveScene(const fs::path& filePath,
                         const std::vector<SceneObject>& objects,
                         int nextId,
                         float timeOfDay,
                         const SkyboxSettings& skyboxSettings = SkyboxSettings{});

    static bool saveScene(const fs::path& filePath,
                         const std::vector<SceneObject>& objects,
                         int nextId,
                         float timeOfDay,
                         const SkyboxSettings& skyboxSettings,
                         const SaveOptions& options);

    static bool loadScene(const fs::path& filePath,
                         std::vector<SceneObject>& objects,
                         int& nextId,
                         int& outVersion,
                         float* outTimeOfDay = nullptr,
                         SkyboxSettings* outSkyboxSettings = nullptr,
                         Metadata* outMetadata = nullptr);

    static bool loadSceneDeferred(const fs::path& filePath,
                         std::vector<SceneObject>& objects,
                         int& nextId,
                         int& outVersion,
                         float* outTimeOfDay = nullptr,
                         SkyboxSettings* outSkyboxSettings = nullptr,
                         Metadata* outMetadata = nullptr);
};
