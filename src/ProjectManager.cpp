#include "ProjectManager.h"
#include "Rendering.h"
#include "ModelLoader.h"

// Project implementation
Project::Project(const std::string& projectName, const fs::path& basePath)
    : name(projectName) {
    projectPath = basePath / projectName;
    assetsPath = projectPath / "Assets";
    scenesPath = assetsPath / "Scenes";
    scriptsPath = assetsPath / "Scripts";
    scriptsConfigPath = projectPath / "scripts.modu";
    usesNewLayout = true;
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
        fs::path engineRoot = fs::current_path();
        std::ofstream scriptCfg(scriptsConfigPath);
        scriptCfg << "# scripts.modu\n";
        scriptCfg << "cppStandard=c++20\n";
        scriptCfg << "scriptsDir=Assets/Scripts\n";
        scriptCfg << "outDir=Library/CompiledScripts\n";
        scriptCfg << "includeDir=" << (engineRoot / "src").string() << "\n";
        scriptCfg << "includeDir=" << (engineRoot / "include").string() << "\n";
        scriptCfg << "includeDir=" << (engineRoot / "src/ThirdParty").string() << "\n";
        scriptCfg << "includeDir=" << (engineRoot / "src/ThirdParty/glm").string() << "\n";
        scriptCfg << "define=MODU_SCRIPTING=1\n";
        scriptCfg << "define=MODU_PROJECT_NAME=\"" << name << "\"\n";
        scriptCfg << "linux.linkLib=pthread\n";
        scriptCfg << "linux.linkLib=dl\n";
        scriptCfg << "win.linkLib=User32.lib\n";
        scriptCfg << "win.linkLib=Advapi32.lib\n";
        scriptCfg.close();

        std::ofstream packageManifest(projectPath / "packages.modu");
        packageManifest << "# Modularity package manifest\n";
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
            if (line.find("name=") == 0) {
                name = line.substr(5);
            } else if (line.find("lastScene=") == 0) {
                currentSceneName = line.substr(10);
            }
        }
        file.close();

        if (currentSceneName.empty()) {
            currentSceneName = "Main";
        }

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
    loadRecentProjects();

    std::string defaultPath = (fs::current_path() / "Projects").string();
    strncpy(newProjectLocation, defaultPath.c_str(), sizeof(newProjectLocation) - 1);
}

void ProjectManager::loadRecentProjects() {
    recentProjects.clear();
    fs::path recentFile = appDataPath / "recent_projects.txt";

    std::cerr << "[DEBUG] Loading recent projects from: " << recentFile << std::endl;

    if (!fs::exists(recentFile)) {
        std::cerr << "[DEBUG] Recent projects file does not exist" << std::endl;
        return;
    }

    std::ifstream file(recentFile);
    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        lineNum++;
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

            std::cerr << "[DEBUG] Line " << lineNum << ": name='" << rp.name 
                      << "' path='" << rp.path << "' exists=" << fs::exists(rp.path) << std::endl;

            if (fs::exists(rp.path)) {
                recentProjects.push_back(rp);
            } else {
                std::cerr << "[DEBUG] Project path does not exist, skipping: " << rp.path << std::endl;
            }
        } else {
            std::cerr << "[DEBUG] Line " << lineNum << " malformed: " << line << std::endl;
        }
    }
    file.close();
    
    std::cerr << "[DEBUG] Loaded " << recentProjects.size() << " recent projects" << std::endl;
}

void ProjectManager::saveRecentProjects() {
    fs::path recentFile = appDataPath / "recent_projects.txt";
    std::cerr << "[DEBUG] Saving recent projects to: " << recentFile << std::endl;
    
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
        std::cerr << "[DEBUG] Saved: " << rp.name << " -> " << absolutePath << std::endl;
    }
    file.close();
}

void ProjectManager::addToRecentProjects(const std::string& name, const std::string& path) {
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
    
    std::cerr << "[DEBUG] Adding to recent: " << name << " -> " << absolutePath << std::endl;

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
}

