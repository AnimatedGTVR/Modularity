#include "EditorUI.h"
#include <chrono>
#include <cstring>
#include <sstream>
#include <unordered_map>

namespace {
constexpr float kModularityUiFontSizeBase = 18.0f;
constexpr float kModularityUiFontSizeOffset = -2.5f;

struct TouchSwipeWindowState {
    ImVec2 targetScroll = ImVec2(0.0f, 0.0f);
    ImVec2 currentScroll = ImVec2(0.0f, 0.0f);
    ImVec2 inputVelocity = ImVec2(0.0f, 0.0f);
    ImVec2 smoothVelocity = ImVec2(0.0f, 0.0f);
    bool initialized = false;
    bool touchedThisFrame = false;
    bool isDragging = false;
};

struct TouchSwipeRuntimeState {
    std::unordered_map<ImGuiID, TouchSwipeWindowState> windowStates;
    ImGuiID activeWindowId = 0;
    ImVec2 dragStartPos = ImVec2(0.0f, 0.0f);
    ImVec2 lastPointerPos = ImVec2(0.0f, 0.0f);
    bool dragging = false;
};

bool hasScrollableAxis(const ImGuiWindow* window, int axis) {
    if (!window || axis < 0 || axis > 1) {
        return false;
    }
    if ((window->Flags & ImGuiWindowFlags_NoInputs) != 0) {
        return false;
    }
    if ((window->Flags & ImGuiWindowFlags_NoScrollWithMouse) != 0) {
        return false;
    }
    return window->ScrollMax[axis] > 0.0f;
}

FileBrowser::RefreshResult BuildFileBrowserRefreshResult(const fs::path& currentPath,
                                                        const std::string& searchFilter,
                                                        bool showHiddenFiles) {
    FileBrowser::RefreshResult result;
    result.path = currentPath;
    result.filter = searchFilter;
    result.showHiddenFiles = showHiddenFiles;

    std::string filterLower = searchFilter;
    std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

    try {
        for (const auto& entry : fs::directory_iterator(currentPath)) {
            std::string filename = entry.path().filename().string();
            if (!showHiddenFiles && !filename.empty() && filename[0] == '.') {
                continue;
            }

            if (!filterLower.empty()) {
                std::string filenameLower = filename;
                std::transform(filenameLower.begin(), filenameLower.end(), filenameLower.begin(), ::tolower);
                if (filenameLower.find(filterLower) == std::string::npos) {
                    continue;
                }
            }

            result.entries.push_back(entry);
        }
        std::sort(result.entries.begin(), result.entries.end(), [](const auto& a, const auto& b) {
            if (a.is_directory() != b.is_directory()) {
                return a.is_directory() > b.is_directory();
            }
            return a.path().filename().string() < b.path().filename().string();
        });
    } catch (...) {
    }

    return result;
}

bool isTouchScrollableWindow(const ImGuiWindow* window) {
    if (!window || !window->Active || window->Collapsed || window->SkipItems) {
        return false;
    }
    return hasScrollableAxis(window, 0) || hasScrollableAxis(window, 1);
}

bool windowNameContains(const ImGuiWindow* window, const char* token) {
    return window && window->Name && token && std::strstr(window->Name, token) != nullptr;
}

bool isConsoleRelatedWindow(const ImGuiWindow* window) {
    if (!window) {
        return false;
    }
    if (windowNameContains(window, "ConsoleOutput") ||
        windowNameContains(window, "Console##MiniLogPanel")) {
        return true;
    }
    const ImGuiWindow* root = window->RootWindow ? window->RootWindow : window;
    return windowNameContains(root, "Console##MiniLogPanel");
}

bool isAnimationRelatedWindow(const ImGuiWindow* window) {
    if (!window) {
        return false;
    }
    for (const ImGuiWindow* cursor = window; cursor != nullptr; cursor = cursor->ParentWindow) {
        if (windowNameContains(cursor, "Animation") ||
            windowNameContains(cursor, "AnimationMainArea") ||
            windowNameContains(cursor, "AnimationTimelineArea") ||
            windowNameContains(cursor, "AnimBindingTreePane") ||
            windowNameContains(cursor, "AnimDopesheetPane") ||
            windowNameContains(cursor, "AnimCurvesPane")) {
            return true;
        }
    }
    const ImGuiWindow* root = window->RootWindow ? window->RootWindow : window;
    return windowNameContains(root, "Animation");
}

bool shouldBypassGlobalSmoothScroll(const ImGuiWindow* window) {
    return isConsoleRelatedWindow(window) || isAnimationRelatedWindow(window);
}

ImGuiWindow* findScrollableWindowFromHover(ImGuiWindow* hovered) {
    for (ImGuiWindow* window = hovered; window != nullptr; window = window->ParentWindow) {
        if (isTouchScrollableWindow(window)) {
            return window;
        }
    }
    return nullptr;
}

float applyEdgeResistance(float value, float minValue, float maxValue, float resistance) {
    if (value < minValue) {
        return minValue + (value - minValue) * resistance;
    }
    if (value > maxValue) {
        return maxValue + (value - maxValue) * resistance;
    }
    return value;
}

float computeElasticOverscrollLimit(float axisExtent, float scrollMax) {
    const float byViewport = axisExtent * 0.14f;
    const float byRange = scrollMax * 0.16f + 6.0f;
    return ImClamp(std::min(byViewport, byRange), 6.0f, 28.0f);
}

float smoothDampScalar(float current,
                       float target,
                       float& currentVelocity,
                       float smoothTime,
                       float maxSpeed,
                       float deltaTime) {
    smoothTime = ImMax(0.0001f, smoothTime);
    const float omega = 2.0f / smoothTime;
    const float x = omega * deltaTime;
    const float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

    float change = current - target;
    const float originalTarget = target;
    const float maxChange = maxSpeed * smoothTime;
    change = ImClamp(change, -maxChange, maxChange);
    target = current - change;

    const float temp = (currentVelocity + omega * change) * deltaTime;
    currentVelocity = (currentVelocity - omega * temp) * exp;
    float output = target + (change + temp) * exp;

    if (((originalTarget - current) > 0.0f) == (output > originalTarget)) {
        output = originalTarget;
        currentVelocity = 0.0f;
    }

    return output;
}

}

