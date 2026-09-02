#pragma once

#include "Common.h"
#include "SceneObject.h"
#include "HardwareProfile.h"
#include "XR/XRSettings.h"
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
// default, since it ships on every platform Modularity targets (including
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

// Modularity's take on Unity's Forward / Forward+ / Deferred switch. Each
// path sets the scene light budget plus how many directional lights keep a
// guaranteed slot on top of it. (The renderer still draws at most
// kRendererMaxRealtimeLights per frame, nearest-first; the budget is the
// design-time cap a scene is allowed to use in that mode.)
enum class ProjectRenderingPath {
    Normal = 0,        // 20 lights max, 1 directional always allowed
    NormalPlus = 1,    // 40 lights max, 1 directional always allowed
    Deferred = 2,      // 200 lights max, 1 directional always allowed
    HeavyDeferred = 3  // 500 lights max, 2 directional always allowed
};

inline void ProjectRenderingPathCaps(ProjectRenderingPath path, int& lightBudget, int& directionalAllowance) {
    switch (path) {
        case ProjectRenderingPath::NormalPlus:    lightBudget = 40;  directionalAllowance = 1; break;
        case ProjectRenderingPath::Deferred:      lightBudget = 200; directionalAllowance = 1; break;
        case ProjectRenderingPath::HeavyDeferred: lightBudget = 500; directionalAllowance = 2; break;
        case ProjectRenderingPath::Normal:
        default:                                  lightBudget = 20;  directionalAllowance = 1; break;
    }
}

inline const char* ProjectRenderingPathLabel(ProjectRenderingPath path) {
    switch (path) {
        case ProjectRenderingPath::NormalPlus:    return "Normal+";
        case ProjectRenderingPath::Deferred:      return "Deferred";
        case ProjectRenderingPath::HeavyDeferred: return "Heavy Deferred";
        case ProjectRenderingPath::Normal:
        default:                                  return "Normal";
    }
}

inline const char* ProjectRenderingPathNote(ProjectRenderingPath path) {
    switch (path) {
        case ProjectRenderingPath::NormalPlus:
            return "40 lights max, 1 directional light always allowed at any amount.";
        case ProjectRenderingPath::Deferred:
            return "200 lights max, 1 directional light always allowed at any amount.";
        case ProjectRenderingPath::HeavyDeferred:
            return "500 lights max, 2 directional lights always allowed at any amount.";
        case ProjectRenderingPath::Normal:
        default:
            return "20 lights max, 1 directional light always allowed at any amount.";
    }
}

// Offscreen color-buffer precision ("Color Resolution" in Graphics Manager).
enum class ProjectColorResolution {
    Auto = 0,     // 16-bit float, required for HDR/bloom (classic behavior)
    Color8 = 1,   // 8-bit LDR, lighter on bandwidth/VRAM
    Color16F = 2  // explicit 16-bit float
};

// Quality preset the graphics settings below were last stamped from. Custom is
// the default on purpose: a project written before presets existed has no key
// for this, so it loads as Custom and keeps every value it already stored
// instead of being silently restamped on open.
enum class ProjectQualityPreset {
    Low = 0,
    Balanced = 1,
    High = 2,
    Custom = 3
};

inline const char* ProjectQualityPresetLabel(ProjectQualityPreset preset) {
    switch (preset) {
        case ProjectQualityPreset::Low:      return "Low";
        case ProjectQualityPreset::Balanced: return "Balanced";
        case ProjectQualityPreset::High:     return "High";
        case ProjectQualityPreset::Custom:
        default:                             return "Custom";
    }
}

inline const char* ProjectQualityPresetNote(ProjectQualityPreset preset) {
    switch (preset) {
        case ProjectQualityPreset::Low:
            return "For integrated graphics and older laptops. No shadows, no HDR, "
                   "no anti-aliasing, and the scene renders at 75% and upscales.";
        case ProjectQualityPreset::Balanced:
            return "One shadowed main light, no soft shadows, 2x MSAA. Sensible on "
                   "a modern iGPU.";
        case ProjectQualityPreset::High:
            return "Everything on: soft shadows on all lights, HDR, 4x MSAA.";
        case ProjectQualityPreset::Custom:
        default:
            return "Settings were tuned by hand and no longer match a preset.";
    }
}

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
    // Graphics Manager additions
    ProjectRenderingPath renderingPath = ProjectRenderingPath::Normal;
    bool hdr = true;
    ProjectColorResolution colorResolution = ProjectColorResolution::Auto;
    std::string defaultTextureFormat = "Auto"; // TextureFormatPolicy ToString name
    // Lighting Manager
    bool mainLightEnabled = true;              // Per Pixel / Off
    bool mainLightCastShadows = true;
    bool additionalLightsEnabled = true;       // Per Pixel / Off
    bool additionalLightsCastShadows = true;
    bool specularEnabled = true;               // Phong/specular contribution; off = ambient + diffuse
    bool softShadows = true;
    float shadowMaxDistance = 0.0f;            // 0 = follow camera far plane
    ProjectQualityPreset qualityPreset = ProjectQualityPreset::Custom;
};

