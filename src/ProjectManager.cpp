#include "ProjectManager.h"
#include "SceneSerializationInternal.h"
#include "Rendering.h"
#include "ModelLoader.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <optional>
#include <unordered_map>

ObjectType GetLegacyTypeFromComponents(const SceneObject& obj);

namespace {
std::string TrimCopy(const std::string& value) {
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string GetPlatformDefaultProjectsPath() {
#ifdef _WIN32
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile && *userProfile) {
        return (fs::path(userProfile) / "Documents" / "ModularityProjects").string();
    }
#else
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return (fs::path(home) / "ModularityProjects").string();
    }
#endif
    return (fs::current_path() / "Projects").string();
}

ProjectMassUnit ParseProjectMassUnit(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "grams" || value == "gram" || value == "g" || value == "1") {
        return ProjectMassUnit::Grams;
    }
    if (value == "pounds" || value == "pound" || value == "lb" || value == "lbs" || value == "2") {
        return ProjectMassUnit::Pounds;
    }
    if (value == "ounces" || value == "ounce" || value == "oz" || value == "3") {
        return ProjectMassUnit::Ounces;
    }
    return ProjectMassUnit::Kilograms;
}

const char* SerializeProjectMassUnit(ProjectMassUnit unit) {
    switch (unit) {
        case ProjectMassUnit::Grams: return "Grams";
        case ProjectMassUnit::Pounds: return "Pounds";
        case ProjectMassUnit::Ounces: return "Ounces";
        case ProjectMassUnit::Kilograms:
        default:
            return "Kilograms";
    }
}

ProjectTextureFiltering ParseProjectTextureFiltering(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "point" || value == "nearest" || value == "1") return ProjectTextureFiltering::Point;
    if (value == "trilinear" || value == "2") return ProjectTextureFiltering::Trilinear;
    return ProjectTextureFiltering::Bilinear;
}

const char* SerializeProjectTextureFiltering(ProjectTextureFiltering value) {
    switch (value) {
        case ProjectTextureFiltering::Point: return "Point";
        case ProjectTextureFiltering::Trilinear: return "Trilinear";
        case ProjectTextureFiltering::Bilinear:
        default: return "Bilinear";
    }
}

ProjectAntiAliasing ParseProjectAntiAliasing(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "off" || value == "none" || value == "0") return ProjectAntiAliasing::Off;
    if (value == "2x" || value == "msaa2x" || value == "1") return ProjectAntiAliasing::MSAA2x;
    if (value == "8x" || value == "msaa8x" || value == "3") return ProjectAntiAliasing::MSAA8x;
    return ProjectAntiAliasing::MSAA4x;
}

const char* SerializeProjectAntiAliasing(ProjectAntiAliasing value) {
    switch (value) {
        case ProjectAntiAliasing::Off: return "Off";
        case ProjectAntiAliasing::MSAA2x: return "MSAA2x";
        case ProjectAntiAliasing::MSAA8x: return "MSAA8x";
        case ProjectAntiAliasing::MSAA4x:
        default: return "MSAA4x";
    }
}

ProjectConsoleMode ParseProjectConsoleMode(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "floatingwindow" || value == "floating" || value == "1") return ProjectConsoleMode::FloatingWindow;
    return ProjectConsoleMode::DockedMiniButton;
}

const char* SerializeProjectConsoleMode(ProjectConsoleMode value) {
    return value == ProjectConsoleMode::FloatingWindow ? "FloatingWindow" : "DockedMiniButton";
}

ProjectConsoleTone ParseProjectConsoleTone(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "concise" || value == "professional" || value == "1") return ProjectConsoleTone::Concise;
    return ProjectConsoleTone::Fun;
}

const char* SerializeProjectConsoleTone(ProjectConsoleTone value) {
    return value == ProjectConsoleTone::Concise ? "Concise" : "Fun";
}

bool ParseProjectBool(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

bool ScriptPathContains(const ScriptComponent& script, const char* token) {
    if (!token || !*token) return false;
    std::string pathLower = script.path;
    std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::string tokenLower = token;
    std::transform(tokenLower.begin(), tokenLower.end(), tokenLower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return pathLower.find(tokenLower) != std::string::npos;
}

ScriptSetting* FindScriptSetting(ScriptComponent& script, const std::string& key) {
    for (ScriptSetting& setting : script.settings) {
        if (setting.key == key) {
            return &setting;
        }
    }
    return nullptr;
}

void UpgradeLegacyScriptSettings(int sceneVersion, std::vector<SceneObject>& objects) {
    if (sceneVersion >= SceneSerializationInternal::kModularSceneFormatVersion) {
        return;
    }

    for (SceneObject& obj : objects) {
        for (ScriptComponent& script : obj.scripts) {
            if (!ScriptPathContains(script, "interactableobject")) {
                continue;
            }

            ScriptSetting* optionsBlob = FindScriptSetting(script, "optionsBlob");
            ScriptSetting* options = FindScriptSetting(script, "options");
            if (!optionsBlob || optionsBlob->value.empty() || (options && !options->value.empty())) {
                continue;
            }

            if (options) {
                options->value = optionsBlob->value;
            } else {
                script.settings.push_back(ScriptSetting{"options", optionsBlob->value});
            }
            optionsBlob->value.clear();
        }
    }
}

} // namespace

// Project implementation
Project::Project(const std::string& projectName, const fs::path& basePath)
    : name(projectName) {
    projectPath = basePath / projectName;
    assetsPath = projectPath / "Assets";
    scenesPath = assetsPath / "Scenes";
    scriptsPath = assetsPath / "Scripts";
    scriptsConfigPath = projectPath / "scripts.modu";
    usesNewLayout = true;
    pipeline = ProjectPipeline::Pipeline3D;
    rendererBackend = Modularity::GraphicsBackend::OpenGL;
    playerSettings.productName = projectName;
    playerSettings.defaultScene = "Main";
}

bool Project::create() {
    try {
        fs::create_directories(projectPath);
        fs::create_directories(assetsPath);
        fs::create_directories(assetsPath / "Scenes");
        fs::create_directories(assetsPath / "Scripts" / "Runtime");
        fs::create_directories(assetsPath / "Scripts" / "Editor");
        fs::create_directories(assetsPath / "Models");
        fs::create_directories(assetsPath / "Shaders");
        fs::create_directories(assetsPath / "Materials");
        fs::create_directories(projectPath / "Library" / "CompiledScripts");
        fs::create_directories(projectPath / "Library" / "InstalledPackages");
        fs::create_directories(projectPath / "Library" / "ScriptTemp");
        fs::create_directories(projectPath / "Library" / "Temp");
        fs::create_directories(projectPath / "ProjectUserSettings" / "ProjectLayout");
        fs::create_directories(projectPath / "ProjectUserSettings" / "ScriptSettings");

        saveProjectFile();

        // Initialize a default scripting build file
        std::ofstream scriptCfg(scriptsConfigPath);
        scriptCfg << "# scripts.modu\n";
        scriptCfg << "# Default native script target is C++14 for older toolchain compatibility. Use c++17, c++20, c++23, or c++26 when needed.\n";
        scriptCfg << "cppStandard=c++14\n";
        scriptCfg << "scriptsDir=Assets/Scripts\n";
        scriptCfg << "outDir=Library/CompiledScripts\n";
        scriptCfg << "define=MODU_SCRIPTING=1\n";
        scriptCfg << "define=MODU_PROJECT_NAME=\"" << name << "\"\n";
        scriptCfg << "linux.linkLib=pthread\n";
        scriptCfg << "linux.linkLib=dl\n";
        scriptCfg << "win.linkLib=User32.lib\n";
        scriptCfg << "win.linkLib=Advapi32.lib\n";
        scriptCfg.close();

        std::ofstream packageManifest(projectPath / "packages.modu");
        packageManifest << "# Modularity package manifest\n";
        packageManifest << "# package=<id>\n";
        packageManifest << "# git=<id>|<name>|<url>|<path>|<includeDirs>|<defines>|<linuxLibs>|<windowsLibs>|<description>\n";
        packageManifest << "# modupak=<id>|<name>|<bundlePath>|<path>|<includeDirs>|<defines>|<linuxLibs>|<windowsLibs>|<description>\n";
        packageManifest.close();

        currentSceneName = "Main";
        isLoaded = true;
        usesNewLayout = true;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create project: " << e.what() << std::endl;
        return false;
    }
}

bool Project::load(const fs::path& projectFilePath) {
    try {
        projectPath = projectFilePath.parent_path();
        assetsPath = projectPath / "Assets";

        fs::path oldScenes = projectPath / "Scenes";
        fs::path oldScripts = projectPath / "Scripts";
        fs::path oldConfig = projectPath / "Scripts.modu";
        fs::path newScenes = assetsPath / "Scenes";
        fs::path newScripts = assetsPath / "Scripts";
        fs::path newConfig = projectPath / "scripts.modu";

        bool hasOldScenes = fs::exists(oldScenes);
        bool hasOldScripts = fs::exists(oldScripts);
        bool hasOldConfig = fs::exists(oldConfig);
        bool hasNewScenes = fs::exists(newScenes);
        bool hasNewScripts = fs::exists(newScripts);
        bool hasNewConfig = fs::exists(newConfig);

        bool useNewLayout = false;
        if (hasOldScenes || hasOldScripts || hasOldConfig) {
            useNewLayout = false;
        } else if (hasNewScenes || hasNewScripts || hasNewConfig) {
            useNewLayout = true;
        }

        if (useNewLayout) {
            scenesPath = newScenes;
            scriptsPath = newScripts;
            scriptsConfigPath = hasNewConfig ? newConfig : (hasOldConfig ? oldConfig : newConfig);
        } else {
            scenesPath = oldScenes;
            scriptsPath = oldScripts;
            scriptsConfigPath = hasOldConfig ? oldConfig : (hasNewConfig ? newConfig : oldConfig);
        }
        usesNewLayout = useNewLayout;

        std::ifstream file(projectFilePath);
        if (!file.is_open()) return false;

        std::string line;
        while (std::getline(file, line)) {
            const size_t equals = line.find('=');
            const std::string key = equals == std::string::npos ? line : line.substr(0, equals);
            const std::string value = equals == std::string::npos ? std::string() : line.substr(equals + 1);
            if (line.find("name=") == 0) {
                name = value;
            } else if (line.find("lastScene=") == 0) {
                currentSceneName = value;
            } else if (line.find("pipeline=") == 0) {
                pipeline = ParseProjectPipeline(value);
            } else if (line.find("renderer=") == 0) {
                rendererBackend = Modularity::GraphicsBackendFromString(value);
            } else if (line.find("physicsMassUnit=") == 0) {
                physicsSettings.massUnit = ParseProjectMassUnit(value);
            } else if (line.find("physicsGlobalGravityScale=") == 0) {
                physicsSettings.globalGravityScale = std::max(0.0f, std::stof(value));
            } else if (key == "physicsFixedTimestep") {
                physicsSettings.fixedTimestep = std::clamp(std::stof(value), 0.001f, 0.1f);
            } else if (key == "physicsSolverIterations") {
                physicsSettings.solverIterations = std::clamp(std::stoi(value), 1, 64);
            } else if (key == "physicsEnable3D") {
                physicsSettings.enable3DPhysics = ParseProjectBool(value);
            } else if (key == "physicsEnable2D") {
                physicsSettings.enable2DPhysics = ParseProjectBool(value);
            } else if (key == "physicsDefaultRaycastDistance") {
                physicsSettings.defaultRaycastDistance = std::max(0.01f, std::stof(value));
            } else if (key == "physicsRaycastHitTriggers") {
                physicsSettings.raycastHitTriggers = ParseProjectBool(value);
            } else if (key == "physicsDefaultRigidbodyMass") {
                physicsSettings.defaultRigidbodyMass = std::max(0.0001f, std::stof(value));
            } else if (key == "physicsDefaultRigidbodyDrag") {
                physicsSettings.defaultRigidbodyDrag = std::max(0.0f, std::stof(value));
            } else if (key == "physicsDefaultMaterialFriction") {
                physicsSettings.defaultMaterialFriction = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "physicsDefaultMaterialBounciness") {
                physicsSettings.defaultMaterialBounciness = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key.rfind("physicsLayer", 0) == 0 && key.size() > 12 && key.substr(key.size() - 5) == "_name") {
                int index = std::stoi(key.substr(12, key.size() - 17));
                if (index >= 0 && index < 32) {
                    if (physicsSettings.collisionLayers.size() <= static_cast<size_t>(index)) {
                        physicsSettings.collisionLayers.resize(static_cast<size_t>(index) + 1);
                    }
                    physicsSettings.collisionLayers[static_cast<size_t>(index)] = value;
                }
            } else if (key.rfind("tag", 0) == 0 && key.size() > 5 && key.substr(key.size() - 5) == "_name") {
                int index = std::stoi(key.substr(3, key.size() - 8));
                if (index >= 0 && index < 256) {
                    if (tags.size() <= static_cast<size_t>(index)) {
                        tags.resize(static_cast<size_t>(index) + 1);
                    }
                    tags[static_cast<size_t>(index)] = value;
                }
            } else if (key == "graphicsVSync") {
                graphicsSettings.vsync = ParseProjectBool(value);
            } else if (key == "graphicsTargetFps") {
                graphicsSettings.targetFps = std::clamp(std::stoi(value), 1, 500);
            } else if (key == "graphicsShadowQuality") {
                graphicsSettings.shadowQuality = std::clamp(std::stoi(value), 0, 3);
            } else if (key == "graphicsRenderResolutionScale") {
                graphicsSettings.renderResolutionScale = std::clamp(std::stof(value), 0.25f, 2.0f);
            } else if (key == "graphicsTextureFiltering") {
                graphicsSettings.textureFiltering = ParseProjectTextureFiltering(value);
            } else if (key == "graphicsAntiAliasing") {
                graphicsSettings.antiAliasing = ParseProjectAntiAliasing(value);
            } else if (key == "graphicsFullscreenStartup") {
                graphicsSettings.fullscreenStartup = ParseProjectBool(value);
            } else if (key == "graphicsEditorPreviewOverrides") {
                graphicsSettings.editorPreviewOverrides = ParseProjectBool(value);
            } else if (key == "graphicsGamePreviewOverrides") {
                graphicsSettings.gamePreviewOverrides = ParseProjectBool(value);
            } else if (key == "consoleMode") {
                consoleSettings.mode = ParseProjectConsoleMode(value);
            } else if (key == "consoleAlwaysOpenOnLaunch") {
                consoleSettings.alwaysOpenOnLaunch = ParseProjectBool(value);
            } else if (key == "consoleOpenOnlyOnErrors") {
                consoleSettings.openOnlyOnErrors = ParseProjectBool(value);
            } else if (key == "consoleTone") {
                consoleSettings.tone = ParseProjectConsoleTone(value);
            } else if (key == "playerProductName") {
                playerSettings.productName = value;
            } else if (key == "playerCompanyName") {
                playerSettings.companyName = value;
            } else if (key == "playerDefaultScene") {
                playerSettings.defaultScene = value;
            } else if (key == "playerStartupWidth") {
                playerSettings.startupWidth = std::clamp(std::stoi(value), 64, 8192);
            } else if (key == "playerStartupHeight") {
                playerSettings.startupHeight = std::clamp(std::stoi(value), 64, 8192);
            } else if (key == "playerFullscreenStartup") {
                playerSettings.fullscreenStartup = ParseProjectBool(value);
            } else if (key == "playerCursorLocked") {
                playerSettings.cursorLocked = ParseProjectBool(value);
            } else if (key == "playerCursorVisible") {
                playerSettings.cursorVisible = ParseProjectBool(value);
            } else if (key == "playerBuildTarget") {
                playerSettings.buildTarget = value;
            } else if (key == "playerApplicationIcon") {
                playerSettings.applicationIconPath = value;
            } else if (key == "playerSaveDataPathBehavior") {
                playerSettings.saveDataPathBehavior = value;
            }
        }
        file.close();

        if (currentSceneName.empty()) {
            currentSceneName = "Main";
        }
        if (physicsSettings.collisionLayers.empty() || physicsSettings.collisionLayers[0].empty()) {
            if (physicsSettings.collisionLayers.empty()) physicsSettings.collisionLayers.resize(1);
            physicsSettings.collisionLayers[0] = "Default";
        }
        if (tags.empty() || tags[0].empty()) {
            if (tags.empty()) tags.resize(1);
            tags[0] = "Untagged";
        }
        if (playerSettings.productName.empty()) playerSettings.productName = name;
        if (playerSettings.defaultScene.empty()) playerSettings.defaultScene = currentSceneName;

        isLoaded = true;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load project: " << e.what() << std::endl;
        return false;
    }
}

void Project::saveProjectFile() const {
    std::ofstream file(projectPath / "project.modu");
    file << "name=" << name << "\n";
    file << "lastScene=" << currentSceneName << "\n";
    file << "pipeline=" << SerializeProjectPipeline(pipeline) << "\n";
    file << "renderer=" << Modularity::ToString(rendererBackend) << "\n";
    file << "physicsMassUnit=" << SerializeProjectMassUnit(physicsSettings.massUnit) << "\n";
    file << "physicsGlobalGravityScale=" << std::max(0.0f, physicsSettings.globalGravityScale) << "\n";
    file << "physicsFixedTimestep=" << std::clamp(physicsSettings.fixedTimestep, 0.001f, 0.1f) << "\n";
    file << "physicsSolverIterations=" << std::clamp(physicsSettings.solverIterations, 1, 64) << "\n";
    file << "physicsEnable3D=" << (physicsSettings.enable3DPhysics ? 1 : 0) << "\n";
    file << "physicsEnable2D=" << (physicsSettings.enable2DPhysics ? 1 : 0) << "\n";
    file << "physicsDefaultRaycastDistance=" << std::max(0.01f, physicsSettings.defaultRaycastDistance) << "\n";
    file << "physicsRaycastHitTriggers=" << (physicsSettings.raycastHitTriggers ? 1 : 0) << "\n";
    file << "physicsDefaultRigidbodyMass=" << std::max(0.0001f, physicsSettings.defaultRigidbodyMass) << "\n";
    file << "physicsDefaultRigidbodyDrag=" << std::max(0.0f, physicsSettings.defaultRigidbodyDrag) << "\n";
    file << "physicsDefaultMaterialFriction=" << std::clamp(physicsSettings.defaultMaterialFriction, 0.0f, 1.0f) << "\n";
    file << "physicsDefaultMaterialBounciness=" << std::clamp(physicsSettings.defaultMaterialBounciness, 0.0f, 1.0f) << "\n";
    file << "physicsLayerCount=" << physicsSettings.collisionLayers.size() << "\n";
    for (size_t i = 0; i < physicsSettings.collisionLayers.size(); ++i) {
        file << "physicsLayer" << i << "_name=" << physicsSettings.collisionLayers[i] << "\n";
    }
    file << "tagCount=" << tags.size() << "\n";
    for (size_t i = 0; i < tags.size(); ++i) {
        file << "tag" << i << "_name=" << tags[i] << "\n";
    }
    file << "graphicsVSync=" << (graphicsSettings.vsync ? 1 : 0) << "\n";
    file << "graphicsTargetFps=" << std::clamp(graphicsSettings.targetFps, 1, 500) << "\n";
    file << "graphicsShadowQuality=" << std::clamp(graphicsSettings.shadowQuality, 0, 3) << "\n";
    file << "graphicsRenderResolutionScale=" << std::clamp(graphicsSettings.renderResolutionScale, 0.25f, 2.0f) << "\n";
    file << "graphicsTextureFiltering=" << SerializeProjectTextureFiltering(graphicsSettings.textureFiltering) << "\n";
    file << "graphicsAntiAliasing=" << SerializeProjectAntiAliasing(graphicsSettings.antiAliasing) << "\n";
    file << "graphicsFullscreenStartup=" << (graphicsSettings.fullscreenStartup ? 1 : 0) << "\n";
    file << "graphicsEditorPreviewOverrides=" << (graphicsSettings.editorPreviewOverrides ? 1 : 0) << "\n";
    file << "graphicsGamePreviewOverrides=" << (graphicsSettings.gamePreviewOverrides ? 1 : 0) << "\n";
    file << "consoleMode=" << SerializeProjectConsoleMode(consoleSettings.mode) << "\n";
    file << "consoleAlwaysOpenOnLaunch=" << (consoleSettings.alwaysOpenOnLaunch ? 1 : 0) << "\n";
    file << "consoleOpenOnlyOnErrors=" << (consoleSettings.openOnlyOnErrors ? 1 : 0) << "\n";
    file << "consoleTone=" << SerializeProjectConsoleTone(consoleSettings.tone) << "\n";
    file << "playerProductName=" << (playerSettings.productName.empty() ? name : playerSettings.productName) << "\n";
    file << "playerCompanyName=" << playerSettings.companyName << "\n";
    file << "playerDefaultScene=" << (playerSettings.defaultScene.empty() ? currentSceneName : playerSettings.defaultScene) << "\n";
    file << "playerStartupWidth=" << std::clamp(playerSettings.startupWidth, 64, 8192) << "\n";
    file << "playerStartupHeight=" << std::clamp(playerSettings.startupHeight, 64, 8192) << "\n";
    file << "playerFullscreenStartup=" << (playerSettings.fullscreenStartup ? 1 : 0) << "\n";
    file << "playerCursorLocked=" << (playerSettings.cursorLocked ? 1 : 0) << "\n";
    file << "playerCursorVisible=" << (playerSettings.cursorVisible ? 1 : 0) << "\n";
    file << "playerBuildTarget=" << playerSettings.buildTarget << "\n";
    file << "playerApplicationIcon=" << playerSettings.applicationIconPath << "\n";
    file << "playerSaveDataPathBehavior=" << playerSettings.saveDataPathBehavior << "\n";
    file.close();
}

std::vector<std::string> Project::getSceneList() const {
    std::vector<std::string> scenes;
    try {
        for (const auto& entry : fs::directory_iterator(scenesPath)) {
            if (entry.path().extension() == ".scene") {
                scenes.push_back(entry.path().stem().string());
            }
        }
    } catch (...) {}
    return scenes;
}

fs::path Project::getSceneFilePath(const std::string& sceneName) const {
    return scenesPath / (sceneName + ".scene");
}

// ProjectManager implementation
ProjectManager::ProjectManager() {
    #ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        appDataPath = fs::path(appdata) / ".Modularity";
    } else {
        appDataPath = fs::current_path() / "AppData";
    }
    #else
    const char* home = std::getenv("HOME");
    if (home) {
        appDataPath = fs::path(home) / ".Modularity";
    } else {
        appDataPath = fs::current_path() / ".Modularity";
    }
    #endif

    fs::create_directories(appDataPath);
#if !MODULARITY_RUNTIME_ONLY
    loadRecentProjects();
    loadLauncherSettings();
#else
    const std::string fallback = GetPlatformDefaultProjectsPath();
    std::snprintf(defaultProjectLocation, sizeof(defaultProjectLocation), "%s", fallback.c_str());
#endif

    std::snprintf(newProjectLocation, sizeof(newProjectLocation), "%s", defaultProjectLocation);
}

