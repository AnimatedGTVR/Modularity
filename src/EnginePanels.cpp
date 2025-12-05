#include "Engine.h"

void Engine::renderLauncher() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.09f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySize);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking  |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("Launcher", nullptr, flags))
    {
        float leftPanelWidth = 280.0f;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.07f, 1.0f));
        ImGui::BeginChild("LauncherLeft", ImVec2(leftPanelWidth, 0), true);
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.45f, 0.72f, 0.95f, 1.0f), "MODULARITY");
        ImGui::TextDisabled("Game Engine");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.78f, 1.0f), "GET STARTED");
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.38f, 0.55f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.48f, 0.68f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.42f, 0.60f, 1.0f));

        if (ImGui::Button("New Project", ImVec2(-1, 36.0f)))
        {
            projectManager.showNewProjectDialog = true;
            projectManager.errorMessage.clear();
            std::memset(projectManager.newProjectName, 0, sizeof(projectManager.newProjectName));

            #ifdef _WIN32
            char documentsPath[MAX_PATH];
            SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, 0, documentsPath);
            std::strcpy(projectManager.newProjectLocation, documentsPath);
            std::strcat(projectManager.newProjectLocation, "\\ModularityProjects");
            #else
            const char* home = std::getenv("HOME");
            if (home)
            {
                std::strcpy(projectManager.newProjectLocation, home);
                std::strcat(projectManager.newProjectLocation, "/ModularityProjects");
            }
            #endif
        }

        ImGui::Spacing();

        if (ImGui::Button("Open Project", ImVec2(-1, 36.0f)))
        {
            projectManager.showOpenProjectDialog = true;
            projectManager.errorMessage.clear();
        }

        ImGui::PopStyleColor(3);

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.78f, 1.0f), "QUICK ACTIONS");
        ImGui::Spacing();

        if (ImGui::Button("Documentation", ImVec2(-1, 30.0f)))
        {
            #ifdef _WIN32
            system("start https://github.com");
            #elif __APPLE__
            system("open https://github.com");
            #else
            system("xdg-open https://github.com");
            #endif
        }

        if (ImGui::Button("Settings", ImVec2(-1, 30.0f)))
        {
            logToConsole("Settings clicked");
        }

        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("LauncherRight", ImVec2(0, 0), false);

        ImGui::Spacing();
        ImGui::Text("Recent Projects");
        ImGui::Separator();
        ImGui::Spacing();

        if (projectManager.recentProjects.empty())
        {
            ImGui::TextDisabled("No recent projects");
            ImGui::TextDisabled("Create a new project or open an existing one");
        }
        else
        {
            for (size_t i = 0; i < projectManager.recentProjects.size(); i++)
            {
                const auto& rp = projectManager.recentProjects[i];

                ImGui::PushID(static_cast<int>(i));

                ImVec2 buttonSize(-1, 60.0f);
                ImVec2 cursorPos = ImGui::GetCursorScreenPos();

                if (ImGui::InvisibleButton("##project", buttonSize))
                {
                    OpenProjectPath(rp.path);
                }

                bool isHovered = ImGui::IsItemHovered();
                ImU32 bgColor = isHovered ? IM_COL32(40, 40, 45, 255) : IM_COL32(30, 30, 35, 255);

                ImGui::GetWindowDrawList()->AddRectFilled(
                    cursorPos,
                    ImVec2(cursorPos.x + ImGui::GetContentRegionAvail().x, cursorPos.y + buttonSize.y),
                    bgColor,
                    4.0f
                );

                ImVec2 savedCursor = ImGui::GetCursorPos();

                ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + 12, cursorPos.y + 10));
                ImGui::TextColored(ImVec4(0.45f, 0.72f, 0.95f, 1.0f), "[P]");
                ImGui::SameLine();
                ImGui::Text("%s", rp.name.c_str());

                ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + 12, cursorPos.y + 32));
                ImGui::TextDisabled("%s", rp.path.c_str());

                ImGui::SetCursorPos(savedCursor);
                ImGui::Dummy(ImVec2(0, 4.0f));

                ImGui::PopID();
            }
        }

        ImGui::EndChild();

        if (projectManager.showNewProjectDialog) {
            renderNewProjectDialog();
        }

        if (projectManager.showOpenProjectDialog) {
            renderOpenProjectDialog();
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void Engine::renderNewProjectDialog() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 200), ImGuiCond_Appearing);

    if (ImGui::Begin("Create New Project", &projectManager.showNewProjectDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {

        ImGui::Text("Project Name:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##ProjectName", projectManager.newProjectName,
                        sizeof(projectManager.newProjectName));

        ImGui::Spacing();

        ImGui::Text("Location:");
        ImGui::SetNextItemWidth(-70);
        ImGui::InputText("##Location", projectManager.newProjectLocation,
                        sizeof(projectManager.newProjectLocation));
        ImGui::SameLine();
        if (ImGui::Button("Browse")) {
        }

        if (!projectManager.errorMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                              projectManager.errorMessage.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float buttonWidth = 100;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
            projectManager.showNewProjectDialog = false;
        }

        ImGui::SameLine();

        if (ImGui::Button("Create", ImVec2(buttonWidth, 0))) {
            if (strlen(projectManager.newProjectName) == 0) {
                projectManager.errorMessage = "Please enter a project name";
            } else {
                createNewProject(projectManager.newProjectName,
                               projectManager.newProjectLocation);
                if (projectManager.currentProject.isLoaded) {
                    projectManager.showNewProjectDialog = false;
                }
            }
        }
    }
    ImGui::End();
}

void Engine::renderOpenProjectDialog() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 180), ImGuiCond_Appearing);

    if (ImGui::Begin("Open Project", &projectManager.showOpenProjectDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {

        ImGui::Text("Project File Path (.modu):");
        ImGui::SetNextItemWidth(-70);
        ImGui::InputText("##OpenPath", projectManager.openProjectPath,
                       sizeof(projectManager.openProjectPath));
        ImGui::SameLine();
        if (ImGui::Button("Browse")) {
        }

        ImGui::TextDisabled("Select a project.modu file");

        if (!projectManager.errorMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                              projectManager.errorMessage.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float buttonWidth = 100;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
            projectManager.showOpenProjectDialog = false;
            memset(projectManager.openProjectPath, 0, sizeof(projectManager.openProjectPath));
        }

        ImGui::SameLine();

        if (ImGui::Button("Open", ImVec2(buttonWidth, 0))) {
            if (strlen(projectManager.openProjectPath) == 0) {
                projectManager.errorMessage = "Please enter a project path";
            } else {
                if (projectManager.loadProject(projectManager.openProjectPath)) {
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
        }
    }
    ImGui::End();
}

void Engine::renderMainMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                showNewSceneDialog = true;
                memset(newSceneName, 0, sizeof(newSceneName));
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                saveCurrentScene();
            }
            if (ImGui::MenuItem("Save Scene As...")) {
                showSaveSceneAsDialog = true;
                strncpy(saveSceneAsName, projectManager.currentProject.currentSceneName.c_str(),
                       sizeof(saveSceneAsName) - 1);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close Project")) {
                if (projectManager.currentProject.hasUnsavedChanges) {
                    saveCurrentScene();
                }
                projectManager.currentProject = Project();
                sceneObjects.clear();
                selectedObjectId = -1;
                showLauncher = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                glfwSetWindowShouldClose(editorWindow, true);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, false)) {}
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, false)) {}
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy);
            ImGui::MenuItem("Inspector", nullptr, &showInspector);
            ImGui::MenuItem("File Browser", nullptr, &showFileBrowser);
            ImGui::MenuItem("Console", nullptr, &showConsole);
            ImGui::MenuItem("Project", nullptr, &showProjectBrowser);
            ImGui::Separator();
            if (ImGui::MenuItem("Fullscreen Viewport", "F11", viewportFullscreen)) {
                viewportFullscreen = !viewportFullscreen;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Cube")) addObject(ObjectType::Cube, "Cube");
            if (ImGui::MenuItem("Sphere")) addObject(ObjectType::Sphere, "Sphere");
            if (ImGui::MenuItem("Capsule")) addObject(ObjectType::Capsule, "Capsule");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                logToConsole("Modularity Engine v0.1");
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void Engine::renderHierarchyPanel() {
    ImGui::Begin("Hierarchy", &showHierarchy);

    static char searchBuffer[128] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##Search", "Search...", searchBuffer, sizeof(searchBuffer));

    ImGui::Separator();

    std::string filter = searchBuffer;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            int draggedId = *(const int*)payload->Data;
            setParent(draggedId, -1);
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::BeginChild("HierarchyList", ImVec2(0, 0), false);

    for (size_t i = 0; i < sceneObjects.size(); i++) {
        if (sceneObjects[i].parentId != -1)
            continue;

        renderObjectNode(sceneObjects[i], filter);
    }

    if (ImGui::BeginPopupContextWindow("HierarchyBackground",
            ImGuiPopupFlags_MouseButtonRight |
            ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Cube"))    addObject(ObjectType::Cube, "Cube");
            if (ImGui::MenuItem("Sphere"))  addObject(ObjectType::Sphere, "Sphere");
            if (ImGui::MenuItem("Capsule")) addObject(ObjectType::Capsule, "Capsule");
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();

    ImGui::End();
}

void Engine::renderObjectNode(SceneObject& obj, const std::string& filter) {
    std::string nameLower = obj.name;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

    if (!filter.empty() && nameLower.find(filter) == std::string::npos) {
        return;
    }

    bool hasChildren = !obj.childIds.empty();
    bool isSelected = (selectedObjectId == obj.id);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

    const char* icon = "";
    switch (obj.type) {
        case ObjectType::Cube: icon = "[#]"; break;
        case ObjectType::Sphere: icon = "(O)"; break;
        case ObjectType::Capsule: icon = "[|]"; break;
        case ObjectType::OBJMesh: icon = "[M]"; break;
    }

    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)obj.id, flags, "%s %s", icon, obj.name.c_str());

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selectedObjectId = obj.id;
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("SCENE_OBJECT", &obj.id, sizeof(int));
        ImGui::Text("Moving: %s", obj.name.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            int draggedId = *(const int*)payload->Data;
            if (draggedId != obj.id) {
                setParent(draggedId, obj.id);
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Duplicate")) {
            selectedObjectId = obj.id;
            duplicateSelected();
        }
        if (ImGui::MenuItem("Delete")) {
            selectedObjectId = obj.id;
            deleteSelected();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Clear Parent") && obj.parentId != -1) {
            setParent(obj.id, -1);
        }
        ImGui::EndPopup();
    }

    if (nodeOpen) {
        for (int childId : obj.childIds) {
            auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                [childId](const SceneObject& o) { return o.id == childId; });
            if (it != sceneObjects.end()) {
                renderObjectNode(*it, filter);
            }
        }
        ImGui::TreePop();
    }
}

