#include "ProjectManager.h"
#include "Rendering.h"

// Project implementation
Project::Project(const std::string& projectName, const fs::path& basePath)
    : name(projectName) {
    projectPath = basePath / projectName;
    scenesPath = projectPath / "Scenes";
    assetsPath = projectPath / "Assets";
    scriptsPath = projectPath / "Scripts";
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

        saveProjectFile();

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
        file << "version=2\n";
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
            
            if (obj.type == ObjectType::OBJMesh && !obj.meshPath.empty()) {
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
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

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
                } else if (key == "scale") {
                    sscanf(value.c_str(), "%f,%f,%f",
                           &currentObj->scale.x,
                           &currentObj->scale.y,
                           &currentObj->scale.z);
                } else if (key == "meshPath") {
                    currentObj->meshPath = value;
                    if (!value.empty() && currentObj->type == ObjectType::OBJMesh) {
                        std::string err;
                        currentObj->meshId = g_objLoader.loadOBJ(value, err);
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