void ProjectManager::loadRecentProjects() {
    recentProjects.clear();
    fs::path recentFile = appDataPath / "recent_projects.txt";

    if (!fs::exists(recentFile)) {
        return;
    }

    std::ifstream file(recentFile);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty()) continue;

        RecentProject rp;
        size_t pos1 = line.find('|');
        size_t pos2 = line.find('|', pos1 + 1);

        if (pos1 != std::string::npos && pos2 != std::string::npos) {
            rp.name = line.substr(0, pos1);
            rp.path = line.substr(pos1 + 1, pos2 - pos1 - 1);
            rp.lastOpened = line.substr(pos2 + 1);

            rp.path.erase(0, rp.path.find_first_not_of(" \t\r\n"));
            rp.path.erase(rp.path.find_last_not_of(" \t\r\n") + 1);

            if (fs::exists(rp.path)) {
                recentProjects.push_back(rp);
            }
        }
    }
    file.close();
}

void ProjectManager::saveRecentProjects() {
    fs::path recentFile = appDataPath / "recent_projects.txt";
    std::ofstream file(recentFile);

    for (const auto& rp : recentProjects) {
        std::string absolutePath = rp.path;
        try {
            if (fs::exists(rp.path)) {
                absolutePath = fs::canonical(rp.path).string();
            }
        } catch (...) {
            // Keep original path if canonical fails
        }
        file << rp.name << "|" << absolutePath << "|" << rp.lastOpened << "\n";
    }
    file.close();
}

void ProjectManager::loadLauncherSettings() {
    defaultProjectLocation[0] = '\0';
    acceptedTermsVersion.clear();
    windowsDisclaimerAcknowledgedV68 = false;
    fs::path settingsFile = appDataPath / "launcher_settings.modu";

    if (fs::exists(settingsFile)) {
        std::ifstream file(settingsFile);
        std::string line;
        while (std::getline(file, line)) {
            const std::string cleaned = TrimCopy(line);
            if (cleaned.empty() || cleaned[0] == '#') continue;

            const size_t eq = cleaned.find('=');
            if (eq == std::string::npos) continue;

            const std::string key = TrimCopy(cleaned.substr(0, eq));
            const std::string value = TrimCopy(cleaned.substr(eq + 1));
            if (key == "defaultProjectLocation" && !value.empty()) {
                std::snprintf(defaultProjectLocation, sizeof(defaultProjectLocation), "%s", value.c_str());
            } else if (key == "acceptedTermsVersion") {
                acceptedTermsVersion = value;
            } else if (key == "WindowsDisclaimerAcknowledged_V6_8") {
                windowsDisclaimerAcknowledgedV68 = (value == "1" || value == "true" || value == "yes");
            }
        }
    }

    if (defaultProjectLocation[0] == '\0') {
        const std::string fallback = GetPlatformDefaultProjectsPath();
        std::snprintf(defaultProjectLocation, sizeof(defaultProjectLocation), "%s", fallback.c_str());
    }
}

void ProjectManager::saveLauncherSettings() const {
    fs::path settingsFile = appDataPath / "launcher_settings.modu";
    std::ofstream file(settingsFile);
    if (!file.is_open()) {
        return;
    }

    file << "# Modularity launcher settings\n";
    if (!acceptedTermsVersion.empty()) {
        file << "acceptedTermsVersion=" << acceptedTermsVersion << "\n";
    }
    file << "WindowsDisclaimerAcknowledged_V6_8=" << (windowsDisclaimerAcknowledgedV68 ? "1" : "0") << "\n";
    file << "defaultProjectLocation=" << defaultProjectLocation << "\n";
}

void ProjectManager::addToRecentProjects(const std::string& name, const std::string& path) {
#if MODULARITY_RUNTIME_ONLY
    (void)name;
    (void)path;
    return;
#else
    std::string absolutePath = path;
    try {
        if (fs::exists(path)) {
            absolutePath = fs::canonical(path).string();
        } else {
            // For new projects, the file might not exist yet - use absolute()
            absolutePath = fs::absolute(path).string();
        }
    } catch (...) {
        // Keep original path if conversion fails
    }
    
    recentProjects.erase(
        std::remove_if(recentProjects.begin(), recentProjects.end(),
            [&absolutePath](const RecentProject& rp) { return rp.path == absolutePath; }),
        recentProjects.end()
    );

    std::time_t now = std::time(nullptr);
    char timeStr[64];
    std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M", std::localtime(&now));

    RecentProject rp;
    rp.name = name;
    rp.path = absolutePath;  // Use absolute path
    rp.lastOpened = timeStr;
    recentProjects.insert(recentProjects.begin(), rp);

    saveRecentProjects();
#endif
}

bool ProjectManager::loadProject(const std::string& path) {
    if (currentProject.load(path)) {
        addToRecentProjects(currentProject.name, path);
        return true;
    }
    errorMessage = "Failed to load project file";
    return false;
}