// Stamps a preset's values over the settings. Only touches the knobs a preset
// owns: vsync, target frame rate, fullscreen and the texture format policy are
// project/authoring choices, not quality choices, so they survive untouched.
inline void ApplyProjectQualityPreset(ProjectGraphicsSettings& g, ProjectQualityPreset preset) {
    switch (preset) {
        case ProjectQualityPreset::Low:
            g.shadowQuality = 0;
            g.mainLightCastShadows = false;
            g.additionalLightsCastShadows = false;
            g.softShadows = false;
            g.specularEnabled = false;
            g.hdr = false;
            // 8-bit color halves offscreen bandwidth, which is the actual
            // bottleneck on shared-memory integrated graphics.
            g.colorResolution = ProjectColorResolution::Color8;
            g.antiAliasing = ProjectAntiAliasing::Off;
            g.renderResolutionScale = 0.75f;
            g.renderingPath = ProjectRenderingPath::Normal;
            break;
        case ProjectQualityPreset::Balanced:
            g.shadowQuality = 1;
            g.mainLightCastShadows = true;
            g.additionalLightsCastShadows = false;
            g.softShadows = false;
            g.specularEnabled = true;
            g.hdr = false;
            g.colorResolution = ProjectColorResolution::Auto;
            g.antiAliasing = ProjectAntiAliasing::MSAA2x;
            g.renderResolutionScale = 1.0f;
            break;
        case ProjectQualityPreset::High:
            g.shadowQuality = 3;
            g.mainLightCastShadows = true;
            g.additionalLightsCastShadows = true;
            g.softShadows = true;
            g.specularEnabled = true;
            g.hdr = true;
            g.colorResolution = ProjectColorResolution::Auto;
            g.antiAliasing = ProjectAntiAliasing::MSAA4x;
            g.renderResolutionScale = 1.0f;
            break;
        case ProjectQualityPreset::Custom:
        default:
            // Custom is a label, not a set of values: nothing to stamp.
            return;
    }
    g.qualityPreset = preset;
}

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

// Language Manager. ModuCPP stays one language internally; this only picks
// which localized spelling of the keywords the project authors in, and what to
// do when a script written in another supported language is imported.
// Ids come from ModuCPPLang::Languages() ("english", "german", ...).
enum class ProjectImportTranslatePolicy {
    Ask = 0,       // show the import popup every time
    Always = 1,    // translate imported scripts into the project language
    Never = 2      // leave imported scripts exactly as they were written
};

struct ProjectLanguageSettings {
    // Empty means "follow the global ModuCPP default". The editor language is
    // deliberately not here: it is a global user preference and must never be
    // written into a project file.
    std::string moduCppSyntaxLanguage;
    ProjectImportTranslatePolicy importTranslatePolicy = ProjectImportTranslatePolicy::Ask;

    bool usesGlobalModuCppDefault() const { return moduCppSyntaxLanguage.empty(); }
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
    ProjectLanguageSettings languageSettings;
    ProjectPlayerSettings playerSettings;
    // OpenXR / VR. Defaults are all-off, so a project that predates these keys
    // loads with XR disabled and behaves exactly as it did before.
    Modularity::XR::ProjectOpenXRSettings openXRSettings;
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

// Global editor preferences. These live next to the recent-projects list in
// launcher_settings.modu and follow the *person*, never the project: a project
// file must stay portable, so nothing here is ever written into one. The
// per-project editor settings file still wins for the handful of keys that
// exist in both (theme, font, chrome scale, ...); these are the defaults a
// project inherits when it has never stored its own.
struct EditorGlobalPreferences {
    // -- Appearance ------------------------------------------------------
    std::string themePreset = "Default";       // UIStylePreset name
    std::string uiFontAsset;                   // empty = engine default font
    std::string chromeScale = "Default";       // Compact | Default | Big
    std::string animationMode = "Fluid";       // Off | Snappy | Fluid
    float uiScale = 1.0f;                      // extra multiplier on top of the DPI scale

