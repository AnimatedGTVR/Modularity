#include "Engine.h"
#include "ModelLoader.h"
#include <algorithm>
#include <cfloat>

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
        float padding = size * 0.15f;
        
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
    
    // === TOOLBAR ===
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    
    // Navigation buttons
    bool canGoBack = fileBrowser.historyIndex > 0;
    bool canGoForward = fileBrowser.historyIndex < (int)fileBrowser.pathHistory.size() - 1;
    bool canGoUp = fileBrowser.currentPath != fileBrowser.projectRoot && 
                   fileBrowser.currentPath.has_parent_path();
    
    ImGui::BeginDisabled(!canGoBack);
    if (ImGui::Button("<##Back", ImVec2(24, 0))) {
        fileBrowser.navigateBack();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Back");
    }
    
    ImGui::SameLine();
    
    ImGui::BeginDisabled(!canGoForward);
    if (ImGui::Button(">##Forward", ImVec2(24, 0))) {
        fileBrowser.navigateForward();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Forward");
    }
    
    ImGui::SameLine();
    
    ImGui::BeginDisabled(!canGoUp);
    if (ImGui::Button("^##Up", ImVec2(24, 0))) {
        fileBrowser.navigateUp();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Up one folder");
    }
    
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    
    // Breadcrumb path
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
    
    fs::path relativePath = fs::relative(fileBrowser.currentPath, fileBrowser.projectRoot);
    std::vector<fs::path> pathParts;
    fs::path accumulated = fileBrowser.projectRoot;
    
    pathParts.push_back(fileBrowser.projectRoot);
    for (const auto& part : relativePath) {
        if (part != ".") {
            accumulated /= part;
            pathParts.push_back(accumulated);
        }
    }
    
    for (size_t i = 0; i < pathParts.size(); i++) {
        std::string name = (i == 0) ? "Project" : pathParts[i].filename().string();
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(name.c_str())) {
            fileBrowser.navigateTo(pathParts[i]);
        }
        ImGui::PopID();
        if (i < pathParts.size() - 1) {
            ImGui::SameLine(0, 2);
            ImGui::TextDisabled("/");
            ImGui::SameLine(0, 2);
        }
    }
    
    ImGui::PopStyleColor(2);
    
    ImGui::PopStyleVar();
    
    // === SECOND ROW: Search, Scale Slider, View Mode ===
    ImGui::Spacing();
    
    // Search box
    ImGui::SetNextItemWidth(150);
    if (ImGui::InputTextWithHint("##Search", "Search...", fileBrowserSearch, sizeof(fileBrowserSearch))) {
        fileBrowser.searchFilter = fileBrowserSearch;
        fileBrowser.needsRefresh = true;
    }
    
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    
    bool isGridMode = fileBrowser.viewMode == FileBrowserViewMode::Grid;
    if (isGridMode) {
        ImGui::Text("Size:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("##IconScale", &fileBrowserIconScale, 0.5f, 2.0f, "%.1fx");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Icon Size: %.1fx", fileBrowserIconScale);
        }
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
    }
    
    // View mode toggle
    if (ImGui::Button(isGridMode ? "Grid" : "List", ImVec2(50, 0))) {
        fileBrowser.viewMode = isGridMode ? FileBrowserViewMode::List : FileBrowserViewMode::Grid;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isGridMode ? "Switch to List View" : "Switch to Grid View");
    }
    
    ImGui::SameLine();
    
    if (ImGui::Button("Refresh", ImVec2(60, 0))) {
        fileBrowser.needsRefresh = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("New Mat", ImVec2(70, 0))) {
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
    
    ImGui::Separator();
    
    // === FILE CONTENT AREA ===
    ImGui::BeginChild("FileContent", ImVec2(0, 0), false);
    
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
                    }
                    if (fileBrowser.getFileCategory(entry) == FileCategory::Material) {
                        if (ImGui::MenuItem("Apply to Selected")) {
                            if (SceneObject* sel = getSelectedObject()) {
                                sel->materialPath = entry.path().string();
                                loadMaterialFromFile(*sel);
                            }
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
                }
                if (fileBrowser.getFileCategory(entry) == FileCategory::Material) {
                    if (ImGui::MenuItem("Apply to Selected")) {
                        if (SceneObject* sel = getSelectedObject()) {
                            sel->materialPath = entry.path().string();
                            loadMaterialFromFile(*sel);
                        }
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

        ImGui::TextDisabled("Modularity Engine - Version 1.0.1");

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
            if (ImGui::MenuItem("Directional Light")) addObject(ObjectType::DirectionalLight, "Directional Light");
            if (ImGui::MenuItem("Point Light")) addObject(ObjectType::PointLight, "Point Light");
            if (ImGui::MenuItem("Spot Light")) addObject(ObjectType::SpotLight, "Spot Light");
            if (ImGui::MenuItem("Area Light")) addObject(ObjectType::AreaLight, "Area Light");
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
            if (ImGui::MenuItem("Directional Light")) addObject(ObjectType::DirectionalLight, "Directional Light");
            if (ImGui::MenuItem("Point Light")) addObject(ObjectType::PointLight, "Point Light");
            if (ImGui::MenuItem("Spot Light")) addObject(ObjectType::SpotLight, "Spot Light");
            if (ImGui::MenuItem("Area Light")) addObject(ObjectType::AreaLight, "Area Light");
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
        case ObjectType::Model: icon = "[A]"; break;
        case ObjectType::DirectionalLight: icon = "(D)"; break;
        case ObjectType::PointLight: icon = "(P)"; break;
        case ObjectType::SpotLight: icon = "(S)"; break;
        case ObjectType::AreaLight: icon = "(L)"; break;
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

void Engine::renderInspectorPanel() {
    ImGui::Begin("Inspector", &showInspector);

    // Environment controls
    if (Skybox* skybox = renderer.getSkybox()) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.45f, 0.55f, 1.0f));
        if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
            float tod = skybox->getTimeOfDay();
            ImGui::TextDisabled("Day/Night Cycle");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##DayNight", &tod, 0.0f, 1.0f, "%.2f")) {
                skybox->setTimeOfDay(tod);
                projectManager.currentProject.hasUnsavedChanges = true;
            }
        }
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

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
                    inspectedUseOverlay
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
                if (ImGui::Button("Reload")) {
                    inspectedMaterialValid = loadMaterialData(
                        selectedMaterialPath.string(),
                        inspectedMaterial,
                        inspectedAlbedo,
                        inspectedOverlay,
                        inspectedNormal,
                        inspectedUseOverlay
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
                            inspectedUseOverlay))
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

    if (selectedObjectId == -1) {
        if (browserHasMaterial) {
            renderMaterialAssetPanel("Material Asset", true);
        } else {
            ImGui::TextDisabled("No object selected");
        }
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
        const char* typeLabel = "Unknown";
        switch (obj.type) {
            case ObjectType::Cube:       typeLabel = "Cube"; break;
            case ObjectType::Sphere:     typeLabel = "Sphere"; break;
            case ObjectType::Capsule:    typeLabel = "Capsule"; break;
            case ObjectType::OBJMesh:    typeLabel = "OBJ Mesh"; break;
            case ObjectType::Model:      typeLabel = "Model"; break;
        case ObjectType::DirectionalLight: typeLabel = "Directional Light"; break;
        case ObjectType::PointLight: typeLabel = "Point Light"; break;
        case ObjectType::SpotLight:  typeLabel = "Spot Light"; break;
        case ObjectType::AreaLight:  typeLabel = "Area Light"; break;
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

    // Material section (skip for pure light objects)
    if (obj.type != ObjectType::DirectionalLight && obj.type != ObjectType::PointLight && obj.type != ObjectType::SpotLight && obj.type != ObjectType::AreaLight) {
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
            ImGui::Separator();
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

        renderer.beginRender(view, proj, camera.position);
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
                        hit = rayAabb(localOrigin, localDir, aabbMin, aabbMax, hitT);
                        break;
                    }
                    case ObjectType::Model: {
                        const auto* info = getModelLoader().getMeshInfo(obj.meshId);
                        if (info && info->boundsMin.x < info->boundsMax.x) {
                            aabbMin = info->boundsMin;
                            aabbMax = info->boundsMax;
                        }
                        hit = rayAabb(localOrigin, localDir, aabbMin, aabbMax, hitT);
                        break;
                    }
                    case ObjectType::DirectionalLight:
                    case ObjectType::PointLight:
                    case ObjectType::SpotLight:
                    case ObjectType::AreaLight:
                        hit = raySphere(localOrigin, localDir, 0.3f, hitT);
                        break;
                }

                if (hit && hitT < closest && hitT >= 0.0f) {
                    closest = hitT;
                    hitId = obj.id;
                }
            }

            viewportController.setFocused(true);
            if (hitId != -1) {
                selectedObjectId = hitId;
            } else {
                selectedObjectId = -1;
            }
        }

        if (mouseOverViewportImage && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            viewportController.setFocused(true);
            cursorLocked = true;
            glfwSetInputMode(editorWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            camera.firstMouse = true;
        }

        if (cursorLocked && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            cursorLocked = false;
            glfwSetInputMode(editorWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            camera.firstMouse = true;
        }
        if (cursorLocked) {
            viewportController.setFocused(true);
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
}
