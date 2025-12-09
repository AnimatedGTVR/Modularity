#include "Engine.h"
#include "ModelLoader.h"
#include <iostream>
#include <fstream>

void window_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

SceneObject* Engine::getSelectedObject() {
    if (selectedObjectId == -1) return nullptr;
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
        [this](const SceneObject& obj) { return obj.id == selectedObjectId; });
    return (it != sceneObjects.end()) ? &(*it) : nullptr;
}

void Engine::DecomposeMatrix(const glm::mat4& matrix, glm::vec3& pos, glm::vec3& rot, glm::vec3& scale) {
    pos = glm::vec3(matrix[3]);
    scale.x = glm::length(glm::vec3(matrix[0]));
    scale.y = glm::length(glm::vec3(matrix[1]));
    scale.z = glm::length(glm::vec3(matrix[2]));

    glm::mat3 rotMat(matrix);
    if (scale.x != 0.0f) rotMat[0] /= scale.x;
    if (scale.y != 0.0f) rotMat[1] /= scale.y;
    if (scale.z != 0.0f) rotMat[2] /= scale.z;

    rot = glm::eulerAngles(glm::quat_cast(rotMat));
}

bool Engine::init() {
    std::cerr << "[DEBUG] Creating window..." << std::endl;
    editorWindow = window.makeWindow();
    if (!editorWindow) {
        std::cerr << "[DEBUG] Window creation failed!" << std::endl;
        return false;
    }
    std::cerr << "[DEBUG] Window created successfully" << std::endl;

    glfwSetWindowUserPointer(editorWindow, this);
    glfwSetWindowSizeCallback(editorWindow, window_size_callback);

    auto mouse_cb = [](GLFWwindow* window, double xpos, double ypos) {
        auto* engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
        if (!engine) return;

        int cursorMode = glfwGetInputMode(window, GLFW_CURSOR);
        if (!engine->viewportController.isViewportFocused() || cursorMode != GLFW_CURSOR_DISABLED) {
            return;
        }

        engine->camera.processMouse(xpos, ypos);
    };
    glfwSetCursorPosCallback(editorWindow, mouse_cb);

    std::cerr << "[DEBUG] Setting up ImGui..." << std::endl;
    setupImGui();
    std::cerr << "[DEBUG] ImGui setup complete" << std::endl;
    
    logToConsole("Engine initialized - Waiting for project selection");
    return true;
}

bool Engine::initRenderer() {
    if (rendererInitialized) return true;

    try {
        renderer.initialize();
        rendererInitialized = true;
        return true;
    } catch (...) {
        return false;
    }
}

