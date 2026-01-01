#include "Engine.h"
#include "ModelLoader.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <cfloat>
#include <cmath>
#include <functional>
#include <sstream>
#include <unordered_set>
#include <optional>
#include <future>
#include <chrono>
#include <future>

#ifdef _WIN32
#include <shlobj.h>
#endif

#pragma region ImGui Helpers
namespace ImGui {

// Animated progress bar that keeps circles moving while work happens in the background.
bool BufferingBar(const char* label, float value, const ImVec2& size_arg, const ImU32& bg_col, const ImU32& fg_col) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = size_arg;
    size.x -= style.FramePadding.x * 2;

    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return false;

    const float circleStart = size.x * 0.7f;
    const float circleEnd = size.x;
    const float circleWidth = circleEnd - circleStart;

    window->DrawList->AddRectFilled(bb.Min, ImVec2(pos.x + circleStart, bb.Max.y), bg_col);
    window->DrawList->AddRectFilled(bb.Min, ImVec2(pos.x + circleStart * value, bb.Max.y), fg_col);

    const float t = g.Time;
    const float r = size.y / 2;
    const float speed = 1.5f;

    const float a = speed * 0;
    const float b = speed * 0.333f;
    const float c = speed * 0.666f;

    const float o1 = (circleWidth + r) * (t + a - speed * (int)((t + a) / speed)) / speed;
    const float o2 = (circleWidth + r) * (t + b - speed * (int)((t + b) / speed)) / speed;
    const float o3 = (circleWidth + r) * (t + c - speed * (int)((t + c) / speed)) / speed;

    window->DrawList->AddCircleFilled(ImVec2(pos.x + circleEnd - o1, bb.Min.y + r), r, bg_col);
    window->DrawList->AddCircleFilled(ImVec2(pos.x + circleEnd - o2, bb.Min.y + r), r, bg_col);
    window->DrawList->AddCircleFilled(ImVec2(pos.x + circleEnd - o3, bb.Min.y + r), r, bg_col);
    return true;
}

// Simple loading spinner for indeterminate tasks.
bool Spinner(const char* label, float radius, int thickness, const ImU32& color) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size((radius) * 2, (radius + style.FramePadding.y) * 2);

    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return false;

    window->DrawList->PathClear();

    int num_segments = 30;
    int start = abs(ImSin(g.Time * 1.8f) * (num_segments - 5));

    const float a_min = IM_PI * 2.0f * ((float)start) / (float)num_segments;
    const float a_max = IM_PI * 2.0f * ((float)num_segments - 3) / (float)num_segments;

    const ImVec2 centre = ImVec2(pos.x + radius, pos.y + radius + style.FramePadding.y);

    for (int i = 0; i < num_segments; i++) {
        const float a = a_min + ((float)i / (float)num_segments) * (a_max - a_min);
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a + g.Time * 8) * radius,
                                            centre.y + ImSin(a + g.Time * 8) * radius));
    }

    window->DrawList->PathStroke(color, false, thickness);
    return true;
}

} // namespace ImGui
#pragma endregion

#pragma region Package Task State
namespace {
struct PackageTaskResult {
    bool success = false;
    std::string message;
};

struct PackageTaskState {
    bool active = false;
    float startTime = 0.0f;
    std::string label;
    std::future<PackageTaskResult> future;
};
} // namespace
#pragma endregion