void Engine::renderFileBrowserPanel() {
    ImGui::Begin("File Browser", &showFileBrowser);

    if (fileBrowser.needsRefresh) {
        fileBrowser.refresh();
    }

    if (ImGui::Button("<")) {
        fileBrowser.navigateUp();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        fileBrowser.needsRefresh = true;
    }
    ImGui::SameLine();

    std::string pathStr = fileBrowser.currentPath.string();
    ImGui::TextWrapped("%s", pathStr.c_str());

    ImGui::Separator();

    ImGui::BeginChild("FileList", ImVec2(0, 0), false);

    for (const auto& entry : fileBrowser.entries) {
        const char* icon = fileBrowser.getFileIcon(entry);
        std::string filename = entry.path().filename().string();

        bool isSelected = (fileBrowser.selectedFile == entry.path());
        bool isOBJ = fileBrowser.isOBJFile(entry);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

        if (isOBJ) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
        }

        ImGui::TreeNodeEx(filename.c_str(), flags, "%s %s", icon, filename.c_str());
        
        if (isOBJ) {
            ImGui::PopStyleColor();
        }

        if (ImGui::IsItemClicked()) {
            fileBrowser.selectedFile = entry.path();
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            if (entry.is_directory()) {
                fileBrowser.navigateTo(entry.path());
            } else if (isOBJ) {
                pendingOBJPath = entry.path().string();
                std::string defaultName = entry.path().stem().string();
                strncpy(importOBJName, defaultName.c_str(), sizeof(importOBJName) - 1);
                showImportOBJDialog = true;
            } else {
                logToConsole("Selected file: " + filename);
            }
        }

        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Open")) {
                if (entry.is_directory()) {
                    fileBrowser.navigateTo(entry.path());
                }
            }
            if (isOBJ) {
                if (ImGui::MenuItem("Import to Scene")) {
                    pendingOBJPath = entry.path().string();
                    std::string defaultName = entry.path().stem().string();
                    strncpy(importOBJName, defaultName.c_str(), sizeof(importOBJName) - 1);
                    showImportOBJDialog = true;
                }
                if (ImGui::MenuItem("Quick Import")) {
                    importOBJToScene(entry.path().string(), "");
                }
            }
            if (ImGui::MenuItem("Show in Explorer")) {
                #ifdef _WIN32
                std::string cmd = "explorer \"" + entry.path().parent_path().string() + "\"";
                system(cmd.c_str());
                #endif
            }
            ImGui::EndPopup();
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

