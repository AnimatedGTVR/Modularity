#include "Engine.h"

#if !MODULARITY_RUNTIME_ONLY

#include "ThirdParty/ModuGUI/imgui.h"

// collapsible touch Quick Tools panel: menu bar items are a pain to hit with a finger and
// there are no shortcuts on Android, so a little bottom-left launcher pops out a mini panel
// with Actions (Undo/Redo/Save + play controls) and Options (sticks, layout, pin, UI scale)
// tabs. gated on ImGuiConfigFlags_IsTouchScreen so the desktop editor never sees it.

void Engine::renderTouchEditToolbar() {
    ImGuiIO& io = ImGui::GetIO();
    if (!(io.ConfigFlags & ImGuiConfigFlags_IsTouchScreen)) return;
    if (showLauncher) return; // nothing to edit at the launcher

    const float scale = uiDpiScale > 0.0f ? uiDpiScale : 1.0f;

    const ImVec4 kAccent(0.22f, 0.40f, 0.58f, 1.0f); // "this option is on" blue

    // Auto-size each button to its label (+ generous touch padding) instead of a
    // fixed width, so scaled-up DPI text never overflows the button.
    const float h = 46.0f * scale;
    const float padX = 22.0f * scale;
    auto bigButton = [&](const char *label) {
        const ImVec2 ts = ImGui::CalcTextSize(label);
        return ImGui::Button(label, ImVec2(ts.x + padX * 2.0f, h));
    };

    // Play / Spec / Pause mirroring the desktop play bar (same icons + active/gray convention
    // as renderPlayControlsBar).
    auto playIcon = [&](const char *idStr, const char *coloredPath,
                        const char *grayPath, bool active,
                        const char *fallback) -> bool {
        const float sz = 50.0f * scale;
        Texture *tex =
            rendererInitialized
                ? renderer.getTexture(active ? coloredPath : grayPath,
                                      MaterialProperties::TextureFilter::Bilinear)
                : nullptr;
        if (tex && tex->GetID()) {
            // Editor PNG icons display V-flipped (bottom-up storage), matching the
            // desktop play bar and file browser.
            return ImGui::ImageButton(idStr, (ImTextureID)(intptr_t)tex->GetID(),
                                      ImVec2(sz, sz), ImVec2(0, 1), ImVec2(1, 0));
        }
        return ImGui::Button(fallback, ImVec2(sz + padX, sz));
    };

    // label-toggle that lights blue when on. latch the pre-press value first: gating
    // PopStyleColor on the now-flipped value unbalances the style stack and asserts.
    auto toggleButton = [&](const char *onLabel, const char *offLabel, bool *v) -> bool {
        const bool wasOn = *v;
        if (wasOn) ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        const bool clicked = bigButton(wasOn ? onLabel : offLabel);
        if (wasOn) ImGui::PopStyleColor();
        if (clicked) *v = !*v;
        return clicked;
    };

    ImGuiViewport *vp = ImGui::GetMainViewport();
    const ImVec2 workPos = vp->WorkPos;
    const ImVec2 workSize = vp->WorkSize;
    const float margin = 14.0f * scale;

    const ImGuiWindowFlags chromeFlags =
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;

    // Launcher button, pinned to the bottom-left corner
    const ImVec2 launcherAnchor(workPos.x + margin,
                                workPos.y + workSize.y - margin);
    float launcherHeight = 0.0f;
    ImGui::SetNextWindowPos(launcherAnchor, ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.0f); // the Button is its own visible chrome
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (ImGui::Begin("##QuickToolsLauncher", nullptr, chromeFlags)) {
        launcherHeight = ImGui::GetWindowSize().y;
        const float lh = 56.0f * scale;
        const char *label = quickToolsOpen ? "Tools v" : "Tools ^";
        if (quickToolsOpen) ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        const ImVec2 ts = ImGui::CalcTextSize(label);
        if (ImGui::Button(label, ImVec2(ts.x + padX * 2.0f, lh))) {
            quickToolsOpen = !quickToolsOpen;
        }
        if (quickToolsOpen) ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    if (!quickToolsOpen) return;

    // Popped-out panel, sitting just above the launcher button
    const ImVec2 panelAnchor(workPos.x + margin,
                             launcherAnchor.y - launcherHeight - 8.0f * scale);
    ImGui::SetNextWindowPos(panelAnchor, ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.94f);
    // Finger-sized controls inside the panel.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * scale, 8.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * scale, 8.0f * scale));
    if (ImGui::Begin("##QuickToolsPanel", nullptr, chromeFlags)) {
        // close after an action when not pinned. safe to flip mid-frame, the window finishes
        // drawing and just won't come back next frame.
        auto autoHide = [&]() { if (!quickToolsPinned) quickToolsOpen = false; };

        if (ImGui::BeginTabBar("##qtTabs")) {
            if (ImGui::BeginTabItem("Actions")) {
                ImGui::BeginDisabled(undoStack.empty());
                if (bigButton("Undo")) { undo(); autoHide(); }
                ImGui::EndDisabled();

                ImGui::SameLine();
                ImGui::BeginDisabled(redoStack.empty());
                if (bigButton("Redo")) { redo(); autoHide(); }
                ImGui::EndDisabled();

                ImGui::SameLine();
                const bool dirty = projectManager.currentProject.isLoaded &&
                                   projectManager.currentProject.hasUnsavedChanges;
                if (dirty) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.55f, 0.30f, 1.0f));
                ImGui::BeginDisabled(!projectManager.currentProject.isLoaded);
                if (bigButton(dirty ? "Save *" : "Save")) {
                    saveCurrentScene(/*allowLegacyUpgradePrompt=*/false);
                    autoHide();
                }
                ImGui::EndDisabled();
                if (dirty) ImGui::PopStyleColor();

                const bool projectLoaded = projectManager.currentProject.isLoaded;
                ImGui::BeginDisabled(!projectLoaded);
                if (playIcon("##qtPlay", "Resources/Engine-Root/Editor/Play Button.png",
                             "Resources/Engine-Root/Editor/Play Button Gray.png",
                             isPlaying, isPlaying ? "Stop" : "Play")) {
                    togglePlayMode();
                    autoHide();
                }
                ImGui::SameLine();
                if (playIcon("##qtSpec",
                             "Resources/Engine-Root/Editor/Spec Mode Button.png",
                             "Resources/Engine-Root/Editor/Spec Mode Button Gray.png",
                             specMode, "Spec")) {
                    toggleSpecMode();
                    autoHide();
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!isPlaying && !isPaused);
                if (playIcon("##qtPause", "Resources/Engine-Root/Editor/Pause Button.png",
                             "Resources/Engine-Root/Editor/Pause Button Gray.png",
                             isPaused, isPaused ? "Resume" : "Pause")) {
                    togglePause();
                    autoHide();
                }
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Options")) {
                // Any option change is written to the editor settings right away so
                // it survives a relaunch (matches how the rest of the editor saves).
                bool changed = false;

                changed |= toggleButton("Sticks: On", "Sticks: Off", &showTouchSticks);

                ImGui::BeginDisabled(!showTouchSticks);
                ImGui::SetNextItemWidth(220.0f * scale);
                if (ImGui::SliderFloat("Stick Size", &touchStickRadius, 28.0f, 80.0f, "%.0f"))
                    changed = true;
                ImGui::SetNextItemWidth(220.0f * scale);
                if (ImGui::SliderFloat("Look Sensitivity", &touchStickSensitivity, 2.0f, 14.0f, "%.1f"))
                    changed = true;
                changed |= toggleButton("Invert Y: On", "Invert Y: Off", &touchStickInvertY);
                ImGui::EndDisabled();

                changed |= toggleButton("Toolbar: Pinned", "Toolbar: Auto-hide", &quickToolsPinned);

                ImGui::Separator();
                ImGui::TextUnformatted("Layout");
                auto layoutButton = [&](const char *label, bool mobileValue) {
                    const bool on = (mobileEditorLayout == mobileValue);
                    if (on) ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
                    if (bigButton(label)) { mobileEditorLayout = mobileValue; changed = true; }
                    if (on) ImGui::PopStyleColor();
                };
                layoutButton("Desktop", false);
                ImGui::SameLine();
                layoutButton("Mobile", true);
                ImGui::TextDisabled("Mobile hides the top play bar");

                ImGui::Separator();
                ImGui::TextUnformatted("UI Scale");
                auto scaleButton = [&](const char *label, EditorChromeScale s) {
                    const bool on = (uiChromeScale == s);
                    if (on) ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
                    if (bigButton(label)) { uiChromeScale = s; changed = true; }
                    if (on) ImGui::PopStyleColor();
                };
                scaleButton("Compact", EditorChromeScale::Compact);
                ImGui::SameLine();
                scaleButton("Default", EditorChromeScale::Default);
                ImGui::SameLine();
                scaleButton("Big", EditorChromeScale::Big);

                if (changed) saveEditorUserSettings();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

#else
void Engine::renderTouchEditToolbar() {}
#endif // !MODULARITY_RUNTIME_ONLY