const EditorChromeMetrics& getEditorChromeMetrics(EditorChromeScale scale) {
    static const EditorChromeMetrics kMetrics[] = {
        {
            0.84f,
            ImVec2(7.0f, 3.0f),
            ImVec2(7.0f, 2.0f),
            15.0f,
            3.0f,
            18.0f,
            ImVec2(88.0f, 27.0f),
            10.0f,
            ImVec2(520.0f, 300.0f)
        },
        {
            0.90f,
            ImVec2(9.0f, 4.0f),
            ImVec2(8.0f, 2.0f),
            16.0f,
            3.0f,
            20.0f,
            ImVec2(96.0f, 30.0f),
            12.0f,
            ImVec2(560.0f, 320.0f)
        },
        {
            0.98f,
            ImVec2(11.0f, 5.0f),
            ImVec2(10.0f, 3.0f),
            18.0f,
            4.0f,
            24.0f,
            ImVec2(108.0f, 34.0f),
            14.0f,
            ImVec2(620.0f, 360.0f)
        }
    };

    const int idx = std::clamp(static_cast<int>(scale), 0, 2);
    return kMetrics[idx];
}

const char* getEditorChromeScaleLabel(EditorChromeScale scale) {
    switch (scale) {
        case EditorChromeScale::Compact: return "Compact";
        case EditorChromeScale::Big: return "Big";
        case EditorChromeScale::Default:
        default:
            return "Default";
    }
}

#pragma region File Browser
FileBrowser::FileBrowser() {
    currentPath = fs::current_path();
    projectRoot = currentPath;
}

void FileBrowser::refresh() {
    if (refreshInFlight && refreshFuture.valid()) {
        const auto status = refreshFuture.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            RefreshResult result = refreshFuture.get();
            refreshInFlight = false;
            if (result.path == currentPath &&
                result.filter == searchFilter &&
                result.showHiddenFiles == showHiddenFiles) {
                entries = std::move(result.entries);
                needsRefresh = false;
            }
        }
    }

    if (!needsRefresh || refreshInFlight) {
        return;
    }

    const fs::path pathSnapshot = currentPath;
    const std::string filterSnapshot = searchFilter;
    const bool showHiddenSnapshot = showHiddenFiles;
    refreshInFlight = true;
    refreshFuture = std::async(std::launch::async,
        [pathSnapshot, filterSnapshot, showHiddenSnapshot]() {
            return BuildFileBrowserRefreshResult(pathSnapshot, filterSnapshot, showHiddenSnapshot);
        });
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
        selectedFile.clear();
        selectedFiles.clear();
        selectedFileKeys.clear();
        selectionAnchorIndex = -1;
        needsRefresh = true;
    }
}

void FileBrowser::navigateBack() {
    if (historyIndex > 0) {
        historyIndex--;
        currentPath = pathHistory[historyIndex];
        selectedFile.clear();
        selectedFiles.clear();
        selectedFileKeys.clear();
        selectionAnchorIndex = -1;
        needsRefresh = true;
    }
}

