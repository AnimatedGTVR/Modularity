#include "ProjectManager.h"
#include "Rendering.h"
#include "ModelLoader.h"
#include <cmath>
#include <unordered_map>

ObjectType GetLegacyTypeFromComponents(const SceneObject& obj);

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
                                int nextId,
                                float timeOfDay) {
    try {
        std::ofstream file(filePath);
        if (!file.is_open()) return false;

        file << "# Scene File\n";
        file << "version=18\n";
        file << "nextId=" << nextId << "\n";
        file << "timeOfDay=" << timeOfDay << "\n";
        file << "objectCount=" << objects.size() << "\n";
        file << "\n";

        for (const auto& obj : objects) {
            file << "[Object]\n";
            file << "id=" << obj.id << "\n";
            file << "name=" << obj.name << "\n";
            ObjectType legacyType = GetLegacyTypeFromComponents(obj);
            file << "type=" << static_cast<int>(legacyType) << "\n";
            file << "enabled=" << (obj.enabled ? 1 : 0) << "\n";
            file << "layer=" << obj.layer << "\n";
            file << "tag=" << obj.tag << "\n";
            file << "hasRenderer=" << (obj.hasRenderer ? 1 : 0) << "\n";
            file << "renderType=" << static_cast<int>(obj.renderType) << "\n";
            file << "hasLight=" << (obj.hasLight ? 1 : 0) << "\n";
            file << "hasCamera=" << (obj.hasCamera ? 1 : 0) << "\n";
            file << "hasPostFX=" << (obj.hasPostFX ? 1 : 0) << "\n";
            file << "hasUI=" << (obj.hasUI ? 1 : 0) << "\n";
            file << "uiType=" << static_cast<int>(obj.ui.type) << "\n";
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
            file << "hasCollider2D=" << (obj.hasCollider2D ? 1 : 0) << "\n";
            if (obj.hasCollider2D) {
                file << "collider2dEnabled=" << (obj.collider2D.enabled ? 1 : 0) << "\n";
                file << "collider2dType=" << static_cast<int>(obj.collider2D.type) << "\n";
                file << "collider2dBox=" << obj.collider2D.boxSize.x << "," << obj.collider2D.boxSize.y << "\n";
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
                file << "audioSpatial=" << (obj.audioSource.spatial ? 1 : 0) << "\n";
                file << "audioMinDistance=" << obj.audioSource.minDistance << "\n";
                file << "audioMaxDistance=" << obj.audioSource.maxDistance << "\n";
                file << "audioRolloffMode=" << static_cast<int>(obj.audioSource.rolloffMode) << "\n";
                file << "audioRolloff=" << obj.audioSource.rolloff << "\n";
                file << "audioCustomMidDistance=" << obj.audioSource.customMidDistance << "\n";
                file << "audioCustomMidGain=" << obj.audioSource.customMidGain << "\n";
                file << "audioCustomEndGain=" << obj.audioSource.customEndGain << "\n";
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
            file << "hasAnimation=" << (obj.hasAnimation ? 1 : 0) << "\n";
            if (obj.hasAnimation) {
                file << "animEnabled=" << (obj.animation.enabled ? 1 : 0) << "\n";
                file << "animClipLength=" << obj.animation.clipLength << "\n";
                file << "animPlaySpeed=" << obj.animation.playSpeed << "\n";
                file << "animLoop=" << (obj.animation.loop ? 1 : 0) << "\n";
                file << "animApplyOnScrub=" << (obj.animation.applyOnScrub ? 1 : 0) << "\n";
                file << "animKeyCount=" << obj.animation.keyframes.size() << "\n";
                for (size_t ki = 0; ki < obj.animation.keyframes.size(); ++ki) {
                    const auto& key = obj.animation.keyframes[ki];
                    file << "animKey" << ki << "_time=" << key.time << "\n";
                    file << "animKey" << ki << "_pos=" << key.position.x << "," << key.position.y << "," << key.position.z << "\n";
                    file << "animKey" << ki << "_rot=" << key.rotation.x << "," << key.rotation.y << "," << key.rotation.z << "\n";
                    file << "animKey" << ki << "_scale=" << key.scale.x << "," << key.scale.y << "," << key.scale.z << "\n";
                    file << "animKey" << ki << "_interp=" << static_cast<int>(key.interpolation) << "\n";
                    file << "animKey" << ki << "_curve=" << static_cast<int>(key.curveMode) << "\n";
                    file << "animKey" << ki << "_in=" << key.bezierIn.x << "," << key.bezierIn.y << "\n";
                    file << "animKey" << ki << "_out=" << key.bezierOut.x << "," << key.bezierOut.y << "\n";
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
            file << "materialAmbient=" << obj.material.ambientStrength << "\n";
            file << "materialSpecular=" << obj.material.specularStrength << "\n";
            file << "materialShininess=" << obj.material.shininess << "\n";
            file << "materialTextureMix=" << obj.material.textureMix << "\n";
            file << "materialTextureFilter=" << static_cast<int>(obj.material.textureFilter) << "\n";
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
            file << "lightEnabled=" << (obj.light.enabled ? 1 : 0) << "\n";
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
            file << "uiRenderIn3D=" << (obj.ui.renderIn3D ? 1 : 0) << "\n";
            file << "uiRenderTargetSize=" << obj.ui.renderTargetSize.x << "," << obj.ui.renderTargetSize.y << "\n";
            if (obj.hasPostFX) {
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

        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save scene: " << e.what() << std::endl;
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

void ApplyLegacyTypePreset(SceneObject& obj, ObjectType legacyType) {
    obj.type = legacyType;
    switch (legacyType) {
        case ObjectType::Cube:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Cube;
            break;
        case ObjectType::Sphere:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Sphere;
            break;
        case ObjectType::Capsule:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Capsule;
            break;
        case ObjectType::OBJMesh:
            obj.hasRenderer = true;
            obj.renderType = RenderType::OBJMesh;
            break;
        case ObjectType::Model:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Model;
            break;
        case ObjectType::Mirror:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Mirror;
            break;
        case ObjectType::Plane:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Plane;
            break;
        case ObjectType::Torus:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Torus;
            break;
        case ObjectType::Sprite:
            obj.hasRenderer = true;
            obj.renderType = RenderType::Sprite;
            break;
        case ObjectType::DirectionalLight:
            obj.hasLight = true;
            obj.light.type = LightType::Directional;
            break;
        case ObjectType::PointLight:
            obj.hasLight = true;
            obj.light.type = LightType::Point;
            break;
        case ObjectType::SpotLight:
            obj.hasLight = true;
            obj.light.type = LightType::Spot;
            break;
        case ObjectType::AreaLight:
            obj.hasLight = true;
            obj.light.type = LightType::Area;
            break;
        case ObjectType::Camera:
            obj.hasCamera = true;
            obj.camera.type = SceneCameraType::Scene;
            break;
        case ObjectType::PostFXNode:
            obj.hasPostFX = true;
            break;
        case ObjectType::Canvas:
            obj.hasUI = true;
            obj.ui.type = UIElementType::Canvas;
            break;
        case ObjectType::UIImage:
            obj.hasUI = true;
            obj.ui.type = UIElementType::Image;
            break;
        case ObjectType::UISlider:
            obj.hasUI = true;
            obj.ui.type = UIElementType::Slider;
            break;
        case ObjectType::UIButton:
            obj.hasUI = true;
            obj.ui.type = UIElementType::Button;
            break;
        case ObjectType::UIText:
            obj.hasUI = true;
            obj.ui.type = UIElementType::Text;
            break;
        case ObjectType::Sprite2D:
            obj.hasUI = true;
            obj.ui.type = UIElementType::Sprite2D;
            break;
        case ObjectType::Empty:
        default:
            break;
    }
}

using KeyHandler = void (*)(SceneObject&, const std::string&);

const std::unordered_map<std::string, KeyHandler>& GetSceneObjectKeyHandlers() {
    static const std::unordered_map<std::string, KeyHandler> handlers = {
        {"id", +[](SceneObject& obj, const std::string& value) { obj.id = std::stoi(value); }},
        {"name", +[](SceneObject& obj, const std::string& value) { obj.name = value; }},
        {"type", +[](SceneObject& obj, const std::string& value) {
             ApplyLegacyTypePreset(obj, static_cast<ObjectType>(std::stoi(value)));
         }},
        {"enabled", +[](SceneObject& obj, const std::string& value) { obj.enabled = (std::stoi(value) != 0); }},
        {"layer", +[](SceneObject& obj, const std::string& value) { obj.layer = std::stoi(value); }},
        {"tag", +[](SceneObject& obj, const std::string& value) { obj.tag = value; }},
        {"hasRenderer", +[](SceneObject& obj, const std::string& value) { obj.hasRenderer = std::stoi(value) != 0; }},
        {"renderType", +[](SceneObject& obj, const std::string& value) {
             obj.renderType = static_cast<RenderType>(std::stoi(value));
             if (obj.renderType != RenderType::None) {
                 obj.hasRenderer = true;
             }
         }},
        {"hasLight", +[](SceneObject& obj, const std::string& value) { obj.hasLight = std::stoi(value) != 0; }},
        {"hasCamera", +[](SceneObject& obj, const std::string& value) { obj.hasCamera = std::stoi(value) != 0; }},
        {"hasPostFX", +[](SceneObject& obj, const std::string& value) { obj.hasPostFX = std::stoi(value) != 0; }},
        {"hasUI", +[](SceneObject& obj, const std::string& value) { obj.hasUI = std::stoi(value) != 0; }},
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
        {"rb2dGravityScale", +[](SceneObject& obj, const std::string& value) { obj.rigidbody2D.gravityScale = std::stof(value); }},
        {"rb2dLinearDamping", +[](SceneObject& obj, const std::string& value) { obj.rigidbody2D.linearDamping = std::stof(value); }},
        {"rb2dVelocity", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.rigidbody2D.velocity); }},
        {"hasCollider2D", +[](SceneObject& obj, const std::string& value) { obj.hasCollider2D = std::stoi(value) != 0; }},
        {"collider2dEnabled", +[](SceneObject& obj, const std::string& value) { obj.collider2D.enabled = std::stoi(value) != 0; }},
        {"collider2dType", +[](SceneObject& obj, const std::string& value) { obj.collider2D.type = static_cast<Collider2DType>(std::stoi(value)); }},
        {"collider2dBox", +[](SceneObject& obj, const std::string& value) { ParseVec2(value, obj.collider2D.boxSize); }},
        {"collider2dClosed", +[](SceneObject& obj, const std::string& value) { obj.collider2D.closed = std::stoi(value) != 0; }},
        {"collider2dEdgeThickness", +[](SceneObject& obj, const std::string& value) { obj.collider2D.edgeThickness = std::stof(value); }},
        {"collider2dPoints", +[](SceneObject& obj, const std::string& value) { ParseVec2List(value, obj.collider2D.points); }},
        {"hasParallaxLayer2D", +[](SceneObject& obj, const std::string& value) { obj.hasParallaxLayer2D = std::stoi(value) != 0; }},
        {"parallax2dEnabled", +[](SceneObject& obj, const std::string& value) { obj.parallaxLayer2D.enabled = std::stoi(value) != 0; }},
        {"parallax2dOrder", +[](SceneObject& obj, const std::string& value) { obj.parallaxLayer2D.order = std::stoi(value); }},
        {"parallax2dFactor", +[](SceneObject& obj, const std::string& value) { obj.parallaxLayer2D.factor = std::stof(value); }},
        {"parallax2dRepeatX", +[](SceneObject& obj, const std::string& value) { obj.parallaxLayer2D.repeatX = std::stoi(value) != 0; }},
        {"parallax2dRepeatY", +[](SceneObject& obj, const std::string& value) { obj.parallaxLayer2D.repeatY = std::stoi(value) != 0; }},
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
        {"audioSpatial", +[](SceneObject& obj, const std::string& value) { obj.audioSource.spatial = std::stoi(value) != 0; }},
        {"audioMinDistance", +[](SceneObject& obj, const std::string& value) { obj.audioSource.minDistance = std::stof(value); }},
        {"audioMaxDistance", +[](SceneObject& obj, const std::string& value) { obj.audioSource.maxDistance = std::stof(value); }},
        {"audioRolloffMode", +[](SceneObject& obj, const std::string& value) { obj.audioSource.rolloffMode = static_cast<AudioRolloffMode>(std::stoi(value)); }},
        {"audioRolloff", +[](SceneObject& obj, const std::string& value) { obj.audioSource.rolloff = std::stof(value); }},
        {"audioCustomMidDistance", +[](SceneObject& obj, const std::string& value) { obj.audioSource.customMidDistance = std::stof(value); }},
        {"audioCustomMidGain", +[](SceneObject& obj, const std::string& value) { obj.audioSource.customMidGain = std::stof(value); }},
        {"audioCustomEndGain", +[](SceneObject& obj, const std::string& value) { obj.audioSource.customEndGain = std::stof(value); }},
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
        {"hasAnimation", +[](SceneObject& obj, const std::string& value) { obj.hasAnimation = std::stoi(value) != 0; }},
        {"animEnabled", +[](SceneObject& obj, const std::string& value) { obj.animation.enabled = std::stoi(value) != 0; }},
        {"animClipLength", +[](SceneObject& obj, const std::string& value) { obj.animation.clipLength = std::stof(value); }},
        {"animPlaySpeed", +[](SceneObject& obj, const std::string& value) { obj.animation.playSpeed = std::stof(value); }},
        {"animLoop", +[](SceneObject& obj, const std::string& value) { obj.animation.loop = std::stoi(value) != 0; }},
        {"animApplyOnScrub", +[](SceneObject& obj, const std::string& value) { obj.animation.applyOnScrub = std::stoi(value) != 0; }},
        {"animKeyCount", +[](SceneObject& obj, const std::string& value) {
             int count = std::stoi(value);
             obj.animation.keyframes.resize(std::max(0, count));
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
        {"materialAmbient", +[](SceneObject& obj, const std::string& value) { obj.material.ambientStrength = std::stof(value); }},
        {"materialSpecular", +[](SceneObject& obj, const std::string& value) { obj.material.specularStrength = std::stof(value); }},
        {"materialShininess", +[](SceneObject& obj, const std::string& value) { obj.material.shininess = std::stof(value); }},
        {"materialTextureMix", +[](SceneObject& obj, const std::string& value) { obj.material.textureMix = std::stof(value); }},
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
        {"vertexShader", +[](SceneObject& obj, const std::string& value) { obj.vertexShaderPath = value; }},
        {"fragmentShader", +[](SceneObject& obj, const std::string& value) { obj.fragmentShaderPath = value; }},
        {"useOverlay", +[](SceneObject& obj, const std::string& value) { obj.useOverlay = (std::stoi(value) != 0); }},
        {"additionalMaterialCount", +[](SceneObject& obj, const std::string& value) {
             int count = std::stoi(value);
             obj.additionalMaterialPaths.resize(std::max(0, count));
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
        {"lightEnabled", +[](SceneObject& obj, const std::string& value) { obj.light.enabled = (std::stoi(value) != 0); }},
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
        {"uiRenderIn3D", +[](SceneObject& obj, const std::string& value) { obj.ui.renderIn3D = (std::stoi(value) != 0); }},
        {"uiRenderTargetSize", +[](SceneObject& obj, const std::string& value) { ParseIVec2(value, obj.ui.renderTargetSize); }},
        {"postEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.enabled = (std::stoi(value) != 0); }},
        {"postBloomEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.bloomEnabled = (std::stoi(value) != 0); }},
        {"postBloomThreshold", +[](SceneObject& obj, const std::string& value) { obj.postFx.bloomThreshold = std::stof(value); }},
        {"postBloomIntensity", +[](SceneObject& obj, const std::string& value) { obj.postFx.bloomIntensity = std::stof(value); }},
        {"postBloomRadius", +[](SceneObject& obj, const std::string& value) { obj.postFx.bloomRadius = std::stof(value); }},
        {"postColorAdjustEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.colorAdjustEnabled = (std::stoi(value) != 0); }},
        {"postExposure", +[](SceneObject& obj, const std::string& value) { obj.postFx.exposure = std::stof(value); }},
        {"postContrast", +[](SceneObject& obj, const std::string& value) { obj.postFx.contrast = std::stof(value); }},
        {"postSaturation", +[](SceneObject& obj, const std::string& value) { obj.postFx.saturation = std::stof(value); }},
        {"postColorFilter", +[](SceneObject& obj, const std::string& value) { ParseVec3(value, obj.postFx.colorFilter); }},
        {"postMotionBlurEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.motionBlurEnabled = (std::stoi(value) != 0); }},
        {"postMotionBlurStrength", +[](SceneObject& obj, const std::string& value) { obj.postFx.motionBlurStrength = std::stof(value); }},
        {"postVignetteEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.vignetteEnabled = (std::stoi(value) != 0); }},
        {"postVignetteIntensity", +[](SceneObject& obj, const std::string& value) { obj.postFx.vignetteIntensity = std::stof(value); }},
        {"postVignetteSmoothness", +[](SceneObject& obj, const std::string& value) { obj.postFx.vignetteSmoothness = std::stof(value); }},
        {"postChromaticEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.chromaticAberrationEnabled = (std::stoi(value) != 0); }},
        {"postChromaticAmount", +[](SceneObject& obj, const std::string& value) { obj.postFx.chromaticAmount = std::stof(value); }},
        {"postAOEnabled", +[](SceneObject& obj, const std::string& value) { obj.postFx.ambientOcclusionEnabled = (std::stoi(value) != 0); }},
        {"postAORadius", +[](SceneObject& obj, const std::string& value) { obj.postFx.aoRadius = std::stof(value); }},
        {"postAOStrength", +[](SceneObject& obj, const std::string& value) { obj.postFx.aoStrength = std::stof(value); }},
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
            case UIElementType::Sprite2D: return ObjectType::Sprite2D;
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
    if (obj.hasCamera) {
        return ObjectType::Camera;
    }
    if (obj.hasPostFX) {
        return ObjectType::PostFXNode;
    }
    return ObjectType::Empty;
}

bool SceneSerializer::loadScene(const fs::path& filePath,
                               std::vector<SceneObject>& objects,
                               int& nextId,
                               int& outVersion,
                               float* outTimeOfDay) {
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) return false;

        objects.clear();
        std::string line;
        SceneObject* currentObj = nullptr;
        int sceneVersion = 9;
        float sceneTimeOfDay = -1.0f;

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
            } else if (currentObj) {
                const auto& handlers = GetSceneObjectKeyHandlers();
                auto handlerIt = handlers.find(key);
                if (handlerIt != handlers.end()) {
                    handlerIt->second(*currentObj, value);
                } else if (key.rfind("animKey", 0) == 0) {
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

        file.close();
        for (auto& obj : objects) {
            obj.type = GetLegacyTypeFromComponents(obj);
        }
        outVersion = sceneVersion;
        if (outTimeOfDay) {
            *outTimeOfDay = sceneTimeOfDay;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load scene: " << e.what() << std::endl;
        return false;
    }
}

bool SceneSerializer::loadSceneDeferred(const fs::path& filePath,
                                       std::vector<SceneObject>& objects,
                                       int& nextId,
                                       int& outVersion,
                                       float* outTimeOfDay) {
    struct DeferGuard {
        bool previous = false;
        explicit DeferGuard(bool enable) {
            previous = g_deferSceneAssetLoading;
            g_deferSceneAssetLoading = enable;
        }
        ~DeferGuard() {
            g_deferSceneAssetLoading = previous;
        }
    };

    DeferGuard guard(true);
    return loadScene(filePath, objects, nextId, outVersion, outTimeOfDay);
}
