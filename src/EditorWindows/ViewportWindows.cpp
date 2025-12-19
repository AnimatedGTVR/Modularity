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

namespace GizmoToolbar {
    enum class Icon {
        Translate,
        Rotate,
        Scale,
        Bounds,
        Universal
    };

    static ImVec4 ScaleColor(const ImVec4& c, float s) {
        return ImVec4(
            std::clamp(c.x * s, 0.0f, 1.0f),
            std::clamp(c.y * s, 0.0f, 1.0f),
            std::clamp(c.z * s, 0.0f, 1.0f),
            c.w
        );
    }
    
    static bool TextButton(const char* label, bool active, const ImVec2& size, ImU32 base, ImU32 hover, ImU32 activeCol, ImU32 accent, ImU32 textColor) {
        ImGui::PushStyleColor(ImGuiCol_Button, active ? accent : base);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? accent : hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, active ? accent : activeCol);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(textColor));
        bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(4);
        return pressed;
    }

    static void DrawTranslateIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        ImVec2 center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        float len = (max.x - min.x) * 0.3f;
        float head = len * 0.5f;

        drawList->AddLine(ImVec2(center.x - len, center.y), ImVec2(center.x + len, center.y), lineColor, 2.4f);
        drawList->AddLine(ImVec2(center.x, center.y - len), ImVec2(center.x, center.y + len), lineColor, 2.4f);

        drawList->AddTriangleFilled(ImVec2(center.x + len, center.y),
                                    ImVec2(center.x + len - head, center.y - head * 0.6f),
                                    ImVec2(center.x + len - head, center.y + head * 0.6f),
                                    accentColor);
        drawList->AddTriangleFilled(ImVec2(center.x - len, center.y),
                                    ImVec2(center.x - len + head, center.y - head * 0.6f),
                                    ImVec2(center.x - len + head, center.y + head * 0.6f),
                                    accentColor);
        drawList->AddTriangleFilled(ImVec2(center.x, center.y - len),
                                    ImVec2(center.x - head * 0.6f, center.y - len + head),
                                    ImVec2(center.x + head * 0.6f, center.y - len + head),
                                    accentColor);
        drawList->AddTriangleFilled(ImVec2(center.x, center.y + len),
                                    ImVec2(center.x - head * 0.6f, center.y + len - head),
                                    ImVec2(center.x + head * 0.6f, center.y + len - head),
                                    accentColor);

        drawList->AddCircleFilled(center, head * 0.35f, lineColor, 16);
    }

    static void DrawRotateIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        ImVec2 center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        float radius = (max.x - min.x) * 0.28f;
        float start = -IM_PI * 0.25f;
        float end = IM_PI * 1.1f;

        drawList->PathArcTo(center, radius, start, end, 32);
        drawList->PathStroke(lineColor, false, 2.4f);

        ImVec2 arrow = ImVec2(center.x + cosf(end) * radius, center.y + sinf(end) * radius);
        ImVec2 dir = ImVec2(cosf(end), sinf(end));
        ImVec2 ortho = ImVec2(-dir.y, dir.x);
        float head = radius * 0.5f;

        ImVec2 a = ImVec2(arrow.x - dir.x * head + ortho.x * head * 0.55f, arrow.y - dir.y * head + ortho.y * head * 0.55f);
        ImVec2 b = ImVec2(arrow.x - dir.x * head - ortho.x * head * 0.55f, arrow.y - dir.y * head - ortho.y * head * 0.55f);
        drawList->AddTriangleFilled(arrow, a, b, accentColor);
    }

    static void DrawScaleIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        ImVec2 pad = ImVec2((max.x - min.x) * 0.2f, (max.y - min.y) * 0.2f);
        ImVec2 rMin = ImVec2(min.x + pad.x, min.y + pad.y);
        ImVec2 rMax = ImVec2(max.x - pad.x, max.y - pad.y);

        drawList->AddRect(rMin, rMax, lineColor, 3.0f, 0, 2.1f);

        ImVec2 center = ImVec2((rMin.x + rMax.x) * 0.5f, (rMin.y + rMax.y) * 0.5f);
        ImVec2 offsets[] = {
            ImVec2(-1, -1),
            ImVec2(1, -1),
            ImVec2(1, 1),
            ImVec2(-1, 1)
        };
        float arrowLen = pad.x * 0.65f;
        float head = arrowLen * 0.5f;
        for (const ImVec2& off : offsets) {
            ImVec2 dir = ImVec2(off.x * 0.7f, off.y * 0.7f);
            ImVec2 tip = ImVec2(center.x + dir.x * arrowLen, center.y + dir.y * arrowLen);
            ImVec2 base = ImVec2(center.x + dir.x * (arrowLen * 0.45f), center.y + dir.y * (arrowLen * 0.45f));
            ImVec2 ortho = ImVec2(-dir.y, dir.x);
            ImVec2 a = ImVec2(base.x + ortho.x * head * 0.35f, base.y + ortho.y * head * 0.35f);
            ImVec2 b = ImVec2(base.x - ortho.x * head * 0.35f, base.y - ortho.y * head * 0.35f);
            drawList->AddTriangleFilled(tip, a, b, accentColor);
            drawList->AddLine(center, tip, lineColor, 2.0f);
        }
    }

    static void DrawBoundsIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        ImVec2 pad = ImVec2((max.x - min.x) * 0.2f, (max.y - min.y) * 0.22f);
        ImVec2 rMin = ImVec2(min.x + pad.x, min.y + pad.y);
        ImVec2 rMax = ImVec2(max.x - pad.x, max.y - pad.y);

        drawList->AddRect(rMin, rMax, lineColor, 4.0f, 0, 2.0f);

        float handle = pad.x * 0.6f;
        ImVec2 handles[] = {
            rMin,
            ImVec2((rMin.x + rMax.x) * 0.5f, rMin.y),
            ImVec2(rMax.x, rMin.y),
            ImVec2(rMax.x, (rMin.y + rMax.y) * 0.5f),
            rMax,
            ImVec2((rMin.x + rMax.x) * 0.5f, rMax.y),
            ImVec2(rMin.x, rMax.y),
            ImVec2(rMin.x, (rMin.y + rMax.y) * 0.5f)
        };

        for (const ImVec2& h : handles) {
            drawList->AddRectFilled(
                ImVec2(h.x - handle * 0.32f, h.y - handle * 0.32f),
                ImVec2(h.x + handle * 0.32f, h.y + handle * 0.32f),
                accentColor,
                4.0f
            );
        }
    }

    static void DrawUniversalIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        ImVec2 center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        float radius = (max.x - min.x) * 0.28f;

        drawList->AddCircle(center, radius, lineColor, 20, 2.0f);

        float len = radius * 0.95f;
        ImVec2 axes[] = {
            ImVec2(1, 0), ImVec2(-1, 0), ImVec2(0, 1), ImVec2(0, -1)
        };
        float head = radius * 0.45f;
        for (const ImVec2& dir : axes) {
            ImVec2 tip = ImVec2(center.x + dir.x * len, center.y + dir.y * len);
            drawList->AddLine(center, tip, accentColor, 2.0f);
            ImVec2 ortho = ImVec2(-dir.y, dir.x);
            ImVec2 a = ImVec2(tip.x - dir.x * head + ortho.x * head * 0.35f, tip.y - dir.y * head + ortho.y * head * 0.35f);
            ImVec2 b = ImVec2(tip.x - dir.x * head - ortho.x * head * 0.35f, tip.y - dir.y * head - ortho.y * head * 0.35f);
            drawList->AddTriangleFilled(tip, a, b, accentColor);
        }

        drawList->AddCircleFilled(center, radius * 0.24f, lineColor, 16);
    }

    static void DrawIcon(Icon icon, ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        switch (icon) {
            case Icon::Translate: DrawTranslateIcon(drawList, min, max, lineColor, accentColor); break;
            case Icon::Rotate:    DrawRotateIcon(drawList, min, max, lineColor, accentColor);    break;
            case Icon::Scale:     DrawScaleIcon(drawList, min, max, lineColor, accentColor);     break;
            case Icon::Bounds:    DrawBoundsIcon(drawList, min, max, lineColor, accentColor);    break;
            case Icon::Universal: DrawUniversalIcon(drawList, min, max, lineColor, accentColor); break;
        }
    }

    static bool IconButton(const char* id, Icon icon, bool active, const ImVec2& size,
                           ImU32 baseColor, ImU32 hoverColor, ImU32 activeColor,
                           ImU32 accentColor, ImU32 iconColor) {
        ImGui::PushID(id);
        ImGui::InvisibleButton("##btn", size);
        bool hovered = ImGui::IsItemHovered();
        bool pressed = ImGui::IsItemClicked();
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        float rounding = 9.0f;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 bg = active ? activeColor : (hovered ? hoverColor : baseColor);

        ImVec4 bgCol = ImGui::ColorConvertU32ToFloat4(bg);
        ImU32 top = ImGui::GetColorU32(ScaleColor(bgCol, 1.07f));
        ImU32 bottom = ImGui::GetColorU32(ScaleColor(bgCol, 0.93f));
        drawList->AddRectFilledMultiColor(min, max, top, top, bottom, bottom);
        drawList->AddRect(min, max, ImGui::GetColorU32(ImVec4(1, 1, 1, active ? 0.35f : 0.18f)), rounding);

        DrawIcon(icon, drawList, min, max, iconColor, accentColor);

        ImGui::PopID();
        return pressed;
    }

    static bool TextButton(const char* id, const char* label, bool active, const ImVec2& size,
                           ImU32 baseColor, ImU32 hoverColor, ImU32 activeColor, ImU32 borderColor, ImVec4 textColor) {
        ImGui::PushID(id);
        ImGui::InvisibleButton("##btn", size);
        bool hovered = ImGui::IsItemHovered();
        bool pressed = ImGui::IsItemClicked();
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        float rounding = 8.0f;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 bg = active ? activeColor : (hovered ? hoverColor : baseColor);

        ImVec4 bgCol = ImGui::ColorConvertU32ToFloat4(bg);
        ImU32 top = ImGui::GetColorU32(ScaleColor(bgCol, 1.06f));
        ImU32 bottom = ImGui::GetColorU32(ScaleColor(bgCol, 0.94f));
        drawList->AddRectFilledMultiColor(min, max, top, top, bottom, bottom);
        drawList->AddRect(min, max, borderColor, rounding);

        ImVec2 textSize = ImGui::CalcTextSize(label);
        ImVec2 textPos = ImVec2(
            min.x + (size.x - textSize.x) * 0.5f,
            min.y + (size.y - textSize.y) * 0.5f - 1.0f
        );
        drawList->AddText(textPos, ImGui::GetColorU32(textColor), label);

        ImGui::PopID();
        return pressed;
    }

    static bool ModeButton(const char* label, bool active, const ImVec2& size, ImVec4 baseColor, ImVec4 activeColor, ImVec4 textColor) {
        ImGui::PushStyleColor(ImGuiCol_Button, active ? activeColor : baseColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? activeColor : baseColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, active ? activeColor : baseColor);
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);
        bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(4);
        return pressed;
    }
}


