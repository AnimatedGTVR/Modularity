#include "EditorUI.h"

// FileBrowser implementation
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
        ext == ".dae" || ext == ".blend" || ext == ".3ds" || ext == ".ply" ||
        ext == ".stl" || ext == ".x" || ext == ".md5mesh") {
        return FileCategory::Model;
    }
    
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

void applyModernTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.14f, 0.98f);

    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);

    colors[ImGuiCol_Button] = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.36f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.26f, 0.30f, 1.00f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);

    colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);

    colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.30f, 0.30f, 0.36f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.40f, 0.48f, 1.00f);

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.30f, 0.36f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.36f, 0.36f, 0.42f, 1.00f);

    colors[ImGuiCol_CheckMark] = ImVec4(0.45f, 0.72f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.72f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.55f, 0.78f, 1.00f, 1.00f);

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.26f, 0.30f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.45f, 0.72f, 0.95f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.45f, 0.72f, 0.95f, 0.95f);

    colors[ImGuiCol_DockingPreview] = ImVec4(0.45f, 0.72f, 0.95f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);

    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.45f, 0.72f, 0.95f, 0.35f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.45f, 0.72f, 0.95f, 1.00f);

    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
}

void setupDockspace() {
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

    ImGuiID dockspaceId = ImGui::GetID("MainDockspace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);

    ImGui::End();
}
