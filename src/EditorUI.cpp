#include "EditorUI.h"

#pragma region File Browser
FileBrowser::FileBrowser() {
    currentPath = fs::current_path();
    projectRoot = currentPath;
}

void FileBrowser::refresh() {
    entries.clear();
    try {
        for (const auto& entry : fs::directory_iterator(currentPath)) {
            // Skip hidden files if not showing them
            std::string filename = entry.path().filename().string();
            if (!showHiddenFiles && !filename.empty() && filename[0] == '.') {
                continue;
            }
            
            // Apply search filter if any
            if (!matchesFilter(entry)) {
                continue;
            }
            
            entries.push_back(entry);
        }
        // Sort: folders first, then alphabetically
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            if (a.is_directory() != b.is_directory()) {
                return a.is_directory() > b.is_directory();
            }
            return a.path().filename().string() < b.path().filename().string();
        });
    } catch (...) {
    }
    needsRefresh = false;
}

void FileBrowser::navigateUp() {
    if (currentPath.has_parent_path() && currentPath != currentPath.root_path()) {
        // Don't go above project root
        if (currentPath != projectRoot) {
            navigateTo(currentPath.parent_path());
        }
    }
}

void FileBrowser::navigateTo(const fs::path& path) {
    if (fs::is_directory(path)) {
        // Add to history
        if (historyIndex < 0 || pathHistory.empty() || pathHistory[historyIndex] != currentPath) {
            // Clear forward history
            if (historyIndex >= 0 && historyIndex < (int)pathHistory.size() - 1) {
                pathHistory.erase(pathHistory.begin() + historyIndex + 1, pathHistory.end());
            }
            pathHistory.push_back(currentPath);
            historyIndex = (int)pathHistory.size() - 1;
        }
        
        currentPath = path;
        needsRefresh = true;
    }
}

void FileBrowser::navigateBack() {
    if (historyIndex > 0) {
        historyIndex--;
        currentPath = pathHistory[historyIndex];
        needsRefresh = true;
    }
}

void FileBrowser::navigateForward() {
    if (historyIndex < (int)pathHistory.size() - 1) {
        historyIndex++;
        currentPath = pathHistory[historyIndex];
        needsRefresh = true;
    }
}

void FileBrowser::setProjectRoot(const fs::path& root) {
    projectRoot = root;
    currentPath = root;
    pathHistory.clear();
    historyIndex = -1;
    needsRefresh = true;
}

FileCategory FileBrowser::getFileCategory(const fs::directory_entry& entry) const {
    if (entry.is_directory()) return FileCategory::Folder;
    
    std::string ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    // Scene files
    if (ext == ".modu" || ext == ".scene") return FileCategory::Scene;
    
    // Model files
    if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb" ||
        ext == ".dae" || ext == ".blend" || ext == ".3ds" || ext == ".b3d" ||
        ext == ".ply" || ext == ".stl" || ext == ".x" || ext == ".md5mesh" ||
        ext == ".rmesh") {
        return FileCategory::Model;
    }
    
    // Material files
    if (ext == ".mat") return FileCategory::Material;

    // Texture files
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || 
        ext == ".tga" || ext == ".dds" || ext == ".hdr") {
        return FileCategory::Texture;
    }
    
    // Shader files
    if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".hlsl" ||
        ext == ".shader") {
        return FileCategory::Shader;
    }
    
    // Script files
    if (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp" ||
        ext == ".lua" || ext == ".py" || ext == ".cs") {
        return FileCategory::Script;
    }
    
    // Audio files
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") {
        return FileCategory::Audio;
    }
    
    // Text files
    if (ext == ".txt" || ext == ".md" || ext == ".json" || ext == ".xml" ||
        ext == ".yaml" || ext == ".ini" || ext == ".cfg") {
        return FileCategory::Text;
    }
    
    return FileCategory::Unknown;
}

const char* FileBrowser::getFileIcon(const fs::directory_entry& entry) const {
    FileCategory category = getFileCategory(entry);
    switch (category) {
        case FileCategory::Folder:  return "folder";
        case FileCategory::Scene:   return "scene";
        case FileCategory::Model:   return "model";
        case FileCategory::Material: return "material";
        case FileCategory::Texture: return "image";
        case FileCategory::Shader:  return "shader";
        case FileCategory::Script:  return "code";
        case FileCategory::Audio:   return "audio";
        case FileCategory::Text:    return "text";
        default:                    return "file";
    }
}

bool FileBrowser::isModelFile(const fs::directory_entry& entry) const {
    return getFileCategory(entry) == FileCategory::Model;
}

