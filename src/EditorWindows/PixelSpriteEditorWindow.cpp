#include "Engine.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/glfw/deps/stb_image_write.h"
#include "../../include/ThirdParty/stb_image.h"
#include "../SpritesheetFormat.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <queue>
#include <sstream>

namespace {
struct PixelRgba {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    unsigned char a = 255;
};

PixelRgba ToRgba8(const glm::vec4& color) {
    return PixelRgba{
        static_cast<unsigned char>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f + 0.5f),
        static_cast<unsigned char>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f + 0.5f),
        static_cast<unsigned char>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f + 0.5f),
        static_cast<unsigned char>(std::clamp(color.a, 0.0f, 1.0f) * 255.0f + 0.5f)
    };
}

bool Matches(const std::vector<unsigned char>& pixels, int width, int height, int x, int y, const PixelRgba& color) {
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    const size_t idx = static_cast<size_t>((y * width + x) * 4);
    return pixels[idx + 0] == color.r &&
           pixels[idx + 1] == color.g &&
           pixels[idx + 2] == color.b &&
           pixels[idx + 3] == color.a;
}

void SetPixel(std::vector<unsigned char>& pixels, int width, int height, int x, int y, const PixelRgba& color) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    const size_t idx = static_cast<size_t>((y * width + x) * 4);
    pixels[idx + 0] = color.r;
    pixels[idx + 1] = color.g;
    pixels[idx + 2] = color.b;
    pixels[idx + 3] = color.a;
}

glm::ivec4 NormalizeRect(glm::ivec2 a, glm::ivec2 b) {
    const int minX = std::min(a.x, b.x);
    const int minY = std::min(a.y, b.y);
    const int maxX = std::max(a.x, b.x);
    const int maxY = std::max(a.y, b.y);
    return glm::ivec4(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

glm::ivec4 ClampSpriteClipRect(glm::ivec4 rect, int width, int height) {
    rect.z = std::max(1, rect.z);
    rect.w = std::max(1, rect.w);
    rect.x = std::clamp(rect.x, 0, std::max(0, width - 1));
    rect.y = std::clamp(rect.y, 0, std::max(0, height - 1));
    rect.z = std::min(rect.z, std::max(1, width - rect.x));
    rect.w = std::min(rect.w, std::max(1, height - rect.y));
    return rect;
}

void FloodFill(std::vector<unsigned char>& pixels, int width, int height, int startX, int startY, const PixelRgba& target, const PixelRgba& replacement) {
    if (target.r == replacement.r && target.g == replacement.g &&
        target.b == replacement.b && target.a == replacement.a) {
        return;
    }
    if (!Matches(pixels, width, height, startX, startY, target)) {
        return;
    }

    std::queue<glm::ivec2> pending;
    pending.push(glm::ivec2(startX, startY));
    while (!pending.empty()) {
        glm::ivec2 p = pending.front();
        pending.pop();
        if (!Matches(pixels, width, height, p.x, p.y, target)) {
            continue;
        }
        SetPixel(pixels, width, height, p.x, p.y, replacement);
        pending.push(glm::ivec2(p.x + 1, p.y));
        pending.push(glm::ivec2(p.x - 1, p.y));
        pending.push(glm::ivec2(p.x, p.y + 1));
        pending.push(glm::ivec2(p.x, p.y - 1));
    }
}

void EnsureSpriteClipNames(std::vector<std::string>& names, size_t count) {
    if (names.size() < count) {
        for (size_t i = names.size(); i < count; ++i) {
            names.push_back("Rect_" + std::to_string(i));
        }
    } else if (names.size() > count) {
        names.resize(count);
    }
}

void EnsureSpriteClipScales(std::vector<glm::vec2>& scales, size_t count) {
    if (scales.size() < count) {
        scales.resize(count, glm::vec2(1.0f));
    } else if (scales.size() > count) {
        scales.resize(count);
    }
    for (glm::vec2& scale : scales) {
        scale.x = std::max(0.01f, scale.x);
        scale.y = std::max(0.01f, scale.y);
    }
}

void EnsureSpriteLayers(std::vector<SpritesheetLayer>& layers) {
    if (layers.empty()) {
        layers.push_back({"Layer_0"});
    }
    for (size_t i = 0; i < layers.size(); ++i) {
        if (layers[i].name.empty()) {
            layers[i].name = "Layer_" + std::to_string(i);
        }
    }
}

ImU32 CheckerColor(bool darkTheme, bool oddCell) {
    if (darkTheme) {
        return oddCell ? IM_COL32(88, 88, 88, 255) : IM_COL32(58, 58, 58, 255);
    }
    return oddCell ? IM_COL32(214, 214, 214, 255) : IM_COL32(244, 244, 244, 255);
}

}

bool Engine::loadPixelSpriteDocument(const fs::path& imagePath) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* data = stbi_load(imagePath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!data || width <= 0 || height <= 0) {
        if (data) stbi_image_free(data);
        addConsoleMessage("Failed to open sprite image: " + imagePath.string(), ConsoleMessageType::Error);
        return false;
    }

    pixelSpriteDocument = PixelSpriteDocument{};
    pixelSpriteDocument.imagePath = imagePath;
    pixelSpriteDocument.sidecarPath = imagePath;
    pixelSpriteDocument.sidecarPath += ".spritesheet";
    pixelSpriteDocument.name = imagePath.filename().string();
    pixelSpriteDocument.width = width;
    pixelSpriteDocument.height = height;
    pixelSpriteDocument.pixels.assign(data, data + static_cast<size_t>(width * height * 4));
    pixelSpriteDocument.loaded = true;
    stbi_image_free(data);

    if (fs::exists(pixelSpriteDocument.sidecarPath)) {
        std::ifstream sidecar(pixelSpriteDocument.sidecarPath);
        std::ostringstream buffer;
        buffer << sidecar.rdbuf();
        const SpritesheetParseResult parsed = ParseSpritesheet(buffer.str());
        pixelSpriteDocument.expectedMinimumModuEngineVersionOrHigher =
            parsed.document.expectedMinimumModuEngineVersionOrHigher;
        pixelSpriteDocument.strictValidation = parsed.document.strictValidation;
        pixelSpriteDocument.spriteFrames = parsed.document.rects;
        pixelSpriteDocument.spriteFrameNames = parsed.document.names;
        pixelSpriteDocument.spriteFrameScales = parsed.document.scales;
        pixelSpriteDocument.layers = parsed.document.layers;
        for (const SpritesheetParseMessage& message : parsed.messages) {
            addConsoleMessage(message.text, ConsoleMessageType::Warning);
        }
    }
    EnsureSpriteClipNames(pixelSpriteDocument.spriteFrameNames, pixelSpriteDocument.spriteFrames.size());
    EnsureSpriteClipScales(pixelSpriteDocument.spriteFrameScales, pixelSpriteDocument.spriteFrames.size());
    EnsureSpriteLayers(pixelSpriteDocument.layers);

    pixelSpriteUndoStack.clear();
    pixelSpriteRedoStack.clear();
    PixelSpriteHistoryState initialState;
    initialState.width = pixelSpriteDocument.width;
    initialState.height = pixelSpriteDocument.height;
    initialState.pixels = pixelSpriteDocument.pixels;
    initialState.selectionActive = pixelSpriteDocument.selectionActive;
    initialState.selectionStart = pixelSpriteDocument.selectionStart;
    initialState.selectionEnd = pixelSpriteDocument.selectionEnd;
    initialState.expectedMinimumModuEngineVersionOrHigher = pixelSpriteDocument.expectedMinimumModuEngineVersionOrHigher;
    initialState.strictValidation = pixelSpriteDocument.strictValidation;
    initialState.spriteFrames = pixelSpriteDocument.spriteFrames;
    initialState.spriteFrameNames = pixelSpriteDocument.spriteFrameNames;
    initialState.spriteFrameScales = pixelSpriteDocument.spriteFrameScales;
    initialState.layers = pixelSpriteDocument.layers;
    initialState.activeLayer = pixelSpriteDocument.activeLayer;
    initialState.activeFrame = pixelSpriteDocument.activeFrame;
    pixelSpriteUndoStack.push_back(std::move(initialState));
    showPixelSpriteEditorWindow = true;
    pixelSpriteCanvasPan = ImVec2(0.0f, 0.0f);
    pixelSpriteCanvasTargetPan = ImVec2(0.0f, 0.0f);
    pixelSpriteCanvasStateInitialized = false;
    pixelSpriteCanvasCenterPending = true;
    pixelSpriteZoom = 12.0f;
    pixelSpriteTargetZoom = 12.0f;
    saveEditorUserSettings();
    return true;
}

