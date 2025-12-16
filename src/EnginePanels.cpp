#include "Engine.h"
#include "ModelLoader.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cfloat>
#include <cmath>
#include <sstream>
#include <unordered_set>
#include <optional>

#ifdef _WIN32
#include <shlobj.h>
#endif

namespace FileIcons {
    // Draw a folder icon
    void DrawFolderIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        float w = size;
        float h = size * 0.75f;
        float tabW = w * 0.4f;
        float tabH = h * 0.15f;
        
        // Folder body
        drawList->AddRectFilled(
            ImVec2(pos.x, pos.y + tabH),
            ImVec2(pos.x + w, pos.y + h),
            color, 3.0f
        );
        // Folder tab
        drawList->AddRectFilled(
            ImVec2(pos.x, pos.y),
            ImVec2(pos.x + tabW, pos.y + tabH + 2),
            color, 2.0f
        );
    }
    
    // Draw a scene/document icon
    void DrawSceneIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        float w = size * 0.8f;
        float h = size;
        float cornerSize = w * 0.25f;
        
        // Main document body
        ImVec2 p1 = ImVec2(pos.x, pos.y);
        ImVec2 p2 = ImVec2(pos.x + w - cornerSize, pos.y);
        ImVec2 p3 = ImVec2(pos.x + w, pos.y + cornerSize);
        ImVec2 p4 = ImVec2(pos.x + w, pos.y + h);
        ImVec2 p5 = ImVec2(pos.x, pos.y + h);
        
        drawList->AddQuadFilled(p1, ImVec2(pos.x + w - cornerSize, pos.y), ImVec2(pos.x + w - cornerSize, pos.y + h), p5, color);
        drawList->AddTriangleFilled(p2, p3, ImVec2(pos.x + w - cornerSize, pos.y + cornerSize), color);
        drawList->AddRectFilled(ImVec2(pos.x + w - cornerSize, pos.y + cornerSize), p4, color);
        
        // Corner fold
        drawList->AddTriangleFilled(p2, ImVec2(pos.x + w - cornerSize, pos.y + cornerSize), p3, 
            IM_COL32(255, 255, 255, 60));
        
        // Scene icon indicator (play triangle)
        float cx = pos.x + w * 0.5f;
        float cy = pos.y + h * 0.55f;
        float triSize = size * 0.25f;
        drawList->AddTriangleFilled(
            ImVec2(cx - triSize * 0.4f, cy - triSize * 0.5f),
            ImVec2(cx - triSize * 0.4f, cy + triSize * 0.5f),
            ImVec2(cx + triSize * 0.5f, cy),
            IM_COL32(255, 255, 255, 180)
        );
    }
    
    // Draw a 3D model icon (cube wireframe)
    void DrawModelIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        float s = size * 0.8f;
        float offset = size * 0.1f;
        float depth = s * 0.3f;
        
        // Front face
        ImVec2 f1 = ImVec2(pos.x + offset, pos.y + offset + depth);
        ImVec2 f2 = ImVec2(pos.x + offset + s, pos.y + offset + depth);
        ImVec2 f3 = ImVec2(pos.x + offset + s, pos.y + offset + s);
        ImVec2 f4 = ImVec2(pos.x + offset, pos.y + offset + s);
        
        // Back face
        ImVec2 b1 = ImVec2(f1.x + depth, f1.y - depth);
        ImVec2 b2 = ImVec2(f2.x + depth, f2.y - depth);
        ImVec2 b3 = ImVec2(f3.x + depth, f3.y - depth);
        
        // Fill front face
        drawList->AddQuadFilled(f1, f2, f3, f4, color);
        
        // Fill top face
        drawList->AddQuadFilled(f1, f2, b2, b1, IM_COL32(
            (color & 0xFF) * 0.7f,
            ((color >> 8) & 0xFF) * 0.7f,
            ((color >> 16) & 0xFF) * 0.7f,
            (color >> 24) & 0xFF
        ));
        
        // Fill right face
        drawList->AddQuadFilled(f2, b2, b3, f3, IM_COL32(
            (color & 0xFF) * 0.5f,
            ((color >> 8) & 0xFF) * 0.5f,
            ((color >> 16) & 0xFF) * 0.5f,
            (color >> 24) & 0xFF
        ));
        
        // Edges
        ImU32 edgeColor = IM_COL32(255, 255, 255, 100);
        drawList->AddLine(f1, f2, edgeColor, 1.0f);
        drawList->AddLine(f2, f3, edgeColor, 1.0f);
        drawList->AddLine(f3, f4, edgeColor, 1.0f);
        drawList->AddLine(f4, f1, edgeColor, 1.0f);
        drawList->AddLine(f1, b1, edgeColor, 1.0f);
        drawList->AddLine(f2, b2, edgeColor, 1.0f);
        drawList->AddLine(b1, b2, edgeColor, 1.0f);
        drawList->AddLine(f3, b3, edgeColor, 1.0f);
        drawList->AddLine(b2, b3, edgeColor, 1.0f);
    }
    
    // Draw a texture/image icon
    void DrawTextureIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        float padding = size * 0.1f;
        ImVec2 tl = ImVec2(pos.x + padding, pos.y + padding);
        ImVec2 br = ImVec2(pos.x + size - padding, pos.y + size - padding);
        
        // Frame
        drawList->AddRectFilled(tl, br, color, 2.0f);
        
        // Mountain landscape
        float midY = pos.y + size * 0.6f;
        drawList->AddTriangleFilled(
            ImVec2(pos.x + size * 0.2f, br.y - padding),
            ImVec2(pos.x + size * 0.45f, midY),
            ImVec2(pos.x + size * 0.7f, br.y - padding),
            IM_COL32(60, 60, 60, 255)
        );
        drawList->AddTriangleFilled(
            ImVec2(pos.x + size * 0.5f, br.y - padding),
            ImVec2(pos.x + size * 0.7f, midY + size * 0.1f),
            ImVec2(pos.x + size * 0.9f, br.y - padding),
            IM_COL32(80, 80, 80, 255)
        );
        
        // Sun
        float sunR = size * 0.1f;
        drawList->AddCircleFilled(ImVec2(pos.x + size * 0.75f, pos.y + size * 0.35f), sunR, IM_COL32(255, 220, 100, 255));
    }
    
    // Draw a shader icon (code brackets)
    void DrawShaderIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        float padding = size * 0.15f;
        ImVec2 tl = ImVec2(pos.x + padding, pos.y + padding);
        ImVec2 br = ImVec2(pos.x + size - padding, pos.y + size - padding);
        
        // Background
        drawList->AddRectFilled(tl, br, color, 3.0f);
        
        // Code lines
        ImU32 lineColor = IM_COL32(255, 255, 255, 180);
        float lineY = pos.y + size * 0.35f;
        float lineH = size * 0.08f;
        float lineSpacing = size * 0.15f;
        
        drawList->AddRectFilled(ImVec2(pos.x + size * 0.25f, lineY), ImVec2(pos.x + size * 0.7f, lineY + lineH), lineColor);
        lineY += lineSpacing;
        drawList->AddRectFilled(ImVec2(pos.x + size * 0.3f, lineY), ImVec2(pos.x + size * 0.8f, lineY + lineH), lineColor);
        lineY += lineSpacing;
        drawList->AddRectFilled(ImVec2(pos.x + size * 0.25f, lineY), ImVec2(pos.x + size * 0.55f, lineY + lineH), lineColor);
    }
    
    // Draw an audio icon (speaker/waveform)
    void DrawAudioIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        // Speaker body
        float spkW = size * 0.25f;
        float spkH = size * 0.3f;
        float cx = pos.x + size * 0.35f;
        float cy = pos.y + size * 0.5f;
        
        drawList->AddRectFilled(
            ImVec2(cx - spkW * 0.5f, cy - spkH * 0.5f),
            ImVec2(cx + spkW * 0.5f, cy + spkH * 0.5f),
            color
        );
        
        // Speaker cone
        drawList->AddTriangleFilled(
            ImVec2(cx + spkW * 0.5f, cy - spkH * 0.5f),
            ImVec2(cx + spkW * 0.5f, cy + spkH * 0.5f),
            ImVec2(cx + spkW * 1.2f, cy + spkH),
            color
        );
        drawList->AddTriangleFilled(
            ImVec2(cx + spkW * 0.5f, cy - spkH * 0.5f),
            ImVec2(cx + spkW * 1.2f, cy - spkH),
            ImVec2(cx + spkW * 1.2f, cy + spkH),
            color
        );
        
        // Sound waves
        ImU32 waveColor = IM_COL32(255, 255, 255, 150);
        float waveX = cx + spkW * 1.5f;
        drawList->AddBezierQuadratic(
            ImVec2(waveX, cy - size * 0.15f),
            ImVec2(waveX + size * 0.1f, cy),
            ImVec2(waveX, cy + size * 0.15f),
            waveColor, 2.0f
        );
        waveX += size * 0.12f;
        drawList->AddBezierQuadratic(
            ImVec2(waveX, cy - size * 0.22f),
            ImVec2(waveX + size * 0.12f, cy),
            ImVec2(waveX, cy + size * 0.22f),
            waveColor, 2.0f
        );
    }
    
    // Draw a generic file icon
    void DrawFileIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        float w = size * 0.7f;
        float h = size * 0.9f;
        float offsetX = (size - w) * 0.5f;
        float offsetY = (size - h) * 0.5f;
        float cornerSize = w * 0.25f;
        
        ImVec2 p1 = ImVec2(pos.x + offsetX, pos.y + offsetY);
        ImVec2 p2 = ImVec2(pos.x + offsetX + w - cornerSize, pos.y + offsetY);
        ImVec2 p3 = ImVec2(pos.x + offsetX + w, pos.y + offsetY + cornerSize);
        ImVec2 p4 = ImVec2(pos.x + offsetX + w, pos.y + offsetY + h);
        ImVec2 p5 = ImVec2(pos.x + offsetX, pos.y + offsetY + h);
        
        // Main body
        drawList->AddQuadFilled(p1, p2, ImVec2(p2.x, p4.y), p5, color);
        drawList->AddTriangleFilled(p2, p3, ImVec2(p2.x, p3.y), color);
        drawList->AddRectFilled(ImVec2(p2.x, p3.y), p4, color);
        
        // Corner fold
        drawList->AddTriangleFilled(p2, ImVec2(p2.x, p3.y), p3, IM_COL32(255, 255, 255, 50));
    }
    
    // Draw a script/code icon
    void DrawScriptIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        float padding = size * 0.12f;
        ImVec2 tl = ImVec2(pos.x + padding, pos.y + padding);
        ImVec2 br = ImVec2(pos.x + size - padding, pos.y + size - padding);
        
        // Background
        drawList->AddRectFilled(tl, br, color, 3.0f);
        
        // Brackets < >
        ImU32 bracketColor = IM_COL32(255, 255, 255, 200);
        float cx = pos.x + size * 0.5f;
        float cy = pos.y + size * 0.5f;
        float bSize = size * 0.2f;
        
        // Left bracket <
        drawList->AddLine(ImVec2(cx - bSize * 0.5f, cy - bSize), ImVec2(cx - bSize * 1.5f, cy), bracketColor, 2.5f);
        drawList->AddLine(ImVec2(cx - bSize * 1.5f, cy), ImVec2(cx - bSize * 0.5f, cy + bSize), bracketColor, 2.5f);
        
        // Right bracket >
        drawList->AddLine(ImVec2(cx + bSize * 0.5f, cy - bSize), ImVec2(cx + bSize * 1.5f, cy), bracketColor, 2.5f);
        drawList->AddLine(ImVec2(cx + bSize * 1.5f, cy), ImVec2(cx + bSize * 0.5f, cy + bSize), bracketColor, 2.5f);
    }
    
    // Draw a text icon
    void DrawTextIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        DrawFileIcon(drawList, pos, size, color);
        
        // Text lines
        ImU32 lineColor = IM_COL32(255, 255, 255, 150);
        float startX = pos.x + size * 0.25f;
        float endX = pos.x + size * 0.65f;
        float lineY = pos.y + size * 0.4f;
        float lineH = size * 0.06f;
        float spacing = size * 0.12f;
        
        for (int i = 0; i < 3; i++) {
            float w = (i == 1) ? (endX - startX) * 0.7f : (endX - startX);
            drawList->AddRectFilled(ImVec2(startX, lineY), ImVec2(startX + w, lineY + lineH), lineColor);
            lineY += spacing;
        }
    }
    
    void DrawIcon(ImDrawList* drawList, FileCategory category, ImVec2 pos, float size, ImU32 color) {
        switch (category) {
            case FileCategory::Folder:  DrawFolderIcon(drawList, pos, size, color); break;
            case FileCategory::Scene:   DrawSceneIcon(drawList, pos, size, color); break;
            case FileCategory::Model:   DrawModelIcon(drawList, pos, size, color); break;
            case FileCategory::Material:DrawShaderIcon(drawList, pos, size, color); break;
            case FileCategory::Texture: DrawTextureIcon(drawList, pos, size, color); break;
            case FileCategory::Shader:  DrawShaderIcon(drawList, pos, size, color); break;
            case FileCategory::Script:  DrawScriptIcon(drawList, pos, size, color); break;
            case FileCategory::Audio:   DrawAudioIcon(drawList, pos, size, color); break;
            case FileCategory::Text:    DrawTextIcon(drawList, pos, size, color); break;
            default:                    DrawFileIcon(drawList, pos, size, color); break;
        }
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

    const SceneObject* playerCam = nullptr;
    for (const auto& obj : sceneObjects) {
        if (obj.type == ObjectType::Camera && obj.camera.type == SceneCameraType::Player) {
            playerCam = &obj;
            break;
        }
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
            playerCam->camera.farClip
        );

        ImGui::Image((void*)(intptr_t)tex, ImVec2((float)width, (float)height), ImVec2(0, 1), ImVec2(1, 0));
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
void Engine::renderFileBrowserPanel() {
    ImGui::Begin("Project", &showFileBrowser);
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 toolbarBg = style.Colors[ImGuiCol_MenuBarBg];
    toolbarBg.x = std::min(toolbarBg.x + 0.02f, 1.0f);
    toolbarBg.y = std::min(toolbarBg.y + 0.02f, 1.0f);
    toolbarBg.z = std::min(toolbarBg.z + 0.02f, 1.0f);

    if (fileBrowser.needsRefresh) {
        fileBrowser.refresh();
    }
    
    // Get colors for categories
    auto getCategoryColor = [](FileCategory cat) -> ImU32 {
        switch (cat) {
            case FileCategory::Folder:  return IM_COL32(255, 200, 80, 255);   // Yellow/orange
            case FileCategory::Scene:   return IM_COL32(100, 180, 255, 255);  // Blue
            case FileCategory::Model:   return IM_COL32(100, 220, 140, 255);  // Green
            case FileCategory::Material:return IM_COL32(220, 200, 120, 255);  // Gold
            case FileCategory::Texture: return IM_COL32(220, 130, 220, 255);  // Purple/pink
            case FileCategory::Shader:  return IM_COL32(255, 140, 90, 255);   // Orange
            case FileCategory::Script:  return IM_COL32(130, 200, 255, 255);  // Light blue
            case FileCategory::Audio:   return IM_COL32(255, 180, 100, 255);  // Warm orange
            case FileCategory::Text:    return IM_COL32(180, 180, 180, 255);  // Gray
            default:                    return IM_COL32(150, 150, 150, 255);  // Dark gray
        }
    };
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toolbarBg);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 3.0f));
    ImGui::BeginChild("ProjectToolbar", ImVec2(0, 44), true, ImGuiWindowFlags_NoScrollbar);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));
    bool canGoBack = fileBrowser.historyIndex > 0;
    bool canGoForward = fileBrowser.historyIndex < (int)fileBrowser.pathHistory.size() - 1;
    bool canGoUp = fileBrowser.currentPath != fileBrowser.projectRoot &&
                   fileBrowser.currentPath.has_parent_path();

    ImGui::BeginDisabled(!canGoBack);
    ImGui::Button("<##Back", ImVec2(26, 0));
    if (ImGui::IsItemActivated()) fileBrowser.navigateBack();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Back");

    ImGui::SameLine();
    ImGui::BeginDisabled(!canGoForward);
    ImGui::Button(">##Forward", ImVec2(26, 0));
    if (ImGui::IsItemActivated()) fileBrowser.navigateForward();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Forward");

    ImGui::SameLine();
    ImGui::BeginDisabled(!canGoUp);
    ImGui::Button("^##Up", ImVec2(26, 0));
    if (ImGui::IsItemActivated()) fileBrowser.navigateUp();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Up one folder");
    ImGui::PopStyleVar();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));

    fs::path relativePath;
    if (fileBrowser.projectRoot.empty()) {
        relativePath = fileBrowser.currentPath.filename();
    } else {
        try {
            relativePath = fs::relative(fileBrowser.currentPath, fileBrowser.projectRoot);
        } catch (...) {
            relativePath = fileBrowser.currentPath.filename();
        }
    }
    std::vector<fs::path> pathParts;
    fs::path accumulated = fileBrowser.projectRoot;

    pathParts.push_back(fileBrowser.projectRoot);
    for (const auto& part : relativePath) {
        if (part != ".") {
            accumulated /= part;
            pathParts.push_back(accumulated);
        }
    }

    struct Breadcrumb {
        std::string label;
        fs::path target;
    };
    std::vector<Breadcrumb> crumbs;
    if (pathParts.size() <= 4) {
        for (size_t i = 0; i < pathParts.size(); ++i) {
            std::string name = (i == 0) ? "Project" : pathParts[i].filename().string();
            crumbs.push_back({name, pathParts[i]});
        }
    } else {
        crumbs.push_back({"Project", pathParts.front()});
        crumbs.push_back({"..", pathParts[pathParts.size() - 3]});
        crumbs.push_back({pathParts[pathParts.size() - 2].filename().string(), pathParts[pathParts.size() - 2]});
        crumbs.push_back({pathParts.back().filename().string(), pathParts.back()});
    }

    for (size_t i = 0; i < crumbs.size(); i++) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(crumbs[i].label.c_str())) {
            fileBrowser.navigateTo(crumbs[i].target);
        }
        ImGui::PopID();
        if (i < crumbs.size() - 1) {
            ImGui::SameLine(0, 2);
            ImGui::TextDisabled("/");
            ImGui::SameLine(0, 2);
        }
    }

    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    if (ImGui::InputTextWithHint("##Search", "Search...", fileBrowserSearch, sizeof(fileBrowserSearch))) {
        fileBrowser.searchFilter = fileBrowserSearch;
        fileBrowser.needsRefresh = true;
    }

    ImGui::SameLine();
    bool isGridMode = fileBrowser.viewMode == FileBrowserViewMode::Grid;
    if (isGridMode) {
        ImGui::TextDisabled("Size");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::SliderFloat("##IconScale", &fileBrowserIconScale, 0.6f, 2.0f, "%.1fx");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Icon Size: %.1fx", fileBrowserIconScale);
        ImGui::SameLine();
    }

    if (ImGui::Button(isGridMode ? "Grid" : "List", ImVec2(54, 0))) {
        fileBrowser.viewMode = isGridMode ? FileBrowserViewMode::List : FileBrowserViewMode::Grid;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(isGridMode ? "Switch to List View" : "Switch to Grid View");

    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(68, 0))) {
        fileBrowser.needsRefresh = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("New Mat", ImVec2(78, 0))) {
        fs::path target = fileBrowser.currentPath / "NewMaterial.mat";
        int counter = 1;
        while (fs::exists(target)) {
            target = fileBrowser.currentPath / ("NewMaterial" + std::to_string(counter++) + ".mat");
        }
        SceneObject temp("Material", ObjectType::Cube, -1);
        temp.materialPath = target.string();
        saveMaterialToFile(temp);
        fileBrowser.needsRefresh = true;
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // === FILE CONTENT AREA ===
    ImVec4 contentBg = style.Colors[ImGuiCol_WindowBg];
    contentBg.x = std::min(contentBg.x + 0.01f, 1.0f);
    contentBg.y = std::min(contentBg.y + 0.01f, 1.0f);
    contentBg.z = std::min(contentBg.z + 0.01f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, contentBg);
    ImGui::BeginChild("FileContent", ImVec2(0, 0), true);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    if (fileBrowser.viewMode == FileBrowserViewMode::Grid) {
        float baseIconSize = 64.0f;
        float iconSize = baseIconSize * fileBrowserIconScale;
        float padding = 8.0f * fileBrowserIconScale;
        float textHeight = 32.0f;  // Space for filename text
        float cellWidth = iconSize + padding * 2;
        float cellHeight = iconSize + padding * 2 + textHeight;
        
        float windowWidth = ImGui::GetContentRegionAvail().x;
        int columns = std::max(1, (int)((windowWidth + padding) / (cellWidth + padding)));
        
        // Use a table for consistent grid layout
        if (ImGui::BeginTable("FileGrid", columns, ImGuiTableFlags_NoPadInnerX)) {
            for (int i = 0; i < (int)fileBrowser.entries.size(); i++) {
                const auto& entry = fileBrowser.entries[i];
                std::string filename = entry.path().filename().string();
                FileCategory category = fileBrowser.getFileCategory(entry);
                bool isSelected = fileBrowser.selectedFile == entry.path();
                
                ImGui::TableNextColumn();
                ImGui::PushID(i);
                
                // Cell content area
                ImVec2 cellStart = ImGui::GetCursorScreenPos();
                ImVec2 cellEnd(cellStart.x + cellWidth, cellStart.y + cellHeight);
                
                // Invisible button for the entire cell
                if (ImGui::InvisibleButton("##cell", ImVec2(cellWidth, cellHeight))) {
                    fileBrowser.selectedFile = entry.path();
                }
                bool hovered = ImGui::IsItemHovered();
                bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(0);
                
                // Draw background
                ImU32 bgColor = isSelected ? IM_COL32(70, 110, 160, 200) :
                               (hovered ? IM_COL32(60, 65, 75, 180) : IM_COL32(0, 0, 0, 0));
                if (bgColor != IM_COL32(0, 0, 0, 0)) {
                    drawList->AddRectFilled(cellStart, cellEnd, bgColor, 6.0f);
                }
                
                // Draw border on selection
                if (isSelected) {
                    drawList->AddRect(cellStart, cellEnd, IM_COL32(100, 150, 220, 255), 6.0f, 0, 2.0f);
                }
                
                // Draw icon centered in cell
                ImVec2 iconPos(
                    cellStart.x + (cellWidth - iconSize) * 0.5f,
                    cellStart.y + padding
                );
                FileIcons::DrawIcon(drawList, category, iconPos, iconSize, getCategoryColor(category));
                
                // Draw filename below icon (centered, with wrapping)
                std::string displayName = filename;
                float maxTextWidth = cellWidth - 4;
                
                // Truncate if too long
                ImVec2 textSize = ImGui::CalcTextSize(displayName.c_str());
                if (textSize.x > maxTextWidth) {
                    while (displayName.length() > 3) {
                        displayName.pop_back();
                        if (ImGui::CalcTextSize((displayName + "...").c_str()).x <= maxTextWidth) {
                            break;
                        }
                    }
                    displayName += "...";
                    textSize = ImGui::CalcTextSize(displayName.c_str());
                }
                
                ImVec2 textPos(
                    cellStart.x + (cellWidth - textSize.x) * 0.5f,
                    cellStart.y + padding + iconSize + 4
                );
                
                // Text with subtle shadow for readability
                drawList->AddText(ImVec2(textPos.x + 1, textPos.y + 1), IM_COL32(0, 0, 0, 100), displayName.c_str());
                drawList->AddText(textPos, IM_COL32(230, 230, 230, 255), displayName.c_str());
                
                // Handle double click
                if (doubleClicked) {
                    if (entry.is_directory()) {
                        fileBrowser.navigateTo(entry.path());
                    } else if (fileBrowser.isModelFile(entry)) {
                        bool isObj = fileBrowser.isOBJFile(entry);
                        std::string defaultName = entry.path().stem().string();
                        if (isObj) {
                            pendingOBJPath = entry.path().string();
                            strncpy(importOBJName, defaultName.c_str(), sizeof(importOBJName) - 1);
                            showImportOBJDialog = true;
                        } else {
                            pendingModelPath = entry.path().string();
                            strncpy(importModelName, defaultName.c_str(), sizeof(importModelName) - 1);
                            showImportModelDialog = true;
                        }
                    } else if (fileBrowser.getFileCategory(entry) == FileCategory::Material) {
                        if (SceneObject* sel = getSelectedObject()) {
                            sel->materialPath = entry.path().string();
                            loadMaterialFromFile(*sel);
                        }
                    } else if (fileBrowser.isSceneFile(entry)) {
                        std::string sceneName = entry.path().stem().string();
                        loadScene(sceneName);
                        logToConsole("Loaded scene: " + sceneName);
                    }
                }
                
                // Context menu
                if (ImGui::BeginPopupContextItem("FileContextMenu")) {
                    if (ImGui::MenuItem("Open")) {
                        if (entry.is_directory()) {
                            fileBrowser.navigateTo(entry.path());
                        } else if (fileBrowser.isSceneFile(entry)) {
                            std::string sceneName = entry.path().stem().string();
                            loadScene(sceneName);
                        }
                    }
                    if (fileBrowser.isModelFile(entry)) {
                        bool isObj = fileBrowser.isOBJFile(entry);
                        if (ImGui::MenuItem("Import to Scene")) {
                            std::string defaultName = entry.path().stem().string();
                            if (isObj) {
                                pendingOBJPath = entry.path().string();
                                strncpy(importOBJName, defaultName.c_str(), sizeof(importOBJName) - 1);
                                showImportOBJDialog = true;
                            } else {
                                pendingModelPath = entry.path().string();
                                strncpy(importModelName, defaultName.c_str(), sizeof(importModelName) - 1);
                                showImportModelDialog = true;
                            }
                        }
                        if (ImGui::MenuItem("Quick Import")) {
                            if (isObj) {
                                importOBJToScene(entry.path().string(), "");
                            } else {
                                importModelToScene(entry.path().string(), "");
                            }
                        }
                        if (ImGui::MenuItem("Convert to Raw Mesh")) {
                            convertModelToRawMesh(entry.path().string());
                        }
                    }
                    if (fileBrowser.getFileCategory(entry) == FileCategory::Material) {
                        if (ImGui::MenuItem("Apply to Selected")) {
                            if (SceneObject* sel = getSelectedObject()) {
                                sel->materialPath = entry.path().string();
                                loadMaterialFromFile(*sel);
                            }
                        }
                    }
                    if (fileBrowser.getFileCategory(entry) == FileCategory::Script) {
                        if (ImGui::MenuItem("Compile Script")) {
                            compileScriptFile(entry.path());
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Show in Explorer")) {
                        #ifdef _WIN32
                        std::string cmd = "explorer \"" + entry.path().parent_path().string() + "\"";
                        system(cmd.c_str());
                        #elif __linux__
                        std::string cmd = "xdg-open \"" + entry.path().parent_path().string() + "\"";
                        system(cmd.c_str());
                        #endif
                    }
                    ImGui::EndPopup();
                }
                
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        
    } else {
        // List View
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
        
        for (int i = 0; i < (int)fileBrowser.entries.size(); i++) {
            const auto& entry = fileBrowser.entries[i];
            std::string filename = entry.path().filename().string();
            FileCategory category = fileBrowser.getFileCategory(entry);
            bool isSelected = fileBrowser.selectedFile == entry.path();
            
            ImGui::PushID(i);
            
            // Selectable row
            if (ImGui::Selectable("##row", isSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 20))) {
                fileBrowser.selectedFile = entry.path();
                
                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (entry.is_directory()) {
                        fileBrowser.navigateTo(entry.path());
                    } else if (fileBrowser.isModelFile(entry)) {
                        bool isObj = fileBrowser.isOBJFile(entry);
                        std::string defaultName = entry.path().stem().string();
                        if (isObj) {
                            pendingOBJPath = entry.path().string();
                            strncpy(importOBJName, defaultName.c_str(), sizeof(importOBJName) - 1);
                            showImportOBJDialog = true;
                        } else {
                            pendingModelPath = entry.path().string();
                            strncpy(importModelName, defaultName.c_str(), sizeof(importModelName) - 1);
                            showImportModelDialog = true;
                        }
                    } else if (fileBrowser.getFileCategory(entry) == FileCategory::Material) {
                        if (SceneObject* sel = getSelectedObject()) {
                            sel->materialPath = entry.path().string();
                            loadMaterialFromFile(*sel);
                        }
                    } else if (fileBrowser.isSceneFile(entry)) {
                        std::string sceneName = entry.path().stem().string();
                        loadScene(sceneName);
                        logToConsole("Loaded scene: " + sceneName);
                    }
                }
            }
            
            // Context menu
            if (ImGui::BeginPopupContextItem("FileContextMenu")) {
                if (ImGui::MenuItem("Open")) {
                    if (entry.is_directory()) {
                        fileBrowser.navigateTo(entry.path());
                    } else if (fileBrowser.isSceneFile(entry)) {
                        std::string sceneName = entry.path().stem().string();
                        loadScene(sceneName);
                    }
                }
                if (fileBrowser.isModelFile(entry)) {
                    bool isObj = fileBrowser.isOBJFile(entry);
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    bool isRaw = ext == ".rmesh";
                    if (ImGui::MenuItem("Import to Scene")) {
                        std::string defaultName = entry.path().stem().string();
                        if (isObj) {
                            pendingOBJPath = entry.path().string();
                            strncpy(importOBJName, defaultName.c_str(), sizeof(importOBJName) - 1);
                            showImportOBJDialog = true;
                        } else {
                            pendingModelPath = entry.path().string();
                            strncpy(importModelName, defaultName.c_str(), sizeof(importModelName) - 1);
                            showImportModelDialog = true;
                        }
                    }
                    if (ImGui::MenuItem("Quick Import")) {
                        if (isObj) {
                            importOBJToScene(entry.path().string(), "");
                        } else {
                            importModelToScene(entry.path().string(), "");
                        }
                    }
                    if (!isRaw && ImGui::MenuItem("Convert to Raw Mesh")) {
                        convertModelToRawMesh(entry.path().string());
                    }
                }
                if (fileBrowser.getFileCategory(entry) == FileCategory::Material) {
                    if (ImGui::MenuItem("Apply to Selected")) {
                        if (SceneObject* sel = getSelectedObject()) {
                            sel->materialPath = entry.path().string();
                            loadMaterialFromFile(*sel);
                        }
                    }
                }
                if (fileBrowser.getFileCategory(entry) == FileCategory::Script) {
                    if (ImGui::MenuItem("Compile Script")) {
                        compileScriptFile(entry.path());
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Show in Explorer")) {
                    #ifdef _WIN32
                    std::string cmd = "explorer \"" + entry.path().parent_path().string() + "\"";
                    system(cmd.c_str());
                    #elif __linux__
                    std::string cmd = "xdg-open \"" + entry.path().parent_path().string() + "\"";
                    system(cmd.c_str());
                    #endif
                }
                ImGui::EndPopup();
            }
            
            // Draw icon inline
            ImGui::SameLine(4);
            ImVec2 iconPos = ImGui::GetCursorScreenPos();
            iconPos.y -= 2;
            FileIcons::DrawIcon(drawList, category, iconPos, 16, getCategoryColor(category));
            
            ImGui::SameLine(26);
            
            // Color-coded filename
            ImVec4 textColor;
            switch (category) {
                case FileCategory::Folder:  textColor = ImVec4(1.0f, 0.85f, 0.4f, 1.0f); break;
                case FileCategory::Scene:   textColor = ImVec4(0.5f, 0.75f, 1.0f, 1.0f); break;
                case FileCategory::Model:   textColor = ImVec4(0.5f, 0.9f, 0.6f, 1.0f); break;
                case FileCategory::Material:textColor = ImVec4(0.95f, 0.8f, 0.45f, 1.0f); break;
                case FileCategory::Texture: textColor = ImVec4(0.9f, 0.6f, 0.9f, 1.0f); break;
                default:                    textColor = ImVec4(0.85f, 0.85f, 0.85f, 1.0f); break;
            }
            ImGui::TextColored(textColor, "%s", filename.c_str());
            
            ImGui::PopID();
        }
        
        ImGui::PopStyleVar();
    }
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
}

void Engine::renderMeshBuilderPanel() {
    ImGui::Begin("Mesh Builder", &showMeshBuilder);

    auto recalcBounds = [this]() {
        if (!meshBuilder.hasMesh || meshBuilder.mesh.positions.empty()) return;
        meshBuilder.mesh.boundsMin = glm::vec3(FLT_MAX);
        meshBuilder.mesh.boundsMax = glm::vec3(-FLT_MAX);
        for (const auto& p : meshBuilder.mesh.positions) {
            meshBuilder.mesh.boundsMin.x = std::min(meshBuilder.mesh.boundsMin.x, p.x);
            meshBuilder.mesh.boundsMin.y = std::min(meshBuilder.mesh.boundsMin.y, p.y);
            meshBuilder.mesh.boundsMin.z = std::min(meshBuilder.mesh.boundsMin.z, p.z);
            meshBuilder.mesh.boundsMax.x = std::max(meshBuilder.mesh.boundsMax.x, p.x);
            meshBuilder.mesh.boundsMax.y = std::max(meshBuilder.mesh.boundsMax.y, p.y);
            meshBuilder.mesh.boundsMax.z = std::max(meshBuilder.mesh.boundsMax.z, p.z);
        }
    };

    ImGui::InputText("Mesh Path", meshBuilderPath, sizeof(meshBuilderPath));
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string err;
        if (!meshBuilder.load(meshBuilderPath, err)) {
            addConsoleMessage("MeshBuilder load failed: " + err, ConsoleMessageType::Error);
        } else {
            addConsoleMessage("Loaded raw mesh: " + meshBuilder.loadedPath, ConsoleMessageType::Success);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string err;
        std::string path = strlen(meshBuilderPath) ? meshBuilderPath : meshBuilder.loadedPath;
        if (!meshBuilder.save(path, err)) {
            addConsoleMessage("MeshBuilder save failed: " + err, ConsoleMessageType::Error);
        } else {
            addConsoleMessage("Saved raw mesh: " + meshBuilder.loadedPath, ConsoleMessageType::Success);
            strncpy(meshBuilderPath, meshBuilder.loadedPath.c_str(), sizeof(meshBuilderPath) - 1);
            meshBuilderPath[sizeof(meshBuilderPath) - 1] = '\0';
        }
    }

    if (ImGui::Button("Load Selected File")) {
        if (!fileBrowser.selectedFile.empty() && fs::path(fileBrowser.selectedFile).extension() == ".rmesh") {
            strncpy(meshBuilderPath, fileBrowser.selectedFile.string().c_str(), sizeof(meshBuilderPath) - 1);
            meshBuilderPath[sizeof(meshBuilderPath) - 1] = '\0';
            std::string err;
            if (!meshBuilder.load(meshBuilderPath, err)) {
                addConsoleMessage("MeshBuilder load failed: " + err, ConsoleMessageType::Error);
            } else {
                addConsoleMessage("Loaded raw mesh: " + meshBuilder.loadedPath, ConsoleMessageType::Success);
            }
        } else {
            addConsoleMessage("Select a .rmesh file in the browser to load", ConsoleMessageType::Warning);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Recompute Normals")) {
        meshBuilder.recomputeNormals();
    }

    ImGui::Separator();

    if (!meshBuilder.hasMesh) {
        ImGui::TextDisabled("No mesh loaded.");
        ImGui::End();
        return;
    }

    ImGui::Text("Vertices: %zu", meshBuilder.mesh.positions.size());
    ImGui::Text("Faces: %zu", meshBuilder.mesh.faces.size());
    ImGui::Text("Bounds Min: (%.3f, %.3f, %.3f)", meshBuilder.mesh.boundsMin.x, meshBuilder.mesh.boundsMin.y, meshBuilder.mesh.boundsMin.z);
    ImGui::Text("Bounds Max: (%.3f, %.3f, %.3f)", meshBuilder.mesh.boundsMax.x, meshBuilder.mesh.boundsMax.y, meshBuilder.mesh.boundsMax.z);
    if (meshBuilder.dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1,0.7f,0.2f,1),"*modified");
    }

    ImGui::SeparatorText("Vertices");
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Selected", &meshBuilder.selectedVertex);
    if (meshBuilder.selectedVertex < 0 || meshBuilder.selectedVertex >= (int)meshBuilder.mesh.positions.size()) {
        meshBuilder.selectedVertex = meshBuilder.mesh.positions.empty() ? -1 : 0;
    }

    if (meshBuilder.selectedVertex >= 0 && meshBuilder.selectedVertex < (int)meshBuilder.mesh.positions.size()) {
        glm::vec3& pos = meshBuilder.mesh.positions[meshBuilder.selectedVertex];
        float edit[3] = { pos.x, pos.y, pos.z };
        if (ImGui::InputFloat3("Position", edit, "%.4f")) {
            pos = glm::vec3(edit[0], edit[1], edit[2]);
            recalcBounds();
            meshBuilder.recomputeNormals();
            meshBuilder.dirty = true;
        }
        if (meshBuilder.mesh.hasUVs && meshBuilder.selectedVertex < (int)meshBuilder.mesh.uvs.size()) {
            glm::vec2& uv = meshBuilder.mesh.uvs[meshBuilder.selectedVertex];
            float uvEdit[2] = { uv.x, uv.y };
            if (ImGui::InputFloat2("UV", uvEdit, "%.4f")) {
                uv = glm::vec2(uvEdit[0], uvEdit[1]);
                meshBuilder.dirty = true;
            }
        }
    }

    ImGui::SeparatorText("Add Face / Fill");
    ImGui::InputTextWithHint("Indices", "e.g. 0,1,2 or 0 1 2 3", meshBuilderFaceInput, sizeof(meshBuilderFaceInput));
    ImGui::SameLine();
    if (ImGui::Button("Add Face")) {
        std::vector<uint32_t> indices;
        std::string token;
        std::stringstream ss(meshBuilderFaceInput);
        while (std::getline(ss, token, ',')) {
            std::stringstream inner(token);
            std::string sub;
            while (inner >> sub) {
                try {
                    uint32_t idx = static_cast<uint32_t>(std::stoul(sub));
                    indices.push_back(idx);
                } catch (...) {}
            }
        }
        if (indices.empty()) {
            addConsoleMessage("Enter vertex indices separated by commas or spaces", ConsoleMessageType::Warning);
        } else {
            std::string err;
            if (!meshBuilder.addFace(indices, err)) {
                addConsoleMessage("Add face failed: " + err, ConsoleMessageType::Error);
            } else {
                addConsoleMessage("Added face with " + std::to_string(indices.size()) + " verts", ConsoleMessageType::Success);
            }
        }
    }

    ImGui::SeparatorText("Faces (first 16)");
    int maxFaces = std::min<int>(16, meshBuilder.mesh.faces.size());
    for (int i = 0; i < maxFaces; i++) {
        const auto& f = meshBuilder.mesh.faces[i];
        ImGui::Text("%d: %u, %u, %u", i, f.x, f.y, f.z);
    }

    ImGui::End();
}

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
            #else
            system("xdg-open https://github.com &");
            #endif
        }

        if (ImGui::Button("Exit", ImVec2(-1, 30.0f)))
        {
            glfwSetWindowShouldClose(editorWindow, GLFW_TRUE);
        }

        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.11f, 1.0f));
        ImGui::BeginChild("LauncherRight", ImVec2(0, 0), true);
        ImGui::PopStyleColor();

        ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.78f, 1.0f), "RECENT PROJECTS");
        ImGui::Spacing();

        if (projectManager.recentProjects.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("No recent projects");
            ImGui::TextDisabled("Create a new project to get started!");
        }
        else
        {
            float availWidth = ImGui::GetContentRegionAvail().x;
            for (size_t i = 0; i < projectManager.recentProjects.size(); ++i)
            {
                const auto& rp = projectManager.recentProjects[i];
                ImGui::PushID(static_cast<int>(i));

                char label[512];
                std::snprintf(label, sizeof(label), "%s\n%s",
                            rp.name.c_str(), rp.path.c_str());

                ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.20f, 0.30f, 0.45f, 0.40f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.38f, 0.55f, 0.70f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.20f, 0.35f, 0.60f, 0.90f));

                bool selected = ImGui::Selectable(
                    label,
                    false,
                    ImGuiSelectableFlags_AllowDoubleClick,
                    ImVec2(availWidth, 48.0f)
                );

                ImGui::PopStyleColor(3);
                
                // Dummy to extend window bounds properly
                ImGui::Dummy(ImVec2(0, 0));

                if (selected || ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    OpenProjectPath(rp.path);
                }

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

                ImGui::PopID();
                ImGui::Spacing();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextDisabled("Modularity Engine - Version 0.6.7");

        ImGui::EndChild();
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    if (projectManager.showNewProjectDialog)
        renderNewProjectDialog();
    if (projectManager.showOpenProjectDialog)
        renderOpenProjectDialog();
}

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