void Engine::renderGameViewportWindow() {
    gameViewportFocused = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::Begin("Game Viewport", &showGameViewport, ImGuiWindowFlags_NoScrollbar);

    bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int width = std::max(160, (int)avail.x);
    int height = std::max(120, (int)avail.y);

    SceneObject* playerCam = nullptr;
    for (auto& obj : sceneObjects) {
        if (obj.type == ObjectType::Camera && obj.camera.type == SceneCameraType::Player) {
            playerCam = &obj;
            break;
        }
    }

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.09f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.12f, 0.14f, 0.16f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.14f, 0.18f, 0.20f, 1.0f));
    ImGui::BeginDisabled(playerCam == nullptr);
    bool dummyToggle = false;
    bool postFxChanged = false;
    if (playerCam) {
        bool before = playerCam->camera.applyPostFX;
        if (ImGui::Checkbox("Post FX", &playerCam->camera.applyPostFX)) {
            postFxChanged = (before != playerCam->camera.applyPostFX);
        }
    } else {
        ImGui::Checkbox("Post FX", &dummyToggle);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Text", &showUITextOverlay);
    ImGui::SameLine();
    ImGui::Checkbox("Canvas Guides", &showCanvasOverlay);
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);

    if (playerCam && postFxChanged) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }

    if (!isPlaying) {
        gameViewCursorLocked = false;
    }

        if (playerCam && rendererInitialized) {
        unsigned int tex = renderer.renderScenePreview(
            makeCameraFromObject(*playerCam),
            sceneObjects,
            width,
            height,
            playerCam->camera.fov,
            playerCam->camera.nearClip,
            playerCam->camera.farClip,
            playerCam->camera.applyPostFX
        );

        ImGui::Image((void*)(intptr_t)tex, ImVec2((float)width, (float)height), ImVec2(0, 1), ImVec2(1, 0));
        ImVec2 imageMin = ImGui::GetItemRectMin();
        ImVec2 imageMax = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (showCanvasOverlay) {
            ImVec2 pad(8.0f, 8.0f);
            ImVec2 tl(imageMin.x + pad.x, imageMin.y + pad.y);
            ImVec2 br(imageMax.x - pad.x, imageMax.y - pad.y);
            drawList->AddRect(tl, br, IM_COL32(110, 170, 255, 180), 8.0f, 0, 2.0f);
        }
        if (showUITextOverlay) {
            const char* textLabel = "Text Overlay";
            ImVec2 textPos(imageMin.x + 16.0f, imageMin.y + 16.0f);
            ImVec2 size = ImGui::CalcTextSize(textLabel);
            ImVec2 bgPad(6.0f, 4.0f);
            ImVec2 bgMin(textPos.x - bgPad.x, textPos.y - bgPad.y);
            ImVec2 bgMax(textPos.x + size.x + bgPad.x, textPos.y + size.y + bgPad.y);
            drawList->AddRectFilled(bgMin, bgMax, IM_COL32(20, 20, 24, 200), 4.0f);
            drawList->AddText(textPos, IM_COL32(235, 235, 245, 255), textLabel);
        }
        bool hovered = ImGui::IsItemHovered();
        bool clicked = hovered && isPlaying && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        if (clicked && !gameViewCursorLocked) {
            gameViewCursorLocked = true;
        }
        if (gameViewCursorLocked && (!isPlaying || !windowFocused || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
            gameViewCursorLocked = false;
        }

        gameViewportFocused = windowFocused && gameViewCursorLocked;
        ImGui::TextDisabled(gameViewCursorLocked ? "Camera captured (ESC to release)" : "Click to capture");
    } else {
        ImGui::TextDisabled("No player camera found (Camera Type: Player).");
        gameViewportFocused = ImGui::IsWindowFocused();
    }

    ImGui::End();
    ImGui::PopStyleVar();
}
void Engine::renderPlayControlsBar() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 buttonPadding(10.0f, 4.0f);
    const char* playLabel = isPlaying ? "Stop" : "Play";
    const char* pauseLabel = isPaused ? "Resume" : "Pause";
    const char* specLabel = specMode ? "Spec On" : "Spec Mode";

    auto buttonWidth = [&](const char* label) {
        ImVec2 textSize = ImGui::CalcTextSize(label);
        return textSize.x + buttonPadding.x * 2.0f + style.FrameBorderSize * 2.0f;
    };

    float playWidth = buttonWidth(playLabel);
    float pauseWidth = buttonWidth(pauseLabel);
    float specWidth = buttonWidth(specLabel);
    float spacing = style.ItemSpacing.x;
    float totalWidth = playWidth + pauseWidth + specWidth + spacing * 2.0f;

    // Center the controls inside the dockspace menu bar.
    float regionMinX = ImGui::GetWindowContentRegionMin().x;
    float regionMaxX = ImGui::GetWindowContentRegionMax().x;
    float regionWidth = regionMaxX - regionMinX;
    float startX = (regionWidth - totalWidth) * 0.5f + regionMinX;
    if (startX < regionMinX) startX = regionMinX;

    ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(startX, cursor.y));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, buttonPadding);
    bool playPressed = ImGui::Button(playLabel);
    ImGui::SameLine(0.0f, spacing);
    bool pausePressed = ImGui::Button(pauseLabel);
    ImGui::SameLine(0.0f, spacing);
    bool specPressed = ImGui::Button(specLabel);
    ImGui::PopStyleVar();

    if (playPressed) {
        bool newState = !isPlaying;
        if (newState) {
            if (physics.isReady() || physics.init()) {
                physics.onPlayStart(sceneObjects);
            } else {
                addConsoleMessage("PhysX failed to initialize; physics disabled for play mode", ConsoleMessageType::Warning);
            }
            audio.onPlayStart(sceneObjects);
        } else {
            physics.onPlayStop();
            audio.onPlayStop();
            isPaused = false;
            if (specMode && (physics.isReady() || physics.init())) {
                physics.onPlayStart(sceneObjects);
            }
        }
        isPlaying = newState;
    }
    if (pausePressed) {
        isPaused = !isPaused;
        if (isPaused) isPlaying = true; // placeholder: pausing implies we’re in play mode
    }
    if (specPressed) {
        bool enable = !specMode;
        if (enable && !physics.isReady() && !physics.init()) {
            addConsoleMessage("PhysX failed to initialize; spec mode disabled", ConsoleMessageType::Warning);
            enable = false;
        }
        specMode = enable;
        if (!isPlaying) {
            if (specMode) {
                physics.onPlayStart(sceneObjects);
                audio.onPlayStart(sceneObjects);
            } else {
                physics.onPlayStop();
                audio.onPlayStop();
            }
        }
    }
}