bool Engine::savePixelSpriteDocument() {
    if (!pixelSpriteDocument.loaded || pixelSpriteDocument.imagePath.empty()) {
        return false;
    }

    fs::create_directories(pixelSpriteDocument.imagePath.parent_path());
    if (!stbi_write_png(pixelSpriteDocument.imagePath.string().c_str(),
                        pixelSpriteDocument.width,
                        pixelSpriteDocument.height,
                        4,
                        pixelSpriteDocument.pixels.data(),
                        pixelSpriteDocument.width * 4)) {
        addConsoleMessage("Failed to save sprite image: " + pixelSpriteDocument.imagePath.string(), ConsoleMessageType::Error);
        return false;
    }

    std::ofstream sidecar(pixelSpriteDocument.sidecarPath);
    if (sidecar.is_open()) {
        EnsureSpriteClipNames(pixelSpriteDocument.spriteFrameNames, pixelSpriteDocument.spriteFrames.size());
        EnsureSpriteClipScales(pixelSpriteDocument.spriteFrameScales, pixelSpriteDocument.spriteFrames.size());
        EnsureSpriteLayers(pixelSpriteDocument.layers);
        SpritesheetDocument sidecarDocument;
        sidecarDocument.linkedSpriteName = pixelSpriteDocument.imagePath.lexically_relative(projectManager.currentProject.projectPath).generic_string();
        if (sidecarDocument.linkedSpriteName.empty() || sidecarDocument.linkedSpriteName == ".") {
            sidecarDocument.linkedSpriteName = pixelSpriteDocument.imagePath.generic_string();
        }
        sidecarDocument.spriteVersion = 1;
        sidecarDocument.expectedMinimumModuEngineVersionOrHigher =
            pixelSpriteDocument.expectedMinimumModuEngineVersionOrHigher.empty() ? "ModuEngine V6.5" : pixelSpriteDocument.expectedMinimumModuEngineVersionOrHigher;
        sidecarDocument.expectLayers = std::max(1, static_cast<int>(pixelSpriteDocument.layers.size()));
        sidecarDocument.expectRects = static_cast<int>(pixelSpriteDocument.spriteFrames.size());
        sidecarDocument.strictValidation = pixelSpriteDocument.strictValidation;
        sidecarDocument.rects = pixelSpriteDocument.spriteFrames;
        sidecarDocument.names = pixelSpriteDocument.spriteFrameNames;
        sidecarDocument.scales = pixelSpriteDocument.spriteFrameScales;
        sidecarDocument.layers = pixelSpriteDocument.layers;
        sidecar << WriteSpritesheet(sidecarDocument);
    }

    renderer.invalidateTexture(pixelSpriteDocument.imagePath.string());
    if (vulkanRenderer) {
        vulkanRenderer->invalidateImagePath(pixelSpriteDocument.imagePath.string());
    }
    pixelSpriteDocument.dirty = false;
    projectManager.currentProject.hasUnsavedChanges = true;
    addConsoleMessage("Saved sprite image: " + pixelSpriteDocument.imagePath.string(), ConsoleMessageType::Success);
    return true;
}