void Engine::run() {
    std::cerr << "[DEBUG] Entering main loop, showLauncher=" << showLauncher << std::endl;
    
    while (!glfwWindowShouldClose(editorWindow)) {
        if (glfwGetWindowAttrib(editorWindow, GLFW_ICONIFIED)) {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        deltaTime = std::min(deltaTime, 1.0f / 30.0f);

        glfwPollEvents();

        if (!showLauncher) {
            handleKeyboardShortcuts();
        }

        viewportController.update(editorWindow, cursorLocked);

        if (!viewportController.isViewportFocused() && cursorLocked) {
            cursorLocked = false;
            glfwSetInputMode(editorWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            camera.firstMouse = true;
        }

        if (viewportController.isViewportFocused() && cursorLocked) {
            camera.processKeyboard(deltaTime, editorWindow);
        }

        if (!showLauncher && projectManager.currentProject.isLoaded && rendererInitialized) {
            glm::mat4 view = camera.getViewMatrix();
            float aspect = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
            if (aspect <= 0.0f) aspect = 1.0f;
            glm::mat4 proj = glm::perspective(glm::radians(FOV), aspect, NEAR_PLANE, FAR_PLANE);

            renderer.beginRender(view, proj, camera.position);
            renderer.renderScene(camera, sceneObjects);
            renderer.endRender();
        }

        if (firstFrame) {
            std::cerr << "[DEBUG] First frame: starting ImGui NewFrame" << std::endl;
        }
        
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (firstFrame) {
            std::cerr << "[DEBUG] First frame: ImGui NewFrame complete, rendering UI..." << std::endl;
        }

        if (showLauncher) {
            if (firstFrame) {
                std::cerr << "[DEBUG] First frame: calling renderLauncher()" << std::endl;
            }
            renderLauncher();
        } else {
            setupDockspace();
            renderMainMenuBar();

            if (!viewportFullscreen) {
                if (showHierarchy) renderHierarchyPanel();
                if (showInspector) renderInspectorPanel();
                if (showFileBrowser) renderFileBrowserPanel();
                if (showConsole) renderConsolePanel();
                if (showProjectBrowser) renderProjectBrowserPanel();
            }

            renderViewport();
            renderDialogs();
        }

        if (firstFrame) {
            std::cerr << "[DEBUG] First frame: UI rendering complete, finalizing frame..." << std::endl;
        }

        int displayW, displayH;
        glfwGetFramebufferSize(editorWindow, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.1f, 0.1f, 0.12f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(editorWindow);
        
        if (firstFrame) {
            std::cerr << "[DEBUG] First frame complete!" << std::endl;
        }
        firstFrame = false;
    }
    
    std::cerr << "[DEBUG] Exiting main loop" << std::endl;
}

void Engine::shutdown() {
    if (projectManager.currentProject.isLoaded && projectManager.currentProject.hasUnsavedChanges) {
        saveCurrentScene();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
}

void Engine::importOBJToScene(const std::string& filepath, const std::string& objectName) {
    std::string errorMsg;
    int meshId = g_objLoader.loadOBJ(filepath, errorMsg);
    
    if (meshId < 0) {
        addConsoleMessage("Failed to load OBJ: " + errorMsg, ConsoleMessageType::Error);
        return;
    }
    
    int id = nextObjectId++;
    std::string name = objectName.empty() ? fs::path(filepath).stem().string() : objectName;
    
    SceneObject obj(name, ObjectType::OBJMesh, id);
    obj.meshPath = filepath;
    obj.meshId = meshId;
    
    sceneObjects.push_back(obj);
    selectedObjectId = id;
    
    if (projectManager.currentProject.isLoaded) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }
    
    const auto* meshInfo = g_objLoader.getMeshInfo(meshId);
    if (meshInfo) {
        addConsoleMessage("Imported OBJ: " + name + " (" + 
                        std::to_string(meshInfo->vertexCount) + " vertices, " +
                        std::to_string(meshInfo->faceCount) + " faces)", 
                        ConsoleMessageType::Success);
    } else {
        addConsoleMessage("Imported OBJ: " + name, ConsoleMessageType::Success);
    }
}

void Engine::importModelToScene(const std::string& filepath, const std::string& objectName) {
    auto& modelLoader = getModelLoader();
    ModelLoadResult result = modelLoader.loadModel(filepath);

    if (!result.success) {
        addConsoleMessage("Failed to load model: " + result.errorMessage, ConsoleMessageType::Error);
        return;
    }

    int id = nextObjectId++;
    std::string name = objectName.empty() ? fs::path(filepath).stem().string() : objectName;

    SceneObject obj(name, ObjectType::Model, id);
    obj.meshPath = filepath;
    obj.meshId = result.meshIndex;

    sceneObjects.push_back(obj);
    selectedObjectId = id;

    if (projectManager.currentProject.isLoaded) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }

    addConsoleMessage(
        "Imported model: " + name + " (" +
        std::to_string(result.vertexCount) + " verts, " +
        std::to_string(result.faceCount) + " faces, " +
        std::to_string(result.meshCount) + " meshes)",
        ConsoleMessageType::Success
    );
}

void Engine::loadMaterialFromFile(SceneObject& obj) {
    if (obj.materialPath.empty()) return;
    try {
        std::ifstream f(obj.materialPath);
        if (!f.is_open()) {
            addConsoleMessage("Failed to open material: " + obj.materialPath, ConsoleMessageType::Error);
            return;
        }
        std::string line;
        while (std::getline(f, line)) {
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            if (line.empty() || line[0] == '#') continue;
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            if (key == "color") {
                sscanf(val.c_str(), "%f,%f,%f", &obj.material.color.r, &obj.material.color.g, &obj.material.color.b);
            } else if (key == "ambient") {
                obj.material.ambientStrength = std::stof(val);
            } else if (key == "specular") {
                obj.material.specularStrength = std::stof(val);
            } else if (key == "shininess") {
                obj.material.shininess = std::stof(val);
            } else if (key == "textureMix") {
                obj.material.textureMix = std::stof(val);
            } else if (key == "albedo") {
                obj.albedoTexturePath = val;
            } else if (key == "overlay") {
                obj.overlayTexturePath = val;
            } else if (key == "normal") {
                obj.normalMapPath = val;
            } else if (key == "useOverlay") {
                obj.useOverlay = std::stoi(val) != 0;
            }
        }
        addConsoleMessage("Applied material: " + obj.materialPath, ConsoleMessageType::Success);
        projectManager.currentProject.hasUnsavedChanges = true;
    } catch (...) {
        addConsoleMessage("Failed to read material: " + obj.materialPath, ConsoleMessageType::Error);
    }
}

void Engine::saveMaterialToFile(const SceneObject& obj) {
    if (obj.materialPath.empty()) {
        addConsoleMessage("Material path is empty", ConsoleMessageType::Warning);
        return;
    }
    try {
        std::ofstream f(obj.materialPath);
        if (!f.is_open()) {
            addConsoleMessage("Failed to open material for writing: " + obj.materialPath, ConsoleMessageType::Error);
            return;
        }
        f << "# Material\n";
        f << "color=" << obj.material.color.r << "," << obj.material.color.g << "," << obj.material.color.b << "\n";
        f << "ambient=" << obj.material.ambientStrength << "\n";
        f << "specular=" << obj.material.specularStrength << "\n";
        f << "shininess=" << obj.material.shininess << "\n";
        f << "textureMix=" << obj.material.textureMix << "\n";
        f << "useOverlay=" << (obj.useOverlay ? 1 : 0) << "\n";
        f << "albedo=" << obj.albedoTexturePath << "\n";
        f << "overlay=" << obj.overlayTexturePath << "\n";
        f << "normal=" << obj.normalMapPath << "\n";
        addConsoleMessage("Saved material: " + obj.materialPath, ConsoleMessageType::Success);
    } catch (...) {
        addConsoleMessage("Failed to save material: " + obj.materialPath, ConsoleMessageType::Error);
    }
}

void Engine::handleKeyboardShortcuts() {
    static bool f11Pressed = false;
    if (glfwGetKey(editorWindow, GLFW_KEY_F11) == GLFW_PRESS && !f11Pressed) {
        viewportFullscreen = !viewportFullscreen;
        f11Pressed = true;
    }
    if (glfwGetKey(editorWindow, GLFW_KEY_F11) == GLFW_RELEASE) {
        f11Pressed = false;
    }

    static bool ctrlSPressed = false;
    bool ctrlDown = glfwGetKey(editorWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                   glfwGetKey(editorWindow, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

    if (ctrlDown && glfwGetKey(editorWindow, GLFW_KEY_S) == GLFW_PRESS && !ctrlSPressed) {
        if (projectManager.currentProject.isLoaded) {
            saveCurrentScene();
        }
        ctrlSPressed = true;
    }
    if (glfwGetKey(editorWindow, GLFW_KEY_S) == GLFW_RELEASE) {
        ctrlSPressed = false;
    }

    static bool ctrlNPressed = false;
    if (ctrlDown && glfwGetKey(editorWindow, GLFW_KEY_N) == GLFW_PRESS && !ctrlNPressed) {
        if (projectManager.currentProject.isLoaded) {
            showNewSceneDialog = true;
            memset(newSceneName, 0, sizeof(newSceneName));
        }
        ctrlNPressed = true;
    }
    if (glfwGetKey(editorWindow, GLFW_KEY_N) == GLFW_RELEASE) {
        ctrlNPressed = false;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Q)) mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
    if (ImGui::IsKeyPressed(ImGuiKey_W)) mCurrentGizmoOperation = ImGuizmo::ROTATE;
    if (ImGui::IsKeyPressed(ImGuiKey_E)) mCurrentGizmoOperation = ImGuizmo::SCALE;
    if (ImGui::IsKeyPressed(ImGuiKey_R)) mCurrentGizmoOperation = ImGuizmo::UNIVERSAL;

    if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
        mCurrentGizmoMode = (mCurrentGizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_LeftCtrl)) useSnap = !useSnap;
}

void Engine::OpenProjectPath(const std::string& path) {
    if (projectManager.loadProject(path)) {
        if (!initRenderer()) {
            addConsoleMessage("Error: Failed to initialize renderer!", ConsoleMessageType::Error);
        } else {
            showLauncher = false;
            loadRecentScenes();
            addConsoleMessage("Opened project: " + projectManager.currentProject.name, ConsoleMessageType::Info);
        }
    } else {
        addConsoleMessage("Error opening project: " + projectManager.errorMessage, ConsoleMessageType::Error);
    }
}

void Engine::createNewProject(const char* name, const char* location) {
    fs::path basePath(location);
    fs::create_directories(basePath);

    Project newProject(name, basePath);
    if (newProject.create()) {
        projectManager.currentProject = newProject;
        projectManager.addToRecentProjects(name,
                                          (newProject.projectPath / "project.modu").string());

        if (!initRenderer()) {
            logToConsole("Error: Failed to initialize renderer!");
            return;
        }

        sceneObjects.clear();
        selectedObjectId = -1;
        nextObjectId = 0;

        addObject(ObjectType::Cube, "Cube");

        fileBrowser.currentPath = projectManager.currentProject.assetsPath;
        fileBrowser.needsRefresh = true;

        showLauncher = false;
        firstFrame = true;

        addConsoleMessage("Created new project: " + std::string(name), ConsoleMessageType::Success);
        addConsoleMessage("Project location: " + newProject.projectPath.string(), ConsoleMessageType::Info);

        saveCurrentScene();
    } else {
        projectManager.errorMessage = "Failed to create project directory";
    }
}

void Engine::loadRecentScenes() {
    sceneObjects.clear();
    selectedObjectId = -1;
    nextObjectId = 0;

    fs::path scenePath = projectManager.currentProject.getSceneFilePath(projectManager.currentProject.currentSceneName);
    if (fs::exists(scenePath)) {
        if (SceneSerializer::loadScene(scenePath, sceneObjects, nextObjectId)) {
            addConsoleMessage("Loaded scene: " + projectManager.currentProject.currentSceneName, ConsoleMessageType::Success);
        } else {
            addConsoleMessage("Warning: Failed to load scene, starting fresh", ConsoleMessageType::Warning);
            addObject(ObjectType::Cube, "Cube");
        }
    } else {
        addConsoleMessage("Default scene not found, starting with a new scene.", ConsoleMessageType::Info);
        addObject(ObjectType::Cube, "Cube");
    }

    fileBrowser.currentPath = projectManager.currentProject.assetsPath;
    fileBrowser.needsRefresh = true;
}

void Engine::saveCurrentScene() {
    if (!projectManager.currentProject.isLoaded) return;

    fs::path scenePath = projectManager.currentProject.getSceneFilePath(projectManager.currentProject.currentSceneName);
    if (SceneSerializer::saveScene(scenePath, sceneObjects, nextObjectId)) {
        projectManager.currentProject.hasUnsavedChanges = false;
        projectManager.currentProject.saveProjectFile();
        addConsoleMessage("Saved scene: " + projectManager.currentProject.currentSceneName, ConsoleMessageType::Success);
    } else {
        addConsoleMessage("Error: Failed to save scene!", ConsoleMessageType::Error);
    }
}

void Engine::loadScene(const std::string& sceneName) {
    if (!projectManager.currentProject.isLoaded) return;

    if (projectManager.currentProject.hasUnsavedChanges) {
        saveCurrentScene();
    }

    fs::path scenePath = projectManager.currentProject.getSceneFilePath(sceneName);
    if (SceneSerializer::loadScene(scenePath, sceneObjects, nextObjectId)) {
        projectManager.currentProject.currentSceneName = sceneName;
        projectManager.currentProject.hasUnsavedChanges = false;
        projectManager.currentProject.saveProjectFile();
        selectedObjectId = -1;
        bool hasDirLight = std::any_of(sceneObjects.begin(), sceneObjects.end(), [](const SceneObject& o) {
            return o.type == ObjectType::DirectionalLight;
        });
        if (!hasDirLight) {
            addObject(ObjectType::DirectionalLight, "Directional Light");
        }
        addConsoleMessage("Loaded scene: " + sceneName, ConsoleMessageType::Success);
    } else {
        addConsoleMessage("Error: Failed to load scene: " + sceneName, ConsoleMessageType::Error);
    }
}

void Engine::createNewScene(const std::string& sceneName) {
    if (!projectManager.currentProject.isLoaded || sceneName.empty()) return;

    if (projectManager.currentProject.hasUnsavedChanges) {
        saveCurrentScene();
    }

    sceneObjects.clear();
    selectedObjectId = -1;
    nextObjectId = 0;

    projectManager.currentProject.currentSceneName = sceneName;
    projectManager.currentProject.hasUnsavedChanges = true;

    addObject(ObjectType::Cube, "Cube");
    addObject(ObjectType::DirectionalLight, "Directional Light");
    saveCurrentScene();

    addConsoleMessage("Created new scene: " + sceneName, ConsoleMessageType::Success);
}

void Engine::addObject(ObjectType type, const std::string& baseName) {
    int id = nextObjectId++;
    std::string name = baseName + " " + std::to_string(id);
    sceneObjects.push_back(SceneObject(name, type, id));
    // Light defaults
    if (type == ObjectType::PointLight) {
        sceneObjects.back().light.type = LightType::Point;
        sceneObjects.back().light.range = 12.0f;
        sceneObjects.back().light.intensity = 2.0f;
    } else if (type == ObjectType::SpotLight) {
        sceneObjects.back().light.type = LightType::Spot;
        sceneObjects.back().light.range = 15.0f;
        sceneObjects.back().light.intensity = 2.5f;
    } else if (type == ObjectType::AreaLight) {
        sceneObjects.back().light.type = LightType::Area;
        sceneObjects.back().light.range = 10.0f;
        sceneObjects.back().light.intensity = 3.0f;
        sceneObjects.back().light.size = glm::vec2(2.0f, 2.0f);
    }
    selectedObjectId = id;
    if (projectManager.currentProject.isLoaded) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }
    logToConsole("Created: " + name);
}

void Engine::duplicateSelected() {
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
        [this](const SceneObject& obj) { return obj.id == selectedObjectId; });

    if (it != sceneObjects.end()) {
        int id = nextObjectId++;
        SceneObject newObj(it->name + " (Copy)", it->type, id);
        newObj.position = it->position + glm::vec3(1.0f, 0.0f, 0.0f);
        newObj.rotation = it->rotation;
        newObj.scale = it->scale;
        newObj.meshPath = it->meshPath;
        newObj.meshId = it->meshId;
        newObj.material = it->material;
        newObj.materialPath = it->materialPath;
        newObj.albedoTexturePath = it->albedoTexturePath;
        newObj.overlayTexturePath = it->overlayTexturePath;
        newObj.normalMapPath = it->normalMapPath;
        newObj.useOverlay = it->useOverlay;
        newObj.light = it->light;
        
        sceneObjects.push_back(newObj);
        selectedObjectId = id;
        if (projectManager.currentProject.isLoaded) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        logToConsole("Duplicated: " + newObj.name);
    }
}

