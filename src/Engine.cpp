#include "Engine.h"
#include "ModelLoader.h"
#include <iostream>
#include <fstream>

namespace {
struct MaterialFileData {
    MaterialProperties props;
    std::string albedo;
    std::string overlay;
    std::string normal;
    bool useOverlay = false;
    std::string vertexShader;
    std::string fragmentShader;
};

bool readMaterialFile(const std::string& path, MaterialFileData& outData) {
    std::ifstream f(path);
    if (!f.is_open()) {
        return false;
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
            sscanf(val.c_str(), "%f,%f,%f", &outData.props.color.r, &outData.props.color.g, &outData.props.color.b);
        } else if (key == "ambient") {
            outData.props.ambientStrength = std::stof(val);
        } else if (key == "specular") {
            outData.props.specularStrength = std::stof(val);
        } else if (key == "shininess") {
            outData.props.shininess = std::stof(val);
        } else if (key == "textureMix") {
            outData.props.textureMix = std::stof(val);
        } else if (key == "albedo") {
            outData.albedo = val;
        } else if (key == "overlay") {
            outData.overlay = val;
        } else if (key == "normal") {
            outData.normal = val;
        } else if (key == "useOverlay") {
            outData.useOverlay = std::stoi(val) != 0;
        } else if (key == "vertexShader") {
            outData.vertexShader = val;
        } else if (key == "fragmentShader") {
            outData.fragmentShader = val;
        }
    }
    return true;
}

bool writeMaterialFile(const MaterialFileData& data, const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) {
        return false;
    }
    f << "# Material\n";
    f << "color=" << data.props.color.r << "," << data.props.color.g << "," << data.props.color.b << "\n";
    f << "ambient=" << data.props.ambientStrength << "\n";
    f << "specular=" << data.props.specularStrength << "\n";
    f << "shininess=" << data.props.shininess << "\n";
    f << "textureMix=" << data.props.textureMix << "\n";
    f << "useOverlay=" << (data.useOverlay ? 1 : 0) << "\n";
    f << "albedo=" << data.albedo << "\n";
    f << "overlay=" << data.overlay << "\n";
    f << "normal=" << data.normal << "\n";
    f << "vertexShader=" << data.vertexShader << "\n";
    f << "fragmentShader=" << data.fragmentShader << "\n";
    return true;
}
} // namespace

void window_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

SceneObject* Engine::getSelectedObject() {
    if (selectedObjectId == -1) return nullptr;
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
        [this](const SceneObject& obj) { return obj.id == selectedObjectId; });
    return (it != sceneObjects.end()) ? &(*it) : nullptr;
}

glm::vec3 Engine::getSelectionCenterWorld(bool worldSpace) const {
    if (selectedObjectIds.empty()) return glm::vec3(0.0f);
    glm::vec3 acc(0.0f);
    int count = 0;
    auto findObj = [&](int id) -> const SceneObject* {
        auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(), [id](const SceneObject& o){ return o.id == id; });
        return it == sceneObjects.end() ? nullptr : &(*it);
    };
    for (int id : selectedObjectIds) {
        const SceneObject* o = findObj(id);
        if (!o) continue;
        acc += worldSpace ? o->position : glm::vec3(0.0f);
        count++;
    }
    if (count == 0) return glm::vec3(0.0f);
    return acc / (float)count;
}

void Engine::setPrimarySelection(int id, bool additive) {
    if (!additive) {
        selectedObjectIds.clear();
    }
    if (id >= 0) {
        selectedObjectIds.push_back(id);
        selectedObjectId = id;
    } else {
        selectedObjectIds.clear();
        selectedObjectId = -1;
    }
}

void Engine::clearSelection() {
    selectedObjectIds.clear();
    selectedObjectId = -1;
}

