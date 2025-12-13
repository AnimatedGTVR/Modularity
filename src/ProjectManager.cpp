#include "ProjectManager.h"
#include "Rendering.h"
#include "ModelLoader.h"

// Project implementation
Project::Project(const std::string& projectName, const fs::path& basePath)
    : name(projectName) {
    projectPath = basePath / projectName;
    scenesPath = projectPath / "Scenes";
    assetsPath = projectPath / "Assets";
    scriptsPath = projectPath / "Scripts";
    scriptsConfigPath = projectPath / "Scripts.modu";
}

bool Project::create() {
    try {
        fs::create_directories(projectPath);
        fs::create_directories(scenesPath);
        fs::create_directories(assetsPath);
        fs::create_directories(assetsPath / "Textures");
        fs::create_directories(assetsPath / "Models");
        fs::create_directories(assetsPath / "Shaders");
        fs::create_directories(scriptsPath);
        fs::create_directories(projectPath / "Cache" / "ScriptBin");

        saveProjectFile();

        // Initialize a default scripting build file
        fs::path engineRoot = fs::current_path();
        std::ofstream scriptCfg(scriptsConfigPath);
        scriptCfg << "# Scripts.modu\n";
        scriptCfg << "cppStandard=c++20\n";
        scriptCfg << "scriptsDir=Scripts\n";
        scriptCfg << "outDir=Cache/ScriptBin\n";
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

        currentSceneName = "Main";
        isLoaded = true;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create project: " << e.what() << std::endl;
        return false;
    }
}

bool Project::load(const fs::path& projectFilePath) {
    try {
        projectPath = projectFilePath.parent_path();
        scenesPath = projectPath / "Scenes";
        assetsPath = projectPath / "Assets";
        scriptsPath = projectPath / "Scripts";
        scriptsConfigPath = projectPath / "Scripts.modu";

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
        file << "version=4\n";
        file << "nextId=" << nextId << "\n";
        file << "objectCount=" << objects.size() << "\n";
        file << "\n";

        for (const auto& obj : objects) {
            file << "[Object]\n";
            file << "id=" << obj.id << "\n";
            file << "name=" << obj.name << "\n";
            file << "type=" << static_cast<int>(obj.type) << "\n";
            file << "parentId=" << obj.parentId << "\n";
            file << "position=" << obj.position.x << "," << obj.position.y << "," << obj.position.z << "\n";
            file << "rotation=" << obj.rotation.x << "," << obj.rotation.y << "," << obj.rotation.z << "\n";
            file << "scale=" << obj.scale.x << "," << obj.scale.y << "," << obj.scale.z << "\n";
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
            file << "scripts=" << obj.scripts.size() << "\n";
            for (size_t si = 0; si < obj.scripts.size(); ++si) {
                const auto& sc = obj.scripts[si];
                file << "script" << si << "_path=" << sc.path << "\n";
                file << "script" << si << "_settings=" << sc.settings.size() << "\n";
                for (size_t k = 0; k < sc.settings.size(); ++k) {
                    file << "script" << si << "_setting" << k << "=" << sc.settings[k].key << ":" << sc.settings[k].value << "\n";
                }
            }
            file << "lightColor=" << obj.light.color.r << "," << obj.light.color.g << "," << obj.light.color.b << "\n";
            file << "lightIntensity=" << obj.light.intensity << "\n";
            file << "lightRange=" << obj.light.range << "\n";
            file << "lightInner=" << obj.light.innerAngle << "\n";
            file << "lightOuter=" << obj.light.outerAngle << "\n";
            file << "lightSize=" << obj.light.size.x << "," << obj.light.size.y << "\n";
            file << "lightEnabled=" << (obj.light.enabled ? 1 : 0) << "\n";
            file << "cameraType=" << static_cast<int>(obj.camera.type) << "\n";
            file << "cameraFov=" << obj.camera.fov << "\n";
            file << "cameraNear=" << obj.camera.nearClip << "\n";
            file << "cameraFar=" << obj.camera.farClip << "\n";
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
            }

            file << "scriptCount=" << obj.scripts.size() << "\n";
            for (size_t s = 0; s < obj.scripts.size(); ++s) {
                const auto& sc = obj.scripts[s];
                file << "script" << s << "_path=" << sc.path << "\n";
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
                               int& nextId) {
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) return false;

        objects.clear();
        std::string line;
        SceneObject* currentObj = nullptr;

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

            if (key == "nextId") {
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
                } else if (key == "parentId") {
                    currentObj->parentId = std::stoi(value);
                } else if (key == "position") {
                    sscanf(value.c_str(), "%f,%f,%f",
                           &currentObj->position.x,
                           &currentObj->position.y,
                           &currentObj->position.z);
                } else if (key == "rotation") {
                    sscanf(value.c_str(), "%f,%f,%f",
                           &currentObj->rotation.x,
                           &currentObj->rotation.y,
                           &currentObj->rotation.z);
                    currentObj->rotation = NormalizeEulerDegrees(currentObj->rotation);
                } else if (key == "scale") {
                    sscanf(value.c_str(), "%f,%f,%f",
                           &currentObj->scale.x,
                           &currentObj->scale.y,
                           &currentObj->scale.z);
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
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load scene: " << e.what() << std::endl;
        return false;
    }
}
