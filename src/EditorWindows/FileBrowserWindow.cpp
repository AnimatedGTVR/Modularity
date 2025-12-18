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