void Engine::renderMainMenuBar() {
    refreshScriptEditorWindows();

    if (ImGui::BeginMainMenuBar()) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 6.0f));
        ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
        ImVec4 subtle = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

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
                clearSelection();
                scriptEditorWindows.clear();
                scriptEditorWindowsDirty = true;
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
            ImGui::MenuItem("Project Manager", nullptr, &showProjectBrowser);
            ImGui::MenuItem("Mesh Builder", nullptr, &showMeshBuilder);
            ImGui::MenuItem("Environment", nullptr, &showEnvironmentWindow);
            ImGui::MenuItem("Camera", nullptr, &showCameraWindow);
            ImGui::MenuItem("View Output", nullptr, &showViewOutput);
            if (!scriptEditorWindows.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("Scripted Windows");
                for (auto& window : scriptEditorWindows) {
                    ImGui::MenuItem(window.label.c_str(), nullptr, &window.open);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Fullscreen Viewport", "F11", viewportFullscreen)) {
                viewportFullscreen = !viewportFullscreen;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Scripts")) {
            auto toggleSpec = [&](bool enabled) {
                if (specMode == enabled) return;
                if (enabled && !physics.isReady() && !physics.init()) {
                    addConsoleMessage("PhysX failed to initialize; spec mode disabled", ConsoleMessageType::Warning);
                    specMode = false;
                    return;
                }
                specMode = enabled;
                if (!isPlaying) {
                    if (specMode) physics.onPlayStart(sceneObjects);
                    else physics.onPlayStop();
                }
            };
            bool specValue = specMode;
            if (ImGui::MenuItem("Spec Mode (run Script_Spec)", nullptr, &specValue)) {
                toggleSpec(specValue);
            }
            ImGui::MenuItem("Test Mode (run Script_TestEditor)", nullptr, &testMode);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Cube")) addObject(ObjectType::Cube, "Cube");
            if (ImGui::MenuItem("Sphere")) addObject(ObjectType::Sphere, "Sphere");
            if (ImGui::MenuItem("Capsule")) addObject(ObjectType::Capsule, "Capsule");
            if (ImGui::MenuItem("Camera")) addObject(ObjectType::Camera, "Camera");
            if (ImGui::MenuItem("Directional Light")) addObject(ObjectType::DirectionalLight, "Directional Light");
            if (ImGui::MenuItem("Point Light")) addObject(ObjectType::PointLight, "Point Light");
            if (ImGui::MenuItem("Spot Light")) addObject(ObjectType::SpotLight, "Spot Light");
            if (ImGui::MenuItem("Area Light")) addObject(ObjectType::AreaLight, "Area Light");
            if (ImGui::MenuItem("Post FX Node")) addObject(ObjectType::PostFXNode, "Post FX");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                logToConsole("Modularity Engine v0.6.8");
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();
        ImGui::TextColored(subtle, "Project");
        ImGui::SameLine();
        std::string projectLabel = projectManager.currentProject.name.empty() ?
            "New Project" : projectManager.currentProject.name;
        ImGui::TextColored(accent, "%s", projectLabel.c_str());
        ImGui::SameLine();
        ImGui::TextColored(subtle, "|");
        ImGui::SameLine();
        std::string sceneLabel = projectManager.currentProject.currentSceneName.empty() ?
            "No Scene Loaded" : projectManager.currentProject.currentSceneName;
        ImGui::TextUnformatted(sceneLabel.c_str());

        float rightX = ImGui::GetWindowWidth() - 220.0f;
        if (rightX > ImGui::GetCursorPosX()) {
            ImGui::SameLine(rightX);
        } else {
            ImGui::SameLine();
        }
        ImGui::TextColored(subtle, "Viewport");
        ImGui::SameLine();
        ImGui::TextColored(accent, viewportFullscreen ? "Fullscreen" : "Docked");

        ImGui::PopStyleVar(2);
        ImGui::EndMainMenuBar();
    }
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

    const float toolbarHeight = 0.0f;
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
    bool blockSelection = false;

    if (rendererInitialized) {
        glm::mat4 proj = glm::perspective(
            glm::radians(FOV),
            (float)viewportWidth / (float)viewportHeight,
            NEAR_PLANE, FAR_PLANE
        );

        glm::mat4 view = camera.getViewMatrix();

        renderer.beginRender(view, proj, camera.position);
        renderer.renderScene(camera, sceneObjects, selectedObjectId);
        unsigned int tex = renderer.getViewportTexture();

        ImGui::Image((void*)(intptr_t)tex, imageSize, ImVec2(0, 1), ImVec2(1, 0));

        ImVec2 imageMin = ImGui::GetItemRectMin();
        ImVec2 imageMax = ImGui::GetItemRectMax();
        mouseOverViewportImage = ImGui::IsItemHovered();
        ImDrawList* viewportDrawList = ImGui::GetWindowDrawList();

        auto setCameraFacing = [&](const glm::vec3& dir) {
            glm::vec3 worldUp = glm::vec3(0, 1, 0);
            glm::vec3 n = glm::normalize(dir);
            glm::vec3 up = worldUp;
            if (std::abs(glm::dot(n, worldUp)) > 0.98f) {
                up = glm::vec3(0, 0, 1);
            }
            glm::vec3 right = glm::normalize(glm::cross(up, n));
            if (glm::length(right) < 1e-4f) {
                right = glm::vec3(1, 0, 0);
            }
            up = glm::normalize(glm::cross(n, right));

            camera.front = n;
            camera.up = up;
            camera.pitch = glm::degrees(std::asin(glm::clamp(n.y, -1.0f, 1.0f)));
            camera.pitch = glm::clamp(camera.pitch, -89.0f, 89.0f);
            camera.yaw = glm::degrees(std::atan2(n.z, n.x));
            camera.firstMouse = true;
        };

        // Draw small axis widget in top-right of viewport
        {
            const float widgetSize = 94.0f;
            const float padding = 12.0f;
            ImVec2 center = ImVec2(
                imageMax.x - padding - widgetSize * 0.5f,
                imageMin.y + padding + widgetSize * 0.5f
            );
            float radius = widgetSize * 0.46f;
            ImU32 ringCol = ImGui::GetColorU32(ImVec4(0.07f, 0.07f, 0.1f, 0.9f));
            ImU32 ringBorder = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.18f));
            viewportDrawList->AddCircleFilled(center, radius + 10.0f, ringCol, 48);
            viewportDrawList->AddCircle(center, radius + 10.0f, ringBorder, 48);
            viewportDrawList->AddCircle(center, radius + 3.0f, ImGui::GetColorU32(ImVec4(1,1,1,0.08f)), 32);
            viewportDrawList->AddCircleFilled(center, 5.5f, ImGui::GetColorU32(ImVec4(1,1,1,0.6f)), 24);

            glm::mat3 viewRot = glm::mat3(view);
            ImVec2 widgetMin = ImVec2(center.x - widgetSize * 0.5f, center.y - widgetSize * 0.5f);
            ImVec2 widgetMax = ImVec2(center.x + widgetSize * 0.5f, center.y + widgetSize * 0.5f);
            bool widgetHover = ImGui::IsMouseHoveringRect(widgetMin, widgetMax);
            struct AxisArrow {
                glm::vec3 dir;
                ImU32 color;
                const char* label;
            };
            AxisArrow arrows[] = {
                { glm::vec3(1, 0, 0), ImGui::GetColorU32(ImVec4(0.9f, 0.2f, 0.2f, 1.0f)), "X" },
                { glm::vec3(-1, 0, 0), ImGui::GetColorU32(ImVec4(0.6f, 0.2f, 0.2f, 1.0f)), "-X" },
                { glm::vec3(0, 1, 0), ImGui::GetColorU32(ImVec4(0.2f, 0.9f, 0.2f, 1.0f)), "Y" },
                { glm::vec3(0,-1, 0), ImGui::GetColorU32(ImVec4(0.2f, 0.6f, 0.2f, 1.0f)), "-Y" },
                { glm::vec3(0, 0, 1), ImGui::GetColorU32(ImVec4(0.2f, 0.4f, 0.9f, 1.0f)), "Z" },
                { glm::vec3(0, 0,-1), ImGui::GetColorU32(ImVec4(0.2f, 0.3f, 0.6f, 1.0f)), "-Z" },
            };

            ImVec2 mouse = ImGui::GetIO().MousePos;
            int clickedIdx = -1;
            float clickRadius = 12.0f;

            for (int i = 0; i < 6; ++i) {
                glm::vec3 camSpace = viewRot * arrows[i].dir;
                glm::vec2 dir2 = glm::normalize(glm::vec2(camSpace.x, -camSpace.y));
                float depthScale = glm::clamp(0.35f + 0.65f * ((camSpace.z + 1.0f) * 0.5f), 0.25f, 1.0f);
                float len = radius * depthScale;
                ImVec2 tip = ImVec2(center.x + dir2.x * len, center.y + dir2.y * len);

                ImVec2 base1 = ImVec2(center.x + dir2.x * (len * 0.55f) + dir2.y * (len * 0.12f),
                                      center.y + dir2.y * (len * 0.55f) - dir2.x * (len * 0.12f));
                ImVec2 base2 = ImVec2(center.x + dir2.x * (len * 0.55f) - dir2.y * (len * 0.12f),
                                      center.y + dir2.y * (len * 0.55f) + dir2.x * (len * 0.12f));

                viewportDrawList->AddTriangleFilled(base1, tip, base2, arrows[i].color);
                viewportDrawList->AddTriangle(base1, tip, base2, ImGui::GetColorU32(ImVec4(0,0,0,0.35f)));

                ImVec2 labelPos = ImVec2(center.x + dir2.x * (len * 0.78f), center.y + dir2.y * (len * 0.78f));
                viewportDrawList->AddCircleFilled(labelPos, 6.0f, ImGui::GetColorU32(ImVec4(0,0,0,0.5f)), 12);
                viewportDrawList->AddText(ImVec2(labelPos.x - 4.0f, labelPos.y - 7.0f), ImGui::GetColorU32(ImVec4(1,1,1,0.95f)), arrows[i].label);

                if (widgetHover) {
                    float dx = mouse.x - tip.x;
                    float dy = mouse.y - tip.y;
                    if (std::sqrt(dx*dx + dy*dy) <= clickRadius && ImGui::IsMouseReleased(0)) {
                        clickedIdx = i;
                    }
                }
            }

            if (clickedIdx >= 0) {
                setCameraFacing(arrows[clickedIdx].dir);
            }

            // Prevent viewport picking when interacting with the axis widget.
            if (widgetHover) {
                blockSelection = true;
            }
        }

        auto projectToScreen = [&](const glm::vec3& p) -> std::optional<ImVec2> {
            glm::vec4 clip = proj * view * glm::vec4(p, 1.0f);
            if (clip.w <= 0.0f) return std::nullopt;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            ImVec2 screen;
            screen.x = imageMin.x + (ndc.x * 0.5f + 0.5f) * (imageMax.x - imageMin.x);
            screen.y = imageMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * (imageMax.y - imageMin.y);
            return screen;
        };

        SceneObject* selectedObj = getSelectedObject();
        if (selectedObj && selectedObj->type != ObjectType::PostFXNode) {
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

            auto compose = [](const SceneObject& o) {
                glm::mat4 m(1.0f);
                m = glm::translate(m, o.position);
                m = glm::rotate(m, glm::radians(o.rotation.x), glm::vec3(1, 0, 0));
                m = glm::rotate(m, glm::radians(o.rotation.y), glm::vec3(0, 1, 0));
                m = glm::rotate(m, glm::radians(o.rotation.z), glm::vec3(0, 0, 1));
                m = glm::scale(m, o.scale);
                return m;
            };

            bool meshModeActive = meshEditMode && ensureMeshEditTarget(selectedObj);

            glm::vec3 pivotPos = selectedObj->position;
            if (!meshModeActive && selectedObjectIds.size() > 1 && mCurrentGizmoMode == ImGuizmo::WORLD) {
                pivotPos = getSelectionCenterWorld(true);
            }

            glm::mat4 modelMatrix(1.0f);
            modelMatrix = glm::translate(modelMatrix, pivotPos);
            modelMatrix = glm::rotate(modelMatrix, glm::radians(selectedObj->rotation.x), glm::vec3(1, 0, 0));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(selectedObj->rotation.y), glm::vec3(0, 1, 0));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(selectedObj->rotation.z), glm::vec3(0, 0, 1));
            modelMatrix = glm::scale(modelMatrix, selectedObj->scale);
            glm::mat4 originalModel = modelMatrix;

            if (meshModeActive && !meshEditAsset.positions.empty()) {
                // Build helper edge list (dedup) for edge/face modes
                std::vector<glm::u32vec2> edges;
                edges.reserve(meshEditAsset.faces.size() * 3);
                std::unordered_set<uint64_t> edgeSet;
                auto edgeKey = [](uint32_t a, uint32_t b) {
                    return (static_cast<uint64_t>(std::min(a,b)) << 32) | static_cast<uint64_t>(std::max(a,b));
                };
                for (size_t fi = 0; fi < meshEditAsset.faces.size(); ++fi) {
                    const auto& f = meshEditAsset.faces[fi];
                    uint32_t tri[3] = { f.x, f.y, f.z };
                    for (int e = 0; e < 3; ++e) {
                        uint32_t a = tri[e];
                        uint32_t b = tri[(e+1)%3];
                        uint64_t key = edgeKey(a,b);
                        if (edgeSet.insert(key).second) {
                            edges.push_back(glm::u32vec2(std::min(a,b), std::max(a,b)));
                        }
                    }
                }

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 vertCol = ImGui::GetColorU32(ImVec4(0.35f, 0.75f, 1.0f, 0.9f));
                ImU32 selCol  = ImGui::GetColorU32(ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
                ImU32 edgeCol = ImGui::GetColorU32(ImVec4(0.6f, 0.9f, 1.0f, 0.6f));
                ImU32 faceCol = ImGui::GetColorU32(ImVec4(1.0f, 0.8f, 0.4f, 0.7f));

                float selectRadius = 10.0f;
                ImVec2 mouse = ImGui::GetIO().MousePos;
                bool clicked = mouseOverViewportImage && ImGui::IsMouseClicked(0);
                bool additiveClick = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
                float bestDist = selectRadius;
                int clickedIndex = -1;

                glm::mat4 invModel = glm::inverse(modelMatrix);

                if (meshEditSelectionMode == MeshEditSelectionMode::Vertex) {
                    const size_t maxDraw = std::min<size_t>(meshEditAsset.positions.size(), 2000);
                    for (size_t i = 0; i < maxDraw; ++i) {
                        glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[i], 1.0f));
                        auto screen = projectToScreen(world);
                        if (!screen) continue;
                        bool sel = std::find(meshEditSelectedVertices.begin(), meshEditSelectedVertices.end(), (int)i) != meshEditSelectedVertices.end();
                        float radius = sel ? 6.5f : 5.0f;
                        dl->AddCircleFilled(*screen, radius, sel ? selCol : vertCol);

                        if (clicked) {
                            float dx = screen->x - mouse.x;
                            float dy = screen->y - mouse.y;
                            float dist = std::sqrt(dx*dx + dy*dy);
                            if (dist < bestDist) {
                                bestDist = dist;
                                clickedIndex = static_cast<int>(i);
                            }
                        }
                    }

                    if (clickedIndex >= 0) {
                        if (additiveClick) {
                            auto itSel = std::find(meshEditSelectedVertices.begin(), meshEditSelectedVertices.end(), clickedIndex);
                            if (itSel == meshEditSelectedVertices.end()) {
                                meshEditSelectedVertices.push_back(clickedIndex);
                            } else {
                                meshEditSelectedVertices.erase(itSel);
                            }
                        } else {
                            meshEditSelectedVertices.clear();
                            meshEditSelectedVertices.push_back(clickedIndex);
                        }
                        meshEditSelectedEdges.clear();
                        meshEditSelectedFaces.clear();
                    }

                    if (meshEditSelectedVertices.empty()) {
                        meshEditSelectedVertices.push_back(0);
                    }
                } else if (meshEditSelectionMode == MeshEditSelectionMode::Edge) {
                    for (size_t ei = 0; ei < edges.size(); ++ei) {
                        const auto& e = edges[ei];
                        glm::vec3 a = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[e.x], 1.0f));
                        glm::vec3 b = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[e.y], 1.0f));
                        auto sa = projectToScreen(a);
                        auto sb = projectToScreen(b);
                        if (!sa || !sb) continue;
                        ImVec2 mid = ImVec2((sa->x + sb->x) * 0.5f, (sa->y + sb->y) * 0.5f);
                        bool sel = std::find(meshEditSelectedEdges.begin(), meshEditSelectedEdges.end(), (int)ei) != meshEditSelectedEdges.end();
                        dl->AddLine(*sa, *sb, edgeCol, sel ? 3.0f : 2.0f);
                        dl->AddCircleFilled(mid, sel ? 6.0f : 4.0f, sel ? selCol : edgeCol);

                        if (clicked) {
                            float dx = mid.x - mouse.x;
                            float dy = mid.y - mouse.y;
                            float dist = std::sqrt(dx*dx + dy*dy);
                            if (dist < bestDist) {
                                bestDist = dist;
                                clickedIndex = static_cast<int>(ei);
                            }
                        }
                    }
                    if (clickedIndex >= 0) {
                        if (additiveClick) {
                            auto itSel = std::find(meshEditSelectedEdges.begin(), meshEditSelectedEdges.end(), clickedIndex);
                            if (itSel == meshEditSelectedEdges.end()) {
                                meshEditSelectedEdges.push_back(clickedIndex);
                            } else {
                                meshEditSelectedEdges.erase(itSel);
                            }
                        } else {
                            meshEditSelectedEdges.clear();
                            meshEditSelectedEdges.push_back(clickedIndex);
                        }
                        meshEditSelectedVertices.clear();
                        meshEditSelectedFaces.clear();
                    }
                    if (meshEditSelectedEdges.empty() && !edges.empty()) {
                        meshEditSelectedEdges.push_back(0);
                    }
                } else if (meshEditSelectionMode == MeshEditSelectionMode::Face) {
                    for (size_t fi = 0; fi < meshEditAsset.faces.size(); ++fi) {
                        const auto& f = meshEditAsset.faces[fi];
                        glm::vec3 a = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[f.x], 1.0f));
                        glm::vec3 b = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[f.y], 1.0f));
                        glm::vec3 c = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[f.z], 1.0f));
                        glm::vec3 centroid = (a + b + c) / 3.0f;
                        auto sc = projectToScreen(centroid);
                        if (!sc) continue;
                        bool sel = std::find(meshEditSelectedFaces.begin(), meshEditSelectedFaces.end(), (int)fi) != meshEditSelectedFaces.end();
                        dl->AddCircleFilled(*sc, sel ? 7.0f : 5.0f, sel ? selCol : faceCol);

                        if (clicked) {
                            float dx = sc->x - mouse.x;
                            float dy = sc->y - mouse.y;
                            float dist = std::sqrt(dx*dx + dy*dy);
                            if (dist < bestDist) {
                                bestDist = dist;
                                clickedIndex = static_cast<int>(fi);
                            }
                        }
                    }
                    if (clickedIndex >= 0) {
                        if (additiveClick) {
                            auto itSel = std::find(meshEditSelectedFaces.begin(), meshEditSelectedFaces.end(), clickedIndex);
                            if (itSel == meshEditSelectedFaces.end()) {
                                meshEditSelectedFaces.push_back(clickedIndex);
                            } else {
                                meshEditSelectedFaces.erase(itSel);
                            }
                        } else {
                            meshEditSelectedFaces.clear();
                            meshEditSelectedFaces.push_back(clickedIndex);
                        }
                        meshEditSelectedVertices.clear();
                        meshEditSelectedEdges.clear();
                    }
                    if (meshEditSelectedFaces.empty() && !meshEditAsset.faces.empty()) {
                        meshEditSelectedFaces.push_back(0);
                    }
                }

                // Compute affected vertices from selection
                std::vector<int> affectedVerts = meshEditSelectedVertices;
                auto pushUnique = [&](int idx) {
                    if (idx < 0) return;
                    if (std::find(affectedVerts.begin(), affectedVerts.end(), idx) == affectedVerts.end()) {
                        affectedVerts.push_back(idx);
                    }
                };
                if (meshEditSelectionMode == MeshEditSelectionMode::Edge) {
                    for (int ei : meshEditSelectedEdges) {
                        if (ei < 0 || ei >= (int)edges.size()) continue;
                        pushUnique(edges[ei].x);
                        pushUnique(edges[ei].y);
                    }
                } else if (meshEditSelectionMode == MeshEditSelectionMode::Face) {
                    for (int fi : meshEditSelectedFaces) {
                        if (fi < 0 || fi >= (int)meshEditAsset.faces.size()) continue;
                        const auto& f = meshEditAsset.faces[fi];
                        pushUnique(f.x);
                        pushUnique(f.y);
                        pushUnique(f.z);
                    }
                }
                if (affectedVerts.empty() && !meshEditAsset.positions.empty()) {
                    affectedVerts.push_back(0);
                }

                glm::vec3 pivotWorld(0.0f);
                for (int idx : affectedVerts) {
                    glm::vec3 wp = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[idx], 1.0f));
                    pivotWorld += wp;
                }
                pivotWorld /= (float)affectedVerts.size();

                glm::mat4 gizmoMat = glm::translate(glm::mat4(1.0f), pivotWorld);

                ImGuizmo::Manipulate(
                    glm::value_ptr(view),
                    glm::value_ptr(proj),
                    ImGuizmo::TRANSLATE,
                    ImGuizmo::WORLD,
                    glm::value_ptr(gizmoMat)
                );

                static bool meshEditHistoryCaptured = false;
                if (ImGuizmo::IsUsing()) {
                    if (!meshEditHistoryCaptured) {
                        recordState("meshEdit");
                        meshEditHistoryCaptured = true;
                    }
                    glm::vec3 deltaWorld = glm::vec3(gizmoMat[3]) - pivotWorld;
                    for (int idx : affectedVerts) {
                        glm::vec3 wp = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[idx], 1.0f));
                        wp += deltaWorld;
                        glm::vec3 newLocal = glm::vec3(invModel * glm::vec4(wp, 1.0f));
                        meshEditAsset.positions[idx] = newLocal;
                    }

                    // Recompute bounds
                    meshEditAsset.boundsMin = glm::vec3(FLT_MAX);
                    meshEditAsset.boundsMax = glm::vec3(-FLT_MAX);
                    for (const auto& p : meshEditAsset.positions) {
                        meshEditAsset.boundsMin.x = std::min(meshEditAsset.boundsMin.x, p.x);
                        meshEditAsset.boundsMin.y = std::min(meshEditAsset.boundsMin.y, p.y);
                        meshEditAsset.boundsMin.z = std::min(meshEditAsset.boundsMin.z, p.z);
                        meshEditAsset.boundsMax.x = std::max(meshEditAsset.boundsMax.x, p.x);
                        meshEditAsset.boundsMax.y = std::max(meshEditAsset.boundsMax.y, p.y);
                        meshEditAsset.boundsMax.z = std::max(meshEditAsset.boundsMax.z, p.z);
                    }

                    // Recompute normals
                    meshEditAsset.normals.assign(meshEditAsset.positions.size(), glm::vec3(0.0f));
                    for (const auto& f : meshEditAsset.faces) {
                        if (f.x >= meshEditAsset.positions.size() || f.y >= meshEditAsset.positions.size() || f.z >= meshEditAsset.positions.size()) continue;
                        const glm::vec3& a = meshEditAsset.positions[f.x];
                        const glm::vec3& b = meshEditAsset.positions[f.y];
                        const glm::vec3& c = meshEditAsset.positions[f.z];
                        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
                        meshEditAsset.normals[f.x] += n;
                        meshEditAsset.normals[f.y] += n;
                        meshEditAsset.normals[f.z] += n;
                    }
                    for (auto& n : meshEditAsset.normals) {
                        if (glm::length(n) > 1e-6f) n = glm::normalize(n);
                    }
                    meshEditAsset.hasNormals = true;

                    syncMeshEditToGPU(selectedObj);
                } else {
                    meshEditHistoryCaptured = false;
                }
            } else {
                // Object transform mode
                float* snapPtr = nullptr;
                float snapRot[3] = { rotationSnapValue, rotationSnapValue, rotationSnapValue };

                if (useSnap) {
                    if (mCurrentGizmoOperation == ImGuizmo::ROTATE) {
                        snapPtr = snapRot;
                    } else {
                        snapPtr = snapValue;
                    }
                }

                glm::vec3 gizmoBoundsMin(-0.5f);
                glm::vec3 gizmoBoundsMax(0.5f);

                switch (selectedObj->type) {
                    case ObjectType::Cube:
                        gizmoBoundsMin = glm::vec3(-0.5f);
                        gizmoBoundsMax = glm::vec3(0.5f);
                        break;
                    case ObjectType::Sphere:
                        gizmoBoundsMin = glm::vec3(-0.5f);
                        gizmoBoundsMax = glm::vec3(0.5f);
                        break;
                    case ObjectType::Capsule:
                        gizmoBoundsMin = glm::vec3(-0.35f, -0.9f, -0.35f);
                        gizmoBoundsMax = glm::vec3(0.35f, 0.9f, 0.35f);
                        break;
                    case ObjectType::OBJMesh: {
                        const auto* info = g_objLoader.getMeshInfo(selectedObj->meshId);
                        if (info && info->boundsMin.x < info->boundsMax.x) {
                            gizmoBoundsMin = info->boundsMin;
                            gizmoBoundsMax = info->boundsMax;
                        }
                        break;
                    }
                    case ObjectType::Model: {
                        const auto* info = getModelLoader().getMeshInfo(selectedObj->meshId);
                        if (info && info->boundsMin.x < info->boundsMax.x) {
                            gizmoBoundsMin = info->boundsMin;
                            gizmoBoundsMax = info->boundsMax;
                        }
                        break;
                    }
                    case ObjectType::Camera:
                        gizmoBoundsMin = glm::vec3(-0.3f);
                        gizmoBoundsMax = glm::vec3(0.3f);
                        break;
                    case ObjectType::DirectionalLight:
                    case ObjectType::PointLight:
                    case ObjectType::SpotLight:
                    case ObjectType::AreaLight:
                        gizmoBoundsMin = glm::vec3(-0.3f);
                        gizmoBoundsMax = glm::vec3(0.3f);
                        break;
                    case ObjectType::PostFXNode:
                        gizmoBoundsMin = glm::vec3(-0.25f);
                        gizmoBoundsMax = glm::vec3(0.25f);
                        break;
                }

                float bounds[6] = {
                    gizmoBoundsMin.x, gizmoBoundsMin.y, gizmoBoundsMin.z,
                    gizmoBoundsMax.x, gizmoBoundsMax.y, gizmoBoundsMax.z
                };
                float boundsSnap[3] = { snapValue[0], snapValue[1], snapValue[2] };
                const float* boundsPtr = (mCurrentGizmoOperation == ImGuizmo::BOUNDS) ? bounds : nullptr;
                const float* boundsSnapPtr = (useSnap && mCurrentGizmoOperation == ImGuizmo::BOUNDS) ? boundsSnap : nullptr;

                ImGuizmo::Manipulate(
                    glm::value_ptr(view),
                    glm::value_ptr(proj),
                    mCurrentGizmoOperation,
                    mCurrentGizmoMode,
                    glm::value_ptr(modelMatrix),
                    nullptr,
                    snapPtr,
                    boundsPtr,
                    boundsSnapPtr
                );

                std::array<glm::vec3, 8> corners = {
                    glm::vec3(gizmoBoundsMin.x, gizmoBoundsMin.y, gizmoBoundsMin.z),
                    glm::vec3(gizmoBoundsMax.x, gizmoBoundsMin.y, gizmoBoundsMin.z),
                    glm::vec3(gizmoBoundsMax.x, gizmoBoundsMax.y, gizmoBoundsMin.z),
                    glm::vec3(gizmoBoundsMin.x, gizmoBoundsMax.y, gizmoBoundsMin.z),
                    glm::vec3(gizmoBoundsMin.x, gizmoBoundsMin.y, gizmoBoundsMax.z),
                    glm::vec3(gizmoBoundsMax.x, gizmoBoundsMin.y, gizmoBoundsMax.z),
                    glm::vec3(gizmoBoundsMax.x, gizmoBoundsMax.y, gizmoBoundsMax.z),
                    glm::vec3(gizmoBoundsMin.x, gizmoBoundsMax.y, gizmoBoundsMax.z),
                };

                std::array<ImVec2, 8> projected{};
                bool allProjected = true;
                for (size_t i = 0; i < corners.size(); ++i) {
                    glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(corners[i], 1.0f));
                    auto p = projectToScreen(world);
                    if (!p.has_value()) { allProjected = false; break; }
                    projected[i] = *p;
                }

                if (allProjected) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 col = ImGui::GetColorU32(ImVec4(1.0f, 0.93f, 0.35f, 0.45f));
                    const int edges[12][2] = {
                        {0,1},{1,2},{2,3},{3,0},
                        {4,5},{5,6},{6,7},{7,4},
                        {0,4},{1,5},{2,6},{3,7}
                    };
                    for (auto& e : edges) {
                        dl->AddLine(projected[e[0]], projected[e[1]], col, 2.0f);
                    }
                }

                if (ImGuizmo::IsUsing()) {
                    if (!gizmoHistoryCaptured) {
                        recordState("gizmo");
                        gizmoHistoryCaptured = true;
                    }
                    glm::mat4 delta = modelMatrix * glm::inverse(originalModel);

                    auto applyDelta = [&](SceneObject& o) {
                        glm::mat4 m = compose(o);
                        glm::mat4 newM = delta * m;
                        glm::vec3 t, r, s;
                        DecomposeMatrix(newM, t, r, s);
                        o.position = t;
                        o.rotation = NormalizeEulerDegrees(glm::degrees(r));
                        o.scale = s;
                    };

                    if (selectedObjectIds.size() <= 1) {
                        applyDelta(*selectedObj);
                    } else {
                        for (int id : selectedObjectIds) {
                            auto itObj = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                                [id](const SceneObject& o){ return o.id == id; });
                            if (itObj != sceneObjects.end()) {
                                applyDelta(*itObj);
                            }
                        }
                    }

                    projectManager.currentProject.hasUnsavedChanges = true;
                } else {
                    gizmoHistoryCaptured = false;
                }
            }
        }

        auto drawCameraDirection = [&](const SceneObject& camObj) {
            glm::quat q = glm::quat(glm::radians(camObj.rotation));
            glm::mat3 rot = glm::mat3_cast(q);
            glm::vec3 forward = glm::normalize(rot * glm::vec3(0.0f, 0.0f, -1.0f));
            glm::vec3 upDir = glm::normalize(rot * glm::vec3(0.0f, 1.0f, 0.0f));
            if (!std::isfinite(forward.x) || glm::length(forward) < 1e-3f) return;

            auto start = projectToScreen(camObj.position);
            auto end = projectToScreen(camObj.position + forward * 1.4f);
            auto upTip = projectToScreen(camObj.position + upDir * 0.6f);
            if (start && end) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 lineCol = ImGui::GetColorU32(ImVec4(0.3f, 0.8f, 1.0f, 0.9f));
                ImU32 headCol = ImGui::GetColorU32(ImVec4(0.9f, 1.0f, 1.0f, 0.95f));
                dl->AddLine(*start, *end, lineCol, 2.5f);
                ImVec2 dir = ImVec2(end->x - start->x, end->y - start->y);
                float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
                if (len > 1.0f) {
                    ImVec2 normDir = ImVec2(dir.x / len, dir.y / len);
                    ImVec2 left = ImVec2(-normDir.y, normDir.x);
                    float head = 10.0f;
                    ImVec2 tip = *end;
                    ImVec2 p1 = ImVec2(tip.x - normDir.x * head + left.x * head * 0.6f, tip.y - normDir.y * head + left.y * head * 0.6f);
                    ImVec2 p2 = ImVec2(tip.x - normDir.x * head - left.x * head * 0.6f, tip.y - normDir.y * head - left.y * head * 0.6f);
                    dl->AddTriangleFilled(tip, p1, p2, headCol);
                }
                if (upTip) {
                    dl->AddCircleFilled(*upTip, 3.0f, ImGui::GetColorU32(ImVec4(0.8f, 1.0f, 0.6f, 0.8f)));
                }
            }
        };

        if (showSceneGizmos) {
            for (const auto& obj : sceneObjects) {
                if (obj.type == ObjectType::Camera) {
                    drawCameraDirection(obj);
                }
            }
        }

        // Light visualization overlays
        auto drawLightOverlays = [&](const SceneObject& lightObj) {
            if (!lightObj.light.enabled) return;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 col = ImGui::GetColorU32(ImVec4(1.0f, 0.9f, 0.4f, 0.7f));
            ImU32 faint = ImGui::GetColorU32(ImVec4(1.0f, 0.9f, 0.4f, 0.25f));
            auto forwardFromRotation = [](const SceneObject& obj) {
                glm::vec3 f = glm::normalize(glm::vec3(
                    glm::sin(glm::radians(obj.rotation.y)) * glm::cos(glm::radians(obj.rotation.x)),
                    glm::sin(glm::radians(obj.rotation.x)),
                    glm::cos(glm::radians(obj.rotation.y)) * glm::cos(glm::radians(obj.rotation.x))
                ));
                if (glm::length(f) < 1e-3f || !std::isfinite(f.x)) f = glm::vec3(0.0f, -1.0f, 0.0f);
                return f;
            };

            if (lightObj.type == ObjectType::PointLight) {
                auto center = projectToScreen(lightObj.position);
                glm::vec3 offset = lightObj.position + glm::vec3(lightObj.light.range, 0.0f, 0.0f);
                auto edge = projectToScreen(offset);
                if (center && edge) {
                    float r = std::sqrt((center->x - edge->x)*(center->x - edge->x) + (center->y - edge->y)*(center->y - edge->y));
                    dl->AddCircle(*center, r, faint, 48, 2.0f);
                }
            } else if (lightObj.type == ObjectType::SpotLight) {
                glm::vec3 dir = forwardFromRotation(lightObj);
                glm::vec3 tip = lightObj.position;
                glm::vec3 end = tip + dir * lightObj.light.range;
                float innerRad = glm::tan(glm::radians(lightObj.light.innerAngle)) * lightObj.light.range;
                float outerRad = glm::tan(glm::radians(lightObj.light.outerAngle)) * lightObj.light.range;

                // Build basis
                glm::vec3 up = glm::abs(dir.y) > 0.9f ? glm::vec3(1,0,0) : glm::vec3(0,1,0);
                glm::vec3 right = glm::normalize(glm::cross(dir, up));
                up = glm::normalize(glm::cross(right, dir));

                auto drawConeRing = [&](float radius, ImU32 color) {
                    const int segments = 24;
                    ImVec2 prev;
                    bool first = true;
                    for (int i = 0; i <= segments; ++i) {
                        float a = (float)i / segments * 2.0f * PI;
                        glm::vec3 p = end + right * std::cos(a) * radius + up * std::sin(a) * radius;
                        auto sp = projectToScreen(p);
                        if (!sp) continue;
                        if (first) { prev = *sp; first = false; continue; }
                        dl->AddLine(prev, *sp, color, 1.5f);
                        prev = *sp;
                    }
                };

                auto sTip = projectToScreen(tip);
                auto sEnd = projectToScreen(end);
                if (sTip && sEnd) {
                    dl->AddLine(*sTip, *sEnd, col, 2.0f);
                    drawConeRing(innerRad, col);
                    drawConeRing(outerRad, faint);
                }
            } else if (lightObj.type == ObjectType::AreaLight) {
                glm::vec3 n = forwardFromRotation(lightObj);
                glm::vec3 up = glm::abs(n.y) > 0.9f ? glm::vec3(1,0,0) : glm::vec3(0,1,0);
                glm::vec3 tangent = glm::normalize(glm::cross(up, n));
                glm::vec3 bitangent = glm::cross(n, tangent);
                glm::vec2 half = lightObj.light.size * 0.5f;
                glm::vec3 c = lightObj.position;
                glm::vec3 corners[4] = {
                    c + tangent * half.x + bitangent * half.y,
                    c - tangent * half.x + bitangent * half.y,
                    c - tangent * half.x - bitangent * half.y,
                    c + tangent * half.x - bitangent * half.y
                };
                ImVec2 projected[4];
                bool ok = true;
                for (int i = 0; i < 4; ++i) {
                    auto p = projectToScreen(corners[i]);
                    if (!p) { ok = false; break; }
                    projected[i] = *p;
                }
                if (ok) {
                    for (int i = 0; i < 4; ++i) {
                        dl->AddLine(projected[i], projected[(i+1)%4], col, 2.0f);
                    }
                    // normal indicator
                    auto cproj = projectToScreen(c);
                    auto nproj = projectToScreen(c + n * glm::max(lightObj.light.range, 0.5f));
                    if (cproj && nproj) {
                        dl->AddLine(*cproj, *nproj, col, 2.0f);
                        dl->AddCircleFilled(*nproj, 4.0f, col);
                    }
                }
            }
        };

        if (showSceneGizmos) {
            for (const auto& obj : sceneObjects) {
                if (obj.type == ObjectType::PointLight || obj.type == ObjectType::SpotLight || obj.type == ObjectType::AreaLight) {
                    drawLightOverlays(obj);
                }
            }
        }

        // Toolbar
        const float toolbarPadding = 6.0f;
        const float toolbarSpacing = 5.0f;
        const ImVec2 gizmoButtonSize(60.0f, 24.0f);
        const float toolbarWidthEstimate = 520.0f;
        const float toolbarHeightEstimate = 42.0f; // rough height to keep toolbar on-screen when anchoring bottom
        ImVec2 desiredBottomLeft = ImVec2(imageMin.x + 12.0f, imageMax.y - 12.0f);

        float minX = imageMin.x + 12.0f;
        float maxX = imageMax.x - 12.0f;
        float toolbarLeft = desiredBottomLeft.x;
        if (toolbarLeft + toolbarWidthEstimate > maxX) toolbarLeft = maxX - toolbarWidthEstimate;
        if (toolbarLeft < minX) toolbarLeft = minX;

        float minY = imageMin.y + 12.0f;
        float toolbarTop = desiredBottomLeft.y - toolbarHeightEstimate;
        if (toolbarTop < minY) toolbarTop = minY;

        ImVec2 toolbarPos = ImVec2(toolbarLeft, toolbarTop);

        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 bgCol = style.Colors[ImGuiCol_PopupBg];
        bgCol.w = 0.78f;
        ImVec4 baseCol = style.Colors[ImGuiCol_FrameBg];
        baseCol.w = 0.85f;
        ImVec4 hoverCol = style.Colors[ImGuiCol_ButtonHovered];
        hoverCol.w = 0.95f;
        ImVec4 activeCol = style.Colors[ImGuiCol_ButtonActive];
        activeCol.w = 1.0f;
        ImVec4 accentCol = style.Colors[ImGuiCol_HeaderActive];
        accentCol.w = 1.0f;
        ImVec4 textCol = style.Colors[ImGuiCol_Text];

        ImU32 baseBtn = ImGui::GetColorU32(baseCol);
        ImU32 hoverBtn = ImGui::GetColorU32(GizmoToolbar::ScaleColor(hoverCol, 1.05f));
        ImU32 activeBtn = ImGui::GetColorU32(GizmoToolbar::ScaleColor(activeCol, 1.08f));
        ImU32 accent = ImGui::GetColorU32(accentCol);
        ImU32 iconColor = ImGui::GetColorU32(ImVec4(0.95f, 0.98f, 1.0f, 0.95f));
        ImU32 toolbarBg = ImGui::GetColorU32(bgCol);
        ImU32 toolbarOutline = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.0f));

        ImDrawList* toolbarDrawList = ImGui::GetWindowDrawList();
        ImDrawListSplitter splitter;
        splitter.Split(toolbarDrawList, 2);
        splitter.SetCurrentChannel(toolbarDrawList, 1);

        ImVec2 contentStart = ImVec2(toolbarPos.x + toolbarPadding, toolbarPos.y + toolbarPadding);
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 contentStartLocal = ImVec2(contentStart.x - windowPos.x, contentStart.y - windowPos.y);
        ImGui::SetCursorPos(contentStartLocal);
        ImVec2 contentStartScreen = ImGui::GetCursorScreenPos();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(toolbarSpacing, toolbarSpacing));
        ImGui::BeginGroup();

        auto gizmoButton = [&](const char* label, ImGuizmo::OPERATION op, const char* tooltip) {
            if (GizmoToolbar::TextButton(label, mCurrentGizmoOperation == op, gizmoButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
                mCurrentGizmoOperation = op;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tooltip);
            }
        };

        gizmoButton("Move", ImGuizmo::TRANSLATE, "Translate");
        ImGui::SameLine(0.0f, toolbarSpacing);
        gizmoButton("Rotate", ImGuizmo::ROTATE, "Rotate");
        ImGui::SameLine(0.0f, toolbarSpacing);
        gizmoButton("Scale", ImGuizmo::SCALE, "Scale");
        ImGui::SameLine(0.0f, toolbarSpacing);
        bool canMeshEdit = false;
        if (selectedObj) {
            std::string ext = fs::path(selectedObj->meshPath).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            canMeshEdit = ext == ".rmesh";
        }
        ImGui::BeginDisabled(!canMeshEdit);
        if (GizmoToolbar::ModeButton("Mesh Edit", meshEditMode, gizmoButtonSize, baseCol, accentCol, textCol)) {
            meshEditMode = !meshEditMode;
            if (!meshEditMode) {
                meshEditLoaded = false;
                meshEditPath.clear();
                meshEditSelectedVertices.clear();
                meshEditSelectedEdges.clear();
                meshEditSelectedFaces.clear();
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle mesh vertex edit mode");
        ImGui::EndDisabled();
        if (meshEditMode) {
            ImGui::SameLine(0.0f, toolbarSpacing);
            if (GizmoToolbar::ModeButton("Verts", meshEditSelectionMode == MeshEditSelectionMode::Vertex, ImVec2(50,24), baseCol, accentCol, textCol)) {
                meshEditSelectionMode = MeshEditSelectionMode::Vertex;
            }
            ImGui::SameLine(0.0f, toolbarSpacing * 0.6f);
            if (GizmoToolbar::ModeButton("Edges", meshEditSelectionMode == MeshEditSelectionMode::Edge, ImVec2(50,24), baseCol, accentCol, textCol)) {
                meshEditSelectionMode = MeshEditSelectionMode::Edge;
            }
            ImGui::SameLine(0.0f, toolbarSpacing * 0.6f);
            if (GizmoToolbar::ModeButton("Faces", meshEditSelectionMode == MeshEditSelectionMode::Face, ImVec2(50,24), baseCol, accentCol, textCol)) {
                meshEditSelectionMode = MeshEditSelectionMode::Face;
            }
        }
        ImGui::SameLine(0.0f, toolbarSpacing);
        gizmoButton("Rect", ImGuizmo::BOUNDS, "Rect scale");
        ImGui::SameLine(0.0f, toolbarSpacing);
        gizmoButton("Uni", ImGuizmo::UNIVERSAL, "Universal");

        ImGui::SameLine(0.0f, toolbarSpacing * 1.25f);
        ImVec2 modeSize(56.0f, 24.0f);
        if (GizmoToolbar::ModeButton("Local", mCurrentGizmoMode == ImGuizmo::LOCAL, modeSize, baseCol, accentCol, textCol)) {
            mCurrentGizmoMode = ImGuizmo::LOCAL;
        }
        ImGui::SameLine(0.0f, toolbarSpacing * 0.8f);
        if (GizmoToolbar::ModeButton("World", mCurrentGizmoMode == ImGuizmo::WORLD, modeSize, baseCol, accentCol, textCol)) {
            mCurrentGizmoMode = ImGuizmo::WORLD;
        }

        ImGui::SameLine(0.0f, toolbarSpacing);
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

        ImGui::SameLine(0.0f, toolbarSpacing * 1.25f);
        if (GizmoToolbar::ModeButton("Gizmos", showSceneGizmos, ImVec2(70, 24), baseCol, accentCol, textCol)) {
            showSceneGizmos = !showSceneGizmos;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle light/camera scene symbols");
        }

        ImGui::EndGroup();
        ImGui::PopStyleVar();

        ImVec2 groupMax = ImGui::GetItemRectMax();

        splitter.SetCurrentChannel(toolbarDrawList, 0);
        float rounding = 10.0f;
        ImVec2 bgMin = ImVec2(contentStartScreen.x - toolbarPadding, contentStartScreen.y - toolbarPadding);
        ImVec2 bgMax = ImVec2(groupMax.x + toolbarPadding, groupMax.y + toolbarPadding);
        toolbarDrawList->AddRectFilled(bgMin, bgMax, toolbarBg, rounding, ImDrawFlags_RoundCornersAll);
        toolbarDrawList->AddRect(bgMin, bgMax, toolbarOutline, rounding, ImDrawFlags_RoundCornersAll, 1.5f);

        splitter.Merge(toolbarDrawList);

        // Prevent viewport picking when clicking on the toolbar overlay.
        if (ImGui::IsMouseHoveringRect(bgMin, bgMax)) {
            blockSelection = true;
        }

        // Left-click picking inside viewport
        if (mouseOverViewportImage &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGuizmo::IsUsing() && !ImGuizmo::IsOver() &&
            !blockSelection)
        {
            glm::mat4 invViewProj = glm::inverse(proj * view);
            ImVec2 mousePos = ImGui::GetMousePos();

            auto makeRay = [&](const ImVec2& pos) {
                float x = (pos.x - imageMin.x) / (imageMax.x - imageMin.x);
                float y = (pos.y - imageMin.y) / (imageMax.y - imageMin.y);
                x = x * 2.0f - 1.0f;
                y = 1.0f - y * 2.0f;

                glm::vec4 nearPt = invViewProj * glm::vec4(x, y, -1.0f, 1.0f);
                glm::vec4 farPt  = invViewProj * glm::vec4(x, y,  1.0f, 1.0f);
                nearPt /= nearPt.w;
                farPt  /= farPt.w;

                glm::vec3 origin = glm::vec3(nearPt);
                glm::vec3 dir = glm::normalize(glm::vec3(farPt - nearPt));
                return std::make_pair(origin, dir);
            };

            auto rayAabb = [](const glm::vec3& orig, const glm::vec3& dir, const glm::vec3& bmin, const glm::vec3& bmax, float& tHit) {
                float tmin = -FLT_MAX;
                float tmax = FLT_MAX;
                for (int i = 0; i < 3; ++i) {
                    if (std::abs(dir[i]) < 1e-6f) {
                        if (orig[i] < bmin[i] || orig[i] > bmax[i]) return false;
                        continue;
                    }
                    float invD = 1.0f / dir[i];
                    float t1 = (bmin[i] - orig[i]) * invD;
                    float t2 = (bmax[i] - orig[i]) * invD;
                    if (t1 > t2) std::swap(t1, t2);
                    tmin = std::max(tmin, t1);
                    tmax = std::min(tmax, t2);
                    if (tmin > tmax) return false;
                }
                tHit = (tmin >= 0.0f) ? tmin : tmax;
                return tmax >= 0.0f;
            };

            auto raySphere = [](const glm::vec3& orig, const glm::vec3& dir, float radius, float& tHit) {
                float b = glm::dot(dir, orig);
                float c = glm::dot(orig, orig) - radius * radius;
                float disc = b * b - c;
                if (disc < 0.0f) return false;
                float sqrtDisc = sqrtf(disc);
                float t0 = -b - sqrtDisc;
                float t1 = -b + sqrtDisc;
                float t = (t0 >= 0.0f) ? t0 : t1;
                if (t < 0.0f) return false;
                tHit = t;
                return true;
            };

            auto rayTriangle = [](const glm::vec3& orig, const glm::vec3& dir, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, float& tHit) {
                const float EPSILON = 1e-6f;
                glm::vec3 e1 = v1 - v0;
                glm::vec3 e2 = v2 - v0;
                glm::vec3 pvec = glm::cross(dir, e2);
                float det = glm::dot(e1, pvec);
                if (fabs(det) < EPSILON) return false;
                float invDet = 1.0f / det;
                glm::vec3 tvec = orig - v0;
                float u = glm::dot(tvec, pvec) * invDet;
                if (u < 0.0f || u > 1.0f) return false;
                glm::vec3 qvec = glm::cross(tvec, e1);
                float v = glm::dot(dir, qvec) * invDet;
                if (v < 0.0f || u + v > 1.0f) return false;
                float t = glm::dot(e2, qvec) * invDet;
                if (t < 0.0f) return false;
                tHit = t;
                return true;
            };

            auto ray = makeRay(mousePos);
            float closest = FLT_MAX;
            int hitId = -1;

            for (const auto& obj : sceneObjects) {
                glm::vec3 aabbMin(-0.5f);
                glm::vec3 aabbMax(0.5f);

                glm::mat4 model(1.0f);
                model = glm::translate(model, obj.position);
                model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1, 0, 0));
                model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
                model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0, 0, 1));
                model = glm::scale(model, obj.scale);

                glm::mat4 invModel = glm::inverse(model);
                glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(ray.first, 1.0f));
                glm::vec3 localDir = glm::normalize(glm::vec3(invModel * glm::vec4(ray.second, 0.0f)));

                float hitT = 0.0f;
                bool hit = false;
                switch (obj.type) {
                    case ObjectType::Cube:
                        hit = rayAabb(localOrigin, localDir, glm::vec3(-0.5f), glm::vec3(0.5f), hitT);
                        break;
                    case ObjectType::Sphere:
                        hit = raySphere(localOrigin, localDir, 0.5f, hitT);
                        break;
                    case ObjectType::Capsule:
                        hit = rayAabb(localOrigin, localDir, glm::vec3(-0.35f, -0.9f, -0.35f), glm::vec3(0.35f, 0.9f, 0.35f), hitT);
                        break;
                    case ObjectType::Mirror:
                        hit = rayAabb(localOrigin, localDir, glm::vec3(-0.5f, -0.5f, -0.02f), glm::vec3(0.5f, 0.5f, 0.02f), hitT);
                        break;
                    case ObjectType::OBJMesh: {
                        const auto* info = g_objLoader.getMeshInfo(obj.meshId);
                        if (info && info->boundsMin.x < info->boundsMax.x) {
                            aabbMin = info->boundsMin;
                            aabbMax = info->boundsMax;
                        }
                        bool aabbHit = rayAabb(localOrigin, localDir, aabbMin, aabbMax, hitT);
                        if (aabbHit && info && !info->triangleVertices.empty()) {
                            float triBest = FLT_MAX;
                            for (size_t i = 0; i + 2 < info->triangleVertices.size(); i += 3) {
                                float triT = 0.0f;
                                if (rayTriangle(localOrigin, localDir, info->triangleVertices[i], info->triangleVertices[i + 1], info->triangleVertices[i + 2], triT)) {
                                    if (triT < triBest && triT >= 0.0f) triBest = triT;
                                }
                            }
                            if (triBest < FLT_MAX) {
                                hit = true;
                                hitT = triBest;
                            } else {
                                hit = false;
                            }
                        } else {
                            hit = aabbHit;
                        }
                        break;
                    }
                    case ObjectType::Model: {
                        const auto* info = getModelLoader().getMeshInfo(obj.meshId);
                        if (info && info->boundsMin.x < info->boundsMax.x) {
                            aabbMin = info->boundsMin;
                            aabbMax = info->boundsMax;
                        }
                        bool aabbHit = rayAabb(localOrigin, localDir, aabbMin, aabbMax, hitT);
                        if (aabbHit && info && !info->triangleVertices.empty()) {
                            float triBest = FLT_MAX;
                            for (size_t i = 0; i + 2 < info->triangleVertices.size(); i += 3) {
                                float triT = 0.0f;
                                if (rayTriangle(localOrigin, localDir, info->triangleVertices[i], info->triangleVertices[i + 1], info->triangleVertices[i + 2], triT)) {
                                    if (triT < triBest && triT >= 0.0f) triBest = triT;
                                }
                            }
                            if (triBest < FLT_MAX) {
                                hit = true;
                                hitT = triBest;
                            } else {
                                hit = false;
                            }
                        } else {
                            hit = aabbHit;
                        }
                        break;
                    }
                    case ObjectType::Camera:
                        hit = raySphere(localOrigin, localDir, 0.3f, hitT);
                        break;
                    case ObjectType::DirectionalLight:
                    case ObjectType::PointLight:
                    case ObjectType::SpotLight:
                    case ObjectType::AreaLight:
                        hit = raySphere(localOrigin, localDir, 0.3f, hitT);
                        break;
                    case ObjectType::PostFXNode:
                        hit = false;
                        break;
                }

                if (hit && hitT < closest && hitT >= 0.0f) {
                    closest = hitT;
                    hitId = obj.id;
                }
            }

            viewportController.setFocused(true);
            if (hitId != -1) {
                bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
                setPrimarySelection(hitId, additive);
            } else {
                clearSelection();
            }
        }

        if (mouseOverViewportImage && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            viewportController.setFocused(true);
            cursorLocked = true;
            camera.firstMouse = true;
        }

        if (cursorLocked && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            cursorLocked = false;
            camera.firstMouse = true;
        }
        if (cursorLocked) {
            viewportController.setFocused(true);
        }

        if (isPlaying && showViewOutput) {
            std::vector<const SceneObject*> playerCams;
            for (const auto& obj : sceneObjects) {
                if (obj.type == ObjectType::Camera && obj.camera.type == SceneCameraType::Player) {
                    playerCams.push_back(&obj);
                }
            }

            if (playerCams.empty()) {
                previewCameraId = -1;
            } else {
                auto findCamById = [&](int id) -> const SceneObject* {
                    auto it = std::find_if(playerCams.begin(), playerCams.end(), [id](const SceneObject* o) { return o->id == id; });
                    return (it != playerCams.end()) ? *it : nullptr;
                };
                const SceneObject* previewCam = findCamById(previewCameraId);
                if (!previewCam) {
                    previewCam = playerCams.front();
                    previewCameraId = previewCam->id;
                }

                int previewWidth = static_cast<int>(imageSize.x * 0.28f);
                previewWidth = std::clamp(previewWidth, 180, 420);
                int previewHeight = static_cast<int>(previewWidth / 16.0f * 9.0f);
                unsigned int previewTex = renderer.renderScenePreview(
                    makeCameraFromObject(*previewCam),
                    sceneObjects,
                    previewWidth,
                    previewHeight,
                    previewCam->camera.fov,
                    previewCam->camera.nearClip,
                    previewCam->camera.farClip,
                    previewCam->camera.applyPostFX
                );

                if (previewTex != 0) {
                    ImVec2 overlaySize(previewWidth + 20.0f, previewHeight + 64.0f);
                    ImVec2 overlayPos = ImVec2(imageMax.x - overlaySize.x - 12.0f, imageMax.y - overlaySize.y - 12.0f);
                    ImVec2 winPos = ImGui::GetWindowPos();
                    ImVec2 localPos = ImVec2(overlayPos.x - winPos.x, overlayPos.y - winPos.y);
                    ImGui::SetCursorPos(localPos);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
                    ImGui::BeginChild("ViewOutputOverlay", overlaySize, true, ImGuiWindowFlags_NoScrollbar);
                    ImGui::TextDisabled("View Output");
                    if (ImGui::BeginCombo("##ViewOutputCamera", previewCam->name.c_str())) {
                        for (const auto* cam : playerCams) {
                            bool selected = cam->id == previewCameraId;
                            if (ImGui::Selectable(cam->name.c_str(), selected)) {
                                previewCameraId = cam->id;
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::Image((void*)(intptr_t)previewTex, ImVec2((float)previewWidth, (float)previewHeight), ImVec2(0, 1), ImVec2(1, 0));
                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                }
            }
        } else {
            previewCameraId = -1;
        }
    }

    // Overlay hint
    ImGui::SetCursorPos(ImVec2(10, 30));
    ImGui::TextColored(
        ImVec4(1, 1, 1, 0.3f),
        "Hold RMB: Look & Move | LMB: Select | WASD+QE: Move | ESC: Release | F11: Fullscreen"
    );

    if (cursorLocked) {
        ImGui::SetCursorPos(ImVec2(10, 50));
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Freelook Active");
    } else if (viewportController.isViewportFocused()) {
        ImGui::SetCursorPos(ImVec2(10, 50));
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Viewport Focused");
    }

    bool windowFocused = ImGui::IsWindowFocused();
    viewportController.updateFocusFromImGui(windowFocused, cursorLocked);

    ImGui::End();
}