void FileBrowser::navigateForward() {
    if (historyIndex < (int)pathHistory.size() - 1) {
        historyIndex++;
        currentPath = pathHistory[historyIndex];
        selectedFile.clear();
        selectedFiles.clear();
        selectedFileKeys.clear();
        selectionAnchorIndex = -1;
        needsRefresh = true;
    }
}

void FileBrowser::setProjectRoot(const fs::path& root) {
    projectRoot = root;
    currentPath = root;
    selectedFile.clear();
    selectedFiles.clear();
    selectedFileKeys.clear();
    selectionAnchorIndex = -1;
    pathHistory.clear();
    historyIndex = -1;
    needsRefresh = true;
}

FileCategory FileBrowser::getFileCategory(const fs::directory_entry& entry) const {
    if (entry.is_directory()) return FileCategory::Folder;
    
    std::string filename = entry.path().filename().string();
    std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);

    std::string ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Project manifests use .modu too, but they are plain text config files.
    if (filename == "project.modu" ||
        filename == "build.modu" ||
        filename == "scripts.modu" ||
        filename == "packages.modu" ||
        filename == "autostart.modu" ||
        filename == "launcher_settings.modu") {
        return FileCategory::Text;
    }
    
    // Scene files
    if (ext == ".modu" || ext == ".scene") return FileCategory::Scene;
    if (ext == ".modupak") return FileCategory::Text;
    
    // Model files
    if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb" ||
        ext == ".dae" || ext == ".blend" || ext == ".3ds" || ext == ".b3d" ||
        ext == ".ply" || ext == ".stl" || ext == ".x" || ext == ".md5mesh" ||
        ext == ".rmesh" || ext == ".mmesh") {
        return FileCategory::Model;
    }
    
    // Material files
    if (ext == ".mat") return FileCategory::Material;

    // Texture files
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || 
        ext == ".tga" || ext == ".dds" || ext == ".hdr") {
        return FileCategory::Texture;
    }

    // Video files
    if (ext == ".mp4" || ext == ".m4v" || ext == ".mov" || ext == ".avi" ||
        ext == ".mkv" || ext == ".webm" || ext == ".wmv" || ext == ".ogv") {
        return FileCategory::Video;
    }
    
    // Shader files
    if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".hlsl" ||
        ext == ".shader" || ext == ".modushader") {
        return FileCategory::Shader;
    }
    
    // Script files
    if (ext == ".cpp" || ext == ".c" || ext == ".moducpp" || ext == ".h" || ext == ".hpp" ||
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
        case FileCategory::Video:   return "video";
        case FileCategory::Shader:  return "text";
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

bool FileBrowser::isVideoFile(const fs::directory_entry& entry) const {
    return getFileCategory(entry) == FileCategory::Video;
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
ImFont* loadModularityUiFont(ImGuiIO& io, float fontSize, std::string* outReport) {
    ImFont* loadedFont = nullptr;
    fs::path primaryFontPath;
    std::ostringstream report;

    const fs::path fontCandidates[] = {
        fs::path("Resources") / "Fonts" / "TheSunset.ttf",
        fs::path("Resources") / "Fonts" / "Thesunsethd-Regular (1).ttf",
        fs::path("TheSunset.ttf"),
        fs::path("Thesunsethd-Regular (1).ttf")
    };

    std::error_code cwdEc;
    const fs::path currentWorkingDir = fs::current_path(cwdEc);
    for (const auto& fontPath : fontCandidates) {
        std::error_code fileEc;
        if (!fs::exists(fontPath, fileEc) || fileEc) {
            report << "missing '" << fontPath.string() << "'";
            if (fileEc) {
                report << " (" << fileEc.message() << ")";
            }
            report << "; ";
            continue;
        }
        if (!fs::is_regular_file(fontPath, fileEc) || fileEc) {
            report << "invalid '" << fontPath.string() << "'";
            if (fileEc) {
                report << " (" << fileEc.message() << ")";
            }
            report << "; ";
            continue;
        }

        const std::string fontPathStr = fontPath.string();
        loadedFont = io.Fonts->AddFontFromFileTTF(fontPathStr.c_str(), fontSize);
        if (loadedFont) {
            primaryFontPath = fontPath;
            break;
        }

        report << "ImGui rejected '" << fontPathStr << "'; ";
    }

    if (!loadedFont) {
        if (outReport) {
            std::ostringstream finalReport;
            finalReport << "UI font load failed. cwd='"
                        << (cwdEc ? std::string("<unavailable>") : currentWorkingDir.string())
                        << "'. Attempts: " << report.str();
            *outReport = finalReport.str();
        }
        return nullptr;
    }

    const fs::path fallbackCandidates[] = {
        fs::path("Resources") / "Fonts" / "TheSunset.ttf",
        fs::path("TheSunset.ttf")
    };
    if (primaryFontPath.filename() != "TheSunset.ttf") {
        for (const auto& fallbackPath : fallbackCandidates) {
            std::error_code fallbackEc;
            if (!fs::exists(fallbackPath, fallbackEc) || fallbackEc) {
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
            if (!fallbackFont && outReport) {
                *outReport = "Failed to merge fallback font '" + fallbackPathStr + "'.";
            }
            break;
        }
    }

    return loadedFont;
}

void applyModernTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;
    ImGuiIO& io = ImGui::GetIO();
    const float fontSize = std::max(1.0f, kModularityUiFontSizeBase + kModularityUiFontSizeOffset);
    std::string fontReport;
    ImFont* editorFont = loadModularityUiFont(io, fontSize, &fontReport);
    if (!editorFont) {
        io.Fonts->AddFontDefault();
        if (!fontReport.empty()) {
            std::cerr << "[WARN] " << fontReport << std::endl;
        }
    } else {
        io.FontDefault = editorFont;
        if (!fontReport.empty()) {
            std::cerr << "[WARN] " << fontReport << std::endl;
        }
    }

    ImVec4 slate = ImVec4(0.11f, 0.12f, 0.19f, 1.00f);
    ImVec4 panel = ImVec4(0.16f, 0.16f, 0.24f, 1.00f);
    ImVec4 overlay = ImVec4(0.10f, 0.11f, 0.17f, 0.98f);
    ImVec4 accent = ImVec4(0.48f, 0.56f, 0.86f, 1.00f);
    ImVec4 accentMuted = ImVec4(0.38f, 0.46f, 0.74f, 1.00f);
    ImVec4 highlight = ImVec4(0.22f, 0.23f, 0.34f, 1.00f);

    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.62f, 0.70f, 1.00f);

    colors[ImGuiCol_WindowBg] = slate;
    colors[ImGuiCol_ChildBg] = panel;
    colors[ImGuiCol_PopupBg] = overlay;
    colors[ImGuiCol_Border] = ImVec4(0.22f, 0.23f, 0.34f, 0.70f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.10f, 0.16f, 1.00f);

    colors[ImGuiCol_Header] = highlight;
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.28f, 0.38f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.30f, 0.42f, 1.00f);

    colors[ImGuiCol_Button] = ImVec4(0.22f, 0.23f, 0.32f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.30f, 0.42f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.33f, 0.36f, 0.48f, 1.00f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.21f, 0.30f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.28f, 0.40f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.34f, 0.46f, 1.00f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.11f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.17f, 0.24f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.09f, 0.10f, 0.15f, 1.00f);

    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.16f, 0.24f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.30f, 0.34f, 0.48f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.22f, 0.32f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.18f, 0.26f, 1.00f);

    colors[ImGuiCol_Separator] = ImVec4(0.22f, 0.23f, 0.34f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.34f, 0.36f, 0.52f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.44f, 0.50f, 0.70f, 1.00f);

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.11f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.26f, 0.36f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.35f, 0.48f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.36f, 0.42f, 0.58f, 1.00f);

    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentMuted;

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.30f, 0.42f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.38f, 0.44f, 0.60f, 0.80f);
    colors[ImGuiCol_ResizeGripActive] = accent;

    colors[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.08f, 0.09f, 0.14f, 1.00f);

    colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.24f);
    colors[ImGuiCol_NavHighlight] = accent;
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.20f, 0.22f, 0.32f, 1.00f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.05f, 0.06f, 0.09f, 0.70f);
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
ImGuiID setupDockspace(EditorChromeScale chromeScale) {
    static bool dockspaceOpen = true;
    static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking;
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
    const float reserveHeight = getEditorBottomStatusReserveHeight(chromeScale);
    ImVec2 dockspaceSize = ImGui::GetContentRegionAvail();
    dockspaceSize.y = ImMax(0.0f, dockspaceSize.y - reserveHeight);
    ImGui::DockSpace(dockspaceId, dockspaceSize, dockspaceFlags);

    ImGui::End();
    return dockspaceId;
}