Camera Engine::makeCameraFromObject(const SceneObject& obj) const {
    Camera cam;
    cam.position = obj.position;
    glm::quat q = glm::quat(glm::radians(obj.rotation));
    glm::mat3 rot = glm::mat3_cast(q);
    cam.front = glm::normalize(rot * glm::vec3(0.0f, 0.0f, -1.0f));
    cam.up = glm::normalize(rot * glm::vec3(0.0f, 1.0f, 0.0f));
    if (!std::isfinite(cam.front.x) || glm::length(cam.front) < 1e-3f) {
        cam.front = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    if (!std::isfinite(cam.up.x) || glm::length(cam.up) < 1e-3f) {
        cam.up = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return cam;
}

namespace {
// Equivalent to glm::extractEulerAngleXYZ without depending on the experimental header.
glm::vec3 ExtractEulerXYZ(const glm::mat3& m) {
    float T1 = std::atan2(m[2][1], m[2][2]);
    float C2 = std::sqrt(m[0][0] * m[0][0] + m[1][0] * m[1][0]);
    float T2 = std::atan2(-m[2][0], C2);
    float S1 = std::sin(T1);
    float C1 = std::cos(T1);
    float T3 = std::atan2(S1 * m[0][2] - C1 * m[0][1], C1 * m[1][1] - S1 * m[1][2]);
    // GLM's extractEulerAngleXYZ returns (-T1, -T2, -T3)
    return glm::vec3(-T1, -T2, -T3);
}
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

    // Use explicit XYZ extraction so yaw isn't clamped to [-90, 90] like glm::yaw/pitch/roll.
    rot = ExtractEulerXYZ(rotMat);
}

void Engine::recordState(const char* /*reason*/) {
    SceneSnapshot snap;
    snap.objects = sceneObjects;
    snap.selectedIds = selectedObjectIds;
    snap.nextId = nextObjectId;

    undoStack.push_back(std::move(snap));
    if (undoStack.size() > 64) {
        undoStack.erase(undoStack.begin());
    }
    redoStack.clear();
}

void Engine::undo() {
    if (undoStack.empty()) return;

    SceneSnapshot current;
    current.objects = sceneObjects;
    current.selectedIds = selectedObjectIds;
    current.nextId = nextObjectId;

    SceneSnapshot snap = undoStack.back();
    undoStack.pop_back();

    redoStack.push_back(std::move(current));
    sceneObjects = std::move(snap.objects);
    selectedObjectIds = snap.selectedIds;
    selectedObjectId = selectedObjectIds.empty() ? -1 : selectedObjectIds.back();
    nextObjectId = snap.nextId;
    projectManager.currentProject.hasUnsavedChanges = true;
}

void Engine::redo() {
    if (redoStack.empty()) return;

    SceneSnapshot current;
    current.objects = sceneObjects;
    current.selectedIds = selectedObjectIds;
    current.nextId = nextObjectId;

    SceneSnapshot snap = redoStack.back();
    redoStack.pop_back();

    undoStack.push_back(std::move(current));
    sceneObjects = std::move(snap.objects);
    selectedObjectIds = snap.selectedIds;
    selectedObjectId = selectedObjectIds.empty() ? -1 : selectedObjectIds.back();
    nextObjectId = snap.nextId;
    projectManager.currentProject.hasUnsavedChanges = true;
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

    if (!audio.init()) {
        std::cerr << "[DEBUG] Audio init failed\n";
        addConsoleMessage("Audio initialization failed. Audio playback will be disabled.", ConsoleMessageType::Warning);
    }
    
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

        if (gameViewCursorLocked) {
            cursorLocked = false;
            viewportController.setFocused(false);
        }
        viewportController.update(editorWindow, cursorLocked);
        if (!isPlaying) {
            gameViewCursorLocked = false;
        }

        // Scroll-wheel speed adjustment while freelook is active
        if (viewportController.isViewportFocused() && cursorLocked) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (std::abs(wheel) > 0.0001f) {
                float factor = std::pow(1.12f, wheel);
                float ratio = (camera.moveSpeed > 0.001f) ? (camera.sprintSpeed / camera.moveSpeed) : 2.0f;
                camera.moveSpeed = std::clamp(camera.moveSpeed * factor, 0.5f, 100.0f);
                camera.sprintSpeed = std::clamp(camera.moveSpeed * ratio, 0.5f, 200.0f);
            }
        }

        if (viewportController.isViewportFocused() && cursorLocked) {
            camera.processKeyboard(deltaTime, editorWindow);
        }

        // Run scripts only in play/spec/test modes to avoid edit-time side effects (e.g., cursor grabs)
        if (projectManager.currentProject.isLoaded) {
            bool runScripts = isPlaying || specMode || testMode;
            if (runScripts) {
                updateScripts(deltaTime);
            }
        }

        if (isPlaying) {
            updatePlayerController(deltaTime);
        }

        bool simulatePhysics = physics.isReady() && ((isPlaying && !isPaused) || (!isPlaying && specMode));
        if (simulatePhysics) {
            physics.simulate(deltaTime, sceneObjects);
        }

        bool audioShouldPlay = isPlaying || specMode || testMode;
        Camera listenerCamera = camera;
        for (const auto& obj : sceneObjects) {
            if (obj.type == ObjectType::Camera && obj.camera.type == SceneCameraType::Player) {
                listenerCamera = makeCameraFromObject(obj);
                listenerCamera.position = obj.position;
                break;
            }
        }
        audio.update(sceneObjects, listenerCamera, audioShouldPlay);

        if (!showLauncher && projectManager.currentProject.isLoaded && rendererInitialized) {
            glm::mat4 view = camera.getViewMatrix();
            float aspect = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
            if (aspect <= 0.0f) aspect = 1.0f;
            glm::mat4 proj = glm::perspective(glm::radians(FOV), aspect, NEAR_PLANE, FAR_PLANE);

#ifdef GL_POLYGON_MODE
            GLint prevPoly[2] = { GL_FILL, GL_FILL };
            glGetIntegerv(GL_POLYGON_MODE, prevPoly);
            glPolygonMode(GL_FRONT_AND_BACK, collisionWireframe ? GL_LINE : GL_FILL);
#endif

            renderer.beginRender(view, proj, camera.position);
            renderer.renderScene(camera, sceneObjects, selectedObjectId);
            renderer.endRender();

#ifdef GL_POLYGON_MODE
            glPolygonMode(GL_FRONT_AND_BACK, prevPoly[0]);
#endif
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
            setupDockspace([this]() { renderPlayControlsBar(); });
            renderMainMenuBar();

            if (!viewportFullscreen) {
                if (showHierarchy) renderHierarchyPanel();
                if (showInspector) renderInspectorPanel();
                if (showFileBrowser) renderFileBrowserPanel();
                if (showMeshBuilder) renderMeshBuilderPanel();
                if (showConsole) renderConsolePanel();
                if (showEnvironmentWindow) renderEnvironmentWindow();
                if (showCameraWindow) renderCameraWindow();
                if (showProjectBrowser) renderProjectBrowserPanel();
            }

            renderViewport();
            if (showGameViewport) renderGameViewportWindow();
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

        // Enforce cursor lock state at the end of the frame based on latest flags.
        bool anyLock = cursorLocked || gameViewCursorLocked;
        int desiredMode = anyLock ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
        if (glfwGetInputMode(editorWindow, GLFW_CURSOR) != desiredMode) {
            glfwSetInputMode(editorWindow, GLFW_CURSOR, desiredMode);
            if (anyLock && glfwRawMouseMotionSupported()) {
                glfwSetInputMode(editorWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
            } else if (!anyLock && glfwRawMouseMotionSupported()) {
                glfwSetInputMode(editorWindow, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
            }
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

    physics.onPlayStop();
    audio.onPlayStop();
    audio.shutdown();
    physics.shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
}

void Engine::importOBJToScene(const std::string& filepath, const std::string& objectName) {
    recordState("importOBJ");
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
    setPrimarySelection(id);
    
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
    recordState("importModel");
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
    setPrimarySelection(id);

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

void Engine::convertModelToRawMesh(const std::string& filepath) {
    auto& modelLoader = getModelLoader();
    fs::path inPath(filepath);
    fs::path outPath = inPath;
    outPath.replace_extension(".rmesh");

    std::string error;
    if (modelLoader.exportRawMesh(filepath, outPath.string(), error)) {
        addConsoleMessage("Converted to raw mesh: " + outPath.string(), ConsoleMessageType::Success);
        fileBrowser.needsRefresh = true;
    } else {
        addConsoleMessage("Raw mesh export failed: " + error, ConsoleMessageType::Error);
    }
}

bool Engine::ensureMeshEditTarget(SceneObject* obj) {
    if (!obj) return false;
    fs::path ext = fs::path(obj->meshPath).extension();
    std::string extLower = ext.string();
    std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
    if (extLower != ".rmesh") return false;

    if (meshEditLoaded && meshEditPath == obj->meshPath) {
        if (meshEditSelectedVertices.empty() && !meshEditAsset.positions.empty()) {
            meshEditSelectedVertices.push_back(0);
        }
        return true;
    }

    std::string err;
    if (!getModelLoader().loadRawMesh(obj->meshPath, meshEditAsset, err)) {
        addConsoleMessage("Mesh edit load failed: " + err, ConsoleMessageType::Error);
        meshEditLoaded = false;
        return false;
    }
    meshEditLoaded = true;
    meshEditPath = obj->meshPath;
    meshEditSelectedVertices.clear();
    meshEditSelectedEdges.clear();
    meshEditSelectedFaces.clear();
    if (!meshEditAsset.positions.empty()) meshEditSelectedVertices.push_back(0);
    return true;
}

bool Engine::syncMeshEditToGPU(SceneObject* obj) {
    if (!obj || !meshEditLoaded) return false;
    std::string err;
    if (!getModelLoader().updateRawMesh(obj->meshId, meshEditAsset, err)) {
        addConsoleMessage("Mesh GPU sync failed: " + err, ConsoleMessageType::Error);
        return false;
    }
    projectManager.currentProject.hasUnsavedChanges = true;
    return true;
}

void Engine::loadMaterialFromFile(SceneObject& obj) {
    if (obj.materialPath.empty()) return;
    try {
        MaterialFileData data;
        if (!readMaterialFile(obj.materialPath, data)) {
            addConsoleMessage("Failed to open material: " + obj.materialPath, ConsoleMessageType::Error);
            return;
        }
        obj.material = data.props;
        obj.albedoTexturePath = data.albedo;
        obj.overlayTexturePath = data.overlay;
        obj.normalMapPath = data.normal;
        obj.useOverlay = data.useOverlay;
        obj.vertexShaderPath = data.vertexShader;
        obj.fragmentShaderPath = data.fragmentShader;
        addConsoleMessage("Applied material: " + obj.materialPath, ConsoleMessageType::Success);
        projectManager.currentProject.hasUnsavedChanges = true;
    } catch (...) {
        addConsoleMessage("Failed to read material: " + obj.materialPath, ConsoleMessageType::Error);
    }
}

bool Engine::loadMaterialData(const std::string& path, MaterialProperties& props,
                              std::string& albedo, std::string& overlay,
                              std::string& normal, bool& useOverlay,
                              std::string* vertexShaderOut,
                              std::string* fragmentShaderOut)
{
    MaterialFileData data;
    if (!readMaterialFile(path, data)) {
        return false;
    }
    props = data.props;
    albedo = data.albedo;
    overlay = data.overlay;
    normal = data.normal;
    useOverlay = data.useOverlay;
    if (vertexShaderOut) *vertexShaderOut = data.vertexShader;
    if (fragmentShaderOut) *fragmentShaderOut = data.fragmentShader;
    return true;
}

bool Engine::saveMaterialData(const std::string& path, const MaterialProperties& props,
                              const std::string& albedo, const std::string& overlay,
                              const std::string& normal, bool useOverlay,
                              const std::string& vertexShader,
                              const std::string& fragmentShader)
{
    MaterialFileData data;
    data.props = props;
    data.albedo = albedo;
    data.overlay = overlay;
    data.normal = normal;
    data.useOverlay = useOverlay;
    data.vertexShader = vertexShader;
    data.fragmentShader = fragmentShader;
    return writeMaterialFile(data, path);
}

void Engine::saveMaterialToFile(const SceneObject& obj) {
    if (obj.materialPath.empty()) {
        addConsoleMessage("Material path is empty", ConsoleMessageType::Warning);
        return;
    }
    try {
        MaterialFileData data;
        data.props = obj.material;
        data.albedo = obj.albedoTexturePath;
        data.overlay = obj.overlayTexturePath;
        data.normal = obj.normalMapPath;
        data.useOverlay = obj.useOverlay;
        data.vertexShader = obj.vertexShaderPath;
        data.fragmentShader = obj.fragmentShaderPath;

        if (!writeMaterialFile(data, obj.materialPath)) {
            addConsoleMessage("Failed to open material for writing: " + obj.materialPath, ConsoleMessageType::Error);
            return;
        }
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

    bool cameraActive = cursorLocked || (viewportController.isViewportFocused() && cursorLocked);
    if (!isPlaying && gameViewCursorLocked) {
        // Prevent edit-mode freelook from conflicting with game view capture
        gameViewCursorLocked = false;
    }
    if (!cameraActive) {
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_W)) mCurrentGizmoOperation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) mCurrentGizmoOperation = ImGuizmo::SCALE;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) mCurrentGizmoOperation = ImGuizmo::BOUNDS;
        if (ImGui::IsKeyPressed(ImGuiKey_T)) mCurrentGizmoOperation = ImGuizmo::UNIVERSAL;

        if (ImGui::IsKeyPressed(ImGuiKey_U)) {
            mCurrentGizmoMode = (mCurrentGizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_3)) {
        collisionWireframe = !collisionWireframe;
        addConsoleMessage(std::string("Collision wireframe ") + (collisionWireframe ? "enabled" : "disabled"), ConsoleMessageType::Info);
    }

    static bool snapPressed = false;
    static bool snapHeldByCtrl = false;
    static bool snapStateBeforeCtrl = false;

    if (!snapHeldByCtrl && ctrlDown) {
        snapStateBeforeCtrl = useSnap;
        snapHeldByCtrl = true;
        useSnap = true;
    } else if (snapHeldByCtrl && !ctrlDown) {
        useSnap = snapStateBeforeCtrl;
        snapHeldByCtrl = false;
    }

    if (!cameraActive) {
        if (ImGui::IsKeyPressed(ImGuiKey_Y) && !snapPressed) {
            useSnap = !useSnap;
            snapPressed = true;
        }
    }
    if (ImGui::IsKeyReleased(ImGuiKey_Y)) {
        snapPressed = false;
    }

    static bool undoPressed = false;
    if (ctrlDown && glfwGetKey(editorWindow, GLFW_KEY_Z) == GLFW_PRESS && !undoPressed) {
        undo();
        undoPressed = true;
    }
    if (glfwGetKey(editorWindow, GLFW_KEY_Z) == GLFW_RELEASE) {
        undoPressed = false;
    }

    static bool redoPressed = false;
    if (ctrlDown && glfwGetKey(editorWindow, GLFW_KEY_Y) == GLFW_PRESS && !redoPressed) {
        redo();
        redoPressed = true;
    }
    if (glfwGetKey(editorWindow, GLFW_KEY_Y) == GLFW_RELEASE) {
        redoPressed = false;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && gameViewCursorLocked) {
        gameViewCursorLocked = false;
    }
}

void Engine::updateScripts(float delta) {
    if (sceneObjects.empty()) return;

    for (auto& obj : sceneObjects) {
        if (!obj.enabled) continue;
        for (auto& sc : obj.scripts) {
            if (!sc.enabled) continue;
            if (sc.path.empty()) continue;
            fs::path binary = resolveScriptBinary(sc.path);
            if (binary.empty() || !fs::exists(binary)) continue;
            ScriptContext ctx;
            ctx.engine = this;
            ctx.object = &obj;
            ctx.script = &sc;

            scriptRuntime.tickModule(binary, ctx, delta, specMode, testMode);
        }
    }
}

void Engine::updatePlayerController(float delta) {
    if (!isPlaying) return;

    SceneObject* player = nullptr;
    for (auto& obj : sceneObjects) {
        if (obj.enabled && obj.hasPlayerController && obj.playerController.enabled) {
            player = &obj;
            activePlayerId = obj.id;
            break;
        }
    }
    if (!player) {
        activePlayerId = -1;
        return;
    }

    auto& pc = player->playerController;
    // Maintain capsule sizing and collider defaults
    if (pc.pitch == 0.0f && pc.yaw == 0.0f && (glm::length(player->rotation) > 0.01f)) {
        pc.pitch = player->rotation.x;
        pc.yaw = player->rotation.y;
    }
    player->hasCollider = true;
    player->collider.type = ColliderType::Capsule;
    player->collider.convex = true;
    player->collider.boxSize = glm::vec3(pc.radius * 2.0f, pc.height, pc.radius * 2.0f);
    player->hasRigidbody = true;
    player->rigidbody.enabled = true;
    player->rigidbody.useGravity = true;
    player->rigidbody.isKinematic = false;

    // Mouse look when game viewport is focused
    if (gameViewportFocused || gameViewCursorLocked) {
        ImGuiIO& io = ImGui::GetIO();
        pc.yaw -= io.MouseDelta.x * 50.0f * pc.lookSensitivity * delta;
        pc.pitch -= io.MouseDelta.y * 50.0f * pc.lookSensitivity * delta;
        pc.pitch = std::clamp(pc.pitch, -89.0f, 89.0f);
    }

    // Movement input aligned to camera facing (-Z forward convention)
    auto key = [&](int k) { return glfwGetKey(editorWindow, k) == GLFW_PRESS; };
    glm::quat q = glm::quat(glm::radians(glm::vec3(pc.pitch, pc.yaw, 0.0f)));
    glm::vec3 forward = glm::normalize(q * glm::vec3(0.0f, 0.0f, -1.0f));
    glm::vec3 right = glm::normalize(q * glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 planarForward = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
    glm::vec3 planarRight = glm::normalize(glm::vec3(right.x, 0.0f, right.z));
    if (!std::isfinite(planarForward.x) || glm::length(planarForward) < 1e-3f) {
        planarForward = glm::vec3(0, 0, -1);
    }
    if (!std::isfinite(planarRight.x) || glm::length(planarRight) < 1e-3f) {
        planarRight = glm::vec3(1, 0, 0);
    }

    glm::vec3 move(0.0f);
    if (key(GLFW_KEY_W)) move += planarForward;
    if (key(GLFW_KEY_S)) move -= planarForward;
    if (key(GLFW_KEY_D)) move += planarRight;
    if (key(GLFW_KEY_A)) move -= planarRight;
    if (glm::length(move) > 0.001f) move = glm::normalize(move);

    float targetSpeed = pc.moveSpeed;
    glm::vec3 velocity(move * targetSpeed);

    // Simple gravity and jump
    float capsuleHalf = std::max(0.1f, pc.height * 0.5f);
    glm::vec3 physVel;
    bool havePhysVel = physics.getLinearVelocity(player->id, physVel);
    if (havePhysVel) pc.verticalVelocity = physVel.y;

    // Ground check via PhysX scene query so mesh colliders work, not just the plane
    glm::vec3 hitPos;
    glm::vec3 hitNormal;
    float hitDist = 0.0f;
    float probeDist = capsuleHalf + 0.4f;
    glm::vec3 rayStart = player->position + glm::vec3(0.0f, 0.1f, 0.0f);
    bool hitGround = physics.raycastClosest(rayStart, glm::vec3(0.0f, -1.0f, 0.0f), probeDist,
                                            player->id, &hitPos, &hitNormal, &hitDist);
    bool grounded = hitGround && hitNormal.y > 0.25f && hitDist <= capsuleHalf + 0.2f && pc.verticalVelocity <= 0.35f;
    if (!hitGround) {
        // Fallback to simple height check to avoid regressions if queries fail
        grounded = player->position.y <= capsuleHalf + 0.12f && pc.verticalVelocity <= 0.35f;
    }

    if (grounded) {
        pc.verticalVelocity = 0.0f;
        if (!havePhysVel) {
            if (hitGround) {
                player->position.y = std::max(player->position.y, hitPos.y + capsuleHalf);
            } else {
                player->position.y = capsuleHalf;
            }
        }
        if (key(GLFW_KEY_SPACE)) {
            pc.verticalVelocity = pc.jumpStrength;
        }
    } else {
        pc.verticalVelocity += -9.81f * delta;
    }
    velocity.y = pc.verticalVelocity;
    velocity.y = std::clamp(velocity.y, -30.0f, 30.0f);

    // Apply yaw to physics actor and keep collider aligned
    physics.setActorYaw(player->id, pc.yaw);
    player->rotation = glm::vec3(pc.pitch, pc.yaw, 0.0f);

    if (!physics.setLinearVelocity(player->id, velocity)) {
        player->position += velocity * delta;
    }
}
void Engine::OpenProjectPath(const std::string& path) {
    try {
        if (projectManager.loadProject(path)) {
            // Make sure project folders exist even for older/minimal projects
            if (!fs::exists(projectManager.currentProject.assetsPath)) {
                fs::create_directories(projectManager.currentProject.assetsPath);
            }
            if (!fs::exists(projectManager.currentProject.scenesPath)) {
                fs::create_directories(projectManager.currentProject.scenesPath);
            }

            packageManager.setProjectRoot(projectManager.currentProject.projectPath);

            if (!initRenderer()) {
                addConsoleMessage("Error: Failed to initialize renderer!", ConsoleMessageType::Error);
                showLauncher = true;
                return;
            }

            if (!physics.isReady() && !physics.init()) {
                addConsoleMessage("Warning: PhysX failed to initialize; physics disabled for this session", ConsoleMessageType::Warning);
            }

            loadRecentScenes();
            fileBrowser.setProjectRoot(projectManager.currentProject.projectPath);
            fileBrowser.currentPath = projectManager.currentProject.projectPath;
            fileBrowser.needsRefresh = true;
            showLauncher = false;
            addConsoleMessage("Opened project: " + projectManager.currentProject.name, ConsoleMessageType::Info);
        } else {
            addConsoleMessage("Error opening project: " + projectManager.errorMessage, ConsoleMessageType::Error);
        }
    } catch (const std::exception& e) {
        addConsoleMessage(std::string("Exception opening project: ") + e.what(), ConsoleMessageType::Error);
        showLauncher = true;
    } catch (...) {
        addConsoleMessage("Unknown exception opening project", ConsoleMessageType::Error);
        showLauncher = true;
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

        packageManager.setProjectRoot(projectManager.currentProject.projectPath);

        if (!initRenderer()) {
            logToConsole("Error: Failed to initialize renderer!");
            return;
        }

        if (!physics.isReady() && !physics.init()) {
            addConsoleMessage("Warning: PhysX failed to initialize; physics disabled for this session", ConsoleMessageType::Warning);
        }

        sceneObjects.clear();
        clearSelection();
        nextObjectId = 0;

        addObject(ObjectType::Cube, "Cube");

        fileBrowser.setProjectRoot(projectManager.currentProject.projectPath);
        fileBrowser.currentPath = projectManager.currentProject.projectPath;
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
    clearSelection();
    nextObjectId = 0;
    undoStack.clear();
    redoStack.clear();

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
    recordState("sceneLoaded");

    fileBrowser.setProjectRoot(projectManager.currentProject.projectPath);
    fileBrowser.currentPath = projectManager.currentProject.projectPath;
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
        undoStack.clear();
        redoStack.clear();
        projectManager.currentProject.currentSceneName = sceneName;
        projectManager.currentProject.hasUnsavedChanges = false;
        projectManager.currentProject.saveProjectFile();
        clearSelection();
        bool hasDirLight = std::any_of(sceneObjects.begin(), sceneObjects.end(), [](const SceneObject& o) {
            return o.type == ObjectType::DirectionalLight;
        });
        if (!hasDirLight) {
            addObject(ObjectType::DirectionalLight, "Directional Light");
        }
        recordState("sceneLoaded");
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
    clearSelection();
    nextObjectId = 0;
    undoStack.clear();
    redoStack.clear();

    projectManager.currentProject.currentSceneName = sceneName;
    projectManager.currentProject.hasUnsavedChanges = true;

    addObject(ObjectType::Cube, "Cube");
    addObject(ObjectType::DirectionalLight, "Directional Light");
    saveCurrentScene();
    recordState("newScene");

    addConsoleMessage("Created new scene: " + sceneName, ConsoleMessageType::Success);
}

void Engine::addObject(ObjectType type, const std::string& baseName) {
    recordState("addObject");
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
    } else if (type == ObjectType::PostFXNode) {
        sceneObjects.back().postFx.enabled = true;
        sceneObjects.back().postFx.bloomEnabled = true;
        sceneObjects.back().postFx.colorAdjustEnabled = true;
    } else if (type == ObjectType::Camera) {
        sceneObjects.back().camera.type = SceneCameraType::Player;
        sceneObjects.back().camera.fov = 60.0f;
    }
    setPrimarySelection(id);
    if (projectManager.currentProject.isLoaded) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }
    logToConsole("Created: " + name);
}

void Engine::duplicateSelected() {
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
        [this](const SceneObject& obj) { return obj.id == selectedObjectId; });

    if (it != sceneObjects.end()) {
        recordState("duplicate");
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
        newObj.vertexShaderPath = it->vertexShaderPath;
        newObj.fragmentShaderPath = it->fragmentShaderPath;
        newObj.useOverlay = it->useOverlay;
        newObj.light = it->light;
        newObj.camera = it->camera;
        newObj.postFx = it->postFx;
        newObj.hasRigidbody = it->hasRigidbody;
        newObj.rigidbody = it->rigidbody;
        newObj.hasCollider = it->hasCollider;
        newObj.collider = it->collider;
        newObj.hasPlayerController = it->hasPlayerController;
        newObj.playerController = it->playerController;
        newObj.hasAudioSource = it->hasAudioSource;
        newObj.audioSource = it->audioSource;
        
        sceneObjects.push_back(newObj);
        setPrimarySelection(id);
        if (projectManager.currentProject.isLoaded) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        logToConsole("Duplicated: " + newObj.name);
    }
}

void Engine::deleteSelected() {
    recordState("delete");
    auto it = std::remove_if(sceneObjects.begin(), sceneObjects.end(),
        [this](const SceneObject& obj) { return obj.id == selectedObjectId; });

    if (it != sceneObjects.end()) {
        logToConsole("Deleted object");
        sceneObjects.erase(it, sceneObjects.end());
        clearSelection();
        if (projectManager.currentProject.isLoaded) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
    }
}

void Engine::setParent(int childId, int parentId) {
    recordState("reparent");
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

void Engine::addConsoleMessageFromScript(const std::string& message, ConsoleMessageType type) {
    addConsoleMessage(message, type);
}

SceneObject* Engine::findObjectByName(const std::string& name) {
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(), [&](const SceneObject& o) {
        return o.name == name;
    });
    if (it != sceneObjects.end()) return &(*it);
    return nullptr;
}

SceneObject* Engine::findObjectById(int id) {
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(), [&](const SceneObject& o) {
        return o.id == id;
    });
    if (it != sceneObjects.end()) return &(*it);
    return nullptr;
}

fs::path Engine::resolveScriptBinary(const fs::path& sourcePath) {
    ScriptBuildConfig config;
    std::string error;
    fs::path cfg = projectManager.currentProject.scriptsConfigPath.empty()
        ? projectManager.currentProject.projectPath / "Scripts.modu"
        : projectManager.currentProject.scriptsConfigPath;
    if (!scriptCompiler.loadConfig(cfg, config, error)) {
        return {};
    }
    ScriptBuildCommands cmds;
    if (!scriptCompiler.makeCommands(config, sourcePath, cmds, error)) {
        return {};
    }
    return cmds.binaryPath;
}

void Engine::markProjectDirty() {
    projectManager.currentProject.hasUnsavedChanges = true;
}

bool Engine::setRigidbodyVelocityFromScript(int id, const glm::vec3& velocity) {
    return physics.setLinearVelocity(id, velocity);
}

bool Engine::getRigidbodyVelocityFromScript(int id, glm::vec3& outVelocity) {
    return physics.getLinearVelocity(id, outVelocity);
}

bool Engine::teleportPhysicsActorFromScript(int id, const glm::vec3& position, const glm::vec3& rotationDeg) {
    return physics.setActorPose(id, position, rotationDeg);
}

bool Engine::playAudioFromScript(int id) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAudioSource) return false;
    return audio.playObjectSound(*obj);
}