bool ProjectManager::loadProject(const std::string& path) {
    if (currentProject.load(path)) {
        addToRecentProjects(currentProject.name, path);
        return true;
    }
    errorMessage = "Failed to load project file";
    return false;
}

// SceneSerializer implementation
bool SceneSerializer::saveScene(const fs::path& filePath,
                                const std::vector<SceneObject>& objects,
                                int nextId) {
    try {
        std::ofstream file(filePath);
        if (!file.is_open()) return false;

        file << "# Scene File\n";
        file << "version=11\n";
        file << "nextId=" << nextId << "\n";
        file << "objectCount=" << objects.size() << "\n";
        file << "\n";

        for (const auto& obj : objects) {
            file << "[Object]\n";
            file << "id=" << obj.id << "\n";
            file << "name=" << obj.name << "\n";
            file << "type=" << static_cast<int>(obj.type) << "\n";
            file << "enabled=" << (obj.enabled ? 1 : 0) << "\n";
            file << "layer=" << obj.layer << "\n";
            file << "tag=" << obj.tag << "\n";
            file << "parentId=" << obj.parentId << "\n";
            file << "position=" << obj.localPosition.x << "," << obj.localPosition.y << "," << obj.localPosition.z << "\n";
            file << "rotation=" << obj.localRotation.x << "," << obj.localRotation.y << "," << obj.localRotation.z << "\n";
            file << "scale=" << obj.localScale.x << "," << obj.localScale.y << "," << obj.localScale.z << "\n";
            file << "hasRigidbody=" << (obj.hasRigidbody ? 1 : 0) << "\n";
            if (obj.hasRigidbody) {
                file << "rbEnabled=" << (obj.rigidbody.enabled ? 1 : 0) << "\n";
                file << "rbMass=" << obj.rigidbody.mass << "\n";
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
                file << "rb2dGravityScale=" << obj.rigidbody2D.gravityScale << "\n";
                file << "rb2dLinearDamping=" << obj.rigidbody2D.linearDamping << "\n";
                file << "rb2dVelocity=" << obj.rigidbody2D.velocity.x << "," << obj.rigidbody2D.velocity.y << "\n";
            }
            file << "hasCollider=" << (obj.hasCollider ? 1 : 0) << "\n";
            if (obj.hasCollider) {
                file << "colliderEnabled=" << (obj.collider.enabled ? 1 : 0) << "\n";
                file << "colliderType=" << static_cast<int>(obj.collider.type) << "\n";
                file << "colliderBox=" << obj.collider.boxSize.x << "," << obj.collider.boxSize.y << "," << obj.collider.boxSize.z << "\n";
                file << "colliderConvex=" << (obj.collider.convex ? 1 : 0) << "\n";
            }
            file << "hasPlayerController=" << (obj.hasPlayerController ? 1 : 0) << "\n";
            if (obj.hasPlayerController) {
                file << "pcEnabled=" << (obj.playerController.enabled ? 1 : 0) << "\n";
                file << "pcMoveSpeed=" << obj.playerController.moveSpeed << "\n";
                file << "pcLookSensitivity=" << obj.playerController.lookSensitivity << "\n";
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
                file << "audioSpatial=" << (obj.audioSource.spatial ? 1 : 0) << "\n";
                file << "audioMinDistance=" << obj.audioSource.minDistance << "\n";
                file << "audioMaxDistance=" << obj.audioSource.maxDistance << "\n";
            }
            file << "materialColor=" << obj.material.color.r << "," << obj.material.color.g << "," << obj.material.color.b << "\n";
            file << "materialAmbient=" << obj.material.ambientStrength << "\n";
            file << "materialSpecular=" << obj.material.specularStrength << "\n";
            file << "materialShininess=" << obj.material.shininess << "\n";
            file << "materialTextureMix=" << obj.material.textureMix << "\n";
            file << "materialPath=" << obj.materialPath << "\n";
            file << "albedoTex=" << obj.albedoTexturePath << "\n";
            file << "overlayTex=" << obj.overlayTexturePath << "\n";
            file << "normalMap=" << obj.normalMapPath << "\n";
            file << "vertexShader=" << obj.vertexShaderPath << "\n";
            file << "fragmentShader=" << obj.fragmentShaderPath << "\n";
            file << "useOverlay=" << (obj.useOverlay ? 1 : 0) << "\n";
            file << "additionalMaterialCount=" << obj.additionalMaterialPaths.size() << "\n";
            for (size_t mi = 0; mi < obj.additionalMaterialPaths.size(); ++mi) {
                file << "additionalMaterial" << mi << "=" << obj.additionalMaterialPaths[mi] << "\n";
            }
            file << "scripts=" << obj.scripts.size() << "\n";
            for (size_t si = 0; si < obj.scripts.size(); ++si) {
                const auto& sc = obj.scripts[si];
                file << "script" << si << "_path=" << sc.path << "\n";
                file << "script" << si << "_enabled=" << (sc.enabled ? 1 : 0) << "\n";
                file << "script" << si << "_settings=" << sc.settings.size() << "\n";
                for (size_t k = 0; k < sc.settings.size(); ++k) {
                    file << "script" << si << "_setting" << k << "=" << sc.settings[k].key << ":" << sc.settings[k].value << "\n";
                }
            }
            file << "lightColor=" << obj.light.color.r << "," << obj.light.color.g << "," << obj.light.color.b << "\n";
            file << "lightIntensity=" << obj.light.intensity << "\n";
            file << "lightRange=" << obj.light.range << "\n";
            file << "lightEdgeFade=" << obj.light.edgeFade << "\n";
            file << "lightInner=" << obj.light.innerAngle << "\n";
            file << "lightOuter=" << obj.light.outerAngle << "\n";
            file << "lightSize=" << obj.light.size.x << "," << obj.light.size.y << "\n";
            file << "lightEnabled=" << (obj.light.enabled ? 1 : 0) << "\n";
            file << "cameraType=" << static_cast<int>(obj.camera.type) << "\n";
            file << "cameraFov=" << obj.camera.fov << "\n";
            file << "cameraNear=" << obj.camera.nearClip << "\n";
            file << "cameraFar=" << obj.camera.farClip << "\n";
            file << "cameraPostFX=" << (obj.camera.applyPostFX ? 1 : 0) << "\n";
            file << "uiAnchor=" << static_cast<int>(obj.ui.anchor) << "\n";
            file << "uiPosition=" << obj.ui.position.x << "," << obj.ui.position.y << "\n";
            file << "uiSize=" << obj.ui.size.x << "," << obj.ui.size.y << "\n";
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
            if (obj.type == ObjectType::PostFXNode) {
                file << "postEnabled=" << (obj.postFx.enabled ? 1 : 0) << "\n";
                file << "postBloomEnabled=" << (obj.postFx.bloomEnabled ? 1 : 0) << "\n";
                file << "postBloomThreshold=" << obj.postFx.bloomThreshold << "\n";
                file << "postBloomIntensity=" << obj.postFx.bloomIntensity << "\n";
                file << "postBloomRadius=" << obj.postFx.bloomRadius << "\n";
                file << "postColorAdjustEnabled=" << (obj.postFx.colorAdjustEnabled ? 1 : 0) << "\n";
                file << "postExposure=" << obj.postFx.exposure << "\n";
                file << "postContrast=" << obj.postFx.contrast << "\n";
                file << "postSaturation=" << obj.postFx.saturation << "\n";
                file << "postColorFilter=" << obj.postFx.colorFilter.r << "," << obj.postFx.colorFilter.g << "," << obj.postFx.colorFilter.b << "\n";
                file << "postMotionBlurEnabled=" << (obj.postFx.motionBlurEnabled ? 1 : 0) << "\n";
                file << "postMotionBlurStrength=" << obj.postFx.motionBlurStrength << "\n";
                file << "postVignetteEnabled=" << (obj.postFx.vignetteEnabled ? 1 : 0) << "\n";
                file << "postVignetteIntensity=" << obj.postFx.vignetteIntensity << "\n";
                file << "postVignetteSmoothness=" << obj.postFx.vignetteSmoothness << "\n";
                file << "postChromaticEnabled=" << (obj.postFx.chromaticAberrationEnabled ? 1 : 0) << "\n";
                file << "postChromaticAmount=" << obj.postFx.chromaticAmount << "\n";
                file << "postAOEnabled=" << (obj.postFx.ambientOcclusionEnabled ? 1 : 0) << "\n";
                file << "postAORadius=" << obj.postFx.aoRadius << "\n";
                file << "postAOStrength=" << obj.postFx.aoStrength << "\n";
            }

            file << "scriptCount=" << obj.scripts.size() << "\n";
            for (size_t s = 0; s < obj.scripts.size(); ++s) {
                const auto& sc = obj.scripts[s];
                file << "script" << s << "_path=" << sc.path << "\n";
                file << "script" << s << "_enabled=" << (sc.enabled ? 1 : 0) << "\n";
                file << "script" << s << "_settingCount=" << sc.settings.size() << "\n";
                for (size_t si = 0; si < sc.settings.size(); ++si) {
                    file << "script" << s << "_setting" << si << "=" << sc.settings[si].key << ":" << sc.settings[si].value << "\n";
                }
            }
            
            if ((obj.type == ObjectType::OBJMesh || obj.type == ObjectType::Model) && !obj.meshPath.empty()) {
                file << "meshPath=" << obj.meshPath << "\n";
            }

            file << "children=";
            for (size_t i = 0; i < obj.childIds.size(); i++) {
                if (i > 0) file << ",";
                file << obj.childIds[i];
            }
            file << "\n\n";
        }

        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save scene: " << e.what() << std::endl;
        return false;
    }
}

