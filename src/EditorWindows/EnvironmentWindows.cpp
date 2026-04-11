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

    const bool vulkanPreviewMode = usingVulkan();

    Skybox* skybox = renderer.getSkybox();
    float tod = getSceneTimeOfDay();
    SkyboxSettings skySettings = getSceneSkyboxSettings();
    ImGui::TextDisabled("Day / Night Cycle");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##EnvDayNight", &tod, 0.0f, 1.0f, "%.2f")) {
        applySceneTimeOfDay(tod);
        projectManager.currentProject.hasUnsavedChanges = true;
    }

    static char sunTexBuf[512] = {};
    static char moonTexBuf[512] = {};
    static char scrollTexBuf[512] = {};
    static SkyboxSettings lastUiSkySettings;
    static int uiSkyMode = static_cast<int>(SkyboxMode::Procedural);
    static float uiScrollRepeat[2] = { 2.0f, 1.0f };
    static float uiScrollLookSensitivity = 1.0f;
    static float uiScrollVerticalInfluence = 0.18f;
    static bool skyUiInitialized = false;
    if (!skyUiInitialized ||
        lastUiSkySettings.mode != skySettings.mode ||
        lastUiSkySettings.sunTexturePath != skySettings.sunTexturePath ||
        lastUiSkySettings.moonTexturePath != skySettings.moonTexturePath ||
        lastUiSkySettings.scrollingTexturePath != skySettings.scrollingTexturePath ||
        std::abs(lastUiSkySettings.scrollingRepeatX - skySettings.scrollingRepeatX) > 0.0001f ||
        std::abs(lastUiSkySettings.scrollingRepeatY - skySettings.scrollingRepeatY) > 0.0001f ||
        std::abs(lastUiSkySettings.scrollingLookSensitivity - skySettings.scrollingLookSensitivity) > 0.0001f ||
        std::abs(lastUiSkySettings.scrollingVerticalInfluence - skySettings.scrollingVerticalInfluence) > 0.0001f) {
        std::snprintf(sunTexBuf, sizeof(sunTexBuf), "%s", skySettings.sunTexturePath.c_str());
        std::snprintf(moonTexBuf, sizeof(moonTexBuf), "%s", skySettings.moonTexturePath.c_str());
        std::snprintf(scrollTexBuf, sizeof(scrollTexBuf), "%s", skySettings.scrollingTexturePath.c_str());
        uiSkyMode = static_cast<int>(skySettings.mode);
        uiScrollRepeat[0] = skySettings.scrollingRepeatX;
        uiScrollRepeat[1] = skySettings.scrollingRepeatY;
        uiScrollLookSensitivity = skySettings.scrollingLookSensitivity;
        uiScrollVerticalInfluence = skySettings.scrollingVerticalInfluence;
        lastUiSkySettings = skySettings;
        skyUiInitialized = true;
    }

    bool selectionIsTexture = false;
    if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
        selectionIsTexture = fileBrowser.getFileCategory(fs::directory_entry(fileBrowser.selectedFile)) == FileCategory::Texture;
    }

    ImGui::Separator();
    ImGui::Text("Skybox");
    const char* skyModeLabels[] = { "Procedural Day/Night", "Scrolling 2.5D Background" };
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("Mode", &uiSkyMode, skyModeLabels, IM_ARRAYSIZE(skyModeLabels));

    ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat2("Scroll Repeat", uiScrollRepeat, 0.05f, 0.01f, 32.0f, "%.2f");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("Look Sensitivity", &uiScrollLookSensitivity, 0.0f, 4.0f, "%.2f");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("Vertical Look Sensitivity", &uiScrollVerticalInfluence, 0.0f, 1.0f, "%.2f");

    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("Sun Texture", sunTexBuf, sizeof(sunTexBuf));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("Moon Texture", moonTexBuf, sizeof(moonTexBuf));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("Scrolling Background", scrollTexBuf, sizeof(scrollTexBuf));

    ImGui::BeginDisabled(!selectionIsTexture);
    if (ImGui::Button("Use Selection as Sun")) {
        std::snprintf(sunTexBuf, sizeof(sunTexBuf), "%s", fileBrowser.selectedFile.string().c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Use Selection as Moon")) {
        std::snprintf(moonTexBuf, sizeof(moonTexBuf), "%s", fileBrowser.selectedFile.string().c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Use Selection as Background")) {
        std::snprintf(scrollTexBuf, sizeof(scrollTexBuf), "%s", fileBrowser.selectedFile.string().c_str());
    }
    ImGui::EndDisabled();

    if (ImGui::Button("Use Built-in Sun / Moon")) {
        std::snprintf(sunTexBuf, sizeof(sunTexBuf), "%s", "Resources/Engine-Root/Skybox/Sun Skybox.png");
        std::snprintf(moonTexBuf, sizeof(moonTexBuf), "%s", "Resources/Engine-Root/Skybox/Moon Skybox.png");
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply Sky Settings")) {
        SkyboxSettings updated = skySettings;
        updated.mode = (uiSkyMode == static_cast<int>(SkyboxMode::Scrolling)) ? SkyboxMode::Scrolling : SkyboxMode::Procedural;
        updated.sunTexturePath = sunTexBuf;
        updated.moonTexturePath = moonTexBuf;
        updated.scrollingTexturePath = scrollTexBuf;
        updated.scrollingRepeatX = uiScrollRepeat[0];
        updated.scrollingRepeatY = uiScrollRepeat[1];
        updated.scrollingLookSensitivity = uiScrollLookSensitivity;
        updated.scrollingVerticalInfluence = uiScrollVerticalInfluence;
        applySceneSkyboxSettings(updated);
        projectManager.currentProject.hasUnsavedChanges = true;
        lastUiSkySettings = updated;
    }

    if (skybox && !vulkanPreviewMode) {
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
    } else if (vulkanPreviewMode) {
        ImGui::Separator();
        ImGui::TextDisabled("Vulkan Preview Mode");
        ImGui::TextWrapped("Built-in Vulkan sky rendering is active. Custom OpenGL skybox shader paths are edited only in OpenGL mode.");
    } else {
        ImGui::TextDisabled("Skybox not available in this backend");
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
