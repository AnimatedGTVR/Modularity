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

#pragma region Environment Window
void Engine::renderEnvironmentWindow() {
    if (!showEnvironmentWindow) return;
    ImGui::Begin("Environment", &showEnvironmentWindow);

    Skybox* skybox = renderer.getSkybox();
    if (skybox) {
        float tod = skybox->getTimeOfDay();
        ImGui::TextDisabled("Day / Night Cycle");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##EnvDayNight", &tod, 0.0f, 1.0f, "%.2f")) {
            skybox->setTimeOfDay(tod);
            projectManager.currentProject.hasUnsavedChanges = true;
        }

        static char skyVertBuf[256] = {};
        static char skyFragBuf[256] = {};
        if (skyVertBuf[0] == '\0') std::snprintf(skyVertBuf, sizeof(skyVertBuf), "%s", skybox->getVertPath().c_str());
        if (skyFragBuf[0] == '\0') std::snprintf(skyFragBuf, sizeof(skyFragBuf), "%s", skybox->getFragPath().c_str());

        ImGui::Separator();
        ImGui::Text("Skybox Shader");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##SkyVert", skyVertBuf, sizeof(skyVertBuf))) {}
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##SkyFrag", skyFragBuf, sizeof(skyFragBuf))) {}

        bool selectionIsShader = false;
        if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
            selectionIsShader = fileBrowser.getFileCategory(fs::directory_entry(fileBrowser.selectedFile)) == FileCategory::Shader;
        }
        ImGui::BeginDisabled(!selectionIsShader);
        if (ImGui::Button("Use Selection as Vert")) {
            std::snprintf(skyVertBuf, sizeof(skyVertBuf), "%s", fileBrowser.selectedFile.string().c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Use Selection as Frag")) {
            std::snprintf(skyFragBuf, sizeof(skyFragBuf), "%s", fileBrowser.selectedFile.string().c_str());
        }
        ImGui::EndDisabled();
        if (ImGui::Button("Reload Skybox Shader")) {
            skybox->setShaderPaths(skyVertBuf, skyFragBuf);
        }
    } else {
        ImGui::TextDisabled("Skybox not available");
    }

    ImGui::Separator();
    ImGui::Text("Global Ambient");
    glm::vec3 ambient = renderer.getAmbientColor();
    if (ImGui::ColorEdit3("##AmbientColor", &ambient.x, ImGuiColorEditFlags_DisplayRGB)) {
        renderer.setAmbientColor(ambient);
        projectManager.currentProject.hasUnsavedChanges = true;
    }

    ImGui::End();
}
#pragma endregion

#pragma region Camera Window
void Engine::renderCameraWindow() {
    if (!showCameraWindow) return;
    ImGui::Begin("Camera", &showCameraWindow);

    ImGui::TextDisabled("Movement");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("Base Speed", &camera.moveSpeed, 0.1f, 0.1f, 100.0f, "%.2f")) {
        camera.moveSpeed = std::max(0.01f, camera.moveSpeed);
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat("Sprint Speed", &camera.sprintSpeed, 0.1f, 0.1f, 200.0f, "%.2f")) {
        camera.sprintSpeed = std::max(camera.moveSpeed, camera.sprintSpeed);
    }
    ImGui::Checkbox("Smooth Movement", &camera.smoothMovement);
    ImGui::BeginDisabled(!camera.smoothMovement);
    ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat("Acceleration", &camera.acceleration, 0.1f, 0.1f, 100.0f, "%.2f");
    ImGui::EndDisabled();

    ImGui::End();
}
#pragma endregion