bool SceneSerializationInternal::WriteLegacySceneStream(std::ostream& file,
                                                        const std::vector<SceneObject>& objects,
                                                        int nextId,
                                                        float timeOfDay,
                                                        const SkyboxSettings& skyboxSettings) {
    try {
        file << "# Scene File\n";
        file << "version=" << SceneSerializationInternal::kLegacySceneFormatVersion << "\n";
        file << "nextId=" << nextId << "\n";
        file << "timeOfDay=" << timeOfDay << "\n";
        file << "skyboxMode=" << static_cast<int>(skyboxSettings.mode) << "\n";
        file << "skyboxSunTexture=" << skyboxSettings.sunTexturePath << "\n";
        file << "skyboxMoonTexture=" << skyboxSettings.moonTexturePath << "\n";
        file << "skyboxScrollTexture=" << skyboxSettings.scrollingTexturePath << "\n";
        file << "skyboxScrollRepeat=" << skyboxSettings.scrollingRepeatX << "," << skyboxSettings.scrollingRepeatY << "\n";
        file << "skyboxScrollLookSensitivity=" << skyboxSettings.scrollingLookSensitivity << "\n";
        file << "skyboxScrollVerticalInfluence=" << skyboxSettings.scrollingVerticalInfluence << "\n";
        file << "skyboxEnvironmentReflections=" << (skyboxSettings.environmentReflections ? 1 : 0) << "\n";
        file << "skyboxEnvironmentReflectionIntensity=" << skyboxSettings.environmentReflectionIntensity << "\n";
        file << "skyboxReflectionDistanceFadeStart=" << skyboxSettings.reflectionDistanceFadeStart << "\n";
        file << "skyboxReflectionDistanceFadeEnd=" << skyboxSettings.reflectionDistanceFadeEnd << "\n";
        file << "fogEnabled=" << (skyboxSettings.fogEnabled ? 1 : 0) << "\n";
        file << "fogMode=" << skyboxSettings.fogMode << "\n";
        file << "fogColor=" << skyboxSettings.fogColor.r << "," << skyboxSettings.fogColor.g << "," << skyboxSettings.fogColor.b << "\n";
        file << "fogStart=" << skyboxSettings.fogStart << "\n";
        file << "fogEnd=" << skyboxSettings.fogEnd << "\n";
        file << "fogDensity=" << skyboxSettings.fogDensity << "\n";
        file << "fogHeight=" << skyboxSettings.fogHeight << "\n";
        file << "fogHeightFalloff=" << skyboxSettings.fogHeightFalloff << "\n";
        file << "objectCount=" << objects.size() << "\n";
        file << "\n";

        for (const auto& sourceObj : objects) {
            SceneObject obj = sourceObj;
            EnsureInspectorComponentMetadata(obj);
            file << "[Object]\n";
            file << "id=" << obj.id << "\n";
            file << "name=" << obj.name << "\n";
            ObjectType legacyType = GetLegacyTypeFromComponents(obj);
            file << "type=" << static_cast<int>(legacyType) << "\n";
            file << "enabled=" << (obj.enabled ? 1 : 0) << "\n";
            file << "invariable=" << (obj.IsInvariable ? 1 : 0) << "\n";
            file << "layer=" << obj.layer << "\n";
            file << "tag=" << obj.tag << "\n";
            file << "hasRenderer=" << (obj.hasRenderer ? 1 : 0) << "\n";
            file << "renderType=" << static_cast<int>(obj.renderType) << "\n";
            file << "faceCamera=" << (obj.faceCamera ? 1 : 0) << "\n";
            file << "hasLight=" << (obj.hasLight ? 1 : 0) << "\n";
            file << "hasLight2D=" << (obj.hasLight2D ? 1 : 0) << "\n";
            file << "hasReflectionCast=" << (obj.hasReflectionCast ? 1 : 0) << "\n";
            file << "hasCamera=" << (obj.hasCamera ? 1 : 0) << "\n";
            file << "hasPostFX=" << (obj.hasPostFX ? 1 : 0) << "\n";
            file << "hasUI=" << (obj.hasUI ? 1 : 0) << "\n";
            file << "hasShadowCaster2D=" << (obj.hasShadowCaster2D ? 1 : 0) << "\n";
            file << "uiType=" << static_cast<int>(obj.ui.type) << "\n";
            file << "parentId=" << obj.parentId << "\n";
            file << "position=" << obj.localPosition.x << "," << obj.localPosition.y << "," << obj.localPosition.z << "\n";
            file << "rotation=" << obj.localRotation.x << "," << obj.localRotation.y << "," << obj.localRotation.z << "\n";
            file << "scale=" << obj.localScale.x << "," << obj.localScale.y << "," << obj.localScale.z << "\n";
            file << "hasRigidbody=" << (obj.hasRigidbody ? 1 : 0) << "\n";
            if (obj.hasRigidbody) {
                file << "rbEnabled=" << (obj.rigidbody.enabled ? 1 : 0) << "\n";
                file << "rbMass=" << obj.rigidbody.mass << "\n";
                file << "rbUseCustomCenterOfMass=" << (obj.rigidbody.useCustomCenterOfMass ? 1 : 0) << "\n";
                file << "rbCenterOfMass=" << obj.rigidbody.centerOfMass.x << "," << obj.rigidbody.centerOfMass.y << "," << obj.rigidbody.centerOfMass.z << "\n";
                file << "rbUseGravity=" << (obj.rigidbody.useGravity ? 1 : 0) << "\n";
                file << "rbKinematic=" << (obj.rigidbody.isKinematic ? 1 : 0) << "\n";
                file << "rbLinearDamping=" << obj.rigidbody.linearDamping << "\n";
                file << "rbAngularDamping=" << obj.rigidbody.angularDamping << "\n";
                file << "rbLockRotX=" << (obj.rigidbody.lockRotationX ? 1 : 0) << "\n";
                file << "rbLockRotY=" << (obj.rigidbody.lockRotationY ? 1 : 0) << "\n";
                file << "rbLockRotZ=" << (obj.rigidbody.lockRotationZ ? 1 : 0) << "\n";
            }
            file << "hasRigidbody2D=" << (obj.hasRigidbody2D ? 1 : 0) << "\n";
            if (obj.hasRigidbody2D) {
                file << "rb2dEnabled=" << (obj.rigidbody2D.enabled ? 1 : 0) << "\n";
                file << "rb2dUseGravity=" << (obj.rigidbody2D.useGravity ? 1 : 0) << "\n";
                file << "rb2dLockRotation=" << (obj.rigidbody2D.lockRotation ? 1 : 0) << "\n";
                file << "rb2dGravityScale=" << obj.rigidbody2D.gravityScale << "\n";
                file << "rb2dLinearDamping=" << obj.rigidbody2D.linearDamping << "\n";
                file << "rb2dVelocity=" << obj.rigidbody2D.velocity.x << "," << obj.rigidbody2D.velocity.y << "\n";
            }
            file << "hasCollider2D=" << (obj.hasCollider2D ? 1 : 0) << "\n";
            if (obj.hasCollider2D) {
                file << "collider2dEnabled=" << (obj.collider2D.enabled ? 1 : 0) << "\n";
                file << "collider2dType=" << static_cast<int>(obj.collider2D.type) << "\n";
                file << "collider2dBox=" << obj.collider2D.boxSize.x << "," << obj.collider2D.boxSize.y << "\n";
                file << "collider2dOffset=" << obj.collider2D.offset.x << "," << obj.collider2D.offset.y << "\n";
                file << "collider2dClosed=" << (obj.collider2D.closed ? 1 : 0) << "\n";
                file << "collider2dEdgeThickness=" << obj.collider2D.edgeThickness << "\n";
                file << "collider2dPoints=";
                for (size_t i = 0; i < obj.collider2D.points.size(); ++i) {
                    if (i > 0) file << ";";
                    file << obj.collider2D.points[i].x << "," << obj.collider2D.points[i].y;
                }
                file << "\n";
            }
            file << "hasParallaxLayer2D=" << (obj.hasParallaxLayer2D ? 1 : 0) << "\n";
            if (obj.hasParallaxLayer2D) {
                file << "parallax2dEnabled=" << (obj.parallaxLayer2D.enabled ? 1 : 0) << "\n";
                file << "parallax2dOrder=" << obj.parallaxLayer2D.order << "\n";
                file << "parallax2dFactor=" << obj.parallaxLayer2D.factor << "\n";
                file << "parallax2dRepeatX=" << (obj.parallaxLayer2D.repeatX ? 1 : 0) << "\n";
                file << "parallax2dRepeatY=" << (obj.parallaxLayer2D.repeatY ? 1 : 0) << "\n";
                file << "parallax2dDisableCulling=" << (obj.parallaxLayer2D.disableCulling ? 1 : 0) << "\n";
                file << "parallax2dSpacing=" << obj.parallaxLayer2D.repeatSpacing.x << "," << obj.parallaxLayer2D.repeatSpacing.y << "\n";
            }
            file << "hasCameraFollow2D=" << (obj.hasCameraFollow2D ? 1 : 0) << "\n";
            if (obj.hasCameraFollow2D) {
                file << "cameraFollow2dEnabled=" << (obj.cameraFollow2D.enabled ? 1 : 0) << "\n";
                file << "cameraFollow2dTarget=" << obj.cameraFollow2D.targetId << "\n";
                file << "cameraFollow2dOffset=" << obj.cameraFollow2D.offset.x << "," << obj.cameraFollow2D.offset.y << "\n";
                file << "cameraFollow2dSmoothTime=" << obj.cameraFollow2D.smoothTime << "\n";
            }
            file << "hasCollider=" << (obj.hasCollider ? 1 : 0) << "\n";
            if (obj.hasCollider) {
                file << "colliderEnabled=" << (obj.collider.enabled ? 1 : 0) << "\n";
                file << "colliderType=" << static_cast<int>(obj.collider.type) << "\n";
                file << "colliderBox=" << obj.collider.boxSize.x << "," << obj.collider.boxSize.y << "," << obj.collider.boxSize.z << "\n";
                file << "colliderOffset=" << obj.collider.offset.x << "," << obj.collider.offset.y << "," << obj.collider.offset.z << "\n";
                file << "colliderConvex=" << (obj.collider.convex ? 1 : 0) << "\n";
                file << "colliderStaticFriction=" << obj.collider.staticFriction << "\n";
                file << "colliderDynamicFriction=" << obj.collider.dynamicFriction << "\n";
                file << "colliderRestitution=" << obj.collider.restitution << "\n";
            }
            file << "hasPlayerController=" << (obj.hasPlayerController ? 1 : 0) << "\n";
            if (obj.hasPlayerController) {
                file << "pcEnabled=" << (obj.playerController.enabled ? 1 : 0) << "\n";
                file << "pcMoveSpeed=" << obj.playerController.moveSpeed << "\n";
                file << "pcRunSpeed=" << obj.playerController.runSpeed << "\n";
                file << "pcLookSensitivity=" << obj.playerController.lookSensitivity << "\n";
                file << "pcGroundAcceleration=" << obj.playerController.groundAcceleration << "\n";
                file << "pcAirAcceleration=" << obj.playerController.airAcceleration << "\n";
                file << "pcBraking=" << obj.playerController.braking << "\n";
                file << "pcMinSurfaceControl=" << obj.playerController.minSurfaceControl << "\n";
                file << "pcSlideGravity=" << obj.playerController.slideGravity << "\n";
                file << "pcPlatformCarry=" << obj.playerController.platformCarry << "\n";
                file << "pcHeight=" << obj.playerController.height << "\n";
                file << "pcRadius=" << obj.playerController.radius << "\n";
                file << "pcJumpStrength=" << obj.playerController.jumpStrength << "\n";
            }
            file << "hasAudioSource=" << (obj.hasAudioSource ? 1 : 0) << "\n";
            if (obj.hasAudioSource) {
                file << "audioEnabled=" << (obj.audioSource.enabled ? 1 : 0) << "\n";
                file << "audioClip=" << obj.audioSource.clipPath << "\n";
                file << "audioVolume=" << obj.audioSource.volume << "\n";
                file << "audioLoop=" << (obj.audioSource.loop ? 1 : 0) << "\n";
                file << "audioPlayOnStart=" << (obj.audioSource.playOnStart ? 1 : 0) << "\n";
                file << "audioSpatial=" << (AudioSourceUsesSpatialization(obj.audioSource) ? 1 : 0) << "\n";
                file << "audioSpatialBlend=" << GetAudioSpatialBlend(obj.audioSource) << "\n";
                file << "audioMinDistance=" << obj.audioSource.minDistance << "\n";
                file << "audioMaxDistance=" << obj.audioSource.maxDistance << "\n";
                file << "audioRolloffMode=" << static_cast<int>(obj.audioSource.rolloffMode) << "\n";
                file << "audioRolloff=" << obj.audioSource.rolloff << "\n";
                file << "audioCustomMidDistance=" << obj.audioSource.customMidDistance << "\n";
                file << "audioCustomMidGain=" << obj.audioSource.customMidGain << "\n";
                file << "audioCustomEndGain=" << obj.audioSource.customEndGain << "\n";
            }
            file << "hasVideoPlayer=" << (obj.hasVideoPlayer ? 1 : 0) << "\n";
            if (obj.hasVideoPlayer) {
                file << "videoEnabled=" << (obj.videoPlayer.enabled ? 1 : 0) << "\n";
                file << "videoPath=" << obj.videoPlayer.videoPath << "\n";
                file << "videoPlayOnAwake=" << (obj.videoPlayer.playOnAwake ? 1 : 0) << "\n";
                file << "videoLoop=" << (obj.videoPlayer.loop ? 1 : 0) << "\n";
                file << "videoFlipX=" << (obj.videoPlayer.flipX ? 1 : 0) << "\n";
                file << "videoFlipY=" << (obj.videoPlayer.flipY ? 1 : 0) << "\n";
                file << "videoPlaybackSpeed=" << obj.videoPlayer.playbackSpeed << "\n";
                file << "videoPlayAudioFromVideo=" << (obj.videoPlayer.playAudioFromVideo ? 1 : 0) << "\n";
                file << "videoRouteAudioToSource=" << (obj.videoPlayer.routeAudioToSource ? 1 : 0) << "\n";
                file << "videoOutputAudioSourceObjectId=" << obj.videoPlayer.outputAudioSourceObjectId << "\n";
                file << "videoAudioVolume=" << obj.videoPlayer.videoAudioVolume << "\n";
                file << "videoAudioMuted=" << (obj.videoPlayer.videoAudioMuted ? 1 : 0) << "\n";
                file << "videoSyncAudioToVideo=" << (obj.videoPlayer.syncAudioToVideo ? 1 : 0) << "\n";
                file << "videoAudioSyncTolerance=" << obj.videoPlayer.audioSyncTolerance << "\n";
            }
            file << "hasParticleSystem2D=" << (obj.hasParticleSystem2D ? 1 : 0) << "\n";
            if (obj.hasParticleSystem2D) {
                const auto& ps = obj.particleSystem2D;
                file << "ps2dEnabled=" << (ps.enabled ? 1 : 0) << "\n";
                file << "ps2dLooping=" << (ps.looping ? 1 : 0) << "\n";
                file << "ps2dPrewarm=" << (ps.prewarm ? 1 : 0) << "\n";
                file << "ps2dPlayOnAwake=" << (ps.playOnAwake ? 1 : 0) << "\n";
                file << "ps2dAutoRandomSeed=" << (ps.autoRandomSeed ? 1 : 0) << "\n";
                file << "ps2dRandomSeed=" << ps.randomSeed << "\n";
                file << "ps2dStartDelay=" << ps.startDelay << "\n";
                file << "ps2dStartLifetime=" << ps.startLifetime.min << "," << ps.startLifetime.max << "," << (ps.startLifetime.random ? 1 : 0) << "\n";
                file << "ps2dStartSpeed=" << ps.startSpeed.min << "," << ps.startSpeed.max << "," << (ps.startSpeed.random ? 1 : 0) << "\n";
                file << "ps2dStartSize=" << ps.startSize.min << "," << ps.startSize.max << "," << (ps.startSize.random ? 1 : 0) << "\n";
                file << "ps2dStartRotation=" << ps.startRotation.min << "," << ps.startRotation.max << "," << (ps.startRotation.random ? 1 : 0) << "\n";
                file << "ps2dStartColor=" << ps.startColor.r << "," << ps.startColor.g << "," << ps.startColor.b << "," << ps.startColor.a << "\n";
                file << "ps2dGravity=" << ps.gravityModifier << "\n";
                file << "ps2dSimulationSpeed=" << ps.simulationSpeed << "\n";
                file << "ps2dMaxParticles=" << ps.maxParticles << "\n";
                file << "ps2dEmissionRate=" << ps.emissionRate << "\n";
                file << "ps2dBurstCount=" << ps.burstCount << "\n";
                file << "ps2dBurstTime=" << ps.burstTime << "\n";
                file << "ps2dBurstLoop=" << (ps.burstLoop ? 1 : 0) << "\n";
                file << "ps2dShape=" << ps.shape << "\n";
                file << "ps2dShapeRadius=" << ps.shapeRadius << "\n";
                file << "ps2dShapeBox=" << ps.shapeBox.x << "," << ps.shapeBox.y << "\n";
                file << "ps2dVelocityOverLifetimeEnabled=" << (ps.velocityOverLifetimeEnabled ? 1 : 0) << "\n";
                file << "ps2dVelocityOverLifetime=" << ps.velocityOverLifetime.x << "," << ps.velocityOverLifetime.y << "\n";
                file << "ps2dColorOverLifetimeEnabled=" << (ps.colorOverLifetimeEnabled ? 1 : 0) << "\n";
                file << "ps2dColorOverLifetime=" << ps.colorOverLifetime.r << "," << ps.colorOverLifetime.g << "," << ps.colorOverLifetime.b << "," << ps.colorOverLifetime.a << "\n";
                file << "ps2dSizeOverLifetimeEnabled=" << (ps.sizeOverLifetimeEnabled ? 1 : 0) << "\n";
                file << "ps2dSizeOverLifetime=" << ps.sizeOverLifetime << "\n";
                file << "ps2dRotationOverLifetimeEnabled=" << (ps.rotationOverLifetimeEnabled ? 1 : 0) << "\n";
                file << "ps2dRotationOverLifetime=" << ps.rotationOverLifetime << "\n";
                file << "ps2dNoiseEnabled=" << (ps.noiseEnabled ? 1 : 0) << "\n";
                file << "ps2dNoiseStrength=" << ps.noiseStrength << "\n";
                file << "ps2dNoiseFrequency=" << ps.noiseFrequency << "\n";
                file << "ps2dTexture=" << ps.texturePath << "\n";
                file << "ps2dMaterial=" << ps.materialPath << "\n";
                file << "ps2dReceiveLighting2D=" << (ps.receiveLighting2D ? 1 : 0) << "\n";
                file << "ps2dUnlitLighting2D=" << (ps.unlitLighting2D ? 1 : 0) << "\n";
                file << "ps2dEmissiveLighting2D=" << ps.emissiveLighting2D << "\n";
            }
            file << "hasReverbZone=" << (obj.hasReverbZone ? 1 : 0) << "\n";
            if (obj.hasReverbZone) {
                file << "reverbEnabled=" << (obj.reverbZone.enabled ? 1 : 0) << "\n";
                file << "reverbPreset=" << static_cast<int>(obj.reverbZone.preset) << "\n";
                file << "reverbShape=" << static_cast<int>(obj.reverbZone.shape) << "\n";
                file << "reverbBox=" << obj.reverbZone.boxSize.x << "," << obj.reverbZone.boxSize.y << "," << obj.reverbZone.boxSize.z << "\n";
                file << "reverbRadius=" << obj.reverbZone.radius << "\n";
                file << "reverbBlend=" << obj.reverbZone.blendDistance << "\n";
                file << "reverbMinDistance=" << obj.reverbZone.minDistance << "\n";
                file << "reverbMaxDistance=" << obj.reverbZone.maxDistance << "\n";
                file << "reverbRoom=" << obj.reverbZone.room << "\n";
                file << "reverbRoomHF=" << obj.reverbZone.roomHF << "\n";
                file << "reverbRoomLF=" << obj.reverbZone.roomLF << "\n";
                file << "reverbDecayTime=" << obj.reverbZone.decayTime << "\n";
                file << "reverbDecayHFRatio=" << obj.reverbZone.decayHFRatio << "\n";
                file << "reverbReflections=" << obj.reverbZone.reflections << "\n";
                file << "reverbReflectionsDelay=" << obj.reverbZone.reflectionsDelay << "\n";
                file << "reverbReverb=" << obj.reverbZone.reverb << "\n";
                file << "reverbReverbDelay=" << obj.reverbZone.reverbDelay << "\n";
                file << "reverbHFReference=" << obj.reverbZone.hfReference << "\n";
                file << "reverbLFReference=" << obj.reverbZone.lfReference << "\n";
                file << "reverbRoomRolloffFactor=" << obj.reverbZone.roomRolloffFactor << "\n";
                file << "reverbDiffusion=" << obj.reverbZone.diffusion << "\n";
                file << "reverbDensity=" << obj.reverbZone.density << "\n";
            }
            file << "hasGroundBakedType=" << (obj.hasGroundBakedType ? 1 : 0) << "\n";
            if (obj.hasGroundBakedType) {
                file << "groundBakedEnabled=" << (obj.groundBakedType.enabled ? 1 : 0) << "\n";
                file << "groundBakedInclude=" << (obj.groundBakedType.includeInBake ? 1 : 0) << "\n";
                file << "groundBakedAreaCost=" << obj.groundBakedType.areaCost << "\n";
            }
            file << "hasObsticleObject=" << (obj.hasObsticleObject ? 1 : 0) << "\n";
            if (obj.hasObsticleObject) {
                file << "obsticleEnabled=" << (obj.obsticleObject.enabled ? 1 : 0) << "\n";
                file << "obsticleCarve=" << (obj.obsticleObject.carve ? 1 : 0) << "\n";
                file << "obsticlePadding=" << obj.obsticleObject.padding << "\n";
            }
            file << "hasAIAgent=" << (obj.hasAIAgent ? 1 : 0) << "\n";
            if (obj.hasAIAgent) {
                file << "aiAgentEnabled=" << (obj.aiAgent.enabled ? 1 : 0) << "\n";
                file << "aiAgentUseTargetObject=" << (obj.aiAgent.useTargetObject ? 1 : 0) << "\n";
                file << "aiAgentTargetId=" << obj.aiAgent.targetId << "\n";
                file << "aiAgentDestination=" << obj.aiAgent.destination.x << "," << obj.aiAgent.destination.y << "," << obj.aiAgent.destination.z << "\n";
                file << "aiAgentSpeed=" << obj.aiAgent.speed << "\n";
                file << "aiAgentStoppingDistance=" << obj.aiAgent.stoppingDistance << "\n";
                file << "aiAgentRepathInterval=" << obj.aiAgent.repathInterval << "\n";
                file << "aiAgentAutoRepath=" << (obj.aiAgent.autoRepath ? 1 : 0) << "\n";
                file << "aiAgentAlignToPath=" << (obj.aiAgent.alignToPath ? 1 : 0) << "\n";
                file << "aiAgentDebugDrawPath=" << (obj.aiAgent.debugDrawPath ? 1 : 0) << "\n";
            }
            file << "hasRig25DRoot=" << (obj.hasRig25DRoot ? 1 : 0) << "\n";
            if (obj.hasRig25DRoot) {
                file << "rig25dRootEnabled=" << (obj.rig25DRoot.enabled ? 1 : 0) << "\n";
            }
            file << "hasRig25DNode=" << (obj.hasRig25DNode ? 1 : 0) << "\n";
            if (obj.hasRig25DNode) {
                file << "rig25dNodeEnabled=" << (obj.rig25DNode.enabled ? 1 : 0) << "\n";
                file << "rig25dNodeId=" << obj.rig25DNode.nodeId << "\n";
                file << "rig25dNodeName=" << obj.rig25DNode.nodeName << "\n";
            }
            file << "hasAnimation=" << (obj.hasAnimation ? 1 : 0) << "\n";
            if (obj.hasAnimation) {
                AnimationComponent animation = obj.animation;
                NormalizeAnimationClipSlots(animation);
                file << "animEnabled=" << (animation.enabled ? 1 : 0) << "\n";
                file << "animClipAsset=" << animation.clipAssetPath << "\n";
                file << "animClipCount=" << animation.clips.size() << "\n";
                file << "animActiveClipIndex=" << animation.activeClipIndex << "\n";
                for (size_t ci = 0; ci < animation.clips.size(); ++ci) {
                    const auto& clip = animation.clips[ci];
                    file << "animClip" << ci << "_name=" << clip.name << "\n";
                    file << "animClip" << ci << "_asset=" << clip.assetPath << "\n";
                }
                file << "animClipLength=" << animation.clipLength << "\n";
                file << "animPlaySpeed=" << animation.playSpeed << "\n";
                file << "animLoop=" << (animation.loop ? 1 : 0) << "\n";
                file << "animPlayOnAwake=" << (animation.playOnAwake ? 1 : 0) << "\n";
                file << "animApplyOnScrub=" << (animation.applyOnScrub ? 1 : 0) << "\n";
                file << "animKeyCount=" << animation.keyframes.size() << "\n";
                for (size_t ki = 0; ki < animation.keyframes.size(); ++ki) {
                    const auto& key = animation.keyframes[ki];
                    file << "animKey" << ki << "_time=" << key.time << "\n";
                    file << "animKey" << ki << "_pos=" << key.position.x << "," << key.position.y << "," << key.position.z << "\n";
                    file << "animKey" << ki << "_rot=" << key.rotation.x << "," << key.rotation.y << "," << key.rotation.z << "\n";
                    file << "animKey" << ki << "_scale=" << key.scale.x << "," << key.scale.y << "," << key.scale.z << "\n";
                    file << "animKey" << ki << "_interp=" << static_cast<int>(key.interpolation) << "\n";
                    file << "animKey" << ki << "_curve=" << static_cast<int>(key.curveMode) << "\n";
                    file << "animKey" << ki << "_in=" << key.bezierIn.x << "," << key.bezierIn.y << "\n";
                    file << "animKey" << ki << "_out=" << key.bezierOut.x << "," << key.bezierOut.y << "\n";
                }
                file << "animEventCount=" << animation.events.size() << "\n";
                for (size_t ei = 0; ei < animation.events.size(); ++ei) {
                    const auto& evt = animation.events[ei];
                    file << "animEvent" << ei << "_time=" << evt.time << "\n";
                    file << "animEvent" << ei << "_id=" << evt.eventId << "\n";
                    file << "animEvent" << ei << "_payload=" << evt.payload << "\n";
                }
                file << "animTrackCount=" << animation.tracks.size() << "\n";
                for (size_t ti = 0; ti < animation.tracks.size(); ++ti) {
                    const auto& track = animation.tracks[ti];
                    file << "animTrack" << ti << "_enabled=" << (track.enabled ? 1 : 0) << "\n";
                    file << "animTrack" << ti << "_path=" << track.path << "\n";
                    file << "animTrack" << ti << "_label=" << track.label << "\n";
                    file << "animTrack" << ti << "_default=" << track.defaultValue << "\n";
                    file << "animTrack" << ti << "_keyCount=" << track.keyframes.size() << "\n";
                    for (size_t ki = 0; ki < track.keyframes.size(); ++ki) {
                        const auto& key = track.keyframes[ki];
                        file << "animTrack" << ti << "_key" << ki << "_time=" << key.time << "\n";
                        file << "animTrack" << ti << "_key" << ki << "_value=" << key.value << "\n";
                        file << "animTrack" << ti << "_key" << ki << "_interp=" << static_cast<int>(key.interpolation) << "\n";
                        file << "animTrack" << ti << "_key" << ki << "_curve=" << static_cast<int>(key.curveMode) << "\n";
                        file << "animTrack" << ti << "_key" << ki << "_in=" << key.bezierIn.x << "," << key.bezierIn.y << "\n";
                        file << "animTrack" << ti << "_key" << ki << "_out=" << key.bezierOut.x << "," << key.bezierOut.y << "\n";
                    }
                }
            }
            file << "hasSkeletalAnimation=" << (obj.hasSkeletalAnimation ? 1 : 0) << "\n";
            if (obj.hasSkeletalAnimation) {
                file << "skelEnabled=" << (obj.skeletal.enabled ? 1 : 0) << "\n";
                file << "skelUseGpu=" << (obj.skeletal.useGpuSkinning ? 1 : 0) << "\n";
                file << "skelAllowCpuFallback=" << (obj.skeletal.allowCpuFallback ? 1 : 0) << "\n";
                file << "skelUseAnimation=" << (obj.skeletal.useAnimation ? 1 : 0) << "\n";
                file << "skelClipIndex=" << obj.skeletal.clipIndex << "\n";
                file << "skelPlaySpeed=" << obj.skeletal.playSpeed << "\n";
                file << "skelLoop=" << (obj.skeletal.loop ? 1 : 0) << "\n";
                file << "skelMaxBones=" << obj.skeletal.maxBones << "\n";
            }
            file << "materialColor=" << obj.material.color.r << "," << obj.material.color.g << "," << obj.material.color.b << "\n";
            file << "materialAlpha=" << obj.material.alpha << "\n";
            file << "materialAmbient=" << obj.material.ambientStrength << "\n";
            file << "materialSpecular=" << obj.material.specularStrength << "\n";
            file << "materialShininess=" << obj.material.shininess << "\n";
            file << "materialNormalMapIntensity=" << obj.material.normalMapIntensity << "\n";
            file << "materialTextureMix=" << obj.material.textureMix << "\n";
            file << "materialUvTiling=" << obj.material.uvTiling.x << "," << obj.material.uvTiling.y << "\n";
            file << "materialUvOffset=" << obj.material.uvOffset.x << "," << obj.material.uvOffset.y << "\n";
            file << "materialTextureFilter=" << static_cast<int>(obj.material.textureFilter) << "\n";
            file << "materialPath=" << obj.materialPath << "\n";
            file << "albedoTex=" << obj.albedoTexturePath << "\n";
            file << "overlayTex=" << obj.overlayTexturePath << "\n";
            file << "normalMap=" << obj.normalMapPath << "\n";
            file << "shaderPack=" << obj.shaderPackPath << "\n";
            file << "vertexShader=" << obj.vertexShaderPath << "\n";
            file << "fragmentShader=" << obj.fragmentShaderPath << "\n";
            file << "useOverlay=" << (obj.useOverlay ? 1 : 0) << "\n";
            file << "additionalMaterialCount=" << obj.additionalMaterialPaths.size() << "\n";
            for (size_t mi = 0; mi < obj.additionalMaterialPaths.size(); ++mi) {
                file << "additionalMaterial" << mi << "=" << obj.additionalMaterialPaths[mi] << "\n";
            }
            file << "nextInspectorScriptId=" << obj.nextInspectorScriptId << "\n";
            file << "componentOrder=";
            for (size_t oi = 0; oi < obj.inspectorComponentOrder.size(); ++oi) {
                if (oi > 0) file << ";";
                file << obj.inspectorComponentOrder[oi];
            }
            file << "\n";
            file << "scripts=" << obj.scripts.size() << "\n";
            for (size_t si = 0; si < obj.scripts.size(); ++si) {
                const auto& sc = obj.scripts[si];
                file << "script" << si << "_id=" << sc.inspectorId << "\n";
                file << "script" << si << "_path=" << sc.path << "\n";
                file << "script" << si << "_lang=" << static_cast<int>(sc.language) << "\n";
                file << "script" << si << "_type=" << sc.managedType << "\n";
                file << "script" << si << "_enabled=" << (sc.enabled ? 1 : 0) << "\n";
                file << "script" << si << "_settings=" << sc.settings.size() << "\n";
                for (size_t k = 0; k < sc.settings.size(); ++k) {
                    file << "script" << si << "_setting" << k << "=" << sc.settings[k].key << ":" << sc.settings[k].value << "\n";
                }
            }
            file << "lightColor=" << obj.light.color.r << "," << obj.light.color.g << "," << obj.light.color.b << "\n";
            if (obj.hasLight) {
                file << "lightType=" << static_cast<int>(obj.light.type) << "\n";
            }
            file << "lightIntensity=" << obj.light.intensity << "\n";
            file << "lightRange=" << obj.light.range << "\n";
            file << "lightEdgeFade=" << obj.light.edgeFade << "\n";
            file << "lightInner=" << obj.light.innerAngle << "\n";
            file << "lightOuter=" << obj.light.outerAngle << "\n";
            file << "lightSize=" << obj.light.size.x << "," << obj.light.size.y << "\n";
            file << "lightCastShadows=" << (obj.light.castShadows ? 1 : 0) << "\n";
            file << "lightSoftShadows=" << (obj.light.softShadows ? 1 : 0) << "\n";
            file << "lightShadowBias=" << obj.light.shadowBias << "\n";
            file << "lightShadowSoftness=" << obj.light.shadowSoftness << "\n";
            file << "lightShadowResolution=" << obj.light.shadowResolution << "\n";
            file << "lightEnabled=" << (obj.light.enabled ? 1 : 0) << "\n";
            if (obj.hasReflectionCast) {
                file << "reflectionCastEnabled=" << (obj.reflectionCast.enabled ? 1 : 0) << "\n";
                file << "reflectionCastUpdateMode=" << static_cast<int>(obj.reflectionCast.updateMode) << "\n";
                file << "reflectionCastBox=" << obj.reflectionCast.boxSize.x << "," << obj.reflectionCast.boxSize.y << "," << obj.reflectionCast.boxSize.z << "\n";
                file << "reflectionCastBlend=" << obj.reflectionCast.blendDistance << "\n";
                file << "reflectionCastIntensity=" << obj.reflectionCast.intensity << "\n";
                file << "reflectionCastResolution=" << obj.reflectionCast.resolution << "\n";
            }
            if (obj.hasLight2D) {
                file << "light2dEnabled=" << (obj.light2D.enabled ? 1 : 0) << "\n";
                file << "light2dType=" << static_cast<int>(obj.light2D.type) << "\n";
                file << "light2dColor="
                     << obj.light2D.color.r << ","
                     << obj.light2D.color.g << ","
                     << obj.light2D.color.b << ","
                     << obj.light2D.color.a << "\n";
                file << "light2dIntensity=" << obj.light2D.intensity << "\n";
                file << "light2dRadius=" << obj.light2D.radius << "\n";
                file << "light2dInnerRadius=" << obj.light2D.innerRadius << "\n";
                file << "light2dOuterRadius=" << obj.light2D.outerRadius << "\n";
                file << "light2dFalloffStrength=" << obj.light2D.falloffStrength << "\n";
                file << "light2dInnerSpotAngle=" << obj.light2D.innerSpotAngle << "\n";
                file << "light2dOuterSpotAngle=" << obj.light2D.outerSpotAngle << "\n";
                file << "light2dBlendStyle=" << obj.light2D.blendStyle << "\n";
                file << "light2dOrder=" << obj.light2D.lightOrder << "\n";
                file << "light2dOverlap=" << static_cast<int>(obj.light2D.overlapOperation) << "\n";
                file << "light2dShadowStrength=" << obj.light2D.shadowStrength << "\n";
                file << "light2dVolumetric=" << (obj.light2D.volumetricEnabled ? 1 : 0) << "\n";
                file << "light2dCastShadows=" << (obj.light2D.castsShadows ? 1 : 0) << "\n";
                file << "light2dTargetAllLayers=" << (obj.light2D.targetAllLayers ? 1 : 0) << "\n";
                file << "light2dTargetLayerMask=" << obj.light2D.targetLayerMask << "\n";
                file << "light2dNormalQuality=" << static_cast<int>(obj.light2D.normalMapQuality) << "\n";
                file << "light2dNormalDistance=" << obj.light2D.normalMapDistance << "\n";
                file << "light2dUseDistanceExponent=" << (obj.light2D.useDistanceExponent ? 1 : 0) << "\n";
                file << "light2dDistanceExponent=" << obj.light2D.distanceExponent << "\n";
                file << "light2dCookie=" << obj.light2D.cookieTexturePath << "\n";
                file << "light2dCookieScale=" << obj.light2D.cookieScale.x << "," << obj.light2D.cookieScale.y << "\n";
                file << "light2dCookieRotation=" << obj.light2D.cookieRotation << "\n";
                file << "light2dFreeformFeather=" << obj.light2D.freeformFeather << "\n";
                file << "light2dFreeformEdgeFalloff=" << obj.light2D.freeformEdgeFalloff << "\n";
                file << "light2dFlickerEnabled=" << (obj.light2D.flicker.enabled ? 1 : 0) << "\n";
                file << "light2dFlickerSpeed=" << obj.light2D.flicker.speed << "\n";
                file << "light2dFlickerAmount=" << obj.light2D.flicker.amount << "\n";
                file << "light2dFlickerSeed=" << obj.light2D.flicker.seed << "\n";
                if (!obj.light2D.shapePoints.empty()) {
                    file << "light2dShape=";
                    for (size_t i = 0; i < obj.light2D.shapePoints.size(); ++i) {
                        if (i > 0) file << ";";
                        file << obj.light2D.shapePoints[i].x << "," << obj.light2D.shapePoints[i].y;
                    }
                    file << "\n";
                }
            }
            if (obj.hasShadowCaster2D) {
                file << "shadowCaster2dEnabled=" << (obj.shadowCaster2D.enabled ? 1 : 0) << "\n";
                file << "shadowCaster2dSelfShadow=" << (obj.shadowCaster2D.castsSelfShadow ? 1 : 0) << "\n";
                file << "shadowCaster2dTargetAllLayers=" << (obj.shadowCaster2D.targetAllLayers ? 1 : 0) << "\n";
                file << "shadowCaster2dTargetLayerMask=" << obj.shadowCaster2D.targetLayerMask << "\n";
                file << "shadowCaster2dStrength=" << obj.shadowCaster2D.shadowStrength << "\n";
                if (!obj.shadowCaster2D.points.empty()) {
                    file << "shadowCaster2dShape=";
                    for (size_t i = 0; i < obj.shadowCaster2D.points.size(); ++i) {
                        if (i > 0) file << ";";
                        file << obj.shadowCaster2D.points[i].x << "," << obj.shadowCaster2D.points[i].y;
                    }
                    file << "\n";
                }
            }
            file << "cameraType=" << static_cast<int>(obj.camera.type) << "\n";
            file << "cameraFov=" << obj.camera.fov << "\n";
            file << "cameraNear=" << obj.camera.nearClip << "\n";
            file << "cameraFar=" << obj.camera.farClip << "\n";
            file << "cameraPostFX=" << (obj.camera.applyPostFX ? 1 : 0) << "\n";
            file << "cameraUse2D=" << (obj.camera.use2D ? 1 : 0) << "\n";
            file << "cameraPixelsPerUnit=" << obj.camera.pixelsPerUnit << "\n";
            file << "uiAnchor=" << static_cast<int>(obj.ui.anchor) << "\n";
            file << "uiPosition=" << obj.ui.position.x << "," << obj.ui.position.y << "\n";
            file << "uiRotation=" << obj.ui.rotation << "\n";
            file << "uiSize=" << obj.ui.size.x << "," << obj.ui.size.y << "\n";
            file << "uiMaskChildren=" << (obj.ui.maskChildren ? 1 : 0) << "\n";
            file << "uiSliderValue=" << obj.ui.sliderValue << "\n";
            file << "uiSliderMin=" << obj.ui.sliderMin << "\n";
            file << "uiSliderMax=" << obj.ui.sliderMax << "\n";
            file << "uiLabel=" << obj.ui.label << "\n";
            file << "uiColor=" << obj.ui.color.r << "," << obj.ui.color.g << "," << obj.ui.color.b << "," << obj.ui.color.a << "\n";
            file << "uiInteractable=" << (obj.ui.interactable ? 1 : 0) << "\n";
            file << "uiSliderStyle=" << static_cast<int>(obj.ui.sliderStyle) << "\n";
            file << "uiButtonStyle=" << static_cast<int>(obj.ui.buttonStyle) << "\n";
            file << "uiStylePreset=" << obj.ui.stylePreset << "\n";
            file << "uiTextScale=" << obj.ui.textScale << "\n";
            file << "uiTextFont=" << obj.ui.textFont << "\n";
            file << "uiTextWrap=" << (obj.ui.textAutoWrap ? 1 : 0) << "\n";
            file << "uiTextHAlign=" << static_cast<int>(obj.ui.textHAlign) << "\n";
            file << "uiTextVAlign=" << static_cast<int>(obj.ui.textVAlign) << "\n";
            file << "uiTextEffectFlags=" << obj.ui.textEffectFlags << "\n";
            file << "uiTextEffectSpeed=" << obj.ui.textEffectSpeed << "\n";
            file << "uiTextEffectIntensity=" << obj.ui.textEffectIntensity << "\n";
            file << "uiRenderIn3D=" << (obj.ui.renderIn3D ? 1 : 0) << "\n";
            file << "uiRenderTargetSize=" << obj.ui.renderTargetSize.x << "," << obj.ui.renderTargetSize.y << "\n";
            file << "uiPseudo3DEnabled=" << (obj.ui.pseudo3DEnabled ? 1 : 0) << "\n";
            file << "uiPseudo3DUseOffscreen=" << (obj.ui.pseudo3DUseOffscreenSurface ? 1 : 0) << "\n";
            file << "uiPseudo3DPanelSize=" << obj.ui.pseudo3DPanelSize.x << "," << obj.ui.pseudo3DPanelSize.y << "\n";
            file << "uiPseudo3DTopLeftOffset=" << obj.ui.pseudo3DTopLeftOffset.x << "," << obj.ui.pseudo3DTopLeftOffset.y << "\n";
            file << "uiPseudo3DTopRightOffset=" << obj.ui.pseudo3DTopRightOffset.x << "," << obj.ui.pseudo3DTopRightOffset.y << "\n";
            file << "uiPseudo3DBottomRightOffset=" << obj.ui.pseudo3DBottomRightOffset.x << "," << obj.ui.pseudo3DBottomRightOffset.y << "\n";
            file << "uiPseudo3DBottomLeftOffset=" << obj.ui.pseudo3DBottomLeftOffset.x << "," << obj.ui.pseudo3DBottomLeftOffset.y << "\n";
            file << "uiPseudo3DPivot=" << obj.ui.pseudo3DPivot.x << "," << obj.ui.pseudo3DPivot.y << "\n";
            file << "uiPseudo3DPerspectiveIntensity=" << obj.ui.pseudo3DPerspectiveIntensity << "\n";
            file << "uiPseudo3DSkewAmount=" << obj.ui.pseudo3DSkewAmount << "\n";
            file << "uiPseudo3DCurvatureAmount=" << obj.ui.pseudo3DCurvatureAmount << "\n";
            file << "uiPseudo3DAnchorTargetId=" << obj.ui.pseudo3DAnchorTargetId << "\n";
            file << "uiPseudo3DDistanceScaling=" << (obj.ui.pseudo3DDistanceScalingEnabled ? 1 : 0) << "\n";
            file << "uiPseudo3DAdjustPerspectiveDistance=" << (obj.ui.pseudo3DAdjustPerspectiveWithDistance ? 1 : 0) << "\n";
            file << "uiPseudo3DMinDistance=" << obj.ui.pseudo3DMinDistance << "\n";
            file << "uiPseudo3DMaxDistance=" << obj.ui.pseudo3DMaxDistance << "\n";
            file << "uiPseudo3DInteractionDistance=" << obj.ui.pseudo3DInteractionDistance << "\n";
            file << "uiPseudo3DDepthSort=" << obj.ui.pseudo3DDepthSort << "\n";
            file << "uiPseudo3DAllowInteraction=" << (obj.ui.pseudo3DAllowInteraction ? 1 : 0) << "\n";
            file << "uiSpriteSheetEnabled=" << (obj.ui.spriteSheetEnabled ? 1 : 0) << "\n";
            file << "uiSpriteSheetGrid=" << obj.ui.spriteSheetColumns << "," << obj.ui.spriteSheetRows << "\n";
            file << "uiSpriteSheetFrame=" << obj.ui.spriteSheetFrame << "\n";
            file << "uiSpriteSheetFps=" << obj.ui.spriteSheetFps << "\n";
            file << "uiSpriteSheetLoop=" << (obj.ui.spriteSheetLoop ? 1 : 0) << "\n";
            file << "uiSpriteCustomFramesEnabled=" << (obj.ui.spriteCustomFramesEnabled ? 1 : 0) << "\n";
            file << "uiSpriteSourceSize=" << obj.ui.spriteSourceWidth << "," << obj.ui.spriteSourceHeight << "\n";
            file << "uiNineSliceEnabled=" << (obj.ui.nineSliceEnabled ? 1 : 0) << "\n";
            file << "uiNineSliceBorder="
                 << obj.ui.nineSliceBorder.x << ","
                 << obj.ui.nineSliceBorder.y << ","
                 << obj.ui.nineSliceBorder.z << ","
                 << obj.ui.nineSliceBorder.w << "\n";
            file << "uiNineSliceTileEdges=" << (obj.ui.nineSliceTileEdges ? 1 : 0) << "\n";
            file << "uiNineSliceTileCenter=" << (obj.ui.nineSliceTileCenter ? 1 : 0) << "\n";
            file << "uiReceiveLighting2D=" << (obj.ui.receiveLighting2D ? 1 : 0) << "\n";
            file << "uiUnlitLighting2D=" << (obj.ui.unlitLighting2D ? 1 : 0) << "\n";
            file << "uiEmissiveLighting2D=" << obj.ui.emissiveLighting2D << "\n";
            file << "uiFillColor=" << obj.ui.fillColor.r << "," << obj.ui.fillColor.g << "," << obj.ui.fillColor.b << "," << obj.ui.fillColor.a << "\n";
            file << "uiBackgroundColor=" << obj.ui.backgroundColor.r << "," << obj.ui.backgroundColor.g << "," << obj.ui.backgroundColor.b << "," << obj.ui.backgroundColor.a << "\n";
            file << "uiBorderColor=" << obj.ui.borderColor.r << "," << obj.ui.borderColor.g << "," << obj.ui.borderColor.b << "," << obj.ui.borderColor.a << "\n";
            file << "uiTextColor=" << obj.ui.textColor.r << "," << obj.ui.textColor.g << "," << obj.ui.textColor.b << "," << obj.ui.textColor.a << "\n";
            file << "uiFontSize=" << obj.ui.fontSize << "\n";
            file << "uiSortingOrder=" << obj.ui.sortingOrder << "\n";
            if (!obj.ui.spriteCustomFrames.empty()) {
                file << "uiSpriteCustomFrames=";
                for (size_t i = 0; i < obj.ui.spriteCustomFrames.size(); ++i) {
                    const glm::ivec4& frame = obj.ui.spriteCustomFrames[i];
                    if (i > 0) file << ";";
                    file << frame.x << "," << frame.y << "," << frame.z << "," << frame.w;
                }
                file << "\n";
            }
            if (!obj.ui.spriteCustomFrameNames.empty()) {
                file << "uiSpriteCustomFrameNames=";
                for (size_t i = 0; i < obj.ui.spriteCustomFrameNames.size(); ++i) {
                    if (i > 0) file << ";";
                    file << obj.ui.spriteCustomFrameNames[i];
                }
                file << "\n";
            }
            if (!obj.ui.spriteCustomFrameScales.empty()) {
                file << "uiSpriteCustomFrameScales=";
                for (size_t i = 0; i < obj.ui.spriteCustomFrameScales.size(); ++i) {
                    if (i > 0) file << ";";
                    const glm::vec2& scale = obj.ui.spriteCustomFrameScales[i];
                    file << scale.x << "," << scale.y;
                }
                file << "\n";
            }
            if (obj.hasPostFX) {
                file << "postEnabled=" << (obj.postFx.enabled ? 1 : 0) << "\n";
                file << "postVolumeGlobal=" << (obj.postFx.isGlobal ? 1 : 0) << "\n";
                file << "postVolumePriority=" << obj.postFx.priority << "\n";
                file << "postVolumeWeight=" << obj.postFx.blendWeight << "\n";
                file << "postVolumeBlendRadius=" << obj.postFx.blendRadius << "\n";
                file << "postHDREnabled=" << (obj.postFx.hdrEnabled ? 1 : 0) << "\n";
                file << "postToneMapper=" << static_cast<int>(obj.postFx.toneMapper) << "\n";
                file << "postWhitePoint=" << obj.postFx.whitePoint << "\n";
                file << "postGamma=" << obj.postFx.gamma << "\n";
                file << "postBloomEnabled=" << (obj.postFx.bloomEnabled ? 1 : 0) << "\n";
                file << "postBloomThreshold=" << obj.postFx.bloomThreshold << "\n";
                file << "postBloomSoftKnee=" << obj.postFx.bloomSoftKnee << "\n";
                file << "postBloomIntensity=" << obj.postFx.bloomIntensity << "\n";
                file << "postBloomRadius=" << obj.postFx.bloomRadius << "\n";
                file << "postColorAdjustEnabled=" << (obj.postFx.colorAdjustEnabled ? 1 : 0) << "\n";
                file << "postExposure=" << obj.postFx.exposure << "\n";
                file << "postContrast=" << obj.postFx.contrast << "\n";
                file << "postSaturation=" << obj.postFx.saturation << "\n";
                file << "postColorFilter=" << obj.postFx.colorFilter.r << "," << obj.postFx.colorFilter.g << "," << obj.postFx.colorFilter.b << "\n";
                file << "postMotionBlurEnabled=" << (obj.postFx.motionBlurEnabled ? 1 : 0) << "\n";
                file << "postMotionBlurStrength=" << obj.postFx.motionBlurStrength << "\n";
                file << "postMotionBlurThreshold=" << obj.postFx.motionBlurThreshold << "\n";
                file << "postMotionBlurClamp=" << obj.postFx.motionBlurClamp << "\n";
                file << "postVignetteEnabled=" << (obj.postFx.vignetteEnabled ? 1 : 0) << "\n";
                file << "postVignetteIntensity=" << obj.postFx.vignetteIntensity << "\n";
                file << "postVignetteSmoothness=" << obj.postFx.vignetteSmoothness << "\n";
                file << "postChromaticEnabled=" << (obj.postFx.chromaticAberrationEnabled ? 1 : 0) << "\n";
                file << "postChromaticAmount=" << obj.postFx.chromaticAmount << "\n";
                file << "postSharpenEnabled=" << (obj.postFx.sharpenEnabled ? 1 : 0) << "\n";
                file << "postSharpenStrength=" << obj.postFx.sharpenStrength << "\n";
                file << "postAOEnabled=" << (obj.postFx.ambientOcclusionEnabled ? 1 : 0) << "\n";
                file << "postAORadius=" << obj.postFx.aoRadius << "\n";
                file << "postAOStrength=" << obj.postFx.aoStrength << "\n";
                file << "postDitherEnabled=" << (obj.postFx.ditherEnabled ? 1 : 0) << "\n";
                file << "postDitherIntensity=" << obj.postFx.ditherIntensity << "\n";
                file << "postDitherColorBits=" << obj.postFx.ditherColorBits << "\n";
                file << "postDitherDarkAdjustment=" << obj.postFx.ditherDarkAdjustment << "\n";
                file << "postDitherPixelation=" << obj.postFx.ditherPixelation << "\n";
                file << "postDitherSize=" << obj.postFx.ditherSize << "\n";
                file << "postDitherContrast=" << obj.postFx.ditherContrast << "\n";
                file << "postDitherOffset=" << obj.postFx.ditherOffset << "\n";
                file << "postDitherPalette=" << static_cast<int>(obj.postFx.ditherPalette) << "\n";
                file << "postDitherPattern=" << static_cast<int>(obj.postFx.ditherPattern) << "\n";
                file << "postStaticEnabled=" << (obj.postFx.staticEnabled ? 1 : 0) << "\n";
                file << "postStaticIntensity=" << obj.postFx.staticIntensity << "\n";
                file << "postStaticGrainScale=" << obj.postFx.staticGrainScale << "\n";
                file << "postStaticDarkAreaInfluence=" << obj.postFx.staticDarkAreaInfluence << "\n";
                file << "postStaticSpeed=" << obj.postFx.staticSpeed << "\n";
                file << "postStaticDistortionEnabled=" << (obj.postFx.staticDistortionEnabled ? 1 : 0) << "\n";
                file << "postStaticDistortionHorizontalJitterAmount="
                     << obj.postFx.staticDistortionHorizontalJitterAmount << "\n";
                file << "postStaticDistortionLineDensity=" << obj.postFx.staticDistortionLineDensity << "\n";
                file << "postStaticDistortionGlitchFrequency=" << obj.postFx.staticDistortionGlitchFrequency << "\n";
                file << "postStaticDistortionStrength=" << obj.postFx.staticDistortionStrength << "\n";
                file << "postLensDistortionEnabled=" << (obj.postFx.lensDistortionEnabled ? 1 : 0) << "\n";
                file << "postLensDistortionAmount=" << obj.postFx.lensDistortionAmount << "\n";
                file << "postLensDistortionEdgeFalloff=" << obj.postFx.lensDistortionEdgeFalloff << "\n";
                file << "postLensDistortionCenterOffset=" << obj.postFx.lensDistortionCenterOffset.x << ","
                     << obj.postFx.lensDistortionCenterOffset.y << "\n";
                file << "postVHSOverlayEnabled=" << (obj.postFx.vhsOverlayEnabled ? 1 : 0) << "\n";
                file << "postVHSOverlayOpacity=" << obj.postFx.vhsOverlayOpacity << "\n";
                file << "postVHSOverlayScanlineStrength=" << obj.postFx.vhsOverlayScanlineStrength << "\n";
                file << "postVHSOverlayTapeNoise=" << obj.postFx.vhsOverlayTapeNoise << "\n";
                file << "postVHSOverlayChromaBleed=" << obj.postFx.vhsOverlayChromaBleed << "\n";
                file << "postVHSOverlayBottomNoiseBandHeight=" << obj.postFx.vhsOverlayBottomNoiseBandHeight << "\n";
                file << "postVHSOverlayBottomNoiseBandIntensity=" << obj.postFx.vhsOverlayBottomNoiseBandIntensity << "\n";
                file << "postVHSOverlayDistortionStrength=" << obj.postFx.vhsOverlayDistortionStrength << "\n";
                file << "postVHSOverlayAnimationSpeed=" << obj.postFx.vhsOverlayAnimationSpeed << "\n";
                file << "postVHSOverlayColorBleed=" << obj.postFx.vhsOverlayColorBleed << "\n";
                file << "postVHSOverlayBanding=" << obj.postFx.vhsOverlayBanding << "\n";
                file << "postWavyEnabled=" << (obj.postFx.wavyEnabled ? 1 : 0) << "\n";
                file << "postWavyAmplitude=" << obj.postFx.wavyAmplitude << "\n";
                file << "postWavyFrequency=" << obj.postFx.wavyFrequency << "\n";
                file << "postWavySpeed=" << obj.postFx.wavySpeed << "\n";
                file << "postWavyVertical=" << (obj.postFx.wavyVertical ? 1 : 0) << "\n";
            }

            file << "scriptCount=" << obj.scripts.size() << "\n";
            for (size_t s = 0; s < obj.scripts.size(); ++s) {
                const auto& sc = obj.scripts[s];
                file << "script" << s << "_path=" << sc.path << "\n";
                file << "script" << s << "_lang=" << static_cast<int>(sc.language) << "\n";
                file << "script" << s << "_type=" << sc.managedType << "\n";
                file << "script" << s << "_enabled=" << (sc.enabled ? 1 : 0) << "\n";
                file << "script" << s << "_settingCount=" << sc.settings.size() << "\n";
                for (size_t si = 0; si < sc.settings.size(); ++si) {
                    file << "script" << s << "_setting" << si << "=" << sc.settings[si].key << ":" << sc.settings[si].value << "\n";
                }
            }
            
            if (obj.hasRenderer &&
                (obj.renderType == RenderType::OBJMesh || obj.renderType == RenderType::Model) &&
                !obj.meshPath.empty()) {
                file << "meshPath=" << obj.meshPath << "\n";
                if (obj.renderType == RenderType::Model && obj.meshSourceIndex >= 0) {
                    file << "meshSourceIndex=" << obj.meshSourceIndex << "\n";
                }
            }

            file << "children=";
            for (size_t i = 0; i < obj.childIds.size(); i++) {
                if (i > 0) file << ",";
                file << obj.childIds[i];
            }
            file << "\n\n";
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to write legacy scene stream: " << e.what() << std::endl;
        return false;
    }
}

bool SceneSerializationInternal::SaveLegacyScene(const fs::path& filePath,
                                                 const std::vector<SceneObject>& objects,
                                                 int nextId,
                                                 float timeOfDay,
                                                 const SkyboxSettings& skyboxSettings,
                                                 SceneSerializer::Metadata* metadata) {
    try {
        std::ofstream file(filePath);
        if (!file.is_open()) return false;
        if (!WriteLegacySceneStream(file, objects, nextId, timeOfDay, skyboxSettings)) {
            return false;
        }
        file.close();
        if (metadata) {
            metadata->version = SceneSerializationInternal::kLegacySceneFormatVersion;
            metadata->fileFormat = SceneSerializer::FileFormat::LegacyFlat;
            metadata->loadedFromLegacyLayout = true;
            metadata->upgradedToModularLayout = false;
            metadata->sourcePath = filePath;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save legacy scene: " << e.what() << std::endl;
        return false;
    }
}

namespace {
template <typename Vec2T>
void ParseVec2(const std::string& value, Vec2T& out) {
    sscanf(value.c_str(), "%f,%f", &out.x, &out.y);
}

void ParseIVec2(const std::string& value, glm::ivec2& out) {
    int x = 0;
    int y = 0;
    if (sscanf(value.c_str(), "%d,%d", &x, &y) == 2) {
        out.x = x;
        out.y = y;
    }
}

void ParseVec2List(const std::string& value, std::vector<glm::vec2>& out) {
    out.clear();
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ';')) {
        if (item.empty()) continue;
        glm::vec2 v(0.0f);
        ParseVec2(item, v);
        out.push_back(v);
    }
}

template <typename Vec3T>
void ParseVec3(const std::string& value, Vec3T& out) {
    sscanf(value.c_str(), "%f,%f,%f", &out.x, &out.y, &out.z);
}

template <typename Vec4T>
void ParseVec4(const std::string& value, Vec4T& out) {
    sscanf(value.c_str(), "%f,%f,%f,%f", &out.x, &out.y, &out.z, &out.w);
}

void ParseMinMaxFloat(const std::string& value, ParticleSystem2DComponent::MinMaxFloat& out) {
    int random = out.random ? 1 : 0;
    if (sscanf(value.c_str(), "%f,%f,%d", &out.min, &out.max, &random) >= 2) {
        out.random = random != 0;
        if (out.max < out.min) std::swap(out.min, out.max);
    }
}

bool g_deferSceneAssetLoading = false;

bool IsDefaultTransform(const SceneObject& obj) {
    auto nearZero = [](float v) { return std::abs(v) < 1e-4f; };
    auto nearOne = [](float v) { return std::abs(v - 1.0f) < 1e-4f; };
    return nearZero(obj.localPosition.x) &&
           nearZero(obj.localPosition.y) &&
           nearZero(obj.localPosition.z) &&
           nearZero(obj.localRotation.x) &&
           nearZero(obj.localRotation.y) &&
           nearZero(obj.localRotation.z) &&
           nearOne(obj.localScale.x) &&
           nearOne(obj.localScale.y) &&
           nearOne(obj.localScale.z);
}

void ApplyModelRootTransform(SceneObject& obj, const ModelSceneData& sceneData) {
    if (sceneData.nodes.empty()) return;
    if (obj.localInitialized && !IsDefaultTransform(obj)) return;
    const auto& root = sceneData.nodes.front();
    obj.localPosition = root.localPosition;
    obj.localRotation = root.localRotation;
    obj.localScale = root.localScale;
    obj.localInitialized = true;
    obj.position = obj.localPosition;
    obj.rotation = obj.localRotation;
    obj.scale = obj.localScale;
}

using KeyHandler = void (*)(SceneObject&, const std::string&);

const std::unordered_map<std::string, KeyHandler>& GetSceneObjectKeyHandlers() {
    static const std::unordered_map<std::string, KeyHandler> handlers = {
        {"id", +[](SceneObject& obj, const std::string& value) { obj.id = std::stoi(value); }},
        {"name", +[](SceneObject& obj, const std::string& value) { obj.name = value; }},
        {"type", +[](SceneObject& obj, const std::string& value) {
             obj.type = static_cast<ObjectType>(std::stoi(value));
         }},
        {"enabled", +[](SceneObject& obj, const std::string& value) { obj.enabled = (std::stoi(value) != 0); }},
        {"invariable", +[](SceneObject& obj, const std::string& value) { obj.IsInvariable = (std::stoi(value) != 0); }},
        {"layer", +[](SceneObject& obj, const std::string& value) { obj.layer = std::stoi(value); }},
        {"tag", +[](SceneObject& obj, const std::string& value) { obj.tag = value; }},
        {"hasRenderer", +[](SceneObject& obj, const std::string& value) { obj.hasRenderer = std::stoi(value) != 0; }},
        {"renderType", +[](SceneObject& obj, const std::string& value) {
             obj.renderType = static_cast<RenderType>(std::stoi(value));
             if (obj.renderType != RenderType::None) {
                 obj.hasRenderer = true;
             }
         }},
        {"faceCamera", +[](SceneObject& obj, const std::string& value) { obj.faceCamera = std::stoi(value) != 0; }},
        {"hasLight", +[](SceneObject& obj, const std::string& value) { obj.hasLight = std::stoi(value) != 0; }},
        {"hasLight2D", +[](SceneObject& obj, const std::string& value) { obj.hasLight2D = std::stoi(value) != 0; }},
        {"hasReflectionCast", +[](SceneObject& obj, const std::string& value) { obj.hasReflectionCast = std::stoi(value) != 0; }},
        {"hasCamera", +[](SceneObject& obj, const std::string& value) { obj.hasCamera = std::stoi(value) != 0; }},
        {"hasPostFX", +[](SceneObject& obj, const std::string& value) { obj.hasPostFX = std::stoi(value) != 0; }},
        {"hasUI", +[](SceneObject& obj, const std::string& value) { obj.hasUI = std::stoi(value) != 0; }},
        {"hasShadowCaster2D", +[](SceneObject& obj, const std::string& value) { obj.hasShadowCaster2D = std::stoi(value) != 0; }},
        {"uiType", +[](SceneObject& obj, const std::string& value) {
             obj.ui.type = static_cast<UIElementType>(std::stoi(value));
             if (obj.ui.type != UIElementType::None) {
                 obj.hasUI = true;
             }
         }},
        {"parentId", +[](SceneObject& obj, const std::string& value) { obj.parentId = std::stoi(value); }},
        {"position", +[](SceneObject& obj, const std::string& value) {
             ParseVec3(value, obj.position);
             obj.localPosition = obj.position;
             obj.localInitialized = true;
         }},
        {"rotation", +[](SceneObject& obj, const std::string& value) {
             ParseVec3(value, obj.rotation);
             obj.rotation = NormalizeEulerDegrees(obj.rotation);
             obj.localRotation = obj.rotation;
             obj.localInitialized = true;
         }},
        {"scale", +[](SceneObject& obj, const std::string& value) {
             ParseVec3(value, obj.scale);
             obj.localScale = obj.scale;
             obj.localInitialized = true;
         }},
        {"hasRigidbody", +[](SceneObject& obj, const std::string& value) { obj.hasRigidbody = std::stoi(value) != 0; }},
        {"rbEnabled", +[](SceneObject& obj, const std::string& value) { obj.rigidbody.enabled = std::stoi(value) != 0; }},
        {"rbMass", +[](SceneObject& obj, const std::string& value) { obj.rigidbody.mass = std::stof(value); }},
        {"rbUseCustomCenterOfMass", +[](SceneObject& obj, const std::string& value) { obj.rigidbody.useCustomCenterOfMass = std::stoi(value) != 0; }},
        {"rbCenterOfMass", +[](SceneObject& obj, const std::string& value) { ParseVec3(value, obj.rigidbody.centerOfMass); }},
        {"rbUseGravity", +[](SceneObject& obj, const std::string& value) { obj.rigidbody.useGravity = std::stoi(value) != 0; }},
        {"rbKinematic", +[](SceneObject& obj, const std::string& value) { obj.rigidbody.isKinematic = std::stoi(value) != 0; }},
        {"rbLinearDamping", +[](SceneObject& obj, const std::string& value) { obj.rigidbody.linearDamping = std::stof(value); }},
        {"rbAngularDamping", +[](SceneObject& obj, const std::string& value) { obj.rigidbody.angularDamping = std::stof(value); }},
        {"rbLockRotX", +[](SceneObject& obj, const std::string& value) { obj.rigidbody.lockRotationX = std::stoi(value) != 0; }},
        {"rbLockRotY", +[](SceneObject& obj, const std::string& value) { obj.rigidbody.lockRotationY = std::stoi(value) != 0; }},
        {"rbLockRotZ", +[](SceneObject& obj, const std::string& value) { obj.rigidbody.lockRotationZ = std::stoi(value) != 0; }},
        {"hasRigidbody2D", +[](SceneObject& obj, const std::string& value) { obj.hasRigidbody2D = std::stoi(value) != 0; }},
        {"rb2dEnabled", +[](SceneObject& obj, const std::string& value) { obj.rigidbody2D.enabled = std::stoi(value) != 0; }},
        {"rb2dUseGravity", +[](SceneObject& obj, const std::string& value) { obj.rigidbody2D.useGravity = std::stoi(value) != 0; }},
        {"rb2dLockRotation", +[](SceneObject& obj, const std::string& value) { obj.rigidbody2D.lockRotation = std::stoi(value) != 0; }},
        {"rb2dGravityScale", +[](SceneObject& obj, const std::string& value) { obj.rigidbody2D.gravityScale = std::stof(value); }},
        {"rb2dLinearDamping", +[](SceneObject& obj, const std::string& value) { obj.rigidbody2D.linearDamping = std::stof(value); }},
        {"rb2dVelocity", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.rigidbody2D.velocity); }},
        {"hasCollider2D", +[](SceneObject& obj, const std::string& value) { obj.hasCollider2D = std::stoi(value) != 0; }},
        {"collider2dEnabled", +[](SceneObject& obj, const std::string& value) { obj.collider2D.enabled = std::stoi(value) != 0; }},
        {"collider2dType", +[](SceneObject& obj, const std::string& value) { obj.collider2D.type = static_cast<Collider2DType>(std::stoi(value)); }},
        {"collider2dBox", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.collider2D.boxSize); }},
        {"collider2dOffset", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.collider2D.offset); }},
        {"collider2dClosed", +[](SceneObject& obj, const std::string& value) { obj.collider2D.closed = std::stoi(value) != 0; }},
        {"collider2dEdgeThickness", +[](SceneObject& obj, const std::string& value) { obj.collider2D.edgeThickness = std::stof(value); }},
        {"collider2dPoints", +[](SceneObject& obj, const std::string& value) { ParseVec2List(value, obj.collider2D.points); }},
        {"hasParallaxLayer2D", +[](SceneObject& obj, const std::string& value) { obj.hasParallaxLayer2D = std::stoi(value) != 0; }},
        {"parallax2dEnabled", +[](SceneObject& obj, const std::string& value) { obj.parallaxLayer2D.enabled = std::stoi(value) != 0; }},
        {"parallax2dOrder", +[](SceneObject& obj, const std::string& value) { obj.parallaxLayer2D.order = std::stoi(value); }},
        {"parallax2dFactor", +[](SceneObject& obj, const std::string& value) { obj.parallaxLayer2D.factor = std::stof(value); }},
        {"parallax2dRepeatX", +[](SceneObject& obj, const std::string& value) { obj.parallaxLayer2D.repeatX = std::stoi(value) != 0; }},
        {"parallax2dRepeatY", +[](SceneObject& obj, const std::string& value) { obj.parallaxLayer2D.repeatY = std::stoi(value) != 0; }},
        {"parallax2dDisableCulling", +[](SceneObject& obj, const std::string& value) { obj.parallaxLayer2D.disableCulling = std::stoi(value) != 0; }},
        {"parallax2dSpacing", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.parallaxLayer2D.repeatSpacing); }},
        {"hasCameraFollow2D", +[](SceneObject& obj, const std::string& value) { obj.hasCameraFollow2D = std::stoi(value) != 0; }},
        {"cameraFollow2dEnabled", +[](SceneObject& obj, const std::string& value) { obj.cameraFollow2D.enabled = std::stoi(value) != 0; }},
        {"cameraFollow2dTarget", +[](SceneObject& obj, const std::string& value) { obj.cameraFollow2D.targetId = std::stoi(value); }},
        {"cameraFollow2dOffset", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.cameraFollow2D.offset); }},
        {"cameraFollow2dSmoothTime", +[](SceneObject& obj, const std::string& value) { obj.cameraFollow2D.smoothTime = std::stof(value); }},
        {"hasCollider", +[](SceneObject& obj, const std::string& value) { obj.hasCollider = std::stoi(value) != 0; }},
        {"colliderEnabled", +[](SceneObject& obj, const std::string& value) { obj.collider.enabled = std::stoi(value) != 0; }},
        {"colliderType", +[](SceneObject& obj, const std::string& value) { obj.collider.type = static_cast<ColliderType>(std::stoi(value)); }},
        {"colliderBox", +[](SceneObject& obj, const std::string& value) { ParseVec3(value, obj.collider.boxSize); }},
        {"colliderOffset", +[](SceneObject& obj, const std::string& value) { ParseVec3(value, obj.collider.offset); }},
        {"colliderConvex", +[](SceneObject& obj, const std::string& value) { obj.collider.convex = std::stoi(value) != 0; }},
        {"colliderStaticFriction", +[](SceneObject& obj, const std::string& value) { obj.collider.staticFriction = std::stof(value); }},
        {"colliderDynamicFriction", +[](SceneObject& obj, const std::string& value) { obj.collider.dynamicFriction = std::stof(value); }},
        {"colliderRestitution", +[](SceneObject& obj, const std::string& value) { obj.collider.restitution = std::stof(value); }},
        {"hasPlayerController", +[](SceneObject& obj, const std::string& value) { obj.hasPlayerController = std::stoi(value) != 0; }},
        {"pcEnabled", +[](SceneObject& obj, const std::string& value) { obj.playerController.enabled = std::stoi(value) != 0; }},
        {"pcMoveSpeed", +[](SceneObject& obj, const std::string& value) { obj.playerController.moveSpeed = std::stof(value); }},
        {"pcRunSpeed", +[](SceneObject& obj, const std::string& value) { obj.playerController.runSpeed = std::stof(value); }},
        {"pcLookSensitivity", +[](SceneObject& obj, const std::string& value) { obj.playerController.lookSensitivity = std::stof(value); }},
        {"pcGroundAcceleration", +[](SceneObject& obj, const std::string& value) { obj.playerController.groundAcceleration = std::stof(value); }},
        {"pcAirAcceleration", +[](SceneObject& obj, const std::string& value) { obj.playerController.airAcceleration = std::stof(value); }},
        {"pcBraking", +[](SceneObject& obj, const std::string& value) { obj.playerController.braking = std::stof(value); }},
        {"pcMinSurfaceControl", +[](SceneObject& obj, const std::string& value) { obj.playerController.minSurfaceControl = std::stof(value); }},
        {"pcSlideGravity", +[](SceneObject& obj, const std::string& value) { obj.playerController.slideGravity = std::stof(value); }},
        {"pcPlatformCarry", +[](SceneObject& obj, const std::string& value) { obj.playerController.platformCarry = std::stof(value); }},
        {"pcHeight", +[](SceneObject& obj, const std::string& value) { obj.playerController.height = std::stof(value); }},
        {"pcRadius", +[](SceneObject& obj, const std::string& value) { obj.playerController.radius = std::stof(value); }},
        {"pcJumpStrength", +[](SceneObject& obj, const std::string& value) { obj.playerController.jumpStrength = std::stof(value); }},
        {"hasAudioSource", +[](SceneObject& obj, const std::string& value) { obj.hasAudioSource = std::stoi(value) != 0; }},
        {"audioEnabled", +[](SceneObject& obj, const std::string& value) { obj.audioSource.enabled = std::stoi(value) != 0; }},
        {"audioClip", +[](SceneObject& obj, const std::string& value) { obj.audioSource.clipPath = value; }},
        {"audioVolume", +[](SceneObject& obj, const std::string& value) { obj.audioSource.volume = std::stof(value); }},
        {"audioLoop", +[](SceneObject& obj, const std::string& value) { obj.audioSource.loop = std::stoi(value) != 0; }},
        {"audioPlayOnStart", +[](SceneObject& obj, const std::string& value) { obj.audioSource.playOnStart = std::stoi(value) != 0; }},
        {"audioSpatial", +[](SceneObject& obj, const std::string& value) {
            obj.audioSource.spatial = std::stoi(value) != 0;
            obj.audioSource.spatialBlend = obj.audioSource.spatial ? 1.0f : 0.0f;
        }},
        {"audioSpatialBlend", +[](SceneObject& obj, const std::string& value) {
            obj.audioSource.spatialBlend = std::clamp(std::stof(value), 0.0f, 1.0f);
            obj.audioSource.spatial = obj.audioSource.spatialBlend > 0.001f;
        }},
        {"audioMinDistance", +[](SceneObject& obj, const std::string& value) { obj.audioSource.minDistance = std::stof(value); }},
        {"audioMaxDistance", +[](SceneObject& obj, const std::string& value) { obj.audioSource.maxDistance = std::stof(value); }},
        {"audioRolloffMode", +[](SceneObject& obj, const std::string& value) { obj.audioSource.rolloffMode = static_cast<AudioRolloffMode>(std::stoi(value)); }},
        {"audioRolloff", +[](SceneObject& obj, const std::string& value) { obj.audioSource.rolloff = std::stof(value); }},
        {"audioCustomMidDistance", +[](SceneObject& obj, const std::string& value) { obj.audioSource.customMidDistance = std::stof(value); }},
        {"audioCustomMidGain", +[](SceneObject& obj, const std::string& value) { obj.audioSource.customMidGain = std::stof(value); }},
        {"audioCustomEndGain", +[](SceneObject& obj, const std::string& value) { obj.audioSource.customEndGain = std::stof(value); }},
        {"hasVideoPlayer", +[](SceneObject& obj, const std::string& value) { obj.hasVideoPlayer = std::stoi(value) != 0; }},
        {"videoEnabled", +[](SceneObject& obj, const std::string& value) { obj.hasVideoPlayer = true; obj.videoPlayer.enabled = std::stoi(value) != 0; }},
        {"videoPath", +[](SceneObject& obj, const std::string& value) { obj.hasVideoPlayer = true; obj.videoPlayer.videoPath = value; }},
        {"videoPlayOnAwake", +[](SceneObject& obj, const std::string& value) { obj.hasVideoPlayer = true; obj.videoPlayer.playOnAwake = std::stoi(value) != 0; }},
        {"videoLoop", +[](SceneObject& obj, const std::string& value) { obj.hasVideoPlayer = true; obj.videoPlayer.loop = std::stoi(value) != 0; }},
        {"videoFlipX", +[](SceneObject& obj, const std::string& value) { obj.hasVideoPlayer = true; obj.videoPlayer.flipX = std::stoi(value) != 0; }},
        {"videoFlipY", +[](SceneObject& obj, const std::string& value) { obj.hasVideoPlayer = true; obj.videoPlayer.flipY = std::stoi(value) != 0; }},
        {"videoPlaybackSpeed", +[](SceneObject& obj, const std::string& value) { obj.hasVideoPlayer = true; obj.videoPlayer.playbackSpeed = std::stof(value); }},
        {"videoPlayAudioFromVideo", +[](SceneObject& obj, const std::string& value) { obj.hasVideoPlayer = true; obj.videoPlayer.playAudioFromVideo = std::stoi(value) != 0; }},
        {"videoRouteAudioToSource", +[](SceneObject& obj, const std::string& value) { obj.hasVideoPlayer = true; obj.videoPlayer.routeAudioToSource = std::stoi(value) != 0; }},
        {"videoOutputAudioSourceObjectId", +[](SceneObject& obj, const std::string& value) { obj.hasVideoPlayer = true; obj.videoPlayer.outputAudioSourceObjectId = std::stoi(value); }},
        {"videoAudioVolume", +[](SceneObject& obj, const std::string& value) { obj.hasVideoPlayer = true; obj.videoPlayer.videoAudioVolume = std::stof(value); }},
        {"videoAudioMuted", +[](SceneObject& obj, const std::string& value) { obj.hasVideoPlayer = true; obj.videoPlayer.videoAudioMuted = std::stoi(value) != 0; }},
        {"videoSyncAudioToVideo", +[](SceneObject& obj, const std::string& value) { obj.hasVideoPlayer = true; obj.videoPlayer.syncAudioToVideo = std::stoi(value) != 0; }},
        {"videoAudioSyncTolerance", +[](SceneObject& obj, const std::string& value) { obj.hasVideoPlayer = true; obj.videoPlayer.audioSyncTolerance = std::stof(value); }},
        {"hasParticleSystem2D", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = std::stoi(value) != 0; }},
        {"ps2dEnabled", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.enabled = std::stoi(value) != 0; }},
        {"ps2dLooping", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.looping = std::stoi(value) != 0; }},
        {"ps2dPrewarm", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.prewarm = std::stoi(value) != 0; }},
        {"ps2dPlayOnAwake", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.playOnAwake = std::stoi(value) != 0; obj.particleSystem2D.playing = obj.particleSystem2D.playOnAwake; }},
        {"ps2dAutoRandomSeed", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.autoRandomSeed = std::stoi(value) != 0; }},
        {"ps2dRandomSeed", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.randomSeed = static_cast<uint32_t>(std::stoul(value)); }},
        {"ps2dStartDelay", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.startDelay = std::stof(value); }},
        {"ps2dStartLifetime", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; ParseMinMaxFloat(value, obj.particleSystem2D.startLifetime); }},
        {"ps2dStartSpeed", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; ParseMinMaxFloat(value, obj.particleSystem2D.startSpeed); }},
        {"ps2dStartSize", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; ParseMinMaxFloat(value, obj.particleSystem2D.startSize); }},
        {"ps2dStartRotation", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; ParseMinMaxFloat(value, obj.particleSystem2D.startRotation); }},
        {"ps2dStartColor", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; ParseVec4(value, obj.particleSystem2D.startColor); }},
        {"ps2dGravity", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.gravityModifier = std::stof(value); }},
        {"ps2dSimulationSpeed", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.simulationSpeed = std::stof(value); }},
        {"ps2dMaxParticles", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.maxParticles = std::max(1, std::stoi(value)); }},
        {"ps2dEmissionRate", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.emissionRate = std::max(0.0f, std::stof(value)); }},
        {"ps2dBurstCount", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.burstCount = std::max(0, std::stoi(value)); }},
        {"ps2dBurstTime", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.burstTime = std::max(0.0f, std::stof(value)); }},
        {"ps2dBurstLoop", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.burstLoop = std::stoi(value) != 0; }},
        {"ps2dShape", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.shape = std::clamp(std::stoi(value), 0, 2); }},
        {"ps2dShapeRadius", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.shapeRadius = std::max(0.0f, std::stof(value)); }},
        {"ps2dShapeBox", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; ParseVec2(value, obj.particleSystem2D.shapeBox); }},
        {"ps2dVelocityOverLifetimeEnabled", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.velocityOverLifetimeEnabled = std::stoi(value) != 0; }},
        {"ps2dVelocityOverLifetime", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; ParseVec2(value, obj.particleSystem2D.velocityOverLifetime); }},
        {"ps2dColorOverLifetimeEnabled", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.colorOverLifetimeEnabled = std::stoi(value) != 0; }},
        {"ps2dColorOverLifetime", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; ParseVec4(value, obj.particleSystem2D.colorOverLifetime); }},
        {"ps2dSizeOverLifetimeEnabled", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.sizeOverLifetimeEnabled = std::stoi(value) != 0; }},
        {"ps2dSizeOverLifetime", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.sizeOverLifetime = std::stof(value); }},
        {"ps2dRotationOverLifetimeEnabled", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.rotationOverLifetimeEnabled = std::stoi(value) != 0; }},
        {"ps2dRotationOverLifetime", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.rotationOverLifetime = std::stof(value); }},
        {"ps2dNoiseEnabled", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.noiseEnabled = std::stoi(value) != 0; }},
        {"ps2dNoiseStrength", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.noiseStrength = std::stof(value); }},
        {"ps2dNoiseFrequency", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.noiseFrequency = std::stof(value); }},
        {"ps2dTexture", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.texturePath = value; }},
        {"ps2dMaterial", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.materialPath = value; }},
        {"ps2dReceiveLighting2D", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.receiveLighting2D = std::stoi(value) != 0; }},
        {"ps2dUnlitLighting2D", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.unlitLighting2D = std::stoi(value) != 0; }},
        {"ps2dEmissiveLighting2D", +[](SceneObject& obj, const std::string& value) { obj.hasParticleSystem2D = true; obj.particleSystem2D.emissiveLighting2D = std::stof(value); }},
        {"hasReverbZone", +[](SceneObject& obj, const std::string& value) { obj.hasReverbZone = std::stoi(value) != 0; }},
        {"reverbEnabled", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.enabled = std::stoi(value) != 0; }},
        {"reverbPreset", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.preset = static_cast<ReverbPreset>(std::stoi(value)); }},
        {"reverbShape", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.shape = static_cast<ReverbZoneShape>(std::stoi(value)); }},
        {"reverbBox", +[](SceneObject& obj, const std::string& value) { ParseVec3(value, obj.reverbZone.boxSize); }},
        {"reverbRadius", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.radius = std::stof(value); }},
        {"reverbBlend", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.blendDistance = std::stof(value); }},
        {"reverbMinDistance", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.minDistance = std::stof(value); }},
        {"reverbMaxDistance", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.maxDistance = std::stof(value); }},
        {"reverbRoom", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.room = std::stof(value); }},
        {"reverbRoomHF", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.roomHF = std::stof(value); }},
        {"reverbRoomLF", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.roomLF = std::stof(value); }},
        {"reverbDecayTime", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.decayTime = std::stof(value); }},
        {"reverbDecayHFRatio", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.decayHFRatio = std::stof(value); }},
        {"reverbReflections", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.reflections = std::stof(value); }},
        {"reverbReflectionsDelay", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.reflectionsDelay = std::stof(value); }},
        {"reverbReverb", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.reverb = std::stof(value); }},
        {"reverbReverbDelay", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.reverbDelay = std::stof(value); }},
        {"reverbHFReference", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.hfReference = std::stof(value); }},
        {"reverbLFReference", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.lfReference = std::stof(value); }},
        {"reverbRoomRolloffFactor", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.roomRolloffFactor = std::stof(value); }},
        {"reverbDiffusion", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.diffusion = std::stof(value); }},
        {"reverbDensity", +[](SceneObject& obj, const std::string& value) { obj.reverbZone.density = std::stof(value); }},
        {"hasGroundBakedType", +[](SceneObject& obj, const std::string& value) { obj.hasGroundBakedType = std::stoi(value) != 0; }},
        {"groundBakedEnabled", +[](SceneObject& obj, const std::string& value) { obj.groundBakedType.enabled = std::stoi(value) != 0; }},
        {"groundBakedInclude", +[](SceneObject& obj, const std::string& value) { obj.groundBakedType.includeInBake = std::stoi(value) != 0; }},
        {"groundBakedAreaCost", +[](SceneObject& obj, const std::string& value) { obj.groundBakedType.areaCost = std::stof(value); }},
        {"hasObsticleObject", +[](SceneObject& obj, const std::string& value) { obj.hasObsticleObject = std::stoi(value) != 0; }},
        {"obsticleEnabled", +[](SceneObject& obj, const std::string& value) { obj.obsticleObject.enabled = std::stoi(value) != 0; }},
        {"obsticleCarve", +[](SceneObject& obj, const std::string& value) { obj.obsticleObject.carve = std::stoi(value) != 0; }},
        {"obsticlePadding", +[](SceneObject& obj, const std::string& value) { obj.obsticleObject.padding = std::stof(value); }},
        {"hasAIAgent", +[](SceneObject& obj, const std::string& value) { obj.hasAIAgent = std::stoi(value) != 0; }},
        {"aiAgentEnabled", +[](SceneObject& obj, const std::string& value) { obj.aiAgent.enabled = std::stoi(value) != 0; }},
        {"aiAgentUseTargetObject", +[](SceneObject& obj, const std::string& value) { obj.aiAgent.useTargetObject = std::stoi(value) != 0; }},
        {"aiAgentTargetId", +[](SceneObject& obj, const std::string& value) { obj.aiAgent.targetId = std::stoi(value); }},
        {"aiAgentDestination", +[](SceneObject& obj, const std::string& value) { ParseVec3(value, obj.aiAgent.destination); }},
        {"aiAgentSpeed", +[](SceneObject& obj, const std::string& value) { obj.aiAgent.speed = std::stof(value); }},
        {"aiAgentStoppingDistance", +[](SceneObject& obj, const std::string& value) { obj.aiAgent.stoppingDistance = std::stof(value); }},
        {"aiAgentRepathInterval", +[](SceneObject& obj, const std::string& value) { obj.aiAgent.repathInterval = std::stof(value); }},
        {"aiAgentAutoRepath", +[](SceneObject& obj, const std::string& value) { obj.aiAgent.autoRepath = std::stoi(value) != 0; }},
        {"aiAgentAlignToPath", +[](SceneObject& obj, const std::string& value) { obj.aiAgent.alignToPath = std::stoi(value) != 0; }},
        {"aiAgentDebugDrawPath", +[](SceneObject& obj, const std::string& value) { obj.aiAgent.debugDrawPath = std::stoi(value) != 0; }},
        {"hasRig25DRoot", +[](SceneObject& obj, const std::string& value) { obj.hasRig25DRoot = std::stoi(value) != 0; }},
        {"rig25dRootEnabled", +[](SceneObject& obj, const std::string& value) { obj.rig25DRoot.enabled = std::stoi(value) != 0; obj.hasRig25DRoot = true; }},
        {"hasRig25DNode", +[](SceneObject& obj, const std::string& value) { obj.hasRig25DNode = std::stoi(value) != 0; }},
        {"rig25dNodeEnabled", +[](SceneObject& obj, const std::string& value) { obj.rig25DNode.enabled = std::stoi(value) != 0; obj.hasRig25DNode = true; }},
        {"rig25dNodeId", +[](SceneObject& obj, const std::string& value) { obj.rig25DNode.nodeId = std::stoi(value); obj.hasRig25DNode = true; }},
        {"rig25dNodeName", +[](SceneObject& obj, const std::string& value) { obj.rig25DNode.nodeName = value; obj.hasRig25DNode = true; }},
        {"hasAnimation", +[](SceneObject& obj, const std::string& value) { obj.hasAnimation = std::stoi(value) != 0; }},
        {"animEnabled", +[](SceneObject& obj, const std::string& value) { obj.hasAnimation = true; obj.animation.enabled = std::stoi(value) != 0; }},
        {"animClipAsset", +[](SceneObject& obj, const std::string& value) { obj.hasAnimation = true; obj.animation.clipAssetPath = value; }},
        {"animClipCount", +[](SceneObject& obj, const std::string& value) {
             obj.hasAnimation = true;
             int count = std::stoi(value);
             obj.animation.clips.resize(std::max(0, count));
         }},
        {"animActiveClipIndex", +[](SceneObject& obj, const std::string& value) {
             obj.hasAnimation = true;
             obj.animation.activeClipIndex = std::stoi(value);
         }},
        {"animClipLength", +[](SceneObject& obj, const std::string& value) { obj.hasAnimation = true; obj.animation.clipLength = std::stof(value); }},
        {"animPlaySpeed", +[](SceneObject& obj, const std::string& value) { obj.hasAnimation = true; obj.animation.playSpeed = std::stof(value); }},
        {"animLoop", +[](SceneObject& obj, const std::string& value) { obj.hasAnimation = true; obj.animation.loop = std::stoi(value) != 0; }},
        {"animPlayOnAwake", +[](SceneObject& obj, const std::string& value) { obj.hasAnimation = true; obj.animation.playOnAwake = std::stoi(value) != 0; }},
        {"animApplyOnScrub", +[](SceneObject& obj, const std::string& value) { obj.hasAnimation = true; obj.animation.applyOnScrub = std::stoi(value) != 0; }},
        {"animKeyCount", +[](SceneObject& obj, const std::string& value) {
             obj.hasAnimation = true;
             int count = std::stoi(value);
             obj.animation.keyframes.resize(std::max(0, count));
         }},
        {"animEventCount", +[](SceneObject& obj, const std::string& value) {
             obj.hasAnimation = true;
             int count = std::stoi(value);
             obj.animation.events.resize(std::max(0, count));
         }},
        {"animTrackCount", +[](SceneObject& obj, const std::string& value) {
             obj.hasAnimation = true;
             int count = std::stoi(value);
             obj.animation.tracks.resize(std::max(0, count));
         }},
        {"hasSkeletalAnimation", +[](SceneObject& obj, const std::string& value) { obj.hasSkeletalAnimation = std::stoi(value) != 0; }},
        {"skelEnabled", +[](SceneObject& obj, const std::string& value) { obj.skeletal.enabled = std::stoi(value) != 0; }},
        {"skelUseGpu", +[](SceneObject& obj, const std::string& value) { obj.skeletal.useGpuSkinning = std::stoi(value) != 0; }},
        {"skelAllowCpuFallback", +[](SceneObject& obj, const std::string& value) { obj.skeletal.allowCpuFallback = std::stoi(value) != 0; }},
        {"skelUseAnimation", +[](SceneObject& obj, const std::string& value) { obj.skeletal.useAnimation = std::stoi(value) != 0; }},
        {"skelClipIndex", +[](SceneObject& obj, const std::string& value) { obj.skeletal.clipIndex = std::stoi(value); }},
        {"skelPlaySpeed", +[](SceneObject& obj, const std::string& value) { obj.skeletal.playSpeed = std::stof(value); }},
        {"skelLoop", +[](SceneObject& obj, const std::string& value) { obj.skeletal.loop = std::stoi(value) != 0; }},
        {"skelMaxBones", +[](SceneObject& obj, const std::string& value) { obj.skeletal.maxBones = std::stoi(value); }},
        {"materialColor", +[](SceneObject& obj, const std::string& value) { ParseVec3(value, obj.material.color); }},
        {"materialAlpha", +[](SceneObject& obj, const std::string& value) { obj.material.alpha = std::clamp(std::stof(value), 0.0f, 1.0f); }},
        {"materialAmbient", +[](SceneObject& obj, const std::string& value) { obj.material.ambientStrength = std::stof(value); }},
        {"materialSpecular", +[](SceneObject& obj, const std::string& value) { obj.material.specularStrength = std::stof(value); }},
        {"materialShininess", +[](SceneObject& obj, const std::string& value) { obj.material.shininess = std::stof(value); }},
        {"materialNormalMapIntensity", +[](SceneObject& obj, const std::string& value) { obj.material.normalMapIntensity = std::clamp(std::stof(value), 0.0f, 2.0f); }},
        {"materialTextureMix", +[](SceneObject& obj, const std::string& value) { obj.material.textureMix = std::stof(value); }},
        {"materialUvTiling", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.material.uvTiling); }},
        {"materialUvOffset", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.material.uvOffset); }},
        {"materialTextureFilter", +[](SceneObject& obj, const std::string& value) {
             int filterValue = std::stoi(value);
             obj.material.textureFilter = (filterValue == 1)
                 ? MaterialProperties::TextureFilter::Point
                 : MaterialProperties::TextureFilter::Bilinear;
         }},
        {"materialPath", +[](SceneObject& obj, const std::string& value) { obj.materialPath = value; }},
        {"albedoTex", +[](SceneObject& obj, const std::string& value) { obj.albedoTexturePath = value; }},
        {"overlayTex", +[](SceneObject& obj, const std::string& value) { obj.overlayTexturePath = value; }},
        {"normalMap", +[](SceneObject& obj, const std::string& value) { obj.normalMapPath = value; }},
        {"shaderPack", +[](SceneObject& obj, const std::string& value) { obj.shaderPackPath = value; }},
        {"vertexShader", +[](SceneObject& obj, const std::string& value) { obj.vertexShaderPath = value; }},
        {"fragmentShader", +[](SceneObject& obj, const std::string& value) { obj.fragmentShaderPath = value; }},
        {"useOverlay", +[](SceneObject& obj, const std::string& value) { obj.useOverlay = (std::stoi(value) != 0); }},
        {"additionalMaterialCount", +[](SceneObject& obj, const std::string& value) {
             int count = std::stoi(value);
             obj.additionalMaterialPaths.resize(std::max(0, count));
         }},
        {"nextInspectorScriptId", +[](SceneObject& obj, const std::string& value) {
             obj.nextInspectorScriptId = std::max(1, std::stoi(value));
         }},
        {"componentOrder", +[](SceneObject& obj, const std::string& value) {
             obj.inspectorComponentOrder.clear();
             std::stringstream ss(value);
             std::string item;
             while (std::getline(ss, item, ';')) {
                 if (!item.empty()) {
                     obj.inspectorComponentOrder.push_back(item);
                 }
             }
         }},
        {"scripts", +[](SceneObject& obj, const std::string& value) {
             int count = std::stoi(value);
             obj.scripts.resize(std::max(0, count));
         }},
        {"scriptCount", +[](SceneObject& obj, const std::string& value) {
             int count = std::stoi(value);
             obj.scripts.resize(std::max(0, count));
         }},
        {"lightColor", +[](SceneObject& obj, const std::string& value) { ParseVec3(value, obj.light.color); }},
        {"lightType", +[](SceneObject& obj, const std::string& value) {
             obj.light.type = static_cast<LightType>(std::stoi(value));
         }},
        {"lightIntensity", +[](SceneObject& obj, const std::string& value) { obj.light.intensity = std::stof(value); }},
        {"lightRange", +[](SceneObject& obj, const std::string& value) { obj.light.range = std::stof(value); }},
        {"lightEdgeFade", +[](SceneObject& obj, const std::string& value) { obj.light.edgeFade = std::stof(value); }},
        {"lightInner", +[](SceneObject& obj, const std::string& value) { obj.light.innerAngle = std::stof(value); }},
        {"lightOuter", +[](SceneObject& obj, const std::string& value) { obj.light.outerAngle = std::stof(value); }},
        {"lightSize", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.light.size); }},
        {"lightCastShadows", +[](SceneObject& obj, const std::string& value) { obj.light.castShadows = (std::stoi(value) != 0); }},
        {"lightSoftShadows", +[](SceneObject& obj, const std::string& value) { obj.light.softShadows = (std::stoi(value) != 0); }},
        {"lightShadowBias", +[](SceneObject& obj, const std::string& value) { obj.light.shadowBias = std::stof(value); }},
        {"lightShadowSoftness", +[](SceneObject& obj, const std::string& value) { obj.light.shadowSoftness = std::stof(value); }},
        {"lightShadowResolution", +[](SceneObject& obj, const std::string& value) { obj.light.shadowResolution = std::clamp(std::stoi(value), 0, 8192); }},
        {"lightEnabled", +[](SceneObject& obj, const std::string& value) { obj.light.enabled = (std::stoi(value) != 0); }},
        {"reflectionCastEnabled", +[](SceneObject& obj, const std::string& value) { obj.hasReflectionCast = true; obj.reflectionCast.enabled = std::stoi(value) != 0; }},
        {"reflectionCastUpdateMode", +[](SceneObject& obj, const std::string& value) {
             obj.hasReflectionCast = true;
             obj.reflectionCast.updateMode = std::stoi(value) == 0
                 ? ReflectionCastUpdateMode::EveryFrame
                 : ReflectionCastUpdateMode::FirstFrame;
         }},
        {"reflectionCastBox", +[](SceneObject& obj, const std::string& value) { obj.hasReflectionCast = true; ParseVec3(value, obj.reflectionCast.boxSize); }},
        {"reflectionCastBlend", +[](SceneObject& obj, const std::string& value) { obj.hasReflectionCast = true; obj.reflectionCast.blendDistance = std::max(0.0f, std::stof(value)); }},
        {"reflectionCastIntensity", +[](SceneObject& obj, const std::string& value) { obj.hasReflectionCast = true; obj.reflectionCast.intensity = std::clamp(std::stof(value), 0.0f, 2.0f); }},
        {"reflectionCastResolution", +[](SceneObject& obj, const std::string& value) { obj.hasReflectionCast = true; obj.reflectionCast.resolution = std::clamp(std::stoi(value), 32, 1024); }},
        {"light2dEnabled", +[](SceneObject& obj, const std::string& value) { obj.light2D.enabled = (std::stoi(value) != 0); obj.hasLight2D = true; }},
        {"light2dType", +[](SceneObject& obj, const std::string& value) { obj.light2D.type = static_cast<Light2DType>(std::stoi(value)); obj.hasLight2D = true; }},
        {"light2dColor", +[](SceneObject& obj, const std::string& value) { ParseVec4(value, obj.light2D.color); obj.hasLight2D = true; }},
        {"light2dIntensity", +[](SceneObject& obj, const std::string& value) { obj.light2D.intensity = std::stof(value); obj.hasLight2D = true; }},
        {"light2dRadius", +[](SceneObject& obj, const std::string& value) { obj.light2D.radius = std::max(0.0f, std::stof(value)); obj.hasLight2D = true; }},
        {"light2dInnerRadius", +[](SceneObject& obj, const std::string& value) { obj.light2D.innerRadius = std::max(0.0f, std::stof(value)); obj.hasLight2D = true; }},
        {"light2dOuterRadius", +[](SceneObject& obj, const std::string& value) { obj.light2D.outerRadius = std::max(0.0f, std::stof(value)); obj.hasLight2D = true; }},
        {"light2dFalloffStrength", +[](SceneObject& obj, const std::string& value) { obj.light2D.falloffStrength = std::max(0.0f, std::stof(value)); obj.hasLight2D = true; }},
        {"light2dInnerSpotAngle", +[](SceneObject& obj, const std::string& value) { obj.light2D.innerSpotAngle = std::clamp(std::stof(value), 0.0f, 360.0f); obj.hasLight2D = true; }},
        {"light2dOuterSpotAngle", +[](SceneObject& obj, const std::string& value) { obj.light2D.outerSpotAngle = std::clamp(std::stof(value), 0.0f, 360.0f); obj.hasLight2D = true; }},
        {"light2dBlendStyle", +[](SceneObject& obj, const std::string& value) { obj.light2D.blendStyle = std::stoi(value); obj.hasLight2D = true; }},
        {"light2dOrder", +[](SceneObject& obj, const std::string& value) { obj.light2D.lightOrder = std::stoi(value); obj.hasLight2D = true; }},
        {"light2dOverlap", +[](SceneObject& obj, const std::string& value) { obj.light2D.overlapOperation = static_cast<Light2DOverlapOperation>(std::stoi(value)); obj.hasLight2D = true; }},
        {"light2dShadowStrength", +[](SceneObject& obj, const std::string& value) { obj.light2D.shadowStrength = std::clamp(std::stof(value), 0.0f, 1.0f); obj.hasLight2D = true; }},
        {"light2dVolumetric", +[](SceneObject& obj, const std::string& value) { obj.light2D.volumetricEnabled = (std::stoi(value) != 0); obj.hasLight2D = true; }},
        {"light2dCastShadows", +[](SceneObject& obj, const std::string& value) { obj.light2D.castsShadows = (std::stoi(value) != 0); obj.hasLight2D = true; }},
        {"light2dTargetAllLayers", +[](SceneObject& obj, const std::string& value) { obj.light2D.targetAllLayers = (std::stoi(value) != 0); obj.hasLight2D = true; }},
        {"light2dTargetLayerMask", +[](SceneObject& obj, const std::string& value) { obj.light2D.targetLayerMask = static_cast<uint32_t>(std::stoul(value)); obj.hasLight2D = true; }},
        {"light2dNormalQuality", +[](SceneObject& obj, const std::string& value) { obj.light2D.normalMapQuality = static_cast<Light2DNormalMapQuality>(std::stoi(value)); obj.hasLight2D = true; }},
        {"light2dNormalDistance", +[](SceneObject& obj, const std::string& value) { obj.light2D.normalMapDistance = std::max(0.0f, std::stof(value)); obj.hasLight2D = true; }},
        {"light2dUseDistanceExponent", +[](SceneObject& obj, const std::string& value) { obj.light2D.useDistanceExponent = (std::stoi(value) != 0); obj.hasLight2D = true; }},
        {"light2dDistanceExponent", +[](SceneObject& obj, const std::string& value) { obj.light2D.distanceExponent = std::max(0.01f, std::stof(value)); obj.hasLight2D = true; }},
        {"light2dCookie", +[](SceneObject& obj, const std::string& value) { obj.light2D.cookieTexturePath = value; obj.hasLight2D = true; }},
        {"light2dCookieScale", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.light2D.cookieScale); obj.hasLight2D = true; }},
        {"light2dCookieRotation", +[](SceneObject& obj, const std::string& value) { obj.light2D.cookieRotation = std::stof(value); obj.hasLight2D = true; }},
        {"light2dFreeformFeather", +[](SceneObject& obj, const std::string& value) { obj.light2D.freeformFeather = std::max(0.0f, std::stof(value)); obj.hasLight2D = true; }},
        {"light2dFreeformEdgeFalloff", +[](SceneObject& obj, const std::string& value) { obj.light2D.freeformEdgeFalloff = std::max(0.01f, std::stof(value)); obj.hasLight2D = true; }},
        {"light2dFlickerEnabled", +[](SceneObject& obj, const std::string& value) { obj.light2D.flicker.enabled = (std::stoi(value) != 0); obj.hasLight2D = true; }},
        {"light2dFlickerSpeed", +[](SceneObject& obj, const std::string& value) { obj.light2D.flicker.speed = std::max(0.01f, std::stof(value)); obj.hasLight2D = true; }},
        {"light2dFlickerAmount", +[](SceneObject& obj, const std::string& value) { obj.light2D.flicker.amount = std::clamp(std::stof(value), 0.0f, 1.0f); obj.hasLight2D = true; }},
        {"light2dFlickerSeed", +[](SceneObject& obj, const std::string& value) { obj.light2D.flicker.seed = std::stof(value); obj.hasLight2D = true; }},
        {"light2dShape", +[](SceneObject& obj, const std::string& value) { ParseVec2List(value, obj.light2D.shapePoints); obj.hasLight2D = true; }},
        {"shadowCaster2dEnabled", +[](SceneObject& obj, const std::string& value) { obj.shadowCaster2D.enabled = (std::stoi(value) != 0); obj.hasShadowCaster2D = true; }},
        {"shadowCaster2dSelfShadow", +[](SceneObject& obj, const std::string& value) { obj.shadowCaster2D.castsSelfShadow = (std::stoi(value) != 0); obj.hasShadowCaster2D = true; }},
        {"shadowCaster2dTargetAllLayers", +[](SceneObject& obj, const std::string& value) { obj.shadowCaster2D.targetAllLayers = (std::stoi(value) != 0); obj.hasShadowCaster2D = true; }},
        {"shadowCaster2dTargetLayerMask", +[](SceneObject& obj, const std::string& value) { obj.shadowCaster2D.targetLayerMask = static_cast<uint32_t>(std::stoul(value)); obj.hasShadowCaster2D = true; }},
        {"shadowCaster2dStrength", +[](SceneObject& obj, const std::string& value) { obj.shadowCaster2D.shadowStrength = std::clamp(std::stof(value), 0.0f, 1.0f); obj.hasShadowCaster2D = true; }},
        {"shadowCaster2dShape", +[](SceneObject& obj, const std::string& value) { ParseVec2List(value, obj.shadowCaster2D.points); obj.hasShadowCaster2D = true; }},
        {"cameraType", +[](SceneObject& obj, const std::string& value) { obj.camera.type = static_cast<SceneCameraType>(std::stoi(value)); }},
        {"cameraFov", +[](SceneObject& obj, const std::string& value) { obj.camera.fov = std::stof(value); }},
        {"cameraNear", +[](SceneObject& obj, const std::string& value) { obj.camera.nearClip = std::stof(value); }},
        {"cameraFar", +[](SceneObject& obj, const std::string& value) { obj.camera.farClip = std::stof(value); }},
        {"cameraPostFX", +[](SceneObject& obj, const std::string& value) { obj.camera.applyPostFX = (std::stoi(value) != 0); }},
        {"cameraUse2D", +[](SceneObject& obj, const std::string& value) { obj.camera.use2D = (std::stoi(value) != 0); }},
        {"cameraPixelsPerUnit", +[](SceneObject& obj, const std::string& value) { obj.camera.pixelsPerUnit = std::stof(value); }},
        {"uiAnchor", +[](SceneObject& obj, const std::string& value) { obj.ui.anchor = static_cast<UIAnchor>(std::stoi(value)); }},
        {"uiPosition", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.ui.position); }},
        {"uiRotation", +[](SceneObject& obj, const std::string& value) { obj.ui.rotation = std::stof(value); }},
        {"uiSize", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.ui.size); }},
        {"uiMaskChildren", +[](SceneObject& obj, const std::string& value) { obj.ui.maskChildren = (std::stoi(value) != 0); }},
        {"uiSliderValue", +[](SceneObject& obj, const std::string& value) { obj.ui.sliderValue = std::stof(value); }},
        {"uiSliderMin", +[](SceneObject& obj, const std::string& value) { obj.ui.sliderMin = std::stof(value); }},
        {"uiSliderMax", +[](SceneObject& obj, const std::string& value) { obj.ui.sliderMax = std::stof(value); }},
        {"uiLabel", +[](SceneObject& obj, const std::string& value) { obj.ui.label = value; }},
        {"uiColor", +[](SceneObject& obj, const std::string& value) { ParseVec4(value, obj.ui.color); }},
        {"uiInteractable", +[](SceneObject& obj, const std::string& value) { obj.ui.interactable = (std::stoi(value) != 0); }},
        {"uiSliderStyle", +[](SceneObject& obj, const std::string& value) { obj.ui.sliderStyle = static_cast<UISliderStyle>(std::stoi(value)); }},
        {"uiButtonStyle", +[](SceneObject& obj, const std::string& value) { obj.ui.buttonStyle = static_cast<UIButtonStyle>(std::stoi(value)); }},
        {"uiStylePreset", +[](SceneObject& obj, const std::string& value) { obj.ui.stylePreset = value; }},
        {"uiTextScale", +[](SceneObject& obj, const std::string& value) { obj.ui.textScale = std::stof(value); }},
        {"uiTextFont", +[](SceneObject& obj, const std::string& value) { obj.ui.textFont = value; }},
        {"uiTextWrap", +[](SceneObject& obj, const std::string& value) { obj.ui.textAutoWrap = (std::stoi(value) != 0); }},
        {"uiTextHAlign", +[](SceneObject& obj, const std::string& value) {
             obj.ui.textHAlign = static_cast<UITextHAlign>(std::clamp(std::stoi(value), 0, 2));
         }},
        {"uiTextVAlign", +[](SceneObject& obj, const std::string& value) {
             obj.ui.textVAlign = static_cast<UITextVAlign>(std::clamp(std::stoi(value), 0, 2));
         }},
        {"uiTextEffectFlags", +[](SceneObject& obj, const std::string& value) { obj.ui.textEffectFlags = std::stoi(value); }},
        {"uiTextEffectSpeed", +[](SceneObject& obj, const std::string& value) { obj.ui.textEffectSpeed = std::max(0.01f, std::stof(value)); }},
        {"uiTextEffectIntensity", +[](SceneObject& obj, const std::string& value) { obj.ui.textEffectIntensity = std::max(0.0f, std::stof(value)); }},
        {"uiRenderIn3D", +[](SceneObject& obj, const std::string& value) { obj.ui.renderIn3D = (std::stoi(value) != 0); }},
        {"uiRenderTargetSize", +[](SceneObject& obj, const std::string& value) { ParseIVec2(value, obj.ui.renderTargetSize); }},
        {"uiPseudo3DEnabled", +[](SceneObject& obj, const std::string& value) { obj.ui.pseudo3DEnabled = (std::stoi(value) != 0); }},
        {"uiPseudo3DUseOffscreen", +[](SceneObject& obj, const std::string& value) { obj.ui.pseudo3DUseOffscreenSurface = (std::stoi(value) != 0); }},
        {"uiPseudo3DPanelSize", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.ui.pseudo3DPanelSize); }},
        {"uiPseudo3DTopLeftOffset", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.ui.pseudo3DTopLeftOffset); }},
        {"uiPseudo3DTopRightOffset", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.ui.pseudo3DTopRightOffset); }},
        {"uiPseudo3DBottomRightOffset", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.ui.pseudo3DBottomRightOffset); }},
        {"uiPseudo3DBottomLeftOffset", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.ui.pseudo3DBottomLeftOffset); }},
        {"uiPseudo3DPivot", +[](SceneObject& obj, const std::string& value) {
             ParseVec2(value, obj.ui.pseudo3DPivot);
             obj.ui.pseudo3DPivot.x = std::clamp(obj.ui.pseudo3DPivot.x, 0.0f, 1.0f);
             obj.ui.pseudo3DPivot.y = std::clamp(obj.ui.pseudo3DPivot.y, 0.0f, 1.0f);
         }},
        {"uiPseudo3DPerspectiveIntensity", +[](SceneObject& obj, const std::string& value) { obj.ui.pseudo3DPerspectiveIntensity = std::stof(value); }},
        {"uiPseudo3DSkewAmount", +[](SceneObject& obj, const std::string& value) { obj.ui.pseudo3DSkewAmount = std::stof(value); }},
        {"uiPseudo3DCurvatureAmount", +[](SceneObject& obj, const std::string& value) { obj.ui.pseudo3DCurvatureAmount = std::stof(value); }},
        {"uiPseudo3DAnchorTargetId", +[](SceneObject& obj, const std::string& value) { obj.ui.pseudo3DAnchorTargetId = std::stoi(value); }},
        {"uiPseudo3DDistanceScaling", +[](SceneObject& obj, const std::string& value) { obj.ui.pseudo3DDistanceScalingEnabled = (std::stoi(value) != 0); }},
        {"uiPseudo3DAdjustPerspectiveDistance", +[](SceneObject& obj, const std::string& value) { obj.ui.pseudo3DAdjustPerspectiveWithDistance = (std::stoi(value) != 0); }},
        {"uiPseudo3DMinDistance", +[](SceneObject& obj, const std::string& value) { obj.ui.pseudo3DMinDistance = std::max(0.01f, std::stof(value)); }},
        {"uiPseudo3DMaxDistance", +[](SceneObject& obj, const std::string& value) { obj.ui.pseudo3DMaxDistance = std::max(0.02f, std::stof(value)); }},
        {"uiPseudo3DInteractionDistance", +[](SceneObject& obj, const std::string& value) { obj.ui.pseudo3DInteractionDistance = std::max(0.0f, std::stof(value)); }},
        {"uiPseudo3DDepthSort", +[](SceneObject& obj, const std::string& value) { obj.ui.pseudo3DDepthSort = std::stoi(value); }},
        {"uiPseudo3DAllowInteraction", +[](SceneObject& obj, const std::string& value) { obj.ui.pseudo3DAllowInteraction = (std::stoi(value) != 0); }},
        {"uiSpriteSheetEnabled", +[](SceneObject& obj, const std::string& value) { obj.ui.spriteSheetEnabled = (std::stoi(value) != 0); }},
        {"uiSpriteSheetGrid", +[](SceneObject& obj, const std::string& value) {
             glm::ivec2 grid(1, 1);
             ParseIVec2(value, grid);
             obj.ui.spriteSheetColumns = std::max(1, grid.x);
             obj.ui.spriteSheetRows = std::max(1, grid.y);
         }},
        {"uiSpriteSheetFrame", +[](SceneObject& obj, const std::string& value) { obj.ui.spriteSheetFrame = std::max(0, std::stoi(value)); }},
        {"uiSpriteSheetFps", +[](SceneObject& obj, const std::string& value) { obj.ui.spriteSheetFps = std::max(1.0f, std::stof(value)); }},
        {"uiSpriteSheetLoop", +[](SceneObject& obj, const std::string& value) { obj.ui.spriteSheetLoop = (std::stoi(value) != 0); }},
        {"uiSpriteCustomFramesEnabled", +[](SceneObject& obj, const std::string& value) { obj.ui.spriteCustomFramesEnabled = (std::stoi(value) != 0); }},
        {"uiSpriteSourceSize", +[](SceneObject& obj, const std::string& value) {
             glm::ivec2 size(0, 0);
             ParseIVec2(value, size);
             obj.ui.spriteSourceWidth = std::max(0, size.x);
             obj.ui.spriteSourceHeight = std::max(0, size.y);
         }},
        {"uiNineSliceEnabled", +[](SceneObject& obj, const std::string& value) {
             obj.ui.nineSliceEnabled = (std::stoi(value) != 0);
         }},
        {"uiNineSliceBorder", +[](SceneObject& obj, const std::string& value) {
             ParseVec4(value, obj.ui.nineSliceBorder);
             obj.ui.nineSliceBorder.x = std::max(0.0f, obj.ui.nineSliceBorder.x);
             obj.ui.nineSliceBorder.y = std::max(0.0f, obj.ui.nineSliceBorder.y);
             obj.ui.nineSliceBorder.z = std::max(0.0f, obj.ui.nineSliceBorder.z);
             obj.ui.nineSliceBorder.w = std::max(0.0f, obj.ui.nineSliceBorder.w);
         }},
        {"uiNineSliceTileEdges", +[](SceneObject& obj, const std::string& value) {
             obj.ui.nineSliceTileEdges = (std::stoi(value) != 0);
         }},
        {"uiNineSliceTileCenter", +[](SceneObject& obj, const std::string& value) {
             obj.ui.nineSliceTileCenter = (std::stoi(value) != 0);
         }},
        {"uiReceiveLighting2D", +[](SceneObject& obj, const std::string& value) {
             obj.ui.receiveLighting2D = (std::stoi(value) != 0);
         }},
        {"uiUnlitLighting2D", +[](SceneObject& obj, const std::string& value) {
             obj.ui.unlitLighting2D = (std::stoi(value) != 0);
         }},
        {"uiEmissiveLighting2D", +[](SceneObject& obj, const std::string& value) {
             obj.ui.emissiveLighting2D = std::max(0.0f, std::stof(value));
         }},
        {"uiFillColor", +[](SceneObject& obj, const std::string& value) { ParseVec4(value, obj.ui.fillColor); }},
        {"uiBackgroundColor", +[](SceneObject& obj, const std::string& value) { ParseVec4(value, obj.ui.backgroundColor); }},
        {"uiBorderColor", +[](SceneObject& obj, const std::string& value) { ParseVec4(value, obj.ui.borderColor); }},
        {"uiTextColor", +[](SceneObject& obj, const std::string& value) { ParseVec4(value, obj.ui.textColor); }},
        {"uiFontSize", +[](SceneObject& obj, const std::string& value) { obj.ui.fontSize = std::max(0.0f, std::stof(value)); }},
        {"uiSortingOrder", +[](SceneObject& obj, const std::string& value) { obj.ui.sortingOrder = std::stoi(value); }},
        {"uiSpriteCustomFrames", +[](SceneObject& obj, const std::string& value) {
             obj.ui.spriteCustomFrames.clear();
             std::stringstream ss(value);
             std::string item;
             while (std::getline(ss, item, ';')) {
                 if (item.empty()) continue;
                 glm::ivec4 rect(0);
                 if (std::sscanf(item.c_str(), "%d,%d,%d,%d", &rect.x, &rect.y, &rect.z, &rect.w) == 4) {
                     rect.z = std::max(1, rect.z);
                     rect.w = std::max(1, rect.w);
                     obj.ui.spriteCustomFrames.push_back(rect);
                 }
             }
         }},
        {"uiSpriteCustomFrameNames", +[](SceneObject& obj, const std::string& value) {
             obj.ui.spriteCustomFrameNames.clear();
             std::stringstream ss(value);
             std::string item;
             while (std::getline(ss, item, ';')) {
                 obj.ui.spriteCustomFrameNames.push_back(item);
             }
         }},
        {"uiSpriteCustomFrameScales", +[](SceneObject& obj, const std::string& value) {
             ParseVec2List(value, obj.ui.spriteCustomFrameScales);
             if (obj.ui.spriteCustomFrameScales.size() < obj.ui.spriteCustomFrames.size()) {
                 obj.ui.spriteCustomFrameScales.resize(obj.ui.spriteCustomFrames.size(), glm::vec2(1.0f));
             } else if (obj.ui.spriteCustomFrameScales.size() > obj.ui.spriteCustomFrames.size()) {
                 obj.ui.spriteCustomFrameScales.resize(obj.ui.spriteCustomFrames.size());
             }
             for (glm::vec2& scale : obj.ui.spriteCustomFrameScales) {
                 scale.x = std::max(0.01f, scale.x);
                 scale.y = std::max(0.01f, scale.y);
             }
         }},
        {"postEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.enabled = (std::stoi(value) != 0); }},
        {"postVolumeGlobal", +[](SceneObject& obj, const std::string& value) { obj.postFx.isGlobal = (std::stoi(value) != 0); }},
        {"postVolumePriority", +[](SceneObject& obj, const std::string& value) { obj.postFx.priority = std::stof(value); }},
        {"postVolumeWeight", +[](SceneObject& obj, const std::string& value) { obj.postFx.blendWeight = std::stof(value); }},
        {"postVolumeBlendRadius", +[](SceneObject& obj, const std::string& value) { obj.postFx.blendRadius = std::stof(value); }},
        {"postHDREnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.hdrEnabled = (std::stoi(value) != 0); }},
        {"postToneMapper", +[](SceneObject& obj, const std::string& value) { obj.postFx.toneMapper = static_cast<PostFXToneMapper>(std::stoi(value)); }},
        {"postWhitePoint", +[](SceneObject& obj, const std::string& value) { obj.postFx.whitePoint = std::stof(value); }},
        {"postGamma", +[](SceneObject& obj, const std::string& value) { obj.postFx.gamma = std::stof(value); }},
        {"postBloomEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.bloomEnabled = (std::stoi(value) != 0); }},
        {"postBloomThreshold", +[](SceneObject& obj, const std::string& value) { obj.postFx.bloomThreshold = std::stof(value); }},
        {"postBloomSoftKnee", +[](SceneObject& obj, const std::string& value) { obj.postFx.bloomSoftKnee = std::stof(value); }},
        {"postBloomIntensity", +[](SceneObject& obj, const std::string& value) { obj.postFx.bloomIntensity = std::stof(value); }},
        {"postBloomRadius", +[](SceneObject& obj, const std::string& value) { obj.postFx.bloomRadius = std::stof(value); }},
        {"postColorAdjustEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.colorAdjustEnabled = (std::stoi(value) != 0); }},
        {"postExposure", +[](SceneObject& obj, const std::string& value) { obj.postFx.exposure = std::stof(value); }},
        {"postContrast", +[](SceneObject& obj, const std::string& value) { obj.postFx.contrast = std::stof(value); }},
        {"postSaturation", +[](SceneObject& obj, const std::string& value) { obj.postFx.saturation = std::stof(value); }},
        {"postColorFilter", +[](SceneObject& obj, const std::string& value) { ParseVec3(value, obj.postFx.colorFilter); }},
        {"postMotionBlurEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.motionBlurEnabled = (std::stoi(value) != 0); }},
        {"postMotionBlurStrength", +[](SceneObject& obj, const std::string& value) { obj.postFx.motionBlurStrength = std::stof(value); }},
        {"postMotionBlurThreshold", +[](SceneObject& obj, const std::string& value) { obj.postFx.motionBlurThreshold = std::stof(value); }},
        {"postMotionBlurClamp", +[](SceneObject& obj, const std::string& value) { obj.postFx.motionBlurClamp = std::stof(value); }},
        {"postVignetteEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.vignetteEnabled = (std::stoi(value) != 0); }},
        {"postVignetteIntensity", +[](SceneObject& obj, const std::string& value) { obj.postFx.vignetteIntensity = std::stof(value); }},
        {"postVignetteSmoothness", +[](SceneObject& obj, const std::string& value) { obj.postFx.vignetteSmoothness = std::stof(value); }},
        {"postChromaticEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.chromaticAberrationEnabled = (std::stoi(value) != 0); }},
        {"postChromaticAmount", +[](SceneObject& obj, const std::string& value) { obj.postFx.chromaticAmount = std::stof(value); }},
        {"postSharpenEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.sharpenEnabled = (std::stoi(value) != 0); }},
        {"postSharpenStrength", +[](SceneObject& obj, const std::string& value) { obj.postFx.sharpenStrength = std::stof(value); }},
        {"postAOEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.ambientOcclusionEnabled = (std::stoi(value) != 0); }},
        {"postAORadius", +[](SceneObject& obj, const std::string& value) { obj.postFx.aoRadius = std::stof(value); }},
        {"postAOStrength", +[](SceneObject& obj, const std::string& value) { obj.postFx.aoStrength = std::stof(value); }},
        {"postDitherEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.ditherEnabled = (std::stoi(value) != 0); }},
        {"postDitherIntensity", +[](SceneObject& obj, const std::string& value) { obj.postFx.ditherIntensity = std::stof(value); }},
        {"postDitherColorBits", +[](SceneObject& obj, const std::string& value) { obj.postFx.ditherColorBits = std::stoi(value); }},
        {"postDitherDarkAdjustment", +[](SceneObject& obj, const std::string& value) { obj.postFx.ditherDarkAdjustment = std::stof(value); }},
        {"postDitherPixelation", +[](SceneObject& obj, const std::string& value) { obj.postFx.ditherPixelation = std::stof(value); }},
        {"postDitherSize", +[](SceneObject& obj, const std::string& value) { obj.postFx.ditherSize = std::stof(value); }},
        {"postDitherContrast", +[](SceneObject& obj, const std::string& value) { obj.postFx.ditherContrast = std::stof(value); }},
        {"postDitherOffset", +[](SceneObject& obj, const std::string& value) { obj.postFx.ditherOffset = std::stof(value); }},
        {"postDitherPalette", +[](SceneObject& obj, const std::string& value) { obj.postFx.ditherPalette = static_cast<PostFXDitherPalette>(std::clamp(std::stoi(value), 0, 4)); }},
        {"postDitherPattern", +[](SceneObject& obj, const std::string& value) { obj.postFx.ditherPattern = static_cast<PostFXDitherPattern>(std::clamp(std::stoi(value), 0, 4)); }},
        {"postStaticEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.staticEnabled = (std::stoi(value) != 0); }},
        {"postStaticIntensity", +[](SceneObject& obj, const std::string& value) { obj.postFx.staticIntensity = std::stof(value); }},
        {"postStaticGrainScale", +[](SceneObject& obj, const std::string& value) { obj.postFx.staticGrainScale = std::stof(value); }},
        {"postStaticDarkAreaInfluence", +[](SceneObject& obj, const std::string& value) { obj.postFx.staticDarkAreaInfluence = std::stof(value); }},
        {"postStaticSpeed", +[](SceneObject& obj, const std::string& value) { obj.postFx.staticSpeed = std::stof(value); }},
        {"postStaticDistortionEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.staticDistortionEnabled = (std::stoi(value) != 0); }},
        {"postStaticDistortionHorizontalJitterAmount", +[](SceneObject& obj, const std::string& value) { obj.postFx.staticDistortionHorizontalJitterAmount = std::stof(value); }},
        {"postStaticDistortionLineDensity", +[](SceneObject& obj, const std::string& value) { obj.postFx.staticDistortionLineDensity = std::stof(value); }},
        {"postStaticDistortionGlitchFrequency", +[](SceneObject& obj, const std::string& value) { obj.postFx.staticDistortionGlitchFrequency = std::stof(value); }},
        {"postStaticDistortionStrength", +[](SceneObject& obj, const std::string& value) { obj.postFx.staticDistortionStrength = std::stof(value); }},
        {"postLensDistortionEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.lensDistortionEnabled = (std::stoi(value) != 0); }},
        {"postLensDistortionAmount", +[](SceneObject& obj, const std::string& value) { obj.postFx.lensDistortionAmount = std::stof(value); }},
        {"postLensDistortionEdgeFalloff", +[](SceneObject& obj, const std::string& value) { obj.postFx.lensDistortionEdgeFalloff = std::stof(value); }},
        {"postLensDistortionCenterOffset", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.postFx.lensDistortionCenterOffset); }},
        {"postVHSOverlayEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.vhsOverlayEnabled = (std::stoi(value) != 0); }},
        {"postVHSOverlayOpacity", +[](SceneObject& obj, const std::string& value) { obj.postFx.vhsOverlayOpacity = std::stof(value); }},
        {"postVHSOverlayScanlineStrength", +[](SceneObject& obj, const std::string& value) { obj.postFx.vhsOverlayScanlineStrength = std::stof(value); }},
        {"postVHSOverlayTapeNoise", +[](SceneObject& obj, const std::string& value) { obj.postFx.vhsOverlayTapeNoise = std::stof(value); }},
        {"postVHSOverlayChromaBleed", +[](SceneObject& obj, const std::string& value) { obj.postFx.vhsOverlayChromaBleed = std::stof(value); }},
        {"postVHSOverlayBottomNoiseBandHeight", +[](SceneObject& obj, const std::string& value) { obj.postFx.vhsOverlayBottomNoiseBandHeight = std::stof(value); }},
        {"postVHSOverlayBottomNoiseBandIntensity", +[](SceneObject& obj, const std::string& value) { obj.postFx.vhsOverlayBottomNoiseBandIntensity = std::stof(value); }},
        {"postVHSOverlayDistortionStrength", +[](SceneObject& obj, const std::string& value) { obj.postFx.vhsOverlayDistortionStrength = std::stof(value); }},
        {"postVHSOverlayAnimationSpeed", +[](SceneObject& obj, const std::string& value) { obj.postFx.vhsOverlayAnimationSpeed = std::stof(value); }},
        {"postVHSOverlayColorBleed", +[](SceneObject& obj, const std::string& value) { obj.postFx.vhsOverlayColorBleed = std::stof(value); }},
        {"postVHSOverlayBanding", +[](SceneObject& obj, const std::string& value) { obj.postFx.vhsOverlayBanding = std::stof(value); }},
        {"postWavyEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.wavyEnabled = (std::stoi(value) != 0); }},
        {"postWavyAmplitude", +[](SceneObject& obj, const std::string& value) { obj.postFx.wavyAmplitude = std::stof(value); }},
        {"postWavyFrequency", +[](SceneObject& obj, const std::string& value) { obj.postFx.wavyFrequency = std::stof(value); }},
        {"postWavySpeed", +[](SceneObject& obj, const std::string& value) { obj.postFx.wavySpeed = std::stof(value); }},
        {"postWavyVertical", +[](SceneObject& obj, const std::string& value) { obj.postFx.wavyVertical = (std::stoi(value) != 0); }},
        {"meshPath", +[](SceneObject& obj, const std::string& value) {
             obj.meshPath = value;
             if (g_deferSceneAssetLoading) {
                 return;
             }
             if (!value.empty() && obj.hasRenderer && obj.renderType == RenderType::OBJMesh) {
                 std::string err;
                 obj.meshId = g_objLoader.loadOBJ(value, err);
             } else if (!value.empty() && obj.hasRenderer && obj.renderType == RenderType::Model) {
                 ModelSceneData sceneData;
                 std::string err;
                 if (getModelLoader().loadModelScene(value, sceneData, err)) {
                    int sourceIndex = obj.meshSourceIndex;
                     if (sourceIndex < 0 || sourceIndex >= (int)sceneData.meshIndices.size()) {
                         sourceIndex = 0;
                     }
                     if (!sceneData.meshIndices.empty() &&
                         sourceIndex >= 0 && sourceIndex < (int)sceneData.meshIndices.size()) {
                         obj.meshId = sceneData.meshIndices[sourceIndex];
                     }
                     ApplyModelRootTransform(obj, sceneData);
                 } else {
                     std::cerr << "Failed to load model from scene: " << err << std::endl;
                     obj.meshId = -1;
                 }
             }
         }},
        {"meshSourceIndex", +[](SceneObject& obj, const std::string& value) {
             obj.meshSourceIndex = std::stoi(value);
             if (g_deferSceneAssetLoading) {
                 return;
             }
             if (!obj.meshPath.empty() && obj.hasRenderer && obj.renderType == RenderType::Model) {
                 ModelSceneData sceneData;
                 std::string err;
                 if (getModelLoader().loadModelScene(obj.meshPath, sceneData, err)) {
                     int sourceIndex = obj.meshSourceIndex;
                     if (sourceIndex < 0 || sourceIndex >= (int)sceneData.meshIndices.size()) {
                         sourceIndex = 0;
                     }
                     if (!sceneData.meshIndices.empty() &&
                         sourceIndex >= 0 && sourceIndex < (int)sceneData.meshIndices.size()) {
                         obj.meshId = sceneData.meshIndices[sourceIndex];
                     }
                     ApplyModelRootTransform(obj, sceneData);
                 } else {
                     std::cerr << "Failed to load model from scene: " << err << std::endl;
                 }
             }
         }},
        {"children", +[](SceneObject& obj, const std::string& value) {
             if (!value.empty()) {
                 std::stringstream ss(value);
                 std::string item;
                 while (std::getline(ss, item, ',')) {
                     if (!item.empty()) {
                         obj.childIds.push_back(std::stoi(item));
                     }
                 }
             }
         }},
    };
    return handlers;
}
} // namespace

ObjectType GetLegacyTypeFromComponents(const SceneObject& obj) {
    if (obj.type == ObjectType::Sprite25D) {
        return ObjectType::Sprite25D;
    }
    if (obj.type == ObjectType::Light2D && obj.hasLight2D) {
        return ObjectType::Light2D;
    }
    if (obj.type == ObjectType::ShadowCaster2D && obj.hasShadowCaster2D) {
        return ObjectType::ShadowCaster2D;
    }
    if (obj.type == ObjectType::ParticleSystem2D || obj.hasParticleSystem2D) {
        return ObjectType::ParticleSystem2D;
    }
    if (obj.hasRenderer) {
        switch (obj.renderType) {
            case RenderType::Cube: return ObjectType::Cube;
            case RenderType::Sphere: return ObjectType::Sphere;
            case RenderType::Capsule: return ObjectType::Capsule;
            case RenderType::OBJMesh: return ObjectType::OBJMesh;
            case RenderType::Model: return ObjectType::Model;
            case RenderType::Mirror: return ObjectType::Mirror;
            case RenderType::Plane: return ObjectType::Plane;
            case RenderType::Torus: return ObjectType::Torus;
            case RenderType::Sprite: return ObjectType::Sprite;
            case RenderType::None: break;
        }
    }
    if (obj.hasUI) {
        switch (obj.ui.type) {
            case UIElementType::Canvas: return ObjectType::Canvas;
            case UIElementType::Image: return ObjectType::UIImage;
            case UIElementType::Slider: return ObjectType::UISlider;
            case UIElementType::Button: return ObjectType::UIButton;
            case UIElementType::Text: return ObjectType::UIText;
            case UIElementType::Sprite2D:
                return (obj.type == ObjectType::Sprite25D || obj.ui.label == "2.5D Sprite")
                    ? ObjectType::Sprite25D
                    : ObjectType::Sprite2D;
            case UIElementType::None: break;
        }
    }
    if (obj.hasLight) {
        switch (obj.light.type) {
            case LightType::Directional: return ObjectType::DirectionalLight;
            case LightType::Point: return ObjectType::PointLight;
            case LightType::Spot: return ObjectType::SpotLight;
            case LightType::Area: return ObjectType::AreaLight;
        }
    }
    if (obj.hasLight2D) {
        return ObjectType::Light2D;
    }
    if (obj.hasReflectionCast) {
        return ObjectType::ReflectionCast;
    }
    if (obj.hasCamera) {
        return ObjectType::Camera;
    }
    if (obj.hasPostFX) {
        return ObjectType::PostFXNode;
    }
    if (obj.hasShadowCaster2D) {
        return ObjectType::ShadowCaster2D;
    }
    return ObjectType::Empty;
}

bool SceneSerializationInternal::LoadLegacySceneStream(std::istream& file,
                                                       std::vector<SceneObject>& objects,
                                                       int& nextId,
                                                       int& outVersion,
                                                       float* outTimeOfDay,
                                                       SkyboxSettings* outSkyboxSettings,
                                                       bool deferAssetLoading,
                                                       SceneSerializer::Metadata* outMetadata) {
    try {
        objects.clear();
        std::string line;
        SceneObject* currentObj = nullptr;
        int sceneVersion = 20;
        float sceneTimeOfDay = -1.0f;
        SkyboxSettings sceneSkyboxSettings;

        struct DeferGuard {
            bool previous = false;
            explicit DeferGuard(bool enable) {
                previous = g_deferSceneAssetLoading;
                g_deferSceneAssetLoading = enable;
            }
            ~DeferGuard() {
                g_deferSceneAssetLoading = previous;
            }
        } guard(deferAssetLoading);

        while (std::getline(file, line)) {
            size_t first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                continue;
            }
            line.erase(0, first);
            size_t last = line.find_last_not_of(" \t\r\n");
            if (last != std::string::npos) {
                line.erase(last + 1);
            } else {
                continue;
            }

            if (line.empty() || line[0] == '#') continue;

            if (line == "[Object]") {
                objects.push_back(SceneObject("", ObjectType::Empty, 0));
                currentObj = &objects.back();
                continue;
            }

            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;

            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);

            if (key == "version") {
                sceneVersion = std::stoi(value);
            } else if (key == "nextId") {
                nextId = std::stoi(value);
            } else if (key == "timeOfDay") {
                sceneTimeOfDay = std::stof(value);
            } else if (key == "skyboxMode") {
                int mode = std::stoi(value);
                sceneSkyboxSettings.mode = (mode == static_cast<int>(SkyboxMode::Scrolling))
                    ? SkyboxMode::Scrolling
                    : SkyboxMode::Procedural;
            } else if (key == "skyboxSunTexture") {
                sceneSkyboxSettings.sunTexturePath = value;
            } else if (key == "skyboxMoonTexture") {
                sceneSkyboxSettings.moonTexturePath = value;
            } else if (key == "skyboxScrollTexture") {
                sceneSkyboxSettings.scrollingTexturePath = value;
            } else if (key == "skyboxScrollRepeat") {
                size_t commaPos = value.find(',');
                if (commaPos != std::string::npos) {
                    sceneSkyboxSettings.scrollingRepeatX = std::max(0.01f, std::stof(value.substr(0, commaPos)));
                    sceneSkyboxSettings.scrollingRepeatY = std::max(0.01f, std::stof(value.substr(commaPos + 1)));
                }
            } else if (key == "skyboxScrollLookSensitivity") {
                sceneSkyboxSettings.scrollingLookSensitivity = std::max(0.0f, std::stof(value));
            } else if (key == "skyboxScrollVerticalInfluence") {
                sceneSkyboxSettings.scrollingVerticalInfluence = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "skyboxEnvironmentReflections") {
                sceneSkyboxSettings.environmentReflections = std::stoi(value) != 0;
            } else if (key == "skyboxEnvironmentReflectionIntensity") {
                sceneSkyboxSettings.environmentReflectionIntensity = std::clamp(std::stof(value), 0.0f, 2.0f);
            } else if (key == "skyboxReflectionDistanceFadeStart") {
                sceneSkyboxSettings.reflectionDistanceFadeStart = std::max(0.0f, std::stof(value));
            } else if (key == "skyboxReflectionDistanceFadeEnd") {
                sceneSkyboxSettings.reflectionDistanceFadeEnd = std::max(0.01f, std::stof(value));
            } else if (key == "fogEnabled") {
                sceneSkyboxSettings.fogEnabled = std::stoi(value) != 0;
            } else if (key == "fogMode") {
                sceneSkyboxSettings.fogMode = std::clamp(std::stoi(value), 0, 2);
            } else if (key == "fogColor") {
                ParseVec3(value, sceneSkyboxSettings.fogColor);
            } else if (key == "fogStart") {
                sceneSkyboxSettings.fogStart = std::max(0.0f, std::stof(value));
            } else if (key == "fogEnd") {
                sceneSkyboxSettings.fogEnd = std::max(0.01f, std::stof(value));
            } else if (key == "fogDensity") {
                sceneSkyboxSettings.fogDensity = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "fogHeight") {
                sceneSkyboxSettings.fogHeight = std::stof(value);
            } else if (key == "fogHeightFalloff") {
                sceneSkyboxSettings.fogHeightFalloff = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (currentObj) {
                const auto& handlers = GetSceneObjectKeyHandlers();
                auto handlerIt = handlers.find(key);
                if (handlerIt != handlers.end()) {
                    handlerIt->second(*currentObj, value);
                } else if (key.rfind("animClip", 0) == 0) {
                    currentObj->hasAnimation = true;
                    size_t underscore = key.find('_');
                    if (underscore != std::string::npos && underscore > 8) {
                        int clipIdx = std::stoi(key.substr(8, underscore - 8));
                        if (clipIdx >= 0 && clipIdx < static_cast<int>(currentObj->animation.clips.size())) {
                            std::string sub = key.substr(underscore + 1);
                            auto& clip = currentObj->animation.clips[clipIdx];
                            if (sub == "name") {
                                clip.name = value;
                            } else if (sub == "asset") {
                                clip.assetPath = value;
                            }
                        }
                    }
                } else if (key.rfind("animKey", 0) == 0) {
                    currentObj->hasAnimation = true;
                    size_t underscore = key.find('_');
                    if (underscore != std::string::npos && underscore > 7) {
                        int idx = std::stoi(key.substr(7, underscore - 7));
                        if (idx >= 0 && idx < static_cast<int>(currentObj->animation.keyframes.size())) {
                            std::string sub = key.substr(underscore + 1);
                            auto& keyframe = currentObj->animation.keyframes[idx];
                            if (sub == "time") {
                                keyframe.time = std::stof(value);
                            } else if (sub == "pos") {
                                sscanf(value.c_str(), "%f,%f,%f",
                                       &keyframe.position.x,
                                       &keyframe.position.y,
                                       &keyframe.position.z);
                            } else if (sub == "rot") {
                                sscanf(value.c_str(), "%f,%f,%f",
                                       &keyframe.rotation.x,
                                       &keyframe.rotation.y,
                                       &keyframe.rotation.z);
                            } else if (sub == "scale") {
                                sscanf(value.c_str(), "%f,%f,%f",
                                       &keyframe.scale.x,
                                       &keyframe.scale.y,
                                       &keyframe.scale.z);
                            } else if (sub == "interp") {
                                keyframe.interpolation = static_cast<AnimationInterpolation>(std::stoi(value));
                            } else if (sub == "curve") {
                                keyframe.curveMode = static_cast<AnimationCurveMode>(std::stoi(value));
                            } else if (sub == "in") {
                                sscanf(value.c_str(), "%f,%f",
                                       &keyframe.bezierIn.x,
                                       &keyframe.bezierIn.y);
                            } else if (sub == "out") {
                                sscanf(value.c_str(), "%f,%f",
                                       &keyframe.bezierOut.x,
                                       &keyframe.bezierOut.y);
                            }
                        }
                    }
                } else if (key.rfind("animEvent", 0) == 0) {
                    currentObj->hasAnimation = true;
                    size_t underscore = key.find('_');
                    if (underscore != std::string::npos && underscore > 9) {
                        int idx = std::stoi(key.substr(9, underscore - 9));
                        if (idx >= 0 && idx < static_cast<int>(currentObj->animation.events.size())) {
                            std::string sub = key.substr(underscore + 1);
                            auto& evt = currentObj->animation.events[idx];
                            if (sub == "time") {
                                evt.time = std::stof(value);
                            } else if (sub == "id") {
                                evt.eventId = value;
                            } else if (sub == "payload") {
                                evt.payload = value;
                            }
                        }
                    }
                } else if (key.rfind("animTrack", 0) == 0) {
                    currentObj->hasAnimation = true;
                    size_t underscore = key.find('_');
                    if (underscore != std::string::npos && underscore > 9) {
                        int trackIdx = std::stoi(key.substr(9, underscore - 9));
                        if (trackIdx >= 0 && trackIdx < static_cast<int>(currentObj->animation.tracks.size())) {
                            std::string sub = key.substr(underscore + 1);
                            auto& track = currentObj->animation.tracks[trackIdx];
                            if (sub == "enabled") {
                                track.enabled = std::stoi(value) != 0;
                            } else if (sub == "path") {
                                track.path = value;
                            } else if (sub == "label") {
                                track.label = value;
                            } else if (sub == "default") {
                                track.defaultValue = std::stof(value);
                            } else if (sub == "keyCount") {
                                int count = std::stoi(value);
                                track.keyframes.resize(std::max(0, count));
                            } else if (sub.rfind("key", 0) == 0) {
                                size_t keyUnderscore = sub.find('_');
                                if (keyUnderscore != std::string::npos && keyUnderscore > 3) {
                                    int keyIdx = std::stoi(sub.substr(3, keyUnderscore - 3));
                                    if (keyIdx >= 0 && keyIdx < static_cast<int>(track.keyframes.size())) {
                                        std::string keySub = sub.substr(keyUnderscore + 1);
                                        auto& kf = track.keyframes[keyIdx];
                                        if (keySub == "time") {
                                            kf.time = std::stof(value);
                                        } else if (keySub == "value") {
                                            kf.value = std::stof(value);
                                        } else if (keySub == "interp") {
                                            kf.interpolation = static_cast<AnimationInterpolation>(std::stoi(value));
                                        } else if (keySub == "curve") {
                                            kf.curveMode = static_cast<AnimationCurveMode>(std::stoi(value));
                                        } else if (keySub == "in") {
                                            sscanf(value.c_str(), "%f,%f",
                                                   &kf.bezierIn.x,
                                                   &kf.bezierIn.y);
                                        } else if (keySub == "out") {
                                            sscanf(value.c_str(), "%f,%f",
                                                   &kf.bezierOut.x,
                                                   &kf.bezierOut.y);
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else if (key.rfind("additionalMaterial", 0) == 0) {
                    int idx = std::stoi(key.substr(18)); // length of "additionalMaterial"
                    if (idx >= 0 && idx < (int)currentObj->additionalMaterialPaths.size()) {
                        currentObj->additionalMaterialPaths[idx] = value;
                    }
                } else if (key.rfind("script", 0) == 0) {
                    size_t underscore = key.find('_');
                    if (underscore != std::string::npos && underscore > 6) {
                        int idx = std::stoi(key.substr(6, underscore - 6));
                        if (idx >= 0 && idx < (int)currentObj->scripts.size()) {
                            std::string sub = key.substr(underscore + 1);
                            ScriptComponent& sc = currentObj->scripts[idx];
                            if (sub == "path") {
                                sc.path = value;
                            } else if (sub == "id") {
                                sc.inspectorId = std::max(0, std::stoi(value));
                            } else if (sub == "lang" || sub == "language") {
                                int langValue = std::stoi(value);
                                if (langValue == static_cast<int>(ScriptLanguage::CSharp)) {
                                    sc.language = ScriptLanguage::CSharp;
                                } else if (langValue == static_cast<int>(ScriptLanguage::C)) {
                                    sc.language = ScriptLanguage::C;
                                } else {
                                    sc.language = ScriptLanguage::Cpp;
                                }
                            } else if (sub == "type") {
                                sc.managedType = value;
                            } else if (sub == "enabled") {
                                sc.enabled = std::stoi(value) != 0;
                            } else if (sub == "settings" || sub == "settingCount") {
                                int cnt = std::stoi(value);
                                sc.settings.resize(std::max(0, cnt));
                            } else if (sub.rfind("setting", 0) == 0) {
                                std::string idxStr = sub.substr(7);
                                if (!idxStr.empty() && std::all_of(idxStr.begin(), idxStr.end(), ::isdigit)) {
                                    int sIdx = std::stoi(idxStr);
                                    if (sIdx >= 0 && sIdx < (int)sc.settings.size()) {
                                        size_t sep = value.find(':');
                                        if (sep != std::string::npos) {
                                            sc.settings[sIdx].key = value.substr(0, sep);
                                            sc.settings[sIdx].value = value.substr(sep + 1);
                                        } else {
                                            sc.settings[sIdx].value = value;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        for (auto& obj : objects) {
            EnsureInspectorComponentMetadata(obj);
            if (!obj.hasAnimation) {
                if (!obj.animation.clips.empty() ||
                    !obj.animation.keyframes.empty() ||
                    !obj.animation.events.empty() ||
                    !obj.animation.tracks.empty() ||
                    !obj.animation.clipAssetPath.empty()) {
                    obj.hasAnimation = true;
                }
            }
            if (obj.hasAnimation) {
                NormalizeAnimationClipSlots(obj.animation);
            }
            obj.type = GetLegacyTypeFromComponents(obj);
        }
        UpgradeLegacyScriptSettings(sceneVersion, objects);
        outVersion = sceneVersion;
        if (outTimeOfDay) {
            *outTimeOfDay = sceneTimeOfDay;
        }
        if (outSkyboxSettings) {
            *outSkyboxSettings = sceneSkyboxSettings;
        }
        if (outMetadata) {
            outMetadata->version = sceneVersion;
            outMetadata->fileFormat = SceneSerializer::FileFormat::LegacyFlat;
            outMetadata->loadedFromLegacyLayout = true;
            outMetadata->upgradedToModularLayout = false;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load legacy scene stream: " << e.what() << std::endl;
        return false;
    }
}

bool SceneSerializationInternal::LoadLegacyScene(const fs::path& filePath,
                                                 std::vector<SceneObject>& objects,
                                                 int& nextId,
                                                 int& outVersion,
                                                 float* outTimeOfDay,
                                                 SkyboxSettings* outSkyboxSettings,
                                                 bool deferAssetLoading,
                                                 SceneSerializer::Metadata* outMetadata) {
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) return false;
        const bool loaded = LoadLegacySceneStream(file,
                                                  objects,
                                                  nextId,
                                                  outVersion,
                                                  outTimeOfDay,
                                                  outSkyboxSettings,
                                                  deferAssetLoading,
                                                  outMetadata);
        if (loaded && outMetadata) {
            outMetadata->sourcePath = filePath;
        }
        return loaded;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load legacy scene: " << e.what() << std::endl;
        return false;
    }
}