void Engine::renderInspectorPanel() {
    ImGui::Begin("Inspector", &showInspector);

    if (selectedObjectId == -1) {
        ImGui::TextDisabled("No object selected");
        ImGui::End();
        return;
    }

    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
        [this](const SceneObject& obj) { return obj.id == selectedObjectId; });

    if (it == sceneObjects.end()) {
        ImGui::TextDisabled("Object not found");
        ImGui::End();
        return;
    }

    SceneObject& obj = *it;

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.4f, 0.6f, 1.0f));

    if (ImGui::CollapsingHeader("Object Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        char nameBuffer[128];
        strncpy(nameBuffer, obj.name.c_str(), sizeof(nameBuffer));
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';

        ImGui::Text("Name:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer))) {
            obj.name = nameBuffer;
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        ImGui::Text("Type:");
        ImGui::SameLine();
        const char* typeNames[] = { "Cube", "Sphere", "Capsule", "OBJ Mesh" };
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", typeNames[(int)obj.type]);

        ImGui::Text("ID:");
        ImGui::SameLine();
        ImGui::TextDisabled("%d", obj.id);
    }

    ImGui::PopStyleColor();

    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.5f, 0.3f, 1.0f));

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(10.0f);

        ImGui::Text("Position");
        ImGui::PushItemWidth(-1);
        if (ImGui::DragFloat3("##Position", &obj.position.x, 0.1f)) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();

        ImGui::Text("Rotation");
        ImGui::PushItemWidth(-1);
        if (ImGui::DragFloat3("##Rotation", &obj.rotation.x, 1.0f, -360.0f, 360.0f)) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();

        ImGui::Text("Scale");
        ImGui::PushItemWidth(-1);
        if (ImGui::DragFloat3("##Scale", &obj.scale.x, 0.05f, 0.01f, 100.0f)) {
            projectManager.currentProject.hasUnsavedChanges = true;
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();

        if (ImGui::Button("Reset Transform", ImVec2(-1, 0))) {
            obj.position = glm::vec3(0.0f);
            obj.rotation = glm::vec3(0.0f);
            obj.scale = glm::vec3(1.0f);
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        ImGui::Unindent(10.0f);
    }

    ImGui::PopStyleColor();

    if (obj.type == ObjectType::OBJMesh) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.4f, 1.0f));
        
        if (ImGui::CollapsingHeader("Mesh Info", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);
            
            const auto* meshInfo = g_objLoader.getMeshInfo(obj.meshId);
            if (meshInfo) {
                ImGui::Text("Source File:");
                ImGui::TextDisabled("%s", fs::path(meshInfo->path).filename().string().c_str());
                
                ImGui::Spacing();
                
                ImGui::Text("Vertices: %d", meshInfo->vertexCount);
                ImGui::Text("Faces: %d", meshInfo->faceCount);
                ImGui::Text("Has Normals: %s", meshInfo->hasNormals ? "Yes" : "No");
                ImGui::Text("Has UVs: %s", meshInfo->hasTexCoords ? "Yes" : "No");
                
                ImGui::Spacing();
                
                if (ImGui::Button("Reload Mesh", ImVec2(-1, 0))) {
                    std::string errMsg;
                    int newId = g_objLoader.loadOBJ(obj.meshPath, errMsg);
                    if (newId >= 0) {
                        obj.meshId = newId;
                        addConsoleMessage("Reloaded mesh: " + obj.name, ConsoleMessageType::Success);
                    } else {
                        addConsoleMessage("Failed to reload: " + errMsg, ConsoleMessageType::Error);
                    }
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Mesh data not found!");
                ImGui::TextDisabled("Path: %s", obj.meshPath.c_str());
                
                if (ImGui::Button("Try Reload", ImVec2(-1, 0))) {
                    std::string errMsg;
                    int newId = g_objLoader.loadOBJ(obj.meshPath, errMsg);
                    if (newId >= 0) {
                        obj.meshId = newId;
                        addConsoleMessage("Reloaded mesh: " + obj.name, ConsoleMessageType::Success);
                    } else {
                        addConsoleMessage("Failed to reload: " + errMsg, ConsoleMessageType::Error);
                    }
                }
            }
            
            ImGui::Unindent(10.0f);
        }
        
        ImGui::PopStyleColor();
    }

    ImGui::End();
}