bool FileBrowser::isSceneFile(const fs::directory_entry& entry) const {
    return getFileCategory(entry) == FileCategory::Scene;
}

bool FileBrowser::isTextureFile(const fs::directory_entry& entry) const {
    return getFileCategory(entry) == FileCategory::Texture;
}

bool FileBrowser::isOBJFile(const fs::directory_entry& entry) const {
    if (entry.is_directory()) return false;
    std::string ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".obj";
}

bool FileBrowser::matchesFilter(const fs::directory_entry& entry) const {
    if (searchFilter.empty()) return true;
    
    std::string filename = entry.path().filename().string();
    std::string filterLower = searchFilter;
    std::string filenameLower = filename;
    
    std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
    std::transform(filenameLower.begin(), filenameLower.end(), filenameLower.begin(), ::tolower);
    
    return filenameLower.find(filterLower) != std::string::npos;
}
#pragma endregion

#pragma region ImGui Theme
void applyModernTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;
    ImGuiIO& io = ImGui::GetIO();
    const float fontSizeBase = 18.0f;
    const float fontSizeOffset = -2.5f;
    const float fontSize = std::max(1.0f, fontSizeBase + fontSizeOffset);
    ImFont* editorFont = nullptr;
    fs::path primaryFontPath;
    const fs::path fontCandidates[] = {
        fs::path("Resources") / "Fonts" / "TheSunset.ttf",
        fs::path("Resources") / "Fonts" / "Thesunsethd-Regular (1).ttf",
        fs::path("TheSunset.ttf"),
        fs::path("Thesunsethd-Regular (1).ttf")
    };
    for (const auto& fontPath : fontCandidates) {
        if (!fs::exists(fontPath)) {
            continue;
        }
        const std::string fontPathStr = fontPath.string();
        editorFont = io.Fonts->AddFontFromFileTTF(fontPathStr.c_str(), fontSize);
        if (editorFont) {
            primaryFontPath = fontPath;
            io.FontDefault = editorFont;
            break;
        }
    }
    if (!editorFont) {
        std::cerr << "[WARN] Failed to load editor font (TheSunset) from Resources/Fonts."
                  << std::endl;
    } else {
        const fs::path fallbackCandidates[] = {
            fs::path("Resources") / "Fonts" / "TheSunset.ttf",
            fs::path("TheSunset.ttf")
        };
        if (primaryFontPath.filename() != "TheSunset.ttf") {
            for (const auto& fallbackPath : fallbackCandidates) {
                if (!fs::exists(fallbackPath)) {
                    continue;
                }
                const std::string fallbackPathStr = fallbackPath.string();
                ImFontConfig mergeConfig;
                mergeConfig.MergeMode = true;
                ImFont* fallbackFont = io.Fonts->AddFontFromFileTTF(
                    fallbackPathStr.c_str(),
                    fontSize,
                    &mergeConfig,
                    io.Fonts->GetGlyphRangesDefault()
                );
                if (!fallbackFont) {
                    std::cerr << "[WARN] Failed to merge fallback font: "
                              << fallbackPathStr << std::endl;
                }
                break;
            }
        }
    }

    ImVec4 slate = ImVec4(0.10f, 0.11f, 0.16f, 1.00f);
    ImVec4 panel = ImVec4(0.14f, 0.15f, 0.21f, 1.00f);
    ImVec4 overlay = ImVec4(0.09f, 0.10f, 0.14f, 0.98f);
    ImVec4 accent = ImVec4(0.36f, 0.52f, 0.74f, 1.00f);
    ImVec4 accentMuted = ImVec4(0.30f, 0.45f, 0.64f, 1.00f);
    ImVec4 highlight = ImVec4(0.20f, 0.22f, 0.30f, 1.00f);

    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.91f, 0.94f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.56f, 0.60f, 0.66f, 1.00f);

    colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.13f, 0.18f, 1.00f);
    colors[ImGuiCol_ChildBg] = panel;
    colors[ImGuiCol_PopupBg] = overlay;
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.22f, 0.30f, 0.70f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.10f, 0.14f, 1.00f);

    colors[ImGuiCol_Header] = highlight;
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.26f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.28f, 0.34f, 1.00f);

    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.30f, 0.36f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.34f, 0.40f, 1.00f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.19f, 0.26f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.26f, 0.34f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.30f, 0.36f, 1.00f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.11f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.15f, 0.21f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.09f, 0.13f, 1.00f);

    colors[ImGuiCol_Tab] = ImVec4(0.13f, 0.14f, 0.20f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.30f, 0.40f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.18f, 0.20f, 0.28f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.11f, 0.16f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.16f, 0.22f, 1.00f);

    colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.22f, 0.30f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.30f, 0.28f, 0.40f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.36f, 0.44f, 0.56f, 1.00f);

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.11f, 0.15f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.22f, 0.24f, 0.32f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.32f, 0.44f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.34f, 0.40f, 0.50f, 1.00f);

    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentMuted;

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.28f, 0.38f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.36f, 0.42f, 0.56f, 0.80f);
    colors[ImGuiCol_ResizeGripActive] = accent;

    colors[ImGuiCol_DockingPreview] = ImVec4(0.36f, 0.52f, 0.74f, 0.45f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.08f, 0.09f, 0.13f, 1.00f);

    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.36f, 0.52f, 0.74f, 0.22f);
    colors[ImGuiCol_NavHighlight] = accent;
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.18f, 0.20f, 0.28f, 1.00f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.05f, 0.06f, 0.08f, 0.70f);
    applyEditorLayoutPreset(style);
}