void Engine::renderMainMenuBar() {
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
                logToConsole("Modularity Engine v0.6.7");
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

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(14.0f, 0.0f));
        ImGui::SameLine();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 4.0f));
        bool playPressed = ImGui::Button(isPlaying ? "Stop" : "Play");
        ImGui::SameLine(0.0f, 6.0f);
        bool pausePressed = ImGui::Button(isPaused ? "Resume" : "Pause");
        ImGui::SameLine(0.0f, 6.0f);
        bool specPressed = ImGui::Button(specMode ? "Spec On" : "Spec Mode");
        ImGui::PopStyleVar();

        if (playPressed) {
            bool newState = !isPlaying;
            if (newState) {
                if (physics.isReady() || physics.init()) {
                    physics.onPlayStart(sceneObjects);
                } else {
                    addConsoleMessage("PhysX failed to initialize; physics disabled for play mode", ConsoleMessageType::Warning);
                }
            } else {
                physics.onPlayStop();
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
                if (specMode) physics.onPlayStart(sceneObjects);
                else physics.onPlayStop();
            }
        }

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

void Engine::renderHierarchyPanel() {
    ImGui::Begin("Hierarchy", &showHierarchy);

    static char searchBuffer[128] = "";
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 headerBg = style.Colors[ImGuiCol_MenuBarBg];
    headerBg.x = std::min(headerBg.x + 0.02f, 1.0f);
    headerBg.y = std::min(headerBg.y + 0.02f, 1.0f);
    headerBg.z = std::min(headerBg.z + 0.02f, 1.0f);
    ImVec4 listBg = style.Colors[ImGuiCol_WindowBg];
    listBg.x = std::min(listBg.x + 0.01f, 1.0f);
    listBg.y = std::min(listBg.y + 0.01f, 1.0f);
    listBg.z = std::min(listBg.z + 0.01f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, headerBg);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::BeginChild("HierarchyHeader", ImVec2(0, 50), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##Search", "Search...", searchBuffer, sizeof(searchBuffer));
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    std::string filter = searchBuffer;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            int draggedId = *(const int*)payload->Data;
            setParent(draggedId, -1);
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, listBg);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 2.0f));
    ImGui::BeginChild("HierarchyList", ImVec2(0, 0), true);

    for (size_t i = 0; i < sceneObjects.size(); i++) {
        if (sceneObjects[i].parentId != -1)
            continue;

        renderObjectNode(sceneObjects[i], filter);
    }

    if (ImGui::BeginPopupContextWindow("HierarchyBackground",
            ImGuiPopupFlags_MouseButtonRight |
            ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::BeginMenu("Create"))
        {
            // ── Primitives ─────────────────────────────
            if (ImGui::BeginMenu("Primitives"))
            {
                if (ImGui::MenuItem("Cube"))    addObject(ObjectType::Cube, "Cube");
                if (ImGui::MenuItem("Sphere"))  addObject(ObjectType::Sphere, "Sphere");
                if (ImGui::MenuItem("Capsule")) addObject(ObjectType::Capsule, "Capsule");
                ImGui::EndMenu();
            }

            // ── Lights ────────────────────────────────
            if (ImGui::BeginMenu("Lights"))
            {
                if (ImGui::MenuItem("Directional Light")) addObject(ObjectType::DirectionalLight, "Directional Light");
                if (ImGui::MenuItem("Point Light"))       addObject(ObjectType::PointLight, "Point Light");
                if (ImGui::MenuItem("Spot Light"))        addObject(ObjectType::SpotLight, "Spot Light");
                if (ImGui::MenuItem("Area Light"))        addObject(ObjectType::AreaLight, "Area Light");
                ImGui::EndMenu();
            }

            // ── Other / Effects ───────────────────────
            if (ImGui::BeginMenu("Effects"))
            {
                if (ImGui::MenuItem("Post FX Node")) addObject(ObjectType::PostFXNode, "Post FX");
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Camera")) addObject(ObjectType::Camera, "Camera");

            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::End();
}

void Engine::renderObjectNode(SceneObject& obj, const std::string& filter) {
    std::string nameLower = obj.name;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

    if (!filter.empty() && nameLower.find(filter) == std::string::npos) {
        return;
    }

    bool hasChildren = !obj.childIds.empty();
    bool isSelected = std::find(selectedObjectIds.begin(), selectedObjectIds.end(), obj.id) != selectedObjectIds.end();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

    const char* icon = "";
    switch (obj.type) {
        case ObjectType::Cube: icon = "[#]"; break;
        case ObjectType::Sphere: icon = "(O)"; break;
        case ObjectType::Capsule: icon = "[|]"; break;
        case ObjectType::OBJMesh: icon = "[M]"; break;
        case ObjectType::Model: icon = "[A]"; break;
        case ObjectType::Camera: icon = "(C)"; break;
        case ObjectType::DirectionalLight: icon = "(D)"; break;
        case ObjectType::PointLight: icon = "(P)"; break;
        case ObjectType::SpotLight: icon = "(S)"; break;
        case ObjectType::AreaLight: icon = "(L)"; break;
        case ObjectType::PostFXNode: icon = "(FX)"; break;
    }

    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)obj.id, flags, "%s %s", icon, obj.name.c_str());

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
        setPrimarySelection(obj.id, additive);
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
            setPrimarySelection(obj.id);
            duplicateSelected();
        }
        if (ImGui::MenuItem("Delete")) {
            setPrimarySelection(obj.id);
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

void Engine::renderInspectorPanel() {
    ImGui::Begin("Inspector", &showInspector);

    fs::path selectedMaterialPath;
    bool browserHasMaterial = false;
    if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
        fs::directory_entry entry(fileBrowser.selectedFile);
        if (fileBrowser.getFileCategory(entry) == FileCategory::Material) {
            selectedMaterialPath = entry.path();
            browserHasMaterial = true;
            if (inspectedMaterialPath != selectedMaterialPath.string()) {
                inspectedMaterialValid = loadMaterialData(
                    selectedMaterialPath.string(),
                    inspectedMaterial,
                    inspectedAlbedo,
                    inspectedOverlay,
                    inspectedNormal,
                    inspectedUseOverlay,
                    &inspectedVertShader,
                    &inspectedFragShader
                );
                inspectedMaterialPath = selectedMaterialPath.string();
            }
        } else {
            inspectedMaterialPath.clear();
            inspectedMaterialValid = false;
        }
    } else {
        inspectedMaterialPath.clear();
        inspectedMaterialValid = false;
    }

    auto renderMaterialAssetPanel = [&](const char* headerTitle, bool allowApply) {
        if (!browserHasMaterial) return;

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.35f, 0.55f, 1.0f));
        if (ImGui::CollapsingHeader(headerTitle, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(8.0f);
            if (!inspectedMaterialValid) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to read material file.");
            } else {
                auto textureField = [&](const char* label, const char* idSuffix, std::string& path) {
                    bool changed = false;
                    ImGui::PushID(idSuffix);
                    ImGui::TextUnformatted(label);
                    ImGui::SetNextItemWidth(-140);
                    char buf[512] = {};
                    std::snprintf(buf, sizeof(buf), "%s", path.c_str());
                    if (ImGui::InputText("##Path", buf, sizeof(buf))) {
                        path = buf;
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear")) {
                        path.clear();
                        changed = true;
                    }
                    ImGui::SameLine();
                    bool canUseTex = !fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile) &&
                                     fileBrowser.isTextureFile(fs::directory_entry(fileBrowser.selectedFile));
                    ImGui::BeginDisabled(!canUseTex);
                    std::string btnLabel = std::string("Use Selection##") + idSuffix;
                    if (ImGui::SmallButton(btnLabel.c_str())) {
                        path = fileBrowser.selectedFile.string();
                        changed = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                    return changed;
                };

                ImGui::TextDisabled("%s", selectedMaterialPath.filename().string().c_str());
                ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", selectedMaterialPath.string().c_str());
                ImGui::Spacing();

                bool matChanged = false;
                if (ImGui::ColorEdit3("Base Color", &inspectedMaterial.color.x)) {
                    matChanged = true;
                }
                float metallic = inspectedMaterial.specularStrength;
                if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) {
                    inspectedMaterial.specularStrength = metallic;
                    matChanged = true;
                }
                float smoothness = inspectedMaterial.shininess / 256.0f;
                if (ImGui::SliderFloat("Smoothness", &smoothness, 0.0f, 1.0f)) {
                    smoothness = std::clamp(smoothness, 0.0f, 1.0f);
                    inspectedMaterial.shininess = smoothness * 256.0f;
                    matChanged = true;
                }
                if (ImGui::SliderFloat("Ambient Light", &inspectedMaterial.ambientStrength, 0.0f, 1.0f)) {
                    matChanged = true;
                }
                if (ImGui::SliderFloat("Detail Mix", &inspectedMaterial.textureMix, 0.0f, 1.0f)) {
                    matChanged = true;
                }

                ImGui::Spacing();
                matChanged |= textureField("Base Map", "PreviewAlbedo", inspectedAlbedo);
                if (ImGui::Checkbox("Use Detail Map", &inspectedUseOverlay)) {
                    matChanged = true;
                }
                matChanged |= textureField("Detail Map", "PreviewOverlay", inspectedOverlay);
                matChanged |= textureField("Normal Map", "PreviewNormal", inspectedNormal);

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1.0f), "Shader");
                auto shaderField = [&](const char* label, const char* idSuffix, std::string& path) {
                    bool changed = false;
                    ImGui::PushID(idSuffix);
                    ImGui::TextUnformatted(label);
                    ImGui::SetNextItemWidth(-140);
                    char buf[512] = {};
                    std::snprintf(buf, sizeof(buf), "%s", path.c_str());
                    if (ImGui::InputText("##Path", buf, sizeof(buf))) {
                        path = buf;
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear")) {
                        path.clear();
                        changed = true;
                    }
                    bool selectionIsShader = false;
                    if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
                        selectionIsShader = fileBrowser.getFileCategory(fs::directory_entry(fileBrowser.selectedFile)) == FileCategory::Shader;
                    }
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!selectionIsShader);
                    std::string btn = std::string("Use Selection##") + idSuffix;
                    if (ImGui::SmallButton(btn.c_str())) {
                        path = fileBrowser.selectedFile.string();
                        changed = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                    return changed;
                };
                matChanged |= shaderField("Vertex Shader", "PreviewVert", inspectedVertShader);
                matChanged |= shaderField("Fragment Shader", "PreviewFrag", inspectedFragShader);

                ImGui::BeginDisabled(inspectedVertShader.empty() && inspectedFragShader.empty());
                if (ImGui::Button("Reload Shader")) {
                    renderer.forceReloadShader(inspectedVertShader, inspectedFragShader);
                }
                ImGui::EndDisabled();

                ImGui::Spacing();
                if (ImGui::Button("Reload")) {
                    inspectedMaterialValid = loadMaterialData(
                        selectedMaterialPath.string(),
                        inspectedMaterial,
                        inspectedAlbedo,
                        inspectedOverlay,
                        inspectedNormal,
                        inspectedUseOverlay,
                        &inspectedVertShader,
                        &inspectedFragShader
                    );
                }
                ImGui::SameLine();
                if (ImGui::Button("Save")) {
                    if (saveMaterialData(
                            selectedMaterialPath.string(),
                            inspectedMaterial,
                            inspectedAlbedo,
                            inspectedOverlay,
                            inspectedNormal,
                            inspectedUseOverlay,
                            inspectedVertShader,
                            inspectedFragShader))
                    {
                        addConsoleMessage("Saved material: " + selectedMaterialPath.string(), ConsoleMessageType::Success);
                    } else {
                        addConsoleMessage("Failed to save material: " + selectedMaterialPath.string(), ConsoleMessageType::Error);
                    }
                }

                if (allowApply) {
                    ImGui::SameLine();
                    SceneObject* target = getSelectedObject();
                    bool canApply = target != nullptr;
                    ImGui::BeginDisabled(!canApply);
                    if (ImGui::Button("Apply to Selection")) {
                        if (target) {
                            target->material = inspectedMaterial;
                            target->albedoTexturePath = inspectedAlbedo;
                            target->overlayTexturePath = inspectedOverlay;
                            target->normalMapPath = inspectedNormal;
                            target->useOverlay = inspectedUseOverlay;
                            target->materialPath = selectedMaterialPath.string();
                            target->vertexShaderPath = inspectedVertShader;
                            target->fragmentShaderPath = inspectedFragShader;
                            projectManager.currentProject.hasUnsavedChanges = true;
                            addConsoleMessage("Applied material to " + target->name, ConsoleMessageType::Success);
                        }
                    }
                    ImGui::EndDisabled();
                }

                if (matChanged) {
                    inspectedMaterialValid = true;
                }
            }
            ImGui::Unindent(8.0f);
        }
        ImGui::PopStyleColor();
    };

    if (selectedObjectIds.empty()) {
        if (browserHasMaterial) {
            renderMaterialAssetPanel("Material Asset", true);
        } else {
            ImGui::TextDisabled("No object selected");
        }
        ImGui::End();
        return;
    }

    int primaryId = selectedObjectId;
    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
        [primaryId](const SceneObject& obj) { return obj.id == primaryId; });

    if (it == sceneObjects.end()) {
        ImGui::TextDisabled("Object not found");
        ImGui::End();
        return;
    }

    SceneObject& obj = *it;
    bool addComponentButtonShown = false;

    if (selectedObjectIds.size() > 1) {
        ImGui::Text("Multiple objects selected: %zu", selectedObjectIds.size());
        ImGui::Separator();
    }

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
        const char* typeLabel = "Unknown";
        switch (obj.type) {
            case ObjectType::Cube:       typeLabel = "Cube"; break;
            case ObjectType::Sphere:     typeLabel = "Sphere"; break;
            case ObjectType::Capsule:    typeLabel = "Capsule"; break;
            case ObjectType::OBJMesh:    typeLabel = "OBJ Mesh"; break;
            case ObjectType::Model:      typeLabel = "Model"; break;
        case ObjectType::Camera: typeLabel = "Camera"; break;
        case ObjectType::DirectionalLight: typeLabel = "Directional Light"; break;
        case ObjectType::PointLight: typeLabel = "Point Light"; break;
        case ObjectType::SpotLight:  typeLabel = "Spot Light"; break;
        case ObjectType::AreaLight:  typeLabel = "Area Light"; break;
        case ObjectType::PostFXNode: typeLabel = "Post FX Node"; break;
        }
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", typeLabel);

        ImGui::Text("ID:");
        ImGui::SameLine();
        ImGui::TextDisabled("%d", obj.id);
    }

    ImGui::PopStyleColor();

    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.5f, 0.3f, 1.0f));

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(10.0f);

        if (obj.type == ObjectType::PostFXNode) {
            ImGui::TextDisabled("Transform is ignored for post-processing nodes.");
        }

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
            obj.rotation = NormalizeEulerDegrees(obj.rotation);
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

    if (obj.hasCollider) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.5f, 0.35f, 1.0f));
        if (ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);
            bool changed = false;

            if (ImGui::Checkbox("Enabled", &obj.collider.enabled)) {
                changed = true;
            }

            const char* colliderTypes[] = { "Box", "Mesh", "Convex Mesh", "Capsule" };
            int colliderType = static_cast<int>(obj.collider.type);
            if (ImGui::Combo("Type", &colliderType, colliderTypes, IM_ARRAYSIZE(colliderTypes))) {
                obj.collider.type = static_cast<ColliderType>(colliderType);
                changed = true;
            }

            if (obj.collider.type == ColliderType::Box) {
                if (ImGui::DragFloat3("Box Size", &obj.collider.boxSize.x, 0.01f, 0.01f, 1000.0f, "%.3f")) {
                    obj.collider.boxSize.x = std::max(0.01f, obj.collider.boxSize.x);
                    obj.collider.boxSize.y = std::max(0.01f, obj.collider.boxSize.y);
                    obj.collider.boxSize.z = std::max(0.01f, obj.collider.boxSize.z);
                    changed = true;
                }
                if (ImGui::SmallButton("Match Object Scale")) {
                    obj.collider.boxSize = glm::max(obj.scale, glm::vec3(0.01f));
                    changed = true;
                }
            } else if (obj.collider.type == ColliderType::Capsule) {
                float radius = std::max(0.05f, std::max(obj.collider.boxSize.x, obj.collider.boxSize.z) * 0.5f);
                float height = std::max(0.1f, obj.collider.boxSize.y);
                if (ImGui::DragFloat("Radius", &radius, 0.01f, 0.05f, 5.0f, "%.3f")) {
                    obj.collider.boxSize.x = obj.collider.boxSize.z = radius * 2.0f;
                    changed = true;
                }
                if (ImGui::DragFloat("Height", &height, 0.01f, 0.1f, 10.0f, "%.3f")) {
                    obj.collider.boxSize.y = height;
                    changed = true;
                }
                ImGui::TextDisabled("Capsule aligned to Y axis.");
            } else {
                if (ImGui::Checkbox("Use Convex Hull (required for Rigidbody)", &obj.collider.convex)) {
                    changed = true;
                }
                ImGui::TextDisabled("Uses mesh from the object (OBJ/Model). Non-convex is static-only.");
            }

            ImGui::Spacing();
            if (ImGui::Button("Remove Collider", ImVec2(-1, 0))) {
                obj.hasCollider = false;
                changed = true;
            }

            if (changed) {
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            ImGui::Unindent(10.0f);
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasPlayerController) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.45f, 0.7f, 1.0f));
        if (ImGui::CollapsingHeader("Player Controller", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);
            bool changed = false;

            if (ImGui::Checkbox("Enabled", &obj.playerController.enabled)) {
                changed = true;
            }
            if (ImGui::DragFloat("Move Speed", &obj.playerController.moveSpeed, 0.1f, 0.1f, 100.0f, "%.2f")) {
                obj.playerController.moveSpeed = std::max(0.1f, obj.playerController.moveSpeed);
                changed = true;
            }
            if (ImGui::DragFloat("Look Sensitivity", &obj.playerController.lookSensitivity, 0.01f, 0.01f, 2.0f, "%.2f")) {
                obj.playerController.lookSensitivity = std::clamp(obj.playerController.lookSensitivity, 0.01f, 2.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Height", &obj.playerController.height, 0.01f, 0.5f, 3.0f, "%.2f")) {
                obj.playerController.height = std::clamp(obj.playerController.height, 0.5f, 3.0f);
                obj.scale.y = obj.playerController.height;
                obj.collider.boxSize.y = obj.playerController.height;
                changed = true;
            }
            if (ImGui::DragFloat("Radius", &obj.playerController.radius, 0.01f, 0.2f, 1.2f, "%.2f")) {
                obj.playerController.radius = std::clamp(obj.playerController.radius, 0.2f, 1.2f);
                obj.scale.x = obj.scale.z = obj.playerController.radius * 2.0f;
                obj.collider.boxSize.x = obj.collider.boxSize.z = obj.playerController.radius * 2.0f;
                changed = true;
            }
            if (ImGui::DragFloat("Jump Strength", &obj.playerController.jumpStrength, 0.1f, 0.1f, 30.0f, "%.1f")) {
                obj.playerController.jumpStrength = std::max(0.1f, obj.playerController.jumpStrength);
                changed = true;
            }

            if (ImGui::Button("Remove Player Controller", ImVec2(-1, 0))) {
                obj.hasPlayerController = false;
                changed = true;
            }

            if (changed) {
                obj.hasCollider = true;
                obj.collider.type = ColliderType::Capsule;
                obj.collider.convex = true;
                obj.hasRigidbody = true;
                obj.rigidbody.enabled = true;
                obj.rigidbody.useGravity = true;
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            ImGui::Unindent(10.0f);
        }
        ImGui::PopStyleColor();
    }

    if (obj.hasRigidbody) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.45f, 0.45f, 0.25f, 1.0f));
        if (ImGui::CollapsingHeader("Rigidbody", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);
            bool changed = false;

            if (ImGui::Checkbox("Enabled", &obj.rigidbody.enabled)) {
                changed = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(true);
            ImGui::Checkbox("Collider (mesh type)", &obj.rigidbody.enabled); // placeholder label to hint geometry from mesh
            ImGui::EndDisabled();

            if (ImGui::DragFloat("Mass", &obj.rigidbody.mass, 0.05f, 0.01f, 1000.0f, "%.2f")) {
                obj.rigidbody.mass = std::max(0.01f, obj.rigidbody.mass);
                changed = true;
            }
            if (ImGui::Checkbox("Use Gravity", &obj.rigidbody.useGravity)) {
                changed = true;
            }
            if (ImGui::Checkbox("Kinematic", &obj.rigidbody.isKinematic)) {
                changed = true;
            }
            if (ImGui::DragFloat("Linear Damping", &obj.rigidbody.linearDamping, 0.01f, 0.0f, 10.0f)) {
                obj.rigidbody.linearDamping = std::clamp(obj.rigidbody.linearDamping, 0.0f, 10.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Angular Damping", &obj.rigidbody.angularDamping, 0.01f, 0.0f, 10.0f)) {
                obj.rigidbody.angularDamping = std::clamp(obj.rigidbody.angularDamping, 0.0f, 10.0f);
                changed = true;
            }

            ImGui::Spacing();
            if (ImGui::Button("Remove Rigidbody", ImVec2(-1, 0))) {
                obj.hasRigidbody = false;
                changed = true;
            }

            if (changed) {
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            ImGui::Unindent(10.0f);
        }
        ImGui::PopStyleColor();
    }

    if (obj.type == ObjectType::Camera) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.45f, 0.35f, 0.65f, 1.0f));
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);
            const char* cameraTypes[] = { "Scene", "Player" };
            int camType = static_cast<int>(obj.camera.type);
            if (ImGui::Combo("Type", &camType, cameraTypes, IM_ARRAYSIZE(cameraTypes))) {
                obj.camera.type = static_cast<SceneCameraType>(camType);
                projectManager.currentProject.hasUnsavedChanges = true;
            }

            if (ImGui::SliderFloat("FOV", &obj.camera.fov, 20.0f, 120.0f, "%.0f deg")) {
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            if (ImGui::DragFloat("Near Clip", &obj.camera.nearClip, 0.01f, 0.01f, obj.camera.farClip - 0.01f, "%.3f")) {
                obj.camera.nearClip = std::max(0.01f, std::min(obj.camera.nearClip, obj.camera.farClip - 0.01f));
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            if (ImGui::DragFloat("Far Clip", &obj.camera.farClip, 0.1f, obj.camera.nearClip + 0.05f, 1000.0f, "%.1f")) {
                obj.camera.farClip = std::max(obj.camera.nearClip + 0.05f, obj.camera.farClip);
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            ImGui::Unindent(10.0f);
        }
        ImGui::PopStyleColor();
    }

    if (obj.type == ObjectType::PostFXNode) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.55f, 0.6f, 1.0f));
        if (ImGui::CollapsingHeader("Post Processing", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);
            bool changed = false;

            if (ImGui::Checkbox("Enable Node", &obj.postFx.enabled)) {
                changed = true;
            }

            ImGui::Separator();
            ImGui::TextDisabled("Bloom");
            if (ImGui::Checkbox("Bloom Enabled", &obj.postFx.bloomEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.bloomEnabled);
            if (ImGui::SliderFloat("Threshold", &obj.postFx.bloomThreshold, 0.0f, 3.0f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Intensity", &obj.postFx.bloomIntensity, 0.0f, 3.0f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Spread", &obj.postFx.bloomRadius, 0.5f, 3.5f, "%.2f")) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::TextDisabled("Color Adjustments");
            if (ImGui::Checkbox("Enable Color Adjust", &obj.postFx.colorAdjustEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.colorAdjustEnabled);
            if (ImGui::SliderFloat("Exposure (EV)", &obj.postFx.exposure, -5.0f, 5.0f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Contrast", &obj.postFx.contrast, 0.0f, 2.5f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Saturation", &obj.postFx.saturation, 0.0f, 2.5f, "%.2f")) {
                changed = true;
            }
            if (ImGui::ColorEdit3("Color Filter", &obj.postFx.colorFilter.x)) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::TextDisabled("Motion Blur");
            if (ImGui::Checkbox("Enable Motion Blur", &obj.postFx.motionBlurEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.motionBlurEnabled);
            if (ImGui::SliderFloat("Strength", &obj.postFx.motionBlurStrength, 0.0f, 0.95f, "%.2f")) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::TextDisabled("Vignette");
            if (ImGui::Checkbox("Enable Vignette", &obj.postFx.vignetteEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.vignetteEnabled);
            if (ImGui::SliderFloat("Intensity", &obj.postFx.vignetteIntensity, 0.0f, 1.5f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Smoothness", &obj.postFx.vignetteSmoothness, 0.05f, 1.0f, "%.2f")) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::TextDisabled("Ambient Occlusion");
            if (ImGui::Checkbox("Enable AO", &obj.postFx.ambientOcclusionEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.ambientOcclusionEnabled);
            if (ImGui::SliderFloat("AO Radius", &obj.postFx.aoRadius, 0.0005f, 0.01f, "%.4f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("AO Strength", &obj.postFx.aoStrength, 0.0f, 2.0f, "%.2f")) {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            ImGui::TextDisabled("Chromatic Aberration");
            if (ImGui::Checkbox("Enable Chromatic", &obj.postFx.chromaticAberrationEnabled)) {
                changed = true;
            }
            ImGui::BeginDisabled(!obj.postFx.chromaticAberrationEnabled);
            if (ImGui::SliderFloat("Fringe Amount", &obj.postFx.chromaticAmount, 0.0f, 0.01f, "%.4f")) {
                changed = true;
            }
            ImGui::EndDisabled();

            if (changed) {
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            ImGui::TextDisabled("Nodes stack in hierarchy order; latest node overrides previous settings.");
            ImGui::TextDisabled("Wireframe/line mode auto-disables post effects.");
            ImGui::Unindent(10.0f);
        }
        ImGui::PopStyleColor();
    }

    // Material section (skip for pure light objects)
    if (obj.type != ObjectType::DirectionalLight && obj.type != ObjectType::PointLight && obj.type != ObjectType::SpotLight && obj.type != ObjectType::AreaLight && obj.type != ObjectType::Camera && obj.type != ObjectType::PostFXNode) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.35f, 0.55f, 1.0f));

        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);

            auto textureField = [&](const char* label, const char* idSuffix, std::string& path) {
                bool changed = false;
                ImGui::PushID(idSuffix);
                ImGui::TextUnformatted(label);
                ImGui::SetNextItemWidth(-160);
                char buf[512] = {};
                std::snprintf(buf, sizeof(buf), "%s", path.c_str());
                if (ImGui::InputText("##Path", buf, sizeof(buf))) {
                    path = buf;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) {
                    path.clear();
                    changed = true;
                }
                ImGui::SameLine();
                bool canUseTex = !fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile) &&
                                 fileBrowser.isTextureFile(fs::directory_entry(fileBrowser.selectedFile));
                ImGui::BeginDisabled(!canUseTex);
                std::string btnLabel = std::string("Use Selection##") + idSuffix;
                if (ImGui::SmallButton(btnLabel.c_str())) {
                    path = fileBrowser.selectedFile.string();
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
                return changed;
            };

            bool materialChanged = false;

            ImGui::TextColored(ImVec4(0.8f, 0.7f, 1.0f, 1.0f), "Surface Inputs");
            if (ImGui::ColorEdit3("Base Color", &obj.material.color.x)) {
                materialChanged = true;
            }

            float metallic = obj.material.specularStrength;
            if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) {
                obj.material.specularStrength = metallic;
                materialChanged = true;
            }

            float smoothness = obj.material.shininess / 256.0f;
            if (ImGui::SliderFloat("Smoothness", &smoothness, 0.0f, 1.0f)) {
                smoothness = std::clamp(smoothness, 0.0f, 1.0f);
                obj.material.shininess = smoothness * 256.0f;
                materialChanged = true;
            }

            if (ImGui::SliderFloat("Ambient Light", &obj.material.ambientStrength, 0.0f, 1.0f)) {
                materialChanged = true;
            }
            if (ImGui::SliderFloat("Detail Mix", &obj.material.textureMix, 0.0f, 1.0f)) {
                materialChanged = true;
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Maps");
            materialChanged |= textureField("Base Map", "ObjAlbedo", obj.albedoTexturePath);
            if (ImGui::Checkbox("Use Detail Map", &obj.useOverlay)) {
                materialChanged = true;
            }
            materialChanged |= textureField("Detail Map", "ObjOverlay", obj.overlayTexturePath);
            materialChanged |= textureField("Normal Map", "ObjNormal", obj.normalMapPath);

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1.0f), "Shader");
            auto shaderField = [&](const char* label, const char* idSuffix, std::string& path) {
                bool changed = false;
                ImGui::PushID(idSuffix);
                ImGui::TextUnformatted(label);
                ImGui::SetNextItemWidth(-160);
                char buf[512] = {};
                std::snprintf(buf, sizeof(buf), "%s", path.c_str());
                if (ImGui::InputText("##Path", buf, sizeof(buf))) {
                    path = buf;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) {
                    path.clear();
                    changed = true;
                }
                bool selectionIsShader = false;
                if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
                    selectionIsShader = fileBrowser.getFileCategory(fs::directory_entry(fileBrowser.selectedFile)) == FileCategory::Shader;
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!selectionIsShader);
                std::string btn = std::string("Use Selection##") + idSuffix;
                if (ImGui::SmallButton(btn.c_str())) {
                    path = fileBrowser.selectedFile.string();
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
                return changed;
            };
            materialChanged |= shaderField("Vertex Shader", "ObjVert", obj.vertexShaderPath);
            materialChanged |= shaderField("Fragment Shader", "ObjFrag", obj.fragmentShaderPath);

            ImGui::BeginDisabled(obj.vertexShaderPath.empty() && obj.fragmentShaderPath.empty());
            if (ImGui::Button("Reload Shader")) {
                renderer.forceReloadShader(obj.vertexShaderPath, obj.fragmentShaderPath);
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Material");
            ImGui::SameLine();
            ImVec4 previewColor(obj.material.color.x, obj.material.color.y, obj.material.color.z, 1.0f);
            ImVec2 sphereStart = ImGui::GetCursorScreenPos();
            float sphereRadius = 12.0f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 shadowCol = ImGui::ColorConvertFloat4ToU32(ImVec4(previewColor.x * 0.3f, previewColor.y * 0.3f, previewColor.z * 0.3f, 1.0f));
            ImU32 baseCol = ImGui::ColorConvertFloat4ToU32(previewColor);
            ImU32 highlightCol = ImGui::ColorConvertFloat4ToU32(ImVec4(std::min(1.0f, previewColor.x + 0.25f), std::min(1.0f, previewColor.y + 0.25f), std::min(1.0f, previewColor.z + 0.25f), 1.0f));
            ImVec2 center = ImVec2(sphereStart.x + sphereRadius, sphereStart.y + sphereRadius);
            dl->AddCircleFilled(center, sphereRadius, shadowCol);
            dl->AddCircleFilled(ImVec2(center.x, center.y - 1.5f), sphereRadius - 1.5f, baseCol);
            dl->AddCircleFilled(ImVec2(center.x - sphereRadius * 0.35f, center.y - sphereRadius * 0.5f), sphereRadius * 0.35f, highlightCol);
            ImGui::Dummy(ImVec2(sphereRadius * 2.0f, sphereRadius * 2.0f));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", obj.materialPath.empty() ? "Unsaved Material" : fs::path(obj.materialPath).filename().string().c_str());
            ImGui::Text("Material Asset");

            char matPathBuf[512] = {};
            std::snprintf(matPathBuf, sizeof(matPathBuf), "%s", obj.materialPath.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##MaterialPath", matPathBuf, sizeof(matPathBuf))) {
                obj.materialPath = matPathBuf;
                materialChanged = true;
            }

            bool hasMatPath = obj.materialPath.size() > 0;
            ImGui::BeginDisabled(!hasMatPath);
            if (ImGui::Button("Save Material")) {
                saveMaterialToFile(obj);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload Material")) {
                loadMaterialFromFile(obj);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(!browserHasMaterial);
            if (ImGui::Button("Load Selected")) {
                obj.materialPath = selectedMaterialPath.string();
                loadMaterialFromFile(obj);
                materialChanged = true;
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::TextDisabled("Material Slots");
            for (size_t slot = 0; slot < obj.additionalMaterialPaths.size(); ++slot) {
                ImGui::PushID(static_cast<int>(slot));
                char slotBuf[512] = {};
                std::snprintf(slotBuf, sizeof(slotBuf), "%s", obj.additionalMaterialPaths[slot].c_str());
                ImGui::SetNextItemWidth(-140);
                if (ImGui::InputText("##AdditionalMat", slotBuf, sizeof(slotBuf))) {
                    obj.additionalMaterialPaths[slot] = slotBuf;
                    materialChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Use Selection / Blender")) {
                    if (!fileBrowser.selectedFile.empty() && fs::exists(fileBrowser.selectedFile)) {
                        fs::directory_entry entry(fileBrowser.selectedFile);
                        if (fileBrowser.getFileCategory(entry) == FileCategory::Material) {
                            obj.additionalMaterialPaths[slot] = entry.path().string();
                            materialChanged = true;
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    obj.additionalMaterialPaths.erase(obj.additionalMaterialPaths.begin() + static_cast<long>(slot));
                    materialChanged = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("Add Material Slot")) {
                obj.additionalMaterialPaths.push_back("");
                materialChanged = true;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Preview");
            ImVec4 previewColorBar(obj.material.color.x, obj.material.color.y, obj.material.color.z, 1.0f);
            ImGui::ColorButton("##MaterialPreview", previewColorBar, ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetContentRegionAvail().x, 32.0f));

            ImGui::Spacing();
            addComponentButtonShown = true;
            ImGui::PushID("MaterialAddComponent");
            if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
                ImGui::OpenPopup("AddComponentPopup");
            }
            if (ImGui::BeginPopup("AddComponentPopup")) {
                if (!obj.hasRigidbody && ImGui::MenuItem("Rigidbody")) {
                    obj.hasRigidbody = true;
                    obj.rigidbody = RigidbodyComponent{};
                    materialChanged = true;
                }
                if (!obj.hasPlayerController && ImGui::MenuItem("Player Controller")) {
                    obj.hasPlayerController = true;
                    obj.playerController = PlayerControllerComponent{};
                    obj.hasCollider = true;
                    obj.collider.type = ColliderType::Capsule;
                    obj.collider.boxSize = glm::vec3(obj.playerController.radius * 2.0f, obj.playerController.height, obj.playerController.radius * 2.0f);
                    obj.collider.convex = true;
                    obj.hasRigidbody = true;
                    obj.rigidbody.enabled = true;
                    obj.rigidbody.useGravity = true;
                    obj.rigidbody.isKinematic = false;
                    obj.scale = glm::vec3(obj.playerController.radius * 2.0f, obj.playerController.height, obj.playerController.radius * 2.0f);
                    materialChanged = true;
                }
                if (!obj.hasCollider && ImGui::BeginMenu("Collider")) {
                    if (ImGui::MenuItem("Box Collider")) {
                        obj.hasCollider = true;
                        obj.collider = ColliderComponent{};
                        obj.collider.boxSize = glm::max(obj.scale, glm::vec3(0.01f));
                        materialChanged = true;
                        addComponentButtonShown = true;
                    }
                    if (ImGui::MenuItem("Mesh Collider (Triangle)")) {
                        obj.hasCollider = true;
                        obj.collider = ColliderComponent{};
                        obj.collider.type = ColliderType::Mesh;
                        obj.collider.convex = false;
                        materialChanged = true;
                        addComponentButtonShown = true;
                    }
                    if (ImGui::MenuItem("Mesh Collider (Convex)")) {
                        obj.hasCollider = true;
                        obj.collider = ColliderComponent{};
                        obj.collider.type = ColliderType::ConvexMesh;
                        obj.collider.convex = true;
                        materialChanged = true;
                        addComponentButtonShown = true;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Script")) {
                    obj.scripts.push_back(ScriptComponent{});
                    materialChanged = true;
                    addComponentButtonShown = true;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();

            if (materialChanged) {
                projectManager.currentProject.hasUnsavedChanges = true;
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopStyleColor();
    }

    if (obj.type == ObjectType::DirectionalLight || obj.type == ObjectType::PointLight || obj.type == ObjectType::SpotLight || obj.type == ObjectType::AreaLight) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.45f, 0.2f, 1.0f));
        if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);

            int currentType = (obj.type == ObjectType::DirectionalLight) ? 0 :
                              (obj.type == ObjectType::PointLight) ? 1 :
                              (obj.type == ObjectType::SpotLight) ? 2 : 3;
            const char* typeLabels[] = { "Directional", "Point", "Spot", "Area" };
            if (ImGui::Combo("Type", &currentType, typeLabels, IM_ARRAYSIZE(typeLabels))) {
                if (currentType == 0) obj.type = ObjectType::DirectionalLight;
                else if (currentType == 1) obj.type = ObjectType::PointLight;
                else if (currentType == 2) obj.type = ObjectType::SpotLight;
                else obj.type = ObjectType::AreaLight;
                obj.light.type = (currentType == 0 ? LightType::Directional :
                                  currentType == 1 ? LightType::Point :
                                  currentType == 2 ? LightType::Spot : LightType::Area);
                // Reset sensible defaults when type changes
                if (obj.type == ObjectType::DirectionalLight) {
                    obj.light.intensity = 1.0f;
                } else if (obj.type == ObjectType::PointLight) {
                    obj.light.range = 12.0f;
                    obj.light.intensity = 2.0f;
                } else if (obj.type == ObjectType::SpotLight) {
                    obj.light.range = 15.0f;
                    obj.light.intensity = 2.5f;
                    obj.light.innerAngle = 15.0f;
                    obj.light.outerAngle = 25.0f;
                } else if (obj.type == ObjectType::AreaLight) {
                    obj.light.range = 10.0f;
                    obj.light.intensity = 3.0f;
                    obj.light.size = glm::vec2(2.0f, 2.0f);
                }
                projectManager.currentProject.hasUnsavedChanges = true;
            }

            if (ImGui::ColorEdit3("Color", &obj.light.color.x)) {
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            if (ImGui::SliderFloat("Intensity", &obj.light.intensity, 0.0f, 10.0f)) {
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            if (obj.type != ObjectType::DirectionalLight) {
                if (ImGui::SliderFloat("Range", &obj.light.range, 0.0f, 50.0f)) {
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
            }
            if (ImGui::Checkbox("Enabled", &obj.light.enabled)) {
                projectManager.currentProject.hasUnsavedChanges = true;
            }

            if (obj.type == ObjectType::SpotLight) {
                if (ImGui::SliderFloat("Inner Angle", &obj.light.innerAngle, 1.0f, 90.0f)) {
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
                if (ImGui::SliderFloat("Outer Angle", &obj.light.outerAngle, obj.light.innerAngle, 120.0f)) {
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
            }

            if (obj.type == ObjectType::AreaLight) {
                if (ImGui::DragFloat2("Size", &obj.light.size.x, 0.05f, 0.1f, 10.0f)) {
                    projectManager.currentProject.hasUnsavedChanges = true;
                }
            }

            ImGui::Unindent(10.0f);
        }
        ImGui::PopStyleColor();
    }

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

    if (obj.type == ObjectType::Model) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.45f, 0.65f, 1.0f));

        if (ImGui::CollapsingHeader("Model Info", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(10.0f);

            const auto* meshInfo = getModelLoader().getMeshInfo(obj.meshId);
            if (meshInfo) {
                ImGui::Text("Source File:");
                ImGui::TextDisabled("%s", fs::path(meshInfo->path).filename().string().c_str());

                ImGui::Spacing();

                ImGui::Text("Vertices: %d", meshInfo->vertexCount);
                ImGui::Text("Faces: %d", meshInfo->faceCount);
                ImGui::Text("Has Normals: %s", meshInfo->hasNormals ? "Yes" : "No");
                ImGui::Text("Has UVs: %s", meshInfo->hasTexCoords ? "Yes" : "No");

                ImGui::Spacing();

                if (ImGui::Button("Reload Model", ImVec2(-1, 0))) {
                    ModelLoadResult result = getModelLoader().loadModel(obj.meshPath);
                    if (result.success) {
                        obj.meshId = result.meshIndex;
                        addConsoleMessage("Reloaded model: " + obj.name, ConsoleMessageType::Success);
                    } else {
                        addConsoleMessage("Failed to reload: " + result.errorMessage, ConsoleMessageType::Error);
                    }
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Model data not found!");
                ImGui::TextDisabled("Path: %s", obj.meshPath.c_str());

                if (ImGui::Button("Try Reload", ImVec2(-1, 0))) {
                    ModelLoadResult result = getModelLoader().loadModel(obj.meshPath);
                    if (result.success) {
                        obj.meshId = result.meshIndex;
                        addConsoleMessage("Reloaded model: " + obj.name, ConsoleMessageType::Success);
                    } else {
                        addConsoleMessage("Failed to reload: " + result.errorMessage, ConsoleMessageType::Error);
                    }
                }
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopStyleColor();
    }

    if (!addComponentButtonShown) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Components");
        addComponentButtonShown = true;
        ImGui::PushID("FallbackAddComponent");
        if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
            ImGui::OpenPopup("AddComponentPopup");
        }
        if (ImGui::BeginPopup("AddComponentPopup")) {
            if (!obj.hasRigidbody && ImGui::MenuItem("Rigidbody")) {
                obj.hasRigidbody = true;
                obj.rigidbody = RigidbodyComponent{};
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            if (!obj.hasPlayerController && ImGui::MenuItem("Player Controller")) {
                obj.hasPlayerController = true;
                obj.playerController = PlayerControllerComponent{};
                obj.hasCollider = true;
                obj.collider.type = ColliderType::Capsule;
                obj.collider.boxSize = glm::vec3(obj.playerController.radius * 2.0f, obj.playerController.height, obj.playerController.radius * 2.0f);
                obj.collider.convex = true;
                obj.hasRigidbody = true;
                obj.rigidbody.enabled = true;
                obj.rigidbody.useGravity = true;
                obj.rigidbody.isKinematic = false;
                obj.scale = glm::vec3(obj.playerController.radius * 2.0f, obj.playerController.height, obj.playerController.radius * 2.0f);
                projectManager.currentProject.hasUnsavedChanges = true;
                addComponentButtonShown = true;
            }
            if (!obj.hasCollider && ImGui::BeginMenu("Collider")) {
                if (ImGui::MenuItem("Box Collider")) {
                    obj.hasCollider = true;
                    obj.collider = ColliderComponent{};
                    obj.collider.boxSize = glm::max(obj.scale, glm::vec3(0.01f));
                    projectManager.currentProject.hasUnsavedChanges = true;
                    addComponentButtonShown = true;
                }
                if (ImGui::MenuItem("Mesh Collider (Triangle)")) {
                    obj.hasCollider = true;
                    obj.collider = ColliderComponent{};
                    obj.collider.type = ColliderType::Mesh;
                    obj.collider.convex = false;
                    projectManager.currentProject.hasUnsavedChanges = true;
                    addComponentButtonShown = true;
                }
                if (ImGui::MenuItem("Mesh Collider (Convex)")) {
                    obj.hasCollider = true;
                    obj.collider = ColliderComponent{};
                    obj.collider.type = ColliderType::ConvexMesh;
                    obj.collider.convex = true;
                    projectManager.currentProject.hasUnsavedChanges = true;
                    addComponentButtonShown = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Script")) {
                obj.scripts.push_back(ScriptComponent{});
                projectManager.currentProject.hasUnsavedChanges = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.9f, 1.0f), "Scripts");

    bool scriptsChanged = false;
    int scriptToRemove = -1;

    for (size_t i = 0; i < obj.scripts.size(); ++i) {
        ImGui::Separator();
        ImGui::PushID(static_cast<int>(i));
        ScriptComponent& sc = obj.scripts[i];

        std::string headerLabel = sc.path.empty() ? "Script" : fs::path(sc.path).filename().string();
        std::string headerId = headerLabel + "##ScriptHeader" + std::to_string(i);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
        bool open = ImGui::CollapsingHeader(headerId.c_str(), flags);

        ImVec2 headerMin = ImGui::GetItemRectMin();
        ImVec2 headerMax = ImGui::GetItemRectMax();
        float menuWidth = ImGui::GetFrameHeight();
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(headerMax.x - menuWidth - ImGui::GetStyle().ItemSpacing.x, headerMin.y + (headerMax.y - headerMin.y - menuWidth) * 0.5f));
        if (ImGui::SmallButton("...")) {
            ImGui::OpenPopup("ScriptComponentMenu");
        }
        if (ImGui::BeginPopup("ScriptComponentMenu")) {
            if (ImGui::MenuItem("Compile", nullptr, false, !sc.path.empty())) {
                compileScriptFile(sc.path);
            }
            if (ImGui::MenuItem("Remove")) {
                scriptToRemove = static_cast<int>(i);
            }
            ImGui::EndPopup();
        }

        if (scriptToRemove == static_cast<int>(i)) {
            ImGui::PopID();
            continue;
        }

        if (open) {
            char pathBuf[512] = {};
            std::snprintf(pathBuf, sizeof(pathBuf), "%s", sc.path.c_str());
            ImGui::TextDisabled("Path");
            ImGui::SetNextItemWidth(-140);
            if (ImGui::InputText("##ScriptPath", pathBuf, sizeof(pathBuf))) {
                sc.path = pathBuf;
                scriptsChanged = true;
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Use Selection")) {
                if (!fileBrowser.selectedFile.empty()) {
                    fs::directory_entry entry(fileBrowser.selectedFile);
                    if (fileBrowser.getFileCategory(entry) == FileCategory::Script) {
                        sc.path = entry.path().string();
                        scriptsChanged = true;
                    }
                }
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(sc.path.empty());
            if (ImGui::SmallButton("Compile")) {
                compileScriptFile(sc.path);
            }
            ImGui::EndDisabled();

            if (!sc.path.empty()) {
                fs::path binary = resolveScriptBinary(sc.path);
                sc.lastBinaryPath = binary.string();
                ScriptRuntime::InspectorFn inspector = scriptRuntime.getInspector(binary);
                if (inspector) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Inspector (from script)");
                    ScriptContext ctx;
                    ctx.engine = this;
                    ctx.object = &obj;
                    inspector(ctx);
                } else if (!scriptRuntime.getLastError().empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "Inspector load failed");
                    ImGui::TextWrapped("%s", scriptRuntime.getLastError().c_str());
                } else {
                    ImGui::TextDisabled("No inspector exported (Script_OnInspector)");
                }
            }

            ImGui::TextDisabled("Settings");
            for (size_t s = 0; s < sc.settings.size(); ++s) {
                ImGui::PushID(static_cast<int>(s));
                char keyBuf[128] = {};
                char valBuf[256] = {};
                std::snprintf(keyBuf, sizeof(keyBuf), "%s", sc.settings[s].key.c_str());
                std::snprintf(valBuf, sizeof(valBuf), "%s", sc.settings[s].value.c_str());
                ImGui::SetNextItemWidth(140);
                if (ImGui::InputText("##Key", keyBuf, sizeof(keyBuf))) {
                    sc.settings[s].key = keyBuf;
                    scriptsChanged = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-100);
                if (ImGui::InputText("##Value", valBuf, sizeof(valBuf))) {
                    sc.settings[s].value = valBuf;
                    scriptsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) {
                    sc.settings.erase(sc.settings.begin() + static_cast<long>(s));
                    scriptsChanged = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }

            if (ImGui::SmallButton("Add Setting")) {
                sc.settings.push_back(ScriptSetting{"", ""});
                scriptsChanged = true;
            }
        }

        ImGui::PopID();
    }

    if (scriptToRemove >= 0 && scriptToRemove < static_cast<int>(obj.scripts.size())) {
        obj.scripts.erase(obj.scripts.begin() + scriptToRemove);
        scriptsChanged = true;
    }

    if (scriptsChanged) {
        projectManager.currentProject.hasUnsavedChanges = true;
    }

    if (browserHasMaterial) {
        ImGui::Spacing();
        renderMaterialAssetPanel("Material Asset (File Browser)", true);
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

        for (const auto& obj : sceneObjects) {
            if (obj.type == ObjectType::Camera) {
                drawCameraDirection(obj);
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

        for (const auto& obj : sceneObjects) {
            if (obj.type == ObjectType::PointLight || obj.type == ObjectType::SpotLight || obj.type == ObjectType::AreaLight) {
                drawLightOverlays(obj);
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

        // Left-click picking inside viewport
        if (mouseOverViewportImage &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGuizmo::IsUsing() && !ImGuizmo::IsOver())
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
                    previewCam->camera.farClip
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

    if (showCompilePopup) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(520, 240), ImGuiCond_FirstUseEver);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("Script Compile", &showCompilePopup, flags)) {
            ImGui::TextWrapped("%s", lastCompileStatus.c_str());
            ImGui::Separator();
            ImGui::BeginChild("CompileLog", ImVec2(0, -40), true);
            ImGui::TextUnformatted(lastCompileLog.c_str());
            ImGui::EndChild();
            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(80, 0))) {
                showCompilePopup = false;
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

    // General model import dialog (Assimp-backed)
    if (showImportModelDialog) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(420, 180), ImGuiCond_Appearing);

        if (ImGui::Begin("Import Model", &showImportModelDialog,
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::Text("File: %s", fs::path(pendingModelPath).filename().string().c_str());
            ImGui::TextDisabled("%s", pendingModelPath.c_str());

            ImGui::Spacing();

            ImGui::Text("Object Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##ImportModelName", importModelName, sizeof(importModelName));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float buttonWidth = 80;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2 - 20);

            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
                showImportModelDialog = false;
                pendingModelPath.clear();
            }
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.4f, 1.0f));
            if (ImGui::Button("Import", ImVec2(buttonWidth, 0))) {
                importModelToScene(pendingModelPath, importModelName);
                showImportModelDialog = false;
                pendingModelPath.clear();
                memset(importModelName, 0, sizeof(importModelName));
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::End();
    }
}

void Engine::renderProjectBrowserPanel() {
    ImVec4 headerCol = ImVec4(0.20f, 0.27f, 0.36f, 1.0f);
    ImVec4 headerColActive = ImVec4(0.24f, 0.34f, 0.46f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Header, headerCol);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, headerColActive);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, headerColActive);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));

    ImGui::Begin("Project Manager", &showProjectBrowser);

    if (!projectManager.currentProject.isLoaded) {
        ImGui::TextDisabled("No project loaded");
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
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
    
    if (ImGui::CollapsingHeader("Loaded OBJ Meshes")) {
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

    if (ImGui::CollapsingHeader("Loaded Models (Assimp)")) {
        const auto& meshes = getModelLoader().getAllMeshes();
        if (meshes.empty()) {
            ImGui::TextDisabled("No models loaded");
            ImGui::TextDisabled("Import FBX/GLTF/other supported models from File Browser");
        } else {
            for (size_t i = 0; i < meshes.size(); i++) {
                const auto& mesh = meshes[i];
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

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}

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