void Engine::renderConsolePanel() {
    ImGui::Begin("Console", &showConsole);

    if (ImGui::Button("Clear")) {
        consoleLog.clear();
    }

    ImGui::SameLine();
    static bool autoScroll = true;
    ImGui::Checkbox("Auto-scroll", &autoScroll);

    ImGui::Separator();

    ImGui::BeginChild("ConsoleOutput", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& log : consoleLog) {
        ImVec4 color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        if (log.find("Error") != std::string::npos) {
            color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
        } else if (log.find("Warning") != std::string::npos) {
            color = ImVec4(1.0f, 0.8f, 0.4f, 1.0f);
        } else if (log.find("Success") != std::string::npos) {
            color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
        }
        ImGui::TextColored(color, "%s", log.c_str());
    }

    if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();

    ImGui::End();
}

void Engine::renderViewport() {
    ImGuiWindowFlags viewportFlags = ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;

    if (viewportFullscreen) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        viewportFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport", nullptr, viewportFlags);
    ImGui::PopStyleVar();

    ImVec2 fullAvail = ImGui::GetContentRegionAvail();

    const float toolbarHeight = 32.0f;
    ImVec2 imageSize = fullAvail;
    imageSize.y = ImMax(1.0f, imageSize.y - toolbarHeight);

    if (imageSize.x > 0 && imageSize.y > 0) {
        viewportWidth  = static_cast<int>(imageSize.x);
        viewportHeight = static_cast<int>(imageSize.y);
        if (rendererInitialized) {
            renderer.resize(viewportWidth, viewportHeight);
        }
    }

    bool mouseOverViewportImage = false;

    if (rendererInitialized) {
        glm::mat4 proj = glm::perspective(
            glm::radians(FOV),
            (float)viewportWidth / (float)viewportHeight,
            NEAR_PLANE, FAR_PLANE
        );

        glm::mat4 view = camera.getViewMatrix();

        renderer.beginRender(view, proj);
        renderer.renderScene(camera, sceneObjects);
        unsigned int tex = renderer.getViewportTexture();

        ImGui::Image((void*)(intptr_t)tex, imageSize, ImVec2(0, 1), ImVec2(1, 0));

        ImVec2 imageMin = ImGui::GetItemRectMin();
        ImVec2 imageMax = ImGui::GetItemRectMax();
        mouseOverViewportImage = ImGui::IsItemHovered();

        SceneObject* selectedObj = getSelectedObject();
        if (selectedObj) {
            ImGuizmo::BeginFrame();
            ImGuizmo::Enable(true);
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());

            ImGuizmo::SetRect(
                imageMin.x,
                imageMin.y,
                imageMax.x - imageMin.x,
                imageMax.y - imageMin.y
            );

            glm::mat4 modelMatrix(1.0f);
            modelMatrix = glm::translate(modelMatrix, selectedObj->position);
            modelMatrix = glm::rotate(modelMatrix, glm::radians(selectedObj->rotation.x), glm::vec3(1, 0, 0));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(selectedObj->rotation.y), glm::vec3(0, 1, 0));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(selectedObj->rotation.z), glm::vec3(0, 0, 1));
            modelMatrix = glm::scale(modelMatrix, selectedObj->scale);

            float* snapPtr = nullptr;
            float snapRot[3] = { rotationSnapValue, rotationSnapValue, rotationSnapValue };

            if (useSnap) {
                if (mCurrentGizmoOperation == ImGuizmo::ROTATE) {
                    snapPtr = snapRot;
                } else {
                    snapPtr = snapValue;
                }
            }

            ImGuizmo::Manipulate(
                glm::value_ptr(view),
                glm::value_ptr(proj),
                mCurrentGizmoOperation,
                mCurrentGizmoMode,
                glm::value_ptr(modelMatrix),
                nullptr,
                snapPtr
            );

            if (ImGuizmo::IsUsing()) {
                float t[3], r[3], s[3];
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(modelMatrix), t, r, s);

                selectedObj->position = glm::vec3(t[0], t[1], t[2]);
                selectedObj->rotation = glm::vec3(r[0], r[1], r[2]);
                selectedObj->scale    = glm::vec3(s[0], s[1], s[2]);

                projectManager.currentProject.hasUnsavedChanges = true;
            }
        }

        // Toolbar
        ImGui::SetCursorPos(ImVec2(10, imageSize.y + 6));

        if (ImGui::RadioButton("Move",   mCurrentGizmoOperation == ImGuizmo::TRANSLATE)) mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE))    mCurrentGizmoOperation = ImGuizmo::ROTATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale",  mCurrentGizmoOperation == ImGuizmo::SCALE))     mCurrentGizmoOperation = ImGuizmo::SCALE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Uni",    mCurrentGizmoOperation == ImGuizmo::UNIVERSAL)) mCurrentGizmoOperation = ImGuizmo::UNIVERSAL;

        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();

        if (ImGui::RadioButton("Local",  mCurrentGizmoMode == ImGuizmo::LOCAL))  mCurrentGizmoMode = ImGuizmo::LOCAL;
        ImGui::SameLine();
        if (ImGui::RadioButton("World",  mCurrentGizmoMode == ImGuizmo::WORLD)) mCurrentGizmoMode = ImGuizmo::WORLD;

        ImGui::SameLine();
        ImGui::Checkbox("Snap", &useSnap);

        if (useSnap) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            if (mCurrentGizmoOperation == ImGuizmo::ROTATE) {
                ImGui::DragFloat("##snapAngle", &rotationSnapValue, 1.0f, 1.0f, 90.0f, "%.0f deg");
            } else {
                ImGui::DragFloat("##snapVal", &snapValue[0], 0.1f, 0.1f, 10.0f, "%.1f");
                snapValue[1] = snapValue[2] = snapValue[0];
            }
        }

        if (mouseOverViewportImage &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGuizmo::IsUsing() && !ImGuizmo::IsOver())
        {
            viewportController.setFocused(true);
            cursorLocked = true;
            glfwSetInputMode(editorWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            camera.firstMouse = true;
        }
    }

    // Overlay hint
    ImGui::SetCursorPos(ImVec2(10, 30));
    ImGui::TextColored(
        ImVec4(1, 1, 1, 0.6f),
        "WASD: Move | QE: Up/Down | Shift: Sprint | ESC: Release | F11: Fullscreen"
    );

    if (viewportController.isViewportFocused()) {
        ImGui::SetCursorPos(ImVec2(10, 50));
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Camera Active");
    }

    bool windowFocused = ImGui::IsWindowFocused();
    viewportController.updateFocusFromImGui(windowFocused);

    ImGui::End();
}

