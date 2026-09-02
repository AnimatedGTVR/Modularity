#pragma once
#include <future>
#include <unordered_set>
#include "Common.h"

// Never call fs::path::string() on a path that came from disk. On Windows it
// converts UTF-16 through the active ANSI code page (1252 here) and THROWS
// std::system_error - "No mapping for the Unicode character exists in the target
// multi-byte code page" - the moment a filename contains anything outside
// Latin-1. An asset folder holding, say, a Japanese track name is enough to take
// the editor down from any call site that formats a path for display.
// u8string() is lossless everywhere and is already what ImGui wants to render.
// On POSIX this is the same bytes it always was, so it costs nothing there.
inline std::string PathToUtf8(const fs::path& path) {
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}
inline std::string PathToUtf8(const std::u8string& utf8) {
    return std::string(utf8.begin(), utf8.end());
}
#pragma region File Browser Enums
enum class FileBrowserViewMode {List, Grid};
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
    struct CachedEntry {
        fs::path path;
        std::string filename;
        std::string selectionKey;
        std::string metadata;
        FileCategory category = FileCategory::Unknown;
        fs::file_time_type lastWriteTime{};
        uintmax_t sizeBytes = 0;
        bool isDirectory = false;
        bool folderHasItems = false;
        bool hasSizeBytes = false;
    };
    struct RefreshResult {
        fs::path path;
        std::string filter;
        bool showHiddenFiles = false;
        std::vector<fs::directory_entry> entries;
        std::vector<CachedEntry> cachedEntries;
        // Why the listing may be short. Filled in when a directory scan hits an error
        // that used to be swallowed whole, leaving the panel silently half-populated.
        std::string scanError;
    };
    fs::path currentPath;
    fs::path selectedFile;
    std::vector<fs::path> selectedFiles;
    std::unordered_set<std::string> selectedFileKeys;
    int selectionAnchorIndex = -1;
    fs::path projectRoot;
    std::vector<fs::directory_entry> entries;
    std::vector<CachedEntry> cachedEntries;
    std::string scanError;
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
    // External-change watch. Everything that goes through the editor (import, paste,
    // create, delete, drag-drop) sets needsRefresh itself, but a file dropped in from a
    // file manager, a git checkout, or an art tool's export announces nothing - so the
    // listing is re-read on a slow timer and swapped in only when it actually differs.
    // The re-read runs on the same worker path as refresh(), so the main thread pays for
    // a hash comparison and nothing else.
    std::future<RefreshResult> externalPollFuture;
    bool externalPollInFlight = false;
    double lastExternalPollTime = -1.0;
    double externalPollInterval = 1.25;
    unsigned long long contentSignature = 0;
    bool hasContentSignature = false;
    FileBrowser();
    void refresh();
    // Call once per frame while the panel is visible, before the entries are drawn:
    // it can replace the entry vectors.
    void pollExternalChanges(double now);
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
    bool isOBJFile(const fs::directory_entry& entry) const;
};
#pragma endregion
#pragma region Editor UI Helpers
enum class EditorChromeScale {Compact = 0, Default = 1, Big = 2};
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
void applyModernTheme();
ImFont* loadModularityUiFont(ImGuiIO& io, float fontSize, std::string* outReport = nullptr);
bool mergeModularityEmojiFont(ImGuiIO& io, float fontSize, std::string* outReport = nullptr);
void applyEditorLayoutPreset(ImGuiStyle& style);
// The default editor theme: violet palette + the shaded desktop look. The two halves are
// independent on purpose, colors in the style and depth in the shade theme, so a custom palette
// can reuse the shading and vice versa. See docs/EditorTheming.md.
void applyModularityStyle(ImGuiStyle& style);
void applyModularityShadeTheme(ImGuiShadeTheme& theme);
void applyGlassStyle(ImGuiStyle& style);
void applySlateStyle(ImGuiStyle& style);
void applyPixelStyle(ImGuiStyle& style);
void applySuperRoundStyle(ImGuiStyle& style);
// ╻━━━ This is the Pop-Up menu thing for stuff like the rename/delete or the new about Pop-Up! ━━━╻
//        It uses the same open/close contract as BeginPopupModal: OpenPopup(name) elsewhere
//              v━━ so for each frame this is the usual structure of the code below ━━v
//      if (beginCardModal("Confirm Delete")) {
//          cardModalText("This item will be deleted from your project.");
//          // Put your optional custom widgets or whatever like you would with ImGui:: or ModuGUI::
//          if (cardModalButton("Cancel", CardButtonKind::Neutral, 0, 2)) ModuGUI::CloseCurrentPopup();
//          if (cardModalButton("Delete", CardButtonKind::Danger,  1, 2)) { doIt(); ModuGUI::CloseCurrentPopup(); }
//          endCardModal();
//      }
// v━━━       So, you should only call endCardModal() when beginCardModal() returns true.       ━━━v
//            (idk what i was doing with this, i just felt like making it look fancy lol)
enum class CardButtonKind { Neutral, Primary, Danger };
struct CardModalIcon {
    ImTextureID id = static_cast<ImTextureID>(0); bool flipY = false;
};
bool beginCardModal(const char* name, float width = 0.0f, bool* open = nullptr, const CardModalIcon& icon = {}); // width 0 is the default, it also scales with the font size!
void cardModalText(const char* text);
bool cardModalButton(const char* label, CardButtonKind kind, int index, int count); // equal-width pill row, call in order
void endCardModal();
const EditorChromeMetrics& getEditorChromeMetrics(EditorChromeScale scale = EditorChromeScale::Default);
const char* getEditorChromeScaleLabel(EditorChromeScale scale);
ImGuiID setupDockspace(EditorChromeScale chromeScale = EditorChromeScale::Default);
float getEditorBottomStatusReserveHeight(EditorChromeScale chromeScale = EditorChromeScale::Default);
void updateTouchSwipeScrolling();
#pragma endregion
