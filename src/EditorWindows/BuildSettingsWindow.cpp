#include "Engine.h"

void Engine::renderBuildSettingsWindow() {
    if (!showBuildSettings) return;

    ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Build Settings", &showBuildSettings)) {
        ImGui::End();
        return;
    }

    if (!projectManager.currentProject.isLoaded) {
        ImGui::TextDisabled("No project loaded.");
        ImGui::End();
        return;
    }

    bool changed = false;

    ImGui::BeginChild("BuildScenesList", ImVec2(0, 150), true);
    ImGui::Text("Scenes In Build");
    ImGui::Separator();
    for (int i = 0; i < static_cast<int>(buildSettings.scenes.size()); ++i) {
        BuildSceneEntry& entry = buildSettings.scenes[i];
        ImGui::PushID(i);
        bool enabled = entry.enabled;
        if (ImGui::Checkbox("##enabled", &enabled)) {
            entry.enabled = enabled;
            changed = true;
        }
        ImGui::SameLine();
        bool selected = (buildSettingsSelectedIndex == i);
        if (ImGui::Selectable(entry.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
            buildSettingsSelectedIndex = i;
        }
        float rightX = ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine(rightX - 24.0f);
        ImGui::TextDisabled("%d", i);
        ImGui::PopID();
    }
    ImGui::EndChild();

    float buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
    float addWidth = 150.0f;
    float removeWidth = 130.0f;
    float totalButtons = addWidth + removeWidth + buttonSpacing;
    float buttonStart = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - totalButtons;
    if (buttonStart > ImGui::GetCursorPosX()) {
        ImGui::SetCursorPosX(buttonStart);
    }
    if (ImGui::Button("Remove Selected", ImVec2(removeWidth, 0.0f))) {
        if (buildSettingsSelectedIndex >= 0 &&
            buildSettingsSelectedIndex < static_cast<int>(buildSettings.scenes.size())) {
            buildSettings.scenes.erase(buildSettings.scenes.begin() + buildSettingsSelectedIndex);
            if (buildSettingsSelectedIndex >= static_cast<int>(buildSettings.scenes.size())) {
                buildSettingsSelectedIndex = static_cast<int>(buildSettings.scenes.size()) - 1;
            }
            changed = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Open Scenes", ImVec2(addWidth, 0.0f))) {
        if (addSceneToBuildSettings(projectManager.currentProject.currentSceneName, true)) {
            changed = true;
        }
    }

    ImGui::Spacing();
    ImGui::Text("Platform");
    ImGui::Separator();

    ImGui::BeginChild("BuildPlatforms", ImVec2(220, 0), true);
    ImGui::Selectable("Windows & Linux Standalone", true);
    ImGui::BeginDisabled(true);
    ImGui::Selectable("Android", false);
    ImGui::Selectable("Android | Meta Quest", false);
    ImGui::EndDisabled();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("BuildPlatformSettings", ImVec2(0, 0), true);
    ImGui::Text("Target Platform");
    const char* targets[] = {"Windows", "Linux"};
    int targetIndex = (buildSettings.platform == BuildPlatform::Linux) ? 1 : 0;
    if (ImGui::Combo("##target-platform", &targetIndex, targets, 2)) {
        buildSettings.platform = (targetIndex == 1) ? BuildPlatform::Linux : BuildPlatform::Windows;
        changed = true;
    }

    ImGui::Text("Architecture");
    const char* arches[] = {"x86_64", "x86"};
    int archIndex = (buildSettings.architecture == "x86") ? 1 : 0;
    if (ImGui::Combo("##architecture", &archIndex, arches, 2)) {
        buildSettings.architecture = arches[archIndex];
        changed = true;
    }

    ImGui::Spacing();
    if (ImGui::Checkbox("Server Build", &buildSettings.serverBuild)) changed = true;
    if (ImGui::Checkbox("Development Build", &buildSettings.developmentBuild)) changed = true;
    if (ImGui::Checkbox("Autoconnect Profiler", &buildSettings.autoConnectProfiler)) changed = true;
    if (ImGui::Checkbox("Deep Profiling Support", &buildSettings.deepProfiling)) changed = true;
    if (ImGui::Checkbox("Script Debugging", &buildSettings.scriptDebugging)) changed = true;
    if (ImGui::Checkbox("Scripts Only Build", &buildSettings.scriptsOnlyBuild)) changed = true;

    ImGui::Spacing();
    ImGui::Text("Compression Method");
    const char* compressionOptions[] = {"Default", "None", "LZ4", "LZ4HC"};
    int compressionIndex = 0;
    for (int i = 0; i < 4; ++i) {
        if (buildSettings.compressionMethod == compressionOptions[i]) {
            compressionIndex = i;
            break;
        }
    }
    if (ImGui::Combo("##compression", &compressionIndex, compressionOptions, 4)) {
        buildSettings.compressionMethod = compressionOptions[compressionIndex];
        changed = true;
    }
    ImGui::TextDisabled("Android support will unlock after OpenGLES is available.");
    ImGui::EndChild();

    ImGui::Separator();
    float buildWidth = 90.0f;
    float buildRunWidth = 120.0f;
    float buildTotal = buildWidth + buildRunWidth + buttonSpacing;
    float buildStart = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buildTotal;
    if (buildStart > ImGui::GetCursorPosX()) {
        ImGui::SetCursorPosX(buildStart);
    }
    if (ImGui::Button("Export Game", ImVec2(buildWidth, 0.0f))) {
        exportRunAfter = false;
        if (exportOutputPath[0] == '\0') {
            fs::path defaultOut = projectManager.currentProject.projectPath / "Builds";
            std::snprintf(exportOutputPath, sizeof(exportOutputPath), "%s", defaultOut.string().c_str());
        }
        showExportDialog = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Export & Run", ImVec2(buildRunWidth, 0.0f))) {
        exportRunAfter = true;
        if (exportOutputPath[0] == '\0') {
            fs::path defaultOut = projectManager.currentProject.projectPath / "Builds";
            std::snprintf(exportOutputPath, sizeof(exportOutputPath), "%s", defaultOut.string().c_str());
        }
        showExportDialog = true;
    }

    if (changed) {
        saveBuildSettings();
    }

    if (showExportDialog) {
        ImGui::SetNextWindowSize(ImVec2(720, 460), ImGuiCond_Appearing);
        ImGui::OpenPopup("Export Game");
        showExportDialog = false;
    }

    bool exportPopupOpen = true;
    ImGuiWindowFlags popupFlags = ImGuiWindowFlags_NoDocking;
    bool exportActive = false;
    bool exportDone = false;
    bool exportSuccess = false;
    float exportProgress = 0.0f;
    std::string exportStatus;
    std::string exportLog;
    fs::path exportDir;
    {
        std::lock_guard<std::mutex> lock(exportMutex);
        exportActive = exportJob.active;
        exportDone = exportJob.done;
        exportSuccess = exportJob.success;
        exportProgress = exportJob.progress;
        exportStatus = exportJob.status;
        exportLog = exportJob.log;
        exportDir = exportJob.outputDir;
    }
    bool allowClose = !exportActive;
    if (ImGui::BeginPopupModal("Export Game", allowClose ? &exportPopupOpen : nullptr, popupFlags)) {
        ImGui::Text("Output Folder");
        ImGui::SetNextItemWidth(-1);
        ImGui::BeginDisabled(exportActive);
        ImGui::InputText("##ExportOutput", exportOutputPath, sizeof(exportOutputPath));
        ImGui::EndDisabled();

        if (!exportActive) {
            if (ImGui::Button("Use Selected Folder")) {
                if (!fileBrowser.selectedFile.empty()) {
                    fs::path selected = fileBrowser.selectedFile;
                    fs::path folder = fs::is_directory(selected) ? selected : selected.parent_path();
                    std::snprintf(exportOutputPath, sizeof(exportOutputPath), "%s", folder.string().c_str());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Use Project Folder")) {
                fs::path folder = projectManager.currentProject.projectPath / "Builds";
                std::snprintf(exportOutputPath, sizeof(exportOutputPath), "%s", folder.string().c_str());
            }
        }

        ImGui::Spacing();
        if (exportActive || exportDone) {
            const char* statusLabel = exportStatus.empty() ? "Working..." : exportStatus.c_str();
            float barValue = exportActive ? exportProgress : 1.0f;
            if (barValue <= 0.0f) barValue = 0.02f;
            ImGui::ProgressBar(barValue, ImVec2(-1, 0), statusLabel);
            if (exportActive) {
                ImGui::TextDisabled("Build can take a while (PhysX/assimp). Output updates after each step finishes.");
            }
            ImGui::BeginChild("ExportLog", ImVec2(0, 180), true);
            if (exportLog.empty()) {
                ImGui::TextUnformatted("Waiting for build output...");
            } else {
                ImGui::TextUnformatted(exportLog.c_str());
            }
            ImGui::EndChild();
        }

        ImGui::Separator();
        if (!exportActive && !exportDone) {
            if (ImGui::Button("Start Export", ImVec2(120, 0))) {
                if (!exportOutputPath[0]) {
                    addConsoleMessage("Please choose an export folder.", ConsoleMessageType::Warning);
                } else {
                    startExportBuild(fs::path(exportOutputPath), exportRunAfter);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                ImGui::CloseCurrentPopup();
            }
        } else if (!exportActive && exportDone) {
            if (exportSuccess && !exportDir.empty()) {
                ImGui::TextDisabled("Exported to: %s", exportDir.string().c_str());
            }
            if (ImGui::Button("Close", ImVec2(100, 0))) {
                ImGui::CloseCurrentPopup();
                std::lock_guard<std::mutex> lock(exportMutex);
                exportJob = ExportJobState{};
            }
        } else {
            if (ImGui::Button("Cancel Export", ImVec2(140, 0))) {
                exportCancelRequested = true;
                std::lock_guard<std::mutex> lock(exportMutex);
                exportJob.status = "Cancelling...";
            }
        }

        ImGui::EndPopup();
    }

    ImGui::End();
}