#pragma region Launcher
void Engine::renderLauncher() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 24.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 18.0f);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
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
        const float leftPanelWidth = 300.0f;
        const float heroHeight = 120.0f;
        const ImVec4 bgTopLeft = ImVec4(0.10f, 0.11f, 0.16f, 1.0f);
        const ImVec4 bgTopRight = ImVec4(0.15f, 0.16f, 0.22f, 1.0f);
        const ImVec4 bgBottomRight = ImVec4(0.07f, 0.08f, 0.12f, 1.0f);
        const ImVec4 bgBottomLeft = ImVec4(0.08f, 0.09f, 0.13f, 1.0f);
        const ImVec4 cardBg = ImVec4(0.14f, 0.15f, 0.21f, 0.98f);
        const ImVec4 cardOutline = ImVec4(0.20f, 0.22f, 0.30f, 0.70f);
        const ImVec4 accent = ImVec4(0.91f, 0.42f, 0.78f, 1.0f);
        const ImVec4 accentCool = ImVec4(0.42f, 0.72f, 0.96f, 1.0f);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();
        drawList->AddRectFilledMultiColor(
            windowPos,
            ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y),
            ImGui::GetColorU32(bgTopLeft),
            ImGui::GetColorU32(bgTopRight),
            ImGui::GetColorU32(bgBottomRight),
            ImGui::GetColorU32(bgBottomLeft)
        );

        ImGui::BeginChild("LauncherHero", ImVec2(0, heroHeight), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

        ImDrawList* heroDraw = ImGui::GetWindowDrawList();
        ImVec2 heroPos = ImGui::GetWindowPos();
        ImVec2 heroSize = ImGui::GetWindowSize();
        heroDraw->AddRectFilled(heroPos, ImVec2(heroPos.x + heroSize.x, heroPos.y + heroSize.y),
                                ImGui::GetColorU32(cardBg), 18.0f);
        heroDraw->AddRect(heroPos, ImVec2(heroPos.x + heroSize.x, heroPos.y + heroSize.y),
                          ImGui::GetColorU32(cardBg), 18.0f);

        ImGui::SetCursorPos(ImVec2(28.0f, 24.0f));
        ImGui::TextDisabled("Project Manager");
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextColored(ImVec4(0.95f, 0.96f, 0.98f, 1.0f), "Modularity");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextColored(ImVec4(0.70f, 0.73f, 0.80f, 1.0f), "Modularity | Beta V1.0");


        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBg);
        ImGui::BeginChild("LauncherLeft", ImVec2(leftPanelWidth, 0), true);
        ImGui::PopStyleColor();

        ImGui::TextColored(ImVec4(0.78f, 0.80f, 0.86f, 1.0f), "GET STARTED");
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.32f, 0.22f, 0.54f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.43f, 0.30f, 0.70f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.38f, 0.26f, 0.60f, 1.0f));

        if (ImGui::Button("New Project", ImVec2(-1, 40.0f)))
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

        if (ImGui::Button("Open Project", ImVec2(-1, 40.0f)))
        {
            projectManager.showOpenProjectDialog = true;
            projectManager.errorMessage.clear();
        }

        ImGui::PopStyleColor(3);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.78f, 0.80f, 0.86f, 1.0f), "QUICK ACTIONS");
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.22f, 0.32f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.30f, 0.42f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.24f, 0.28f, 0.40f, 1.0f));

        if (ImGui::Button("Documentation", ImVec2(-1, 34.0f)))
        {
            #ifdef _WIN32
            system("start https://docs.shockinteractive.xyz");
            #else
            system("xdg-open https://docs.shockinteractive.xyz &");
            #endif
        }

        if (ImGui::Button("Exit", ImVec2(-1, 34.0f)))
        {
            glfwSetWindowShouldClose(editorWindow, GLFW_TRUE);
        }

        ImGui::PopStyleColor(3);

        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBg);
        ImGui::BeginChild("LauncherRight", ImVec2(0, 0), true);
        ImGui::PopStyleColor();

        ImGui::TextColored(ImVec4(0.78f, 0.80f, 0.86f, 1.0f), "RECENT PROJECTS");
        ImGui::Spacing();

        if (projectManager.recentProjects.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("There are no recent projects yet.\nCreate or open a project to get started.");
        }
        else
        {
            float availWidth = ImGui::GetContentRegionAvail().x;
            for (size_t i = 0; i < projectManager.recentProjects.size(); ++i)
            {
                const auto& rp = projectManager.recentProjects[i];
                ImGui::PushID(static_cast<int>(i));

                const ImVec2 cardSize(availWidth, 72.0f);
                const ImVec2 cardPos = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("RecentCard", cardSize);
                bool hovered = ImGui::IsItemHovered();
                bool clicked = ImGui::IsItemClicked();
                ImVec2 afterPos = ImGui::GetCursorScreenPos();

                if (ImGui::BeginPopupContextItem("RecentProjectContext"))
                {
                    if (ImGui::MenuItem("Open"))
                    {
                        OpenProjectPath(rp.path);
                    }

                    if (ImGui::MenuItem("Remove from Recent"))
                    {
                        projectManager.recentProjects.erase(
                            projectManager.recentProjects.begin() + i
                        );
                        projectManager.saveRecentProjects();
                        ImGui::EndPopup();
                        ImGui::PopID();
                        break;
                    }

                    ImGui::EndPopup();
                }

                ImU32 cardCol = ImGui::GetColorU32(hovered ? ImVec4(0.18f, 0.19f, 0.27f, 1.0f)
                                                          : ImVec4(0.16f, 0.17f, 0.24f, 1.0f));
                ImDrawList* list = ImGui::GetWindowDrawList();
                list->AddRectFilled(cardPos, ImVec2(cardPos.x + cardSize.x, cardPos.y + cardSize.y), cardCol, 14.0f);
                list->AddRect(cardPos, ImVec2(cardPos.x + cardSize.x, cardPos.y + cardSize.y),
                              ImGui::GetColorU32(cardOutline), 14.0f);

                ImVec2 textPos = ImVec2(cardPos.x + 16.0f, cardPos.y + 14.0f);
                ImGui::SetCursorScreenPos(textPos);
                ImGui::TextColored(ImVec4(0.92f, 0.93f, 0.96f, 1.0f), "%s", rp.name.c_str());
                ImGui::SetCursorScreenPos(ImVec2(textPos.x, textPos.y + 22.0f));
                ImGui::TextDisabled("%s", rp.path.c_str());

                const float buttonWidth = 88.0f;
                ImGui::SetCursorScreenPos(ImVec2(cardPos.x + cardSize.x - buttonWidth - 16.0f, cardPos.y + 20.0f));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.28f, 0.40f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.38f, 0.55f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.35f, 0.50f, 1.0f));
                bool openClicked = ImGui::Button("Open", ImVec2(buttonWidth, 30.0f));
                ImGui::PopStyleColor(3);

                if ((clicked && !openClicked) || openClicked)
                {
                    OpenProjectPath(rp.path);
                }

                ImGui::SetCursorScreenPos(afterPos);

                ImGui::PopID();
                ImGui::Spacing();
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("Modularity Engine - Beta V1.0");
        ImGui::EndChild();
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(5);

    if (projectManager.showNewProjectDialog)
        renderNewProjectDialog();
    if (projectManager.showOpenProjectDialog)
        renderOpenProjectDialog();

    if (projectLoadInProgress) {
        float elapsed = static_cast<float>(glfwGetTime() - projectLoadStartTime);
        if (elapsed > 0.15f) {
            ImGuiIO& io = ImGui::GetIO();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.06f, 0.08f, 0.65f));
            ImGui::Begin("ProjectLoadOverlay", nullptr,
                         ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoInputs);
            ImGui::End();
            ImGui::PopStyleColor();

            ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(420, 160));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.13f, 0.18f, 0.98f));
            ImGui::Begin("ProjectLoadCard", nullptr,
                         ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings);

            ImGui::TextColored(ImVec4(0.88f, 0.90f, 0.96f, 1.0f), "Loading project...");
            ImGui::Spacing();
            ImGui::TextDisabled("%s", projectLoadPath.c_str());
            ImGui::Spacing();
            ImGui::Spinner("##project_load_spinner", 16.0f, 4, ImGui::GetColorU32(ImGuiCol_ButtonHovered));
            ImGui::SameLine();
            ImGui::BufferingBar("##project_load_bar", std::fmod(elapsed * 0.25f, 1.0f),
                                ImVec2(ImGui::GetContentRegionAvail().x - 40.0f, 8.0f),
                                ImGui::GetColorU32(ImGuiCol_Button),
                                ImGui::GetColorU32(ImGuiCol_ButtonHovered));

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }
    }
}
#pragma endregion