void Engine::renderPixelSpriteEditorWindow() {
    if (!showPixelSpriteEditorWindow) return;

    if (!pixelSpriteDocument.loaded) {
        pixelSpriteDocument = PixelSpriteDocument{};
        pixelSpriteDocument.pixels.assign(static_cast<size_t>(16 * 16 * 4), 0);
        pixelSpriteDocument.loaded = true;
        EnsureSpriteLayers(pixelSpriteDocument.layers);
        pixelSpriteUndoStack.clear();
        pixelSpriteRedoStack.clear();
        PixelSpriteHistoryState initialState;
        initialState.width = pixelSpriteDocument.width;
        initialState.height = pixelSpriteDocument.height;
        initialState.pixels = pixelSpriteDocument.pixels;
        initialState.selectionActive = pixelSpriteDocument.selectionActive;
        initialState.selectionStart = pixelSpriteDocument.selectionStart;
        initialState.selectionEnd = pixelSpriteDocument.selectionEnd;
        initialState.expectedMinimumModuEngineVersionOrHigher = pixelSpriteDocument.expectedMinimumModuEngineVersionOrHigher;
        initialState.strictValidation = pixelSpriteDocument.strictValidation;
        initialState.spriteFrames = pixelSpriteDocument.spriteFrames;
        initialState.spriteFrameNames = pixelSpriteDocument.spriteFrameNames;
        initialState.spriteFrameScales = pixelSpriteDocument.spriteFrameScales;
        initialState.layers = pixelSpriteDocument.layers;
        initialState.activeLayer = pixelSpriteDocument.activeLayer;
        initialState.activeFrame = pixelSpriteDocument.activeFrame;
        pixelSpriteUndoStack.push_back(std::move(initialState));
    }
    EnsureSpriteLayers(pixelSpriteDocument.layers);
    EnsureSpriteClipScales(pixelSpriteDocument.spriteFrameScales, pixelSpriteDocument.spriteFrames.size());
    pixelSpriteDocument.activeLayer = std::clamp(pixelSpriteDocument.activeLayer, 0, std::max(0, static_cast<int>(pixelSpriteDocument.layers.size()) - 1));

    if (!ImGui::Begin("Pixel Sprite Editor", &showPixelSpriteEditorWindow, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    auto ensureProjectAssetPath = [&]() {
        if (!pixelSpriteDocument.imagePath.empty() || !projectManager.currentProject.isLoaded) {
            return;
        }
        fs::path base = projectManager.currentProject.projectPath / "Assets" / "Sprites";
        fs::create_directories(base);
        pixelSpriteDocument.imagePath = base / "sprite.png";
        pixelSpriteDocument.sidecarPath = pixelSpriteDocument.imagePath;
        pixelSpriteDocument.sidecarPath += ".spritesheet";
    };

    auto pushHistory = [&]() {
        PixelSpriteHistoryState state;
        state.width = pixelSpriteDocument.width;
        state.height = pixelSpriteDocument.height;
        state.pixels = pixelSpriteDocument.pixels;
        state.selectionActive = pixelSpriteDocument.selectionActive;
        state.selectionStart = pixelSpriteDocument.selectionStart;
        state.selectionEnd = pixelSpriteDocument.selectionEnd;
        state.expectedMinimumModuEngineVersionOrHigher = pixelSpriteDocument.expectedMinimumModuEngineVersionOrHigher;
        state.strictValidation = pixelSpriteDocument.strictValidation;
        state.spriteFrames = pixelSpriteDocument.spriteFrames;
        state.spriteFrameNames = pixelSpriteDocument.spriteFrameNames;
        state.spriteFrameScales = pixelSpriteDocument.spriteFrameScales;
        state.layers = pixelSpriteDocument.layers;
        state.activeLayer = pixelSpriteDocument.activeLayer;
        state.activeFrame = pixelSpriteDocument.activeFrame;
        pixelSpriteUndoStack.push_back(std::move(state));
        if (pixelSpriteUndoStack.size() > 128) {
            pixelSpriteUndoStack.erase(pixelSpriteUndoStack.begin());
        }
        pixelSpriteRedoStack.clear();
    };

    auto commitHistoryTop = [&]() {
        if (!pixelSpriteUndoStack.empty()) {
            pixelSpriteUndoStack.back().width = pixelSpriteDocument.width;
            pixelSpriteUndoStack.back().height = pixelSpriteDocument.height;
            pixelSpriteUndoStack.back().pixels = pixelSpriteDocument.pixels;
            pixelSpriteUndoStack.back().selectionActive = pixelSpriteDocument.selectionActive;
            pixelSpriteUndoStack.back().selectionStart = pixelSpriteDocument.selectionStart;
            pixelSpriteUndoStack.back().selectionEnd = pixelSpriteDocument.selectionEnd;
            pixelSpriteUndoStack.back().expectedMinimumModuEngineVersionOrHigher = pixelSpriteDocument.expectedMinimumModuEngineVersionOrHigher;
            pixelSpriteUndoStack.back().strictValidation = pixelSpriteDocument.strictValidation;
            pixelSpriteUndoStack.back().spriteFrames = pixelSpriteDocument.spriteFrames;
            pixelSpriteUndoStack.back().spriteFrameNames = pixelSpriteDocument.spriteFrameNames;
            pixelSpriteUndoStack.back().spriteFrameScales = pixelSpriteDocument.spriteFrameScales;
            pixelSpriteUndoStack.back().layers = pixelSpriteDocument.layers;
            pixelSpriteUndoStack.back().activeLayer = pixelSpriteDocument.activeLayer;
            pixelSpriteUndoStack.back().activeFrame = pixelSpriteDocument.activeFrame;
        }
    };

    auto undoHistory = [&]() {
        if (pixelSpriteUndoStack.size() <= 1) return;
        pixelSpriteRedoStack.push_back(pixelSpriteUndoStack.back());
        pixelSpriteUndoStack.pop_back();
        const PixelSpriteHistoryState& state = pixelSpriteUndoStack.back();
        pixelSpriteDocument.width = state.width;
        pixelSpriteDocument.height = state.height;
        pixelSpriteDocument.pixels = state.pixels;
        pixelSpriteDocument.selectionActive = state.selectionActive;
        pixelSpriteDocument.selectionStart = state.selectionStart;
        pixelSpriteDocument.selectionEnd = state.selectionEnd;
        pixelSpriteDocument.expectedMinimumModuEngineVersionOrHigher = state.expectedMinimumModuEngineVersionOrHigher;
        pixelSpriteDocument.strictValidation = state.strictValidation;
        pixelSpriteDocument.spriteFrames = state.spriteFrames;
        pixelSpriteDocument.spriteFrameNames = state.spriteFrameNames;
        pixelSpriteDocument.spriteFrameScales = state.spriteFrameScales;
        pixelSpriteDocument.layers = state.layers;
        pixelSpriteDocument.activeLayer = std::clamp(state.activeLayer, 0, std::max(0, static_cast<int>(state.layers.size()) - 1));
        pixelSpriteDocument.activeFrame = std::clamp(state.activeFrame, 0, std::max(0, static_cast<int>(state.spriteFrames.size()) - 1));
        pixelSpriteDocument.dirty = true;
    };

    auto redoHistory = [&]() {
        if (pixelSpriteRedoStack.empty()) return;
        pixelSpriteUndoStack.push_back(pixelSpriteRedoStack.back());
        pixelSpriteRedoStack.pop_back();
        const PixelSpriteHistoryState& state = pixelSpriteUndoStack.back();
        pixelSpriteDocument.width = state.width;
        pixelSpriteDocument.height = state.height;
        pixelSpriteDocument.pixels = state.pixels;
        pixelSpriteDocument.selectionActive = state.selectionActive;
        pixelSpriteDocument.selectionStart = state.selectionStart;
        pixelSpriteDocument.selectionEnd = state.selectionEnd;
        pixelSpriteDocument.expectedMinimumModuEngineVersionOrHigher = state.expectedMinimumModuEngineVersionOrHigher;
        pixelSpriteDocument.strictValidation = state.strictValidation;
        pixelSpriteDocument.spriteFrames = state.spriteFrames;
        pixelSpriteDocument.spriteFrameNames = state.spriteFrameNames;
        pixelSpriteDocument.spriteFrameScales = state.spriteFrameScales;
        pixelSpriteDocument.layers = state.layers;
        pixelSpriteDocument.activeLayer = std::clamp(state.activeLayer, 0, std::max(0, static_cast<int>(state.layers.size()) - 1));
        pixelSpriteDocument.activeFrame = std::clamp(state.activeFrame, 0, std::max(0, static_cast<int>(state.spriteFrames.size()) - 1));
        pixelSpriteDocument.dirty = true;
    };

    auto applyDocToSelectedSprite = [&]() {
        SceneObject* selected = getSelectedObject();
        if (!selected || !selected->hasUI ||
            (selected->ui.type != UIElementType::Sprite2D && selected->ui.type != UIElementType::Image)) {
            addConsoleMessage("Select a Sprite2D, Sprite 2.5D, or UI Image object to apply sprite clips.", ConsoleMessageType::Warning);
            return;
        }
        ensureProjectAssetPath();
        if (pixelSpriteDocument.imagePath.empty()) {
            return;
        }
        if (pixelSpriteDocument.dirty) {
            savePixelSpriteDocument();
        }
        selected->albedoTexturePath = pixelSpriteDocument.imagePath.string();
        selected->ui.spriteSheetEnabled = true;
        selected->ui.spriteCustomFramesEnabled = !pixelSpriteDocument.spriteFrames.empty();
        selected->ui.spriteSourceWidth = pixelSpriteDocument.width;
        selected->ui.spriteSourceHeight = pixelSpriteDocument.height;
        selected->ui.spriteCustomFrames = pixelSpriteDocument.spriteFrames;
        selected->ui.spriteCustomFrameNames = pixelSpriteDocument.spriteFrameNames;
        selected->ui.spriteCustomFrameScales = pixelSpriteDocument.spriteFrameScales;
        if (!pixelSpriteDocument.spriteFrames.empty()) {
            selected->ui.size.x = static_cast<float>(pixelSpriteDocument.spriteFrames[0].z);
            selected->ui.size.y = static_cast<float>(pixelSpriteDocument.spriteFrames[0].w);
        } else {
            selected->ui.size.x = static_cast<float>(pixelSpriteDocument.width);
            selected->ui.size.y = static_cast<float>(pixelSpriteDocument.height);
        }
        projectManager.currentProject.hasUnsavedChanges = true;
        addConsoleMessage("Applied sprite clips to: " + selected->name, ConsoleMessageType::Success);
    };

    const bool editorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (editorFocused && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        undoHistory();
    }
    if (editorFocused && ((ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) ||
        (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))) {
        redoHistory();
    }

    char pathBuf[512];
    std::snprintf(pathBuf, sizeof(pathBuf), "%s", pixelSpriteDocument.imagePath.string().c_str());

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.10f, 0.17f, 0.96f));
    if (ImGui::BeginChild("PixelSpriteToolbar", ImVec2(0.0f, 156.0f), true)) {
        ImGui::Columns(2, "PixelSpriteToolbarColumns", false);
        ImGui::SetColumnWidth(0, std::max(420.0f, ImGui::GetWindowWidth() * 0.58f));

        ImGui::TextDisabled("Workspace");
        const char* modeLabels[] = { "Edit Mode", "Spritesheet Mode" };
        int modeIndex = static_cast<int>(pixelSpriteEditorMode);
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::Combo("##PixelMode", &modeIndex, modeLabels, IM_ARRAYSIZE(modeLabels))) {
            pixelSpriteEditorMode = static_cast<PixelSpriteEditorMode>(modeIndex);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            ensureProjectAssetPath();
            savePixelSpriteDocument();
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply To Selected")) {
            applyDocToSelectedSprite();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(pixelSpriteUndoStack.size() <= 1);
        if (ImGui::Button("Undo")) {
            undoHistory();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(pixelSpriteRedoStack.empty());
        if (ImGui::Button("Redo")) {
            redoHistory();
        }
        ImGui::EndDisabled();

        ImGui::TextDisabled("Document");
        if (ImGui::InputText("Image Path", pathBuf, sizeof(pathBuf))) {
            pixelSpriteDocument.imagePath = fs::path(pathBuf);
            pixelSpriteDocument.sidecarPath = pixelSpriteDocument.imagePath;
            pixelSpriteDocument.sidecarPath += ".spritesheet";
            pixelSpriteDocument.name = pixelSpriteDocument.imagePath.filename().string();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load") && fs::exists(pixelSpriteDocument.imagePath)) {
            loadPixelSpriteDocument(pixelSpriteDocument.imagePath);
        }

        int dims[2] = { pixelSpriteDocument.width, pixelSpriteDocument.height };
        if (ImGui::InputInt2("Canvas Size", dims)) {
            dims[0] = std::clamp(dims[0], 1, 1024);
            dims[1] = std::clamp(dims[1], 1, 1024);
            if (dims[0] != pixelSpriteDocument.width || dims[1] != pixelSpriteDocument.height) {
                pushHistory();
                std::vector<unsigned char> resized(static_cast<size_t>(dims[0] * dims[1] * 4), 0);
                const int copyW = std::min(dims[0], pixelSpriteDocument.width);
                const int copyH = std::min(dims[1], pixelSpriteDocument.height);
                for (int y = 0; y < copyH; ++y) {
                    for (int x = 0; x < copyW; ++x) {
                        for (int c = 0; c < 4; ++c) {
                            resized[static_cast<size_t>((y * dims[0] + x) * 4 + c)] =
                                pixelSpriteDocument.pixels[static_cast<size_t>((y * pixelSpriteDocument.width + x) * 4 + c)];
                        }
                    }
                }
                pixelSpriteDocument.width = dims[0];
                pixelSpriteDocument.height = dims[1];
                pixelSpriteDocument.pixels.swap(resized);
                pixelSpriteDocument.dirty = true;
                commitHistoryTop();
            }
        }

        ImGui::NextColumn();
        ImGui::TextDisabled("View");
        if (ImGui::SliderFloat("Zoom", &pixelSpriteTargetZoom, 1.0f, 128.0f, "%.1f")) {
            if (pixelSpritePixelPerfect) {
                pixelSpriteTargetZoom = std::round(pixelSpriteTargetZoom);
            }
        }
        ImGui::Checkbox("Pixel Perfect", &pixelSpritePixelPerfect);
        ImGui::SameLine();
        ImGui::Checkbox("Show Grid", &pixelSpriteShowGrid);

        const char* checkerLabels[] = { "Light Checker", "Dark Checker" };
        int checkerIndex = static_cast<int>(pixelSpriteCheckerTheme);
        ImGui::SetNextItemWidth(170.0f);
        if (ImGui::Combo("Background", &checkerIndex, checkerLabels, IM_ARRAYSIZE(checkerLabels))) {
            pixelSpriteCheckerTheme = static_cast<PixelSpriteCheckerTheme>(checkerIndex);
        }
        ImGui::ColorEdit4("Primary", &pixelSpritePrimaryColor.x, ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Secondary", &pixelSpriteSecondaryColor.x, ImGuiColorEditFlags_NoInputs);

        ImGui::TextDisabled("Tools");
        if (pixelSpriteEditorMode == PixelSpriteEditorMode::Edit) {
            const char* toolLabels[] = { "Pencil", "Eraser", "Fill", "Select" };
            int toolIndex = static_cast<int>(pixelSpriteTool);
            ImGui::SetNextItemWidth(170.0f);
            if (ImGui::Combo("Tool", &toolIndex, toolLabels, IM_ARRAYSIZE(toolLabels))) {
                pixelSpriteTool = static_cast<PixelSpriteTool>(toolIndex);
            }
        } else {
            if (ImGui::Button("Add Selection As Clip") && pixelSpriteDocument.selectionActive) {
                pushHistory();
                pixelSpriteDocument.spriteFrames.push_back(NormalizeRect(pixelSpriteDocument.selectionStart, pixelSpriteDocument.selectionEnd));
                pixelSpriteDocument.spriteFrameNames.push_back("Rect_" + std::to_string(pixelSpriteDocument.spriteFrames.size() - 1));
                pixelSpriteDocument.spriteFrameScales.push_back(glm::vec2(1.0f));
                pixelSpriteDocument.activeFrame = static_cast<int>(pixelSpriteDocument.spriteFrames.size()) - 1;
                pixelSpriteDocument.dirty = true;
                commitHistoryTop();
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Clips")) {
                pushHistory();
                pixelSpriteDocument.spriteFrames.clear();
                pixelSpriteDocument.spriteFrameNames.clear();
                pixelSpriteDocument.spriteFrameScales.clear();
                pixelSpriteDocument.activeFrame = 0;
                pixelSpriteDocument.dirty = true;
                commitHistoryTop();
            }
            if (!pixelSpriteDocument.spriteFrames.empty()) {
                EnsureSpriteClipNames(pixelSpriteDocument.spriteFrameNames, pixelSpriteDocument.spriteFrames.size());
                EnsureSpriteClipScales(pixelSpriteDocument.spriteFrameScales, pixelSpriteDocument.spriteFrames.size());
                ImGui::TextDisabled("%d clipped sprites", static_cast<int>(pixelSpriteDocument.spriteFrames.size()));
                ImGui::SliderInt("Selected Clip", &pixelSpriteDocument.activeFrame, 0, static_cast<int>(pixelSpriteDocument.spriteFrames.size()) - 1);
                char clipNameBuf[128];
                std::snprintf(clipNameBuf, sizeof(clipNameBuf), "%s",
                              pixelSpriteDocument.spriteFrameNames[pixelSpriteDocument.activeFrame].c_str());
                if (ImGui::InputText("Clip Name", clipNameBuf, sizeof(clipNameBuf))) {
                    pixelSpriteDocument.spriteFrameNames[pixelSpriteDocument.activeFrame] = clipNameBuf;
                    pixelSpriteDocument.dirty = true;
                    commitHistoryTop();
                }
                glm::ivec4 clipRect = pixelSpriteDocument.spriteFrames[pixelSpriteDocument.activeFrame];
                int clipPosition[2] = { clipRect.x, clipRect.y };
                int clipSize[2] = { clipRect.z, clipRect.w };
                bool clipRectChanged = false;
                if (ImGui::DragInt2("Clip Position", clipPosition, 1.0f, 0, std::max(pixelSpriteDocument.width, pixelSpriteDocument.height))) {
                    clipRect.x = clipPosition[0];
                    clipRect.y = clipPosition[1];
                    clipRectChanged = true;
                }
                if (ImGui::DragInt2("Clip Size", clipSize, 1.0f, 1, std::max(pixelSpriteDocument.width, pixelSpriteDocument.height))) {
                    clipRect.z = clipSize[0];
                    clipRect.w = clipSize[1];
                    clipRectChanged = true;
                }
                if (clipRectChanged) {
                    pixelSpriteDocument.spriteFrames[pixelSpriteDocument.activeFrame] =
                        ClampSpriteClipRect(clipRect, pixelSpriteDocument.width, pixelSpriteDocument.height);
                    pixelSpriteDocument.dirty = true;
                    commitHistoryTop();
                }
            }

            ImGui::SeparatorText("Spritesheet");
            char versionBuf[128];
            std::snprintf(versionBuf, sizeof(versionBuf), "%s",
                          pixelSpriteDocument.expectedMinimumModuEngineVersionOrHigher.empty()
                              ? "ModuEngine V6.5"
                              : pixelSpriteDocument.expectedMinimumModuEngineVersionOrHigher.c_str());
            if (ImGui::InputText("Engine Version", versionBuf, sizeof(versionBuf))) {
                pixelSpriteDocument.expectedMinimumModuEngineVersionOrHigher = versionBuf;
                pixelSpriteDocument.dirty = true;
            }
            if (ImGui::Checkbox("Strict Validation", &pixelSpriteDocument.strictValidation)) {
                pixelSpriteDocument.dirty = true;
            }

            ImGui::SeparatorText("Layers");
            EnsureSpriteLayers(pixelSpriteDocument.layers);
            if (ImGui::Button("Add Layer")) {
                pushHistory();
                pixelSpriteDocument.layers.push_back({"Layer_" + std::to_string(pixelSpriteDocument.layers.size())});
                pixelSpriteDocument.activeLayer = static_cast<int>(pixelSpriteDocument.layers.size()) - 1;
                pixelSpriteDocument.dirty = true;
                commitHistoryTop();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(pixelSpriteDocument.layers.size() <= 1);
            if (ImGui::Button("Remove Layer")) {
                pushHistory();
                pixelSpriteDocument.layers.erase(pixelSpriteDocument.layers.begin() + pixelSpriteDocument.activeLayer);
                EnsureSpriteLayers(pixelSpriteDocument.layers);
                pixelSpriteDocument.activeLayer = std::clamp(pixelSpriteDocument.activeLayer, 0, std::max(0, static_cast<int>(pixelSpriteDocument.layers.size()) - 1));
                pixelSpriteDocument.dirty = true;
                commitHistoryTop();
            }
            ImGui::EndDisabled();
            ImGui::SliderInt("Active Layer", &pixelSpriteDocument.activeLayer, 0, static_cast<int>(pixelSpriteDocument.layers.size()) - 1);
            char layerNameBuf[128];
            std::snprintf(layerNameBuf, sizeof(layerNameBuf), "%s",
                          pixelSpriteDocument.layers[pixelSpriteDocument.activeLayer].name.c_str());
            if (ImGui::InputText("Layer Name", layerNameBuf, sizeof(layerNameBuf))) {
                pixelSpriteDocument.layers[pixelSpriteDocument.activeLayer].name = layerNameBuf;
                pixelSpriteDocument.dirty = true;
                commitHistoryTop();
            }
        }

        ImGui::Columns(1);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    ImGui::Separator();
    ImGui::Text("%s%s", pixelSpriteDocument.name.c_str(), pixelSpriteDocument.dirty ? " *" : "");
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+Wheel zoom | MMB pan | Ctrl+Z/Y history");

    const float zoomBlend = std::clamp(deltaTime * 14.0f, 0.0f, 1.0f);
    if (pixelSpritePixelPerfect) {
        pixelSpriteTargetZoom = std::round(std::clamp(pixelSpriteTargetZoom, 1.0f, 128.0f));
    } else {
        pixelSpriteTargetZoom = std::clamp(pixelSpriteTargetZoom, 1.0f, 128.0f);
    }
    pixelSpriteZoom += (pixelSpriteTargetZoom - pixelSpriteZoom) * zoomBlend;
    if (pixelSpritePixelPerfect) {
        pixelSpriteZoom = std::round(pixelSpriteZoom);
    }

    const ImVec2 imageSize(
        pixelSpriteDocument.width * pixelSpriteZoom,
        pixelSpriteDocument.height * pixelSpriteZoom);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.06f, 0.11f, 1.0f));
    ImGui::BeginChild("PixelSpriteCanvas",
                      ImVec2(0.0f, 0.0f),
                      true,
                      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();

    const ImVec2 childWindowPos = ImGui::GetWindowPos();
    const ImVec2 childContentMin = ImGui::GetWindowContentRegionMin();
    const ImVec2 childContentMax = ImGui::GetWindowContentRegionMax();
    const ImVec2 childPos(childWindowPos.x + childContentMin.x, childWindowPos.y + childContentMin.y);
    const ImVec2 childMax(childWindowPos.x + childContentMax.x, childWindowPos.y + childContentMax.y);
    const ImVec2 avail(childMax.x - childPos.x, childMax.y - childPos.y);
    if (pixelSpriteCanvasCenterPending) {
        pixelSpriteCanvasPan = ImVec2(0.0f, 0.0f);
        pixelSpriteCanvasTargetPan = pixelSpriteCanvasPan;
        pixelSpriteCanvasCenterPending = false;
    }

    ImGui::SetCursorScreenPos(childPos);
    ImGui::InvisibleButton("PixelCanvasButton", avail,
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvasHovered = ImGui::IsItemHovered();
    const bool canvasActive = ImGui::IsItemActive();

    const ImVec2 mousePos = ImGui::GetIO().MousePos;
    const ImVec2 mouseViewport(mousePos.x - childPos.x, mousePos.y - childPos.y);
    const bool ctrlWheelZoom = canvasHovered && ImGui::GetIO().KeyCtrl && std::abs(ImGui::GetIO().MouseWheel) > 0.0f;
    const bool middleMousePan = canvasActive &&
        ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f);

    if (ctrlWheelZoom) {
        const float oldZoom = std::max(0.001f, pixelSpriteZoom);
        float nextZoom = oldZoom;
        if (pixelSpritePixelPerfect) {
            const float direction = (ImGui::GetIO().MouseWheel > 0.0f) ? 1.0f : -1.0f;
            nextZoom = std::clamp(std::round(oldZoom) + direction, 1.0f, 128.0f);
        } else {
            nextZoom = std::clamp(oldZoom * (ImGui::GetIO().MouseWheel > 0.0f ? 1.12f : (1.0f / 1.12f)), 1.0f, 128.0f);
        }

        const ImVec2 oldImageMin(
            childPos.x + (avail.x - imageSize.x) * 0.5f + pixelSpriteCanvasPan.x,
            childPos.y + (avail.y - imageSize.y) * 0.5f + pixelSpriteCanvasPan.y);
        const float imageSpaceX = (mousePos.x - oldImageMin.x) / oldZoom;
        const float imageSpaceY = (mousePos.y - oldImageMin.y) / oldZoom;
        pixelSpriteTargetZoom = nextZoom;
        pixelSpriteZoom = nextZoom;

        const ImVec2 nextImageSize(
            pixelSpriteDocument.width * nextZoom,
            pixelSpriteDocument.height * nextZoom);
        const ImVec2 nextCenteredMin(
            childPos.x + (avail.x - nextImageSize.x) * 0.5f,
            childPos.y + (avail.y - nextImageSize.y) * 0.5f);
        pixelSpriteCanvasPan.x = mousePos.x - (nextCenteredMin.x + imageSpaceX * nextZoom);
        pixelSpriteCanvasPan.y = mousePos.y - (nextCenteredMin.y + imageSpaceY * nextZoom);
        pixelSpriteCanvasTargetPan = pixelSpriteCanvasPan;
    } else if (middleMousePan) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        pixelSpriteCanvasPan.x += delta.x;
        pixelSpriteCanvasPan.y += delta.y;
        pixelSpriteCanvasTargetPan = pixelSpriteCanvasPan;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(childPos, childMax, true);
    const ImVec2 canvasOrigin(
        childPos.x + (avail.x - imageSize.x) * 0.5f + pixelSpriteCanvasPan.x,
        childPos.y + (avail.y - imageSize.y) * 0.5f + pixelSpriteCanvasPan.y);
    const ImVec2 imageMin = canvasOrigin;
    const ImVec2 imageMax(canvasOrigin.x + imageSize.x, canvasOrigin.y + imageSize.y);
    const int sampleStep = std::max(1, static_cast<int>(std::ceil(3.0f / std::max(0.001f, pixelSpriteZoom))));
    const bool darkChecker = pixelSpriteCheckerTheme == PixelSpriteCheckerTheme::Dark;
    for (int y = 0; y < pixelSpriteDocument.height; y += sampleStep) {
        for (int x = 0; x < pixelSpriteDocument.width; x += sampleStep) {
            const int blockW = std::min(sampleStep, pixelSpriteDocument.width - x);
            const int blockH = std::min(sampleStep, pixelSpriteDocument.height - y);
            const ImVec2 p0(canvasOrigin.x + x * pixelSpriteZoom, canvasOrigin.y + y * pixelSpriteZoom);
            const ImVec2 p1(p0.x + blockW * pixelSpriteZoom, p0.y + blockH * pixelSpriteZoom);
            draw->AddRectFilled(p0, p1, CheckerColor(darkChecker, ((x + y) & 1) != 0));
        }
    }

    for (int y = 0; y < pixelSpriteDocument.height; y += sampleStep) {
        for (int x = 0; x < pixelSpriteDocument.width; x += sampleStep) {
            const size_t idx = static_cast<size_t>((y * pixelSpriteDocument.width + x) * 4);
            const ImU32 color = IM_COL32(
                pixelSpriteDocument.pixels[idx + 0],
                pixelSpriteDocument.pixels[idx + 1],
                pixelSpriteDocument.pixels[idx + 2],
                pixelSpriteDocument.pixels[idx + 3]);
            if ((color >> IM_COL32_A_SHIFT) == 0) {
                continue;
            }
            const int blockW = std::min(sampleStep, pixelSpriteDocument.width - x);
            const int blockH = std::min(sampleStep, pixelSpriteDocument.height - y);
            const ImVec2 p0(canvasOrigin.x + x * pixelSpriteZoom, canvasOrigin.y + y * pixelSpriteZoom);
            const ImVec2 p1(p0.x + blockW * pixelSpriteZoom, p0.y + blockH * pixelSpriteZoom);
            draw->AddRectFilled(p0, p1, color);
            if (sampleStep == 1 && pixelSpriteShowGrid && pixelSpriteZoom >= 8.0f) {
                draw->AddRect(p0, p1, IM_COL32(0, 0, 0, 72));
            }
        }
    }

    for (size_t i = 0; i < pixelSpriteDocument.spriteFrames.size(); ++i) {
        const glm::ivec4 frame = pixelSpriteDocument.spriteFrames[i];
        EnsureSpriteClipNames(pixelSpriteDocument.spriteFrameNames, pixelSpriteDocument.spriteFrames.size());
        const ImU32 frameColor = static_cast<int>(i) == pixelSpriteDocument.activeFrame
            ? IM_COL32(250, 210, 80, 255)
            : IM_COL32(80, 220, 170, 220);
        draw->AddRect(
            ImVec2(canvasOrigin.x + frame.x * pixelSpriteZoom, canvasOrigin.y + frame.y * pixelSpriteZoom),
            ImVec2(canvasOrigin.x + (frame.x + frame.z) * pixelSpriteZoom, canvasOrigin.y + (frame.y + frame.w) * pixelSpriteZoom),
            frameColor,
            0.0f,
            0,
            2.0f);
        draw->AddText(
            ImVec2(canvasOrigin.x + frame.x * pixelSpriteZoom + 4.0f,
                   canvasOrigin.y + frame.y * pixelSpriteZoom + 2.0f),
            frameColor,
            pixelSpriteDocument.spriteFrameNames[i].c_str());
    }

    if (pixelSpriteDocument.selectionActive) {
        const glm::ivec4 rect = NormalizeRect(pixelSpriteDocument.selectionStart, pixelSpriteDocument.selectionEnd);
        draw->AddRect(
            ImVec2(canvasOrigin.x + rect.x * pixelSpriteZoom, canvasOrigin.y + rect.y * pixelSpriteZoom),
            ImVec2(canvasOrigin.x + (rect.x + rect.z) * pixelSpriteZoom, canvasOrigin.y + (rect.y + rect.w) * pixelSpriteZoom),
            IM_COL32(255, 255, 255, 220),
            0.0f,
            0,
            2.0f);
    }

    draw->AddRect(imageMin, imageMax, IM_COL32(255, 255, 255, 42));
    draw->PopClipRect();

    const bool hovered = canvasHovered;
    const bool held = canvasActive;
    const int px = static_cast<int>((mousePos.x - canvasOrigin.x) / pixelSpriteZoom);
    const int py = static_cast<int>((mousePos.y - canvasOrigin.y) / pixelSpriteZoom);
    const bool validPixel = hovered && px >= 0 && py >= 0 && px < pixelSpriteDocument.width && py < pixelSpriteDocument.height;

    if (validPixel && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (pixelSpriteTool == PixelSpriteTool::Select || pixelSpriteEditorMode == PixelSpriteEditorMode::SpriteSheet) {
            bool clickedExistingClip = false;
            if (pixelSpriteEditorMode == PixelSpriteEditorMode::SpriteSheet) {
                for (size_t i = 0; i < pixelSpriteDocument.spriteFrames.size(); ++i) {
                    const glm::ivec4& rect = pixelSpriteDocument.spriteFrames[i];
                    if (px >= rect.x && py >= rect.y && px < rect.x + rect.z && py < rect.y + rect.w) {
                        pixelSpriteDocument.activeFrame = static_cast<int>(i);
                        pixelSpriteDocument.selectionActive = true;
                        pixelSpriteDocument.selectionStart = glm::ivec2(rect.x, rect.y);
                        pixelSpriteDocument.selectionEnd = glm::ivec2(rect.x + rect.z - 1, rect.y + rect.w - 1);
                        clickedExistingClip = true;
                        break;
                    }
                }
            }
            if (clickedExistingClip) {
                // Selection synced to an existing clip; don't start a new drag box.
            } else {
                pushHistory();
                pixelSpriteDocument.selectionActive = true;
                pixelSpriteDocument.selectionStart = glm::ivec2(px, py);
                pixelSpriteDocument.selectionEnd = glm::ivec2(px, py);
                commitHistoryTop();
            }
        } else if (pixelSpriteTool == PixelSpriteTool::Fill) {
            const size_t idx = static_cast<size_t>((py * pixelSpriteDocument.width + px) * 4);
            PixelRgba target{
                pixelSpriteDocument.pixels[idx + 0],
                pixelSpriteDocument.pixels[idx + 1],
                pixelSpriteDocument.pixels[idx + 2],
                pixelSpriteDocument.pixels[idx + 3]
            };
            pushHistory();
            FloodFill(pixelSpriteDocument.pixels, pixelSpriteDocument.width, pixelSpriteDocument.height, px, py, target, ToRgba8(pixelSpritePrimaryColor));
            pixelSpriteDocument.dirty = true;
            commitHistoryTop();
        } else {
            pushHistory();
            const PixelRgba color = (pixelSpriteTool == PixelSpriteTool::Eraser)
                ? ToRgba8(pixelSpriteSecondaryColor)
                : ToRgba8(pixelSpritePrimaryColor);
            SetPixel(pixelSpriteDocument.pixels, pixelSpriteDocument.width, pixelSpriteDocument.height, px, py, color);
            pixelSpriteDocument.dirty = true;
            commitHistoryTop();
        }
    }

    if (validPixel && held && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        if (pixelSpriteTool == PixelSpriteTool::Select || pixelSpriteEditorMode == PixelSpriteEditorMode::SpriteSheet) {
            pixelSpriteDocument.selectionEnd = glm::ivec2(px, py);
            commitHistoryTop();
        } else if (pixelSpriteTool != PixelSpriteTool::Fill) {
            const PixelRgba color = (pixelSpriteTool == PixelSpriteTool::Eraser)
                ? ToRgba8(pixelSpriteSecondaryColor)
                : ToRgba8(pixelSpritePrimaryColor);
            SetPixel(pixelSpriteDocument.pixels, pixelSpriteDocument.width, pixelSpriteDocument.height, px, py, color);
            pixelSpriteDocument.dirty = true;
            commitHistoryTop();
        }
    }

    ImGui::EndChild();
    ImGui::End();
}