bool Engine::stopAudioFromScript(int id) {
    return audio.stopObjectSound(id);
}

bool Engine::setAudioLoopFromScript(int id, bool loop) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAudioSource) return false;
    obj->audioSource.loop = loop;
    markProjectDirty();
    return audio.setObjectLoop(*obj, loop);
}

bool Engine::setAudioVolumeFromScript(int id, float volume) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAudioSource) return false;
    obj->audioSource.volume = std::clamp(volume, 0.0f, 2.0f);
    markProjectDirty();
    return audio.setObjectVolume(*obj, obj->audioSource.volume);
}

bool Engine::setAudioClipFromScript(int id, const std::string& path) {
    SceneObject* obj = findObjectById(id);
    if (!obj || !obj->hasAudioSource) return false;
    obj->audioSource.clipPath = path;
    markProjectDirty();
    // Ensure clip is loaded; do not auto-play unless PlayAudio is called.
    audio.setObjectLoop(*obj, obj->audioSource.loop);
    return true;
}

void Engine::compileScriptFile(const fs::path& scriptPath) {
    if (!projectManager.currentProject.isLoaded) {
        addConsoleMessage("No project is loaded", ConsoleMessageType::Warning);
        return;
    }

    showCompilePopup = true;
    lastCompileLog.clear();
    lastCompileStatus = "Compiling " + scriptPath.filename().string();

    fs::path configPath = projectManager.currentProject.scriptsConfigPath;
    if (configPath.empty()) {
        configPath = projectManager.currentProject.projectPath / "Scripts.modu";
    }

    ScriptBuildConfig config;
    std::string error;
    if (!scriptCompiler.loadConfig(configPath, config, error)) {
        lastCompileSuccess = false;
        lastCompileLog = error;
        addConsoleMessage("Script config error: " + error, ConsoleMessageType::Error);
        return;
    }

    packageManager.applyToBuildConfig(config);

    ScriptBuildCommands commands;
    if (!scriptCompiler.makeCommands(config, scriptPath, commands, error)) {
        lastCompileSuccess = false;
        lastCompileLog = error;
        addConsoleMessage("Script build error: " + error, ConsoleMessageType::Error);
        return;
    }

    ScriptCompileOutput output;
    if (!scriptCompiler.compile(commands, output, error)) {
        lastCompileSuccess = false;
        lastCompileStatus = "Compile failed";
        lastCompileLog = output.compileLog + output.linkLog + error;
        addConsoleMessage("Compile failed: " + error, ConsoleMessageType::Error);
        if (!output.compileLog.empty()) addConsoleMessage(output.compileLog, ConsoleMessageType::Info);
        if (!output.linkLog.empty()) addConsoleMessage(output.linkLog, ConsoleMessageType::Info);
        return;
    }

    scriptRuntime.unloadAll();

    lastCompileSuccess = true;
    lastCompileStatus = "Reloading EngineRoot";
    lastCompileLog = output.compileLog + output.linkLog;
    addConsoleMessage("Compiled script -> " + commands.binaryPath.string(), ConsoleMessageType::Success);
    if (!output.compileLog.empty()) addConsoleMessage(output.compileLog, ConsoleMessageType::Info);
    if (!output.linkLog.empty()) addConsoleMessage(output.linkLog, ConsoleMessageType::Info);
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