#pragma region New Project Dialog
void Engine::renderNewProjectDialog() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 250), ImGuiCond_Appearing);

    if (ImGui::Begin("New Project", &projectManager.showNewProjectDialog,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {

        ImGui::Text("Project Name:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##ProjectName", projectManager.newProjectName,
                       sizeof(projectManager.newProjectName));

        ImGui::Spacing();

        if (ImGui::Button("Choose Pipeline Mode")) {
        }

        ImGui::Spacing();

        ImGui::Text("Location:");
        ImGui::SetNextItemWidth(-70);
        ImGui::InputText("##Location", projectManager.newProjectLocation,
                       sizeof(projectManager.newProjectLocation));
        ImGui::SameLine();
        if (ImGui::Button("Browse")) {
        }

        ImGui::Spacing();

        if (strlen(projectManager.newProjectName) > 0) {
            fs::path previewPath = fs::path(projectManager.newProjectLocation) /
                                  projectManager.newProjectName;
            ImGui::TextDisabled("Project will be created at:");
            ImGui::TextWrapped("%s", previewPath.string().c_str());
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
            memset(projectManager.newProjectName, 0, sizeof(projectManager.newProjectName));
        }

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.4f, 1.0f));
        if (ImGui::Button("Create", ImVec2(buttonWidth, 0))) {
            if (strlen(projectManager.newProjectName) == 0) {
                projectManager.errorMessage = "Please enter a project name";
            } else if (strlen(projectManager.newProjectLocation) == 0) {
                projectManager.errorMessage = "Please specify a location";
            } else {
                createNewProject(projectManager.newProjectName,
                               projectManager.newProjectLocation);
                projectManager.showNewProjectDialog = false;
            }
        }
        ImGui::PopStyleColor(2);
    }
    ImGui::End();
}
#pragma endregion

