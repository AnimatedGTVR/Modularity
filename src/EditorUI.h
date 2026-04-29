#pragma once

#include <future>
#include <unordered_set>
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
    Video,
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
    struct RefreshResult {
        fs::path path;
        std::string filter;
        bool showHiddenFiles = false;
        std::vector<fs::directory_entry> entries;
    };

    fs::path currentPath;
    fs::path selectedFile;
    std::vector<fs::path> selectedFiles;
    std::unordered_set<std::string> selectedFileKeys;
    int selectionAnchorIndex = -1;
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
    std::future<RefreshResult> refreshFuture;
    bool refreshInFlight = false;

    FileBrowser();

    // Call refresh after mutating currentPath/searchFilter/showHiddenFiles.
    void refresh();
    bool isRefreshing() const { return refreshInFlight; }
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
    bool isVideoFile(const fs::directory_entry& entry) const;
    bool matchesFilter(const fs::directory_entry& entry) const;
    
    // Legacy compatibility
    bool isOBJFile(const fs::directory_entry& entry) const;
};
#pragma endregion

#pragma region Editor UI Helpers

enum class EditorChromeScale {
    Compact = 0,
    Default = 1,
    Big = 2
};

struct EditorChromeMetrics {
    float fontScale = 1.0f;
    ImVec2 menuItemSpacing = ImVec2(9.0f, 4.0f);
    ImVec2 menuFramePadding = ImVec2(8.0f, 2.0f);
    float buttonSize = 16.0f;
    float buttonSpacing = 3.0f;
    float bottomReserveHeight = 20.0f;
    ImVec2 consoleTabSize = ImVec2(96.0f, 30.0f);
    float consoleMargin = 12.0f;
    ImVec2 consoleMiniSize = ImVec2(560.0f, 320.0f);
};

// Apply the modern dark theme to ImGui
void applyModernTheme();
ImFont* loadModularityUiFont(ImGuiIO& io, float fontSize, std::string* outReport = nullptr);
void applyEditorLayoutPreset(ImGuiStyle& style);
void applyPixelStyle(ImGuiStyle& style);
void applySuperRoundStyle(ImGuiStyle& style);

const EditorChromeMetrics& getEditorChromeMetrics(EditorChromeScale scale = EditorChromeScale::Default);
const char* getEditorChromeScaleLabel(EditorChromeScale scale);

// Setup ImGui dockspace for the editor and return its stable dockspace ID.
ImGuiID setupDockspace(EditorChromeScale chromeScale = EditorChromeScale::Default);
float getEditorBottomStatusReserveHeight(EditorChromeScale chromeScale = EditorChromeScale::Default);

// Apply touch-style swipe scrolling with inertial motion and elastic edge return.
void updateTouchSwipeScrolling();
#pragma endregion