void Engine::deleteSelected() {
    auto it = std::remove_if(sceneObjects.begin(), sceneObjects.end(),
        [this](const SceneObject& obj) { return obj.id == selectedObjectId; });

    if (it != sceneObjects.end()) {
        logToConsole("Deleted object");
        sceneObjects.erase(it, sceneObjects.end());
        selectedObjectId = -1;
        if (projectManager.currentProject.isLoaded) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
    }
}

void Engine::setParent(int childId, int parentId) {
    auto childIt = std::find_if(sceneObjects.begin(), sceneObjects.end(),
        [childId](const SceneObject& obj) { return obj.id == childId; });

    if (childIt == sceneObjects.end()) return;

    if (childIt->parentId != -1) {
        auto oldParentIt = std::find_if(sceneObjects.begin(), sceneObjects.end(),
            [&childIt](const SceneObject& obj) { return obj.id == childIt->parentId; });
        if (oldParentIt != sceneObjects.end()) {
            auto& children = oldParentIt->childIds;
            children.erase(std::remove(children.begin(), children.end(), childId), children.end());
        }
    }

    childIt->parentId = parentId;

    if (parentId != -1) {
        auto newParentIt = std::find_if(sceneObjects.begin(), sceneObjects.end(),
            [parentId](const SceneObject& obj) { return obj.id == parentId; });
        if (newParentIt != sceneObjects.end()) {
            newParentIt->childIds.push_back(childId);
        }
    }

    if (projectManager.currentProject.isLoaded) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }
    logToConsole("Reparented object");
}