#pragma region Open Project Dialog
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
                OpenProjectPath(projectManager.openProjectPath);
                if (!projectManager.errorMessage.empty()) {
                    // Error handled in OpenProjectPath
                } else {
                    projectManager.showOpenProjectDialog = false;
                }
            }
        }
    }
    ImGui::End();
}
#pragma endregion

#pragma region Project Browser Panel
void Engine::renderProjectBrowserPanel() {
    ImVec4 headerCol = ImVec4(0.20f, 0.27f, 0.36f, 1.0f);
    ImVec4 headerColActive = ImVec4(0.24f, 0.34f, 0.46f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Header, headerCol);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, headerColActive);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, headerColActive);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));

    ImGui::Begin("Project Settings", &showProjectBrowser);

    if (!projectManager.currentProject.isLoaded) {
        ImGui::TextDisabled("No project loaded");
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        return;
    }

    ImGui::TextColored(ImVec4(0.4f, 0.7f, 0.95f, 1.0f), "%s", projectManager.currentProject.name.c_str());
    if (projectManager.currentProject.hasUnsavedChanges) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "*");
    }

    ImGui::Separator();

    static int selectedTab = 0;
    const char* tabs[] = { "Scenes", "Packages", "Assets" };

    ImGui::BeginChild("SettingsNav", ImVec2(180, 0), true);
    for (int i = 0; i < 3; ++i) {
        if (ImGui::Selectable(tabs[i], selectedTab == i, 0, ImVec2(0, 32))) {
            selectedTab = i;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("SettingsBody", ImVec2(0, 0), false);

    if (selectedTab == 0) {
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
    } else if (selectedTab == 1) {
        static PackageTaskState packageTask;
        static std::string packageStatus;
        static char gitUrlBuf[256] = "";
        static char gitNameBuf[128] = "";
        static char gitIncludeBuf[128] = "include";

        auto pollPackageTask = [&]() {
            if (!packageTask.active) return;
            if (!packageTask.future.valid()) {
                packageTask.active = false;
                return;
            }
            auto state = packageTask.future.wait_for(std::chrono::milliseconds(0));
            if (state == std::future_status::ready) {
                PackageTaskResult result = packageTask.future.get();
                packageStatus = result.message;
                packageTask.active = false;
                packageTask.future = std::future<PackageTaskResult>();
            }
        };

        auto startPackageTask = [&](const char* label, std::function<PackageTaskResult()> fn) {
            if (packageTask.active) return;
            packageTask.active = true;
            packageTask.label = label;
            packageTask.startTime = static_cast<float>(ImGui::GetTime());
            packageTask.future = std::async(std::launch::async, std::move(fn));
        };

        pollPackageTask();

        if (!packageStatus.empty()) {
            ImGui::TextWrapped("%s", packageStatus.c_str());
        }

        if (packageTask.active) {
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
            const ImU32 bg = ImGui::GetColorU32(ImGuiCol_Button);
            float elapsed = static_cast<float>(ImGui::GetTime()) - packageTask.startTime;
            float phase = std::fmod(elapsed * 0.25f, 1.0f);

            ImGui::Separator();
            ImGui::Text("%s", packageTask.label.c_str());
            ImGui::BufferingBar("##pkg_buffer", phase, ImVec2(ImGui::GetContentRegionAvail().x, 6.0f), bg, col);
            ImGui::Spinner("##pkg_spinner", 10.0f, 4, col);
        }

        ImGui::BeginDisabled(packageTask.active);
        ImGui::TextDisabled("Add package from Git");
        ImGui::InputTextWithHint("URL", "https://github.com/user/repo.git", gitUrlBuf, sizeof(gitUrlBuf));
        ImGui::InputTextWithHint("Name (optional)", "use repo name", gitNameBuf, sizeof(gitNameBuf));
        ImGui::InputTextWithHint("Include dir", "include", gitIncludeBuf, sizeof(gitIncludeBuf));
        if (ImGui::Button("Add as submodule")) {
            std::string url = gitUrlBuf;
            std::string name = gitNameBuf;
            std::string include = gitIncludeBuf;
            startPackageTask("Installing package...", [this, url, name, include]() {
                PackageTaskResult result;
                std::string newId;
                if (packageManager.installGitPackage(url, name, include, newId)) {
                    result.success = true;
                    result.message = "Installed package: " + newId;
                } else {
                    result.message = packageManager.getLastError();
                }
                return result;
            });
        }

        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::TextDisabled("Installed packages");
        if (packageTask.active) {
            ImGui::TextDisabled("Working...");
        } else {
            const auto& registry = packageManager.getRegistry();
            const auto& installedIds = packageManager.getInstalled();
            if (installedIds.empty()) {
                ImGui::TextDisabled("None installed");
            } else {
                for (const auto& id : installedIds) {
                    const PackageInfo* pkg = nullptr;
                    for (const auto& p : registry) {
                        if (p.id == id) { pkg = &p; break; }
                    }
                    if (!pkg) continue;

                    ImGui::PushID(pkg->id.c_str());
                    ImGui::Separator();
                    ImGui::Text("%s", pkg->name.c_str());
                    ImGui::TextDisabled("%s", pkg->description.c_str());
                    if (!pkg->external) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.4f, 0.7f, 0.95f, 1.0f), "[bundled]");
                        ImGui::PopID();
                        continue;
                    }

                    ImGui::TextDisabled("Path: %s", pkg->localPath.string().c_str());
                    ImGui::TextDisabled("Git: %s", pkg->gitUrl.c_str());

                    if (ImGui::Button("Check updates")) {
                        std::string id = pkg->id;
                        startPackageTask("Checking package status...", [this, id]() {
                            PackageTaskResult result;
                            std::string status;
                            if (packageManager.checkGitStatus(id, status)) {
                                result.success = true;
                                result.message = status;
                            } else {
                                result.message = packageManager.getLastError();
                            }
                            return result;
                        });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Update")) {
                        std::string id = pkg->id;
                        std::string name = pkg->name;
                        startPackageTask("Updating package...", [this, id, name]() {
                            PackageTaskResult result;
                            std::string log;
                            if (packageManager.updateGitPackage(id, log)) {
                                result.success = true;
                                result.message = "Updated " + name + "\n" + log;
                            } else {
                                result.message = packageManager.getLastError();
                            }
                            return result;
                        });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Uninstall")) {
                        std::string id = pkg->id;
                        std::string name = pkg->name;
                        startPackageTask("Removing package...", [this, id, name]() {
                            PackageTaskResult result;
                            if (packageManager.remove(id)) {
                                result.success = true;
                                result.message = "Removed " + name;
                            } else {
                                result.message = packageManager.getLastError();
                            }
                            return result;
                        });
                    }
                    ImGui::PopID();
                }
            }
        }
    } else if (selectedTab == 2) {
        ImGui::TextDisabled("Loaded OBJ Meshes");
        const auto& meshesObj = g_objLoader.getAllMeshes();
        if (meshesObj.empty()) {
            ImGui::TextDisabled("No meshes loaded");
            ImGui::TextDisabled("Import .obj files from File Browser");
        } else {
            for (size_t i = 0; i < meshesObj.size(); i++) {
                const auto& mesh = meshesObj[i];
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

        ImGui::Separator();
        ImGui::TextDisabled("Loaded Models (Assimp)");
        const auto& meshesAssimp = getModelLoader().getAllMeshes();
        if (meshesAssimp.empty()) {
            ImGui::TextDisabled("No models loaded");
            ImGui::TextDisabled("Import FBX/GLTF/other supported models from File Browser");
        } else {
            for (size_t i = 0; i < meshesAssimp.size(); i++) {
                const auto& mesh = meshesAssimp[i];
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
                                          ImGuiTreeNodeFlags_SpanAvailWidth |
                                          ImGuiTreeNodeFlags_NoTreePushOnOpen;

                ImGui::TreeNodeEx((void*)(intptr_t)(10000 + i), flags, "[A] %s", mesh.name.c_str());

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

    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}
#pragma endregion