float getEditorBottomStatusReserveHeight(EditorChromeScale chromeScale) {
    return getEditorChromeMetrics(chromeScale).bottomReserveHeight;
}
#pragma endregion

#pragma region Touch Swipe Scroll
void updateTouchSwipeScrolling() {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context) {
        return;
    }

    ImGuiContext& g = *context;
    ImGuiIO& io = ImGui::GetIO();
    static TouchSwipeRuntimeState runtime;

    const bool touchScreenMode = (io.ConfigFlags & ImGuiConfigFlags_IsTouchScreen) != 0;
    if (!touchScreenMode) {
        runtime.windowStates.clear();
        runtime.activeWindowId = 0;
        runtime.dragging = false;
        return;
    }

    const bool hasWheelInput = std::abs(io.MouseWheelH) > 0.0001f || std::abs(io.MouseWheel) > 0.0001f;
    ImVec2 wheel(io.MouseWheelH, io.MouseWheel);
    if (io.MouseWheelRequestAxisSwap) {
        wheel = ImVec2(wheel.y, 0.0f);
    }
    ImGuiWindow* wheelWindow = nullptr;
    if ((std::abs(wheel.x) > 0.0001f || std::abs(wheel.y) > 0.0001f) && g.MovingWindow == nullptr) {
        ImGuiWindow* baseWindow = g.WheelingWindow ? g.WheelingWindow : g.HoveredWindow;
        wheelWindow = findScrollableWindowFromHover(baseWindow);
        if (shouldBypassGlobalSmoothScroll(wheelWindow)) {
            wheelWindow = nullptr;
        }
    }

    const bool pointerActive = io.MouseDown[0] || io.MouseClicked[0] || runtime.dragging || runtime.activeWindowId != 0;
    if (!hasWheelInput && !pointerActive) {
        bool hasResidualMotion = false;
        for (const auto& [id, state] : runtime.windowStates) {
            (void)id;
            if (state.isDragging ||
                std::abs(state.inputVelocity.x) > 6.0f ||
                std::abs(state.inputVelocity.y) > 6.0f ||
                std::abs(state.smoothVelocity.x) > 6.0f ||
                std::abs(state.smoothVelocity.y) > 6.0f ||
                std::abs(state.targetScroll.x - state.currentScroll.x) > 0.08f ||
                std::abs(state.targetScroll.y - state.currentScroll.y) > 0.08f) {
                hasResidualMotion = true;
                break;
            }
        }
        if (!hasResidualMotion) {
            runtime.windowStates.clear();
            return;
        }
    }

    const float dt = ImClamp(io.DeltaTime, 1.0f / 240.0f, 1.0f / 30.0f);
    const float dragThresholdSqr = 16.0f;
    const float edgeResistance = 0.34f;
    const float wheelVelocityBlend = 0.14f;
    const float touchDragVelocityBlend = 0.22f;
    const float wheelImpulseScale = 0.24f;
    const float overscrollEdgeImpulseScale = 0.10f;
    const float freeScrollFriction = 7.2f;
    const float overscrollInputFriction = 11.0f;
    const float overscrollReturnSmoothing = 8.0f;
    const float scrollSmoothTime = 0.13f;
    const float dragSmoothTime = 0.055f;
    const float overscrollSmoothTime = 0.15f;
    const float maxSmoothSpeed = 5000.0f;
    const float settleVelocityEpsilon = 6.0f;
    const float settlePositionEpsilon = 0.04f;
    const float boundaryEpsilon = 0.10f;

    for (auto& [id, state] : runtime.windowStates) {
        state.touchedThisFrame = false;
        if (id != runtime.activeWindowId) {
            state.isDragging = false;
        }
    }

    for (ImGuiWindow* window : g.Windows) {
        if (!isTouchScrollableWindow(window)) {
            continue;
        }
        TouchSwipeWindowState& state = runtime.windowStates[window->ID];
        state.touchedThisFrame = true;
        if (!state.initialized) {
            state.initialized = true;
            state.targetScroll = window->Scroll;
            state.currentScroll = window->Scroll;
            state.inputVelocity = ImVec2(0.0f, 0.0f);
            state.smoothVelocity = ImVec2(0.0f, 0.0f);
            continue;
        }

        const bool stateIsIdle = !state.isDragging &&
                                 std::abs(state.inputVelocity.x) < settleVelocityEpsilon &&
                                 std::abs(state.inputVelocity.y) < settleVelocityEpsilon &&
                                 std::abs(state.smoothVelocity.x) < settleVelocityEpsilon &&
                                 std::abs(state.smoothVelocity.y) < settleVelocityEpsilon &&
                                 std::abs(state.targetScroll.x - state.currentScroll.x) < boundaryEpsilon &&
                                 std::abs(state.targetScroll.y - state.currentScroll.y) < boundaryEpsilon;
        if (stateIsIdle && !(wheelWindow && wheelWindow->ID == window->ID)) {
            state.targetScroll = window->Scroll;
            state.currentScroll = window->Scroll;
            state.inputVelocity = ImVec2(0.0f, 0.0f);
            state.smoothVelocity = ImVec2(0.0f, 0.0f);
        }
    }

    if (runtime.activeWindowId != 0) {
        ImGuiWindow* activeWindow = ImGui::FindWindowByID(runtime.activeWindowId);
        auto it = runtime.windowStates.find(runtime.activeWindowId);
        if (!activeWindow || !isTouchScrollableWindow(activeWindow) ||
            it == runtime.windowStates.end() || !it->second.touchedThisFrame) {
            runtime.activeWindowId = 0;
            runtime.dragging = false;
        }
    }

    if (touchScreenMode) {
        if (io.MouseClicked[0] && runtime.activeWindowId == 0 &&
            g.ActiveId == 0 && g.MovingWindow == nullptr) {
            ImGuiWindow* hovered = findScrollableWindowFromHover(g.HoveredWindow);
            if (hovered && !shouldBypassGlobalSmoothScroll(hovered)) {
                const bool clickInTitleBar = hovered->TitleBarHeight > 0.0f &&
                                             hovered->TitleBarRect().Contains(io.MouseClickedPos[0]);
                if (!clickInTitleBar) {
                    runtime.activeWindowId = hovered->ID;
                    runtime.dragStartPos = io.MouseClickedPos[0];
                    runtime.lastPointerPos = io.MousePos;
                    runtime.dragging = false;
                    TouchSwipeWindowState& state = runtime.windowStates[hovered->ID];
                    state.isDragging = false;
                    state.inputVelocity = ImVec2(0.0f, 0.0f);
                    state.smoothVelocity = ImVec2(0.0f, 0.0f);
                    state.targetScroll = hovered->Scroll;
                    state.currentScroll = hovered->Scroll;
                    state.initialized = true;
                }
            }
        }

        if (!io.MouseDown[0]) {
            if (runtime.activeWindowId != 0) {
                auto it = runtime.windowStates.find(runtime.activeWindowId);
                if (it != runtime.windowStates.end()) {
                    it->second.isDragging = false;
                }
            }
            runtime.activeWindowId = 0;
            runtime.dragging = false;
        } else if (runtime.activeWindowId != 0) {
            ImGuiWindow* activeWindow = ImGui::FindWindowByID(runtime.activeWindowId);
            auto it = runtime.windowStates.find(runtime.activeWindowId);
            if (activeWindow && it != runtime.windowStates.end()) {
                TouchSwipeWindowState& state = it->second;
                const ImVec2 totalDragDelta(
                    io.MousePos.x - runtime.dragStartPos.x,
                    io.MousePos.y - runtime.dragStartPos.y);
                if (!runtime.dragging && ImLengthSqr(totalDragDelta) >= dragThresholdSqr) {
                    runtime.dragging = true;
                    state.isDragging = true;
                }

                const ImVec2 pointerDelta(
                    io.MousePos.x - runtime.lastPointerPos.x,
                    io.MousePos.y - runtime.lastPointerPos.y);
                runtime.lastPointerPos = io.MousePos;

                if (runtime.dragging && g.ActiveId == 0 && g.MovingWindow == nullptr) {
                    for (int axis = 0; axis < 2; ++axis) {
                        if (!hasScrollableAxis(activeWindow, axis)) {
                            continue;
                        }
                        const float maxScroll = activeWindow->ScrollMax[axis];
                        const float previousValue = state.targetScroll[axis];
                        const float draggedValue = previousValue - pointerDelta[axis];
                        state.targetScroll[axis] = applyEdgeResistance(
                            draggedValue, 0.0f, maxScroll, edgeResistance);
                        const float frameVelocity = (state.targetScroll[axis] - previousValue) / dt;
                        state.inputVelocity[axis] = ImLerp(state.inputVelocity[axis], frameVelocity, touchDragVelocityBlend);
                    }
                }
            } else {
                runtime.activeWindowId = 0;
                runtime.dragging = false;
            }
        }
    } else {
        if (runtime.activeWindowId != 0) {
            auto it = runtime.windowStates.find(runtime.activeWindowId);
            if (it != runtime.windowStates.end()) {
                it->second.isDragging = false;
            }
        }
        runtime.activeWindowId = 0;
        runtime.dragging = false;
    }

    for (auto& [windowId, state] : runtime.windowStates) {
        if (!state.touchedThisFrame) {
            continue;
        }

        ImGuiWindow* window = ImGui::FindWindowByID(windowId);
        if (!window) {
            continue;
        }

        if (shouldBypassGlobalSmoothScroll(window)) {
            state.targetScroll = window->Scroll;
            state.currentScroll = window->Scroll;
            state.inputVelocity = ImVec2(0.0f, 0.0f);
            state.smoothVelocity = ImVec2(0.0f, 0.0f);
            continue;
        }

        const bool draggingThisWindow = runtime.dragging &&
                                        runtime.activeWindowId == windowId &&
                                        io.MouseDown[0] &&
                                        state.isDragging;

        for (int axis = 0; axis < 2; ++axis) {
            if (!hasScrollableAxis(window, axis)) {
                state.targetScroll[axis] = 0.0f;
                state.currentScroll[axis] = 0.0f;
                state.inputVelocity[axis] = 0.0f;
                state.smoothVelocity[axis] = 0.0f;
                continue;
            }

            const float maxScroll = window->ScrollMax[axis];
            const float wheelDelta = (axis == 0) ? wheel.x : wheel.y;
            const float externalDelta = window->Scroll[axis] - state.currentScroll[axis];
            const bool wheelTargetsThisWindow = !touchScreenMode &&
                                                wheelWindow &&
                                                wheelWindow->ID == windowId;
            const bool wheelAxisInput = wheelTargetsThisWindow &&
                                        (std::abs(wheelDelta) > 0.0001f ||
                                         std::abs(externalDelta) > 0.001f);
            const bool hasAxisInput = draggingThisWindow || wheelAxisInput;

            if (!draggingThisWindow) {
                const bool outsideBounds = state.targetScroll[axis] < -0.01f ||
                                           state.targetScroll[axis] > maxScroll + 0.01f;
                if (!touchScreenMode &&
                    !wheelAxisInput &&
                    std::abs(state.inputVelocity[axis]) < settleVelocityEpsilon &&
                    std::abs(state.smoothVelocity[axis]) < settleVelocityEpsilon &&
                    std::abs(state.targetScroll[axis] - state.currentScroll[axis]) < boundaryEpsilon &&
                    !outsideBounds) {
                    state.targetScroll[axis] = window->Scroll[axis];
                    state.currentScroll[axis] = window->Scroll[axis];
                    state.inputVelocity[axis] = 0.0f;
                    state.smoothVelocity[axis] = 0.0f;
                    continue;
                }

                if (wheelAxisInput) {
                    // ImGui has already moved scroll this frame; absorb that jump into
                    // our target and smooth only the visible position.
                    state.targetScroll[axis] += externalDelta;
                    const float impulseVelocity = ImClamp(externalDelta / dt, -2200.0f, 2200.0f) * wheelImpulseScale;
                    state.inputVelocity[axis] = ImLerp(state.inputVelocity[axis], impulseVelocity, wheelVelocityBlend);

                    const bool pushingMin = wheelDelta > 0.0f;
                    const bool pushingMax = wheelDelta < 0.0f;
                    const bool atMin = state.targetScroll[axis] <= 0.5f;
                    const bool atMax = state.targetScroll[axis] >= maxScroll - 0.5f;
                    if ((pushingMin && atMin) || (pushingMax && atMax)) {
                        const float maxStep = (axis == 0)
                            ? (window->InnerRect.GetWidth() * 0.67f)
                            : (window->InnerRect.GetHeight() * 0.67f);
                        const float baseStep = (axis == 0)
                            ? (1.0f * window->FontRefSize)
                            : (2.2f * window->FontRefSize);
                        const float scrollStep = ImTrunc(ImMin(baseStep, maxStep));
                        const float delta = -wheelDelta * scrollStep;

                        const float axisExtent = (axis == 0)
                            ? window->InnerRect.GetWidth()
                            : window->InnerRect.GetHeight();
                        const float overscrollLimit = computeElasticOverscrollLimit(axisExtent, maxScroll);
                        const float clampedValue = ImClamp(state.targetScroll[axis], 0.0f, maxScroll);
                        const float overshoot = std::abs(state.targetScroll[axis] - clampedValue);
                        const float remainingFactor = ImClamp(
                            1.0f - (overshoot / std::max(overscrollLimit, 0.001f)),
                            0.10f,
                            1.0f);
                        const float prev = state.targetScroll[axis];
                        state.targetScroll[axis] = applyEdgeResistance(
                            prev + delta * 0.10f * remainingFactor,
                            0.0f,
                            maxScroll,
                            0.45f);

                        const float edgeImpulse = ImClamp((delta * remainingFactor) / dt, -1000.0f, 1000.0f) * overscrollEdgeImpulseScale;
                        state.inputVelocity[axis] = ImLerp(state.inputVelocity[axis], edgeImpulse, wheelVelocityBlend);
                    }
                } else if (!touchScreenMode) {
                    // External movement (scrollbar drag, programmatic jump): follow target, no extra inertia.
                    const float syncThreshold = ImClamp(maxScroll * 0.012f, 0.01f, 0.20f);
                    if (maxScroll <= 1.0f || std::abs(externalDelta) > syncThreshold) {
                        state.targetScroll[axis] = window->Scroll[axis];
                        state.inputVelocity[axis] = 0.0f;
                    }
                }

                state.targetScroll[axis] += state.inputVelocity[axis] * dt;
                const float clampedTarget = ImClamp(state.targetScroll[axis], 0.0f, maxScroll);
                const float stretch = state.targetScroll[axis] - clampedTarget;
                if (std::abs(stretch) > 0.0f) {
                    state.inputVelocity[axis] *= std::exp(-overscrollInputFriction * dt);
                } else {
                    state.inputVelocity[axis] *= std::exp(-freeScrollFriction * dt);
                }
                if (std::abs(state.inputVelocity[axis]) < settleVelocityEpsilon) {
                    state.inputVelocity[axis] = 0.0f;
                }

                if (!hasAxisInput) {
                    const float returnBlend = 1.0f - std::exp(-overscrollReturnSmoothing * dt);
                    state.targetScroll[axis] = ImLerp(state.targetScroll[axis], clampedTarget, returnBlend);
                    if (std::abs(state.targetScroll[axis] - clampedTarget) < boundaryEpsilon) {
                        state.targetScroll[axis] = clampedTarget;
                    }
                }
            }

            const float axisExtent = (axis == 0) ? window->InnerRect.GetWidth() : window->InnerRect.GetHeight();
            const float overscrollLimit = computeElasticOverscrollLimit(axisExtent, maxScroll);
            state.targetScroll[axis] = ImClamp(
                state.targetScroll[axis],
                -overscrollLimit,
                maxScroll + overscrollLimit);

            const bool targetOutsideBounds = state.targetScroll[axis] < 0.0f || state.targetScroll[axis] > maxScroll;
            const float smoothTime = draggingThisWindow
                ? dragSmoothTime
                : (targetOutsideBounds ? overscrollSmoothTime : scrollSmoothTime);

            state.currentScroll[axis] = smoothDampScalar(
                state.currentScroll[axis],
                state.targetScroll[axis],
                state.smoothVelocity[axis],
                smoothTime,
                maxSmoothSpeed,
                dt);

            state.currentScroll[axis] = ImClamp(
                state.currentScroll[axis],
                -overscrollLimit,
                maxScroll + overscrollLimit);

            const float clampedCurrent = ImClamp(state.currentScroll[axis], 0.0f, maxScroll);
            const float clampedTarget = ImClamp(state.targetScroll[axis], 0.0f, maxScroll);
            if (!hasAxisInput &&
                std::abs(state.currentScroll[axis] - clampedCurrent) < boundaryEpsilon &&
                std::abs(state.targetScroll[axis] - clampedTarget) < boundaryEpsilon) {
                state.currentScroll[axis] = clampedCurrent;
                state.targetScroll[axis] = clampedTarget;
                if ((clampedCurrent <= 0.0f && state.smoothVelocity[axis] < 0.0f) ||
                    (clampedCurrent >= maxScroll && state.smoothVelocity[axis] > 0.0f)) {
                    state.smoothVelocity[axis] = 0.0f;
                }
            }

            if (!hasAxisInput &&
                std::abs(state.currentScroll[axis] - state.targetScroll[axis]) < settlePositionEpsilon &&
                std::abs(state.inputVelocity[axis]) < settleVelocityEpsilon &&
                std::abs(state.smoothVelocity[axis]) < settleVelocityEpsilon) {
                state.currentScroll[axis] = state.targetScroll[axis];
                state.inputVelocity[axis] = 0.0f;
                state.smoothVelocity[axis] = 0.0f;
            }

            if (axis == 0) {
                ImGui::SetScrollX(window, state.currentScroll[axis]);
            } else {
                ImGui::SetScrollY(window, state.currentScroll[axis]);
            }
        }
    }

    for (auto it = runtime.windowStates.begin(); it != runtime.windowStates.end();) {
        if (!it->second.touchedThisFrame && it->first != runtime.activeWindowId) {
            it = runtime.windowStates.erase(it);
        } else {
            ++it;
        }
    }
}
#pragma endregion