void Engine::renderDialogs() {
    if (showNewSceneDialog) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(350, 130), ImGuiCond_Appearing);

        if (ImGui::Begin("New Scene", &showNewSceneDialog,
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::Text("Scene Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##NewSceneName", newSceneName, sizeof(newSceneName));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float buttonWidth = 80;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
                showNewSceneDialog = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Create", ImVec2(buttonWidth, 0))) {
                if (strlen(newSceneName) > 0) {
                    createNewScene(newSceneName);
                    showNewSceneDialog = false;
                    memset(newSceneName, 0, sizeof(newSceneName));
                }
            }
        }
        ImGui::End();
    }

    if (showSaveSceneAsDialog) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(350, 130), ImGuiCond_Appearing);

        if (ImGui::Begin("Save Scene As", &showSaveSceneAsDialog,
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::Text("Scene Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##SaveSceneAsName", saveSceneAsName, sizeof(saveSceneAsName));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float buttonWidth = 80;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
                showSaveSceneAsDialog = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Save", ImVec2(buttonWidth, 0))) {
                if (strlen(saveSceneAsName) > 0) {
                    projectManager.currentProject.currentSceneName = saveSceneAsName;
                    saveCurrentScene();
                    showSaveSceneAsDialog = false;
                    memset(saveSceneAsName, 0, sizeof(saveSceneAsName));
                }
            }
        }
        ImGui::End();
    }
    
    // OBJ Import dialog
    if (showImportOBJDialog) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(400, 160), ImGuiCond_Appearing);

        if (ImGui::Begin("Import OBJ Model", &showImportOBJDialog,
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::Text("File: %s", fs::path(pendingOBJPath).filename().string().c_str());
            ImGui::TextDisabled("%s", pendingOBJPath.c_str());
            
            ImGui::Spacing();
            
            ImGui::Text("Object Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##ImportOBJName", importOBJName, sizeof(importOBJName));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float buttonWidth = 80;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
                showImportOBJDialog = false;
                pendingOBJPath.clear();
            }
            ImGui::SameLine();
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.4f, 1.0f));
            if (ImGui::Button("Import", ImVec2(buttonWidth, 0))) {
                importOBJToScene(pendingOBJPath, importOBJName);
                showImportOBJDialog = false;
                pendingOBJPath.clear();
                memset(importOBJName, 0, sizeof(importOBJName));
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::End();
    }
}

