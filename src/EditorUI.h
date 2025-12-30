#pragma once

#include <functional>
#include "Common.h"

#pragma region File Browser Enums

enum class FileBrowserViewMode {
    List,
    Grid
};

enum class FileCategory {
    Folder,
    Scene,
    Model,
    Material,
    Texture,
    Shader,
    Script,
    Audio,
    Text,
    Unknown
};
#pragma endregion

#pragma region File Browser

class FileBrowser {
public:
    fs::path currentPath;
    fs::path selectedFile;
    fs::path projectRoot;  // Root of current project
    std::vector<fs::directory_entry> entries;
    bool needsRefresh = true;
    
    FileBrowserViewMode viewMode = FileBrowserViewMode::Grid;
    float iconSize = 64.0f;
    float padding = 8.0f;
    std::string searchFilter;
    bool showHiddenFiles = false;
    
    std::vector<fs::path> pathHistory;
    int historyIndex = -1;

    FileBrowser();

    // Call refresh after mutating currentPath/searchFilter/showHiddenFiles.
    void refresh();
    void navigateUp();
    void navigateTo(const fs::path& path);
    void navigateBack();
    void navigateForward();
    void setProjectRoot(const fs::path& root);
    
    const char* getFileIcon(const fs::directory_entry& entry) const;
    FileCategory getFileCategory(const fs::directory_entry& entry) const;
    bool isModelFile(const fs::directory_entry& entry) const;
    bool isSceneFile(const fs::directory_entry& entry) const;
    bool isTextureFile(const fs::directory_entry& entry) const;
    bool matchesFilter(const fs::directory_entry& entry) const;
    
    // Legacy compatibility
    bool isOBJFile(const fs::directory_entry& entry) const;
};
#pragma endregion

#pragma region Editor UI Helpers

// Apply the modern dark theme to ImGui
void applyModernTheme();

// Setup ImGui dockspace for the editor
void setupDockspace(const std::function<void()>& menuBarContent = nullptr);
#pragma endregion
