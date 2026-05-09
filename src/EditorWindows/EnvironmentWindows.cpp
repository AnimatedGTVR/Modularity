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

    static char sunTexBuf[512] = {};
    static char moonTexBuf[512] = {};
    static char scrollTexBuf[512] = {};
    static SkyboxSettings lastUiSkySettings;
    static int uiSkyMode = static_cast<int>(SkyboxMode::Procedural);
    static float uiScrollRepeat[2] = { 2.0f, 1.0f };
    static float uiScrollLookSensitivity = 1.0f;
    static float uiScrollVerticalInfluence = 0.18f;
    static bool uiEnvironmentReflections = false;
    static float uiEnvironmentReflectionIntensity = 0.5f;
    static float uiReflectionDistanceFade[2] = { 4.0f, 24.0f };
    static bool uiFogEnabled = false;
    static int uiFogMode = 0;
    static glm::vec3 uiFogColor = glm::vec3(0.65f, 0.72f, 0.78f);
    static float uiFogStart = 20.0f;
    static float uiFogEnd = 120.0f;
    static float uiFogDensity = 0.015f;
    static float uiFogHeight = 0.0f;
    static float uiFogHeightFalloff = 0.0f;
    static bool skyUiInitialized = false;
    if (!skyUiInitialized ||
        lastUiSkySettings.mode != skySettings.mode ||
        lastUiSkySettings.sunTexturePath != skySettings.sunTexturePath ||
        lastUiSkySettings.moonTexturePath != skySettings.moonTexturePath ||
        lastUiSkySettings.scrollingTexturePath != skySettings.scrollingTexturePath ||
        std::abs(lastUiSkySettings.scrollingRepeatX - skySettings.scrollingRepeatX) > 0.0001f ||
        std::abs(lastUiSkySettings.scrollingRepeatY - skySettings.scrollingRepeatY) > 0.0001f ||
        std::abs(lastUiSkySettings.scrollingLookSensitivity - skySettings.scrollingLookSensitivity) > 0.0001f ||
        std::abs(lastUiSkySettings.scrollingVerticalInfluence - skySettings.scrollingVerticalInfluence) > 0.0001f ||
        lastUiSkySettings.environmentReflections != skySettings.environmentReflections ||
        std::abs(lastUiSkySettings.environmentReflectionIntensity - skySettings.environmentReflectionIntensity) > 0.0001f ||
        std::abs(lastUiSkySettings.reflectionDistanceFadeStart - skySettings.reflectionDistanceFadeStart) > 0.0001f ||
        std::abs(lastUiSkySettings.reflectionDistanceFadeEnd - skySettings.reflectionDistanceFadeEnd) > 0.0001f ||
        lastUiSkySettings.fogEnabled != skySettings.fogEnabled ||
        lastUiSkySettings.fogMode != skySettings.fogMode ||
        glm::length(lastUiSkySettings.fogColor - skySettings.fogColor) > 0.0001f ||
        std::abs(lastUiSkySettings.fogStart - skySettings.fogStart) > 0.0001f ||
        std::abs(lastUiSkySettings.fogEnd - skySettings.fogEnd) > 0.0001f ||
        std::abs(lastUiSkySettings.fogDensity - skySettings.fogDensity) > 0.0001f ||
        std::abs(lastUiSkySettings.fogHeight - skySettings.fogHeight) > 0.0001f ||
        std::abs(lastUiSkySettings.fogHeightFalloff - skySettings.fogHeightFalloff) > 0.0001f) {
        std::snprintf(sunTexBuf, sizeof(sunTexBuf), "%s", skySettings.sunTexturePath.c_str());
        std::snprintf(moonTexBuf, sizeof(moonTexBuf), "%s", skySettings.moonTexturePath.c_str());
        std::snprintf(scrollTexBuf, sizeof(scrollTexBuf), "%s", skySettings.scrollingTexturePath.c_str());
        uiSkyMode = static_cast<int>(skySettings.mode);
        uiScrollRepeat[0] = skySettings.scrollingRepeatX;
        uiScrollRepeat[1] = skySettings.scrollingRepeatY;
        uiScrollLookSensitivity = skySettings.scrollingLookSensitivity;
        uiScrollVerticalInfluence = skySettings.scrollingVerticalInfluence;
        uiEnvironmentReflections = skySettings.environmentReflections;
        uiEnvironmentReflectionIntensity = skySettings.environmentReflectionIntensity;
        uiReflectionDistanceFade[0] = skySettings.reflectionDistanceFadeStart;
        uiReflectionDistanceFade[1] = skySettings.reflectionDistanceFadeEnd;
        uiFogEnabled = skySettings.fogEnabled;
        uiFogMode = skySettings.fogMode;
        uiFogColor = skySettings.fogColor;
        uiFogStart = skySettings.fogStart;
        uiFogEnd = skySettings.fogEnd;
        uiFogDensity = skySettings.fogDensity;
        uiFogHeight = skySettings.fogHeight;
        uiFogHeightFalloff = skySettings.fogHeightFalloff;
        lastUiSkySettings = skySettings;
        skyUiInitialized = true;
    }

    bool selectionIsTexture = false;
    if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
        selectionIsTexture = fileBrowser.getFileCategory(fs::directory_entry(fileBrowser.selectedFile)) == FileCategory::Texture;
    }

    auto beginSettingsTable = [&] (const char* id) {
        const float width = ImGui::GetContentRegionAvail().x;
        const float labelWidth = std::clamp(width * 0.28f, 78.0f, 118.0f);
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            return false;
        }
        ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, labelWidth);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    };

    auto drawSettingRow = [&] (const char* label, const std::function<void()>& drawControl) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);
        drawControl();
    };

    auto drawSection = [] (const char* label) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", label);
    };

    auto applySkySettings = [&] () {
        SkyboxSettings updated = skySettings;
        updated.mode = (uiSkyMode == static_cast<int>(SkyboxMode::Scrolling)) ? SkyboxMode::Scrolling : SkyboxMode::Procedural;
        updated.sunTexturePath = sunTexBuf;
        updated.moonTexturePath = moonTexBuf;
        updated.scrollingTexturePath = scrollTexBuf;
        updated.scrollingRepeatX = uiScrollRepeat[0];
        updated.scrollingRepeatY = uiScrollRepeat[1];
        updated.scrollingLookSensitivity = uiScrollLookSensitivity;
        updated.scrollingVerticalInfluence = uiScrollVerticalInfluence;
        updated.environmentReflections = uiEnvironmentReflections;
        updated.environmentReflectionIntensity = std::clamp(uiEnvironmentReflectionIntensity, 0.0f, 2.0f);
        updated.reflectionDistanceFadeStart = std::max(0.0f, uiReflectionDistanceFade[0]);
        updated.reflectionDistanceFadeEnd = std::max(updated.reflectionDistanceFadeStart + 0.01f, uiReflectionDistanceFade[1]);
        updated.fogEnabled = uiFogEnabled;
        updated.fogMode = std::clamp(uiFogMode, 0, 2);
        updated.fogColor = uiFogColor;
        updated.fogStart = std::max(0.0f, uiFogStart);
        updated.fogEnd = std::max(updated.fogStart + 0.01f, uiFogEnd);
        updated.fogDensity = std::clamp(uiFogDensity, 0.0f, 1.0f);
        updated.fogHeight = uiFogHeight;
        updated.fogHeightFalloff = std::clamp(uiFogHeightFalloff, 0.0f, 1.0f);
        applySceneSkyboxSettings(updated);
        projectManager.currentProject.hasUnsavedChanges = true;
        lastUiSkySettings = updated;
    };

    const char* skyModeLabels[] = { "Procedural Day/Night", "Scrolling 2.5D Background" };
    const char* fogModeLabels[] = { "Linear", "Exponential", "Exponential Squared" };

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 2.0f));
    if (ImGui::BeginTabBar("##EnvironmentTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("Skydome")) {
            if (beginSettingsTable("##EnvironmentSkyTable")) {
                drawSettingRow("Day / Night", [&] {
                    if (ImGui::SliderFloat("##EnvDayNight", &tod, 0.0f, 1.0f, "%.2f")) {
                        applySceneTimeOfDay(tod);
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                });
                drawSettingRow("Mode", [&] {
                    ImGui::Combo("##SkyMode", &uiSkyMode, skyModeLabels, IM_ARRAYSIZE(skyModeLabels));
                });
                drawSettingRow("Repeat", [&] {
                    ImGui::DragFloat2("##ScrollRepeat", uiScrollRepeat, 0.05f, 0.01f, 32.0f, "%.2f");
                });
                drawSettingRow("Look", [&] {
                    ImGui::SliderFloat("##LookSensitivity", &uiScrollLookSensitivity, 0.0f, 4.0f, "%.2f");
                });
                drawSettingRow("Vertical", [&] {
                    ImGui::SliderFloat("##VerticalLookSensitivity", &uiScrollVerticalInfluence, 0.0f, 1.0f, "%.2f");
                });
                ImGui::EndTable();
            }

            drawSection("Textures");
            if (beginSettingsTable("##EnvironmentTextureTable")) {
                drawSettingRow("Sun", [&] {
                    ImGui::InputText("##SunTexture", sunTexBuf, sizeof(sunTexBuf));
                });
                drawSettingRow("Moon", [&] {
                    ImGui::InputText("##MoonTexture", moonTexBuf, sizeof(moonTexBuf));
                });
                drawSettingRow("Background", [&] {
                    ImGui::InputText("##ScrollingBackground", scrollTexBuf, sizeof(scrollTexBuf));
                });
                drawSettingRow("Use Selection", [&] {
                    ImGui::BeginDisabled(!selectionIsTexture);
                    if (ImGui::Button("Sun")) {
                        std::snprintf(sunTexBuf, sizeof(sunTexBuf), "%s", fileBrowser.selectedFile.string().c_str());
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Moon")) {
                        std::snprintf(moonTexBuf, sizeof(moonTexBuf), "%s", fileBrowser.selectedFile.string().c_str());
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Background")) {
                        std::snprintf(scrollTexBuf, sizeof(scrollTexBuf), "%s", fileBrowser.selectedFile.string().c_str());
                    }
                    ImGui::EndDisabled();
                });
                drawSettingRow("Actions", [&] {
                    if (ImGui::Button("Built-in Sun/Moon")) {
                        std::snprintf(sunTexBuf, sizeof(sunTexBuf), "%s", "Resources/Engine-Root/Skybox/Sun Skybox.png");
                        std::snprintf(moonTexBuf, sizeof(moonTexBuf), "%s", "Resources/Engine-Root/Skybox/Moon Skybox.png");
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Apply")) {
                        applySkySettings();
                    }
                });
                ImGui::EndTable();
            }

            if (skybox && !vulkanPreviewMode) {
                static char skyVertBuf[256] = {};
                static char skyFragBuf[256] = {};
                if (skyVertBuf[0] == '\0') std::snprintf(skyVertBuf, sizeof(skyVertBuf), "%s", skybox->getVertPath().c_str());
                if (skyFragBuf[0] == '\0') std::snprintf(skyFragBuf, sizeof(skyFragBuf), "%s", skybox->getFragPath().c_str());

                bool selectionIsShader = false;
                if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
                    selectionIsShader = fileBrowser.getFileCategory(fs::directory_entry(fileBrowser.selectedFile)) == FileCategory::Shader;
                }

                drawSection("Skydome Shader");
                if (beginSettingsTable("##EnvironmentShaderTable")) {
                    drawSettingRow("Vertex", [&] {
                        ImGui::InputText("##SkyVert", skyVertBuf, sizeof(skyVertBuf));
                    });
                    drawSettingRow("Fragment", [&] {
                        ImGui::InputText("##SkyFrag", skyFragBuf, sizeof(skyFragBuf));
                    });
                    drawSettingRow("Use Selection", [&] {
                        ImGui::BeginDisabled(!selectionIsShader);
                        if (ImGui::Button("Vertex")) {
                            std::snprintf(skyVertBuf, sizeof(skyVertBuf), "%s", fileBrowser.selectedFile.string().c_str());
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Fragment")) {
                            std::snprintf(skyFragBuf, sizeof(skyFragBuf), "%s", fileBrowser.selectedFile.string().c_str());
                        }
                        ImGui::EndDisabled();
                    });
                    drawSettingRow("Actions", [&] {
                        if (ImGui::Button("Reload")) {
                            skybox->setShaderPaths(skyVertBuf, skyFragBuf);
                        }
                    });
                    ImGui::EndTable();
                }
            } else if (vulkanPreviewMode) {
                drawSection("Skydome Shader");
                ImGui::TextWrapped("Built-in Vulkan sky rendering is active. Custom OpenGL Skydome shader paths are edited only in OpenGL mode.");
            } else {
                ImGui::TextDisabled("Skydome not available in this backend");
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Environment")) {
            ImGui::TextDisabled("Reflections");
            if (beginSettingsTable("##EnvironmentReflectionTable")) {
                drawSettingRow("Enabled", [&] {
                    ImGui::Checkbox("##EnvironmentReflections", &uiEnvironmentReflections);
                });
                ImGui::BeginDisabled(!uiEnvironmentReflections);
                drawSettingRow("Intensity", [&] {
                    ImGui::SliderFloat("##ReflectionIntensity", &uiEnvironmentReflectionIntensity, 0.0f, 2.0f, "%.2f");
                });
                drawSettingRow("Distance", [&] {
                    if (ImGui::DragFloat2("##ReflectionDistanceFade", uiReflectionDistanceFade, 0.25f, 0.0f, 10000.0f, "%.1f")) {
                        uiReflectionDistanceFade[0] = std::max(0.0f, uiReflectionDistanceFade[0]);
                        uiReflectionDistanceFade[1] = std::max(uiReflectionDistanceFade[0] + 0.01f, uiReflectionDistanceFade[1]);
                    }
                });
                ImGui::EndDisabled();
                ImGui::EndTable();
            }

            drawSection("Fog");
            if (beginSettingsTable("##EnvironmentFogTable")) {
                drawSettingRow("Enabled", [&] {
                    ImGui::Checkbox("##EnableFog", &uiFogEnabled);
                });
                ImGui::BeginDisabled(!uiFogEnabled);
                drawSettingRow("Mode", [&] {
                    ImGui::Combo("##FogMode", &uiFogMode, fogModeLabels, IM_ARRAYSIZE(fogModeLabels));
                });
                drawSettingRow("Color", [&] {
                    ImGui::ColorEdit3("##FogColor", &uiFogColor.x, ImGuiColorEditFlags_DisplayRGB);
                });
                drawSettingRow("Start", [&] {
                    if (ImGui::DragFloat("##FogStart", &uiFogStart, 0.25f, 0.0f, 10000.0f, "%.1f")) {
                        uiFogStart = std::max(0.0f, uiFogStart);
                        uiFogEnd = std::max(uiFogStart + 0.01f, uiFogEnd);
                    }
                });
                drawSettingRow("End", [&] {
                    if (ImGui::DragFloat("##FogEnd", &uiFogEnd, 0.25f, 0.01f, 10000.0f, "%.1f")) {
                        uiFogEnd = std::max(uiFogStart + 0.01f, uiFogEnd);
                    }
                });
                drawSettingRow("Density", [&] {
                    ImGui::SliderFloat("##FogDensity", &uiFogDensity, 0.0f, 0.2f, "%.3f");
                });
                drawSettingRow("Height", [&] {
                    ImGui::DragFloat("##FogHeight", &uiFogHeight, 0.05f, -10000.0f, 10000.0f, "%.2f");
                });
                drawSettingRow("Falloff", [&] {
                    ImGui::SliderFloat("##FogHeightFalloff", &uiFogHeightFalloff, 0.0f, 1.0f, "%.2f");
                });
                ImGui::EndDisabled();
                ImGui::EndTable();
            }

            drawSection("Global Ambient");
            glm::vec3 ambient = renderer.getAmbientColor();
            if (beginSettingsTable("##EnvironmentAmbientTable")) {
                drawSettingRow("Color", [&] {
                    if (ImGui::ColorEdit3("##AmbientColor", &ambient.x, ImGuiColorEditFlags_DisplayRGB)) {
                        renderer.setAmbientColor(ambient);
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                });
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Lighting")) {
            int active3DLights = 0;
            for (const SceneObject& obj : sceneObjects) {
                if (IsObjectEnabledInHierarchy(obj) && obj.hasLight && obj.light.enabled && obj.light.intensity > 0.0f) {
                    ++active3DLights;
                }
            }

            ImGui::TextDisabled("Realtime Lighting");
            if (beginSettingsTable("##EnvironmentLightingTable")) {
                int maxLights = renderer.getMaxRealtimeLights();
                drawSettingRow("Max 3D Lights", [&] {
                    if (ImGui::SliderInt("##MaxRealtimeLights", &maxLights, 1, kRendererMaxRealtimeLights)) {
                        renderer.setMaxRealtimeLights(maxLights);
                        saveEditorUserSettings();
                    }
                });
                int shadowResolution = renderer.getShadowMapResolution();
                drawSettingRow("Shadow Res", [&] {
                    if (ImGui::DragInt("##DefaultShadowResolution", &shadowResolution, 64.0f, 128, 4096)) {
                        renderer.setShadowMapResolution(shadowResolution);
                    }
                });
                bool shaderAutoReload = renderer.isShaderAutoReloadEnabled();
                drawSettingRow("Shader Reload", [&] {
                    if (ImGui::Checkbox("##ShaderAutoReload", &shaderAutoReload)) {
                        renderer.setShaderAutoReload(shaderAutoReload);
                    }
                });
                drawSettingRow("2D Buffer", [&] {
                    if (ImGui::SliderFloat("##Light2DBufferScale", &light2DLightingBufferScale, 0.5f, 1.0f, "%.2fx")) {
                        light2DLightingBufferScale = std::clamp(light2DLightingBufferScale, 0.5f, 1.0f);
                        saveEditorUserSettings();
                    }
                });
                drawSettingRow("Active 3D", [&] {
                    ImGui::Text("%d / %d", active3DLights, renderer.getMaxRealtimeLights());
                });
                drawSettingRow("Active 2D", [&] {
                    ImGui::Text("%d", light2DActiveCountLastFrame);
                });
                drawSettingRow("Shadow Maps", [&] {
                    ImGui::Text("4 realtime shadow maps");
                });
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar(3);

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