void Engine::addConsoleMessage(const std::string& message, ConsoleMessageType type) {
    std::string prefix;
    switch (type) {
        case ConsoleMessageType::Info: prefix = "Info: "; break;
        case ConsoleMessageType::Warning: prefix = "Warning: "; break;
        case ConsoleMessageType::Error: prefix = "Error: "; break;
        case ConsoleMessageType::Success: prefix = "Success: "; break;
    }

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::string timeStr = std::ctime(&time);
    timeStr = timeStr.substr(11, 8);

    consoleLog.push_back("[" + timeStr + "] " + prefix + " " + message);
    if (consoleLog.size() > 1000) {
        consoleLog.erase(consoleLog.begin());
    }
}

void Engine::logToConsole(const std::string& message) {
    addConsoleMessage(message, ConsoleMessageType::Info);
}

void Engine::setupImGui() {
    std::cerr << "[DEBUG] setupImGui: getting primary monitor..." << std::endl;
    float mainScale = 1.0f;
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    if (primaryMonitor) {
        std::cerr << "[DEBUG] setupImGui: got primary monitor, getting content scale..." << std::endl;
        mainScale = ImGui_ImplGlfw_GetContentScaleForMonitor(primaryMonitor);
        std::cerr << "[DEBUG] setupImGui: content scale = " << mainScale << std::endl;
    } else {
        std::cerr << "[DEBUG] setupImGui: WARNING - no primary monitor found!" << std::endl;
    }
    
    std::cerr << "[DEBUG] setupImGui: creating ImGui context..." << std::endl;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    #ifndef __linux__
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    #endif

    std::cerr << "[DEBUG] setupImGui: applying theme..." << std::endl;
    applyModernTheme();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(mainScale);
    style.FontScaleDpi = mainScale;

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    std::cerr << "[DEBUG] setupImGui: initializing ImGui GLFW backend..." << std::endl;
    ImGui_ImplGlfw_InitForOpenGL(editorWindow, true);
    
    std::cerr << "[DEBUG] setupImGui: initializing ImGui OpenGL3 backend..." << std::endl;
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        std::cerr << "[DEBUG] ImGui OpenGL3 init failed!" << std::endl;
        throw std::runtime_error("ImGui error");
    }
    std::cerr << "[DEBUG] setupImGui: complete!" << std::endl;
}