    // -- Launcher --------------------------------------------------------
    bool launcherIntroAnimation = true;
    bool launcherProjectThumbnails = true;
    bool launcherRememberLastTab = true;
    int launcherLastTab = 0;                   // 0 Projects, 1 ModuPAK, 2 Settings
    bool launcherShowFullPaths = true;
    int launcherProjectSort = 0;               // 0 Recent, 1 Name, 2 Pipeline

    // -- Sound feedback --------------------------------------------------
    bool feedbackSounds = true;
    bool feedbackClickSounds = true;
    bool feedbackErrorSounds = true;
    bool feedbackOtherSounds = true;

    // -- Editor behaviour ------------------------------------------------
    bool reopenLastProject = false;
    bool hierarchyTexturePreviews = true;
    bool sceneGizmos = true;
    bool gizmoCameraOverlays = true;
    bool consoleWrapText = true;
    bool quickToolsPinned = false;

    // -- Performance -----------------------------------------------------
    bool editorVSync = true;
    bool editorFpsCapEnabled = false;
    float editorFpsCap = 120.0f;
    // Hardware tier the editor runs its own chrome at. "Auto" means: whatever
    // the benchmark measured (hardwareTier below). Anything else is the user
    // overriding us, and the benchmark never overwrites an explicit choice.
    // Values are HardwareProfile::Tier names, or "Auto".
    std::string performanceMode = "Auto";
    // Last measured tier, cached so the editor does not re-benchmark on every
    // launch. Empty until the benchmark has run at least once.
    std::string hardwareTier;
    // Cached raw numbers behind hardwareTier, so Preferences can show what was
    // measured without re-running the probe. Zero when never measured.
    float benchFullscreenPassMs = 0.0f;
    float benchBlurPassMs = 0.0f;
    float benchDrawCallBatchMs = 0.0f;
    // The offer-to-benchmark popup is a once-per-machine thing. Set the moment
    // the user answers it either way, so a dismissal actually sticks.
    bool  lowSpecPromptAnswered = false;
    bool scriptAutoCompile = true;
    float scriptAutoCompileInterval = 0.5f;    // seconds between change checks
    float scriptDirectoryScanInterval = 5.0f;  // seconds between full rescans

    // -- New project defaults --------------------------------------------
    int defaultPipeline = 0;                   // ProjectPipelineToUiIndex order
    int defaultRenderer = 0;                   // 0 = OpenGL, 1 = Vulkan
    std::string defaultCompanyName = "DefaultCompany";
    // ModuPAKs installed into every project the moment it is created. Ids come
    // from the ModuEngine registry; unknown ids are ignored rather than
    // erroring, so a preference set on a machine with extra packages stays
    // loadable everywhere.
    bool applyDefaultPackages = true;
    std::vector<std::string> defaultPackageIds;

    // The tier the editor should actually behave as. An explicit performanceMode
    // always wins; "Auto" falls back to what was measured, and an unmeasured
    // machine is assumed capable rather than assumed slow - guessing Low and
    // being wrong degrades a perfectly good editor for no reason.
    Modularity::HardwareProfile::Tier effectiveTier() const {
        if (performanceMode != "Auto" && !performanceMode.empty()) {
            return Modularity::HardwareProfile::FromString(performanceMode);
        }
        if (!hardwareTier.empty()) {
            return Modularity::HardwareProfile::FromString(hardwareTier);
        }
        return Modularity::HardwareProfile::Tier::High;
    }

    bool isDefaultPackage(const std::string& id) const {
        return std::find(defaultPackageIds.begin(), defaultPackageIds.end(), id) !=
               defaultPackageIds.end();
    }
    void setDefaultPackage(const std::string& id, bool enabled) {
        const auto it = std::find(defaultPackageIds.begin(), defaultPackageIds.end(), id);
        if (enabled && it == defaultPackageIds.end()) {
            defaultPackageIds.push_back(id);
        } else if (!enabled && it != defaultPackageIds.end()) {
            defaultPackageIds.erase(it);
        }
    }
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
    bool androidStoragePromptDismissedV1 = false;
    // Global user preferences (launcher_settings.modu), never project files.
    // editorLanguage drives the editor UI; moduCppDefaultLanguage is the syntax
    // language new projects start with and the fallback for projects that do
    // not store their own. The two are independent on purpose.
    std::string editorLanguage = "english";
    std::string moduCppDefaultLanguage = "english";
    bool osLanguagePromptAnswered = false;
    // Everything else the launcher's Settings tab edits. Same file, same
    // lifetime as the two language keys above.
    EditorGlobalPreferences preferences;
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

    // The ModuCPP syntax language actually in force for the open project:
    // its own choice, or the global default when it has none.
    std::string effectiveModuCppLanguage() const;
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