bool SceneSerializer::loadScene(const fs::path& filePath,
                               std::vector<SceneObject>& objects,
                               int& nextId,
                               int& outVersion) {
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) return false;

        objects.clear();
        std::string line;
        SceneObject* currentObj = nullptr;
        int sceneVersion = 9;

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
                objects.push_back(SceneObject("", ObjectType::Cube, 0));
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
            } else if (currentObj) {
                if (key == "id") {
                    currentObj->id = std::stoi(value);
                } else if (key == "name") {
                    currentObj->name = value;
                } else if (key == "type") {
                    currentObj->type = static_cast<ObjectType>(std::stoi(value));
                    if (currentObj->type == ObjectType::DirectionalLight) currentObj->light.type = LightType::Directional;
                    else if (currentObj->type == ObjectType::PointLight) currentObj->light.type = LightType::Point;
                    else if (currentObj->type == ObjectType::SpotLight) currentObj->light.type = LightType::Spot;
                    else if (currentObj->type == ObjectType::AreaLight) currentObj->light.type = LightType::Area;
                    else if (currentObj->type == ObjectType::Camera) {
                        currentObj->camera.type = SceneCameraType::Scene;
                    }
                } else if (key == "enabled") {
                    currentObj->enabled = (std::stoi(value) != 0);
                } else if (key == "layer") {
                    currentObj->layer = std::stoi(value);
                } else if (key == "tag") {
                    currentObj->tag = value;
                } else if (key == "parentId") {
                    currentObj->parentId = std::stoi(value);
                } else if (key == "position") {
                    sscanf(value.c_str(), "%f,%f,%f",
                           &currentObj->position.x,
                           &currentObj->position.y,
                           &currentObj->position.z);
                    currentObj->localPosition = currentObj->position;
                    currentObj->localInitialized = true;
                } else if (key == "rotation") {
                    sscanf(value.c_str(), "%f,%f,%f",
                           &currentObj->rotation.x,
                           &currentObj->rotation.y,
                           &currentObj->rotation.z);
                    currentObj->rotation = NormalizeEulerDegrees(currentObj->rotation);
                    currentObj->localRotation = currentObj->rotation;
                    currentObj->localInitialized = true;
                } else if (key == "scale") {
                    sscanf(value.c_str(), "%f,%f,%f",
                           &currentObj->scale.x,
                           &currentObj->scale.y,
                           &currentObj->scale.z);
                    currentObj->localScale = currentObj->scale;
                    currentObj->localInitialized = true;
                } else if (key == "hasRigidbody") {
                    currentObj->hasRigidbody = std::stoi(value) != 0;
                } else if (key == "rbEnabled") {
                    currentObj->rigidbody.enabled = std::stoi(value) != 0;
                } else if (key == "rbMass") {
                    currentObj->rigidbody.mass = std::stof(value);
                } else if (key == "rbUseGravity") {
                    currentObj->rigidbody.useGravity = std::stoi(value) != 0;
                } else if (key == "rbKinematic") {
                    currentObj->rigidbody.isKinematic = std::stoi(value) != 0;
                } else if (key == "rbLinearDamping") {
                    currentObj->rigidbody.linearDamping = std::stof(value);
                } else if (key == "rbAngularDamping") {
                    currentObj->rigidbody.angularDamping = std::stof(value);
                } else if (key == "rbLockRotX") {
                    currentObj->rigidbody.lockRotationX = std::stoi(value) != 0;
                } else if (key == "rbLockRotY") {
                    currentObj->rigidbody.lockRotationY = std::stoi(value) != 0;
                } else if (key == "rbLockRotZ") {
                    currentObj->rigidbody.lockRotationZ = std::stoi(value) != 0;
                } else if (key == "hasRigidbody2D") {
                    currentObj->hasRigidbody2D = std::stoi(value) != 0;
                } else if (key == "rb2dEnabled") {
                    currentObj->rigidbody2D.enabled = std::stoi(value) != 0;
                } else if (key == "rb2dUseGravity") {
                    currentObj->rigidbody2D.useGravity = std::stoi(value) != 0;
                } else if (key == "rb2dGravityScale") {
                    currentObj->rigidbody2D.gravityScale = std::stof(value);
                } else if (key == "rb2dLinearDamping") {
                    currentObj->rigidbody2D.linearDamping = std::stof(value);
                } else if (key == "rb2dVelocity") {
                    sscanf(value.c_str(), "%f,%f",
                           &currentObj->rigidbody2D.velocity.x,
                           &currentObj->rigidbody2D.velocity.y);
                } else if (key == "hasCollider") {
                    currentObj->hasCollider = std::stoi(value) != 0;
                } else if (key == "colliderEnabled") {
                    currentObj->collider.enabled = std::stoi(value) != 0;
                } else if (key == "colliderType") {
                    currentObj->collider.type = static_cast<ColliderType>(std::stoi(value));
                } else if (key == "colliderBox") {
                    sscanf(value.c_str(), "%f,%f,%f",
                           &currentObj->collider.boxSize.x,
                           &currentObj->collider.boxSize.y,
                           &currentObj->collider.boxSize.z);
                } else if (key == "colliderConvex") {
                    currentObj->collider.convex = std::stoi(value) != 0;
                } else if (key == "hasPlayerController") {
                    currentObj->hasPlayerController = std::stoi(value) != 0;
                } else if (key == "pcEnabled") {
                    currentObj->playerController.enabled = std::stoi(value) != 0;
                } else if (key == "pcMoveSpeed") {
                    currentObj->playerController.moveSpeed = std::stof(value);
                } else if (key == "pcLookSensitivity") {
                    currentObj->playerController.lookSensitivity = std::stof(value);
                } else if (key == "pcHeight") {
                    currentObj->playerController.height = std::stof(value);
                } else if (key == "pcRadius") {
                    currentObj->playerController.radius = std::stof(value);
                } else if (key == "pcJumpStrength") {
                    currentObj->playerController.jumpStrength = std::stof(value);
                } else if (key == "hasAudioSource") {
                    currentObj->hasAudioSource = std::stoi(value) != 0;
                } else if (key == "audioEnabled") {
                    currentObj->audioSource.enabled = std::stoi(value) != 0;
                } else if (key == "audioClip") {
                    currentObj->audioSource.clipPath = value;
                } else if (key == "audioVolume") {
                    currentObj->audioSource.volume = std::stof(value);
                } else if (key == "audioLoop") {
                    currentObj->audioSource.loop = std::stoi(value) != 0;
                } else if (key == "audioPlayOnStart") {
                    currentObj->audioSource.playOnStart = std::stoi(value) != 0;
                } else if (key == "audioSpatial") {
                    currentObj->audioSource.spatial = std::stoi(value) != 0;
                } else if (key == "audioMinDistance") {
                    currentObj->audioSource.minDistance = std::stof(value);
                } else if (key == "audioMaxDistance") {
                    currentObj->audioSource.maxDistance = std::stof(value);
                } else if (key == "materialColor") {
                    sscanf(value.c_str(), "%f,%f,%f",
                           &currentObj->material.color.r,
                           &currentObj->material.color.g,
                           &currentObj->material.color.b);
                } else if (key == "materialAmbient") {
                    currentObj->material.ambientStrength = std::stof(value);
                } else if (key == "materialSpecular") {
                    currentObj->material.specularStrength = std::stof(value);
                } else if (key == "materialShininess") {
                    currentObj->material.shininess = std::stof(value);
                } else if (key == "materialTextureMix") {
                    currentObj->material.textureMix = std::stof(value);
                } else if (key == "materialPath") {
                    currentObj->materialPath = value;
                } else if (key == "albedoTex") {
                    currentObj->albedoTexturePath = value;
                } else if (key == "overlayTex") {
                    currentObj->overlayTexturePath = value;
                } else if (key == "normalMap") {
                    currentObj->normalMapPath = value;
                } else if (key == "vertexShader") {
                    currentObj->vertexShaderPath = value;
                } else if (key == "fragmentShader") {
                    currentObj->fragmentShaderPath = value;
                } else if (key == "useOverlay") {
                    currentObj->useOverlay = (std::stoi(value) != 0);
                } else if (key == "additionalMaterialCount") {
                    int count = std::stoi(value);
                    currentObj->additionalMaterialPaths.resize(std::max(0, count));
                } else if (key.rfind("additionalMaterial", 0) == 0) {
                    int idx = std::stoi(key.substr(18)); // length of "additionalMaterial"
                    if (idx >= 0 && idx < (int)currentObj->additionalMaterialPaths.size()) {
                        currentObj->additionalMaterialPaths[idx] = value;
                    }
                } else if (key == "scripts") {
                    int count = std::stoi(value);
                    currentObj->scripts.resize(std::max(0, count));
                } else if (key.rfind("script", 0) == 0) {
                    size_t underscore = key.find('_');
                    if (underscore != std::string::npos && underscore > 6) {
                        int idx = std::stoi(key.substr(6, underscore - 6));
                        if (idx >= 0 && idx < (int)currentObj->scripts.size()) {
                            std::string sub = key.substr(underscore + 1);
                            ScriptComponent& sc = currentObj->scripts[idx];
                            if (sub == "path") {
                                sc.path = value;
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
                } else if (key == "lightColor") {
                    sscanf(value.c_str(), "%f,%f,%f",
                           &currentObj->light.color.r,
                           &currentObj->light.color.g,
                           &currentObj->light.color.b);
                } else if (key == "lightIntensity") {
                    currentObj->light.intensity = std::stof(value);
                } else if (key == "lightRange") {
                    currentObj->light.range = std::stof(value);
                } else if (key == "lightEdgeFade") {
                    currentObj->light.edgeFade = std::stof(value);
                } else if (key == "lightInner") {
                    currentObj->light.innerAngle = std::stof(value);
                } else if (key == "lightOuter") {
                    currentObj->light.outerAngle = std::stof(value);
                } else if (key == "lightSize") {
                    sscanf(value.c_str(), "%f,%f",
                           &currentObj->light.size.x,
                           &currentObj->light.size.y);
                } else if (key == "lightEnabled") {
                    currentObj->light.enabled = (std::stoi(value) != 0);
                } else if (key == "cameraType") {
                    currentObj->camera.type = static_cast<SceneCameraType>(std::stoi(value));
                } else if (key == "cameraFov") {
                    currentObj->camera.fov = std::stof(value);
                } else if (key == "cameraNear") {
                    currentObj->camera.nearClip = std::stof(value);
                } else if (key == "cameraFar") {
                    currentObj->camera.farClip = std::stof(value);
                } else if (key == "cameraPostFX") {
                    currentObj->camera.applyPostFX = (std::stoi(value) != 0);
                } else if (key == "uiAnchor") {
                    currentObj->ui.anchor = static_cast<UIAnchor>(std::stoi(value));
                } else if (key == "uiPosition") {
                    sscanf(value.c_str(), "%f,%f",
                           &currentObj->ui.position.x,
                           &currentObj->ui.position.y);
                } else if (key == "uiSize") {
                    sscanf(value.c_str(), "%f,%f",
                           &currentObj->ui.size.x,
                           &currentObj->ui.size.y);
                } else if (key == "uiSliderValue") {
                    currentObj->ui.sliderValue = std::stof(value);
                } else if (key == "uiSliderMin") {
                    currentObj->ui.sliderMin = std::stof(value);
                } else if (key == "uiSliderMax") {
                    currentObj->ui.sliderMax = std::stof(value);
                } else if (key == "uiLabel") {
                    currentObj->ui.label = value;
                } else if (key == "uiColor") {
                    sscanf(value.c_str(), "%f,%f,%f,%f",
                           &currentObj->ui.color.r,
                           &currentObj->ui.color.g,
                           &currentObj->ui.color.b,
                           &currentObj->ui.color.a);
                } else if (key == "uiInteractable") {
                    currentObj->ui.interactable = (std::stoi(value) != 0);
                } else if (key == "uiSliderStyle") {
                    currentObj->ui.sliderStyle = static_cast<UISliderStyle>(std::stoi(value));
                } else if (key == "uiButtonStyle") {
                    currentObj->ui.buttonStyle = static_cast<UIButtonStyle>(std::stoi(value));
                } else if (key == "uiStylePreset") {
                    currentObj->ui.stylePreset = value;
                } else if (key == "uiTextScale") {
                    currentObj->ui.textScale = std::stof(value);
                } else if (key == "postEnabled") {
                    currentObj->postFx.enabled = (std::stoi(value) != 0);
                } else if (key == "postBloomEnabled") {
                    currentObj->postFx.bloomEnabled = (std::stoi(value) != 0);
                } else if (key == "postBloomThreshold") {
                    currentObj->postFx.bloomThreshold = std::stof(value);
                } else if (key == "postBloomIntensity") {
                    currentObj->postFx.bloomIntensity = std::stof(value);
                } else if (key == "postBloomRadius") {
                    currentObj->postFx.bloomRadius = std::stof(value);
                } else if (key == "postColorAdjustEnabled") {
                    currentObj->postFx.colorAdjustEnabled = (std::stoi(value) != 0);
                } else if (key == "postExposure") {
                    currentObj->postFx.exposure = std::stof(value);
                } else if (key == "postContrast") {
                    currentObj->postFx.contrast = std::stof(value);
                } else if (key == "postSaturation") {
                    currentObj->postFx.saturation = std::stof(value);
                } else if (key == "postColorFilter") {
                    sscanf(value.c_str(), "%f,%f,%f",
                           &currentObj->postFx.colorFilter.r,
                           &currentObj->postFx.colorFilter.g,
                           &currentObj->postFx.colorFilter.b);
                } else if (key == "postMotionBlurEnabled") {
                    currentObj->postFx.motionBlurEnabled = (std::stoi(value) != 0);
                } else if (key == "postMotionBlurStrength") {
                    currentObj->postFx.motionBlurStrength = std::stof(value);
                } else if (key == "postVignetteEnabled") {
                    currentObj->postFx.vignetteEnabled = (std::stoi(value) != 0);
                } else if (key == "postVignetteIntensity") {
                    currentObj->postFx.vignetteIntensity = std::stof(value);
                } else if (key == "postVignetteSmoothness") {
                    currentObj->postFx.vignetteSmoothness = std::stof(value);
                } else if (key == "postChromaticEnabled") {
                    currentObj->postFx.chromaticAberrationEnabled = (std::stoi(value) != 0);
                } else if (key == "postChromaticAmount") {
                    currentObj->postFx.chromaticAmount = std::stof(value);
                } else if (key == "postAOEnabled") {
                    currentObj->postFx.ambientOcclusionEnabled = (std::stoi(value) != 0);
                } else if (key == "postAORadius") {
                    currentObj->postFx.aoRadius = std::stof(value);
                } else if (key == "postAOStrength") {
                    currentObj->postFx.aoStrength = std::stof(value);
                } else if (key == "scriptCount") {
                    int count = std::stoi(value);
                    currentObj->scripts.resize(std::max(0, count));
                } else if (key.rfind("script", 0) == 0) {
                    size_t underscore = key.find('_');
                    if (underscore != std::string::npos && underscore > 6) {
                        int idx = std::stoi(key.substr(6, underscore - 6));
                        if (idx >= 0 && idx < (int)currentObj->scripts.size()) {
                            std::string subKey = key.substr(underscore + 1);
                            ScriptComponent& sc = currentObj->scripts[idx];
                            if (subKey == "path") {
                                sc.path = value;
                            } else if (subKey == "enabled") {
                                sc.enabled = std::stoi(value) != 0;
                            } else if (subKey == "settingCount") {
                                int cnt = std::stoi(value);
                                sc.settings.resize(std::max(0, cnt));
                            } else if (subKey.rfind("setting", 0) == 0) {
                                int sIdx = std::stoi(subKey.substr(7));
                                if (sIdx >= 0 && sIdx < (int)sc.settings.size()) {
                                    size_t sep = value.find(':');
                                    if (sep != std::string::npos) {
                                        sc.settings[sIdx].key = value.substr(0, sep);
                                        sc.settings[sIdx].value = value.substr(sep + 1);
                                    } else {
                                        sc.settings[sIdx].key.clear();
                                        sc.settings[sIdx].value = value;
                                    }
                                }
                            }
                        }
                    }
                } else if (key == "meshPath") {
                    currentObj->meshPath = value;
                    if (!value.empty() && currentObj->type == ObjectType::OBJMesh) {
                        std::string err;
                        currentObj->meshId = g_objLoader.loadOBJ(value, err);
                    } else if (!value.empty() && currentObj->type == ObjectType::Model) {
                        ModelLoadResult result = getModelLoader().loadModel(value);
                        if (result.success) {
                            currentObj->meshId = result.meshIndex;
                        } else {
                            std::cerr << "Failed to load model from scene: " << result.errorMessage << std::endl;
                            currentObj->meshId = -1;
                        }
                    }
                } else if (key == "children" && !value.empty()) {
                    std::stringstream ss(value);
                    std::string item;
                    while (std::getline(ss, item, ',')) {
                        if (!item.empty()) {
                            currentObj->childIds.push_back(std::stoi(item));
                        }
                    }
                }
            }
        }

        file.close();
        outVersion = sceneVersion;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load scene: " << e.what() << std::endl;
        return false;
    }
}