void Engine::renderProjectBrowserPanel() {
    ImGui::Begin("Project", &showProjectBrowser);

    if (!projectManager.currentProject.isLoaded) {
        ImGui::TextDisabled("No project loaded");
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(0.4f, 0.7f, 0.95f, 1.0f), "[P] %s", projectManager.currentProject.name.c_str());
    if (projectManager.currentProject.hasUnsavedChanges) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "*");
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Scenes", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("+ New Scene")) {
            showNewSceneDialog = true;
            memset(newSceneName, 0, sizeof(newSceneName));
        }

        ImGui::Spacing();

        auto scenes = projectManager.currentProject.getSceneList();
        for (const auto& scene : scenes) {
            bool isCurrentScene = (scene == projectManager.currentProject.currentSceneName);

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
                                      ImGuiTreeNodeFlags_SpanAvailWidth |
                                      ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (isCurrentScene) flags |= ImGuiTreeNodeFlags_Selected;

            ImGui::TreeNodeEx(scene.c_str(), flags, "[S] %s", scene.c_str());

            if (ImGui::IsItemClicked() && !isCurrentScene) {
                loadScene(scene);
            }

            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Load") && !isCurrentScene) {
                    loadScene(scene);
                }
                if (ImGui::MenuItem("Duplicate")) {
                    addConsoleMessage("Scene duplication not yet implemented.", ConsoleMessageType::Info);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete") && !isCurrentScene) {
                    fs::remove(projectManager.currentProject.getSceneFilePath(scene));
                    addConsoleMessage("Deleted scene: " + scene, ConsoleMessageType::Info);
                }
                ImGui::EndPopup();
            }
        }

        if (scenes.empty()) {
            ImGui::TextDisabled("No scenes yet");
        }
    }
    
    if (ImGui::CollapsingHeader("Loaded Meshes")) {
        const auto& meshes = g_objLoader.getAllMeshes();
        if (meshes.empty()) {
            ImGui::TextDisabled("No meshes loaded");
            ImGui::TextDisabled("Import .obj files from File Browser");
        } else {
            for (size_t i = 0; i < meshes.size(); i++) {
                const auto& mesh = meshes[i];
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
                                          ImGuiTreeNodeFlags_SpanAvailWidth |
                                          ImGuiTreeNodeFlags_NoTreePushOnOpen;
                
                ImGui::TreeNodeEx((void*)(intptr_t)i, flags, "[M] %s", mesh.name.c_str());
                
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Vertices: %d", mesh.vertexCount);
                    ImGui::Text("Faces: %d", mesh.faceCount);
                    ImGui::Text("Has Normals: %s", mesh.hasNormals ? "Yes" : "No");
                    ImGui::Text("Has UVs: %s", mesh.hasTexCoords ? "Yes" : "No");
                    ImGui::TextDisabled("%s", mesh.path.c_str());
                    ImGui::EndTooltip();
                }
            }
        }
    }

    ImGui::End();
}