void applyEditorLayoutPreset(ImGuiStyle& style) {
    style.WindowPadding = ImVec2(3.0f, 3.0f);
    style.FramePadding = ImVec2(4.0f, 4.0f);
    style.ItemSpacing = ImVec2(10.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(2.0f, 2.0f);
    style.CellPadding = ImVec2(4.0f, 2.0f);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing = 11.0f;
    style.GrabMinSize = 8.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    style.WindowRounding = 12.0f;
    style.ChildRounding = 12.0f;
    style.FrameRounding = 12.0f;
    style.PopupRounding = 12.0f;
    style.GrabRounding = 12.0f;

    style.ScrollbarSize = 11.0f;
    style.ScrollbarRounding = 10.0f;
    style.ScrollbarPadding = 1.0f;

    style.TabBorderSize = 1.0f;
    style.TabBarBorderSize = 1.0f;
    style.TabBarOverlineSize = 1.0f;
    style.TabMinWidthBase = 1.0f;
    style.TabMinWidthShrink = 80.0f;
    style.TabCloseButtonMinWidthSelected = -1.0f;
    style.TabCloseButtonMinWidthUnselected = 0.0f;
    style.TabRounding = 10.0f;

    style.TableAngledHeadersAngle = 35.0f;
    style.TableAngledHeadersTextAlign = ImVec2(0.50f, 0.00f);

    style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesNone;
    style.TreeLinesSize = 1.0f;
    style.TreeLinesRounding = 0.0f;

    style.WindowTitleAlign = ImVec2(0.50f, 0.50f);
    style.WindowBorderHoverPadding = 6.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;

    style.ColorButtonPosition = ImGuiDir_Right;
    style.ButtonTextAlign = ImVec2(0.50f, 0.50f);
    style.SelectableTextAlign = ImVec2(0.00f, 0.00f);
    style.SeparatorTextBorderSize = 2.0f;
    style.SeparatorTextAlign = ImVec2(0.50f, 0.50f);
    style.SeparatorTextPadding = ImVec2(4.0f, 0.0f);
    style.LogSliderDeadzone = 4.0f;
    style.ImageBorderSize = 0.0f;

    style.DockingNodeHasCloseButton = true;
    style.DockingSeparatorSize = 0.0f;

    style.DisplayWindowPadding = ImVec2(19.0f, 19.0f);
    style.DisplaySafeAreaPadding = ImVec2(0.0f, 0.0f);
}

void applyPixelStyle(ImGuiStyle& style) {
    applyEditorLayoutPreset(style);
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;

    style.WindowPadding = ImVec2(8.0f, 6.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 14.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;
}

void applySuperRoundStyle(ImGuiStyle& style) {
    applyEditorLayoutPreset(style);
    style.WindowRounding = 18.0f;
    style.ChildRounding = 16.0f;
    style.FrameRounding = 16.0f;
    style.PopupRounding = 16.0f;
    style.ScrollbarRounding = 16.0f;
    style.GrabRounding = 14.0f;
    style.TabRounding = 16.0f;

    style.WindowPadding = ImVec2(14.0f, 10.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.IndentSpacing = 18.0f;

    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
}
#pragma endregion

#pragma region Dockspace
// Call once per frame before rendering editor panels.
void setupDockspace(const std::function<void()>& menuBarContent) {
    static bool dockspaceOpen = true;
    static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace", &dockspaceOpen, windowFlags);
    ImGui::PopStyleVar(3);

    if (ImGui::BeginMenuBar()) {
        if (menuBarContent) {
            menuBarContent();
        }
        ImGui::EndMenuBar();
    }

    ImGuiID dockspaceId = ImGui::GetID("MainDockspace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);

    ImGui::End();
}
#pragma endregion
