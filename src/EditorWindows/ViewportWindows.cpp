#include "Engine.h"
#include "ModelLoader.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cfloat>
#include <cmath>
#include <cctype>
#include <functional>
#include <numeric>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <future>
#include <chrono>
#include <future>

#ifdef _WIN32
#include <shlobj.h>
#endif

void ModuRuntime2DProfiler_RecordUiRuntime(double uiRuntimeMs,
                                           double spriteBatchBuildMs,
                                           uint32_t visibleObjectCount);

namespace {
bool layoutFileNeedsUtilityDockMigration(const fs::path& layoutPath,
                                         bool projectSettingsVisible,
                                         bool modupakVisible) {
    std::ifstream in(layoutPath);
    if (!in.is_open()) {
        return false;
    }

    std::string currentWindow;
    std::string projectDockId;
    std::string projectSettingsDockId;
    bool hasModupakWindow = false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("[Window][", 0) == 0) {
            const size_t close = line.find(']', 9);
            currentWindow = (close == std::string::npos) ? std::string() : line.substr(9, close - 9);
            if (currentWindow == "Modupak Manager") {
                hasModupakWindow = true;
            }
            continue;
        }

        if (line.rfind("DockId=", 0) != 0) {
            continue;
        }

        const std::string dockId = line.substr(7, line.find(',') == std::string::npos ? std::string::npos
                                                                                       : line.find(',') - 7);
        if (currentWindow == "Project" && projectDockId.empty()) {
            projectDockId = dockId;
        } else if (currentWindow == "Project Settings" && projectSettingsDockId.empty()) {
            projectSettingsDockId = dockId;
        }
    }

        if (projectSettingsVisible &&
        !projectDockId.empty() &&
        !projectSettingsDockId.empty() &&
        projectDockId == projectSettingsDockId) {
        return true;
    }

    if (modupakVisible && !hasModupakWindow) {
        return true;
    }

    return false;
}

constexpr int kRuntimeInternalWidth = 1280;
constexpr int kRuntimeInternalHeight = 720;

struct EmbeddedViewportLayout {
    ImVec2 panelMin = ImVec2(0.0f, 0.0f);
    ImVec2 panelMax = ImVec2(0.0f, 0.0f);
    ImVec2 panelSize = ImVec2(0.0f, 0.0f);
    ImVec2 displayMin = ImVec2(0.0f, 0.0f);
    ImVec2 displayMax = ImVec2(0.0f, 0.0f);
    ImVec2 displaySize = ImVec2(0.0f, 0.0f);
    ImVec2 uvMin = ImVec2(0.0f, 0.0f);
    ImVec2 uvMax = ImVec2(1.0f, 1.0f);
};

ImVec2 ComputeAspectFitSize(const ImVec2& available, float aspect) {
    const float safeWidth = std::max(1.0f, available.x);
    const float safeHeight = std::max(1.0f, available.y);
    float width = safeWidth;
    float height = width / std::max(0.001f, aspect);
    if (height > safeHeight) {
        height = safeHeight;
        width = height * std::max(0.001f, aspect);
    }
    return ImVec2(std::max(1.0f, std::floor(width)),
                  std::max(1.0f, std::floor(height)));
}

EmbeddedViewportLayout BuildEmbeddedViewportLayout(const ImVec2& panelMin,
                                                   const ImVec2& panelSize,
                                                   int renderWidth,
                                                   int renderHeight,
                                                   ViewportDisplayMode displayMode,
                                                   float zoom = 1.0f) {
    EmbeddedViewportLayout layout;
    layout.panelMin = panelMin;
    layout.panelSize = ImVec2(std::max(1.0f, panelSize.x), std::max(1.0f, panelSize.y));
    layout.panelMax = ImVec2(layout.panelMin.x + layout.panelSize.x,
                             layout.panelMin.y + layout.panelSize.y);

    const float safeRenderWidth = static_cast<float>(std::max(1, renderWidth));
    const float safeRenderHeight = static_cast<float>(std::max(1, renderHeight));
    const float aspect = safeRenderWidth / safeRenderHeight;
    const float panelAspect = layout.panelSize.x / std::max(1.0f, layout.panelSize.y);

    ImVec2 displaySize = layout.panelSize;
    ImVec2 baseUvMin(0.0f, 0.0f);
    ImVec2 baseUvMax(1.0f, 1.0f);
    switch (displayMode) {
        case ViewportDisplayMode::Fit:
            displaySize = ComputeAspectFitSize(layout.panelSize, aspect);
            break;
        case ViewportDisplayMode::Fill:
            displaySize = layout.panelSize;
            if (panelAspect > aspect) {
                const float croppedHeight = std::clamp(aspect / std::max(0.001f, panelAspect), 0.0f, 1.0f);
                const float cropOffset = (1.0f - croppedHeight) * 0.5f;
                baseUvMin.y = cropOffset;
                baseUvMax.y = cropOffset + croppedHeight;
            } else {
                const float croppedWidth = std::clamp(panelAspect / std::max(0.001f, aspect), 0.0f, 1.0f);
                const float cropOffset = (1.0f - croppedWidth) * 0.5f;
                baseUvMin.x = cropOffset;
                baseUvMax.x = cropOffset + croppedWidth;
            }
            break;
        case ViewportDisplayMode::IntegerScale: {
            const float scaleX = layout.panelSize.x / safeRenderWidth;
            const float scaleY = layout.panelSize.y / safeRenderHeight;
            float integerScale = std::floor(std::min(scaleX, scaleY));
            if (integerScale < 1.0f) {
                displaySize = ComputeAspectFitSize(layout.panelSize, aspect);
            } else {
                displaySize = ImVec2(
                    std::max(1.0f, std::floor(safeRenderWidth * integerScale)),
                    std::max(1.0f, std::floor(safeRenderHeight * integerScale)));
            }
            break;
        }
        case ViewportDisplayMode::Stretch:
        default:
            displaySize = layout.panelSize;
            break;
    }

    const float offsetX = (layout.panelSize.x - displaySize.x) * 0.5f;
    const float offsetY = (layout.panelSize.y - displaySize.y) * 0.5f;
    layout.displayMin = ImVec2(layout.panelMin.x + offsetX, layout.panelMin.y + offsetY);
    layout.displayMax = ImVec2(layout.displayMin.x + displaySize.x,
                               layout.displayMin.y + displaySize.y);
    layout.displaySize = displaySize;

    const float safeZoom = std::clamp(zoom, 1.0f, 8.0f);
    const float baseUvWidth = std::max(0.0f, baseUvMax.x - baseUvMin.x);
    const float baseUvHeight = std::max(0.0f, baseUvMax.y - baseUvMin.y);
    const float zoomedUvWidth = std::clamp(baseUvWidth / safeZoom, 0.0f, 1.0f);
    const float zoomedUvHeight = std::clamp(baseUvHeight / safeZoom, 0.0f, 1.0f);
    const float zoomOffsetX = (baseUvWidth - zoomedUvWidth) * 0.5f;
    const float zoomOffsetY = (baseUvHeight - zoomedUvHeight) * 0.5f;
    layout.uvMin = ImVec2(baseUvMin.x + zoomOffsetX, baseUvMin.y + zoomOffsetY);
    layout.uvMax = ImVec2(layout.uvMin.x + zoomedUvWidth, layout.uvMin.y + zoomedUvHeight);
    return layout;
}

bool TryMapScreenPointToRenderPixel(const EmbeddedViewportLayout& layout,
                                    const ImVec2& screenPoint,
                                    int renderWidth,
                                    int renderHeight,
                                    glm::vec2& outRenderPixel,
                                    glm::vec2* outNormalized = nullptr) {
    const ImVec2 visibleMin(std::max(layout.panelMin.x, layout.displayMin.x),
                            std::max(layout.panelMin.y, layout.displayMin.y));
    const ImVec2 visibleMax(std::min(layout.panelMax.x, layout.displayMax.x),
                            std::min(layout.panelMax.y, layout.displayMax.y));
    if (screenPoint.x < visibleMin.x || screenPoint.x > visibleMax.x ||
        screenPoint.y < visibleMin.y || screenPoint.y > visibleMax.y) {
        return false;
    }

    const float displayWidth = std::max(1.0f, layout.displaySize.x);
    const float displayHeight = std::max(1.0f, layout.displaySize.y);
    const float normX = std::clamp((screenPoint.x - layout.displayMin.x) / displayWidth, 0.0f, 1.0f);
    const float normY = std::clamp((screenPoint.y - layout.displayMin.y) / displayHeight, 0.0f, 1.0f);
    const float sourceU = layout.uvMin.x + (layout.uvMax.x - layout.uvMin.x) * normX;
    const float sourceV = layout.uvMin.y + (layout.uvMax.y - layout.uvMin.y) * normY;
    outRenderPixel = glm::vec2(
        sourceU * static_cast<float>(std::max(1, renderWidth)),
        sourceV * static_cast<float>(std::max(1, renderHeight)));
    if (outNormalized != nullptr) {
        *outNormalized = glm::vec2(sourceU, sourceV);
    }
    return true;
}

ImVec2 MapRenderPixelToScreenPoint(const EmbeddedViewportLayout& layout,
                                   int renderWidth,
                                   int renderHeight,
                                   const glm::vec2& renderPixel) {
    const float safeRenderWidth = static_cast<float>(std::max(1, renderWidth));
    const float safeRenderHeight = static_cast<float>(std::max(1, renderHeight));
    const float visibleUvWidth = std::max(0.0001f, layout.uvMax.x - layout.uvMin.x);
    const float visibleUvHeight = std::max(0.0001f, layout.uvMax.y - layout.uvMin.y);
    const float sourceU = renderPixel.x / safeRenderWidth;
    const float sourceV = renderPixel.y / safeRenderHeight;
    const float screenNormX = (sourceU - layout.uvMin.x) / visibleUvWidth;
    const float screenNormY = (sourceV - layout.uvMin.y) / visibleUvHeight;
    return ImVec2(layout.displayMin.x + screenNormX * layout.displaySize.x,
                  layout.displayMin.y + screenNormY * layout.displaySize.y);
}

ImVec2 MapRenderDeltaToScreenDelta(const EmbeddedViewportLayout& layout,
                                   int renderWidth,
                                   int renderHeight,
                                   const glm::vec2& renderDelta) {
    const float safeRenderWidth = static_cast<float>(std::max(1, renderWidth));
    const float safeRenderHeight = static_cast<float>(std::max(1, renderHeight));
    const float visibleUvWidth = std::max(0.0001f, layout.uvMax.x - layout.uvMin.x);
    const float visibleUvHeight = std::max(0.0001f, layout.uvMax.y - layout.uvMin.y);
    return ImVec2((renderDelta.x / safeRenderWidth) * (layout.displaySize.x / visibleUvWidth),
                  (renderDelta.y / safeRenderHeight) * (layout.displaySize.y / visibleUvHeight));
}

void MapRenderRectToScreenRect(const EmbeddedViewportLayout& layout,
                               int renderWidth,
                               int renderHeight,
                               const glm::vec2& renderMin,
                               const glm::vec2& renderMax,
                               ImVec2& outMin,
                               ImVec2& outMax) {
    const ImVec2 p0 = MapRenderPixelToScreenPoint(layout, renderWidth, renderHeight, renderMin);
    const ImVec2 p1 = MapRenderPixelToScreenPoint(layout, renderWidth, renderHeight, renderMax);
    outMin = ImVec2(std::min(p0.x, p1.x), std::min(p0.y, p1.y));
    outMax = ImVec2(std::max(p0.x, p1.x), std::max(p0.y, p1.y));
}

void ApplyNearestTextureSampling(GLuint textureId) {
    if (textureId == 0 || glfwGetCurrentContext() == nullptr) return;

    GLint previousTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
}

bool RuntimeUiDrawOrderLess(const SceneObject* a, const SceneObject* b) {
    if (a == nullptr || b == nullptr) return false;
    if (a->layer != b->layer) return a->layer < b->layer;

    const int parallaxOrderA = (a->hasParallaxLayer2D && a->parallaxLayer2D.enabled)
        ? a->parallaxLayer2D.order
        : 0;
    const int parallaxOrderB = (b->hasParallaxLayer2D && b->parallaxLayer2D.enabled)
        ? b->parallaxLayer2D.order
        : 0;
    if (parallaxOrderA != parallaxOrderB) return parallaxOrderA < parallaxOrderB;

    return false;
}

void StableSortRuntimeUiDrawList(std::vector<SceneObject*>& drawList) {
    std::stable_sort(drawList.begin(), drawList.end(), RuntimeUiDrawOrderLess);
}

bool ProjectWorldToOverlayPoint(const glm::vec3& worldPos,
                                const glm::mat4& view,
                                const glm::mat4& proj,
                                const ImVec2& overlayPos,
                                const ImVec2& overlaySize,
                                ImVec2& outScreen) {
    glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0001f) {
        return false;
    }

    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) {
        return false;
    }

    outScreen.x = overlayPos.x + (ndc.x * 0.5f + 0.5f) * overlaySize.x;
    outScreen.y = overlayPos.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * overlaySize.y;
    return true;
}

bool HasMeaningfulSpriteFrameScales(const UIElementComponent& ui) {
    if (ui.spriteCustomFrameScales.size() != ui.spriteCustomFrames.size() ||
        ui.spriteCustomFrameScales.empty()) {
        return false;
    }
    for (const glm::vec2& scale : ui.spriteCustomFrameScales) {
        if (std::abs(scale.x - 1.0f) > 0.0001f || std::abs(scale.y - 1.0f) > 0.0001f) {
            return true;
        }
    }
    return false;
}

glm::vec2 ResolveSpriteFrameScale(const UIElementComponent& ui) {
    if (!ui.spriteCustomFramesEnabled || ui.spriteCustomFrames.empty()) {
        return glm::vec2(1.0f);
    }

    const int frameCount = static_cast<int>(ui.spriteCustomFrames.size());
    const int frame = std::clamp(ui.spriteSheetFrame, 0, frameCount - 1);
    if (HasMeaningfulSpriteFrameScales(ui)) {
        const glm::vec2 authored = ui.spriteCustomFrameScales[static_cast<size_t>(frame)];
        return glm::vec2(std::max(0.01f, authored.x), std::max(0.01f, authored.y));
    }

    const glm::ivec4& referenceRect = ui.spriteCustomFrames.front();
    const glm::ivec4& frameRect = ui.spriteCustomFrames[static_cast<size_t>(frame)];
    const float referenceWidth = static_cast<float>(std::max(1, referenceRect.z));
    const float referenceHeight = static_cast<float>(std::max(1, referenceRect.w));
    return glm::vec2(
        static_cast<float>(std::max(1, frameRect.z)) / referenceWidth,
        static_cast<float>(std::max(1, frameRect.w)) / referenceHeight);
}

bool ResolveProjectedSprite25DRect(const SceneObject& obj,
                                   const glm::mat4& view,
                                   const glm::mat4& proj,
                                   const ImVec2& overlayPos,
                                   const ImVec2& overlaySize,
                                   ImVec2& outMin,
                                   ImVec2& outMax) {
    glm::mat4 invView = glm::inverse(view);
    glm::vec3 cameraRight = glm::normalize(glm::vec3(invView[0]));
    glm::vec3 cameraUp = glm::normalize(glm::vec3(invView[1]));
    glm::vec2 baseSize = glm::max(obj.ui.size, glm::vec2(0.01f)) * ResolveSpriteFrameScale(obj.ui);
    glm::vec3 objectScale = glm::max(glm::abs(obj.scale), glm::vec3(0.01f));
    glm::vec2 worldHalfExtents = glm::vec2(baseSize.x * objectScale.x, baseSize.y * objectScale.y) * 0.005f;

    ImVec2 center;
    ImVec2 rightPoint;
    ImVec2 upPoint;
    if (!ProjectWorldToOverlayPoint(obj.position, view, proj, overlayPos, overlaySize, center) ||
        !ProjectWorldToOverlayPoint(obj.position + cameraRight * worldHalfExtents.x, view, proj, overlayPos, overlaySize, rightPoint) ||
        !ProjectWorldToOverlayPoint(obj.position + cameraUp * worldHalfExtents.y, view, proj, overlayPos, overlaySize, upPoint)) {
        return false;
    }

    float halfWidth = std::max(1.0f, std::abs(rightPoint.x - center.x));
    float halfHeight = std::max(1.0f, std::abs(upPoint.y - center.y));
    outMin = ImVec2(center.x - halfWidth, center.y - halfHeight);
    outMax = ImVec2(center.x + halfWidth, center.y + halfHeight);
    return true;
}

void ApplyUIFontFilterCallback(const ImDrawList*, const ImDrawCmd* cmd) {
    if (!cmd) return;
    if (glfwGetCurrentContext() == nullptr) return;

    const intptr_t mode = reinterpret_cast<intptr_t>(cmd->UserCallbackData);
    const bool usePoint = (mode == 1);
    const ImTextureID fontTexRef = ImGui::GetIO().Fonts->TexRef.GetTexID();
    const uintptr_t rawTextureId = (uintptr_t)fontTexRef;
    const GLuint fontTextureId = static_cast<GLuint>(rawTextureId);
    if (fontTextureId == 0) return;

    glBindTexture(GL_TEXTURE_2D, fontTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, usePoint ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, usePoint ? GL_NEAREST : GL_LINEAR);
}

void AppendWrappedTextSegment(ImFont* font,
                              float fontSize,
                              const char* start,
                              const char* end,
                              float wrapWidth,
                              bool autoWrap,
                              std::vector<std::string>& outLines) {
    if (!start || !end || end < start) return;
    if (start == end) {
        outLines.emplace_back();
        return;
    }

    if (!autoWrap || wrapWidth <= 1.0f) {
        outLines.emplace_back(start, end);
        return;
    }

    const char* cursor = start;
    while (cursor < end) {
        while (cursor < end && (*cursor == ' ' || *cursor == '\t')) {
            ++cursor;
        }
        if (cursor >= end) {
            outLines.emplace_back();
            break;
        }

        const char* wrapPos = font->CalcWordWrapPositionA(fontSize, cursor, end, wrapWidth);
        if (!wrapPos || wrapPos <= cursor) {
            unsigned int codepoint = 0;
            int bytes = ImTextCharFromUtf8(&codepoint, cursor, end);
            if (bytes <= 0) bytes = 1;
            wrapPos = std::min(end, cursor + bytes);
        }

        const char* trimmedEnd = wrapPos;
        while (trimmedEnd > cursor && (trimmedEnd[-1] == ' ' || trimmedEnd[-1] == '\t')) {
            --trimmedEnd;
        }
        outLines.emplace_back(cursor, trimmedEnd);
        cursor = wrapPos;
    }
}

std::vector<std::string> BuildWrappedTextLines(ImFont* font,
                                               float fontSize,
                                               const char* text,
                                               float wrapWidth,
                                               bool autoWrap) {
    std::vector<std::string> lines;
    if (!font || !text) return lines;

    const char* lineStart = text;
    const char* cursor = text;
    while (true) {
        if (*cursor == '\n' || *cursor == '\0') {
            AppendWrappedTextSegment(font, fontSize, lineStart, cursor, wrapWidth, autoWrap, lines);
            if (*cursor == '\0') break;
            lineStart = cursor + 1;
        }
        ++cursor;
    }
    return lines;
}

void DrawUITextLineWithEffects(ImDrawList* drawList,
                               ImFont* font,
                               float fontSize,
                               const ImVec2& linePos,
                               ImU32 baseColor,
                               const char* lineText,
                               int effectFlags,
                               float effectSpeed,
                               float effectIntensity) {
    if (!drawList || !font || !lineText || !*lineText) return;
    if (effectFlags == 0 || effectIntensity <= 0.0f) {
        drawList->AddText(font, fontSize, linePos, baseColor, lineText);
        return;
    }

    const int r = (baseColor >> IM_COL32_R_SHIFT) & 0xFF;
    const int g = (baseColor >> IM_COL32_G_SHIFT) & 0xFF;
    const int b = (baseColor >> IM_COL32_B_SHIFT) & 0xFF;
    const int a = (baseColor >> IM_COL32_A_SHIFT) & 0xFF;

    const float time = static_cast<float>(ImGui::GetTime());
    const float speed = std::max(0.01f, effectSpeed);
    const float amplitude = std::max(0.0f, effectIntensity) * std::max(1.0f, fontSize * 0.15f);

    float penX = linePos.x;
    int glyphIndex = 0;
    const char* cursor = lineText;
    while (*cursor != '\0') {
        unsigned int codepoint = 0;
        int bytes = ImTextCharFromUtf8(&codepoint, cursor, nullptr);
        if (bytes <= 0) bytes = 1;

        char glyphBuf[8] = {};
        const int copyBytes = std::min(bytes, static_cast<int>(sizeof(glyphBuf) - 1));
        std::memcpy(glyphBuf, cursor, static_cast<size_t>(copyBytes));
        glyphBuf[copyBytes] = '\0';

        const float advance = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, glyphBuf).x;
        ImVec2 glyphPos(penX, linePos.y);
        ImU32 glyphColor = baseColor;
        const bool whitespace = (codepoint == ' ' || codepoint == '\t');

        if (!whitespace) {
            const float phase = time * speed * 2.0f + static_cast<float>(glyphIndex) * 0.45f;
            if ((effectFlags & (1 << 0)) != 0) {
                glyphPos.y += std::sin(phase) * amplitude;
            }
            if ((effectFlags & (1 << 1)) != 0) {
                glyphPos.x += std::sin(phase * 12.7f) * amplitude * 0.35f;
                glyphPos.y += std::cos(phase * 17.3f) * amplitude * 0.35f;
            }
            if ((effectFlags & (1 << 2)) != 0) {
                glyphPos.y -= std::abs(std::sin(phase)) * amplitude * 1.15f;
            }
            if ((effectFlags & (1 << 3)) != 0) {
                glyphPos.x += std::cos(phase) * amplitude * 0.6f;
                glyphPos.y += std::sin(phase) * amplitude * 0.6f;
            }
            if ((effectFlags & (1 << 4)) != 0) {
                const float fade = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(phase));
                glyphColor = IM_COL32(r, g, b, static_cast<int>(std::clamp(fade * static_cast<float>(a), 0.0f, 255.0f)));
            }
        }

        drawList->AddText(font, fontSize, glyphPos, glyphColor, glyphBuf, glyphBuf + copyBytes);
        penX += advance;
        cursor += bytes;
        ++glyphIndex;
    }
}

void AddUITextWithFilter(ImDrawList* drawList,
                         MaterialProperties::TextureFilter filter,
                         ImFont* font,
                         float fontSize,
                         const ImVec2& drawMin,
                         const ImVec2& drawMax,
                         ImU32 color,
                         const char* text,
                         bool autoWrap,
                         UITextHAlign hAlign,
                         UITextVAlign vAlign,
                         int effectFlags,
                         float effectSpeed,
                         float effectIntensity) {
    if (!drawList || !font || !text || !*text) return;

    const ImVec2 contentMin(drawMin.x + 4.0f, drawMin.y + 2.0f);
    const ImVec2 contentMax(drawMax.x - 4.0f, drawMax.y - 2.0f);
    const float contentWidth = std::max(1.0f, contentMax.x - contentMin.x);
    const float contentHeight = std::max(1.0f, contentMax.y - contentMin.y);

    std::vector<std::string> lines = BuildWrappedTextLines(font, fontSize, text, contentWidth, autoWrap);
    if (lines.empty()) return;

    const float lineHeight = std::max(1.0f, fontSize);
    const float totalHeight = lineHeight * static_cast<float>(lines.size());
    float startY = contentMin.y;
    if (vAlign == UITextVAlign::Middle) {
        startY = contentMin.y + (contentHeight - totalHeight) * 0.5f;
    } else if (vAlign == UITextVAlign::Bottom) {
        startY = contentMax.y - totalHeight;
    }
    startY = std::max(contentMin.y, startY);

    if (filter == MaterialProperties::TextureFilter::Point) {
        drawList->AddCallback(ApplyUIFontFilterCallback, reinterpret_cast<void*>(static_cast<intptr_t>(1)));
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        const float y = startY + static_cast<float>(i) * lineHeight;
        if (y > contentMax.y || y + lineHeight < contentMin.y) {
            continue;
        }

        const float lineWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, line.c_str()).x;
        float x = contentMin.x;
        if (hAlign == UITextHAlign::Center) {
            x = contentMin.x + (contentWidth - lineWidth) * 0.5f;
        } else if (hAlign == UITextHAlign::Right) {
            x = contentMax.x - lineWidth;
        }
        x = std::max(contentMin.x, x);

        DrawUITextLineWithEffects(drawList, font, fontSize, ImVec2(x, y), color, line.c_str(),
                                  effectFlags, effectSpeed, effectIntensity);
    }

    if (filter == MaterialProperties::TextureFilter::Point) {
        drawList->AddCallback(ApplyUIFontFilterCallback, reinterpret_cast<void*>(static_cast<intptr_t>(0)));
    }
}

template <typename EnumType, size_t N>
ObjectType MapEnumToObjectType(EnumType value, const std::array<ObjectType, N>& mapping) {
    const size_t index = static_cast<size_t>(value);
    if (index >= mapping.size()) {
        return ObjectType::Empty;
    }
    return mapping[index];
}

static constexpr std::array<ObjectType, static_cast<size_t>(RenderType::Sprite) + 1> kRenderTypeMainObjectMap = {{
    ObjectType::Empty,     // RenderType::None
    ObjectType::Cube,      // RenderType::Cube
    ObjectType::Sphere,    // RenderType::Sphere
    ObjectType::Capsule,   // RenderType::Capsule
    ObjectType::OBJMesh,   // RenderType::OBJMesh
    ObjectType::Model,     // RenderType::Model
    ObjectType::Mirror,    // RenderType::Mirror
    ObjectType::Plane,     // RenderType::Plane
    ObjectType::Torus,     // RenderType::Torus
    ObjectType::Sprite     // RenderType::Sprite
}};

static constexpr std::array<ObjectType, static_cast<size_t>(UIElementType::Sprite2D) + 1> kUiTypeMainObjectMap = {{
    ObjectType::Empty,      // UIElementType::None
    ObjectType::Canvas,     // UIElementType::Canvas
    ObjectType::UIImage,    // UIElementType::Image
    ObjectType::UISlider,   // UIElementType::Slider
    ObjectType::UIButton,   // UIElementType::Button
    ObjectType::UIText,     // UIElementType::Text
    ObjectType::Sprite2D    // UIElementType::Sprite2D
}};

static constexpr std::array<ObjectType, static_cast<size_t>(LightType::Area) + 1> kLightTypeMainObjectMap = {{
    ObjectType::DirectionalLight, // LightType::Directional
    ObjectType::PointLight,       // LightType::Point
    ObjectType::SpotLight,        // LightType::Spot
    ObjectType::AreaLight         // LightType::Area
}};

struct UiSceneLookupCache {
    explicit UiSceneLookupCache(const std::vector<SceneObject>& objects) {
        byId.reserve(objects.size());
        parentOffsetCache.reserve(objects.size());
        canvas3DIdCache.reserve(objects.size());
        pseudoCanvasIdCache.reserve(objects.size());
        for (const SceneObject& obj : objects) {
            byId.emplace(obj.id, &obj);
        }
    }

    const SceneObject* find(int id) const {
        auto it = byId.find(id);
        return (it != byId.end()) ? it->second : nullptr;
    }

    glm::vec2 getWorldParentOffset(const SceneObject& obj) {
        auto cached = parentOffsetCache.find(obj.id);
        if (cached != parentOffsetCache.end()) {
            return cached->second;
        }

        glm::vec2 offset(0.0f);
        if (const SceneObject* parent = find(obj.parentId)) {
            offset = getWorldParentOffset(*parent);
            if (parent->type == ObjectType::Sprite25D) {
                offset += glm::vec2(parent->position.x, parent->position.y);
            } else if (parent->hasUI && parent->ui.type != UIElementType::None) {
                offset += glm::vec2(parent->ui.position.x, parent->ui.position.y);
            } else {
                offset += glm::vec2(parent->position.x, parent->position.y);
            }
        }

        parentOffsetCache.emplace(obj.id, offset);
        return offset;
    }

    int find3DCanvasId(const SceneObject& obj) {
        auto cached = canvas3DIdCache.find(obj.id);
        if (cached != canvas3DIdCache.end()) {
            return cached->second;
        }

        int canvasId = -1;
        if (obj.hasUI && obj.ui.type == UIElementType::Canvas && obj.ui.renderIn3D) {
            canvasId = obj.id;
        } else if (const SceneObject* parent = find(obj.parentId)) {
            canvasId = find3DCanvasId(*parent);
        }

        canvas3DIdCache.emplace(obj.id, canvasId);
        return canvasId;
    }

    int findPseudo3DCanvasId(const SceneObject& obj) {
        auto cached = pseudoCanvasIdCache.find(obj.id);
        if (cached != pseudoCanvasIdCache.end()) {
            return cached->second;
        }

        int canvasId = -1;
        if (obj.hasUI &&
            obj.ui.type == UIElementType::Canvas &&
            !obj.ui.renderIn3D &&
            obj.ui.pseudo3DEnabled &&
            obj.ui.pseudo3DUseOffscreenSurface) {
            canvasId = obj.id;
        } else if (const SceneObject* parent = find(obj.parentId)) {
            canvasId = findPseudo3DCanvasId(*parent);
        }

        pseudoCanvasIdCache.emplace(obj.id, canvasId);
        return canvasId;
    }

private:
    std::unordered_map<int, const SceneObject*> byId;
    std::unordered_map<int, glm::vec2> parentOffsetCache;
    std::unordered_map<int, int> canvas3DIdCache;
    std::unordered_map<int, int> pseudoCanvasIdCache;
};

class SpriteTextureResolver {
public:
    explicit SpriteTextureResolver(Renderer* renderer) : renderer(renderer) {}

    Texture* resolveTexture(const SceneObject& obj) {
        if (renderer == nullptr || obj.albedoTexturePath.empty()) {
            return nullptr;
        }

        auto cached = textureIdCache.find(obj.albedoTexturePath);
        if (cached != textureIdCache.end()) {
            return cached->second.texture;
        }

        Texture* texture = renderer->getTexture(obj.albedoTexturePath, MaterialProperties::TextureFilter::Point);
        CachedTexture cachedTexture;
        cachedTexture.texture = texture;
        cachedTexture.textureId = (texture != nullptr) ? texture->GetID() : 0;
        textureIdCache.emplace(obj.albedoTexturePath, cachedTexture);
        return texture;
    }

    unsigned int resolve(const SceneObject& obj) {
        Texture* texture = resolveTexture(obj);
        if (texture == nullptr) {
            return 0;
        }
        auto cached = textureIdCache.find(obj.albedoTexturePath);
        return (cached != textureIdCache.end()) ? cached->second.textureId : texture->GetID();
    }

private:
    struct CachedTexture {
        Texture* texture = nullptr;
        unsigned int textureId = 0;
    };

    Renderer* renderer = nullptr;
    std::unordered_map<std::string, CachedTexture> textureIdCache;
};

struct BatchedSpriteQuad {
    ImTextureID textureId = 0;
    ImVec2 pos[4];
    ImVec2 uv[4];
    ImU32 color = 0;
};

class BatchedSpriteEmitter {
public:
    explicit BatchedSpriteEmitter(ImDrawList* drawList) : drawList(drawList) {}

    void reserve(size_t quadCount) {
        quads.reserve(quadCount);
    }

    void push(ImTextureID textureId,
              const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3,
              const ImVec2& uv0, const ImVec2& uv1, const ImVec2& uv2, const ImVec2& uv3,
              ImU32 color) {
        if (textureId == 0) {
            flush();
            return;
        }
        if (!quads.empty() && textureId != currentTextureId) {
            flush();
        }
        currentTextureId = textureId;
        BatchedSpriteQuad& quad = quads.emplace_back();
        quad.textureId = textureId;
        quad.pos[0] = p0;
        quad.pos[1] = p1;
        quad.pos[2] = p2;
        quad.pos[3] = p3;
        quad.uv[0] = uv0;
        quad.uv[1] = uv1;
        quad.uv[2] = uv2;
        quad.uv[3] = uv3;
        quad.color = color;
    }

    void flush() {
        if (quads.empty() || drawList == nullptr || currentTextureId == 0) {
            quads.clear();
            currentTextureId = 0;
            return;
        }

        drawList->PushTextureID(currentTextureId);
        drawList->PrimReserve(static_cast<int>(quads.size()) * 6, static_cast<int>(quads.size()) * 4);
        for (const BatchedSpriteQuad& quad : quads) {
            unsigned int idx = drawList->_VtxCurrentIdx;
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(idx));
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(idx + 1));
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(idx + 2));
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(idx));
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(idx + 2));
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(idx + 3));
            drawList->PrimWriteVtx(quad.pos[0], quad.uv[0], quad.color);
            drawList->PrimWriteVtx(quad.pos[1], quad.uv[1], quad.color);
            drawList->PrimWriteVtx(quad.pos[2], quad.uv[2], quad.color);
            drawList->PrimWriteVtx(quad.pos[3], quad.uv[3], quad.color);
        }
        drawList->PopTextureID();
        quads.clear();
        currentTextureId = 0;
    }

private:
    ImDrawList* drawList = nullptr;
    ImTextureID currentTextureId = 0;
    std::vector<BatchedSpriteQuad> quads;
};

ImVec2 ResolveUiSourceFrameSizePx(const SceneObject& obj, int frame, const Texture* texture) {
    if (obj.ui.spriteCustomFramesEnabled && !obj.ui.spriteCustomFrames.empty()) {
        const int clampedFrame = std::clamp(frame, 0, static_cast<int>(obj.ui.spriteCustomFrames.size()) - 1);
        const glm::ivec4& rect = obj.ui.spriteCustomFrames[static_cast<size_t>(clampedFrame)];
        return ImVec2(static_cast<float>(std::max(1, rect.z)),
                      static_cast<float>(std::max(1, rect.w)));
    }

    if (texture != nullptr) {
        float frameWidth = static_cast<float>(std::max(1, texture->GetWidth()));
        float frameHeight = static_cast<float>(std::max(1, texture->GetHeight()));
        if (obj.ui.spriteSheetEnabled) {
            frameWidth = std::max(1.0f, frameWidth / static_cast<float>(std::max(1, obj.ui.spriteSheetColumns)));
            frameHeight = std::max(1.0f, frameHeight / static_cast<float>(std::max(1, obj.ui.spriteSheetRows)));
        }
        return ImVec2(frameWidth, frameHeight);
    }

    return ImVec2(std::max(1.0f, obj.ui.size.x), std::max(1.0f, obj.ui.size.y));
}

bool DrawNineSliceSprite(BatchedSpriteEmitter& spriteBatch,
                         ImTextureID textureId,
                         const SceneObject& obj,
                         const ImVec2& drawMin,
                         const ImVec2& drawMax,
                         const std::array<ImVec2, 4>& uvQuad,
                         const ImVec2& sourceFrameSizePx,
                         float angleRad,
                         ImU32 color) {
    if (!obj.ui.nineSliceEnabled || textureId == 0) {
        return false;
    }

    const float rectWidth = drawMax.x - drawMin.x;
    const float rectHeight = drawMax.y - drawMin.y;
    if (rectWidth <= 1.0f || rectHeight <= 1.0f) {
        return false;
    }

    const float srcW = std::max(1.0f, sourceFrameSizePx.x);
    const float srcH = std::max(1.0f, sourceFrameSizePx.y);

    float srcLeft = std::max(0.0f, obj.ui.nineSliceBorder.x);
    float srcRight = std::max(0.0f, obj.ui.nineSliceBorder.y);
    float srcTop = std::max(0.0f, obj.ui.nineSliceBorder.z);
    float srcBottom = std::max(0.0f, obj.ui.nineSliceBorder.w);

    if (srcLeft + srcRight > srcW) {
        const float k = srcW / std::max(1e-4f, srcLeft + srcRight);
        srcLeft *= k;
        srcRight *= k;
    }
    if (srcTop + srcBottom > srcH) {
        const float k = srcH / std::max(1e-4f, srcTop + srcBottom);
        srcTop *= k;
        srcBottom *= k;
    }

    const float uniformScale = std::max(0.01f, std::min(rectWidth / srcW, rectHeight / srcH));
    float dstLeft = srcLeft * uniformScale;
    float dstRight = srcRight * uniformScale;
    float dstTop = srcTop * uniformScale;
    float dstBottom = srcBottom * uniformScale;

    if (dstLeft + dstRight > rectWidth) {
        const float k = rectWidth / std::max(1e-4f, dstLeft + dstRight);
        dstLeft *= k;
        dstRight *= k;
    }
    if (dstTop + dstBottom > rectHeight) {
        const float k = rectHeight / std::max(1e-4f, dstTop + dstBottom);
        dstTop *= k;
        dstBottom *= k;
    }

    const float x[4] = {
        drawMin.x,
        drawMin.x + dstLeft,
        drawMax.x - dstRight,
        drawMax.x
    };
    const float y[4] = {
        drawMin.y,
        drawMin.y + dstTop,
        drawMax.y - dstBottom,
        drawMax.y
    };

    const float sx[4] = {
        0.0f,
        srcLeft / srcW,
        1.0f - srcRight / srcW,
        1.0f
    };
    const float sy[4] = {
        0.0f,
        srcTop / srcH,
        1.0f - srcBottom / srcH,
        1.0f
    };

    const float uStart = uvQuad[0].x;
    const float uEnd = uvQuad[2].x;
    const float vStart = uvQuad[0].y;
    const float vEnd = uvQuad[2].y;
    const ImVec2 center((drawMin.x + drawMax.x) * 0.5f, (drawMin.y + drawMax.y) * 0.5f);
    const float c = std::cos(angleRad);
    const float s = std::sin(angleRad);
    const bool rotate = std::abs(angleRad) > 1e-4f;

    auto remapU = [&](float t) {
        return uStart + (uEnd - uStart) * t;
    };
    auto remapV = [&](float t) {
        return vStart + (vEnd - vStart) * t;
    };
    auto rotatePoint = [&](ImVec2& point) {
        if (!rotate) return;
        const float dx = point.x - center.x;
        const float dy = point.y - center.y;
        point.x = center.x + dx * c - dy * s;
        point.y = center.y + dx * s + dy * c;
    };
    auto emit = [&](float x0, float x1, float y0, float y1, float tx0, float tx1, float ty0, float ty1) {
        if (x1 <= x0 || y1 <= y0) return;
        ImVec2 p0(x0, y0);
        ImVec2 p1(x1, y0);
        ImVec2 p2(x1, y1);
        ImVec2 p3(x0, y1);
        rotatePoint(p0);
        rotatePoint(p1);
        rotatePoint(p2);
        rotatePoint(p3);
        spriteBatch.push(textureId,
                         p0, p1, p2, p3,
                         ImVec2(remapU(tx0), remapV(ty0)),
                         ImVec2(remapU(tx1), remapV(ty0)),
                         ImVec2(remapU(tx1), remapV(ty1)),
                         ImVec2(remapU(tx0), remapV(ty1)),
                         color);
    };
    auto emitTiledX = [&](float x0, float x1, float y0, float y1,
                          float tx0, float tx1, float ty0, float ty1,
                          float tileWidth) {
        if (x1 <= x0 || y1 <= y0 || tileWidth <= 0.01f) return;
        const int tileCount = static_cast<int>(std::ceil((x1 - x0) / tileWidth));
        if (tileCount > 2048) {
            emit(x0, x1, y0, y1, tx0, tx1, ty0, ty1);
            return;
        }
        float cursor = x0;
        while (cursor < x1 - 0.001f) {
            const float next = std::min(cursor + tileWidth, x1);
            const float frac = (next - cursor) / tileWidth;
            emit(cursor, next, y0, y1, tx0, tx0 + (tx1 - tx0) * frac, ty0, ty1);
            cursor = next;
        }
    };
    auto emitTiledY = [&](float x0, float x1, float y0, float y1,
                          float tx0, float tx1, float ty0, float ty1,
                          float tileHeight) {
        if (x1 <= x0 || y1 <= y0 || tileHeight <= 0.01f) return;
        const int tileCount = static_cast<int>(std::ceil((y1 - y0) / tileHeight));
        if (tileCount > 2048) {
            emit(x0, x1, y0, y1, tx0, tx1, ty0, ty1);
            return;
        }
        float cursor = y0;
        while (cursor < y1 - 0.001f) {
            const float next = std::min(cursor + tileHeight, y1);
            const float frac = (next - cursor) / tileHeight;
            emit(x0, x1, cursor, next, tx0, tx1, ty0, ty0 + (ty1 - ty0) * frac);
            cursor = next;
        }
    };
    auto emitTiledXY = [&](float x0, float x1, float y0, float y1,
                           float tx0, float tx1, float ty0, float ty1,
                           float tileWidth, float tileHeight) {
        if (x1 <= x0 || y1 <= y0 || tileWidth <= 0.01f || tileHeight <= 0.01f) return;
        const int tilesX = static_cast<int>(std::ceil((x1 - x0) / tileWidth));
        const int tilesY = static_cast<int>(std::ceil((y1 - y0) / tileHeight));
        if (tilesX <= 0 || tilesY <= 0 || static_cast<long long>(tilesX) * static_cast<long long>(tilesY) > 2048) {
            emit(x0, x1, y0, y1, tx0, tx1, ty0, ty1);
            return;
        }
        float yCursor = y0;
        while (yCursor < y1 - 0.001f) {
            const float yNext = std::min(yCursor + tileHeight, y1);
            const float yFrac = (yNext - yCursor) / tileHeight;
            float xCursor = x0;
            while (xCursor < x1 - 0.001f) {
                const float xNext = std::min(xCursor + tileWidth, x1);
                const float xFrac = (xNext - xCursor) / tileWidth;
                emit(xCursor, xNext, yCursor, yNext,
                     tx0, tx0 + (tx1 - tx0) * xFrac,
                     ty0, ty0 + (ty1 - ty0) * yFrac);
                xCursor = xNext;
            }
            yCursor = yNext;
        }
    };

    // Corners
    emit(x[0], x[1], y[0], y[1], sx[0], sx[1], sy[0], sy[1]);
    emit(x[2], x[3], y[0], y[1], sx[2], sx[3], sy[0], sy[1]);
    emit(x[0], x[1], y[2], y[3], sx[0], sx[1], sy[2], sy[3]);
    emit(x[2], x[3], y[2], y[3], sx[2], sx[3], sy[2], sy[3]);

    // Edges
    const float centerSrcW = std::max(0.0f, srcW - srcLeft - srcRight);
    const float centerSrcH = std::max(0.0f, srcH - srcTop - srcBottom);
    const float centerDstTileW = centerSrcW * uniformScale;
    const float centerDstTileH = centerSrcH * uniformScale;

    if (obj.ui.nineSliceTileEdges && centerSrcW > 0.01f) {
        emitTiledX(x[1], x[2], y[0], y[1], sx[1], sx[2], sy[0], sy[1], centerDstTileW);
        emitTiledX(x[1], x[2], y[2], y[3], sx[1], sx[2], sy[2], sy[3], centerDstTileW);
    } else {
        emit(x[1], x[2], y[0], y[1], sx[1], sx[2], sy[0], sy[1]);
        emit(x[1], x[2], y[2], y[3], sx[1], sx[2], sy[2], sy[3]);
    }

    if (obj.ui.nineSliceTileEdges && centerSrcH > 0.01f) {
        emitTiledY(x[0], x[1], y[1], y[2], sx[0], sx[1], sy[1], sy[2], centerDstTileH);
        emitTiledY(x[2], x[3], y[1], y[2], sx[2], sx[3], sy[1], sy[2], centerDstTileH);
    } else {
        emit(x[0], x[1], y[1], y[2], sx[0], sx[1], sy[1], sy[2]);
        emit(x[2], x[3], y[1], y[2], sx[2], sx[3], sy[1], sy[2]);
    }

    // Center
    if (obj.ui.nineSliceTileCenter && centerSrcW > 0.01f && centerSrcH > 0.01f) {
        emitTiledXY(x[1], x[2], y[1], y[2], sx[1], sx[2], sy[1], sy[2], centerDstTileW, centerDstTileH);
    } else {
        emit(x[1], x[2], y[1], y[2], sx[1], sx[2], sy[1], sy[2]);
    }

    return true;
}

glm::vec2 ResolvePseudo3DLayoutSize(const SceneObject& canvas) {
    const glm::vec2 fallback(std::max(1.0f, canvas.ui.size.x), std::max(1.0f, canvas.ui.size.y));
    if (canvas.ui.pseudo3DPanelSize.x > 0.0f && canvas.ui.pseudo3DPanelSize.y > 0.0f) {
        return glm::vec2(std::max(1.0f, canvas.ui.pseudo3DPanelSize.x),
                         std::max(1.0f, canvas.ui.pseudo3DPanelSize.y));
    }
    return fallback;
}

float Cross2D(const ImVec2& a, const ImVec2& b) {
    return a.x * b.y - a.y * b.x;
}

bool PointInTriangleBarycentric(const ImVec2& p,
                                const ImVec2& a,
                                const ImVec2& b,
                                const ImVec2& c,
                                float& wa,
                                float& wb,
                                float& wc) {
    const ImVec2 v0 = ImVec2(b.x - a.x, b.y - a.y);
    const ImVec2 v1 = ImVec2(c.x - a.x, c.y - a.y);
    const ImVec2 v2 = ImVec2(p.x - a.x, p.y - a.y);
    const float denom = Cross2D(v0, v1);
    if (std::abs(denom) <= 1e-6f) {
        return false;
    }
    wb = Cross2D(v2, v1) / denom;
    wc = Cross2D(v0, v2) / denom;
    wa = 1.0f - wb - wc;
    const float eps = -1e-4f;
    return wa >= eps && wb >= eps && wc >= eps;
}

bool MapPointToPseudo3DQuadUV(const std::array<ImVec2, 4>& corners,
                              const ImVec2& point,
                              ImVec2& outUv) {
    float w0 = 0.0f;
    float w1 = 0.0f;
    float w2 = 0.0f;
    if (PointInTriangleBarycentric(point, corners[0], corners[1], corners[2], w0, w1, w2)) {
        const ImVec2 uv0(0.0f, 1.0f);
        const ImVec2 uv1(1.0f, 1.0f);
        const ImVec2 uv2(1.0f, 0.0f);
        outUv = ImVec2(
            uv0.x * w0 + uv1.x * w1 + uv2.x * w2,
            uv0.y * w0 + uv1.y * w1 + uv2.y * w2);
        return true;
    }

    if (PointInTriangleBarycentric(point, corners[0], corners[2], corners[3], w0, w1, w2)) {
        const ImVec2 uv0(0.0f, 1.0f);
        const ImVec2 uv2(1.0f, 0.0f);
        const ImVec2 uv3(0.0f, 0.0f);
        outUv = ImVec2(
            uv0.x * w0 + uv2.x * w1 + uv3.x * w2,
            uv0.y * w0 + uv2.y * w1 + uv3.y * w2);
        return true;
    }

    return false;
}

std::array<ImVec2, 4> BuildPseudo3DPanelCorners(const ImVec2& panelMin,
                                                const ImVec2& panelMax,
                                                const UIElementComponent& ui,
                                                float distanceScale,
                                                float perspectiveDistanceFactor) {
    const ImVec2 baseSize(std::max(1.0f, panelMax.x - panelMin.x),
                          std::max(1.0f, panelMax.y - panelMin.y));
    const ImVec2 pivotNorm(std::clamp(ui.pseudo3DPivot.x, 0.0f, 1.0f),
                           std::clamp(ui.pseudo3DPivot.y, 0.0f, 1.0f));
    const ImVec2 pivot(panelMin.x + baseSize.x * pivotNorm.x,
                       panelMin.y + baseSize.y * pivotNorm.y);
    const ImVec2 scaledSize(baseSize.x * std::max(0.01f, distanceScale),
                            baseSize.y * std::max(0.01f, distanceScale));
    const ImVec2 scaledMin(pivot.x - scaledSize.x * pivotNorm.x,
                           pivot.y - scaledSize.y * pivotNorm.y);
    const ImVec2 scaledMax(scaledMin.x + scaledSize.x, scaledMin.y + scaledSize.y);

    std::array<ImVec2, 4> corners = {
        ImVec2(scaledMin.x, scaledMin.y),
        ImVec2(scaledMax.x, scaledMin.y),
        ImVec2(scaledMax.x, scaledMax.y),
        ImVec2(scaledMin.x, scaledMax.y)
    };

    const float perspective = ui.pseudo3DPerspectiveIntensity * perspectiveDistanceFactor;
    const float skew = ui.pseudo3DSkewAmount;
    const float curvature = ui.pseudo3DCurvatureAmount;
    const float offsetScale = std::max(0.01f, distanceScale);
    const float halfW = scaledSize.x * 0.5f;
    const float halfH = scaledSize.y * 0.5f;

    corners[0].x += perspective * halfW;
    corners[1].x -= perspective * halfW;
    corners[2].x += perspective * halfW;
    corners[3].x -= perspective * halfW;

    corners[0].x += skew * halfH;
    corners[1].x += skew * halfH;
    corners[2].x -= skew * halfH;
    corners[3].x -= skew * halfH;

    corners[0].y -= curvature * halfH;
    corners[1].y -= curvature * halfH;
    corners[2].y += curvature * halfH;
    corners[3].y += curvature * halfH;

    corners[0].x += ui.pseudo3DTopLeftOffset.x * offsetScale;
    corners[0].y += ui.pseudo3DTopLeftOffset.y * offsetScale;
    corners[1].x += ui.pseudo3DTopRightOffset.x * offsetScale;
    corners[1].y += ui.pseudo3DTopRightOffset.y * offsetScale;
    corners[2].x += ui.pseudo3DBottomRightOffset.x * offsetScale;
    corners[2].y += ui.pseudo3DBottomRightOffset.y * offsetScale;
    corners[3].x += ui.pseudo3DBottomLeftOffset.x * offsetScale;
    corners[3].y += ui.pseudo3DBottomLeftOffset.y * offsetScale;
    return corners;
}

void ResolvePseudo3DDistanceState(const UIElementComponent& ui,
                                  float distance,
                                  float& outScale,
                                  float& outPerspectiveFactor,
                                  bool& outAllowInteraction) {
    outScale = 1.0f;
    outPerspectiveFactor = 1.0f;
    outAllowInteraction = ui.pseudo3DAllowInteraction;

    if (ui.pseudo3DDistanceScalingEnabled) {
        const float minDist = std::max(0.01f, ui.pseudo3DMinDistance);
        const float maxDist = std::max(minDist + 0.01f, ui.pseudo3DMaxDistance);
        const float t = std::clamp((distance - minDist) / (maxDist - minDist), 0.0f, 1.0f);
        outScale = 1.0f - t * 0.65f;
        if (ui.pseudo3DAdjustPerspectiveWithDistance) {
            outPerspectiveFactor = 1.0f - t;
        }
    }

    if (ui.pseudo3DInteractionDistance > 0.0f && distance > ui.pseudo3DInteractionDistance) {
        outAllowInteraction = false;
    }
}
}

#pragma region Gizmo Toolbar
namespace GizmoToolbar {
    enum class Icon {
        Translate,
        Rotate,
        Scale,
        Bounds,
        Universal,
        Mesh,
        GizmoToggle,
        GridToggle,
        SnapToggle,
        LocalMode,
        WorldMode,
        UiWorldToggle
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
        ImGui::SetNextItemAllowOverlap();
        bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(4);
        return pressed;
    }

    static void GetIconBounds(const ImVec2& min, const ImVec2& max, ImVec2& outMin, ImVec2& outMax) {
        float size = std::min(max.x - min.x, max.y - min.y);
        ImVec2 center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        outMin = ImVec2(center.x - size * 0.5f, center.y - size * 0.5f);
        outMax = ImVec2(center.x + size * 0.5f, center.y + size * 0.5f);
    }

    struct SvgPathSpec {
        const char* d;
        bool stroke;
    };

    struct SvgIconSpec {
        float viewW;
        float viewH;
        const SvgPathSpec* paths;
        int pathCount;
    };

    struct SvgSubpath {
        std::vector<ImVec2> points;
        bool closed = false;
        bool stroke = false;
    };

    struct SvgIconCache {
        bool built = false;
        std::vector<SvgSubpath> subpaths;
    };

    static bool IsCommandChar(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }

    static void SkipSvgSeparators(const char*& s) {
        while (*s) {
            if (*s == ' ' || *s == '\n' || *s == '\t' || *s == '\r' || *s == ',') {
                ++s;
                continue;
            }
            break;
        }
    }

    static bool ParseSvgNumber(const char*& s, float& out) {
        SkipSvgSeparators(s);
        if (!*s) return false;
        char* end = nullptr;
        out = strtof(s, &end);
        if (end == s) return false;
        s = end;
        return true;
    }

    static ImVec2 SvgLerp(const ImVec2& a, const ImVec2& b, float t) {
        return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    }

    static ImVec2 SvgCubic(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t) {
        ImVec2 a = SvgLerp(p0, p1, t);
        ImVec2 b = SvgLerp(p1, p2, t);
        ImVec2 c = SvgLerp(p2, p3, t);
        ImVec2 d = SvgLerp(a, b, t);
        ImVec2 e = SvgLerp(b, c, t);
        return SvgLerp(d, e, t);
    }

    static void AppendSvgCubic(std::vector<ImVec2>& pts, const ImVec2& p0, const ImVec2& p1,
                               const ImVec2& p2, const ImVec2& p3, int segments) {
        for (int i = 1; i <= segments; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(segments);
            pts.push_back(SvgCubic(p0, p1, p2, p3, t));
        }
    }

    static float SvgVectorAngle(const ImVec2& u, const ImVec2& v) {
        float dot = u.x * v.x + u.y * v.y;
        float det = u.x * v.y - u.y * v.x;
        return std::atan2(det, dot);
    }

    static void AppendSvgArc(std::vector<ImVec2>& pts, const ImVec2& start, const ImVec2& end,
                             float rx, float ry, float xAxisRotationDeg, bool largeArc, bool sweep) {
        if (rx == 0.0f || ry == 0.0f) {
            pts.push_back(end);
            return;
        }

        float phi = xAxisRotationDeg * (IM_PI / 180.0f);
        float cosPhi = std::cos(phi);
        float sinPhi = std::sin(phi);

        float dx2 = (start.x - end.x) * 0.5f;
        float dy2 = (start.y - end.y) * 0.5f;
        float x1p = cosPhi * dx2 + sinPhi * dy2;
        float y1p = -sinPhi * dx2 + cosPhi * dy2;

        rx = std::fabs(rx);
        ry = std::fabs(ry);

        float lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
        if (lambda > 1.0f) {
            float s = std::sqrt(lambda);
            rx *= s;
            ry *= s;
        }

        float rx2 = rx * rx;
        float ry2 = ry * ry;
        float x1p2 = x1p * x1p;
        float y1p2 = y1p * y1p;

        float denom = (rx2 * y1p2 + ry2 * x1p2);
        float num = rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2;
        float coef = 0.0f;
        if (denom > 0.0f) {
            float sign = (largeArc == sweep) ? -1.0f : 1.0f;
            coef = sign * std::sqrt(std::max(0.0f, num / denom));
        }

        float cxp = coef * (rx * y1p / ry);
        float cyp = coef * (-ry * x1p / rx);

        float cx = cosPhi * cxp - sinPhi * cyp + (start.x + end.x) * 0.5f;
        float cy = sinPhi * cxp + cosPhi * cyp + (start.y + end.y) * 0.5f;

        ImVec2 v1((x1p - cxp) / rx, (y1p - cyp) / ry);
        ImVec2 v2((-x1p - cxp) / rx, (-y1p - cyp) / ry);

        float startAngle = std::atan2(v1.y, v1.x);
        float deltaAngle = SvgVectorAngle(v1, v2);
        if (!sweep && deltaAngle > 0.0f) deltaAngle -= 2.0f * IM_PI;
        if (sweep && deltaAngle < 0.0f) deltaAngle += 2.0f * IM_PI;

        float absDelta = std::fabs(deltaAngle);
        int segments = std::max(4, static_cast<int>(std::ceil(absDelta / (IM_PI / 8.0f))));
        for (int i = 1; i <= segments; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(segments);
            float angle = startAngle + deltaAngle * t;
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);
            float x = cx + cosPhi * rx * cosA - sinPhi * ry * sinA;
            float y = cy + sinPhi * rx * cosA + cosPhi * ry * sinA;
            pts.push_back(ImVec2(x, y));
        }
    }

    static void FinalizeSvgSubpath(std::vector<SvgSubpath>& out, std::vector<ImVec2>& current, bool closed, bool stroke) {
        if (current.size() < 2) {
            current.clear();
            return;
        }
        bool shouldClose = closed || !stroke;
        if (shouldClose && current.size() > 2) {
            if (current.front().x != current.back().x || current.front().y != current.back().y) {
                current.push_back(current.front());
            }
        }
        SvgSubpath sub;
        sub.points = std::move(current);
        sub.closed = shouldClose;
        sub.stroke = stroke;
        out.push_back(std::move(sub));
        current.clear();
    }

    static void ParseSvgPathData(const char* d, std::vector<SvgSubpath>& out, bool stroke) {
        const char* s = d;
        char cmd = 0;
        ImVec2 cur(0, 0);
        ImVec2 start(0, 0);
        ImVec2 lastCtrl(0, 0);
        bool hasCtrl = false;
        std::vector<ImVec2> current;

        while (*s) {
            SkipSvgSeparators(s);
            if (!*s) break;
            if (IsCommandChar(*s)) {
                cmd = *s++;
            } else if (!cmd) {
                break;
            }

            bool relative = (cmd >= 'a' && cmd <= 'z');
            char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));

            if (upper == 'M') {
                float x, y;
                if (!ParseSvgNumber(s, x) || !ParseSvgNumber(s, y)) break;
                if (relative) {
                    cur.x += x;
                    cur.y += y;
                } else {
                    cur = ImVec2(x, y);
                }
                FinalizeSvgSubpath(out, current, false, stroke);
                current.push_back(cur);
                start = cur;
                hasCtrl = false;

                while (ParseSvgNumber(s, x) && ParseSvgNumber(s, y)) {
                    ImVec2 p = relative ? ImVec2(cur.x + x, cur.y + y) : ImVec2(x, y);
                    cur = p;
                    current.push_back(cur);
                }
                continue;
            }

            if (upper == 'Z') {
                FinalizeSvgSubpath(out, current, true, stroke);
                cur = start;
                hasCtrl = false;
                continue;
            }

            if (upper == 'L') {
                float x, y;
                while (ParseSvgNumber(s, x) && ParseSvgNumber(s, y)) {
                    cur = relative ? ImVec2(cur.x + x, cur.y + y) : ImVec2(x, y);
                    current.push_back(cur);
                }
                hasCtrl = false;
                continue;
            }

            if (upper == 'H') {
                float x;
                while (ParseSvgNumber(s, x)) {
                    cur.x = relative ? cur.x + x : x;
                    current.push_back(cur);
                }
                hasCtrl = false;
                continue;
            }

            if (upper == 'V') {
                float y;
                while (ParseSvgNumber(s, y)) {
                    cur.y = relative ? cur.y + y : y;
                    current.push_back(cur);
                }
                hasCtrl = false;
                continue;
            }

            if (upper == 'C') {
                float x1, y1, x2, y2, x, y;
                while (ParseSvgNumber(s, x1) && ParseSvgNumber(s, y1) &&
                       ParseSvgNumber(s, x2) && ParseSvgNumber(s, y2) &&
                       ParseSvgNumber(s, x) && ParseSvgNumber(s, y)) {
                    ImVec2 p1 = relative ? ImVec2(cur.x + x1, cur.y + y1) : ImVec2(x1, y1);
                    ImVec2 p2 = relative ? ImVec2(cur.x + x2, cur.y + y2) : ImVec2(x2, y2);
                    ImVec2 p3 = relative ? ImVec2(cur.x + x, cur.y + y) : ImVec2(x, y);
                    AppendSvgCubic(current, cur, p1, p2, p3, 12);
                    cur = p3;
                    lastCtrl = p2;
                    hasCtrl = true;
                }
                continue;
            }

            if (upper == 'S') {
                float x2, y2, x, y;
                while (ParseSvgNumber(s, x2) && ParseSvgNumber(s, y2) &&
                       ParseSvgNumber(s, x) && ParseSvgNumber(s, y)) {
                    ImVec2 p1 = hasCtrl ? ImVec2(cur.x * 2.0f - lastCtrl.x, cur.y * 2.0f - lastCtrl.y) : cur;
                    ImVec2 p2 = relative ? ImVec2(cur.x + x2, cur.y + y2) : ImVec2(x2, y2);
                    ImVec2 p3 = relative ? ImVec2(cur.x + x, cur.y + y) : ImVec2(x, y);
                    AppendSvgCubic(current, cur, p1, p2, p3, 12);
                    cur = p3;
                    lastCtrl = p2;
                    hasCtrl = true;
                }
                continue;
            }

            if (upper == 'A') {
                float rx, ry, xRot, largeFlag, sweepFlag, x, y;
                while (ParseSvgNumber(s, rx) && ParseSvgNumber(s, ry) &&
                       ParseSvgNumber(s, xRot) && ParseSvgNumber(s, largeFlag) &&
                       ParseSvgNumber(s, sweepFlag) && ParseSvgNumber(s, x) &&
                       ParseSvgNumber(s, y)) {
                    ImVec2 end = relative ? ImVec2(cur.x + x, cur.y + y) : ImVec2(x, y);
                    AppendSvgArc(current, cur, end, rx, ry, xRot, largeFlag != 0.0f, sweepFlag != 0.0f);
                    cur = end;
                    hasCtrl = false;
                }
                continue;
            }

            hasCtrl = false;
        }

        FinalizeSvgSubpath(out, current, false, stroke);
    }

    static float SvgArea(const std::vector<ImVec2>& pts) {
        if (pts.size() < 3) return 0.0f;
        float a = 0.0f;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            a += pts[i].x * pts[i + 1].y - pts[i + 1].x * pts[i].y;
        }
        return a * 0.5f;
    }

    static bool SvgPointInTri(const ImVec2& p, const ImVec2& a, const ImVec2& b, const ImVec2& c) {
        float ab = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        float bc = (c.x - b.x) * (p.y - b.y) - (c.y - b.y) * (p.x - b.x);
        float ca = (a.x - c.x) * (p.y - c.y) - (a.y - c.y) * (p.x - c.x);
        bool hasNeg = (ab < 0.0f) || (bc < 0.0f) || (ca < 0.0f);
        bool hasPos = (ab > 0.0f) || (bc > 0.0f) || (ca > 0.0f);
        return !(hasNeg && hasPos);
    }

    static void SvgTriangulate(const std::vector<ImVec2>& pts, std::vector<ImVec2>& outTris) {
        outTris.clear();
        if (pts.size() < 3) return;

        std::vector<ImVec2> poly = pts;
        if (poly.front().x == poly.back().x && poly.front().y == poly.back().y) {
            poly.pop_back();
        }
        int n = static_cast<int>(poly.size());
        if (n < 3) return;

        std::vector<int> idx(n);
        float area = SvgArea(poly);
        if (area > 0.0f) {
            for (int i = 0; i < n; ++i) idx[i] = i;
        } else {
            for (int i = 0; i < n; ++i) idx[i] = n - 1 - i;
        }

        int guard = 0;
        while (idx.size() > 2 && guard < 10000) {
            bool earFound = false;
            int m = static_cast<int>(idx.size());
            for (int i = 0; i < m; ++i) {
                int i0 = idx[(i + m - 1) % m];
                int i1 = idx[i];
                int i2 = idx[(i + 1) % m];
                const ImVec2& a = poly[i0];
                const ImVec2& b = poly[i1];
                const ImVec2& c = poly[i2];

                float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                if (cross <= 0.0f) continue;

                bool anyInside = false;
                for (int j = 0; j < m; ++j) {
                    int ii = idx[j];
                    if (ii == i0 || ii == i1 || ii == i2) continue;
                    if (SvgPointInTri(poly[ii], a, b, c)) {
                        anyInside = true;
                        break;
                    }
                }
                if (anyInside) continue;

                outTris.push_back(a);
                outTris.push_back(b);
                outTris.push_back(c);
                idx.erase(idx.begin() + i);
                earFound = true;
                break;
            }
            if (!earFound) break;
            ++guard;
        }
    }

    static void BuildSvgIconCache(const SvgIconSpec& spec, SvgIconCache& cache) {
        if (cache.built) return;
        for (int i = 0; i < spec.pathCount; ++i) {
            ParseSvgPathData(spec.paths[i].d, cache.subpaths, spec.paths[i].stroke);
        }
        cache.built = true;
    }

    static ImVec2 SvgTransformPoint(const ImVec2& p, const ImVec2& min, const ImVec2& max, float viewW, float viewH, float scaleFactor) {
        float size = std::min(max.x - min.x, max.y - min.y) * scaleFactor;
        ImVec2 center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        float scale = size / std::max(viewW, viewH);
        ImVec2 offset = ImVec2(center.x - (viewW * scale) * 0.5f, center.y - (viewH * scale) * 0.5f);
        return ImVec2(offset.x + p.x * scale, offset.y + p.y * scale);
    }

    static void DrawSvgIcon(ImDrawList* drawList, const SvgIconSpec& spec, SvgIconCache& cache,
                            const ImVec2& min, const ImVec2& max, ImU32 color, float strokeScale, float scaleFactor) {
        BuildSvgIconCache(spec, cache);
        std::vector<ImVec2> tris;
        for (const SvgSubpath& sub : cache.subpaths) {
            if (sub.points.size() < 2) continue;
            if (sub.stroke) {
                drawList->PathClear();
                for (const ImVec2& p : sub.points) {
                    drawList->PathLineTo(SvgTransformPoint(p, min, max, spec.viewW, spec.viewH, scaleFactor));
                }
                ImDrawFlags flags = sub.closed ? ImDrawFlags_Closed : 0;
                drawList->PathStroke(color, flags, strokeScale);
            } else {
                SvgTriangulate(sub.points, tris);
                if (tris.empty()) continue;
                for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                    ImVec2 a = SvgTransformPoint(tris[i], min, max, spec.viewW, spec.viewH, scaleFactor);
                    ImVec2 b = SvgTransformPoint(tris[i + 1], min, max, spec.viewW, spec.viewH, scaleFactor);
                    ImVec2 c = SvgTransformPoint(tris[i + 2], min, max, spec.viewW, spec.viewH, scaleFactor);
                    drawList->AddTriangleFilled(a, b, c, color);
                }
            }
        }
    }

    static const SvgPathSpec kTranslateSvgPaths[] = {
        { "M12 3L12.3123 2.60957L12 2.35969L11.6877 2.60957L12 3ZM11.5 9C11.5 9.27614 11.7239 9.5 12 9.5C12.2761 9.5 12.5 9.27614 12.5 9H11.5ZM16.3123 5.80957L12.3123 2.60957L11.6877 3.39043L15.6877 6.59043L16.3123 5.80957ZM11.6877 2.60957L7.68765 5.80957L8.31235 6.59043L12.3123 3.39043L11.6877 2.60957ZM11.5 3V9H12.5V3H11.5Z", true },
        { "M21 12L21.3904 12.3123L21.6403 12L21.3904 11.6877L21 12ZM15 11.5C14.7239 11.5 14.5 11.7239 14.5 12C14.5 12.2761 14.7239 12.5 15 12.5L15 11.5ZM18.1904 16.3123L21.3904 12.3123L20.6096 11.6877L17.4096 15.6877L18.1904 16.3123ZM21.3904 11.6877L18.1904 7.68765L17.4096 8.31235L20.6096 12.3123L21.3904 11.6877ZM21 11.5L15 11.5L15 12.5L21 12.5L21 11.5Z", true },
        { "M12 21L12.3123 21.3904L12 21.6403L11.6877 21.3904L12 21ZM11.5 15C11.5 14.7239 11.7239 14.5 12 14.5C12.2761 14.5 12.5 14.7239 12.5 15H11.5ZM16.3123 18.1904L12.3123 21.3904L11.6877 20.6096L15.6877 17.4096L16.3123 18.1904ZM11.6877 21.3904L7.68765 18.1904L8.31235 17.4096L12.3123 20.6096L11.6877 21.3904ZM11.5 21V15H12.5V21H11.5Z", true },
        { "M3 12L2.60957 12.3123L2.35969 12L2.60957 11.6877L3 12ZM9 11.5C9.27614 11.5 9.5 11.7239 9.5 12C9.5 12.2761 9.27614 12.5 9 12.5L9 11.5ZM5.80956 16.3123L2.60957 12.3123L3.39043 11.6877L6.59043 15.6877L5.80956 16.3123ZM2.60957 11.6877L5.80957 7.68765L6.59043 8.31235L3.39043 12.3123L2.60957 11.6877ZM3 11.5L9 11.5L9 12.5L3 12.5L3 11.5Z", true }
    };

    static const SvgPathSpec kRotateSvgPaths[] = {
        { "M11.2797426,15.9868494 L10.1464466,14.8535534 C9.95118446,14.6582912 9.95118446,14.3417088 10.1464466,14.1464466 C10.3417088,13.9511845 10.6582912,13.9511845 10.8535534,14.1464466 L12.8535534,16.1464466 C13.0488155,16.3417088 13.0488155,16.6582912 12.8535534,16.8535534 L10.8535534,18.8535534 C10.6582912,19.0488155 10.3417088,19.0488155 10.1464466,18.8535534 C9.95118446,18.6582912 9.95118446,18.3417088 10.1464466,18.1464466 L11.3044061,16.9884871 C10.3667147,16.9573314 9.46306739,16.8635462 8.61196501,16.7145167 C9.33747501,19.2936084 10.6229353,21 12,21 C14.0051086,21 15.8160018,17.3821896 15.9868494,12.7202574 L14.8535534,13.8535534 C14.6582912,14.0488155 14.3417088,14.0488155 14.1464466,13.8535534 C13.9511845,13.6582912 13.9511845,13.3417088 14.1464466,13.1464466 L16.1464466,11.1464466 C16.3417088,10.9511845 16.6582912,10.9511845 16.8535534,11.1464466 L18.8535534,13.1464466 C19.0488155,13.3417088 19.0488155,13.6582912 18.8535534,13.8535534 C18.6582912,14.0488155 18.3417088,14.0488155 18.1464466,13.8535534 L16.9884871,12.6955939 C16.8167229,17.8651676 14.7413901,22 12,22 C9.97580598,22 8.3147521,19.7456544 7.515026,16.484974 C4.2543456,15.6852479 2,14.024194 2,12 C2,9.97580598 4.2543456,8.3147521 7.515026,7.515026 C8.3147521,4.2543456 9.97580598,2 12,2 C13.5021775,2 14.8263891,3.23888365 15.7433738,5.30744582 C15.8552836,5.55989543 15.7413536,5.8552671 15.4889039,5.96717692 C15.2364543,6.07908673 14.9410827,5.96515672 14.8291729,5.71270711 C14.0550111,3.96632921 13.0221261,3 12,3 C10.6229353,3 9.33747501,4.70639159 8.61196501,7.28548333 C9.67174589,7.09991387 10.812997,7 12,7 C17.4892085,7 22,9.13669069 22,12 C22,13.5021775 20.7611164,14.8263891 18.6925542,15.7433738 C18.4401046,15.8552836 18.1447329,15.7413536 18.0328231,15.4889039 C17.9209133,15.2364543 18.0348433,14.9410827 18.2872929,14.8291729 C20.0336708,14.0550111 21,13.0221261 21,12 C21,9.89274656 17.0042017,8 12,8 C10.6991081,8 9.46636321,8.12791023 8.35424759,8.35424759 C8.12791023,9.46636321 8,10.6991081 8,12 C8,13.3008919 8.12791023,14.5336368 8.35424759,15.6457524 C9.25899447,15.8298862 10.2435788,15.9488767 11.2797426,15.9868494 Z M7.28548333,8.61196501 C4.70639159,9.33747501 3,10.6229353 3,12 C3,13.3770647 4.70639159,14.662525 7.28548333,15.388035 C7.09991387,14.3282541 7,13.187003 7,12 C7,10.812997 7.09991387,9.67174589 7.28548333,8.61196501 L7.28548333,8.61196501 Z", true }
    };

    static const SvgPathSpec kScaleSvgPaths[] = {
        { "M20,19.2928932 L20,16.5 C20,16.2238576 20.2238576,16 20.5,16 C20.7761424,16 21,16.2238576 21,16.5 L21,20.5 C21,20.7761424 20.7761424,21 20.5,21 L16.5,21 C16.2238576,21 16,20.7761424 16,20.5 C16,20.2238576 16.2238576,20 16.5,20 L19.2928932,20 L16.1464466,16.8535534 C15.9511845,16.6582912 15.9511845,16.3417088 16.1464466,16.1464466 C16.3417088,15.9511845 16.6582912,15.9511845 16.8535534,16.1464466 L20,19.2928932 Z M4,4.70710678 L4,7.5 C4,7.77614237 3.77614237,8 3.5,8 C3.22385763,8 3,7.77614237 3,7.5 L3,3.5 C3,3.22385763 3.22385763,3 3.5,3 L7.5,3 C7.77614237,3 8,3.22385763 8,3.5 C8,3.77614237 7.77614237,4 7.5,4 L4.70710678,4 L7.85355339,7.14644661 C8.04881554,7.34170876 8.04881554,7.65829124 7.85355339,7.85355339 C7.65829124,8.04881554 7.34170876,8.04881554 7.14644661,7.85355339 L4,4.70710678 Z M4.70710678,20 L7.5,20 C7.77614237,20 8,20.2238576 8,20.5 C8,20.7761424 7.77614237,21 7.5,21 L3.5,21 C3.22385763,21 3,20.7761424 3,20.5 L3,16.5 C3,16.2238576 3.22385763,16 3.5,16 C3.77614237,16 4,16.2238576 4,16.5 L4,19.2928932 L7.14644661,16.1464466 C7.34170876,15.9511845 7.65829124,15.9511845 7.85355339,16.1464466 C8.04881554,16.3417088 8.04881554,16.6582912 7.85355339,16.8535534 L4.70710678,20 Z M19.2928932,4 L16.5,4 C16.2238576,4 16,3.77614237 16,3.5 C16,3.22385763 16.2238576,3 16.5,3 L20.5,3 C20.7761424,3 21,3.22385763 21,3.5 L21,7.53112887 C21,7.80727125 20.7761424,8.03112887 20.5,8.03112887 C20.2238576,8.03112887 20,7.80727125 20,7.53112887 L20,4.70710678 L16.8535534,7.85355339 C16.6582912,8.04881554 16.3417088,8.04881554 16.1464466,7.85355339 C15.9511845,7.65829124 15.9511845,7.34170876 16.1464466,7.14644661 L19.2928932,4 L19.2928932,4 Z M8,10.4949109 C8,9.11668583 9.11540994,7.99843045 10.4936306,7.99491906 L13.4936306,7.98727573 C14.8807119,7.98726762 16,9.10655574 16,10.4872676 L16,13.5 C16,14.8807119 14.8807119,16 13.5,16 L10.5,16 C9.11928813,16 8,14.8807119 8,13.5 L8,10.4949109 Z M9,10.4949109 L9,13.5 C9,14.3284271 9.67157288,15 10.5,15 L13.5,15 C14.3284271,15 15,14.3284271 15,13.5 L15,10.4872676 C15,9.65884049 14.3284271,8.98726762 13.5,8.98726762 L10.4961784,8.99491581 C9.66924596,8.99702265 9,9.66797587 9,10.4949109 Z", true }
    };

    static const SvgPathSpec kBoundsSvgPaths[] = {
        { "M11 13.6V21H3.6C3.26863 21 3 20.7314 3 20.4V13H10.4C10.7314 13 11 13.2686 11 13.6Z", true },
        { "M11 21H14", true },
        { "M3 13V10", true },
        { "M6 3H3.6C3.26863 3 3 3.26863 3 3.6V6", true },
        { "M14 3H10", true },
        { "M21 10V14", true },
        { "M18 3H20.4C20.7314 3 21 3.26863 21 3.6V6", true },
        { "M18 21H20.4C20.7314 21 21 20.7314 21 20.4V18", true },
        { "M11 10H14V13", true }
    };

    static const SvgPathSpec kMeshSvgPaths[] = {
        { "M363.6 36.48c-22.2 0-40 17.8-40 40 0 22.23 17.8 40.02 40 40.02s40-17.79 40-40.02c0-22.2-17.8-40-40-40zm-56.7 51.97c-53.2 18.95-108.7 34.95-169 45.25 1.8 4.6 2.8 9.6 2.8 14.8 0 4.8-.8 9.4-2.4 13.6 96.2 12.9 182.8 36 257.8 71.9 1.6-5.9 4.5-11.3 8.3-15.9-71.2-34.3-152.4-57.2-241.5-70.7 53.2-10.6 102.8-25.4 150.4-42.2-3-5.2-5.2-10.79-6.4-16.75zm97.8 28.85c-4.3 4.3-9.2 8-14.6 10.8 15.3 24.8 26 50.6 31.8 77.8 4.3-1.5 9-2.4 13.8-2.4 1.4 0 2.8.1 4.1.2-6.3-30.3-18.2-59.1-35.1-86.4zm-305 8.2c-12.81 0-23 10.2-23 23s10.19 23 23 23c12.8 0 23-10.2 23-23s-10.2-23-23-23zm34.7 44.6c-3.2 5.2-7.5 9.6-12.6 12.9 32.1 32.6 66.1 65.9 120.6 80.4 0-.9-.1-1.9-.1-2.8 0-5.3 1.3-10.3 3.5-14.8-49.5-13.5-80-43.8-111.4-75.7zm-57 12.7c-21.76 67.8-27.12 137.2-32.29 206 2.13-.5 4.34-.7 6.6-.7 3.99 0 7.81.7 11.35 2.1 5.19-68.4 10.57-136 31.29-201.1-6.18-.8-11.94-3-16.95-6.3zm358.3 38.7c-12.8 0-23 10.2-23 23s10.2 23 23 23 23-10.2 23-23-10.2-23-23-23zm-41 22.2c-28.4 5.8-56.6 10.8-86 10.5.4 2.1.6 4.2.6 6.4 0 4-.7 7.9-2.1 11.5 32 .6 62-4.7 91.2-10.8-2.4-5.1-3.7-10.8-3.7-16.8zm-118.9 1.4c-8.7 0-15.5 6.8-15.5 15.5s6.8 15.5 15.5 15.5 15.5-6.8 15.5-15.5-6.8-15.5-15.5-15.5zM399 262.7c-55.6 45.9-106.6 94.4-143.1 150.7 5.9 1.8 11.2 5 15.6 9.1 34.9-53.5 84.2-100.8 138.8-145.9-4.7-3.7-8.6-8.5-11.3-13.9zm-152 15c-47.9 46.4-109.6 83.2-172.85 119.5 4.36 4.2 7.56 9.6 9.05 15.6C146.8 376.4 210 338.9 260 290.1c-5.4-2.9-9.9-7.2-13-12.4zm179.4 6.7c1.3 28.8 6 57.3 14.3 85.2 4.8-3.4 10.7-5.6 17-6-7.6-26-11.9-52.3-13.2-79.1-2.9.7-5.8 1-8.8 1-3.2 0-6.3-.4-9.3-1.1zm33.3 97.1c-8.4 0-15 6.6-15 15s6.6 15 15 15 15-6.6 15-15-6.6-15-15-15zM51.71 406.1c-8.07 0-14.42 6.4-14.42 14.4 0 8.1 6.35 14.5 14.42 14.5s14.42-6.4 14.42-14.5c0-8-6.35-14.4-14.42-14.4zm376.49.3c-44.7 24.5-93.8 32.6-144.9 35.6.9 3.4 1.4 6.9 1.4 10.5 0 2.6-.3 5.1-.7 7.5 53.1-3.1 105.8-11.6 154.3-38.5-4.7-4-8.2-9.2-10.1-15.1zM83.91 416.8c.14 1.2.22 2.4.22 3.7 0 5-1.15 9.7-3.19 14l121.86 20.3c-.1-.8-.1-1.5-.1-2.3 0-5.4 1.1-10.6 3-15.4zm159.79 12.7c-12.8 0-23 10.2-23 23s10.2 23 23 23 23-10.2 23-23-10.2-23-23-23z", true }
    };

    static const SvgPathSpec kGizmoToggleSvgPaths[] = {
        { "M2 17h1v5h5v1H2zm21 0h-1v5h-5v1h6zM3 3h5V2H2v6h1zm20-1h-6v1h5v5h1zm-9.75 12h-1.5a.75.75 0 0 1-.75-.75v-1.5a.75.75 0 0 1 .75-.75h1.5a.75.75 0 0 1 .75.75v1.5a.75.75 0 0 1-.75.75zM13 12h-1v1h1zm7 0h-5v1h5zm-10 0H5v1h5zm3 8v-5h-1v5zm-1-10h1V5h-1z", false }
    };

    static const SvgPathSpec kGridToggleSvgPaths[] = {
        { "M47.547,63.547V448.453a16,16,0,0,0,16,16H448.453a16,16,0,0,0,16-16V63.547a16,16,0,0,0-16-16H63.547A16,16,0,0,0,47.547,63.547Zm288.6,16h96.3v96.3h-96.3Zm0,128.3h96.3v96.3h-96.3Zm0,128.3h96.3v96.3h-96.3Zm-128.3-256.6h96.3v96.3h-96.3Zm0,128.3h96.3v96.3h-96.3Zm0,128.3h96.3v96.3h-96.3Zm-128.3-256.6h96.3v96.3h-96.3Zm0,128.3h96.3v96.3h-96.3Zm0,128.3h96.3v96.3h-96.3Z", true }
    };

    static const SvgPathSpec kSnapToggleSvgPaths[] = {
        { "M13.3,7.7l8.1,8.1c1.5,1.5,1.5,3.9,0,5.4c-1.5,1.5-3.9,1.5-5.4,0l-8.1-8.1l-4.7,4.7l8.1,8.1 c4.1,4.1,10.7,4.1,14.8,0s4.1-10.7,0-14.8L18,3L13.3,7.7z", true }
    };

    static const SvgPathSpec kLocalModeSvgPaths[] = {
        { "M8 10C9.10457 10 10 9.10457 10 8C10 6.89543 9.10457 6 8 6C6.89543 6 6 6.89543 6 8C6 9.10457 6.89543 10 8 10Z", false },
        { "M2.08296 7C2.50448 4.48749 4.48749 2.50448 7 2.08296V0H9V2.08296C11.5125 2.50448 13.4955 4.48749 13.917 7H16V9H13.917C13.4955 11.5125 11.5125 13.4955 9 13.917V16H7V13.917C4.48749 13.4955 2.50448 11.5125 2.08296 9H0V7H2.08296ZM4 8C4 5.79086 5.79086 4 8 4C10.2091 4 12 5.79086 12 8C12 10.2091 10.2091 12 8 12C5.79086 12 4 10.2091 4 8Z", false }
    };

    static const SvgPathSpec kWorldModeSvgPaths[] = {
        { "M19.5 6L18.0333 7.1C17.6871 7.35964 17.2661 7.5 16.8333 7.5H13.475C12.8775 7.5 12.3312 7.83761 12.064 8.37206V8.37206C11.7342 9.03161 11.9053 9.83161 12.476 10.2986L14.476 11.9349C16.0499 13.2227 16.8644 15.22 16.6399 17.2412L16.6199 17.4206C16.5403 18.1369 16.3643 18.8392 16.0967 19.5083L15.5 21", true },
        { "M2.5 10.5L5.7381 9.96032C7.09174 9.73471 8.26529 10.9083 8.03968 12.2619L7.90517 13.069C7.66434 14.514 8.3941 15.9471 9.70437 16.6022V16.6022C10.7535 17.1268 11.2976 18.3097 11.0131 19.4476L10.5 21.5", true },
        { "M12 2.5C6.75329 2.5 2.5 6.75329 2.5 12C2.5 17.2467 6.75329 21.5 12 21.5C17.2467 21.5 21.5 17.2467 21.5 12C21.5 6.75329 17.2467 2.5 12 2.5Z", true }
    };

    static const SvgPathSpec kUiWorldToggleSvgPaths[] = {
        { "M1 1 L17 1 L17 17 L1 17 L1 1 Z M20 7 L23 7 L23 23 L7 23 L7 20 L7 20", true }
    };

    static const SvgIconSpec kTranslateSvg = { 24.0f, 24.0f, kTranslateSvgPaths, 4 };
    static const SvgIconSpec kRotateSvg = { 24.0f, 24.0f, kRotateSvgPaths, 1 };
    static const SvgIconSpec kScaleSvg = { 24.0f, 24.0f, kScaleSvgPaths, 1 };
    static const SvgIconSpec kBoundsSvg = { 24.0f, 24.0f, kBoundsSvgPaths, 9 };
    static const SvgIconSpec kMeshSvg = { 512.0f, 512.0f, kMeshSvgPaths, 1 };
    static const SvgIconSpec kGizmoToggleSvg = { 20.0f, 20.0f, kGizmoToggleSvgPaths, 1 };
    static const SvgIconSpec kGridToggleSvg = { 512.0f, 512.0f, kGridToggleSvgPaths, 1 };
    static const SvgIconSpec kSnapToggleSvg = { 32.0f, 32.0f, kSnapToggleSvgPaths, 1 };
    static const SvgIconSpec kLocalModeSvg = { 16.0f, 16.0f, kLocalModeSvgPaths, 1 };
    static const SvgIconSpec kWorldModeSvg = { 24.0f, 24.0f, kWorldModeSvgPaths, 3 };
    static const SvgIconSpec kUiWorldToggleSvg = { 24.0f, 24.0f, kUiWorldToggleSvgPaths, 1 };

    static SvgIconCache gTranslateSvgCache;
    static SvgIconCache gRotateSvgCache;
    static SvgIconCache gScaleSvgCache;
    static SvgIconCache gBoundsSvgCache;
    static SvgIconCache gMeshSvgCache;
    static SvgIconCache gGizmoToggleSvgCache;
    static SvgIconCache gGridToggleSvgCache;
    static SvgIconCache gSnapToggleSvgCache;
    static SvgIconCache gLocalModeSvgCache;
    static SvgIconCache gWorldModeSvgCache;
    static SvgIconCache gUiWorldToggleSvgCache;

    static void DrawTranslateIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kTranslateSvg, gTranslateSvgCache, min, max, lineColor, 1.15f, 0.8f);
    }

    static void DrawRotateIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kRotateSvg, gRotateSvgCache, min, max, lineColor, 1.2f, 0.8f);
    }

    static void DrawScaleIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kScaleSvg, gScaleSvgCache, min, max, lineColor, 1.2f, 0.8f);
    }

    static void DrawBoundsIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        float size = std::min(max.x - min.x, max.y - min.y) * 0.8f;
        DrawSvgIcon(drawList, kBoundsSvg, gBoundsSvgCache, min, max, lineColor, size * 0.06f, 0.82f);
    }

    static void DrawUniversalIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kRotateSvg, gRotateSvgCache, min, max, lineColor, 1.1f, 0.85f);
        DrawSvgIcon(drawList, kTranslateSvg, gTranslateSvgCache, min, max, lineColor, 1.1f, 0.62f);
    }

    static void DrawMeshIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kMeshSvg, gMeshSvgCache, min, max, lineColor, 1.0f, 0.78f);
    }

    static void DrawGizmoToggleIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        ImVec2 iconMin, iconMax;
        GetIconBounds(min, max, iconMin, iconMax);
        float size = iconMax.x - iconMin.x;
        float thickness = std::max(1.0f, size * 0.08f);

        auto T = [&](float x, float y) {
            return ImVec2(iconMin.x + (x / 24.0f) * size, iconMin.y + (y / 24.0f) * size);
        };

        // Corner brackets
        drawList->AddLine(T(2, 2), T(8, 2), lineColor, thickness);
        drawList->AddLine(T(2, 2), T(2, 8), lineColor, thickness);
        drawList->AddLine(T(16, 2), T(22, 2), lineColor, thickness);
        drawList->AddLine(T(22, 2), T(22, 8), lineColor, thickness);
        drawList->AddLine(T(2, 22), T(8, 22), lineColor, thickness);
        drawList->AddLine(T(2, 16), T(2, 22), lineColor, thickness);
        drawList->AddLine(T(16, 22), T(22, 22), lineColor, thickness);
        drawList->AddLine(T(22, 16), T(22, 22), lineColor, thickness);

        // Crosshair
        drawList->AddLine(T(5, 12), T(10, 12), lineColor, thickness);
        drawList->AddLine(T(14, 12), T(19, 12), lineColor, thickness);
        drawList->AddLine(T(12, 5), T(12, 10), lineColor, thickness);
        drawList->AddLine(T(12, 14), T(12, 19), lineColor, thickness);

        // Center square
        float half = 1.1f;
        ImVec2 c = T(12, 12);
        drawList->AddRectFilled(ImVec2(c.x - (half / 24.0f) * size, c.y - (half / 24.0f) * size),
                                ImVec2(c.x + (half / 24.0f) * size, c.y + (half / 24.0f) * size),
                                lineColor, 1.5f);
    }

    static void DrawGridToggleIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kGridToggleSvg, gGridToggleSvgCache, min, max, lineColor, 0.8f, 0.72f);
    }

    static void DrawSnapToggleIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        ImVec2 iconMin, iconMax;
        GetIconBounds(min, max, iconMin, iconMax);
        float size = iconMax.x - iconMin.x;
        float thickness = std::max(1.0f, size * 0.075f);
        DrawSvgIcon(drawList, kSnapToggleSvg, gSnapToggleSvgCache, min, max, lineColor, thickness, 0.78f);

        auto T = [&](float x, float y) {
            return ImVec2(iconMin.x + (x / 32.0f) * size, iconMin.y + (y / 32.0f) * size);
        };
        auto DrawRotRect = [&](float cx, float cy, float w, float h) {
            float hx = w * 0.5f;
            float hy = h * 0.5f;
            float c = 0.70710678f;
            float s = 0.70710678f;
            ImVec2 corners[4] = {
                ImVec2(-hx, -hy),
                ImVec2(hx, -hy),
                ImVec2(hx, hy),
                ImVec2(-hx, hy)
            };
            drawList->PathClear();
            for (int i = 0; i < 4; ++i) {
                float rx = corners[i].x * c - corners[i].y * s;
                float ry = corners[i].x * s + corners[i].y * c;
                drawList->PathLineTo(T(cx + rx, cy + ry));
            }
            drawList->PathStroke(lineColor, ImDrawFlags_Closed, thickness);
        };

        DrawRotRect(6.95f, 16.8f, 6.7f, 3.8f);
        DrawRotRect(17.05f, 6.7f, 6.7f, 3.8f);
    }

    static void DrawLocalModeIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        ImVec2 iconMin, iconMax;
        GetIconBounds(min, max, iconMin, iconMax);
        ImVec2 center = ImVec2((iconMin.x + iconMax.x) * 0.5f, (iconMin.y + iconMax.y) * 0.5f);
        float size = iconMax.x - iconMin.x;
        float outerR = size * 0.36f;
        float innerR = size * 0.2f;
        float dotR = size * 0.08f;
        float thickness = std::max(1.0f, size * 0.08f);

        drawList->AddCircle(center, outerR, lineColor, 28, thickness);
        drawList->AddCircleFilled(center, dotR, lineColor, 12);

        const float tickLen = size * 0.12f;
        const float tickR = outerR + tickLen * 0.5f;
        for (int i = 0; i < 4; ++i) {
            float angle = (IM_PI * 0.5f) * static_cast<float>(i);
            ImVec2 dir(std::cos(angle), std::sin(angle));
            ImVec2 a = ImVec2(center.x + dir.x * (outerR - tickLen * 0.2f),
                              center.y + dir.y * (outerR - tickLen * 0.2f));
            ImVec2 b = ImVec2(center.x + dir.x * (outerR + tickLen),
                              center.y + dir.y * (outerR + tickLen));
            drawList->AddLine(a, b, lineColor, thickness);
        }
    }

    static void DrawWorldModeIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kWorldModeSvg, gWorldModeSvgCache, min, max, lineColor, 1.0f, 0.8f);
    }

    static void DrawUiWorldToggleIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        (void)accentColor;
        DrawSvgIcon(drawList, kUiWorldToggleSvg, gUiWorldToggleSvgCache, min, max, lineColor, 0.9f, 0.8f);

        ImVec2 iconMin, iconMax;
        GetIconBounds(min, max, iconMin, iconMax);
        float size = iconMax.x - iconMin.x;
        auto T = [&](float x, float y) {
            return ImVec2(iconMin.x + (x / 24.0f) * size, iconMin.y + (y / 24.0f) * size);
        };

        ImVec2 boxMin = T(1, 1);
        ImVec2 boxMax = T(17, 17);
        float fontSize = size * 0.38f;
        ImVec2 textSize = ImGui::CalcTextSize("2D");
        float textScale = fontSize / ImGui::GetFontSize();
        ImVec2 scaledTextSize(textSize.x * textScale, textSize.y * textScale);
        ImVec2 textPos(boxMin.x + (boxMax.x - boxMin.x - scaledTextSize.x) * 0.5f,
                       boxMin.y + (boxMax.y - boxMin.y - scaledTextSize.y) * 0.5f - size * 0.02f);

        ImFont* font = ImGui::GetFont();
        const ImVec2 offsets[] = {
            ImVec2(-0.6f, 0.0f), ImVec2(0.6f, 0.0f),
            ImVec2(0.0f, -0.6f), ImVec2(0.0f, 0.6f)
        };
        for (const ImVec2& off : offsets) {
            drawList->AddText(font, fontSize, ImVec2(textPos.x + off.x, textPos.y + off.y), lineColor, "2D");
        }
    }

    static void DrawIcon(Icon icon, ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 lineColor, ImU32 accentColor) {
        switch (icon) {
            case Icon::Translate: DrawTranslateIcon(drawList, min, max, lineColor, accentColor); break;
            case Icon::Rotate:    DrawRotateIcon(drawList, min, max, lineColor, accentColor);    break;
            case Icon::Scale:     DrawScaleIcon(drawList, min, max, lineColor, accentColor);     break;
            case Icon::Bounds:    DrawBoundsIcon(drawList, min, max, lineColor, accentColor);    break;
            case Icon::Universal: DrawUniversalIcon(drawList, min, max, lineColor, accentColor); break;
            case Icon::Mesh:      DrawMeshIcon(drawList, min, max, lineColor, accentColor);      break;
            case Icon::GizmoToggle: DrawGizmoToggleIcon(drawList, min, max, lineColor, accentColor); break;
            case Icon::GridToggle:  DrawGridToggleIcon(drawList, min, max, lineColor, accentColor);  break;
            case Icon::SnapToggle:  DrawSnapToggleIcon(drawList, min, max, lineColor, accentColor);  break;
            case Icon::LocalMode:   DrawLocalModeIcon(drawList, min, max, lineColor, accentColor);   break;
            case Icon::WorldMode:   DrawWorldModeIcon(drawList, min, max, lineColor, accentColor);   break;
            case Icon::UiWorldToggle: DrawUiWorldToggleIcon(drawList, min, max, lineColor, accentColor); break;
        }
    }

    static bool IconButton(const char* id, Icon icon, bool active, const ImVec2& size,
                           ImU32 baseColor, ImU32 hoverColor, ImU32 activeColor,
                           ImU32 accentColor, ImU32 iconColor) {
        ImGui::PushID(id);
        ImGui::SetNextItemAllowOverlap();
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

        ImDrawListFlags prevFlags = drawList->Flags;
        drawList->Flags |= ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill;
        DrawIcon(icon, drawList, min, max, iconColor, accentColor);
        drawList->Flags = prevFlags;

        ImGui::PopID();
        return pressed;
    }

    static bool TextButton(const char* id, const char* label, bool active, const ImVec2& size,
                           ImU32 baseColor, ImU32 hoverColor, ImU32 activeColor, ImU32 borderColor, ImVec4 textColor) {
        ImGui::PushID(id);
        ImGui::SetNextItemAllowOverlap();
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
        ImGui::SetNextItemAllowOverlap();
        bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(4);
        return pressed;
    }
}
#pragma endregion

namespace {
void updateDockDrawerAnimations();
}

#pragma region Game Viewport Window
void Engine::renderGameViewportWindow() {
    gameViewportFocused = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    const bool windowVisible = ImGui::Begin("Game Viewport", &showGameViewport, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    if (!windowVisible) {
        ImGui::End();
        return;
    }
    const bool showGameViewportToolbar = true;
    bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    struct GameResolutionOption {
        const char* label;
        int width;
        int height;
        bool useWindow;
        bool custom;
    };
    static const std::array<GameResolutionOption, 5> kGameResolutions = {{
        { "Default (1280x720)", 0, 0, false, false },
        { "1920x1080 (1080p)", 1920, 1080, false, false },
        { "1280x720 (720p)", 1280, 720, false, false },
        { "2560x1440 (1440p)", 2560, 1440, false, false },
        { "Custom", 0, 0, false, true }
    }};
    if (gameViewportResolutionIndex < 0 || gameViewportResolutionIndex >= (int)kGameResolutions.size()) {
        gameViewportResolutionIndex = 0;
    }
    gameViewportZoom = std::clamp(gameViewportZoom, 1.0f, 8.0f);

    static constexpr int kGameViewportPreviewSlot = 5001;

    SceneObject* playerCam = nullptr;
    for (auto& obj : sceneObjects) {
        if (obj.hasCamera && obj.camera.type == SceneCameraType::Player) {
            playerCam = &obj;
            break;
        }
    }
    const bool hasVulkanSceneTexture = usingVulkan() && vulkanRendererInitialized && (vulkanRenderer != nullptr);
    const bool project2DPipeline = isProject2DPipeline();

    const GameResolutionOption& resOption = kGameResolutions[gameViewportResolutionIndex];
    const int activeCustomWidth = std::clamp(gameViewportCustomWidth, 64, 8192);
    const int activeCustomHeight = std::clamp(gameViewportCustomHeight, 64, 8192);
    const std::string resolutionComboLabel = resOption.custom
        ? ("Custom (" + std::to_string(activeCustomWidth) + "x" + std::to_string(activeCustomHeight) + ")")
        : std::string(resOption.label);
    auto viewportDisplayModeLabel = [](ViewportDisplayMode mode) {
        switch (mode) {
            case ViewportDisplayMode::Stretch: return "Stretch";
            case ViewportDisplayMode::Fill: return "Fill";
            case ViewportDisplayMode::IntegerScale: return "Integer";
            case ViewportDisplayMode::Fit:
            default:
                return "Fit";
        }
    };
    auto toolbarSeparator = []() {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    };
    auto toolbarTooltip = [](const char* text) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", text);
        }
    };

    if (!isPlaying && showGameViewportToolbar) {
        ImGui::SetNextItemWidth(210.0f);
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::BeginCombo("##GameViewportResolution", resolutionComboLabel.c_str())) {
            for (int i = 0; i < (int)kGameResolutions.size(); ++i) {
                bool selected = (i == gameViewportResolutionIndex);
                if (ImGui::Selectable(kGameResolutions[i].label, selected)) {
                    gameViewportResolutionIndex = i;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            if (kGameResolutions[gameViewportResolutionIndex].custom) {
                ImGui::Separator();
                ImGui::TextDisabled("Custom Resolution");
                ImGui::SetNextItemWidth(160.0f);
                ImGui::DragInt("Width", &gameViewportCustomWidth, 1.0f, 64, 8192);
                ImGui::SetNextItemWidth(160.0f);
                ImGui::DragInt("Height", &gameViewportCustomHeight, 1.0f, 64, 8192);
                gameViewportCustomWidth = std::clamp(gameViewportCustomWidth, 64, 8192);
                gameViewportCustomHeight = std::clamp(gameViewportCustomHeight, 64, 8192);
            }
            ImGui::EndCombo();
        }
        toolbarTooltip("Internal render resolution");

        toolbarSeparator();
        ImGui::Checkbox("Auto Fit", &gameViewportAutoFit);

        toolbarSeparator();
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::BeginCombo("##GameViewportDisplayMode", viewportDisplayModeLabel(gameViewportDisplayMode))) {
            const ViewportDisplayMode displayModes[] = {
                ViewportDisplayMode::Fit,
                ViewportDisplayMode::Stretch,
                ViewportDisplayMode::Fill,
                ViewportDisplayMode::IntegerScale
            };
            for (ViewportDisplayMode mode : displayModes) {
                bool selected = (mode == gameViewportDisplayMode);
                if (ImGui::Selectable(viewportDisplayModeLabel(mode), selected)) {
                    gameViewportDisplayMode = mode;
                    saveEditorUserSettings();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        toolbarTooltip("Viewport display mode");

        toolbarSeparator();
        float zoomPercent = gameViewportZoom * 100.0f;
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::SliderFloat("##GameViewportZoom", &zoomPercent, 100.0f, 800.0f, "%.0f%%", ImGuiSliderFlags_Logarithmic)) {
            gameViewportZoom = std::clamp(zoomPercent / 100.0f, 1.0f, 8.0f);
        }
        toolbarTooltip("Viewport zoom");

        toolbarSeparator();
        ImGui::Checkbox("Profiler", &showGameProfiler);
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int renderWidth = kRuntimeInternalWidth;
    int renderHeight = kRuntimeInternalHeight;
    getRuntimeInternalResolution(renderWidth, renderHeight);
    gameViewportLastRenderWidth = std::max(1, renderWidth);
    gameViewportLastRenderHeight = std::max(1, renderHeight);
    const ViewportDisplayMode activeDisplayMode =
        gameViewportAutoFit
            ? gameViewportDisplayMode
            : ViewportDisplayMode::Stretch;
    const ImVec2 panelMin = ImGui::GetCursorScreenPos();
    const ImVec2 panelSize(std::max(1.0f, avail.x), std::max(1.0f, avail.y));
    const EmbeddedViewportLayout gameLayout = BuildEmbeddedViewportLayout(
        panelMin,
        panelSize,
        renderWidth,
        renderHeight,
        activeDisplayMode,
        gameViewportZoom);
    const ImVec2 frameSize = gameLayout.displaySize;

    if (!isPlaying) {
        gameViewCursorLocked = false;
    }

    if (!rendererInitialized && !hasVulkanSceneTexture) {
        ImGui::InvisibleButton("##GameViewportPanelEmpty", panelSize);
        ImVec2 imageSize = frameSize;
        ImVec2 imageMin = gameLayout.displayMin;
        ImVec2 imageMax = gameLayout.displayMax;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(imageMin, imageMax, IM_COL32(14, 18, 30, 255), 8.0f);
        drawList->AddRect(imageMin, imageMax, IM_COL32(78, 96, 128, 210), 8.0f, 0, 1.5f);

        const char* title = usingVulkan()
            ? "Vulkan Game Viewport Unavailable"
            : "Game Viewport Unavailable";
        const char* line1 = usingVulkan()
            ? "Vulkan game render target is not ready."
            : "Renderer is not initialized for this session.";
        const char* line2 = usingVulkan()
            ? "Open a project scene or retry after renderer initialization."
            : "Open or create a project to initialize rendering.";

        ImVec2 titleSize = ImGui::CalcTextSize(title);
        ImVec2 line1Size = ImGui::CalcTextSize(line1);
        ImVec2 line2Size = ImGui::CalcTextSize(line2);
        float centerX = imageMin.x + imageSize.x * 0.5f;
        float baseY = imageMin.y + imageSize.y * 0.5f - 28.0f;
        drawList->AddText(ImVec2(centerX - titleSize.x * 0.5f, baseY),
                          IM_COL32(220, 228, 244, 255),
                          title);
        drawList->AddText(ImVec2(centerX - line1Size.x * 0.5f, baseY + 24.0f),
                          IM_COL32(170, 184, 212, 255),
                          line1);
        drawList->AddText(ImVec2(centerX - line2Size.x * 0.5f, baseY + 44.0f),
                          IM_COL32(170, 184, 212, 255),
                          line2);

        gameViewportFocused = ImGui::IsWindowFocused();
    } else if (!playerCam && (rendererInitialized || hasVulkanSceneTexture)) {
        ImGui::InvisibleButton("##GameViewportPanelNoCamera", panelSize);
        ImVec2 imageSize = frameSize;
        ImVec2 imageMin = gameLayout.displayMin;
        ImVec2 imageMax = gameLayout.displayMax;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(imageMin, imageMax, IM_COL32(14, 18, 30, 255), 8.0f);
        drawList->AddRect(imageMin, imageMax, IM_COL32(78, 96, 128, 210), 8.0f, 0, 1.5f);

        const char* title = "Game Viewport Camera Missing";
        const char* line1 = "No enabled Player camera was found in the scene.";
        const char* line2 = "Create a Camera and set its type to Player.";
        ImVec2 titleSize = ImGui::CalcTextSize(title);
        ImVec2 line1Size = ImGui::CalcTextSize(line1);
        ImVec2 line2Size = ImGui::CalcTextSize(line2);
        float centerX = imageMin.x + imageSize.x * 0.5f;
        float baseY = imageMin.y + imageSize.y * 0.5f - 28.0f;
        drawList->AddText(ImVec2(centerX - titleSize.x * 0.5f, baseY),
                          IM_COL32(220, 228, 244, 255),
                          title);
        drawList->AddText(ImVec2(centerX - line1Size.x * 0.5f, baseY + 24.0f),
                          IM_COL32(170, 184, 212, 255),
                          line1);
        drawList->AddText(ImVec2(centerX - line2Size.x * 0.5f, baseY + 44.0f),
                          IM_COL32(170, 184, 212, 255),
                          line2);
        gameViewportFocused = ImGui::IsWindowFocused();
    } else if (playerCam && (rendererInitialized || hasVulkanSceneTexture)) {
        ImTextureID texId = static_cast<ImTextureID>(0);
        if (rendererInitialized) {
            unsigned int tex = renderer.renderScenePreview(
                makeCameraFromObject(*playerCam),
                sceneObjects,
                renderWidth,
                renderHeight,
                playerCam->camera.fov,
                playerCam->camera.nearClip,
                playerCam->camera.farClip,
                playerCam->camera.applyPostFX,
                kGameViewportPreviewSlot
            );
            texId = (ImTextureID)(intptr_t)tex;
        } else if (vulkanRenderer) {
            vulkanRenderer->setGameSceneSize(static_cast<uint32_t>(std::max(1, renderWidth)),
                                             static_cast<uint32_t>(std::max(1, renderHeight)));
            texId = vulkanRenderer->getGameSceneTextureID();
        }

        ImVec2 imageSize = frameSize;
        float effectiveOutputZoom = std::clamp(gameViewportZoom, 1.0f, 8.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
        ImGui::InvisibleButton("GameViewportRenderFrame", panelSize);
        ImGui::PopStyleColor(3);

        ImVec2 imageMin = gameLayout.displayMin;
        ImVec2 imageMax = gameLayout.displayMax;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(gameLayout.panelMin, gameLayout.panelMax, IM_COL32(12, 14, 20, 255), 6.0f);

        glm::vec2 hoveredPixel(0.0f);
        bool imageHovered = TryMapScreenPointToRenderPixel(
            gameLayout,
            ImGui::GetIO().MousePos,
            renderWidth,
            renderHeight,
            hoveredPixel);
        if (imageHovered) {
            ImGuiIO& io = ImGui::GetIO();
            if (io.MouseWheel != 0.0f) {
                const float factor = std::pow(1.12f, io.MouseWheel);
                gameViewportZoom = std::clamp(gameViewportZoom * factor, 1.0f, 8.0f);
                effectiveOutputZoom = std::clamp(gameViewportZoom, 1.0f, 8.0f);
            }
        }

        const EmbeddedViewportLayout outputLayout = BuildEmbeddedViewportLayout(
            panelMin,
            panelSize,
            renderWidth,
            renderHeight,
            activeDisplayMode,
            effectiveOutputZoom);
        imageSize = outputLayout.displaySize;
        imageMin = outputLayout.displayMin;
        imageMax = outputLayout.displayMax;

        if (texId != static_cast<ImTextureID>(0)) {
            if (rendererInitialized) {
                const GLuint tex = static_cast<GLuint>(texId);
                ApplyNearestTextureSampling(tex);
            }
            drawList->PushClipRect(outputLayout.panelMin, outputLayout.panelMax, true);
            drawList->AddImage(texId,
                               outputLayout.displayMin,
                               outputLayout.displayMax,
                               rendererInitialized
                                   ? ImVec2(outputLayout.uvMin.x, 1.0f - outputLayout.uvMin.y)
                                   : outputLayout.uvMin,
                               rendererInitialized
                                   ? ImVec2(outputLayout.uvMax.x, 1.0f - outputLayout.uvMax.y)
                                   : outputLayout.uvMax);
            drawList->PopClipRect();
        } else {
            drawList->AddRectFilled(imageMin,
                                    imageMax,
                                    IM_COL32(32, 36, 48, 255));
        }
        const ImVec2 renderToScreenScale = MapRenderDeltaToScreenDelta(
            outputLayout,
            renderWidth,
            renderHeight,
            glm::vec2(1.0f, 1.0f));
        float uiScaleX = renderToScreenScale.x;
        float uiScaleY = renderToScreenScale.y;
        if (showGameViewportToolbar && showCanvasOverlay) {
            ImVec2 pad(8.0f, 8.0f);
            ImVec2 tl(imageMin.x + pad.x, imageMin.y + pad.y);
            ImVec2 br(imageMax.x - pad.x, imageMax.y - pad.y);
            drawList->AddRect(tl, br, IM_COL32(110, 170, 255, 180), 8.0f, 0, 2.0f);
        }
        if (showGameViewportToolbar && showGameProfiler) {
            float fps = ImGui::GetIO().Framerate;
            float frameMs = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
            int zoomPercent = (int)std::round(effectiveOutputZoom * 100.0f);
            static const Renderer::RenderStats zeroStats{};
            static const Renderer::PostProcessStats zeroPostStats{};
            const Renderer::RenderStats& stats = rendererInitialized ? renderer.getLastPreviewStats() : zeroStats;
            const Renderer::PostProcessStats& postStats = rendererInitialized ? renderer.getLastPreviewPostStats() : zeroPostStats;

            char line1[128];
            char line2[128];
            char line3[128];
            char line4[128];
            char line5[160];
            char line6[192];
            std::snprintf(line1, sizeof(line1), "FPS: %.0f (%.1f ms)", fps, frameMs);
            std::snprintf(line2, sizeof(line2), "Batches: %d", stats.drawCalls);
            std::snprintf(line3, sizeof(line3), "Meshes: %d", stats.meshDraws);
            std::snprintf(line4, sizeof(line4), "Render: %dx%d @ %d%%", renderWidth, renderHeight, zoomPercent);
            std::snprintf(line5, sizeof(line5), "PostFX: %.2f ms | Bloom: %.2f / %.2f", postStats.totalMs, postStats.bloomExtractMs, postStats.bloomBlurMs);
            std::snprintf(line6, sizeof(line6), "ModuVolume: %s x%.2f (%d active)",
                          postStats.resolvedVolumeName.empty() ? "None" : postStats.resolvedVolumeName.c_str(),
                          postStats.resolvedBlend,
                          postStats.activeVolumeCount);

            const char* lines[] = { line1, line2, line3, line4, line5, line6 };
            float lineHeight = ImGui::GetFontSize() + 2.0f;
            float maxWidth = 0.0f;
            for (const char* line : lines) {
                ImVec2 size = ImGui::CalcTextSize(line);
                maxWidth = std::max(maxWidth, size.x);
            }
            ImVec2 pad(8.0f, 6.0f);
            ImVec2 panelMin(imageMin.x + 14.0f, imageMin.y + 14.0f);
            ImVec2 panelMax(panelMin.x + maxWidth + pad.x * 2.0f,
                            panelMin.y + lineHeight * (float)(sizeof(lines) / sizeof(lines[0])) + pad.y * 2.0f);
            ImDrawList* profilerDrawList = ImGui::GetForegroundDrawList(ImGui::GetWindowViewport());
            profilerDrawList->AddRectFilled(panelMin, panelMax, IM_COL32(18, 18, 24, 210), 6.0f);
            profilerDrawList->AddRect(panelMin, panelMax, IM_COL32(255, 255, 255, 40), 6.0f);
            for (int i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); ++i) {
                ImVec2 textPos(panelMin.x + pad.x, panelMin.y + pad.y + lineHeight * i);
                profilerDrawList->AddText(textPos, IM_COL32(235, 235, 245, 255), lines[i]);
            }
        }
        bool uiInteracting = false;
        UiSceneLookupCache uiSceneLookup(sceneObjects);
        auto find3DCanvasId = [&](const SceneObject& target) -> int {
            return uiSceneLookup.find3DCanvasId(target);
        };
        auto findPseudo3DCanvasId = [&](const SceneObject& target) -> int {
            return uiSceneLookup.findPseudo3DCanvasId(target);
        };
        auto isUiOn3DCanvas = [&](const SceneObject& target) {
            return find3DCanvasId(target) >= 0;
        };
        int editCanvas3DId = -1;
        if (SceneObject* selected = getSelectedObject()) {
            editCanvas3DId = find3DCanvasId(*selected);
        }
        auto isUIType = [&](const SceneObject& target) {
            if (!target.hasUI || target.ui.type == UIElementType::None) return false;
            int canvasId = find3DCanvasId(target);
            if (!((canvasId < 0) || (canvasId == editCanvas3DId))) {
                return false;
            }
            return findPseudo3DCanvasId(target) < 0;
        };
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetCursorScreenPos(imageMin);
        ImGui::BeginChild("GameUIOverlay",
                          ImVec2(imageMax.x - imageMin.x, imageMax.y - imageMin.y),
                          false,
                          ImGuiWindowFlags_NoTitleBar |
                          ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoBackground);
        auto anchorToPivot = [](UIAnchor anchor, const ImVec2& size) {
            switch (anchor) {
                case UIAnchor::Center: return ImVec2(size.x * 0.5f, size.y * 0.5f);
                case UIAnchor::TopLeft: return ImVec2(0.0f, 0.0f);
                case UIAnchor::TopRight: return ImVec2(size.x, 0.0f);
                case UIAnchor::BottomLeft: return ImVec2(0.0f, size.y);
                case UIAnchor::BottomRight: return ImVec2(size.x, size.y);
                default: return ImVec2(size.x * 0.5f, size.y * 0.5f);
            }
        };
        auto anchorToPoint = [](UIAnchor anchor, const ImVec2& min, const ImVec2& max) {
            switch (anchor) {
                case UIAnchor::Center: return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
                case UIAnchor::TopLeft: return min;
                case UIAnchor::TopRight: return ImVec2(max.x, min.y);
                case UIAnchor::BottomLeft: return ImVec2(min.x, max.y);
                case UIAnchor::BottomRight: return max;
                default: return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            }
        };

        auto resolveUIRect = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax, ImVec2* parentMin = nullptr, ImVec2* parentMax = nullptr) {
            std::vector<const SceneObject*> chain;
            const SceneObject* current = &obj;
            while (current) {
                if (isUIType(*current)) {
                    chain.push_back(current);
                }
                if (current->parentId < 0) break;
                auto pit = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                    [&](const SceneObject& o) { return o.id == current->parentId; });
                if (pit == sceneObjects.end()) break;
                current = &(*pit);
            }
            std::reverse(chain.begin(), chain.end());

            glm::vec2 regionMinRender(0.0f, 0.0f);
            glm::vec2 regionMaxRender(static_cast<float>(renderWidth), static_cast<float>(renderHeight));
            for (size_t idx = 0; idx < chain.size(); ++idx) {
                const SceneObject* node = chain[idx];
                if (idx + 1 == chain.size() && parentMin && parentMax) {
                    MapRenderRectToScreenRect(outputLayout,
                                              renderWidth,
                                              renderHeight,
                                              regionMinRender,
                                              regionMaxRender,
                                              *parentMin,
                                              *parentMax);
                }
                glm::vec2 nodeSizeWorld = getSpriteDisplaySize(*node);
                ImVec2 size = ImVec2(std::max(1.0f, nodeSizeWorld.x), std::max(1.0f, nodeSizeWorld.y));
                ImVec2 anchorPoint = anchorToPoint(node->ui.anchor,
                                                   ImVec2(regionMinRender.x, regionMinRender.y),
                                                   ImVec2(regionMaxRender.x, regionMaxRender.y));
                ImVec2 pivot(anchorPoint.x + node->ui.position.x, anchorPoint.y + node->ui.position.y);
                ImVec2 pivotOffset = anchorToPivot(node->ui.anchor, size);
                regionMinRender = glm::vec2(pivot.x - pivotOffset.x, pivot.y - pivotOffset.y);
                regionMaxRender = regionMinRender + glm::vec2(size.x, size.y);
            }
            MapRenderRectToScreenRect(outputLayout,
                                      renderWidth,
                                      renderHeight,
                                      regionMinRender,
                                      regionMaxRender,
                                      outMin,
                                      outMax);
        };

        ImVec2 overlayPos = ImGui::GetWindowPos();
        ImVec2 overlaySize = ImGui::GetWindowSize();
        bool allowEditorUi = false;
        bool useWorldUi = project2DPipeline || (playerCam && playerCam->camera.use2D);
        UIWorldCamera2D uiWorldCameraBackup = uiWorldCamera;
        bool restoreUiWorldCamera = false;
        if (playerCam && useWorldUi) {
            useWorldUi = true;
            restoreUiWorldCamera = true;
            uiWorldCamera.position = glm::vec2(playerCam->position.x, playerCam->position.y);
            uiWorldCamera.zoom = std::max(1.0f, playerCam->camera.pixelsPerUnit);
        }
        uiWorldPanning = false;
        if (useWorldUi) {
            uiWorldCamera.viewportSize = glm::vec2(static_cast<float>(renderWidth),
                                                   static_cast<float>(renderHeight));
        }
        Camera projectedUiCamera = playerCam ? makeCameraFromObject(*playerCam) : Camera{};
        glm::mat4 projectedUiView(1.0f);
        glm::mat4 projectedUiProj(1.0f);
        bool hasProjectedUiCamera = false;
        if (playerCam && !useWorldUi) {
            projectedUiView = projectedUiCamera.getViewMatrix();
            projectedUiProj = glm::perspective(glm::radians(playerCam->camera.fov),
                                               static_cast<float>(renderWidth) / std::max(1.0f, static_cast<float>(renderHeight)),
                                               playerCam->camera.nearClip,
                                               playerCam->camera.farClip);
            hasProjectedUiCamera = true;
        }
        auto worldToScreen = [&](const glm::vec2& world) {
            glm::vec2 renderLocal = uiWorldCamera.WorldToScreen(world);
            return MapRenderPixelToScreenPoint(outputLayout, renderWidth, renderHeight, renderLocal);
        };
        auto worldToRenderLocal = [&](const glm::vec2& world) {
            return uiWorldCamera.WorldToScreen(world);
        };
        auto screenToWorld = [&](const ImVec2& screen) {
            glm::vec2 renderLocal(0.0f);
            if (!TryMapScreenPointToRenderPixel(outputLayout, screen, renderWidth, renderHeight, renderLocal)) {
                const float normX = std::clamp(
                    (screen.x - outputLayout.displayMin.x) / std::max(1.0f, outputLayout.displaySize.x),
                    0.0f,
                    1.0f);
                const float normY = std::clamp(
                    (screen.y - outputLayout.displayMin.y) / std::max(1.0f, outputLayout.displaySize.y),
                    0.0f,
                    1.0f);
                const float sourceU =
                    outputLayout.uvMin.x + (outputLayout.uvMax.x - outputLayout.uvMin.x) * normX;
                const float sourceV =
                    outputLayout.uvMin.y + (outputLayout.uvMax.y - outputLayout.uvMin.y) * normY;
                renderLocal.x = sourceU * static_cast<float>(renderWidth);
                renderLocal.y = sourceV * static_cast<float>(renderHeight);
            }
            return uiWorldCamera.ScreenToWorld(renderLocal);
        };
        auto getWorldParentOffset = [&](const SceneObject& obj) {
            glm::vec2 offset(0.0f);
            const SceneObject* current = &obj;
            while (current && current->parentId >= 0) {
                auto pit = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                    [&](const SceneObject& o) { return o.id == current->parentId; });
                if (pit == sceneObjects.end()) break;
                current = &(*pit);
                if (current->type == ObjectType::Sprite25D) {
                    offset += glm::vec2(current->position.x, current->position.y);
                } else if (current->hasUI && current->ui.type != UIElementType::None) {
                    offset += glm::vec2(current->ui.position.x, current->ui.position.y);
                } else {
                    offset += glm::vec2(current->position.x, current->position.y);
                }
            }
            return offset;
        };
        auto parallaxOffset = [&](const SceneObject& obj) {
            if (!obj.hasParallaxLayer2D || !obj.parallaxLayer2D.enabled) return glm::vec2(0.0f);
            float factor = std::clamp(obj.parallaxLayer2D.factor, 0.0f, 1.0f);
            return uiWorldCamera.position * (1.0f - factor);
        };
        auto resolveUIRectWorld = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax) {
            if (obj.type == ObjectType::Sprite25D && hasProjectedUiCamera) {
                ImVec2 renderMin;
                ImVec2 renderMax;
                if (!ResolveProjectedSprite25DRect(obj,
                                                   projectedUiView,
                                                   projectedUiProj,
                                                   ImVec2(0.0f, 0.0f),
                                                   ImVec2(static_cast<float>(renderWidth), static_cast<float>(renderHeight)),
                                                   renderMin,
                                                   renderMax)) {
                    return false;
                }
                MapRenderRectToScreenRect(outputLayout,
                                          renderWidth,
                                          renderHeight,
                                          glm::vec2(renderMin.x, renderMin.y),
                                          glm::vec2(renderMax.x, renderMax.y),
                                          outMin,
                                          outMax);
                return true;
            }
            glm::vec2 parentOffset = getWorldParentOffset(obj);
            glm::vec2 worldPos = parentOffset + glm::vec2(obj.ui.position.x, obj.ui.position.y) + parallaxOffset(obj);
            glm::vec2 sizeWorld = getSpriteDisplaySize(obj);
            ImVec2 pivotOffset = anchorToPivot(obj.ui.anchor, ImVec2(sizeWorld.x, sizeWorld.y));
            glm::vec2 worldMin = worldPos - glm::vec2(pivotOffset.x, pivotOffset.y);
            glm::vec2 worldMax = worldMin + sizeWorld;
            ImVec2 s0 = worldToScreen(worldMin);
            ImVec2 s1 = worldToScreen(worldMax);
            outMin = ImVec2(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
            outMax = ImVec2(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
            return true;
        };
        auto resolveUIRectWorldRender = [&](const SceneObject& obj, glm::vec2& outMin, glm::vec2& outMax) {
            if (obj.type == ObjectType::Sprite25D && hasProjectedUiCamera) {
                ImVec2 renderMin;
                ImVec2 renderMax;
                if (!ResolveProjectedSprite25DRect(obj,
                                                   projectedUiView,
                                                   projectedUiProj,
                                                   ImVec2(0.0f, 0.0f),
                                                   ImVec2(static_cast<float>(renderWidth), static_cast<float>(renderHeight)),
                                                   renderMin,
                                                   renderMax)) {
                    return false;
                }
                outMin = glm::vec2(renderMin.x, renderMin.y);
                outMax = glm::vec2(renderMax.x, renderMax.y);
                return true;
            }
            glm::vec2 parentOffset = getWorldParentOffset(obj);
            glm::vec2 worldPos = parentOffset + glm::vec2(obj.ui.position.x, obj.ui.position.y) + parallaxOffset(obj);
            glm::vec2 sizeWorld = getSpriteDisplaySize(obj);
            ImVec2 pivotOffset = anchorToPivot(obj.ui.anchor, ImVec2(sizeWorld.x, sizeWorld.y));
            glm::vec2 worldMin = worldPos - glm::vec2(pivotOffset.x, pivotOffset.y);
            glm::vec2 worldMax = worldMin + sizeWorld;
            glm::vec2 s0 = worldToRenderLocal(worldMin);
            glm::vec2 s1 = worldToRenderLocal(worldMax);
            outMin = glm::vec2(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
            outMax = glm::vec2(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
            return true;
        };
        auto rectOutsideOverlay = [&](const ImVec2& min, const ImVec2& max) {
            return (max.x < overlayPos.x || min.x > overlayPos.x + overlaySize.x ||
                    max.y < overlayPos.y || min.y > overlayPos.y + overlaySize.y);
        };

        bool uiWorldHover = imageHovered || ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        bool uiWorldCameraActive = false;
        if (useWorldUi && allowEditorUi) {
            ImGuiIO& io = ImGui::GetIO();
            bool panHeld = uiWorldHover && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                (ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDown(ImGuiMouseButton_Left)));
            if (panHeld) {
                uiWorldPanning = true;
            } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
                       !(ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
                uiWorldPanning = false;
            }
            if (uiWorldPanning) {
                ImVec2 delta = io.MouseDelta;
                if (delta.x != 0.0f || delta.y != 0.0f) {
                    uiWorldCamera.position.x -= delta.x / uiWorldCamera.zoom;
                    uiWorldCamera.position.y += delta.y / uiWorldCamera.zoom;
                }
                uiWorldCameraActive = true;
            }
            if (uiWorldHover && io.MouseWheel != 0.0f) {
                glm::vec2 worldBefore = screenToWorld(io.MousePos);
                float zoomFactor = 1.0f + io.MouseWheel * 0.1f;
                float newZoom = std::clamp(uiWorldCamera.zoom * zoomFactor, 5.0f, 2000.0f);
                if (newZoom != uiWorldCamera.zoom) {
                    uiWorldCamera.zoom = newZoom;
                    glm::vec2 worldAfter = screenToWorld(io.MousePos);
                    uiWorldCamera.position += (worldBefore - worldAfter);
                    uiWorldCameraActive = true;
                }
            }
            if (uiWorldHover) {
                glm::vec2 panDir(0.0f);
                if (ImGui::IsKeyDown(ImGuiKey_A)) panDir.x -= 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_D)) panDir.x += 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_W)) panDir.y += 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_S)) panDir.y -= 1.0f;
                if (panDir.x != 0.0f || panDir.y != 0.0f) {
                    float panSpeed = 6.0f;
                    if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) {
                        panSpeed *= 2.5f;
                    }
                    uiWorldCamera.position += panDir * (panSpeed * deltaTime);
                    uiWorldCameraActive = true;
                }
            }
        }
        auto brighten = [](const ImVec4& c, float k) {
            return ImVec4(std::clamp(c.x * k, 0.0f, 1.0f),
                          std::clamp(c.y * k, 0.0f, 1.0f),
                          std::clamp(c.z * k, 0.0f, 1.0f),
                          c.w);
        };
        float animSpeed = 0.0f;
        if (uiAnimationMode == UIAnimationMode::Fluid) {
            animSpeed = 8.0f;
        } else if (uiAnimationMode == UIAnimationMode::Snappy) {
            animSpeed = 18.0f;
        }
        float animStep = (uiAnimationMode == UIAnimationMode::Off) ? 1.0f
            : (1.0f - std::exp(-animSpeed * ImGui::GetIO().DeltaTime));
        auto animateValue = [&](float& current, float target, bool immediate) {
            if (uiAnimationMode == UIAnimationMode::Off || immediate) {
                current = target;
            } else {
                current += (target - current) * animStep;
            }
            return current;
        };
        SpriteTextureResolver spriteTextureResolver(rendererInitialized ? &renderer : nullptr);

        std::vector<SceneObject*> uiDrawList;
        uiDrawList.reserve(sceneObjects.size());
        for (auto& obj : sceneObjects) {
            if (!IsObjectEnabledInHierarchy(obj) || !isUIType(obj)) continue;
            uiDrawList.push_back(&obj);
        }
        if (useWorldUi) {
            StableSortRuntimeUiDrawList(uiDrawList);
        }
        glm::vec2 worldViewMin = useWorldUi
            ? uiWorldCamera.ScreenToWorld(glm::vec2(0.0f, static_cast<float>(renderHeight)))
            : glm::vec2(0.0f);
        glm::vec2 worldViewMax = useWorldUi
            ? uiWorldCamera.ScreenToWorld(glm::vec2(static_cast<float>(renderWidth), 0.0f))
            : glm::vec2(0.0f);
        BatchedSpriteEmitter spriteBatch(ImGui::GetWindowDrawList());
        auto resolveCanvasMaskRectForObject = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax) -> bool {
            bool hasMask = false;
            ImVec2 maskMin(0.0f, 0.0f);
            ImVec2 maskMax(0.0f, 0.0f);
            const SceneObject* current = &obj;
            while (current && current->parentId >= 0) {
                current = uiSceneLookup.find(current->parentId);
                if (!current) break;
                if (!(current->hasUI && current->ui.type == UIElementType::Canvas && current->ui.maskChildren)) {
                    continue;
                }

                ImVec2 canvasMin, canvasMax;
                bool hasCanvasRect = true;
                if (useWorldUi || current->type == ObjectType::Sprite25D) {
                    hasCanvasRect = resolveUIRectWorld(*current, canvasMin, canvasMax);
                } else {
                    resolveUIRect(*current, canvasMin, canvasMax);
                }
                if (!hasCanvasRect) {
                    continue;
                }

                if (!hasMask) {
                    maskMin = canvasMin;
                    maskMax = canvasMax;
                    hasMask = true;
                } else {
                    maskMin.x = std::max(maskMin.x, canvasMin.x);
                    maskMin.y = std::max(maskMin.y, canvasMin.y);
                    maskMax.x = std::min(maskMax.x, canvasMax.x);
                    maskMax.y = std::min(maskMax.y, canvasMax.y);
                }
            }

            if (!hasMask) return false;
            outMin = maskMin;
            outMax = maskMax;
            return (outMax.x > outMin.x) && (outMax.y > outMin.y);
        };

        std::unordered_set<int> light2DRenderedObjectIds;
        bool renderedLight2DComposite = false;
        Light2DDebugStats light2DStats;
        int activeLight2DCount = 0;
        int litSprite2DCount = 0;
        int litWorldImageCount = 0;
        bool lightBufferHadContent = false;
        std::unordered_map<int, std::string> light2DRoutingReasons;
        SceneObject* selectedForRoutingReasons = showInspector ? getSelectedObject() : nullptr;
        const bool captureLight2DRoutingReasons = selectedForRoutingReasons && selectedForRoutingReasons->hasUI;
        if (captureLight2DRoutingReasons) {
            light2DRoutingReasons.reserve(uiDrawList.size());
        }
        auto setLight2DRoutingReason = [&](int objectId, const char* reason) {
            if (captureLight2DRoutingReasons) {
                light2DRoutingReasons[objectId] = reason;
            }
        };
        if (rendererInitialized) {
            Light2DRenderRequest lightRequest;
            lightRequest.width = std::max(1, renderWidth);
            lightRequest.height = std::max(1, renderHeight);
            lightRequest.clearColor = glm::vec4(0.0f);
            lightRequest.baseAmbient = glm::vec3(0.0f);
            lightRequest.lightingBufferScale = light2DLightingBufferScale;
            lightRequest.blendStyles = light2DBlendStyles;
            auto computeFlickerMultiplier = [](const Light2DFlickerSettings& flicker) {
                if (!flicker.enabled || flicker.amount <= 0.0001f) {
                    return 1.0f;
                }
                const float time = static_cast<float>(glfwGetTime());
                const float base = std::sin(time * std::max(0.01f, flicker.speed) + flicker.seed);
                const float jitter = std::sin(time * std::max(0.01f, flicker.speed * 2.173f) + flicker.seed * 1.913f);
                const float noise = 0.5f + 0.35f * base + 0.15f * jitter;
                return glm::mix(1.0f, std::max(0.0f, noise), std::clamp(flicker.amount, 0.0f, 1.0f));
            };

            int spriteDrawOrder = 0;
            for (SceneObject* objPtr : uiDrawList) {
                SceneObject& obj = *objPtr;
                if (!(obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D)) {
                    continue;
                }
                if (obj.ui.nineSliceEnabled) {
                    setLight2DRoutingReason(obj.id, "Legacy path: nine-slice sprites are not routed through Light2D yet.");
                    continue;
                }
                if (obj.ui.unlitLighting2D) {
                    setLight2DRoutingReason(obj.id, "Legacy path: Force Unlit keeps this sprite on the legacy 2D renderer.");
                    continue;
                }

                const bool repeatX = obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.repeatX;
                const bool repeatY = obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.repeatY;
                const bool disableCulling = obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.disableCulling;

                ImVec2 rectMin, rectMax;
                glm::vec2 renderRectMin(0.0f);
                glm::vec2 renderRectMax(0.0f);
                if (!resolveUIRectWorld(obj, rectMin, rectMax) ||
                    !resolveUIRectWorldRender(obj, renderRectMin, renderRectMax)) {
                    setLight2DRoutingReason(obj.id, "Legacy path: failed to resolve a world-space sprite rect for the active viewport.");
                    continue;
                }
                if (!disableCulling && !repeatX && !repeatY && rectOutsideOverlay(rectMin, rectMax)) {
                    setLight2DRoutingReason(obj.id, "Skipped Light2D: object is outside the visible 2D world overlay.");
                    continue;
                }

                Texture* spriteTex = spriteTextureResolver.resolveTexture(obj);
                if (!spriteTex || spriteTex->GetID() == 0) {
                    setLight2DRoutingReason(obj.id, "Legacy path: no sprite texture is bound for this object.");
                    continue;
                }

                std::array<ImVec2, 4> uvQuad = buildSpriteSheetUvs(obj);
                const float angle = glm::radians(obj.ui.rotation);
                const float c = std::cos(angle);
                const float s = std::sin(angle);
                ImVec2 maskMin, maskMax;
                const bool hasMaskRect = resolveCanvasMaskRectForObject(obj, maskMin, maskMax);
                auto appendSpriteQuad = [&](const ImVec2& screenQuadMin,
                                            const ImVec2& screenQuadMax,
                                            const glm::vec2& renderQuadMin,
                                            const glm::vec2& renderQuadMax) {
                    if (!disableCulling && rectOutsideOverlay(screenQuadMin, screenQuadMax)) {
                        return false;
                    }
                    if (hasMaskRect) {
                        const bool maskClipsSprite =
                            screenQuadMin.x < maskMin.x || screenQuadMax.x > maskMax.x ||
                            screenQuadMin.y < maskMin.y || screenQuadMax.y > maskMax.y;
                        if (maskClipsSprite) {
                            return false;
                        }
                    }

                    Light2DScreenSprite sprite;
                    sprite.objectId = obj.id;
                    sprite.layer = obj.layer;
                    sprite.drawOrder = spriteDrawOrder++;
                    sprite.textureId = spriteTex->GetID();
                    sprite.tint = obj.ui.color;
                    sprite.receiveLighting = obj.ui.receiveLighting2D;
                    sprite.unlit = obj.ui.unlitLighting2D;
                    sprite.emissiveIntensity = obj.ui.emissiveLighting2D;

                    const glm::vec2 center(
                        (renderQuadMin.x + renderQuadMax.x) * 0.5f,
                        (renderQuadMin.y + renderQuadMax.y) * 0.5f);
                    const glm::vec2 half(
                        std::max(0.5f, (renderQuadMax.x - renderQuadMin.x) * 0.5f),
                        std::max(0.5f, (renderQuadMax.y - renderQuadMin.y) * 0.5f));
                    auto rotatePoint = [&](float x, float y) {
                        return glm::vec2(center.x + x * c - y * s, center.y + x * s + y * c);
                    };
                    sprite.positions[0] = rotatePoint(-half.x, -half.y);
                    sprite.positions[1] = rotatePoint(half.x, -half.y);
                    sprite.positions[2] = rotatePoint(half.x, half.y);
                    sprite.positions[3] = rotatePoint(-half.x, half.y);
                    sprite.uvs[0] = glm::vec2(uvQuad[0].x, uvQuad[0].y);
                    sprite.uvs[1] = glm::vec2(uvQuad[1].x, uvQuad[1].y);
                    sprite.uvs[2] = glm::vec2(uvQuad[2].x, uvQuad[2].y);
                    sprite.uvs[3] = glm::vec2(uvQuad[3].x, uvQuad[3].y);
                    lightRequest.sprites.push_back(sprite);
                    return true;
                };

                bool addedAnySprite = false;
                if (repeatX || repeatY) {
                    glm::vec2 spriteSizeWorld = getSpriteDisplaySize(obj);
                    glm::vec2 spacing = obj.hasParallaxLayer2D ? obj.parallaxLayer2D.repeatSpacing : glm::vec2(0.0f);
                    float stepX = spriteSizeWorld.x + spacing.x;
                    float stepY = spriteSizeWorld.y + spacing.y;
                    ImVec2 pivotOffset(spriteSizeWorld.x * 0.5f, spriteSizeWorld.y * 0.5f);
                    switch (obj.ui.anchor) {
                        case UIAnchor::TopLeft: pivotOffset = ImVec2(0.0f, 0.0f); break;
                        case UIAnchor::TopRight: pivotOffset = ImVec2(spriteSizeWorld.x, 0.0f); break;
                        case UIAnchor::BottomLeft: pivotOffset = ImVec2(0.0f, spriteSizeWorld.y); break;
                        case UIAnchor::BottomRight: pivotOffset = ImVec2(spriteSizeWorld.x, spriteSizeWorld.y); break;
                        default: break;
                    }
                    glm::vec2 parentOffset = getWorldParentOffset(obj);
                    glm::vec2 worldPos = parentOffset + glm::vec2(obj.ui.position.x, obj.ui.position.y) + parallaxOffset(obj);
                    glm::vec2 baseWorldMin = worldPos - glm::vec2(pivotOffset.x, pivotOffset.y);
                    int startX = repeatX ? static_cast<int>(std::floor((worldViewMin.x - baseWorldMin.x) / stepX)) - 1 : 0;
                    int endX = repeatX ? static_cast<int>(std::ceil((worldViewMax.x - baseWorldMin.x) / stepX)) + 1 : 0;
                    int startY = repeatY ? static_cast<int>(std::floor((worldViewMin.y - baseWorldMin.y) / stepY)) - 1 : 0;
                    int endY = repeatY ? static_cast<int>(std::ceil((worldViewMax.y - baseWorldMin.y) / stepY)) + 1 : 0;
                    for (int ix = startX; ix <= endX; ++ix) {
                        for (int iy = startY; iy <= endY; ++iy) {
                            float dx = repeatX ? static_cast<float>(ix) * stepX : 0.0f;
                            float dy = repeatY ? static_cast<float>(iy) * stepY : 0.0f;
                            glm::vec2 tileMin = baseWorldMin + glm::vec2(dx, dy);
                            ImVec2 s0 = worldToScreen(tileMin);
                            ImVec2 s1 = worldToScreen(tileMin + glm::vec2(spriteSizeWorld.x, spriteSizeWorld.y));
                            glm::vec2 r0 = worldToRenderLocal(tileMin);
                            glm::vec2 r1 = worldToRenderLocal(tileMin + glm::vec2(spriteSizeWorld.x, spriteSizeWorld.y));
                            ImVec2 tileRectMin(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
                            ImVec2 tileRectMax(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
                            glm::vec2 renderTileRectMin(std::min(r0.x, r1.x), std::min(r0.y, r1.y));
                            glm::vec2 renderTileRectMax(std::max(r0.x, r1.x), std::max(r0.y, r1.y));
                            addedAnySprite = appendSpriteQuad(tileRectMin, tileRectMax, renderTileRectMin, renderTileRectMax) || addedAnySprite;
                        }
                    }
                } else {
                    addedAnySprite = appendSpriteQuad(rectMin, rectMax, renderRectMin, renderRectMax);
                }

                if (!addedAnySprite) {
                    setLight2DRoutingReason(obj.id, hasMaskRect
                        ? "Legacy path: repeating or masked tiles still use legacy rendering when the canvas clip cuts the visible tile."
                        : "Skipped Light2D: object has no visible tiles inside the current 2D world overlay.");
                    continue;
                }

                light2DRenderedObjectIds.insert(obj.id);
                if (obj.ui.type == UIElementType::Sprite2D) {
                    if (obj.ui.receiveLighting2D && !obj.ui.unlitLighting2D) {
                        ++litSprite2DCount;
                    }
                } else if (obj.ui.receiveLighting2D && !obj.ui.unlitLighting2D) {
                    ++litWorldImageCount;
                }
                if (obj.ui.receiveLighting2D && !obj.ui.unlitLighting2D) {
                    setLight2DRoutingReason(obj.id, repeatX || repeatY
                        ? "Lit path: repeating parallax tiles are routed through the Light2D compositor."
                        : "Lit path: routed through the Light2D compositor.");
                } else if (obj.ui.unlitLighting2D) {
                    setLight2DRoutingReason(obj.id, "Lit compositor path: object is routed, but Force Unlit is enabled.");
                } else {
                    setLight2DRoutingReason(obj.id, "Lit compositor path: object is routed, but Receive Lighting is disabled.");
                }
            }

            for (const SceneObject& obj : sceneObjects) {
                if (!IsObjectEnabledInHierarchy(obj) || !obj.hasLight2D || !obj.light2D.enabled) {
                    continue;
                }
                ++activeLight2DCount;

                if (obj.light2D.type == Light2DType::Global) {
                    lightRequest.baseAmbient += glm::vec3(obj.light2D.color) * obj.light2D.intensity;
                    continue;
                }

                Light2DScreenLight light;
                light.objectId = obj.id;
                light.enabled = obj.light2D.enabled;
                light.type = obj.light2D.type;
                light.blendStyle = obj.light2D.blendStyle;
                light.lightOrder = obj.light2D.lightOrder;
                light.overlapOperation = obj.light2D.overlapOperation;
                light.targetAllLayers = obj.light2D.targetAllLayers;
                light.targetLayerMask = obj.light2D.targetLayerMask;
                light.color = obj.light2D.color;
                light.intensity = obj.light2D.intensity * computeFlickerMultiplier(obj.light2D.flicker);
                light.radius = std::max(obj.light2D.radius, obj.light2D.outerRadius) * uiWorldCamera.zoom;
                light.innerRadius = obj.light2D.innerRadius * uiWorldCamera.zoom;
                light.outerRadius = std::max(obj.light2D.innerRadius, obj.light2D.outerRadius) * uiWorldCamera.zoom;
                light.falloffStrength = obj.light2D.falloffStrength;
                light.innerSpotAngle = obj.light2D.innerSpotAngle;
                light.outerSpotAngle = obj.light2D.outerSpotAngle;
                light.shadowStrength = obj.light2D.shadowStrength;
                light.volumetricEnabled = obj.light2D.volumetricEnabled;
                light.castsShadows = obj.light2D.castsShadows;
                light.rotationRad = glm::radians(obj.rotation.z);
                light.cookieScale = obj.light2D.cookieScale;
                light.cookieRotationRad = glm::radians(obj.light2D.cookieRotation);
                light.freeformFeatherPx = obj.light2D.freeformFeather * uiWorldCamera.zoom;
                light.freeformEdgeFalloff = obj.light2D.freeformEdgeFalloff;
                if (!obj.light2D.cookieTexturePath.empty()) {
                    if (Texture* cookieTexture = renderer.getTexture(obj.light2D.cookieTexturePath, MaterialProperties::TextureFilter::Bilinear)) {
                        light.cookieTextureId = cookieTexture->GetID();
                    }
                }

                glm::vec2 lightPos = worldToRenderLocal(glm::vec2(obj.position.x, obj.position.y));
                light.position = lightPos;

                if (obj.light2D.type == Light2DType::Freeform || obj.light2D.type == Light2DType::Sprite) {
                    light.polygon.reserve(obj.light2D.shapePoints.size());
                    for (const glm::vec2& point : obj.light2D.shapePoints) {
                        glm::vec2 renderPoint = worldToRenderLocal(glm::vec2(obj.position.x + point.x, obj.position.y + point.y));
                        light.polygon.emplace_back(renderPoint.x, renderPoint.y);
                    }
                    if (!light.polygon.empty()) {
                        glm::vec2 boundsMin(FLT_MAX);
                        glm::vec2 boundsMax(-FLT_MAX);
                        for (const glm::vec2& point : light.polygon) {
                            boundsMin.x = std::min(boundsMin.x, point.x);
                            boundsMin.y = std::min(boundsMin.y, point.y);
                            boundsMax.x = std::max(boundsMax.x, point.x);
                            boundsMax.y = std::max(boundsMax.y, point.y);
                        }
                        light.boundsMin = boundsMin;
                        light.boundsMax = boundsMax;
                    }
                } else {
                    const float extent = std::max(light.radius, light.outerRadius);
                    light.boundsMin = light.position - glm::vec2(extent);
                    light.boundsMax = light.position + glm::vec2(extent);
                }

                lightRequest.lights.push_back(light);
            }

            for (const SceneObject& obj : sceneObjects) {
                if (!IsObjectEnabledInHierarchy(obj) || !obj.hasShadowCaster2D || !obj.shadowCaster2D.enabled) {
                    continue;
                }

                Light2DScreenShadowCaster caster;
                caster.objectId = obj.id;
                caster.enabled = obj.shadowCaster2D.enabled;
                caster.targetAllLayers = obj.shadowCaster2D.targetAllLayers;
                caster.targetLayerMask = obj.shadowCaster2D.targetLayerMask;
                caster.shadowStrength = obj.shadowCaster2D.shadowStrength;
                caster.polygon.reserve(obj.shadowCaster2D.points.size());
                for (const glm::vec2& point : obj.shadowCaster2D.points) {
                    glm::vec2 renderPoint = worldToRenderLocal(glm::vec2(obj.position.x + point.x, obj.position.y + point.y));
                    caster.polygon.emplace_back(renderPoint.x, renderPoint.y);
                }
                if (caster.polygon.size() >= 3) {
                    lightRequest.shadowCasters.push_back(std::move(caster));
                }
            }

            const bool hasAmbientOnly = glm::length(lightRequest.baseAmbient) > 0.0001f;
            lightBufferHadContent = hasAmbientOnly || !lightRequest.lights.empty();
            if (!lightRequest.sprites.empty() && (hasAmbientOnly || !lightRequest.lights.empty())) {
                unsigned int lightTexture = lighting2DRenderer.render(lightRequest, renderer);
                if (lightTexture != 0) {
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)(intptr_t)lightTexture,
                        outputLayout.displayMin,
                        outputLayout.displayMax,
                        ImVec2(outputLayout.uvMin.x, 1.0f - outputLayout.uvMin.y),
                        ImVec2(outputLayout.uvMax.x, 1.0f - outputLayout.uvMax.y));
                    renderedLight2DComposite = true;
                    light2DStats = lighting2DRenderer.getLastStats();
                } else {
                    for (int objectId : light2DRenderedObjectIds) {
                        setLight2DRoutingReason(objectId, "Legacy path: Light2D compositor did not produce a valid output texture this frame.");
                    }
                    light2DRenderedObjectIds.clear();
                }
            } else {
                for (int objectId : light2DRenderedObjectIds) {
                    setLight2DRoutingReason(objectId, "Legacy path: no active Light2D or Global Light2D affected this frame.");
                }
                light2DRenderedObjectIds.clear();
            }
        }

        for (SceneObject* objPtr : uiDrawList) {
            SceneObject& obj = *objPtr;
            ImVec2 rectMin, rectMax;
            if (useWorldUi || obj.type == ObjectType::Sprite25D) {
                if (!resolveUIRectWorld(obj, rectMin, rectMax)) continue;
            } else {
                resolveUIRect(obj, rectMin, rectMax);
            }
            ImVec2 rectSize(rectMax.x - rectMin.x, rectMax.y - rectMin.y);
            if (rectSize.x <= 1.0f || rectSize.y <= 1.0f) continue;
            const bool disableCulling =
                obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.disableCulling;
            if (!disableCulling && rectOutsideOverlay(rectMin, rectMax)) continue;

            ImGuiStyle savedStyle = ImGui::GetStyle();
            bool styleApplied = false;
            if (!obj.ui.stylePreset.empty()) {
                if (const auto* preset = getUIStylePreset(obj.ui.stylePreset)) {
                    ImGui::GetStyle() = preset->style;
                    styleApplied = true;
                }
            }

            if (obj.ui.type == UIElementType::Canvas) {
                spriteBatch.flush();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImU32 edgeColor = obj.ui.maskChildren ? IM_COL32(74, 228, 255, 225)
                                                            : IM_COL32(110, 170, 255, 140);
                const float thickness = obj.ui.maskChildren ? 2.4f : 1.5f;
                dl->AddRect(rectMin, rectMax, edgeColor, 6.0f, 0, thickness);
                if (obj.ui.maskChildren) {
                    const float inset = 2.0f;
                    if ((rectMax.x - rectMin.x) > inset * 2.0f && (rectMax.y - rectMin.y) > inset * 2.0f) {
                        dl->AddRect(ImVec2(rectMin.x + inset, rectMin.y + inset),
                                    ImVec2(rectMax.x - inset, rectMax.y - inset),
                                    IM_COL32(32, 190, 230, 175), 5.0f, 0, 1.0f);
                    }
                }
                if (styleApplied) ImGui::GetStyle() = savedStyle;
                continue;
            }

            ImVec2 drawMin = rectMin;
            ImVec2 drawMax = rectMax;
            ImVec2 drawSize(drawMax.x - drawMin.x, drawMax.y - drawMin.y);
            ImVec2 localMin(drawMin.x - overlayPos.x, drawMin.y - overlayPos.y);
            bool pushedCanvasMask = false;
            if (obj.ui.type != UIElementType::Canvas) {
                ImVec2 maskMin, maskMax;
                if (resolveCanvasMaskRectForObject(obj, maskMin, maskMax)) {
                    maskMin.x = std::max(maskMin.x, overlayPos.x);
                    maskMin.y = std::max(maskMin.y, overlayPos.y);
                    maskMax.x = std::min(maskMax.x, overlayPos.x + overlaySize.x);
                    maskMax.y = std::min(maskMax.y, overlayPos.y + overlaySize.y);
                    if (maskMax.x <= maskMin.x || maskMax.y <= maskMin.y) {
                        if (styleApplied) ImGui::GetStyle() = savedStyle;
                        continue;
                    }
                    if (drawMax.x <= maskMin.x || drawMin.x >= maskMax.x ||
                        drawMax.y <= maskMin.y || drawMin.y >= maskMax.y) {
                        if (styleApplied) ImGui::GetStyle() = savedStyle;
                        continue;
                    }
                    spriteBatch.flush();
                    ImGui::PushClipRect(maskMin, maskMax, true);
                    pushedCanvasMask = true;
                }
            }
            ImGui::PushID(obj.id);
            UIAnimationState& animState = uiAnimationStates[obj.id];
            if (!animState.initialized) {
                animState.sliderValue = obj.ui.sliderValue;
                animState.initialized = true;
            }
            if (obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D) {
                if (light2DRenderedObjectIds.find(obj.id) != light2DRenderedObjectIds.end()) {
                    if (pushedCanvasMask) {
                        ImGui::PopClipRect();
                    }
                    ImGui::PopID();
                    if (styleApplied) ImGui::GetStyle() = savedStyle;
                    continue;
                }
                Texture* spriteTex = nullptr;
                unsigned int texId = 0;
                if (rendererInitialized && !obj.albedoTexturePath.empty()) {
                    spriteTex = renderer.getTexture(obj.albedoTexturePath, MaterialProperties::TextureFilter::Point);
                    if (spriteTex != nullptr) {
                        texId = spriteTex->GetID();
                    }
                }
                std::array<ImVec2, 4> uvQuad = buildSpriteSheetUvs(obj);
                const int frame = resolveSpriteSheetFrame(obj);
                const ImVec2 sourceFrameSizePx = ResolveUiSourceFrameSizePx(obj, frame, spriteTex);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                const ImU32 tintColor = ImGui::GetColorU32(tint);
                bool repeatX = useWorldUi && obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.repeatX;
                bool repeatY = useWorldUi && obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.repeatY;
                glm::vec2 spacing = obj.hasParallaxLayer2D ? obj.parallaxLayer2D.repeatSpacing : glm::vec2(0.0f);
                glm::vec2 spriteSizeWorld = getSpriteDisplaySize(obj);
                float stepX = spriteSizeWorld.x + spacing.x;
                float stepY = spriteSizeWorld.y + spacing.y;
                glm::vec2 baseWorldMin = worldViewMin;
                if (repeatX || repeatY) {
                    glm::vec2 sizeWorld = spriteSizeWorld;
                    ImVec2 pivotOffset = anchorToPivot(obj.ui.anchor, ImVec2(sizeWorld.x, sizeWorld.y));
                    glm::vec2 parentOffset = getWorldParentOffset(obj);
                    glm::vec2 worldPos = parentOffset + glm::vec2(obj.ui.position.x, obj.ui.position.y) + parallaxOffset(obj);
                    baseWorldMin = worldPos - glm::vec2(pivotOffset.x, pivotOffset.y);
                }
                float angle = glm::radians(obj.ui.rotation);
                auto drawImageRect = [&](const ImVec2& min, const ImVec2& max) {
                    ImVec2 size(max.x - min.x, max.y - min.y);
                    if (size.x <= 1.0f || size.y <= 1.0f) return;
                    if (DrawNineSliceSprite(spriteBatch,
                                            (ImTextureID)(intptr_t)texId,
                                            obj,
                                            min,
                                            max,
                                            uvQuad,
                                            sourceFrameSizePx,
                                            angle,
                                            tintColor)) {
                        return;
                    }
                    if (std::abs(angle) > 1e-4f) {
                        ImVec2 center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
                        ImVec2 half = ImVec2(size.x * 0.5f, size.y * 0.5f);
                        float c = std::cos(angle);
                        float s = std::sin(angle);
                        auto rotPt = [&](float x, float y) {
                            return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
                        };
                        ImVec2 p0 = rotPt(-half.x, -half.y);
                        ImVec2 p1 = rotPt( half.x, -half.y);
                        ImVec2 p2 = rotPt( half.x,  half.y);
                        ImVec2 p3 = rotPt(-half.x,  half.y);
                        if (texId != 0) {
                            spriteBatch.push((ImTextureID)(intptr_t)texId,
                                             p0, p1, p2, p3,
                                             uvQuad[0], uvQuad[1], uvQuad[2], uvQuad[3],
                                             tintColor);
                        } else {
                            spriteBatch.flush();
                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            ImU32 fill = tintColor;
                            ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                            dl->AddQuadFilled(p0, p1, p2, p3, fill);
                            dl->AddQuad(p0, p1, p2, p3, border, 2.0f);
                            ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                            ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                            dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                        }
                    } else {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        if (texId != 0) {
                            spriteBatch.push((ImTextureID)(intptr_t)texId,
                                             min,
                                             ImVec2(max.x, min.y),
                                             max,
                                             ImVec2(min.x, max.y),
                                             uvQuad[0],
                                             ImVec2(uvQuad[2].x, uvQuad[0].y),
                                             uvQuad[2],
                                             ImVec2(uvQuad[0].x, uvQuad[2].y),
                                             tintColor);
                        } else {
                            spriteBatch.flush();
                            ImU32 fill = tintColor;
                            ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                            dl->AddRectFilled(min, max, fill, 6.0f);
                            dl->AddRect(min, max, border, 6.0f);
                            ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                            ImVec2 textPos(min.x + (size.x - textSize.x) * 0.5f,
                                           min.y + (size.y - textSize.y) * 0.5f);
                            dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                        }
                    }
                };

                if (repeatX || repeatY) {
                    int startX = repeatX ? static_cast<int>(std::floor((worldViewMin.x - baseWorldMin.x) / stepX)) - 1 : 0;
                    int endX = repeatX ? static_cast<int>(std::ceil((worldViewMax.x - baseWorldMin.x) / stepX)) + 1 : 0;
                    int startY = repeatY ? static_cast<int>(std::floor((worldViewMin.y - baseWorldMin.y) / stepY)) - 1 : 0;
                    int endY = repeatY ? static_cast<int>(std::ceil((worldViewMax.y - baseWorldMin.y) / stepY)) + 1 : 0;
                    for (int ix = startX; ix <= endX; ++ix) {
                        for (int iy = startY; iy <= endY; ++iy) {
                            float dx = repeatX ? (float)ix * stepX : 0.0f;
                            float dy = repeatY ? (float)iy * stepY : 0.0f;
                            glm::vec2 tileMin = baseWorldMin + glm::vec2(dx, dy);
                            ImVec2 s0 = worldToScreen(tileMin);
                            ImVec2 s1 = worldToScreen(tileMin + glm::vec2(spriteSizeWorld.x, spriteSizeWorld.y));
                            ImVec2 tMin(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
                            ImVec2 tMax(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
                            drawImageRect(tMin, tMax);
                        }
                    }
                } else {
                    drawImageRect(drawMin, drawMax);
                }
            } else if (obj.ui.type == UIElementType::Slider) {
                spriteBatch.flush();
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                const bool uiWidgetInteractive = isPlaying && !uiWorldCameraActive && obj.ui.interactable;
                if (uiWidgetInteractive) {
                    ImGui::SetCursorPos(localMin);
                }
                if (obj.ui.sliderStyle == UISliderStyle::ImGui) {
                    float minValue = obj.ui.sliderMin;
                    float maxValue = obj.ui.sliderMax;
                    float range = (maxValue - minValue);
                    if (range <= 1e-6f) range = 1.0f;
                    if (uiWidgetInteractive) {
                        ImGui::PushItemWidth(drawSize.x);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, brighten(tint, 0.5f));
                        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, brighten(tint, 0.7f));
                        ImGui::PushStyleColor(ImGuiCol_SliderGrab, brighten(tint, 0.9f));
                        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, brighten(tint, 1.1f));
                        if (ImGui::SliderFloat(obj.ui.label.c_str(), &obj.ui.sliderValue, minValue, maxValue)) {
                            projectManager.currentProject.hasUnsavedChanges = true;
                        }
                        ImGui::PopStyleColor(5);
                        ImGui::PopItemWidth();
                    } else {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImU32 bg = ImGui::GetColorU32(ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                        ImU32 fill = ImGui::GetColorU32(tint);
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        float t = (obj.ui.sliderValue - minValue) / range;
                        t = std::clamp(t, 0.0f, 1.0f);
                        float rounding = 6.0f;
                        ImVec2 fillMax(drawMin.x + drawSize.x * t, drawMax.y);
                        dl->AddRectFilled(drawMin, drawMax, bg, rounding);
                        if (fillMax.x > drawMin.x) {
                            dl->AddRectFilled(drawMin, fillMax, fill, rounding);
                        }
                        dl->AddRect(drawMin, drawMax, border, rounding);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    }
                } else {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 bg = ImGui::GetColorU32(ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                    ImU32 fill = ImGui::GetColorU32(tint);
                    ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                    float minValue = obj.ui.sliderMin;
                    float maxValue = obj.ui.sliderMax;
                    float range = (maxValue - minValue);
                    if (range <= 1e-6f) range = 1.0f;
                    bool held = false;
                    if (uiWidgetInteractive) {
                        ImGui::InvisibleButton("##UISlider", drawSize);
                        held = ImGui::IsItemActive();
                    }
                    if (held && ImGui::IsMouseDown(ImGuiMouseButton_Left) && drawSize.x > 1.0f) {
                        float mouseT = (ImGui::GetIO().MousePos.x - drawMin.x) / drawSize.x;
                        mouseT = std::clamp(mouseT, 0.0f, 1.0f);
                        float newValue = minValue + mouseT * range;
                        if (newValue != obj.ui.sliderValue) {
                            obj.ui.sliderValue = newValue;
                            projectManager.currentProject.hasUnsavedChanges = true;
                        }
                    }

                    animateValue(animState.sliderValue, obj.ui.sliderValue, held);
                    float displayValue = (uiAnimationMode == UIAnimationMode::Off) ? obj.ui.sliderValue : animState.sliderValue;
                    float t = (displayValue - minValue) / range;
                    t = std::clamp(t, 0.0f, 1.0f);

                    if (obj.ui.sliderStyle == UISliderStyle::Fill) {
                        float rounding = 6.0f;
                        ImVec2 fillMax(drawMin.x + drawSize.x * t, drawMax.y);
                        dl->AddRectFilled(drawMin, drawMax, bg, rounding);
                        if (fillMax.x > drawMin.x) {
                            dl->AddRectFilled(drawMin, fillMax, fill, rounding);
                        }
                        dl->AddRect(drawMin, drawMax, border, rounding);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    } else if (obj.ui.sliderStyle == UISliderStyle::Circle) {
                        ImVec2 center((drawMin.x + drawMax.x) * 0.5f, (drawMin.y + drawMax.y) * 0.5f);
                        float radius = std::max(2.0f, std::min(drawSize.x, drawSize.y) * 0.5f - 2.0f);
                        dl->AddCircleFilled(center, radius, bg, 32);
                        float start = -IM_PI * 0.5f;
                        float end = start + t * IM_PI * 2.0f;
                        dl->PathClear();
                        dl->PathArcTo(center, radius, start, end, 32);
                        dl->PathLineTo(center);
                        dl->PathFillConvex(fill);
                        dl->AddCircle(center, radius, border, 32, 2.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    }
                }
            } else if (obj.ui.type == UIElementType::Button) {
                spriteBatch.flush();
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                obj.ui.buttonPressed = false;
                const bool uiWidgetInteractive = isPlaying && !uiWorldCameraActive && obj.ui.interactable;
                if (uiWidgetInteractive) {
                    ImGui::SetCursorPos(localMin);
                }
                if (obj.ui.buttonStyle == UIButtonStyle::ImGui) {
                    if (uiWidgetInteractive) {
                        ImGui::PushStyleColor(ImGuiCol_Button, tint);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brighten(tint, 1.1f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, brighten(tint, 1.2f));
                        obj.ui.buttonPressed = ImGui::Button(obj.ui.label.c_str(), drawSize);
                        ImGui::PopStyleColor(3);
                    } else {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImU32 fill = ImGui::GetColorU32(tint);
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        dl->AddRectFilled(drawMin, drawMax, fill, 6.0f);
                        dl->AddRect(drawMin, drawMax, border, 6.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    }
                } else if (obj.ui.buttonStyle == UIButtonStyle::Outline) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 border = ImGui::GetColorU32(tint);
                    bool hovered = false;
                    bool active = false;
                    if (uiWidgetInteractive) {
                        if (ImGui::InvisibleButton("##UIButton", drawSize)) {
                            obj.ui.buttonPressed = true;
                        }
                        hovered = ImGui::IsItemHovered();
                        active = ImGui::IsItemActive();
                    }
                    float hoverT = animateValue(animState.hover, hovered ? 1.0f : 0.0f, false);
                    float activeT = animateValue(animState.active, active ? 1.0f : 0.0f, false);
                    if (hoverT > 0.001f) {
                        ImVec4 hoverCol = brighten(tint, 0.45f);
                        hoverCol.w *= std::clamp(hoverT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(hoverCol), 6.0f);
                    }
                    if (activeT > 0.001f) {
                        ImVec4 activeCol = brighten(tint, 0.65f);
                        activeCol.w *= std::clamp(activeT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(activeCol), 6.0f);
                    }
                    dl->AddRect(drawMin, drawMax, border, 6.0f, 0, 2.0f);
                    ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                    ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                   drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                    dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                }
            } else if (obj.ui.type == UIElementType::Text) {
                spriteBatch.flush();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                float scale = std::max(0.1f, obj.ui.textScale);
                float scaleFactor = useWorldUi ? std::max(0.01f, uiWorldCamera.zoom / 100.0f)
                                               : std::min(uiScaleX, uiScaleY);
                float fontSize = std::max(1.0f, ImGui::GetFontSize() * scale * scaleFactor);
                ImGui::PushClipRect(drawMin, drawMax, true);
                AddUITextWithFilter(dl,
                                    obj.material.textureFilter,
                                    ImGui::GetFont(),
                                    fontSize,
                                    drawMin,
                                    drawMax,
                                    ImGui::GetColorU32(tint),
                                    obj.ui.label.c_str(),
                                    obj.ui.textAutoWrap,
                                    obj.ui.textHAlign,
                                    obj.ui.textVAlign,
                                    obj.ui.textEffectFlags,
                                    obj.ui.textEffectSpeed,
                                    obj.ui.textEffectIntensity);
                ImGui::PopClipRect();
            }
            if (pushedCanvasMask) {
                spriteBatch.flush();
                ImGui::PopClipRect();
            }
            ImGui::PopID();
            if (styleApplied) ImGui::GetStyle() = savedStyle;
        }
        spriteBatch.flush();
        if (useWorldUi) {
            light2DCompositorRanLastFrame = renderedLight2DComposite;
            light2DLightBufferHadContentLastFrame = lightBufferHadContent;
            light2DActiveCountLastFrame = activeLight2DCount;
            light2DLitSprite2DCountLastFrame = litSprite2DCount;
            light2DLitWorldImageCountLastFrame = litWorldImageCount;
            if (captureLight2DRoutingReasons) {
                light2DObjectRoutingReasonsLastFrame = std::move(light2DRoutingReasons);
            }
        }

        bool pseudoPanelInteracting = false;
        struct PseudoPanelDrawEntry {
            int canvasId = -1;
            unsigned int textureId = 0;
            ImVec2 layoutSize = ImVec2(1.0f, 1.0f);
            std::array<ImVec2, 4> corners;
            int depthSort = 0;
            bool allowInteraction = false;
        };
        std::vector<PseudoPanelDrawEntry> pseudoPanels;
        pseudoPanels.reserve(sceneObjects.size());

        auto resolvePseudoAnchorScreen = [&](const SceneObject& canvas, ImVec2& outScreen, float& outDistance) -> bool {
            outDistance = 1.0f;
            if (canvas.ui.pseudo3DAnchorTargetId < 0) {
                return false;
            }
            const SceneObject* anchorObj = uiSceneLookup.find(canvas.ui.pseudo3DAnchorTargetId);
            if (!anchorObj) {
                return false;
            }

            if (useWorldUi) {
                outScreen = worldToScreen(glm::vec2(anchorObj->position.x, anchorObj->position.y));
                outDistance = glm::length(
                    glm::vec2(uiWorldCamera.position.x - anchorObj->position.x,
                              uiWorldCamera.position.y - anchorObj->position.y));
                return true;
            }

            if (hasProjectedUiCamera &&
                ProjectWorldToOverlayPoint(anchorObj->position,
                                           projectedUiView,
                                           projectedUiProj,
                                           overlayPos,
                                           overlaySize,
                                           outScreen)) {
                outDistance = glm::length(projectedUiCamera.position - anchorObj->position);
                return true;
            }

            return false;
        };
        auto resolvePseudoCanvasRect = [&](const SceneObject& canvas,
                                           const glm::vec2& layoutSizePx,
                                           ImVec2& outMin,
                                           ImVec2& outMax) -> bool {
            std::vector<const SceneObject*> chain;
            chain.reserve(8);
            const SceneObject* current = &canvas;
            while (current) {
                if (current->hasUI && current->ui.type != UIElementType::None) {
                    const int canvas3DId = find3DCanvasId(*current);
                    if (canvas3DId < 0 || current->id == canvas.id) {
                        chain.push_back(current);
                    }
                }
                if (current->parentId < 0) break;
                current = uiSceneLookup.find(current->parentId);
                if (!current) break;
            }
            if (chain.empty()) {
                return false;
            }
            std::reverse(chain.begin(), chain.end());

            ImVec2 regionMin = overlayPos;
            ImVec2 regionMax = ImVec2(overlayPos.x + overlaySize.x, overlayPos.y + overlaySize.y);
            for (const SceneObject* node : chain) {
                ImVec2 size(1.0f, 1.0f);
                if (node->id == canvas.id) {
                    size = ImVec2(std::max(1.0f, layoutSizePx.x * uiScaleX),
                                  std::max(1.0f, layoutSizePx.y * uiScaleY));
                } else {
                    const glm::vec2 nodeSize = getSpriteDisplaySize(*node);
                    size = ImVec2(std::max(1.0f, nodeSize.x * uiScaleX),
                                  std::max(1.0f, nodeSize.y * uiScaleY));
                }
                const ImVec2 anchorPoint = anchorToPoint(node->ui.anchor, regionMin, regionMax);
                const ImVec2 pivot(anchorPoint.x + node->ui.position.x * uiScaleX,
                                   anchorPoint.y + node->ui.position.y * uiScaleY);
                const ImVec2 pivotOffset = anchorToPivot(node->ui.anchor, size);
                regionMin = ImVec2(pivot.x - pivotOffset.x, pivot.y - pivotOffset.y);
                regionMax = ImVec2(regionMin.x + size.x, regionMin.y + size.y);
            }
            outMin = regionMin;
            outMax = regionMax;
            return true;
        };

        for (auto& canvas : sceneObjects) {
            if (!IsObjectEnabledInHierarchy(canvas) ||
                !canvas.hasUI ||
                canvas.ui.type != UIElementType::Canvas ||
                canvas.ui.renderIn3D ||
                !canvas.ui.pseudo3DEnabled ||
                !canvas.ui.pseudo3DUseOffscreenSurface) {
                continue;
            }

            const glm::vec2 layoutSizePx = ResolvePseudo3DLayoutSize(canvas);
            ImVec2 rectMin;
            ImVec2 rectMax;
            if (!resolvePseudoCanvasRect(canvas, layoutSizePx, rectMin, rectMax)) {
                continue;
            }
            const int targetWidth = std::clamp(
                (canvas.ui.renderTargetSize.x > 0) ? canvas.ui.renderTargetSize.x : static_cast<int>(layoutSizePx.x),
                16,
                4096);
            const int targetHeight = std::clamp(
                (canvas.ui.renderTargetSize.y > 0) ? canvas.ui.renderTargetSize.y : static_cast<int>(layoutSizePx.y),
                16,
                4096);
            Renderer::UiTargetInfo target = renderer.ensureUiTarget(canvas.id, targetWidth, targetHeight);
            if (target.texture == 0) {
                continue;
            }

            float distance = 1.0f;
            ImVec2 anchorScreen(0.0f, 0.0f);
            const bool anchored = resolvePseudoAnchorScreen(canvas, anchorScreen, distance);
            if (!anchored) {
                if (useWorldUi) {
                    distance = glm::length(
                        glm::vec2(uiWorldCamera.position.x - canvas.position.x,
                                  uiWorldCamera.position.y - canvas.position.y));
                } else if (hasProjectedUiCamera) {
                    distance = glm::length(projectedUiCamera.position - canvas.position);
                }
            }

            if (anchored) {
                const ImVec2 center((rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y) * 0.5f);
                const ImVec2 shift(anchorScreen.x - center.x, anchorScreen.y - center.y);
                rectMin = ImVec2(rectMin.x + shift.x, rectMin.y + shift.y);
                rectMax = ImVec2(rectMax.x + shift.x, rectMax.y + shift.y);
            }

            float distanceScale = 1.0f;
            float perspectiveFactor = 1.0f;
            bool allowInteraction = false;
            ResolvePseudo3DDistanceState(canvas.ui, distance, distanceScale, perspectiveFactor, allowInteraction);

            PseudoPanelDrawEntry entry;
            entry.canvasId = canvas.id;
            entry.textureId = target.texture;
            entry.layoutSize = ImVec2(layoutSizePx.x, layoutSizePx.y);
            entry.corners = BuildPseudo3DPanelCorners(rectMin, rectMax, canvas.ui, distanceScale, perspectiveFactor);
            entry.depthSort = canvas.ui.pseudo3DDepthSort;
            entry.allowInteraction = allowInteraction;
            pseudoPanels.push_back(entry);
        }

        if (!pseudoPanels.empty()) {
            std::stable_sort(pseudoPanels.begin(), pseudoPanels.end(),
                             [](const PseudoPanelDrawEntry& a, const PseudoPanelDrawEntry& b) {
                                 if (a.depthSort != b.depthSort) return a.depthSort < b.depthSort;
                                 return a.canvasId < b.canvasId;
                             });

            ImDrawList* panelDrawList = ImGui::GetWindowDrawList();
            for (const PseudoPanelDrawEntry& panel : pseudoPanels) {
                panelDrawList->AddImageQuad(
                    (ImTextureID)(intptr_t)panel.textureId,
                    panel.corners[0], panel.corners[1], panel.corners[2], panel.corners[3],
                    ImVec2(0.0f, 1.0f), ImVec2(1.0f, 1.0f), ImVec2(1.0f, 0.0f), ImVec2(0.0f, 0.0f),
                    IM_COL32_WHITE);
            }

            if (imageHovered && !uiWorldCameraActive) {
                const ImVec2 mousePos = ImGui::GetIO().MousePos;
                bool inputAssigned = false;
                for (auto it = pseudoPanels.rbegin(); it != pseudoPanels.rend(); ++it) {
                    ImVec2 uv(0.0f, 0.0f);
                    if (!MapPointToPseudo3DQuadUV(it->corners, mousePos, uv)) {
                        continue;
                    }

                    pseudoPanelInteracting = pseudoPanelInteracting ||
                        ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                        ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                        std::abs(ImGui::GetIO().MouseWheel) > 0.0f;
                    if (!it->allowInteraction || inputAssigned) {
                        continue;
                    }

                    UiCanvas3DInput& input = uiCanvas3DInputs[it->canvasId];
                    const float u = std::clamp(uv.x, 0.0f, 1.0f);
                    const float v = std::clamp(uv.y, 0.0f, 1.0f);
                    input.mousePos = ImVec2(
                        u * std::max(1.0f, it->layoutSize.x),
                        (1.0f - v) * std::max(1.0f, it->layoutSize.y));
                    input.mouseDown[0] = ImGui::GetIO().MouseDown[0];
                    input.mouseDown[1] = ImGui::GetIO().MouseDown[1];
                    input.mouseDown[2] = ImGui::GetIO().MouseDown[2];
                    input.mouseWheel = ImGui::GetIO().MouseWheel;
                    input.hasInput = true;
                    input.hitT = -1000.0f - static_cast<float>(it->depthSort);
                    inputAssigned = true;
                }
            }
        }

        bool gizmoUsed = false;
        if (allowEditorUi && !isPlaying) {
            SceneObject* selected = getSelectedObject();
            if (selected && isUIType(*selected) && selected->ui.type != UIElementType::Canvas) {
                ImVec2 rectMin, rectMax;
                ImVec2 parentMin, parentMax;
                bool haveRect = true;
                if (useWorldUi || selected->type == ObjectType::Sprite25D) {
                    haveRect = resolveUIRectWorld(*selected, rectMin, rectMax);
                } else {
                    resolveUIRect(*selected, rectMin, rectMax, &parentMin, &parentMax);
                }
                if (haveRect) {
                    auto anchorToPivotUI = [](UIAnchor anchor, const ImVec2& size) {
                        switch (anchor) {
                            case UIAnchor::TopLeft: return ImVec2(0.0f, 0.0f);
                            case UIAnchor::TopRight: return ImVec2(size.x, 0.0f);
                            case UIAnchor::BottomLeft: return ImVec2(0.0f, size.y);
                            case UIAnchor::BottomRight: return ImVec2(size.x, size.y);
                            default: return ImVec2(size.x * 0.5f, size.y * 0.5f);
                        }
                    };

                    std::vector<int> worldUiRoots;
                    ImVec2 worldUiBoundsMin = rectMin;
                    ImVec2 worldUiBoundsMax = rectMax;
                    if (useWorldUi) {
                        std::vector<int> candidateIds;
                        if (!selectedObjectIds.empty()) {
                            candidateIds = selectedObjectIds;
                        } else if (selectedObjectId >= 0) {
                            candidateIds.push_back(selectedObjectId);
                        }
                        if (candidateIds.empty()) {
                            candidateIds.push_back(selected->id);
                        }

                        std::vector<int> validIds;
                        validIds.reserve(candidateIds.size());
                        for (int id : candidateIds) {
                            SceneObject* candidate = findObjectById(id);
                            if (!candidate || !IsObjectEnabledInHierarchy(*candidate)) continue;
                            if (!isUIType(*candidate) || candidate->ui.type == UIElementType::Canvas) continue;
                            ImVec2 candidateMin, candidateMax;
                            if (!resolveUIRectWorld(*candidate, candidateMin, candidateMax)) continue;
                            validIds.push_back(id);
                        }
                        if (validIds.empty()) {
                            validIds.push_back(selected->id);
                        }

                        std::unordered_set<int> selectedSet(validIds.begin(), validIds.end());
                        auto hasSelectedAncestor = [&](int id) {
                            SceneObject* current = findObjectById(id);
                            int parentId = current ? current->parentId : -1;
                            while (parentId != -1) {
                                if (selectedSet.count(parentId) > 0) {
                                    return true;
                                }
                                SceneObject* parent = findObjectById(parentId);
                                parentId = parent ? parent->parentId : -1;
                            }
                            return false;
                        };

                        worldUiRoots.reserve(validIds.size());
                        for (int id : validIds) {
                            if (!hasSelectedAncestor(id)) {
                                worldUiRoots.push_back(id);
                            }
                        }
                        if (worldUiRoots.empty()) {
                            worldUiRoots = validIds;
                        }

                        ImVec2 boundsMin(FLT_MAX, FLT_MAX);
                        ImVec2 boundsMax(-FLT_MAX, -FLT_MAX);
                        for (int id : worldUiRoots) {
                            SceneObject* target = findObjectById(id);
                            if (!target) continue;
                            ImVec2 targetMin, targetMax;
                            if (!resolveUIRectWorld(*target, targetMin, targetMax)) continue;
                            boundsMin.x = std::min(boundsMin.x, targetMin.x);
                            boundsMin.y = std::min(boundsMin.y, targetMin.y);
                            boundsMax.x = std::max(boundsMax.x, targetMax.x);
                            boundsMax.y = std::max(boundsMax.y, targetMax.y);
                        }
                        if (boundsMin.x != FLT_MAX && boundsMin.y != FLT_MAX) {
                            worldUiBoundsMin = boundsMin;
                            worldUiBoundsMax = boundsMax;
                            rectMin = boundsMin;
                            rectMax = boundsMax;
                        } else {
                            worldUiRoots.clear();
                            worldUiRoots.push_back(selected->id);
                        }
                    }

                    ImVec2 rectSize(rectMax.x - rectMin.x, rectMax.y - rectMin.y);

                    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
                    if (mCurrentGizmoOperation == ImGuizmo::SCALE) {
                        op = ImGuizmo::SCALE;
                    } else if (mCurrentGizmoOperation == ImGuizmo::BOUNDS) {
                        op = ImGuizmo::BOUNDS;
                    } else if (mCurrentGizmoOperation == ImGuizmo::ROTATE) {
                        op = ImGuizmo::ROTATE;
                    }
                    glm::mat4 view(1.0f);
                    glm::mat4 proj = glm::ortho(0.0f, (float)(imageMax.x - imageMin.x),
                                                (float)(imageMax.y - imageMin.y), 0.0f, -1.0f, 1.0f);
                    glm::vec2 parentOffset = getWorldParentOffset(*selected);
                    glm::vec2 pivotWorld = parentOffset + glm::vec2(selected->ui.position.x, selected->ui.position.y);
                    ImVec2 pivotScreen;
                    if (useWorldUi) {
                        if (worldUiRoots.size() > 1) {
                            pivotScreen = ImVec2((worldUiBoundsMin.x + worldUiBoundsMax.x) * 0.5f,
                                                 (worldUiBoundsMin.y + worldUiBoundsMax.y) * 0.5f);
                        } else {
                            pivotScreen = worldToScreen(pivotWorld);
                        }
                    } else {
                        ImVec2 anchorPoint = anchorToPoint(selected->ui.anchor, parentMin, parentMax);
                        pivotScreen = ImVec2(anchorPoint.x + selected->ui.position.x * uiScaleX,
                                             anchorPoint.y + selected->ui.position.y * uiScaleY);
                    }
                    ImVec2 rectCenter = (op == ImGuizmo::SCALE || op == ImGuizmo::BOUNDS)
                        ? ImVec2((rectMin.x + rectMax.x) * 0.5f - imageMin.x,
                                 (rectMin.y + rectMax.y) * 0.5f - imageMin.y)
                        : ImVec2(pivotScreen.x - imageMin.x, pivotScreen.y - imageMin.y);
                    glm::vec3 gizmoScale(1.0f, 1.0f, 1.0f);
                    if (op == ImGuizmo::SCALE || op == ImGuizmo::BOUNDS) {
                        gizmoScale = glm::vec3(rectSize.x, rectSize.y, 1.0f);
                    }
                    glm::mat4 model(1.0f);
                    model = glm::translate(model, glm::vec3(rectCenter.x, rectCenter.y, 0.0f));
                    model = glm::rotate(model, glm::radians(selected->ui.rotation), glm::vec3(0.0f, 0.0f, 1.0f));
                    model = glm::scale(model, gizmoScale);
                    const bool stableRectScale = (op == ImGuizmo::SCALE || op == ImGuizmo::BOUNDS);
                    if (stableRectScale && gameUiGizmoHistoryCaptured &&
                        gameUiRectGizmoOperation == op && !gameUiRectGizmoSnapshots.empty()) {
                        model = gameUiRectGizmoModel;
                    }
                    const glm::mat4 originalModel = model;

                    ImGuizmo::BeginFrame();
                    ImGuizmo::Enable(true);
                    ImGuizmo::SetOrthographic(true);
                    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
                    ImGuizmo::SetRect(imageMin.x, imageMin.y, imageMax.x - imageMin.x, imageMax.y - imageMin.y);
                    glm::mat4 delta(1.0f);
                    float bounds[6] = { -0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.0f };
                    const float* boundsPtr = (op == ImGuizmo::BOUNDS) ? bounds : nullptr;
                    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, ImGuizmo::LOCAL,
                                         glm::value_ptr(model), glm::value_ptr(delta), nullptr, boundsPtr, nullptr);
                    if (ImGuizmo::IsUsing()) {
                        if (!gameUiGizmoHistoryCaptured) {
                            recordState("gameUiGizmo");
                            gameUiRectGizmoOperation = op;
                            gameUiRectGizmoModel = originalModel;
                            gameUiRectGizmoStartMouse = ImGui::GetIO().MousePos;
                            gameUiRectGizmoSnapshots.clear();
                            for (int id : worldUiRoots) {
                                SceneObject* target = findObjectById(id);
                                if (!target) continue;
                                ImVec2 targetMin, targetMax;
                                if (!resolveUIRectWorld(*target, targetMin, targetMax)) continue;
                                gameUiRectGizmoSnapshots.push_back(UiRectGizmoSnapshot{
                                    id,
                                    target->ui.position,
                                    target->ui.size,
                                    target->ui.rotation,
                                    targetMin,
                                    targetMax
                                });
                            }
                            gameUiGizmoHistoryCaptured = true;
                        }
                        const float scaleDragDx = ImGui::GetIO().MousePos.x - gameUiRectGizmoStartMouse.x;
                        const float scaleDragDy = ImGui::GetIO().MousePos.y - gameUiRectGizmoStartMouse.y;
                        const bool allowScaleApply = !stableRectScale ||
                                                     ((scaleDragDx * scaleDragDx + scaleDragDy * scaleDragDy) >= 9.0f);
                        const bool applyPixelSnap = pixelGridSnapEnabled;
                        const float pixelStep = static_cast<float>(std::max(1, pixelGridSnapStep));
                        auto snapScreenToPixel = [&](ImVec2 p) {
                            p.x = imageMin.x + std::round((p.x - imageMin.x) / pixelStep) * pixelStep;
                            p.y = imageMin.y + std::round((p.y - imageMin.y) / pixelStep) * pixelStep;
                            return p;
                        };
                        auto findRectSnapshot = [&](int id) -> const UiRectGizmoSnapshot* {
                            for (const UiRectGizmoSnapshot& snapshot : gameUiRectGizmoSnapshots) {
                                if (snapshot.objectId == id) return &snapshot;
                            }
                            return nullptr;
                        };
                        auto extractRectFromModel = [](const glm::mat4& rectModel, ImVec2& outCenter, ImVec2& outSize) {
                            const glm::vec4 corners[4] = {
                                glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f),
                                glm::vec4( 0.5f, -0.5f, 0.0f, 1.0f),
                                glm::vec4( 0.5f,  0.5f, 0.0f, 1.0f),
                                glm::vec4(-0.5f,  0.5f, 0.0f, 1.0f)
                            };
                            ImVec2 pts[4];
                            for (int i = 0; i < 4; ++i) {
                                const glm::vec4 p = rectModel * corners[i];
                                pts[i] = ImVec2(p.x, p.y);
                            }
                            outCenter = ImVec2((pts[0].x + pts[1].x + pts[2].x + pts[3].x) * 0.25f,
                                               (pts[0].y + pts[1].y + pts[2].y + pts[3].y) * 0.25f);
                            auto distance = [](const ImVec2& a, const ImVec2& b) {
                                const float dx = b.x - a.x;
                                const float dy = b.y - a.y;
                                return std::sqrt(dx * dx + dy * dy);
                            };
                            outSize = ImVec2(std::max(0.01f, distance(pts[0], pts[1])),
                                             std::max(0.01f, distance(pts[0], pts[3])));
                        };

                        if (useWorldUi && !worldUiRoots.empty()) {
                            const glm::mat4 gizmoDelta = model * glm::inverse(stableRectScale ? gameUiRectGizmoModel : originalModel);
                            const bool groupRotate = (op == ImGuizmo::ROTATE && worldUiRoots.size() > 1);

                            for (int id : worldUiRoots) {
                                SceneObject* target = findObjectById(id);
                                if (!target) continue;

                                ImVec2 targetMin, targetMax;
                                float targetRotation = target->ui.rotation;
                                if (stableRectScale) {
                                    const UiRectGizmoSnapshot* snapshot = findRectSnapshot(id);
                                    if (!snapshot) continue;
                                    targetMin = snapshot->rectMin;
                                    targetMax = snapshot->rectMax;
                                    targetRotation = snapshot->rotation;
                                } else {
                                    if (!resolveUIRectWorld(*target, targetMin, targetMax)) continue;
                                }
                                ImVec2 targetSize(targetMax.x - targetMin.x, targetMax.y - targetMin.y);
                                if (targetSize.x <= 0.01f || targetSize.y <= 0.01f) continue;
                                ImVec2 targetCenter((targetMin.x + targetMax.x) * 0.5f - imageMin.x,
                                                    (targetMin.y + targetMax.y) * 0.5f - imageMin.y);

                                glm::mat4 targetModel(1.0f);
                                targetModel = glm::translate(targetModel, glm::vec3(targetCenter.x, targetCenter.y, 0.0f));
                                targetModel = glm::rotate(targetModel, glm::radians(targetRotation), glm::vec3(0.0f, 0.0f, 1.0f));
                                targetModel = glm::scale(targetModel, glm::vec3(targetSize.x, targetSize.y, 1.0f));

                                glm::mat4 targetNewModel = gizmoDelta * targetModel;
                                glm::vec3 pos, rot, scl;
                                DecomposeMatrix(targetNewModel, pos, rot, scl);
                                glm::vec3 euler = NormalizeEulerDegrees(glm::degrees(rot));
                                ImVec2 targetNewCenter(imageMin.x + pos.x, imageMin.y + pos.y);
                                ImVec2 targetNewScreenSize(targetSize.x, targetSize.y);
                                if (stableRectScale) {
                                    extractRectFromModel(targetNewModel, targetNewCenter, targetNewScreenSize);
                                    targetNewCenter.x += imageMin.x;
                                    targetNewCenter.y += imageMin.y;
                                }
                                if (applyPixelSnap && op == ImGuizmo::TRANSLATE) {
                                    targetNewCenter = snapScreenToPixel(targetNewCenter);
                                }

                                glm::vec2 targetParentOffset = getWorldParentOffset(*target);
                                glm::vec2 worldCenter = screenToWorld(targetNewCenter);

                                if (op == ImGuizmo::ROTATE) {
                                    target->ui.rotation = euler.z;
                                    if (groupRotate) {
                                        glm::vec2 worldSize = target->ui.size;
                                        ImVec2 pivotOffset = anchorToPivotUI(target->ui.anchor, ImVec2(worldSize.x, worldSize.y));
                                        glm::vec2 worldMin = worldCenter - worldSize * 0.5f;
                                        glm::vec2 worldPivot = worldMin + glm::vec2(pivotOffset.x, pivotOffset.y);
                                        target->ui.position = worldPivot - targetParentOffset - parallaxOffset(*target);
                                    }
                                } else if (op == ImGuizmo::TRANSLATE) {
                                    glm::vec2 worldSize = target->ui.size;
                                    ImVec2 pivotOffset = anchorToPivotUI(target->ui.anchor, ImVec2(worldSize.x, worldSize.y));
                                    glm::vec2 worldMin = worldCenter - worldSize * 0.5f;
                                    glm::vec2 worldPivot = worldMin + glm::vec2(pivotOffset.x, pivotOffset.y);
                                    target->ui.position = worldPivot - targetParentOffset - parallaxOffset(*target);
                                } else if (op == ImGuizmo::SCALE || op == ImGuizmo::BOUNDS) {
                                    if (allowScaleApply) {
                                    const float minUiSize = (target->ui.type == UIElementType::Image ||
                                                             target->ui.type == UIElementType::Sprite2D)
                                        ? 0.01f
                                        : 1.0f;
                                    ImVec2 newSize = stableRectScale
                                        ? ImVec2(std::max(minUiSize, targetNewScreenSize.x), std::max(minUiSize, targetNewScreenSize.y))
                                        : ImVec2(std::max(minUiSize, scl.x), std::max(minUiSize, scl.y));
                                    if (stableRectScale) {
                                        constexpr float kRectScaleDeadZonePx = 0.1f;
                                        if (std::abs(newSize.x - targetSize.x) < kRectScaleDeadZonePx) newSize.x = targetSize.x;
                                        if (std::abs(newSize.y - targetSize.y) < kRectScaleDeadZonePx) newSize.y = targetSize.y;
                                    }
                                    if (applyPixelSnap) {
                                        newSize.x = std::max(pixelStep, std::round(newSize.x / pixelStep) * pixelStep);
                                        newSize.y = std::max(pixelStep, std::round(newSize.y / pixelStep) * pixelStep);
                                    }
                                    glm::vec2 worldSize = glm::vec2(newSize.x, newSize.y) / uiWorldCamera.zoom;
                                    ImVec2 pivotOffset = anchorToPivotUI(target->ui.anchor, ImVec2(worldSize.x, worldSize.y));
                                    glm::vec2 worldMin = worldCenter - worldSize * 0.5f;
                                    glm::vec2 worldPivot = worldMin + glm::vec2(pivotOffset.x, pivotOffset.y);
                                    target->ui.position = worldPivot - targetParentOffset - parallaxOffset(*target);
                                    target->ui.size = worldSize;
                                    }
                                }
                            }
                        } else {
                            glm::vec3 pos, rot, scl;
                            DecomposeMatrix(model, pos, rot, scl);
                            glm::vec3 euler = NormalizeEulerDegrees(glm::degrees(rot));
                            ImVec2 newPivot(imageMin.x + pos.x, imageMin.y + pos.y);
                            ImVec2 newScaleCenter = newPivot;
                            ImVec2 newScaleScreenSize(std::max(0.01f, scl.x), std::max(0.01f, scl.y));
                            if (stableRectScale) {
                                extractRectFromModel(model, newScaleCenter, newScaleScreenSize);
                                newScaleCenter.x += imageMin.x;
                                newScaleCenter.y += imageMin.y;
                            }
                            if (applyPixelSnap && op == ImGuizmo::TRANSLATE) {
                                newPivot = snapScreenToPixel(newPivot);
                            }
                            if (op == ImGuizmo::ROTATE) {
                                selected->ui.rotation = euler.z;
                            } else if (op == ImGuizmo::TRANSLATE) {
                                if (useWorldUi) {
                                    glm::vec2 worldPivot = screenToWorld(newPivot);
                                    selected->ui.position = worldPivot - parentOffset - parallaxOffset(*selected);
                                } else {
                                    ImVec2 anchorPoint = anchorToPoint(selected->ui.anchor, parentMin, parentMax);
                                    float invScaleX = (uiScaleX > 0.0f) ? 1.0f / uiScaleX : 1.0f;
                                    float invScaleY = (uiScaleY > 0.0f) ? 1.0f / uiScaleY : 1.0f;
                                    selected->ui.position = glm::vec2((newPivot.x - anchorPoint.x) * invScaleX,
                                                                     (newPivot.y - anchorPoint.y) * invScaleY);
                                }
                            } else if (op == ImGuizmo::SCALE || op == ImGuizmo::BOUNDS) {
                                if (allowScaleApply) {
                                const float minUiSize = (selected->ui.type == UIElementType::Image ||
                                                         selected->ui.type == UIElementType::Sprite2D)
                                    ? 0.01f
                                    : 1.0f;
                                ImVec2 newSize = stableRectScale
                                    ? ImVec2(std::max(minUiSize, newScaleScreenSize.x), std::max(minUiSize, newScaleScreenSize.y))
                                    : ImVec2(std::max(minUiSize, scl.x), std::max(minUiSize, scl.y));
                                if (stableRectScale) {
                                    constexpr float kRectScaleDeadZonePx = 0.1f;
                                    if (std::abs(newSize.x - rectSize.x) < kRectScaleDeadZonePx) newSize.x = rectSize.x;
                                    if (std::abs(newSize.y - rectSize.y) < kRectScaleDeadZonePx) newSize.y = rectSize.y;
                                }
                                if (applyPixelSnap) {
                                    newSize.x = std::max(pixelStep, std::round(newSize.x / pixelStep) * pixelStep);
                                    newSize.y = std::max(pixelStep, std::round(newSize.y / pixelStep) * pixelStep);
                                }
                                if (useWorldUi) {
                                    glm::vec2 worldSize = glm::vec2(newSize.x, newSize.y) / uiWorldCamera.zoom;
                                    glm::vec2 worldCenter = screenToWorld(newScaleCenter);
                                    ImVec2 pivotOffset = anchorToPivotUI(selected->ui.anchor, ImVec2(worldSize.x, worldSize.y));
                                    glm::vec2 worldMin = worldCenter - worldSize * 0.5f;
                                    glm::vec2 worldPivot = worldMin + glm::vec2(pivotOffset.x, pivotOffset.y);
                                    selected->ui.position = worldPivot - parentOffset - parallaxOffset(*selected);
                                    selected->ui.size = worldSize;
                                } else {
                                    float invScaleX = (uiScaleX > 0.0f) ? 1.0f / uiScaleX : 1.0f;
                                    float invScaleY = (uiScaleY > 0.0f) ? 1.0f / uiScaleY : 1.0f;
                                    glm::vec2 uiSize(newSize.x * invScaleX, newSize.y * invScaleY);
                                    ImVec2 anchorPoint = anchorToPoint(selected->ui.anchor, parentMin, parentMax);
                                    ImVec2 pivotOffset = anchorToPivotUI(selected->ui.anchor, ImVec2(uiSize.x, uiSize.y));
                                    ImVec2 screenMin(newScaleCenter.x - newSize.x * 0.5f, newScaleCenter.y - newSize.y * 0.5f);
                                    ImVec2 screenPivot(screenMin.x + pivotOffset.x * uiScaleX,
                                                       screenMin.y + pivotOffset.y * uiScaleY);
                                    selected->ui.position = glm::vec2((screenPivot.x - anchorPoint.x) * invScaleX,
                                                                     (screenPivot.y - anchorPoint.y) * invScaleY);
                                    selected->ui.size = uiSize;
                                    }
                                }
                            }
                        }
                        projectManager.currentProject.hasUnsavedChanges = true;
                        gizmoUsed = true;
                    } else {
                        gameUiGizmoHistoryCaptured = false;
                        gameUiRectGizmoSnapshots.clear();
                        gameUiRectGizmoModel = glm::mat4(1.0f);
                        gameUiRectGizmoStartMouse = ImVec2(0.0f, 0.0f);
                    }
                }
            } else {
                gameUiGizmoHistoryCaptured = false;
                gameUiRectGizmoSnapshots.clear();
                gameUiRectGizmoModel = glm::mat4(1.0f);
                gameUiRectGizmoStartMouse = ImVec2(0.0f, 0.0f);
            }
        } else {
            gameUiGizmoHistoryCaptured = false;
            gameUiRectGizmoSnapshots.clear();
            gameUiRectGizmoModel = glm::mat4(1.0f);
            gameUiRectGizmoStartMouse = ImVec2(0.0f, 0.0f);
        }

        uiInteracting = ImGui::IsAnyItemActive() || gizmoUsed || uiWorldCameraActive || pseudoPanelInteracting;

        ImGui::EndChild();
        ImGui::PopStyleVar();
        bool clicked = imageHovered && isPlaying && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !uiInteracting;

        if (clicked && !gameViewCursorLocked) {
            gameViewCursorLocked = true;
        }
        if (gameViewCursorLocked && (!isPlaying || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
            gameViewCursorLocked = false;
        }

        gameViewportFocused = windowFocused || gameViewCursorLocked;
        if (restoreUiWorldCamera) {
            uiWorldCamera = uiWorldCameraBackup;
        }
    } else {
        ImGui::TextDisabled("No player camera found (Camera Type: Player).");
        gameViewportFocused = ImGui::IsWindowFocused();
    }

    ImGui::End();

    // Keep dock drawer collapse/expand responsive when Game Viewport is the active tab.
    // Skip while workspace transitions are in-flight to avoid split-node thrashing.
    if (!(pendingWorkspaceReload || workspaceLayoutDirty || glfwGetTime() < workspaceLayoutStabilizeUntil)) {
        updateDockDrawerAnimations();
    }
}
#pragma endregion

#pragma region Play Controls Bar
void Engine::renderPlayControlsBar() {
    const char* playTooltip = isPlaying ? "Stop Play Mode" : "Play Mode";
    const char* specTooltip = specMode ? "Disable Spec Mode" : "Spec Mode";
    const char* pauseTooltip = isPaused ? "Resume" : "Pause";
    const bool hasVulkanSceneTexture = usingVulkan() && vulkanRendererInitialized && (vulkanRenderer != nullptr);
    float animSpeed = 0.0f;
    if (uiAnimationMode == UIAnimationMode::Fluid) {
        animSpeed = 8.0f;
    } else if (uiAnimationMode == UIAnimationMode::Snappy) {
        animSpeed = 18.0f;
    }
    float animStep = (uiAnimationMode == UIAnimationMode::Off) ? 1.0f
        : (1.0f - std::exp(-animSpeed * ImGui::GetIO().DeltaTime));

    struct ToolbarIcon {
        ImTextureID id = static_cast<ImTextureID>(0);
        bool flipY = false;
    };

    auto resolveToolbarIcon = [&](const char* iconPath) -> ToolbarIcon {
        if (!iconPath || !*iconPath) {
            return {};
        }
        if (rendererInitialized) {
            if (Texture* icon = renderer.getTexture(iconPath, MaterialProperties::TextureFilter::Bilinear);
                icon && icon->GetID()) {
                return { static_cast<ImTextureID>(icon->GetID()), true };
            }
            return {};
        }
        if (hasVulkanSceneTexture && vulkanRenderer) {
            ImTextureID vkIcon = vulkanRenderer->getOrCreateUIImage(iconPath);
            if (vkIcon != static_cast<ImTextureID>(0)) {
                return { vkIcon, false };
            }
        }
        return {};
    };

    const float buttonSide = std::max(24.0f, ImGui::GetFrameHeight());
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float totalWidth = buttonSide * 3.0f + spacing * 2.0f;

    const float regionMinX = ImGui::GetWindowContentRegionMin().x;
    const float regionMaxX = ImGui::GetWindowContentRegionMax().x;
    float startX = regionMaxX - totalWidth;
    if (startX < regionMinX) startX = regionMinX;

    ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(startX, cursor.y));

    auto iconButton = [&](const char* id,
                          const char* iconPathColored,
                          const char* iconPathGray,
                          const char* fallbackText,
                          const char* tooltip,
                          bool toggled) -> bool {
        const ImVec2 slotSize(buttonSide, buttonSide);
        const ImVec2 slotPos = ImGui::GetCursorScreenPos();
        bool pressed = ImGui::InvisibleButton(id, slotSize);
        bool hovered = ImGui::IsItemHovered();
        bool active = ImGui::IsItemActive();
        ImGuiID buttonId = ImGui::GetID(id);
        UIAnimationState& st = editorUiAnimationStates[buttonId];
        if (uiAnimationMode == UIAnimationMode::Off) {
            st.hover = hovered ? 1.0f : 0.0f;
            st.active = active ? 1.0f : 0.0f;
        } else {
            const float hoverTarget = hovered ? 1.0f : 0.0f;
            const float activeTarget = active ? 1.0f : 0.0f;
            st.hover += (hoverTarget - st.hover) * animStep;
            st.active += (activeTarget - st.active) * animStep;
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 slotMax(slotPos.x + slotSize.x, slotPos.y + slotSize.y);
        const float zoom = 1.0f + st.hover * 0.08f + st.active * 0.14f;
        const float drawSide = slotSize.x * zoom;
        const ImVec2 iconCenter(slotPos.x + slotSize.x * 0.5f, slotPos.y + slotSize.y * 0.5f);
        const ImVec2 iconMin(iconCenter.x - drawSide * 0.5f, iconCenter.y - drawSide * 0.5f);
        const ImVec2 iconMax(iconCenter.x + drawSide * 0.5f, iconCenter.y + drawSide * 0.5f);

        const char* iconPath = toggled ? iconPathColored : iconPathGray;
        ToolbarIcon icon = resolveToolbarIcon(iconPath);
        int alpha = toggled ? 255 : 192;
        if (hovered && !toggled) alpha = 218;
        if (active && !toggled) alpha = 236;
        const ImU32 iconTint = IM_COL32(255, 255, 255, alpha);

        const float highlightStrength = std::max(st.hover, st.active);
        if (highlightStrength > 0.01f) {
            const int a = static_cast<int>(24.0f + 68.0f * highlightStrength);
            dl->AddRect(slotPos, slotMax, IM_COL32(255, 255, 255, a), 6.0f, 0, 1.0f);
        }
        if (toggled) {
            dl->AddRect(slotPos, slotMax, IM_COL32(255, 255, 255, 146), 6.0f, 0, 1.2f);
        }

        if (icon.id != static_cast<ImTextureID>(0)) {
            // Inset UVs slightly to avoid transparent-edge bleed artifacts between icons.
            const float uvInset = 1.0f / 32.0f;
            const ImVec2 uvMin = icon.flipY ? ImVec2(uvInset, 1.0f - uvInset) : ImVec2(uvInset, uvInset);
            const ImVec2 uvMax = icon.flipY ? ImVec2(1.0f - uvInset, uvInset) : ImVec2(1.0f - uvInset, 1.0f - uvInset);
            dl->AddImage(icon.id, iconMin, iconMax, uvMin, uvMax, iconTint);
        } else {
            ImVec2 textSize = ImGui::CalcTextSize(fallbackText);
            ImVec2 textPos(iconCenter.x - textSize.x * 0.5f,
                           iconCenter.y - textSize.y * 0.5f);
            dl->AddText(textPos, iconTint, fallbackText);
        }
        if (hovered && tooltip && *tooltip) {
            ImGui::SetTooltip("%s", tooltip);
        }
        return pressed;
    };

    bool playPressed = iconButton(
        "##PlayModeToolbarButton",
        "Resources/Engine-Root/Editor/Play Button.png",
        "Resources/Engine-Root/Editor/Play Button Gray.png",
        "P",
        playTooltip,
        isPlaying);
    ImGui::SameLine(0.0f, spacing);
    bool specPressed = iconButton(
        "##SpecModeToolbarButton",
        "Resources/Engine-Root/Editor/Spec Mode Button.png",
        "Resources/Engine-Root/Editor/Spec Mode Button Gray.png",
        "S",
        specTooltip,
        specMode);
    ImGui::SameLine(0.0f, spacing);
    bool pausePressed = iconButton(
        "##PauseToolbarButton",
        "Resources/Engine-Root/Editor/Pause Button.png",
        "Resources/Engine-Root/Editor/Pause Button Gray.png",
        "||",
        pauseTooltip,
        isPaused);

    if (playPressed) {
        bool newState = !isPlaying;
        if (newState) {
            // Reset script module state so Begin/static script state is fresh each play session.
            resetScriptRuntimeStateForReload(false);
            capturePlayModeSnapshot();
            for (SceneObject& obj : sceneObjects) {
                if (!obj.hasAnimation) continue;
                obj.animation.runtimePlaying = false;
                obj.animation.runtimePaused = false;
                obj.animation.runtimeTime = 0.0f;
                obj.animation.runtimeDirection = 1.0f;
                obj.animation.runtimeInitialized = false;
                obj.animation.runtimeClipPath.clear();
            }
            if (physics.isReady() || physics.init()) {
                physics.onPlayStart(sceneObjects);
            } else {
                addConsoleMessage("PhysX failed to initialize; physics disabled for play mode", ConsoleMessageType::Warning);
            }
            audio.onPlayStart(sceneObjects);
            bool hasPlayerController = false;
            for (const auto& obj : sceneObjects) {
                if (IsObjectEnabledInHierarchy(obj) && obj.hasPlayerController && obj.playerController.enabled) {
                    hasPlayerController = true;
                    break;
                }
            }
            if (hasPlayerController && showGameViewport) {
                gameViewCursorLocked = true;
                gameViewportFocused = true;
            }
        } else {
            physics.onPlayStop();
            audio.onPlayStop();
            restorePlayModeSnapshot();
            resetScriptRuntimeStateForReload(false);
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
            if (specMode) {
                physics.onPlayStart(sceneObjects);
                audio.onPlayStart(sceneObjects);
            } else {
                physics.onPlayStop();
                audio.onPlayStop();
            }
        }
    }
}

#pragma endregion

#pragma region Main Menu Bar
namespace {
enum class DockDrawerSide {
    Left,
    Right,
    Bottom
};

struct DockDrawerTarget {
    ImGuiDockNode* splitParent = nullptr;
    ImGuiDockNode* drawerBranch = nullptr;
    ImGuiDockNode* oppositeBranch = nullptr;
};

struct DockDrawerState {
    ImGuiID activeSplitParentId = 0;
    bool collapsed = false;
    float openAmount = 1.0f;
    float expandedExtent = 0.0f;
    ImGuiID pendingTabFocusId = 0;
};

void addRotatedText90CW(ImDrawList* drawList,
                        ImFont* font,
                        float fontSize,
                        const ImRect& bounds,
                        ImU32 color,
                        const char* text) {
    if (!drawList || !font || !text || !*text) return;
    if ((color & IM_COL32_A_MASK) == 0) return;

    ImFontBaked* baked = font->GetFontBaked(fontSize);
    if (!baked) return;

    const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text, nullptr, nullptr);
    if (textSize.x <= 0.0f || textSize.y <= 0.0f) return;

    // Rotating CW maps original (w,h) text bounds to (h,w).
    const float rotatedWidth = textSize.y;
    const float rotatedHeight = textSize.x;
    const float originX = bounds.Min.x + (bounds.GetWidth() - rotatedWidth) * 0.5f;
    const float originY = bounds.Min.y + (bounds.GetHeight() - rotatedHeight) * 0.5f;
    const float scale = (baked->Size > 0.0f) ? (fontSize / baked->Size) : 1.0f;

    float cursorX = 0.0f;
    const char* s = text;
    while (s && *s) {
        unsigned int c = 0;
        const int bytes = ImTextCharFromUtf8(&c, s, nullptr);
        if (bytes <= 0) break;
        s += bytes;

        if (c == '\n' || c == '\r') continue;
        ImFontGlyph* glyph = baked->FindGlyphNoFallback(static_cast<ImWchar>(c));
        if (!glyph) continue;

        const float x1 = cursorX + glyph->X0 * scale;
        const float x2 = cursorX + glyph->X1 * scale;
        const float y1 = glyph->Y0 * scale;
        const float y2 = glyph->Y1 * scale;
        const float u1 = glyph->U0;
        const float v1 = glyph->V0;
        const float u2 = glyph->U1;
        const float v2 = glyph->V1;

        auto rotateCW = [&](float x, float y) -> ImVec2 {
            // (x,y) -> (y, textWidth - x), then centered in target bounds.
            return ImVec2(originX + y, originY + (textSize.x - x));
        };

        const ImVec2 pTL = rotateCW(x1, y1);
        const ImVec2 pTR = rotateCW(x2, y1);
        const ImVec2 pBR = rotateCW(x2, y2);
        const ImVec2 pBL = rotateCW(x1, y2);

        drawList->AddImageQuad(ImGui::GetIO().Fonts->TexRef,
                               pTL, pTR, pBR, pBL,
                               ImVec2(u1, v1), ImVec2(u2, v1),
                               ImVec2(u2, v2), ImVec2(u1, v2),
                               color);

        cursorX += glyph->AdvanceX * scale;
    }
}

struct DockTabInteractionState {
    bool hovered = false;
    bool clicked = false;
    bool doubleClicked = false;
};

bool matchesVisibleWindowTitle(const char* windowName, const char* expectedTitle) {
    if (!windowName || !expectedTitle) return false;
    const char* idSep = std::strstr(windowName, "###");
    if (!idSep) {
        idSep = std::strstr(windowName, "##");
    }
    const size_t visibleLen = idSep ? static_cast<size_t>(idSep - windowName) : std::strlen(windowName);
    return std::strlen(expectedTitle) == visibleLen &&
           std::strncmp(windowName, expectedTitle, visibleLen) == 0;
}

ImGuiWindow* findWindowByVisibleTitle(const char* expectedTitle) {
    if (!expectedTitle || !*expectedTitle) return nullptr;
    if (ImGuiWindow* exact = ImGui::FindWindowByName(expectedTitle)) {
        return exact;
    }

    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) return nullptr;
    for (ImGuiWindow* window : ctx->Windows) {
        if (!window) continue;
        if ((window->Flags & ImGuiWindowFlags_ChildWindow) != 0) continue;
        if (!window->DockNode) continue;
        if (window && matchesVisibleWindowTitle(window->Name, expectedTitle)) {
            return window;
        }
    }
    return nullptr;
}

DockTabInteractionState queryDockTabInteraction(const DockDrawerTarget& target,
                                                const char* const* anchorWindows,
                                                int anchorCount) {
    DockTabInteractionState out;

    if (ImGuiTabBar* tabBar = target.drawerBranch ? target.drawerBranch->TabBar : nullptr) {
        if (ImGui::IsMouseHoveringRect(tabBar->BarRect.Min, tabBar->BarRect.Max, false)) {
            out.hovered = true;
        }
    }

    for (int i = 0; i < anchorCount; ++i) {
        ImGuiWindow* window = findWindowByVisibleTitle(anchorWindows[i]);
        if (!window) continue;
        ImRect tabRect = window->DC.DockTabItemRect;
        if (tabRect.GetWidth() <= 0.0f || tabRect.GetHeight() <= 0.0f) continue;
        if (ImGui::IsMouseHoveringRect(tabRect.Min, tabRect.Max, false)) {
            out.hovered = true;
            break;
        }
    }

    if (!out.hovered && target.drawerBranch) {
        const ImVec2 headerMin = target.drawerBranch->Pos;
        const ImVec2 headerMax(target.drawerBranch->Pos.x + target.drawerBranch->Size.x,
                               target.drawerBranch->Pos.y + ImGui::GetFrameHeight() + 8.0f);
        if (ImGui::IsMouseHoveringRect(headerMin, headerMax, false)) {
            out.hovered = true;
        }
    }

    if (out.hovered) {
        ImGuiIO& io = ImGui::GetIO();
        out.doubleClicked = io.MouseClicked[ImGuiMouseButton_Left] &&
                            io.MouseClickedCount[ImGuiMouseButton_Left] >= 2;
        out.clicked = !out.doubleClicked && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    }
    return out;
}

void queueDrawerTabFocus(DockDrawerState& state, ImGuiTabBar* tabBar, ImGuiID tabId) {
    if (!tabBar || tabId == 0) return;
    for (int i = 0; i < tabBar->Tabs.Size; ++i) {
        ImGuiTabItem* tab = &tabBar->Tabs[i];
        if (tab->ID != tabId) continue;
        ImGui::TabBarQueueFocus(tabBar, tab);
        state.pendingTabFocusId = tabId;
        return;
    }
}

void applyPendingDrawerTabFocus(DockDrawerState& state, ImGuiTabBar* tabBar) {
    if (!tabBar || state.pendingTabFocusId == 0) return;

    bool found = false;
    for (int i = 0; i < tabBar->Tabs.Size; ++i) {
        ImGuiTabItem* tab = &tabBar->Tabs[i];
        if (tab->ID != state.pendingTabFocusId) continue;
        found = true;
        if (tabBar->SelectedTabId == tab->ID || tabBar->VisibleTabId == tab->ID) {
            state.pendingTabFocusId = 0;
            return;
        }
        ImGui::TabBarQueueFocus(tabBar, tab);
        break;
    }

    if (!found) {
        state.pendingTabFocusId = 0;
    }
}

void renderCollapsedSideDockRail(DockDrawerState& state,
                                 const DockDrawerTarget& target,
                                 DockDrawerSide side,
                                 float railWidth,
                                 float revealAmount) {
    if (side == DockDrawerSide::Bottom) return;
    if (!target.drawerBranch || !target.splitParent) return;
    ImGuiTabBar* tabBar = target.drawerBranch->TabBar;
    if (!tabBar || tabBar->Tabs.Size <= 0) return;
    const float reveal = std::clamp(revealAmount, 0.0f, 1.0f);
    if (reveal <= 0.001f) return;

    const float splitMinX = target.splitParent->Pos.x;
    const float splitMaxX = target.splitParent->Pos.x + target.splitParent->Size.x;
    const float splitWidth = ImMax(1.0f, splitMaxX - splitMinX);
    const float fullRailWidth = std::clamp(ImMax(railWidth, 22.0f), 8.0f, splitWidth);
    const float visibleRailWidth = ImMax(1.0f, fullRailWidth * reveal);

    const float branchMinX = target.drawerBranch->Pos.x;
    const float branchMaxX = target.drawerBranch->Pos.x + target.drawerBranch->Size.x;
    const float branchMinY = target.drawerBranch->Pos.y;
    const float branchMaxY = target.drawerBranch->Pos.y + target.drawerBranch->Size.y;

    ImVec2 railPos(branchMinX, branchMinY);
    ImVec2 railSize = target.drawerBranch->Size;
    railSize.x = visibleRailWidth;

    const bool hasValidBarRect = tabBar->BarRect.GetWidth() > 1.0f && tabBar->BarRect.GetHeight() > 1.0f;
    const float hingeX = [&]() {
        if (side == DockDrawerSide::Left) {
            const float preferred = hasValidBarRect ? tabBar->BarRect.Max.x : branchMaxX;
            return std::clamp(preferred, splitMinX, splitMaxX);
        }
        const float preferred = hasValidBarRect ? tabBar->BarRect.Min.x : branchMinX;
        return std::clamp(preferred, splitMinX, splitMaxX);
    }();

    railPos.x = (side == DockDrawerSide::Left) ? (hingeX - visibleRailWidth) : hingeX;
    railPos.x = std::clamp(railPos.x, splitMinX, splitMaxX - railSize.x);

    float railTopY = branchMinY;
    if (hasValidBarRect) {
        railTopY = ImMax(railTopY, tabBar->BarRect.Max.y - 1.0f);
    }
    railPos.y = railTopY;
    railSize.y = ImMax(1.0f, branchMaxY - railTopY);

    char railWindowName[64];
    std::snprintf(railWindowName, sizeof(railWindowName), "##DockRail_%c_%08X",
                  side == DockDrawerSide::Left ? 'L' : 'R',
                  target.splitParent->ID);

    ImGuiWindowFlags railFlags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::SetNextWindowPos(railPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(railSize, ImGuiCond_Always);
    if (target.drawerBranch->HostWindow) {
        ImGui::SetNextWindowViewport(target.drawerBranch->HostWindow->ViewportId);
    }
    ImGui::SetNextWindowBgAlpha(0.94f * reveal);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    if (ImGui::Begin(railWindowName, nullptr, railFlags)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, 1.0f));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float slotSpacing = ImMax(1.0f, style.ItemInnerSpacing.y);
        const ImVec2 railMin = ImGui::GetWindowPos();
        const ImVec2 railMax(railMin.x + ImGui::GetWindowSize().x, railMin.y + ImGui::GetWindowSize().y);
        const ImRect railRect(railMin, railMax);
        draw->AddRectFilled(railRect.Min, railRect.Max, ImGui::GetColorU32(ImGuiCol_Tab));
        draw->AddRect(railRect.Min, railRect.Max, ImGui::GetColorU32(ImGuiCol_Border));

        const bool tabBarFocused = (tabBar->Flags & ImGuiTabBarFlags_IsFocused) != 0;
        const float minSlotHeight = ImGui::GetFrameHeight();
        const float maxSlotHeight = ImGui::GetFrameHeight() * 3.2f;
        const float slotWidth = ImMax(3.0f, ImGui::GetContentRegionAvail().x);
        float cursorY = ImGui::GetCursorPosY() + style.FramePadding.y;

        for (int i = 0; i < tabBar->Tabs.Size; ++i) {
            ImGuiTabItem* tab = &tabBar->Tabs[i];
            const char* tabName = ImGui::TabBarGetTabName(tabBar, tab);
            const bool selected = (tabBar->SelectedTabId == tab->ID) || (tabBar->VisibleTabId == tab->ID);
            const ImVec2 labelSize = ImGui::CalcTextSize(tabName);
            const float slotHeight = std::clamp(labelSize.x + style.FramePadding.x * 2.0f, minSlotHeight, maxSlotHeight);

            ImGui::PushID(static_cast<int>(tab->ID));
            ImGui::SetCursorPosY(cursorY);
            ImVec2 slotPos = ImGui::GetCursorScreenPos();
            ImVec2 slotSize(slotWidth, slotHeight);
            if (ImGui::InvisibleButton("##SideTab", slotSize)) {
                queueDrawerTabFocus(state, tabBar, tab->ID);
                state.collapsed = false;
            }
            const bool hovered = ImGui::IsItemHovered();
            if (hovered) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            const ImRect slotRect(slotPos, ImVec2(slotPos.x + slotSize.x, slotPos.y + slotSize.y));

            const ImU32 bg = selected
                ? ImGui::GetColorU32(tabBarFocused ? ImGuiCol_TabSelected : ImGuiCol_TabDimmedSelected)
                : (hovered ? ImGui::GetColorU32(ImGuiCol_TabHovered)
                           : ImGui::GetColorU32(tabBarFocused ? ImGuiCol_Tab : ImGuiCol_TabDimmed));
            const ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
            const ImU32 overline = ImGui::GetColorU32(
                tabBarFocused ? ImGuiCol_TabSelectedOverline : ImGuiCol_TabDimmedSelectedOverline);

            ImDrawFlags roundFlags = (side == DockDrawerSide::Left)
                ? (ImDrawFlags_RoundCornersTopRight | ImDrawFlags_RoundCornersBottomRight)
                : (ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomLeft);
            draw->AddRectFilled(slotRect.Min, slotRect.Max, bg, style.TabRounding, roundFlags);
            draw->AddRect(slotRect.Min, slotRect.Max, border, style.TabRounding, roundFlags);
            if (selected) {
                const float overlineThickness = ImMax(1.0f, style.TabBarOverlineSize);
                if (side == DockDrawerSide::Left) {
                    draw->AddRectFilled(ImVec2(slotRect.Max.x - overlineThickness, slotRect.Min.y + 1.0f),
                                        ImVec2(slotRect.Max.x, slotRect.Max.y - 1.0f),
                                        overline);
                } else {
                    draw->AddRectFilled(ImVec2(slotRect.Min.x, slotRect.Min.y + 1.0f),
                                        ImVec2(slotRect.Min.x + overlineThickness, slotRect.Max.y - 1.0f),
                                        overline);
                }
            }

            const ImU32 textCol = ImGui::GetColorU32((selected || hovered) ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            const float baseFontSize = ImGui::GetFontSize();
            const float minRailFont = baseFontSize * 0.70f;
            const float maxRailFont = baseFontSize * 1.05f;
            float railFontSize = std::clamp(baseFontSize * 0.95f, minRailFont, maxRailFont);
            const ImVec2 unscaledText = ImGui::GetFont()->CalcTextSizeA(railFontSize, FLT_MAX, 0.0f, tabName);
            if (unscaledText.x > 0.5f && unscaledText.y > 0.5f) {
                const float fitToWidth = (slotRect.GetWidth() - 4.0f) / unscaledText.y;
                const float fitToHeight = (slotRect.GetHeight() - 6.0f) / unscaledText.x;
                const float fitScale = ImMin(fitToWidth, fitToHeight);
                railFontSize = std::clamp(railFontSize * fitScale, minRailFont, maxRailFont);
            }
            addRotatedText90CW(draw, ImGui::GetFont(), railFontSize, slotRect, textCol, tabName);
            if (hovered) {
                ImGui::SetItemTooltip("%s", tabName);
            }
            ImGui::PopID();
            cursorY += slotHeight + slotSpacing;
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

DockDrawerTarget findDockDrawerTarget(const char* const* anchorWindows, int anchorCount, DockDrawerSide side) {
    const ImGuiAxis axis = (side == DockDrawerSide::Bottom) ? ImGuiAxis_Y : ImGuiAxis_X;

    for (int anchorIdx = 0; anchorIdx < anchorCount; ++anchorIdx) {
        const char* name = anchorWindows[anchorIdx];
        ImGuiWindow* anchor = findWindowByVisibleTitle(name);
        if (!anchor || !anchor->DockNode) continue;

        ImGuiDockNode* source = anchor->DockNode;
        ImGuiDockNode* current = source;
        while (current && current->ParentNode) {
            ImGuiDockNode* parent = current->ParentNode;
            if (parent->SplitAxis == axis && parent->ChildNodes[0] && parent->ChildNodes[1]) {
                ImGuiDockNode* child0 = parent->ChildNodes[0];
                ImGuiDockNode* child1 = parent->ChildNodes[1];

                const bool sourceInChild0 = ImGui::DockNodeIsInHierarchyOf(source, child0);
                const bool sourceInChild1 = ImGui::DockNodeIsInHierarchyOf(source, child1);
                if (!sourceInChild0 && !sourceInChild1) {
                    current = parent;
                    continue;
                }

                ImGuiDockNode* drawerCandidate = sourceInChild0 ? child0 : child1;
                ImGuiDockNode* oppositeCandidate = (drawerCandidate == child0) ? child1 : child0;

                bool matchesSide = false;
                if (side == DockDrawerSide::Bottom) {
                    matchesSide = drawerCandidate->Pos.y >= oppositeCandidate->Pos.y - 0.5f;
                } else if (side == DockDrawerSide::Left) {
                    matchesSide = drawerCandidate->Pos.x <= oppositeCandidate->Pos.x + 0.5f;
                } else {
                    matchesSide = drawerCandidate->Pos.x >= oppositeCandidate->Pos.x - 0.5f;
                }

                if (matchesSide) {
                    return DockDrawerTarget{parent, drawerCandidate, oppositeCandidate};
                }
            }
            current = parent;
        }
    }

    return {};
}

void updateDockDrawerAnimation(DockDrawerState& state,
                               const DockDrawerTarget& target,
                               DockDrawerSide side,
                               const char* const* anchorWindows,
                               int anchorCount) {
    if (!target.splitParent || !target.drawerBranch || !target.oppositeBranch ||
        !target.splitParent->ChildNodes[0] || !target.splitParent->ChildNodes[1]) {
        state.activeSplitParentId = 0;
        state.collapsed = false;
        state.openAmount = 1.0f;
        state.expandedExtent = 0.0f;
        state.pendingTabFocusId = 0;
        return;
    }

    if (state.activeSplitParentId != target.splitParent->ID) {
        state.activeSplitParentId = target.splitParent->ID;
        state.collapsed = false;
        state.openAmount = 1.0f;
        state.expandedExtent =
            (side == DockDrawerSide::Bottom) ? ImMax(0.0f, target.drawerBranch->Size.y)
                                             : ImMax(0.0f, target.drawerBranch->Size.x);
        state.pendingTabFocusId = 0;
    }

    ImGuiTabBar* tabBar = target.drawerBranch->TabBar;
    applyPendingDrawerTabFocus(state, tabBar);
    constexpr float kCollapsedSideRailWidth = 25.0f;
    float collapsedExtent = (side == DockDrawerSide::Bottom)
        ? (ImGui::GetFrameHeight() + 8.0f)
        : kCollapsedSideRailWidth;
    if (tabBar) {
        if (side == DockDrawerSide::Bottom) {
            collapsedExtent = ImMax(collapsedExtent, tabBar->BarRect.GetHeight() + 6.0f);
        } else {
            collapsedExtent = ImMax(collapsedExtent, kCollapsedSideRailWidth);
        }
    }

    DockTabInteractionState interaction = queryDockTabInteraction(target, anchorWindows, anchorCount);
    if (interaction.doubleClicked) {
        if (!state.collapsed) {
            const float liveExtent =
                (side == DockDrawerSide::Bottom) ? target.drawerBranch->Size.y : target.drawerBranch->Size.x;
            if (liveExtent > 1.0f) {
                state.expandedExtent = liveExtent;
            }
        }
        state.collapsed = !state.collapsed;
    } else if (state.collapsed && interaction.clicked) {
        state.collapsed = false;
    }

    const float totalExtent =
        (side == DockDrawerSide::Bottom) ? ImMax(0.0f, target.splitParent->Size.y)
                                         : ImMax(0.0f, target.splitParent->Size.x);
    const float minOppositeExtent = (side == DockDrawerSide::Bottom) ? 96.0f : 220.0f;
    const float maxDrawerExtent = ImMax(collapsedExtent, totalExtent - minOppositeExtent);
    const float expandedMinExtent =
        ImMin(collapsedExtent + ((side == DockDrawerSide::Bottom) ? 24.0f : 40.0f), maxDrawerExtent);

    const bool captureExpandedExtent = !state.collapsed && state.openAmount >= 0.995f;
    if (captureExpandedExtent) {
        const float liveExtent =
            (side == DockDrawerSide::Bottom) ? target.drawerBranch->Size.y : target.drawerBranch->Size.x;
        const float clampedLiveExtent = std::clamp(liveExtent, collapsedExtent, maxDrawerExtent);
        if (clampedLiveExtent > collapsedExtent + 1.0f) {
            state.expandedExtent = clampedLiveExtent;
        }
    }

    if (state.expandedExtent < collapsedExtent + 1.0f) {
        const float defaultRatio = (side == DockDrawerSide::Bottom) ? 0.30f : 0.22f;
        state.expandedExtent = std::clamp(totalExtent * defaultRatio, expandedMinExtent, maxDrawerExtent);
    } else {
        state.expandedExtent = std::clamp(state.expandedExtent, expandedMinExtent, maxDrawerExtent);
    }

    const float targetOpen = state.collapsed ? 0.0f : 1.0f;
    const float blend = 1.0f - std::exp(-12.0f * ImGui::GetIO().DeltaTime);
    state.openAmount += (targetOpen - state.openAmount) * blend;
    if (std::fabs(state.openAmount - targetOpen) < 0.001f) {
        state.openAmount = targetOpen;
    }
    state.openAmount = std::clamp(state.openAmount, 0.0f, 1.0f);

    const float desiredDrawerExtent =
        collapsedExtent + (state.expandedExtent - collapsedExtent) * state.openAmount;
    const float drawerExtent = std::clamp(desiredDrawerExtent, collapsedExtent, maxDrawerExtent);
    const float oppositeExtent = ImMax(minOppositeExtent, totalExtent - drawerExtent);

    target.splitParent->AuthorityForSize = ImGuiDataAuthority_DockNode;
    target.drawerBranch->AuthorityForSize = ImGuiDataAuthority_DockNode;
    target.oppositeBranch->AuthorityForSize = ImGuiDataAuthority_DockNode;
    if (side == DockDrawerSide::Bottom) {
        target.drawerBranch->Size.y = drawerExtent;
        target.drawerBranch->SizeRef.y = drawerExtent;
        target.oppositeBranch->Size.y = oppositeExtent;
        target.oppositeBranch->SizeRef.y = oppositeExtent;
    } else {
        target.drawerBranch->Size.x = drawerExtent;
        target.drawerBranch->SizeRef.x = drawerExtent;
        target.oppositeBranch->Size.x = oppositeExtent;
        target.oppositeBranch->SizeRef.x = oppositeExtent;
    }

    if (side != DockDrawerSide::Bottom) {
        const float railReveal = std::clamp(1.0f - state.openAmount, 0.0f, 1.0f);
        const bool useSideRail = railReveal > 0.01f;
        ImGuiDockNodeFlags desiredFlags = target.drawerBranch->LocalFlags;
        if (state.collapsed) {
            desiredFlags |= ImGuiDockNodeFlags_HiddenTabBar;
        } else {
            desiredFlags &= ~ImGuiDockNodeFlags_HiddenTabBar;
        }
        if (desiredFlags != target.drawerBranch->LocalFlags) {
            target.drawerBranch->SetLocalFlags(desiredFlags);
        }
        if (useSideRail) {
            renderCollapsedSideDockRail(state, target, side, collapsedExtent, railReveal);
        }
    }
}

void updateDockDrawerAnimations() {
    static int lastUpdatedFrame = -1;
    const int currentFrame = ImGui::GetFrameCount();
    if (lastUpdatedFrame == currentFrame) {
        return;
    }
    lastUpdatedFrame = currentFrame;

    static DockDrawerState leftState;
    static DockDrawerState rightState;
    static DockDrawerState bottomState;

    static const char* kLeftAnchors[] = {
        "Hierarchy",
        "Camera"
    };
    static const char* kRightAnchors[] = {
        "Inspector",
        "Environment"
    };
    static const char* kBottomAnchors[] = {
        "Project",
        "Project Settings",
        "Animation",
        "AI Pathfinding"
    };

    updateDockDrawerAnimation(leftState,
                              findDockDrawerTarget(kLeftAnchors, IM_ARRAYSIZE(kLeftAnchors), DockDrawerSide::Left),
                              DockDrawerSide::Left,
                              kLeftAnchors, IM_ARRAYSIZE(kLeftAnchors));
    updateDockDrawerAnimation(rightState,
                              findDockDrawerTarget(kRightAnchors, IM_ARRAYSIZE(kRightAnchors), DockDrawerSide::Right),
                              DockDrawerSide::Right,
                              kRightAnchors, IM_ARRAYSIZE(kRightAnchors));
    updateDockDrawerAnimation(bottomState,
                              findDockDrawerTarget(kBottomAnchors, IM_ARRAYSIZE(kBottomAnchors), DockDrawerSide::Bottom),
                              DockDrawerSide::Bottom,
                              kBottomAnchors, IM_ARRAYSIZE(kBottomAnchors));
}
} // namespace

void Engine::renderMainMenuBar() {
    refreshScriptEditorWindows();

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
            if (ImGui::MenuItem("Build Settings...")) {
                showBuildSettings = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close Project")) {
                if (projectManager.currentProject.hasUnsavedChanges) {
                    saveCurrentScene();
                }
                projectManager.currentProject = Project();
                sceneObjects.clear();
                clearSelection();
                scriptEditorWindows.clear();
                scriptEditorWindowsDirty = true;
                resetBuildSettings();
                showBuildSettings = false;
                playerMode = false;
                autoStartRequested = false;
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
            if (hasScriptingWindowPackage()) {
                ImGui::MenuItem("Scripting", nullptr, &showScriptingWindow);
            }
            bool prevProjectBrowser = showProjectBrowser;
            ImGui::MenuItem("Project Settings", nullptr, &showProjectBrowser);
            if (prevProjectBrowser != showProjectBrowser) {
                saveEditorUserSettings();
            }
            bool prevRegistryPackages = showRegistryPackagesWindow;
            ImGui::MenuItem("Modupak Manager", nullptr, &showRegistryPackagesWindow);
            if (prevRegistryPackages != showRegistryPackagesWindow) {
                saveEditorUserSettings();
            }
            if (hasMeshBuilderPackage()) {
                ImGui::MenuItem("Mesh Builder (Legacy Window)", nullptr, &showMeshBuilder);
            }
            ImGui::MenuItem("Environment", nullptr, &showEnvironmentWindow);
            ImGui::MenuItem("Camera", nullptr, &showCameraWindow);
            bool prevAnimationWindow = showAnimationWindow;
            ImGui::MenuItem("Animation", nullptr, &showAnimationWindow);
            if (prevAnimationWindow != showAnimationWindow) {
                saveEditorUserSettings();
            }
            bool prevAIPathWindow = showAIPathfindingWindow;
            ImGui::MenuItem("AI Pathfinding", nullptr, &showAIPathfindingWindow);
            if (prevAIPathWindow != showAIPathfindingWindow) {
                saveEditorUserSettings();
            }
            if (hasSpriteEditorPackage()) {
                bool prevPixelSpriteEditor = showPixelSpriteEditorWindow;
                ImGui::MenuItem("Pixel Sprite Editor", nullptr, &showPixelSpriteEditorWindow);
                if (prevPixelSpriteEditor != showPixelSpriteEditorWindow) {
                    saveEditorUserSettings();
                }
            }
            ImGui::MenuItem("View Output", nullptr, &showViewOutput);
            ImGui::Separator();
            if (isProject2DPipeline()) {
                bool forced2DOverlay = true;
                ImGui::BeginDisabled();
                ImGui::MenuItem("UI World Overlay", nullptr, &forced2DOverlay);
                ImGui::EndDisabled();
            } else {
                ImGui::MenuItem("UI World Overlay", nullptr, &uiWorldMode);
            }
            ImGui::MenuItem("3D Grid", nullptr, &showSceneGrid3D);
            if (!scriptEditorWindows.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("Scripted Windows");
                for (auto& window : scriptEditorWindows) {
                    ImGui::MenuItem(window.label.c_str(), nullptr, &window.open);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Fullscreen Viewport", "F11", viewportFullscreen)) {
                viewportFullscreen = !viewportFullscreen;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Style")) {
            ImGui::TextDisabled("Editor Styles");
            for (size_t i = 0; i < uiStylePresets.size(); ++i) {
                bool selected = static_cast<int>(i) == uiStylePresetIndex;
                if (ImGui::MenuItem(uiStylePresets[i].name.c_str(), nullptr, selected)) {
                    applyUIStylePresetByName(uiStylePresets[i].name);
                    saveEditorUserSettings();
                }
            }
            ImGui::Separator();
            ImGui::TextDisabled("UI Animations");
            if (ImGui::MenuItem("Fluid", nullptr, uiAnimationMode == UIAnimationMode::Fluid)) {
                uiAnimationMode = UIAnimationMode::Fluid;
                saveEditorUserSettings();
            }
            if (ImGui::MenuItem("Snappy", nullptr, uiAnimationMode == UIAnimationMode::Snappy)) {
                uiAnimationMode = UIAnimationMode::Snappy;
                saveEditorUserSettings();
            }
            if (ImGui::MenuItem("Off", nullptr, uiAnimationMode == UIAnimationMode::Off)) {
                uiAnimationMode = UIAnimationMode::Off;
                saveEditorUserSettings();
            }
            ImGui::Separator();
            ImGui::MenuItem("Style Editor", nullptr, &showStyleEditor);
            if (ImGui::MenuItem("Export Theme + Layout")) {
                exportEditorThemeLayout();
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
            if (ImGui::MenuItem("Empty")) addObject(ObjectType::Empty, "Empty");
            if (ImGui::MenuItem("Cube")) addObject(ObjectType::Cube, "Cube");
            if (ImGui::MenuItem("Sphere")) addObject(ObjectType::Sphere, "Sphere");
            if (ImGui::MenuItem("Capsule")) addObject(ObjectType::Capsule, "Capsule");
            if (ImGui::MenuItem("Plane")) addObject(ObjectType::Plane, "Plane");
            if (ImGui::MenuItem("Torus")) addObject(ObjectType::Torus, "Torus");
            if (ImGui::MenuItem("2.5D Object")) addObject(ObjectType::Sprite25D, "2.5D Object");
            if (ImGui::MenuItem("Mirror")) addObject(ObjectType::Mirror, "Mirror");
            if (ImGui::MenuItem("Camera")) addObject(ObjectType::Camera, "Camera");
            if (ImGui::MenuItem("Directional Light")) addObject(ObjectType::DirectionalLight, "Directional Light");
            if (ImGui::MenuItem("Point Light")) addObject(ObjectType::PointLight, "Point Light");
            if (ImGui::MenuItem("Spot Light")) addObject(ObjectType::SpotLight, "Spot Light");
            if (ImGui::MenuItem("Area Light")) addObject(ObjectType::AreaLight, "Area Light");
            if (ImGui::MenuItem("ModuVolume")) addObject(ObjectType::PostFXNode, "ModuVolume");
            if (ImGui::MenuItem("Audio Reverb Zone")) {
                addObject(ObjectType::Empty, "Reverb Zone");
                if (!sceneObjects.empty()) {
                    sceneObjects.back().hasReverbZone = true;
                    sceneObjects.back().reverbZone = ReverbZoneComponent{};
                    sceneObjects.back().reverbZone.boxSize = glm::max(sceneObjects.back().scale, glm::vec3(1.0f));
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                logToConsole("Modularity Engine - Beta V6.7\nThis build is in beta and might have issues,\n\nif you'd like to report any bugs or missing features, feel free to contact us!");
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();
        ImGui::TextColored(subtle, "Workspace");
        ImGui::SameLine();
        struct WorkspaceTabConfig {
            WorkspaceMode mode;
            const char* label;
        };
        const WorkspaceTabConfig workspaceTabs[] = {
            { WorkspaceMode::Default, "Default" },
            { WorkspaceMode::Animation, "Animation" },
            { WorkspaceMode::Scripting, "Scripting" }
        };
        auto workspaceToIndex = [](WorkspaceMode mode) {
            switch (mode) {
                case WorkspaceMode::Default: return 0;
                case WorkspaceMode::Animation: return 1;
                case WorkspaceMode::Scripting: return 2;
            }
            return 0;
        };
        auto visibleWorkspaceCount = [&]() {
            int count = 0;
            for (bool visible : workspaceTabVisible) {
                if (visible) {
                    ++count;
                }
            }
            return count;
        };

        bool workspaceVisibilityChanged = false;
        bool workspaceSelectionChanged = false;
        WorkspaceMode targetWorkspace = currentWorkspace;
        const double now = glfwGetTime();
        const bool workspaceTransitionActive =
            pendingWorkspaceReload || workspaceLayoutDirty || now < workspaceLayoutStabilizeUntil;
        const bool workspaceSwitchLocked = now < workspaceSwitchLockUntil;
        if (ImGui::BeginTabBar("##WorkspaceTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs)) {
            int canCloseCount = visibleWorkspaceCount();
            for (const WorkspaceTabConfig& tab : workspaceTabs) {
                const int idx = workspaceToIndex(tab.mode);
                if (tab.mode == WorkspaceMode::Scripting && !hasScriptingWindowPackage()) {
                    continue;
                }
                if (!workspaceTabVisible[idx]) {
                    continue;
                }

                bool open = true;
                if (ImGui::BeginTabItem(tab.label, &open)) {
                    const bool shouldSwitchWorkspace =
                        !workspaceTransitionActive &&
                        !workspaceSwitchLocked &&
                        (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemActivated()) &&
                        currentWorkspace != tab.mode;
                    if (shouldSwitchWorkspace) {
                        targetWorkspace = tab.mode;
                        workspaceSelectionChanged = true;
                    }
                    ImGui::EndTabItem();
                }

                if (!open) {
                    if (canCloseCount > 1) {
                        workspaceTabVisible[idx] = false;
                        workspaceVisibilityChanged = true;
                        --canCloseCount;
                        if (targetWorkspace == tab.mode) {
                            for (const WorkspaceTabConfig& fallback : workspaceTabs) {
                                if (workspaceTabVisible[workspaceToIndex(fallback.mode)]) {
                                    targetWorkspace = fallback.mode;
                                    workspaceSelectionChanged = true;
                                    break;
                                }
                            }
                        }
                    } else {
                        workspaceTabVisible[idx] = true;
                    }
                }
            }
            ImGui::EndTabBar();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+##WorkspaceTabsAdd")) {
            ImGui::OpenPopup("WorkspaceTabsAddPopup");
        }
        if (ImGui::BeginPopup("WorkspaceTabsAddPopup")) {
            for (const WorkspaceTabConfig& tab : workspaceTabs) {
                const int idx = workspaceToIndex(tab.mode);
                if (tab.mode == WorkspaceMode::Scripting && !hasScriptingWindowPackage()) {
                    continue;
                }
                if (workspaceTabVisible[idx]) {
                    continue;
                }
                if (ImGui::MenuItem(tab.label)) {
                    workspaceTabVisible[idx] = true;
                    workspaceVisibilityChanged = true;
                }
            }
            ImGui::EndPopup();
        }
        if (workspaceSelectionChanged && currentWorkspace != targetWorkspace) {
            saveWorkspaceLayout(currentWorkspace);
            applyWorkspacePreset(targetWorkspace, true);
            workspaceSwitchLockUntil = glfwGetTime() + 0.22;
        }
        if (workspaceVisibilityChanged || workspaceSelectionChanged) {
            saveEditorUserSettings();
        }

        ImGui::SameLine();
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

    auto layoutFileHasDockNodesForDockspace = [](const fs::path& layoutPath, ImGuiID dockspaceId) {
        if (dockspaceId == 0) {
            return false;
        }
        std::ifstream in(layoutPath);
        if (!in.is_open()) {
            return false;
        }

        char dockspaceIdHex[16];
        std::snprintf(dockspaceIdHex, sizeof(dockspaceIdHex), "0x%08X", dockspaceId);

        bool hasDockingData = false;
        bool hasDockNodes = false;
        bool hasMatchingDockspace = false;
        std::string line;
        while (std::getline(in, line)) {
            if (line == "[Docking][Data]") {
                hasDockingData = true;
                continue;
            }
            if (!hasDockingData) {
                continue;
            }
            if (!line.empty() && line.front() == '[') {
                break;
            }
            if (line.find("DockNode") != std::string::npos) {
                hasDockNodes = true;
            }
            if (line.find("DockSpace") != std::string::npos &&
                line.find(dockspaceIdHex) != std::string::npos) {
                hasMatchingDockspace = true;
            }
        }
        return hasDockingData && hasDockNodes && hasMatchingDockspace;
    };

    if (pendingWorkspaceReload) {
        if (mainDockspaceId != 0) {
            const bool hasLayoutFile = !pendingWorkspaceIniPath.empty() && fs::exists(pendingWorkspaceIniPath);
            const bool hasMatchingDockspace =
                hasLayoutFile && layoutFileHasDockNodesForDockspace(pendingWorkspaceIniPath, mainDockspaceId);
            if (hasMatchingDockspace) {
                ImGui::ClearIniSettings();
                ImGui::LoadIniSettingsFromDisk(pendingWorkspaceIniPath.string().c_str());
                workspaceLayoutSettlingFrame = true;
                if (ImGui::DockBuilderGetNode(mainDockspaceId) == nullptr) {
                    ImGui::DockBuilderRemoveNode(mainDockspaceId);
                    workspaceLayoutDirty = true;
                    workspaceLayoutAutoRepairPending = true;
                } else {
                    // A valid persisted layout should win over the default workspace
                    // repair path instead of being rebuilt a frame later.
                    workspaceLayoutAutoRepairPending = false;
                }
            } else {
                // No persisted layout to load (or stale DockSpace ID): force deterministic rebuild.
                ImGui::ClearIniSettings();
                ImGui::DockBuilderRemoveNode(mainDockspaceId);
                workspaceLayoutDirty = true;
                workspaceLayoutAutoRepairPending = true;
                workspaceLayoutSettlingFrame = true;
            }
            pendingWorkspaceReload = false;
            workspaceLayoutSavePending = false;
            workspaceLayoutStabilizeUntil = glfwGetTime() + 0.75;
        }
    }

    if (!pendingWorkspaceReload && workspaceLayoutDirty) {
        ImGui::ClearIniSettings();
        buildWorkspaceLayout(currentWorkspace);
        workspaceLayoutSavePending = true;
        workspaceLayoutSettlingFrame = true;
    }

    if (showStyleEditor) {
        if (ImGui::Begin("Style Editor", &showStyleEditor)) {
            if (ImGui::Button("Save Colors")) {
                saveEditorUserSettings();
            }
            ImGui::SameLine();
            if (ImGui::Button("Export Theme + Layout")) {
                exportEditorThemeLayout();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Applies to all presets");
            ImGui::Separator();
            ImGuiStyle& style = ImGui::GetStyle();
            ImGui::ShowStyleEditor(&style);
        }
        ImGui::End();
    }
}

void Engine::applyWorkspacePreset(WorkspaceMode mode, bool rebuildLayout) {
    currentWorkspace = mode;
    workspaceLayoutSavePending = false;
    workspaceLayoutAutoRepairPending = true;
    workspaceLayoutStabilizeUntil = glfwGetTime() + 0.75;
    switch (mode) {
        case WorkspaceMode::Default:
            showHierarchy = true;
            showInspector = true;
            showFileBrowser = true;
            showConsole = true;
            showScriptingWindow = false;
            showAnimationWindow = false;
            showAIPathfindingWindow = false;
            showEnvironmentWindow = true;
            showCameraWindow = true;
            showGameViewport = true;
            break;
        case WorkspaceMode::Animation:
            showHierarchy = true;
            showInspector = true;
            showFileBrowser = false;
            showConsole = true;
            showScriptingWindow = false;
            showAnimationWindow = true;
            showAIPathfindingWindow = true;
            showEnvironmentWindow = false;
            showCameraWindow = false;
            showGameViewport = true;
            break;
        case WorkspaceMode::Scripting:
            showHierarchy = true;
            showInspector = true;
            showFileBrowser = true;
            showConsole = true;
            showScriptingWindow = hasScriptingWindowPackage();
            showAnimationWindow = false;
            showAIPathfindingWindow = false;
            showEnvironmentWindow = false;
            showCameraWindow = false;
            showGameViewport = true;
            break;
    }

    if (rebuildLayout) {
        // Explicit workspace switches should rebuild from the preset instead of
        // reloading stale persisted dock data that can fight the new layout.
        pendingWorkspaceIniPath.clear();
        pendingWorkspaceReload = false;
        buildWorkspaceLayout(mode);
        if (workspaceLayoutDirty) {
            pendingWorkspaceReload = true;
        }
        workspaceLayoutStabilizeUntil = glfwGetTime() + 0.75;
        return;
    }

    auto layoutFileIsUsable = [&](const fs::path& layoutPath) {
        std::ifstream in(layoutPath);
        if (!in.is_open()) return false;

        bool hasDockingData = false;
        bool hasDockNodes = false;
        bool hasDockspace = false;
        std::string line;
        while (std::getline(in, line)) {
            if (line == "[Docking][Data]") {
                hasDockingData = true;
                continue;
            }
            if (hasDockingData && line.find("DockNode") != std::string::npos) {
                hasDockNodes = true;
            }
            if (hasDockingData && line.find("DockSpace") != std::string::npos) {
                hasDockspace = true;
            }
        }
        if (!(hasDockingData && hasDockNodes && hasDockspace)) {
            return false;
        }

        if (loadedWorkspaceLayoutVersion < kWorkspaceLayoutVersion &&
            layoutFileNeedsUtilityDockMigration(layoutPath, showProjectBrowser, showRegistryPackagesWindow)) {
            return false;
        }

        return true;
    };

    fs::path layoutPath = getWorkspaceLayoutPath(mode);
    if (!layoutPath.empty() && fs::exists(layoutPath) && layoutFileIsUsable(layoutPath)) {
        pendingWorkspaceIniPath = layoutPath;
        pendingWorkspaceReload = true;
        workspaceLayoutDirty = false;
        workspaceLayoutStabilizeUntil = glfwGetTime() + 0.75;
        return;
    }

    // applyWorkspacePreset() is also called during project/settings load, where
    // there may be no active ImGui window for ImGui::GetID() yet.
    // Defer dock layout rebuild to the normal UI frame path.
    pendingWorkspaceIniPath.clear();
    pendingWorkspaceReload = true;
    workspaceLayoutDirty = true;
    workspaceLayoutStabilizeUntil = glfwGetTime() + 0.75;
}

void Engine::buildWorkspaceLayout(WorkspaceMode mode) {
    if (!ImGui::GetCurrentContext()) {
        workspaceLayoutDirty = true;
        return;
    }

    if (mainDockspaceId == 0) {
        workspaceLayoutDirty = true;
        return;
    }
    const ImGuiID dockspaceId = mainDockspaceId;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) {
        workspaceLayoutDirty = true;
        return;
    }

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImVec2 dockspaceSize = viewport->WorkSize;
    if (ImGuiWindow* dockHost = ImGui::FindWindowByName("DockSpace")) {
        dockspaceSize = dockHost->Size;
    }
    dockspaceSize.y = ImMax(0.0f, dockspaceSize.y - getEditorBottomStatusReserveHeight());
    ImGui::DockBuilderSetNodeSize(dockspaceId, dockspaceSize);

    ImGuiID dockMain = dockspaceId;
    if (mode == WorkspaceMode::Default) {
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, nullptr, &dockMain);
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.28f, nullptr, &dockMain);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.28f, nullptr, &dockMain);
        ImGuiID dockUtility = ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.42f, nullptr, &dockRight);

        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Camera", dockLeft);
        ImGui::DockBuilderDockWindow("Inspector", dockRight);
        ImGui::DockBuilderDockWindow("Environment", dockRight);
        ImGui::DockBuilderDockWindow("Project", dockBottom);
        ImGui::DockBuilderDockWindow("Project Settings", dockUtility);
        ImGui::DockBuilderDockWindow("Modupak Manager", dockUtility);
        if (hasSpriteEditorPackage()) {
            ImGui::DockBuilderDockWindow("Pixel Sprite Editor", dockMain);
        }
        ImGui::DockBuilderDockWindow("Viewport", dockMain);
        ImGui::DockBuilderDockWindow("Game Viewport", dockMain);
    } else if (mode == WorkspaceMode::Animation) {
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, nullptr, &dockMain);
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.27f, nullptr, &dockMain);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.35f, nullptr, &dockMain);
        ImGuiID dockUtility = ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.42f, nullptr, &dockRight);

        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Camera", dockLeft);
        ImGui::DockBuilderDockWindow("Inspector", dockRight);
        ImGui::DockBuilderDockWindow("Environment", dockRight);
        ImGui::DockBuilderDockWindow("Animation", dockBottom);
        ImGui::DockBuilderDockWindow("AI Pathfinding", dockBottom);
        ImGui::DockBuilderDockWindow("Project", dockBottom);
        ImGui::DockBuilderDockWindow("Project Settings", dockUtility);
        ImGui::DockBuilderDockWindow("Modupak Manager", dockUtility);
        if (hasSpriteEditorPackage()) {
            ImGui::DockBuilderDockWindow("Pixel Sprite Editor", dockMain);
        }
        ImGui::DockBuilderDockWindow("Viewport", dockMain);
    } else {
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.25f, nullptr, &dockMain);
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.35f, nullptr, &dockMain);
        ImGuiID dockUtility = ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.42f, nullptr, &dockRight);

        ImGui::DockBuilderDockWindow("Project", dockLeft);
        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Camera", dockLeft);
        if (hasScriptingWindowPackage()) {
            ImGui::DockBuilderDockWindow("Scripting", dockRight);
        }
        ImGui::DockBuilderDockWindow("Inspector", dockRight);
        ImGui::DockBuilderDockWindow("Environment", dockRight);
        ImGui::DockBuilderDockWindow("Project Settings", dockUtility);
        ImGui::DockBuilderDockWindow("Modupak Manager", dockUtility);
        if (hasSpriteEditorPackage()) {
            ImGui::DockBuilderDockWindow("Pixel Sprite Editor", dockMain);
        }
        ImGui::DockBuilderDockWindow("Viewport", dockMain);
        ImGui::DockBuilderDockWindow("Game Viewport", dockMain);
    }

    ImGui::DockBuilderFinish(dockspaceId);
    workspaceLayoutDirty = false;
    workspaceLayoutSavePending = false;
    workspaceLayoutStabilizeUntil = glfwGetTime() + 0.75;
}

#pragma endregion

#pragma region Scene Viewport
// Final scene output for the editor viewport.
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
    const bool windowVisible = ImGui::Begin("Viewport", nullptr, viewportFlags);
    ImGui::PopStyleVar();
    if (!windowVisible) {
        ImGui::End();
        return;
    }

    ImVec2 panelMin = ImGui::GetCursorScreenPos();
    ImVec2 panelSize = ImGui::GetContentRegionAvail();
    panelSize.x = std::max(1.0f, panelSize.x);
    panelSize.y = std::max(1.0f, panelSize.y);

    getSceneViewportInternalResolution(viewportWidth, viewportHeight);
    if (rendererInitialized) {
        renderer.resize(viewportWidth, viewportHeight);
    }

    const EmbeddedViewportLayout sceneLayout = BuildEmbeddedViewportLayout(
        panelMin,
        panelSize,
        viewportWidth,
        viewportHeight,
        sceneViewportDisplayMode);
    ImVec2 imageSize = sceneLayout.displaySize;

    bool mouseOverViewportImage = false;
    bool blockSelection = false;
    ImVec2 viewportImageMin(0.0f, 0.0f);
    ImVec2 viewportImageMax(0.0f, 0.0f);
    bool hasViewportImageRect = false;
    const bool project2DPipeline = isProject2DPipeline();
    const bool worldUiEditing = is2DWorldEditingEnabled();
    const bool hasVulkanSceneTexture = usingVulkan() && vulkanRendererInitialized && (vulkanRenderer != nullptr);
    int activeGameResolutionWidth = 0;
    int activeGameResolutionHeight = 0;
    switch (gameViewportResolutionIndex) {
        case 0:
            if (gameViewportLastRenderWidth > 0 && gameViewportLastRenderHeight > 0) {
                activeGameResolutionWidth = gameViewportLastRenderWidth;
                activeGameResolutionHeight = gameViewportLastRenderHeight;
            } else {
                activeGameResolutionWidth = std::max(1, sceneViewportRenderWidth);
                activeGameResolutionHeight = std::max(1, sceneViewportRenderHeight);
            }
            break;
        case 1:
            activeGameResolutionWidth = 1920;
            activeGameResolutionHeight = 1080;
            break;
        case 2:
            activeGameResolutionWidth = 1280;
            activeGameResolutionHeight = 720;
            break;
        case 3:
            activeGameResolutionWidth = 2560;
            activeGameResolutionHeight = 1440;
            break;
        case 4:
            activeGameResolutionWidth = std::clamp(gameViewportCustomWidth, 64, 8192);
            activeGameResolutionHeight = std::clamp(gameViewportCustomHeight, 64, 8192);
            break;
        default:
            activeGameResolutionWidth = std::max(1, sceneViewportRenderWidth);
            activeGameResolutionHeight = std::max(1, sceneViewportRenderHeight);
            break;
    }
    int worldUiReferenceResolutionWidth = activeGameResolutionWidth;
    int worldUiReferenceResolutionHeight = activeGameResolutionHeight;
    if (worldUiEditing) {
        int bestArea = 0;
        for (const auto& obj : sceneObjects) {
            if (!IsObjectEnabledInHierarchy(obj)) {
                continue;
            }
            if (!(obj.hasUI && obj.ui.type == UIElementType::Canvas) || obj.ui.renderIn3D) {
                continue;
            }
            const int canvasWidth = std::clamp(static_cast<int>(std::round(std::max(1.0f, obj.ui.size.x))), 1, 8192);
            const int canvasHeight = std::clamp(static_cast<int>(std::round(std::max(1.0f, obj.ui.size.y))), 1, 8192);
            const int area = canvasWidth * canvasHeight;
            if (area > bestArea) {
                bestArea = area;
                worldUiReferenceResolutionWidth = canvasWidth;
                worldUiReferenceResolutionHeight = canvasHeight;
            }
        }
    }
    const float activeGameResolutionAspect = static_cast<float>(activeGameResolutionWidth) /
        std::max(1.0f, static_cast<float>(activeGameResolutionHeight));

    if (hasVulkanSceneTexture) {
        vulkanRenderer->setViewportSceneSize(static_cast<uint32_t>(std::max(1, viewportWidth)),
                                             static_cast<uint32_t>(std::max(1, viewportHeight)));
    }

    if (!rendererInitialized && !hasVulkanSceneTexture) {
        ImGui::InvisibleButton("##SceneViewportPanelEmpty", panelSize);
        ImVec2 imageMin = sceneLayout.displayMin;
        ImVec2 imageMax = sceneLayout.displayMax;
        ImVec2 drawSize(std::max(1.0f, imageMax.x - imageMin.x), std::max(1.0f, imageMax.y - imageMin.y));
        viewportImageMin = imageMin;
        viewportImageMax = imageMax;
        hasViewportImageRect = true;
        glm::vec2 hoveredPixel(0.0f);
        mouseOverViewportImage = TryMapScreenPointToRenderPixel(
            sceneLayout,
            ImGui::GetIO().MousePos,
            viewportWidth,
            viewportHeight,
            hoveredPixel);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(imageMin, imageMax, IM_COL32(14, 18, 30, 255), 8.0f);
        dl->AddRect(imageMin, imageMax, IM_COL32(78, 96, 128, 210), 8.0f, 0, 1.5f);

        const char* title = usingVulkan()
            ? "Vulkan Scene Viewport Unavailable"
            : "Scene Viewport Unavailable";
        const char* line1 = usingVulkan()
            ? "Vulkan scene render target is not ready."
            : "Renderer is not initialized for this session.";
        const char* line2 = usingVulkan()
            ? "Open a project scene or retry after renderer initialization."
            : "Open or create a project to initialize rendering.";

        ImVec2 titleSize = ImGui::CalcTextSize(title);
        ImVec2 line1Size = ImGui::CalcTextSize(line1);
        ImVec2 line2Size = ImGui::CalcTextSize(line2);
        float centerX = imageMin.x + drawSize.x * 0.5f;
        float baseY = imageMin.y + drawSize.y * 0.5f - 28.0f;
        dl->AddText(ImVec2(centerX - titleSize.x * 0.5f, baseY),
                    IM_COL32(220, 228, 244, 255),
                    title);
        dl->AddText(ImVec2(centerX - line1Size.x * 0.5f, baseY + 24.0f),
                    IM_COL32(170, 184, 212, 255),
                    line1);
        dl->AddText(ImVec2(centerX - line2Size.x * 0.5f, baseY + 44.0f),
                    IM_COL32(170, 184, 212, 255),
                    line2);
    }

    if (rendererInitialized || hasVulkanSceneTexture) {
        glm::mat4 proj = glm::perspective(
            glm::radians(buildSettings.editorCameraFov),
            (float)viewportWidth / (float)viewportHeight,
            buildSettings.editorCameraNear, buildSettings.editorCameraFar
        );

        glm::mat4 view = camera.getViewMatrix();
        if (rendererInitialized) {
            const bool showSelected3DColliderPreview = [&]() {
                if (worldUiEditing) return false;
                auto shouldPreview = [&](int id) {
                    auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(), [&](const SceneObject& obj) {
                        return obj.id == id;
                    });
                    return it != sceneObjects.end() &&
                           IsObjectEnabledInHierarchy(*it) &&
                           it->hasCollider &&
                           it->collider.enabled;
                };
                for (int id : selectedObjectIds) {
                    if (shouldPreview(id)) return true;
                }
                return selectedObjectIds.empty() && selectedObjectId >= 0 && shouldPreview(selectedObjectId);
            }();
            renderer.beginRender(view, proj, camera.position);
            renderer.renderScene(camera, sceneObjects, selectedObjectId,
                                buildSettings.editorCameraFov,
                                buildSettings.editorCameraNear,
                                buildSettings.editorCameraFar,
                                showSelected3DColliderPreview,
                                &selectedObjectIds);
            unsigned int tex = renderer.getViewportTexture();
            ImGui::InvisibleButton("##SceneViewportPanel", panelSize);
            if (tex != 0) {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(sceneLayout.panelMin, sceneLayout.panelMax, IM_COL32(10, 12, 18, 255));
                ApplyNearestTextureSampling(tex);
                drawList->PushClipRect(sceneLayout.panelMin, sceneLayout.panelMax, true);
                drawList->AddImage((void*)(intptr_t)tex,
                                   sceneLayout.displayMin,
                                   sceneLayout.displayMax,
                                   ImVec2(sceneLayout.uvMin.x, 1.0f - sceneLayout.uvMin.y),
                                   ImVec2(sceneLayout.uvMax.x, 1.0f - sceneLayout.uvMax.y));
                drawList->PopClipRect();
            }
        } else {
            ImTextureID texId = vulkanRenderer ? vulkanRenderer->getViewportSceneTextureID() : static_cast<ImTextureID>(0);
            ImGui::InvisibleButton("##SceneViewportPanelVk", panelSize);
            if (texId != static_cast<ImTextureID>(0)) {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(sceneLayout.panelMin, sceneLayout.panelMax, IM_COL32(10, 12, 18, 255));
                drawList->PushClipRect(sceneLayout.panelMin, sceneLayout.panelMax, true);
                drawList->AddImage(texId,
                                   sceneLayout.displayMin,
                                   sceneLayout.displayMax,
                                   sceneLayout.uvMin,
                                   sceneLayout.uvMax);
                drawList->PopClipRect();
            }
        }

        ImVec2 imageMin = sceneLayout.displayMin;
        ImVec2 imageMax = sceneLayout.displayMax;
        viewportImageMin = imageMin;
        viewportImageMax = imageMax;
        hasViewportImageRect = true;
        glm::vec2 hoveredPixel(0.0f);
        mouseOverViewportImage = TryMapScreenPointToRenderPixel(
            sceneLayout,
            ImGui::GetIO().MousePos,
            viewportWidth,
            viewportHeight,
            hoveredPixel);
        ImDrawList* viewportDrawList = ImGui::GetWindowDrawList();
        viewportDrawList->PushClipRect(sceneLayout.panelMin, sceneLayout.panelMax, true);

        if (worldUiEditing) {
            viewportDrawList->AddRectFilled(imageMin, imageMax, IM_COL32(14, 16, 20, 255));
        } else if (showSceneGrid3D) {
            auto projectToScreen = [&](const glm::vec3& p) -> std::optional<ImVec2> {
                glm::vec4 clip = proj * view * glm::vec4(p, 1.0f);
                if (clip.w <= 0.0f) return std::nullopt;
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                ImVec2 screen;
                screen.x = imageMin.x + (ndc.x * 0.5f + 0.5f) * (imageMax.x - imageMin.x);
                screen.y = imageMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * (imageMax.y - imageMin.y);
                return screen;
            };
            auto clipLineToScreen = [&](glm::vec3 a, glm::vec3 b, ImVec2& outA, ImVec2& outB) -> bool {
                glm::vec4 va = view * glm::vec4(a, 1.0f);
                glm::vec4 vb = view * glm::vec4(b, 1.0f);
                const float nearZ = -buildSettings.editorCameraNear;
                if (va.z > nearZ && vb.z > nearZ) {
                    return false;
                }
                if (va.z > nearZ || vb.z > nearZ) {
                    float t = (nearZ - va.z) / (vb.z - va.z);
                    t = std::clamp(t, 0.0f, 1.0f);
                    glm::vec4 vclip = va + (vb - va) * t;
                    if (va.z > nearZ) {
                        va = vclip;
                    } else {
                        vb = vclip;
                    }
                }
                glm::vec4 ca = proj * va;
                glm::vec4 cb = proj * vb;
                if (ca.w <= 0.0f || cb.w <= 0.0f) return false;
                glm::vec3 ndcA = glm::vec3(ca) / ca.w;
                glm::vec3 ndcB = glm::vec3(cb) / cb.w;
                outA = ImVec2(
                    imageMin.x + (ndcA.x * 0.5f + 0.5f) * (imageMax.x - imageMin.x),
                    imageMin.y + (1.0f - (ndcA.y * 0.5f + 0.5f)) * (imageMax.y - imageMin.y)
                );
                outB = ImVec2(
                    imageMin.x + (ndcB.x * 0.5f + 0.5f) * (imageMax.x - imageMin.x),
                    imageMin.y + (1.0f - (ndcB.y * 0.5f + 0.5f)) * (imageMax.y - imageMin.y)
                );
                return true;
            };
            glm::vec2 camXZ(camera.position.x, camera.position.z);
            float camDist = glm::length(camXZ);
            float extent = 60.0f + camDist * 0.5f + std::abs(camera.position.y) * 4.0f;
            extent = std::clamp(extent, 60.0f, 1200.0f);
            float step = 1.0f;
            if (extent > 400.0f) {
                step = 20.0f;
            } else if (extent > 200.0f) {
                step = 10.0f;
            } else if (extent > 120.0f) {
                step = 5.0f;
            } else if (extent > 70.0f) {
                step = 2.0f;
            }
            float gridStrength = std::clamp(camDist / 120.0f, 0.15f, 1.0f);
            ImVec4 baseCol(0.35f, 0.43f, 0.55f, 0.55f * gridStrength);
            ImVec4 axisXCol(0.94f, 0.45f, 0.45f, 0.9f);
            ImVec4 axisZCol(0.5f, 0.7f, 0.95f, 0.9f);

            float startX = std::floor((camera.position.x - extent) / step) * step;
            float endX = std::floor((camera.position.x + extent) / step) * step;
            for (float x = startX; x <= endX; x += step) {
                float t = 1.0f - std::min(1.0f, std::abs(x - camera.position.x) / extent);
                ImVec4 col = baseCol;
                col.w *= t;
                if (col.w < 0.02f) continue;
                ImVec2 s0, s1;
                if (clipLineToScreen(glm::vec3(x, 0.0f, camera.position.z - extent),
                                     glm::vec3(x, 0.0f, camera.position.z + extent), s0, s1)) {
                    viewportDrawList->AddLine(s0, s1, ImGui::GetColorU32(col), 1.0f);
                }
            }
            float startZ = std::floor((camera.position.z - extent) / step) * step;
            float endZ = std::floor((camera.position.z + extent) / step) * step;
            for (float z = startZ; z <= endZ; z += step) {
                float t = 1.0f - std::min(1.0f, std::abs(z - camera.position.z) / extent);
                ImVec4 col = baseCol;
                col.w *= t;
                if (col.w < 0.02f) continue;
                ImVec2 s0, s1;
                if (clipLineToScreen(glm::vec3(camera.position.x - extent, 0.0f, z),
                                     glm::vec3(camera.position.x + extent, 0.0f, z), s0, s1)) {
                    viewportDrawList->AddLine(s0, s1, ImGui::GetColorU32(col), 1.0f);
                }
            }
            ImVec2 ax0, ax1;
            if (clipLineToScreen(glm::vec3(-extent, 0.0f, 0.0f), glm::vec3(extent, 0.0f, 0.0f), ax0, ax1)) {
                viewportDrawList->AddLine(ax0, ax1, ImGui::GetColorU32(axisXCol), 2.0f);
            }
            ImVec2 az0, az1;
            if (clipLineToScreen(glm::vec3(0.0f, 0.0f, -extent), glm::vec3(0.0f, 0.0f, extent), az0, az1)) {
                viewportDrawList->AddLine(az0, az1, ImGui::GetColorU32(axisZCol), 2.0f);
            }
        }

        auto importDroppedModel = [&](const fs::path& path) {
            std::error_code ec;
            fs::directory_entry entry(path, ec);
            if (ec || !fileBrowser.isModelFile(entry)) {
                return;
            }
            if (fileBrowser.isOBJFile(entry)) {
                importOBJToScene(path.string(), "");
            } else {
                importModelToScene(path.string(), "");
            }
        };

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
                const char* path = static_cast<const char*>(payload->Data);
                importDroppedModel(fs::path(path));
            }
            ImGui::EndDragDropTarget();
        }

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
        if (!worldUiEditing) {
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

            // Prevent viewport picking when interacting with the axis widget.
            if (widgetHover) {
                blockSelection = true;
            }
        }

    bool windowActive = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
                        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    const float toolbarWidthEstimate = 520.0f;
    const float toolbarHeightEstimate = 42.0f;
    static ImVec2 toolbarSizeCache(toolbarWidthEstimate, toolbarHeightEstimate);
    ImVec2 toolbarRectMin(imageMin.x, imageMin.y);
    ImVec2 toolbarRectMax(imageMin.x, imageMin.y);
    auto computeToolbarRect = [&]() {
        ImVec2 desiredBottomLeft = ImVec2(imageMin.x + 12.0f, imageMax.y - 12.0f);
        float minX = imageMin.x + 12.0f;
        float maxX = imageMax.x - 12.0f;
        float toolbarLeft = desiredBottomLeft.x;
        if (toolbarLeft + toolbarSizeCache.x > maxX) toolbarLeft = maxX - toolbarSizeCache.x;
        if (toolbarLeft < minX) toolbarLeft = minX;
        float minY = imageMin.y + 12.0f;
        float toolbarTop = desiredBottomLeft.y - toolbarSizeCache.y;
        if (toolbarTop < minY) toolbarTop = minY;
        toolbarRectMin = ImVec2(toolbarLeft, toolbarTop);
        toolbarRectMax = ImVec2(toolbarLeft + toolbarSizeCache.x, toolbarTop + toolbarSizeCache.y);
    };
    computeToolbarRect();
    static float toolbarHideAnim = 0.0f;
    float toolbarHideOffset = toolbarSizeCache.y + 14.0f;
    ImVec2 toolbarRectMinAnim = ImVec2(toolbarRectMin.x, toolbarRectMin.y + toolbarHideOffset * toolbarHideAnim);
    ImVec2 toolbarRectMaxAnim = ImVec2(toolbarRectMax.x, toolbarRectMax.y + toolbarHideOffset * toolbarHideAnim);
    bool mouseInViewportRect = ImGui::IsMouseHoveringRect(imageMin, imageMax, true);
    ImVec2 toolbarGuardMin(imageMin.x + 6.0f, imageMax.y - std::max(84.0f, toolbarSizeCache.y + 30.0f));
    ImVec2 toolbarGuardMax(imageMin.x + std::min((imageMax.x - imageMin.x) - 6.0f, std::max(760.0f, toolbarSizeCache.x + 40.0f)),
                           imageMax.y - 4.0f);
    bool mouseInToolbarGuard = ImGui::IsMouseHoveringRect(toolbarGuardMin, toolbarGuardMax, true);
    bool mouseInToolbar = ImGui::IsMouseHoveringRect(
        ImVec2(toolbarRectMinAnim.x - 4.0f, toolbarRectMinAnim.y - 4.0f),
        ImVec2(toolbarRectMaxAnim.x + 4.0f, toolbarRectMaxAnim.y + 4.0f),
        true
    ) || mouseInToolbarGuard;
    bool toolbarAllowed = !gameViewportFocused && !(isPlaying && showGameViewport);
    bool showViewportToolbar = toolbarAllowed &&
                               (windowActive || mouseInViewportRect || mouseInToolbar || toolbarHideAnim < 0.999f);
    bool toolbarHover = toolbarAllowed && (mouseInViewportRect || mouseInToolbar);
    float toolbarAnimSpeed = 10.0f;
    float toolbarTarget = toolbarHover ? 0.0f : 1.0f;
    if (worldUiEditing && toolbarAllowed && mouseInViewportRect) {
        toolbarTarget = 0.0f;
    }
    float toolbarAnimStep = 1.0f - std::exp(-toolbarAnimSpeed * ImGui::GetIO().DeltaTime);
    toolbarHideAnim += (toolbarTarget - toolbarHideAnim) * toolbarAnimStep;
    toolbarRectMin.y += toolbarHideOffset * toolbarHideAnim;
    toolbarRectMax.y += toolbarHideOffset * toolbarHideAnim;
    mouseInToolbar = ImGui::IsMouseHoveringRect(
        ImVec2(toolbarRectMin.x - 4.0f, toolbarRectMin.y - 4.0f),
        ImVec2(toolbarRectMax.x + 4.0f, toolbarRectMax.y + 4.0f),
        true
    ) || mouseInToolbarGuard;
    if (showViewportToolbar && mouseInToolbar) {
        blockSelection = true;
    }

    SpriteTextureResolver spriteTextureResolver(rendererInitialized ? &renderer : nullptr);
    auto drawProjected25DSceneSprites = [&]() {
        auto brightenTint = [](const ImVec4& c, float k) {
            return ImVec4(std::clamp(c.x * k, 0.0f, 1.0f),
                          std::clamp(c.y * k, 0.0f, 1.0f),
                          std::clamp(c.z * k, 0.0f, 1.0f),
                          c.w);
        };
        BatchedSpriteEmitter spriteBatch(viewportDrawList);
        spriteBatch.reserve(sceneObjects.size());
        for (auto& obj : sceneObjects) {
            if (!IsObjectEnabledInHierarchy(obj) || obj.type != ObjectType::Sprite25D || !obj.hasUI) {
                continue;
            }
            if (!(obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D)) {
                continue;
            }
            ImVec2 rectMin, rectMax;
            if (!ResolveProjectedSprite25DRect(obj, view, proj, imageMin, ImVec2(imageMax.x - imageMin.x, imageMax.y - imageMin.y), rectMin, rectMax)) {
                continue;
            }
            ImVec2 drawSize(rectMax.x - rectMin.x, rectMax.y - rectMin.y);
            if (drawSize.x <= 1.0f || drawSize.y <= 1.0f) continue;

            Texture* spriteTex = spriteTextureResolver.resolveTexture(obj);
            unsigned int texId = (spriteTex != nullptr) ? spriteTex->GetID() : 0;
            std::array<ImVec2, 4> uvQuad = buildSpriteSheetUvs(obj);
            const int frame = resolveSpriteSheetFrame(obj);
            const ImVec2 sourceFrameSizePx = ResolveUiSourceFrameSizePx(obj, frame, spriteTex);
            ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
            const ImU32 tintColor = ImGui::GetColorU32(tint);
            float angle = glm::radians(obj.ui.rotation);
            if (DrawNineSliceSprite(spriteBatch,
                                    (ImTextureID)(intptr_t)texId,
                                    obj,
                                    rectMin,
                                    rectMax,
                                    uvQuad,
                                    sourceFrameSizePx,
                                    angle,
                                    tintColor)) {
            } else if (std::abs(angle) > 1e-4f) {
                ImVec2 center((rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y) * 0.5f);
                ImVec2 half(drawSize.x * 0.5f, drawSize.y * 0.5f);
                float c = std::cos(angle);
                float s = std::sin(angle);
                auto rotPt = [&](float x, float y) {
                    return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
                };
                ImVec2 p0 = rotPt(-half.x, -half.y);
                ImVec2 p1 = rotPt( half.x, -half.y);
                ImVec2 p2 = rotPt( half.x,  half.y);
                ImVec2 p3 = rotPt(-half.x,  half.y);
                if (texId != 0) {
                    spriteBatch.push((ImTextureID)(intptr_t)texId,
                                     p0, p1, p2, p3,
                                     uvQuad[0], uvQuad[1], uvQuad[2], uvQuad[3],
                                     tintColor);
                } else {
                    spriteBatch.flush();
                    ImU32 fill = tintColor;
                    ImU32 border = ImGui::GetColorU32(brightenTint(tint, 0.85f));
                    viewportDrawList->AddQuadFilled(p0, p1, p2, p3, fill);
                    viewportDrawList->AddQuad(p0, p1, p2, p3, border, 1.5f);
                }
            } else if (texId != 0) {
                spriteBatch.push((ImTextureID)(intptr_t)texId,
                                 rectMin,
                                 ImVec2(rectMax.x, rectMin.y),
                                 rectMax,
                                 ImVec2(rectMin.x, rectMax.y),
                                 uvQuad[0],
                                 ImVec2(uvQuad[2].x, uvQuad[0].y),
                                 uvQuad[2],
                                 ImVec2(uvQuad[0].x, uvQuad[2].y),
                                 tintColor);
            } else {
                spriteBatch.flush();
                ImU32 fill = tintColor;
                ImU32 border = ImGui::GetColorU32(brightenTint(tint, 0.85f));
                viewportDrawList->AddRectFilled(rectMin, rectMax, fill, 4.0f);
                viewportDrawList->AddRect(rectMin, rectMax, border, 4.0f, 0, 1.5f);
            }
        }
        spriteBatch.flush();
    };
    drawProjected25DSceneSprites();

    bool uiWorldCameraActive = false;
    if (worldUiEditing) {
        UiSceneLookupCache uiSceneLookup(sceneObjects);
        int editCanvas3DId = -1;
        if (SceneObject* selected = getSelectedObject()) {
            editCanvas3DId = uiSceneLookup.find3DCanvasId(*selected);
        }
        auto isUIType = [&](const SceneObject& target) {
            if (target.type == ObjectType::Sprite25D) return true;
            if (!worldUiEditing) return false;
            if (!target.hasUI || target.ui.type == UIElementType::None) return false;
            int canvasId = uiSceneLookup.find3DCanvasId(target);
            return (canvasId < 0) || (canvasId == editCanvas3DId);
        };
        float overlayHeight = imageMax.y - imageMin.y;
        if (showViewportToolbar) {
            overlayHeight = std::min(overlayHeight, std::max(1.0f, (toolbarRectMin.y - imageMin.y) - 2.0f));
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetCursorScreenPos(imageMin);
        ImGui::BeginChild("SceneUIWorldOverlay",
                          ImVec2(imageMax.x - imageMin.x, overlayHeight),
                          false,
                          ImGuiWindowFlags_NoTitleBar |
                          ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoBackground);

        ImVec2 overlayPos = ImGui::GetWindowPos();
        ImVec2 overlaySize = ImGui::GetWindowSize();
        uiWorldCamera.viewportSize = glm::vec2(overlaySize.x, overlaySize.y);
        bool mouseInToolbar = ImGui::IsMouseHoveringRect(
            ImVec2(toolbarRectMin.x - 4.0f, toolbarRectMin.y - 4.0f),
            ImVec2(toolbarRectMax.x + 4.0f, toolbarRectMax.y + 4.0f),
            true
        ) || mouseInToolbarGuard;
        bool uiWorldHover = (mouseOverViewportImage || ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) && !mouseInToolbar;
        auto worldToScreen = [&](const glm::vec2& world) {
            glm::vec2 local = uiWorldCamera.WorldToScreen(world);
            return ImVec2(overlayPos.x + local.x, overlayPos.y + local.y);
        };
        auto screenToWorld = [&](const ImVec2& screen) {
            glm::vec2 local(screen.x - overlayPos.x, screen.y - overlayPos.y);
            return uiWorldCamera.ScreenToWorld(local);
        };
        auto parallaxOffset = [&](const SceneObject& obj) {
            if (!obj.hasParallaxLayer2D || !obj.parallaxLayer2D.enabled) return glm::vec2(0.0f);
            float factor = std::clamp(obj.parallaxLayer2D.factor, 0.0f, 1.0f);
            return uiWorldCamera.position * (1.0f - factor);
        };
        auto resolveUIRectWorld = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax) {
            if (obj.type == ObjectType::Sprite25D) {
                return ResolveProjectedSprite25DRect(obj, view, proj, overlayPos, overlaySize, outMin, outMax);
            }
            glm::vec2 parentOffset = uiSceneLookup.getWorldParentOffset(obj);
            glm::vec2 worldPos = parentOffset + glm::vec2(obj.ui.position.x, obj.ui.position.y) + parallaxOffset(obj);
            glm::vec2 sizeWorld = getSpriteDisplaySize(obj);
            ImVec2 pivotOffset = ImVec2(sizeWorld.x * 0.5f, sizeWorld.y * 0.5f);
            switch (obj.ui.anchor) {
                case UIAnchor::TopLeft: pivotOffset = ImVec2(0.0f, 0.0f); break;
                case UIAnchor::TopRight: pivotOffset = ImVec2(sizeWorld.x, 0.0f); break;
                case UIAnchor::BottomLeft: pivotOffset = ImVec2(0.0f, sizeWorld.y); break;
                case UIAnchor::BottomRight: pivotOffset = ImVec2(sizeWorld.x, sizeWorld.y); break;
                default: break;
            }
            glm::vec2 worldMin = worldPos - glm::vec2(pivotOffset.x, pivotOffset.y);
            glm::vec2 worldMax = worldMin + sizeWorld;
            ImVec2 s0 = worldToScreen(worldMin);
            ImVec2 s1 = worldToScreen(worldMax);
            outMin = ImVec2(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
            outMax = ImVec2(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
            return true;
        };
        auto rectOutsideOverlay = [&](const ImVec2& min, const ImVec2& max) {
            return (max.x < overlayPos.x || min.x > overlayPos.x + overlaySize.x ||
                    max.y < overlayPos.y || min.y > overlayPos.y + overlaySize.y);
        };

        if (uiWorldHover) {
            ImGuiIO& io = ImGui::GetIO();
            bool panHeld = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                (ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDown(ImGuiMouseButton_Left));
            if (panHeld) {
                uiWorldPanning = true;
            } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
                       !(ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
                uiWorldPanning = false;
            }
            if (uiWorldPanning) {
                ImVec2 delta = io.MouseDelta;
                if (delta.x != 0.0f || delta.y != 0.0f) {
                    uiWorldCamera.position.x -= delta.x / uiWorldCamera.zoom;
                    uiWorldCamera.position.y += delta.y / uiWorldCamera.zoom;
                }
                uiWorldCameraActive = true;
            }
            if (io.MouseWheel != 0.0f) {
                glm::vec2 mouseLocal(io.MousePos.x - overlayPos.x, io.MousePos.y - overlayPos.y);
                glm::vec2 worldBefore = uiWorldCamera.ScreenToWorld(mouseLocal);
                float zoomFactor = 1.0f + io.MouseWheel * 0.1f;
                float newZoom = std::clamp(uiWorldCamera.zoom * zoomFactor, 5.0f, 2000.0f);
                if (newZoom != uiWorldCamera.zoom) {
                    uiWorldCamera.zoom = newZoom;
                    glm::vec2 worldAfter = uiWorldCamera.ScreenToWorld(mouseLocal);
                    uiWorldCamera.position += (worldBefore - worldAfter);
                    uiWorldCameraActive = true;
                }
            }
            glm::vec2 panDir(0.0f);
            if (ImGui::IsKeyDown(ImGuiKey_A)) panDir.x -= 1.0f;
            if (ImGui::IsKeyDown(ImGuiKey_D)) panDir.x += 1.0f;
            if (ImGui::IsKeyDown(ImGuiKey_W)) panDir.y += 1.0f;
            if (ImGui::IsKeyDown(ImGuiKey_S)) panDir.y -= 1.0f;
            if (panDir.x != 0.0f || panDir.y != 0.0f) {
                float panSpeed = 6.0f;
                if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) {
                    panSpeed *= 2.5f;
                }
                uiWorldCamera.position += panDir * (panSpeed * deltaTime);
                uiWorldCameraActive = true;
            }
        }

        auto brighten = [](const ImVec4& c, float k) {
            return ImVec4(std::clamp(c.x * k, 0.0f, 1.0f),
                          std::clamp(c.y * k, 0.0f, 1.0f),
                          std::clamp(c.z * k, 0.0f, 1.0f),
                          c.w);
        };

        if (showUIWorldGrid) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 overlayMax(overlayPos.x + overlaySize.x, overlayPos.y + overlaySize.y);
            if (showViewportToolbar && toolbarRectMin.y > overlayPos.y) {
                overlayMax.y = std::min(overlayMax.y, toolbarRectMin.y - 2.0f);
            }
            dl->PushClipRect(overlayPos, overlayMax, true);
            float step = 1.0f;
            float minPx = 30.0f;
            float maxPx = 140.0f;
            while (step * uiWorldCamera.zoom < minPx) step *= 2.0f;
            while (step * uiWorldCamera.zoom > maxPx) step *= 0.5f;

            glm::vec2 worldMin = uiWorldCamera.ScreenToWorld(glm::vec2(0.0f, overlaySize.y));
            glm::vec2 worldMax = uiWorldCamera.ScreenToWorld(glm::vec2(overlaySize.x, 0.0f));
            float startX = std::floor(worldMin.x / step) * step;
            float endX = std::ceil(worldMax.x / step) * step;
            float startY = std::floor(worldMin.y / step) * step;
            float endY = std::ceil(worldMax.y / step) * step;
            ImU32 gridColor = IM_COL32(90, 110, 140, 50);
            ImU32 axisColorX = IM_COL32(240, 120, 120, 170);
            ImU32 axisColorY = IM_COL32(120, 240, 150, 170);

            for (float x = startX; x <= endX; x += step) {
                ImVec2 p0 = worldToScreen(glm::vec2(x, worldMin.y));
                ImVec2 p1 = worldToScreen(glm::vec2(x, worldMax.y));
                dl->AddLine(p0, p1, gridColor, 1.0f);
            }
            for (float y = startY; y <= endY; y += step) {
                ImVec2 p0 = worldToScreen(glm::vec2(worldMin.x, y));
                ImVec2 p1 = worldToScreen(glm::vec2(worldMax.x, y));
                dl->AddLine(p0, p1, gridColor, 1.0f);
            }

            ImVec2 axisX0 = worldToScreen(glm::vec2(worldMin.x, 0.0f));
            ImVec2 axisX1 = worldToScreen(glm::vec2(worldMax.x, 0.0f));
            ImVec2 axisY0 = worldToScreen(glm::vec2(0.0f, worldMin.y));
            ImVec2 axisY1 = worldToScreen(glm::vec2(0.0f, worldMax.y));
            dl->AddLine(axisX0, axisX1, axisColorX, 2.0f);
            dl->AddLine(axisY0, axisY1, axisColorY, 2.0f);
            dl->PopClipRect();
        }

        if (showCanvasOverlay && worldUiReferenceResolutionWidth > 0 && worldUiReferenceResolutionHeight > 0) {
            ImDrawList* dl = ImGui::GetForegroundDrawList(ImGui::GetWindowViewport());
            ImVec2 overlayMax(overlayPos.x + overlaySize.x, overlayPos.y + overlaySize.y);
            if (showViewportToolbar && toolbarRectMin.y > overlayPos.y) {
                overlayMax.y = std::min(overlayMax.y, toolbarRectMin.y - 2.0f);
            }
            dl->PushClipRect(overlayPos, overlayMax, true);

            const float fitScale = std::max(0.01f, std::min(
                overlaySize.x / static_cast<float>(worldUiReferenceResolutionWidth),
                overlaySize.y / static_cast<float>(worldUiReferenceResolutionHeight)));
            const ImVec2 frameSize(
                static_cast<float>(worldUiReferenceResolutionWidth) * fitScale,
                static_cast<float>(worldUiReferenceResolutionHeight) * fitScale);
            const ImVec2 frameMin(
                overlayPos.x + (overlaySize.x - frameSize.x) * 0.5f,
                overlayPos.y + (overlaySize.y - frameSize.y) * 0.5f);
            const ImVec2 frameMax(frameMin.x + frameSize.x, frameMin.y + frameSize.y);
            dl->AddRect(frameMin, frameMax, IM_COL32(84, 176, 255, 220), 4.0f, 0, 2.0f);

            char label[80];
            std::snprintf(label, sizeof(label), "Resolution %dx%d", worldUiReferenceResolutionWidth, worldUiReferenceResolutionHeight);
            ImVec2 labelSize = ImGui::CalcTextSize(label);
            ImVec2 labelPad(6.0f, 3.0f);
            ImVec2 labelMin(frameMin.x + 8.0f, frameMin.y + 8.0f);
            ImVec2 labelMax(labelMin.x + labelSize.x + labelPad.x * 2.0f,
                            labelMin.y + labelSize.y + labelPad.y * 2.0f);
            dl->AddRectFilled(labelMin, labelMax, IM_COL32(12, 22, 34, 190), 4.0f);
            dl->AddRect(labelMin, labelMax, IM_COL32(84, 176, 255, 180), 4.0f, 0, 1.0f);
            dl->AddText(ImVec2(labelMin.x + labelPad.x, labelMin.y + labelPad.y),
                        IM_COL32(214, 240, 255, 235), label);
            dl->PopClipRect();
        }

        if (showSceneGizmos && gizmoShowCameraOverlays) {
            ImDrawList* dl = ImGui::GetForegroundDrawList(ImGui::GetWindowViewport());
            ImVec2 overlayMax(overlayPos.x + overlaySize.x, overlayPos.y + overlaySize.y);
            if (showViewportToolbar && toolbarRectMin.y > overlayPos.y) {
                overlayMax.y = std::min(overlayMax.y, toolbarRectMin.y - 2.0f);
            }
            dl->PushClipRect(overlayPos, overlayMax, true);

            for (const auto& camObj : sceneObjects) {
                if (!camObj.hasCamera || !IsObjectEnabledInHierarchy(camObj)) continue;
                const bool cameraIs2D = project2DPipeline || camObj.camera.use2D;
                if (!cameraIs2D) continue;

                const float alpha = camObj.enabled ? 1.0f : 0.35f;
                const float pixelsPerUnit = std::max(1.0f, camObj.camera.pixelsPerUnit);
                const int cameraResolutionWidth = activeGameResolutionWidth;
                const int cameraResolutionHeight = activeGameResolutionHeight;
                const float halfWidth = static_cast<float>(cameraResolutionWidth) / (2.0f * pixelsPerUnit);
                const float halfHeight = static_cast<float>(cameraResolutionHeight) / (2.0f * pixelsPerUnit);

                glm::quat q = glm::quat(glm::radians(camObj.rotation));
                glm::mat3 rot = glm::mat3_cast(q);
                glm::vec2 right(rot[0].x, rot[0].y);
                glm::vec2 up(rot[1].x, rot[1].y);
                if (glm::length(right) < 1e-4f) right = glm::vec2(1.0f, 0.0f);
                if (glm::length(up) < 1e-4f) up = glm::vec2(0.0f, 1.0f);
                right = glm::normalize(right);
                up = glm::normalize(up);

                glm::vec2 center(camObj.position.x, camObj.position.y);
                std::array<glm::vec2, 4> corners = {
                    center - right * halfWidth + up * halfHeight,
                    center + right * halfWidth + up * halfHeight,
                    center + right * halfWidth - up * halfHeight,
                    center - right * halfWidth - up * halfHeight
                };
                std::array<ImVec2, 4> screenCorners = {
                    worldToScreen(corners[0]),
                    worldToScreen(corners[1]),
                    worldToScreen(corners[2]),
                    worldToScreen(corners[3])
                };

                ImU32 boundsCol = ImGui::GetColorU32(ImVec4(0.28f, 0.88f, 1.0f, 0.92f * alpha));
                ImU32 diagCol = ImGui::GetColorU32(ImVec4(0.28f, 0.88f, 1.0f, 0.45f * alpha));
                const float edgeThickness = std::max(1.4f, 2.0f * std::clamp(sceneGizmoOverlayScale, 0.4f, 3.0f));
                for (int i = 0; i < 4; ++i) {
                    dl->AddLine(screenCorners[i], screenCorners[(i + 1) % 4], boundsCol, edgeThickness);
                }
                dl->AddLine(screenCorners[0], screenCorners[2], diagCol, 1.2f);
                dl->AddLine(screenCorners[1], screenCorners[3], diagCol, 1.2f);

                ImVec2 camCenter = worldToScreen(center);
                ImVec2 camForward = worldToScreen(center + up * std::max(0.1f, halfHeight * 0.45f));
                dl->AddLine(camCenter, camForward,
                            ImGui::GetColorU32(ImVec4(0.94f, 0.98f, 1.0f, 0.88f * alpha)),
                            edgeThickness);

                if (gizmoShowCameraFrustumLabels) {
                    char label[96];
                    std::snprintf(label, sizeof(label), "2D %dx%d | %.2fx%.2f",
                                  cameraResolutionWidth,
                                  cameraResolutionHeight,
                                  halfWidth * 2.0f,
                                  halfHeight * 2.0f);
                    ImVec2 textSize = ImGui::CalcTextSize(label);
                    ImVec2 labelPos(screenCorners[1].x + 6.0f, screenCorners[1].y - textSize.y - 4.0f);
                    ImVec2 pad(4.0f, 2.0f);
                    ImVec2 bgMin(labelPos.x, labelPos.y);
                    ImVec2 bgMax(labelPos.x + textSize.x + pad.x * 2.0f, labelPos.y + textSize.y + pad.y * 2.0f);
                    dl->AddRectFilled(bgMin, bgMax, IM_COL32(16, 24, 34, static_cast<int>(195.0f * alpha)), 4.0f);
                    dl->AddRect(bgMin, bgMax, IM_COL32(120, 200, 240, static_cast<int>(180.0f * alpha)), 4.0f, 0, 1.0f);
                    dl->AddText(ImVec2(bgMin.x + pad.x, bgMin.y + pad.y),
                                IM_COL32(214, 242, 255, static_cast<int>(235.0f * alpha)),
                                label);
                }
            }

            dl->PopClipRect();
        }

        float animSpeed = 0.0f;
        if (uiAnimationMode == UIAnimationMode::Fluid) {
            animSpeed = 8.0f;
        } else if (uiAnimationMode == UIAnimationMode::Snappy) {
            animSpeed = 18.0f;
        }
        float animStep = (uiAnimationMode == UIAnimationMode::Off) ? 1.0f
            : (1.0f - std::exp(-animSpeed * ImGui::GetIO().DeltaTime));
        auto animateValue = [&](float& current, float target, bool immediate) {
            if (uiAnimationMode == UIAnimationMode::Off || immediate) {
                current = target;
            } else {
                current += (target - current) * animStep;
            }
            return current;
        };

        std::vector<SceneObject*> uiDrawList;
        uiDrawList.reserve(sceneObjects.size());
        bool needsParallaxSort = false;
        for (auto& obj : sceneObjects) {
            if (!IsObjectEnabledInHierarchy(obj) || !isUIType(obj)) continue;
            uiDrawList.push_back(&obj);
            needsParallaxSort = needsParallaxSort ||
                (obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.order != 0);
        }
        if (worldUiEditing && needsParallaxSort && uiDrawList.size() > 1) {
            StableSortRuntimeUiDrawList(uiDrawList);
        }

        glm::vec2 worldViewMin = uiWorldCamera.ScreenToWorld(glm::vec2(0.0f, overlaySize.y));
        glm::vec2 worldViewMax = uiWorldCamera.ScreenToWorld(glm::vec2(overlaySize.x, 0.0f));
        BatchedSpriteEmitter spriteBatch(ImGui::GetWindowDrawList());
        spriteBatch.reserve(uiDrawList.size());
        struct ResolvedUiRect {
            ImVec2 min;
            ImVec2 max;
        };
        std::unordered_map<int, ResolvedUiRect> resolvedUiRects;
        resolvedUiRects.reserve(uiDrawList.size());
        std::vector<SceneObject*> drawnUiObjects;
        drawnUiObjects.reserve(uiDrawList.size());
        auto resolveCanvasMaskRectForObject = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax) -> bool {
            bool hasMask = false;
            ImVec2 maskMin(0.0f, 0.0f);
            ImVec2 maskMax(0.0f, 0.0f);
            const SceneObject* current = &obj;
            while (current && current->parentId >= 0) {
                current = uiSceneLookup.find(current->parentId);
                if (!current) break;
                if (!(current->hasUI && current->ui.type == UIElementType::Canvas && current->ui.maskChildren)) {
                    continue;
                }

                ImVec2 canvasMin, canvasMax;
                if (!resolveUIRectWorld(*current, canvasMin, canvasMax)) {
                    continue;
                }

                if (!hasMask) {
                    maskMin = canvasMin;
                    maskMax = canvasMax;
                    hasMask = true;
                } else {
                    maskMin.x = std::max(maskMin.x, canvasMin.x);
                    maskMin.y = std::max(maskMin.y, canvasMin.y);
                    maskMax.x = std::min(maskMax.x, canvasMax.x);
                    maskMax.y = std::min(maskMax.y, canvasMax.y);
                }
            }

            if (!hasMask) return false;
            outMin = maskMin;
            outMax = maskMax;
            return (outMax.x > outMin.x) && (outMax.y > outMin.y);
        };

        std::unordered_set<int> light2DRenderedObjectIds;
        bool renderedLight2DComposite = false;
        Light2DDebugStats light2DStats;
        int activeLight2DCount = 0;
        int litSprite2DCount = 0;
        int litWorldImageCount = 0;
        bool lightBufferHadContent = false;
        std::unordered_map<int, std::string> light2DRoutingReasons;
        SceneObject* selectedForRoutingReasons = showInspector ? getSelectedObject() : nullptr;
        const bool captureLight2DRoutingReasons = selectedForRoutingReasons && selectedForRoutingReasons->hasUI;
        if (captureLight2DRoutingReasons) {
            light2DRoutingReasons.reserve(uiDrawList.size());
        }
        auto setLight2DRoutingReason = [&](int objectId, const char* reason) {
            if (captureLight2DRoutingReasons) {
                light2DRoutingReasons[objectId] = reason;
            }
        };
        if (rendererInitialized) {
            Light2DRenderRequest lightRequest;
            lightRequest.width = std::max(1, static_cast<int>(std::round(overlaySize.x)));
            lightRequest.height = std::max(1, static_cast<int>(std::round(overlaySize.y)));
            lightRequest.clearColor = glm::vec4(0.0f);
            lightRequest.baseAmbient = glm::vec3(0.0f);
            lightRequest.lightingBufferScale = light2DLightingBufferScale;
            lightRequest.blendStyles = light2DBlendStyles;
            auto computeFlickerMultiplier = [](const Light2DFlickerSettings& flicker) {
                if (!flicker.enabled || flicker.amount <= 0.0001f) {
                    return 1.0f;
                }
                const float time = static_cast<float>(glfwGetTime());
                const float base = std::sin(time * std::max(0.01f, flicker.speed) + flicker.seed);
                const float jitter = std::sin(time * std::max(0.01f, flicker.speed * 2.173f) + flicker.seed * 1.913f);
                const float noise = 0.5f + 0.35f * base + 0.15f * jitter;
                return glm::mix(1.0f, std::max(0.0f, noise), std::clamp(flicker.amount, 0.0f, 1.0f));
            };

            int spriteDrawOrder = 0;
            for (SceneObject* objPtr : uiDrawList) {
                SceneObject& obj = *objPtr;
                if (!(obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D)) {
                    continue;
                }
                if (obj.ui.nineSliceEnabled) {
                    setLight2DRoutingReason(obj.id, "Legacy path: nine-slice sprites are not routed through Light2D yet.");
                    continue;
                }
                if (obj.ui.unlitLighting2D) {
                    setLight2DRoutingReason(obj.id, "Legacy path: Force Unlit keeps this sprite on the legacy 2D renderer.");
                    continue;
                }

                const bool repeatX = obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.repeatX;
                const bool repeatY = obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.repeatY;
                const bool disableCulling = obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.disableCulling;

                ImVec2 rectMin, rectMax;
                if (!resolveUIRectWorld(obj, rectMin, rectMax)) {
                    setLight2DRoutingReason(obj.id, "Legacy path: failed to resolve a world-space sprite rect for the active viewport.");
                    continue;
                }
                if (!disableCulling && !repeatX && !repeatY && rectOutsideOverlay(rectMin, rectMax)) {
                    setLight2DRoutingReason(obj.id, "Skipped Light2D: object is outside the visible 2D world overlay.");
                    continue;
                }

                Texture* spriteTex = spriteTextureResolver.resolveTexture(obj);
                if (!spriteTex || spriteTex->GetID() == 0) {
                    setLight2DRoutingReason(obj.id, "Legacy path: no sprite texture is bound for this object.");
                    continue;
                }

                std::array<ImVec2, 4> uvQuad = buildSpriteSheetUvs(obj);
                const float angle = glm::radians(obj.ui.rotation);
                const float c = std::cos(angle);
                const float s = std::sin(angle);
                ImVec2 maskMin, maskMax;
                const bool hasMaskRect = resolveCanvasMaskRectForObject(obj, maskMin, maskMax);
                auto appendSpriteQuad = [&](const ImVec2& quadMin, const ImVec2& quadMax) {
                    if (!disableCulling && rectOutsideOverlay(quadMin, quadMax)) {
                        return false;
                    }
                    if (hasMaskRect) {
                        const bool maskClipsSprite =
                            quadMin.x < maskMin.x || quadMax.x > maskMax.x ||
                            quadMin.y < maskMin.y || quadMax.y > maskMax.y;
                        if (maskClipsSprite) {
                            return false;
                        }
                    }

                    Light2DScreenSprite sprite;
                    sprite.objectId = obj.id;
                    sprite.layer = obj.layer;
                    sprite.drawOrder = spriteDrawOrder++;
                    sprite.textureId = spriteTex->GetID();
                    sprite.tint = obj.ui.color;
                    sprite.receiveLighting = obj.ui.receiveLighting2D;
                    sprite.unlit = obj.ui.unlitLighting2D;
                    sprite.emissiveIntensity = obj.ui.emissiveLighting2D;

                    const glm::vec2 center(
                        ((quadMin.x + quadMax.x) * 0.5f) - overlayPos.x,
                        ((quadMin.y + quadMax.y) * 0.5f) - overlayPos.y);
                    const glm::vec2 half(
                        std::max(0.5f, (quadMax.x - quadMin.x) * 0.5f),
                        std::max(0.5f, (quadMax.y - quadMin.y) * 0.5f));
                    auto rotatePoint = [&](float x, float y) {
                        return glm::vec2(center.x + x * c - y * s, center.y + x * s + y * c);
                    };
                    sprite.positions[0] = rotatePoint(-half.x, -half.y);
                    sprite.positions[1] = rotatePoint(half.x, -half.y);
                    sprite.positions[2] = rotatePoint(half.x, half.y);
                    sprite.positions[3] = rotatePoint(-half.x, half.y);
                    sprite.uvs[0] = glm::vec2(uvQuad[0].x, uvQuad[0].y);
                    sprite.uvs[1] = glm::vec2(uvQuad[1].x, uvQuad[1].y);
                    sprite.uvs[2] = glm::vec2(uvQuad[2].x, uvQuad[2].y);
                    sprite.uvs[3] = glm::vec2(uvQuad[3].x, uvQuad[3].y);
                    lightRequest.sprites.push_back(sprite);
                    return true;
                };

                bool addedAnySprite = false;
                if (repeatX || repeatY) {
                    glm::vec2 spriteSizeWorld = getSpriteDisplaySize(obj);
                    glm::vec2 spacing = obj.hasParallaxLayer2D ? obj.parallaxLayer2D.repeatSpacing : glm::vec2(0.0f);
                    float stepX = spriteSizeWorld.x + spacing.x;
                    float stepY = spriteSizeWorld.y + spacing.y;
                    ImVec2 pivotOffset(spriteSizeWorld.x * 0.5f, spriteSizeWorld.y * 0.5f);
                    switch (obj.ui.anchor) {
                        case UIAnchor::TopLeft: pivotOffset = ImVec2(0.0f, 0.0f); break;
                        case UIAnchor::TopRight: pivotOffset = ImVec2(spriteSizeWorld.x, 0.0f); break;
                        case UIAnchor::BottomLeft: pivotOffset = ImVec2(0.0f, spriteSizeWorld.y); break;
                        case UIAnchor::BottomRight: pivotOffset = ImVec2(spriteSizeWorld.x, spriteSizeWorld.y); break;
                        default: break;
                    }
                    glm::vec2 parentOffset = uiSceneLookup.getWorldParentOffset(obj);
                    glm::vec2 worldPos = parentOffset + glm::vec2(obj.ui.position.x, obj.ui.position.y) + parallaxOffset(obj);
                    glm::vec2 baseWorldMin = worldPos - glm::vec2(pivotOffset.x, pivotOffset.y);
                    int startX = repeatX ? static_cast<int>(std::floor((worldViewMin.x - baseWorldMin.x) / stepX)) - 1 : 0;
                    int endX = repeatX ? static_cast<int>(std::ceil((worldViewMax.x - baseWorldMin.x) / stepX)) + 1 : 0;
                    int startY = repeatY ? static_cast<int>(std::floor((worldViewMin.y - baseWorldMin.y) / stepY)) - 1 : 0;
                    int endY = repeatY ? static_cast<int>(std::ceil((worldViewMax.y - baseWorldMin.y) / stepY)) + 1 : 0;
                    for (int ix = startX; ix <= endX; ++ix) {
                        for (int iy = startY; iy <= endY; ++iy) {
                            float dx = repeatX ? static_cast<float>(ix) * stepX : 0.0f;
                            float dy = repeatY ? static_cast<float>(iy) * stepY : 0.0f;
                            glm::vec2 tileMin = baseWorldMin + glm::vec2(dx, dy);
                            ImVec2 s0 = worldToScreen(tileMin);
                            ImVec2 s1 = worldToScreen(tileMin + glm::vec2(spriteSizeWorld.x, spriteSizeWorld.y));
                            ImVec2 tileRectMin(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
                            ImVec2 tileRectMax(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
                            addedAnySprite = appendSpriteQuad(tileRectMin, tileRectMax) || addedAnySprite;
                        }
                    }
                } else {
                    addedAnySprite = appendSpriteQuad(rectMin, rectMax);
                }

                if (!addedAnySprite) {
                    setLight2DRoutingReason(obj.id, hasMaskRect
                        ? "Legacy path: repeating or masked tiles still use legacy rendering when the canvas clip cuts the visible tile."
                        : "Skipped Light2D: object has no visible tiles inside the current 2D world overlay.");
                    continue;
                }

                light2DRenderedObjectIds.insert(obj.id);
                if (obj.ui.type == UIElementType::Sprite2D) {
                    if (obj.ui.receiveLighting2D && !obj.ui.unlitLighting2D) {
                        ++litSprite2DCount;
                    }
                } else if (obj.ui.receiveLighting2D && !obj.ui.unlitLighting2D) {
                    ++litWorldImageCount;
                }
                if (obj.ui.receiveLighting2D && !obj.ui.unlitLighting2D) {
                    setLight2DRoutingReason(obj.id, repeatX || repeatY
                        ? "Lit path: repeating parallax tiles are routed through the Light2D compositor."
                        : "Lit path: routed through the Light2D compositor.");
                } else if (obj.ui.unlitLighting2D) {
                    setLight2DRoutingReason(obj.id, "Lit compositor path: object is routed, but Force Unlit is enabled.");
                } else {
                    setLight2DRoutingReason(obj.id, "Lit compositor path: object is routed, but Receive Lighting is disabled.");
                }
            }

            for (const SceneObject& obj : sceneObjects) {
                if (!IsObjectEnabledInHierarchy(obj) || !obj.hasLight2D || !obj.light2D.enabled) {
                    continue;
                }
                ++activeLight2DCount;

                if (obj.light2D.type == Light2DType::Global) {
                    lightRequest.baseAmbient += glm::vec3(obj.light2D.color) * obj.light2D.intensity;
                    continue;
                }

                Light2DScreenLight light;
                light.objectId = obj.id;
                light.enabled = obj.light2D.enabled;
                light.type = obj.light2D.type;
                light.blendStyle = obj.light2D.blendStyle;
                light.lightOrder = obj.light2D.lightOrder;
                light.overlapOperation = obj.light2D.overlapOperation;
                light.targetAllLayers = obj.light2D.targetAllLayers;
                light.targetLayerMask = obj.light2D.targetLayerMask;
                light.color = obj.light2D.color;
                light.intensity = obj.light2D.intensity * computeFlickerMultiplier(obj.light2D.flicker);
                light.radius = std::max(obj.light2D.radius, obj.light2D.outerRadius) * uiWorldCamera.zoom;
                light.innerRadius = obj.light2D.innerRadius * uiWorldCamera.zoom;
                light.outerRadius = std::max(obj.light2D.innerRadius, obj.light2D.outerRadius) * uiWorldCamera.zoom;
                light.falloffStrength = obj.light2D.falloffStrength;
                light.innerSpotAngle = obj.light2D.innerSpotAngle;
                light.outerSpotAngle = obj.light2D.outerSpotAngle;
                light.shadowStrength = obj.light2D.shadowStrength;
                light.volumetricEnabled = obj.light2D.volumetricEnabled;
                light.castsShadows = obj.light2D.castsShadows;
                light.rotationRad = glm::radians(obj.rotation.z);
                light.cookieScale = obj.light2D.cookieScale;
                light.cookieRotationRad = glm::radians(obj.light2D.cookieRotation);
                light.freeformFeatherPx = obj.light2D.freeformFeather * uiWorldCamera.zoom;
                light.freeformEdgeFalloff = obj.light2D.freeformEdgeFalloff;
                if (!obj.light2D.cookieTexturePath.empty()) {
                    if (Texture* cookieTexture = renderer.getTexture(obj.light2D.cookieTexturePath, MaterialProperties::TextureFilter::Bilinear)) {
                        light.cookieTextureId = cookieTexture->GetID();
                    }
                }

                ImVec2 lightPos = worldToScreen(glm::vec2(obj.position.x, obj.position.y));
                light.position = glm::vec2(lightPos.x - overlayPos.x, lightPos.y - overlayPos.y);

                if (obj.light2D.type == Light2DType::Freeform || obj.light2D.type == Light2DType::Sprite) {
                    light.polygon.reserve(obj.light2D.shapePoints.size());
                    for (const glm::vec2& point : obj.light2D.shapePoints) {
                        ImVec2 screenPoint = worldToScreen(glm::vec2(obj.position.x + point.x, obj.position.y + point.y));
                        light.polygon.emplace_back(screenPoint.x - overlayPos.x, screenPoint.y - overlayPos.y);
                    }
                    if (!light.polygon.empty()) {
                        glm::vec2 boundsMin(FLT_MAX);
                        glm::vec2 boundsMax(-FLT_MAX);
                        for (const glm::vec2& point : light.polygon) {
                            boundsMin.x = std::min(boundsMin.x, point.x);
                            boundsMin.y = std::min(boundsMin.y, point.y);
                            boundsMax.x = std::max(boundsMax.x, point.x);
                            boundsMax.y = std::max(boundsMax.y, point.y);
                        }
                        light.boundsMin = boundsMin;
                        light.boundsMax = boundsMax;
                    }
                } else {
                    const float extent = std::max(light.radius, light.outerRadius);
                    light.boundsMin = light.position - glm::vec2(extent);
                    light.boundsMax = light.position + glm::vec2(extent);
                }

                lightRequest.lights.push_back(light);
            }

            for (const SceneObject& obj : sceneObjects) {
                if (!IsObjectEnabledInHierarchy(obj) || !obj.hasShadowCaster2D || !obj.shadowCaster2D.enabled) {
                    continue;
                }

                Light2DScreenShadowCaster caster;
                caster.objectId = obj.id;
                caster.enabled = obj.shadowCaster2D.enabled;
                caster.targetAllLayers = obj.shadowCaster2D.targetAllLayers;
                caster.targetLayerMask = obj.shadowCaster2D.targetLayerMask;
                caster.shadowStrength = obj.shadowCaster2D.shadowStrength;
                caster.polygon.reserve(obj.shadowCaster2D.points.size());
                for (const glm::vec2& point : obj.shadowCaster2D.points) {
                    ImVec2 screenPoint = worldToScreen(glm::vec2(obj.position.x + point.x, obj.position.y + point.y));
                    caster.polygon.emplace_back(screenPoint.x - overlayPos.x, screenPoint.y - overlayPos.y);
                }
                if (caster.polygon.size() >= 3) {
                    lightRequest.shadowCasters.push_back(std::move(caster));
                }
            }

            const bool hasAmbientOnly = glm::length(lightRequest.baseAmbient) > 0.0001f;
            lightBufferHadContent = hasAmbientOnly || !lightRequest.lights.empty();
            if (!lightRequest.sprites.empty() && (hasAmbientOnly || !lightRequest.lights.empty())) {
                unsigned int lightTexture = lighting2DRenderer.render(lightRequest, renderer);
                if (lightTexture != 0) {
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)(intptr_t)lightTexture,
                        overlayPos,
                        ImVec2(overlayPos.x + overlaySize.x, overlayPos.y + overlaySize.y),
                        ImVec2(0.0f, 1.0f),
                        ImVec2(1.0f, 0.0f));
                    renderedLight2DComposite = true;
                    light2DStats = lighting2DRenderer.getLastStats();
                } else {
                    for (int objectId : light2DRenderedObjectIds) {
                        setLight2DRoutingReason(objectId, "Legacy path: Light2D compositor did not produce a valid output texture this frame.");
                    }
                    light2DRenderedObjectIds.clear();
                }
            } else {
                for (int objectId : light2DRenderedObjectIds) {
                    setLight2DRoutingReason(objectId, "Legacy path: no active Light2D or Global Light2D affected this frame.");
                }
                light2DRenderedObjectIds.clear();
            }
        }

        for (SceneObject* objPtr : uiDrawList) {
            SceneObject& obj = *objPtr;
            ImVec2 rectMin, rectMax;
            if (!resolveUIRectWorld(obj, rectMin, rectMax)) continue;
            ImVec2 rectSize(rectMax.x - rectMin.x, rectMax.y - rectMin.y);
            if (rectSize.x <= 1.0f || rectSize.y <= 1.0f) continue;
            const bool disableCulling =
                obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.disableCulling;
            if (!disableCulling && rectOutsideOverlay(rectMin, rectMax)) continue;
            resolvedUiRects[obj.id] = ResolvedUiRect{rectMin, rectMax};
            drawnUiObjects.push_back(&obj);

            ImGuiStyle savedStyle = ImGui::GetStyle();
            bool styleApplied = false;
            if (!obj.ui.stylePreset.empty()) {
                if (const auto* preset = getUIStylePreset(obj.ui.stylePreset)) {
                    ImGui::GetStyle() = preset->style;
                    styleApplied = true;
                }
            }

            if (obj.ui.type == UIElementType::Canvas) {
                spriteBatch.flush();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImU32 edgeColor = obj.ui.maskChildren ? IM_COL32(74, 228, 255, 225)
                                                            : IM_COL32(110, 170, 255, 140);
                const float thickness = obj.ui.maskChildren ? 2.4f : 1.5f;
                dl->AddRect(rectMin, rectMax, edgeColor, 6.0f, 0, thickness);
                if (obj.ui.maskChildren) {
                    const float inset = 2.0f;
                    if ((rectMax.x - rectMin.x) > inset * 2.0f && (rectMax.y - rectMin.y) > inset * 2.0f) {
                        dl->AddRect(ImVec2(rectMin.x + inset, rectMin.y + inset),
                                    ImVec2(rectMax.x - inset, rectMax.y - inset),
                                    IM_COL32(32, 190, 230, 175), 5.0f, 0, 1.0f);
                    }
                }
                if (styleApplied) ImGui::GetStyle() = savedStyle;
                continue;
            }

            ImVec2 drawMin = rectMin;
            ImVec2 drawMax = rectMax;
            ImVec2 drawSize(drawMax.x - drawMin.x, drawMax.y - drawMin.y);
            ImVec2 localMin(drawMin.x - overlayPos.x, drawMin.y - overlayPos.y);
            bool pushedCanvasMask = false;
            if (obj.ui.type != UIElementType::Canvas) {
                ImVec2 maskMin, maskMax;
                if (resolveCanvasMaskRectForObject(obj, maskMin, maskMax)) {
                    maskMin.x = std::max(maskMin.x, overlayPos.x);
                    maskMin.y = std::max(maskMin.y, overlayPos.y);
                    maskMax.x = std::min(maskMax.x, overlayPos.x + overlaySize.x);
                    maskMax.y = std::min(maskMax.y, overlayPos.y + overlaySize.y);
                    if (maskMax.x <= maskMin.x || maskMax.y <= maskMin.y) {
                        if (styleApplied) ImGui::GetStyle() = savedStyle;
                        continue;
                    }
                    if (drawMax.x <= maskMin.x || drawMin.x >= maskMax.x ||
                        drawMax.y <= maskMin.y || drawMin.y >= maskMax.y) {
                        if (styleApplied) ImGui::GetStyle() = savedStyle;
                        continue;
                    }
                    spriteBatch.flush();
                    ImGui::PushClipRect(maskMin, maskMax, true);
                    pushedCanvasMask = true;
                }
            }

            ImGui::PushID(obj.id);
            UIAnimationState& animState = uiAnimationStates[obj.id];
            if (!animState.initialized) {
                animState.sliderValue = obj.ui.sliderValue;
                animState.initialized = true;
            }
            if (obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D) {
                if (light2DRenderedObjectIds.find(obj.id) != light2DRenderedObjectIds.end()) {
                    if (pushedCanvasMask) {
                        ImGui::PopClipRect();
                    }
                    ImGui::PopID();
                    if (styleApplied) ImGui::GetStyle() = savedStyle;
                    continue;
                }
                Texture* spriteTex = spriteTextureResolver.resolveTexture(obj);
                unsigned int texId = (spriteTex != nullptr) ? spriteTex->GetID() : 0;
                std::array<ImVec2, 4> uvQuad = buildSpriteSheetUvs(obj);
                const int frame = resolveSpriteSheetFrame(obj);
                const ImVec2 sourceFrameSizePx = ResolveUiSourceFrameSizePx(obj, frame, spriteTex);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                const ImU32 tintColor = ImGui::GetColorU32(tint);
                bool repeatX = obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.repeatX;
                bool repeatY = obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.repeatY;
                glm::vec2 spriteSizeWorld = getSpriteDisplaySize(obj);
                glm::vec2 spacing = obj.hasParallaxLayer2D ? obj.parallaxLayer2D.repeatSpacing : glm::vec2(0.0f);
                float stepX = spriteSizeWorld.x + spacing.x;
                float stepY = spriteSizeWorld.y + spacing.y;
                glm::vec2 baseWorldMin = worldViewMin;
                if (repeatX || repeatY) {
                    glm::vec2 sizeWorld = spriteSizeWorld;
                    ImVec2 pivotOffset = ImVec2(sizeWorld.x * 0.5f, sizeWorld.y * 0.5f);
                    switch (obj.ui.anchor) {
                        case UIAnchor::TopLeft: pivotOffset = ImVec2(0.0f, 0.0f); break;
                        case UIAnchor::TopRight: pivotOffset = ImVec2(sizeWorld.x, 0.0f); break;
                        case UIAnchor::BottomLeft: pivotOffset = ImVec2(0.0f, sizeWorld.y); break;
                        case UIAnchor::BottomRight: pivotOffset = ImVec2(sizeWorld.x, sizeWorld.y); break;
                        default: break;
                    }
                    glm::vec2 parentOffset = uiSceneLookup.getWorldParentOffset(obj);
                    glm::vec2 worldPos = parentOffset + glm::vec2(obj.ui.position.x, obj.ui.position.y) + parallaxOffset(obj);
                    baseWorldMin = worldPos - glm::vec2(pivotOffset.x, pivotOffset.y);
                }
                float angle = glm::radians(obj.ui.rotation);
                auto drawImageRect = [&](const ImVec2& min, const ImVec2& max) {
                    ImVec2 size(max.x - min.x, max.y - min.y);
                    if (size.x <= 1.0f || size.y <= 1.0f) return;
                    ImVec2 drawMinLocal(min.x, min.y);
                    ImVec2 drawMaxLocal(max.x, max.y);
                    if (DrawNineSliceSprite(spriteBatch,
                                            (ImTextureID)(intptr_t)texId,
                                            obj,
                                            drawMinLocal,
                                            drawMaxLocal,
                                            uvQuad,
                                            sourceFrameSizePx,
                                            angle,
                                            tintColor)) {
                        return;
                    }
                    if (std::abs(angle) > 1e-4f) {
                        ImVec2 center((drawMinLocal.x + drawMaxLocal.x) * 0.5f, (drawMinLocal.y + drawMaxLocal.y) * 0.5f);
                        ImVec2 half(size.x * 0.5f, size.y * 0.5f);
                        float c = std::cos(angle);
                        float s = std::sin(angle);
                        auto rotPt = [&](float x, float y) {
                            return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
                        };
                        ImVec2 p0 = rotPt(-half.x, -half.y);
                        ImVec2 p1 = rotPt( half.x, -half.y);
                        ImVec2 p2 = rotPt( half.x,  half.y);
                        ImVec2 p3 = rotPt(-half.x,  half.y);
                        if (texId != 0) {
                            spriteBatch.push((ImTextureID)(intptr_t)texId,
                                             p0, p1, p2, p3,
                                             uvQuad[0], uvQuad[1], uvQuad[2], uvQuad[3],
                                             tintColor);
                        } else {
                            spriteBatch.flush();
                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            ImU32 fill = tintColor;
                            ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                            dl->AddQuadFilled(p0, p1, p2, p3, fill);
                            dl->AddQuad(p0, p1, p2, p3, border, 2.0f);
                            ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                            ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                            dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                        }
                    } else {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        if (texId != 0) {
                            spriteBatch.push((ImTextureID)(intptr_t)texId,
                                             drawMinLocal,
                                             ImVec2(drawMaxLocal.x, drawMinLocal.y),
                                             drawMaxLocal,
                                             ImVec2(drawMinLocal.x, drawMaxLocal.y),
                                             uvQuad[0],
                                             ImVec2(uvQuad[2].x, uvQuad[0].y),
                                             uvQuad[2],
                                             ImVec2(uvQuad[0].x, uvQuad[2].y),
                                             tintColor);
                        } else {
                            spriteBatch.flush();
                            ImU32 fill = tintColor;
                            ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                            dl->AddRectFilled(drawMinLocal, drawMaxLocal, fill, 6.0f);
                            dl->AddRect(drawMinLocal, drawMaxLocal, border, 6.0f);
                            ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                            ImVec2 textPos(drawMinLocal.x + (size.x - textSize.x) * 0.5f,
                                           drawMinLocal.y + (size.y - textSize.y) * 0.5f);
                            dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                        }
                    }
                };

                if (repeatX || repeatY) {
                    int startX = repeatX ? static_cast<int>(std::floor((worldViewMin.x - baseWorldMin.x) / stepX)) - 1 : 0;
                    int endX = repeatX ? static_cast<int>(std::ceil((worldViewMax.x - baseWorldMin.x) / stepX)) + 1 : 0;
                    int startY = repeatY ? static_cast<int>(std::floor((worldViewMin.y - baseWorldMin.y) / stepY)) - 1 : 0;
                    int endY = repeatY ? static_cast<int>(std::ceil((worldViewMax.y - baseWorldMin.y) / stepY)) + 1 : 0;
                    for (int ix = startX; ix <= endX; ++ix) {
                        for (int iy = startY; iy <= endY; ++iy) {
                            float dx = repeatX ? (float)ix * stepX : 0.0f;
                            float dy = repeatY ? (float)iy * stepY : 0.0f;
                            glm::vec2 tileMin = baseWorldMin + glm::vec2(dx, dy);
                            ImVec2 s0 = worldToScreen(tileMin);
                            ImVec2 s1 = worldToScreen(tileMin + glm::vec2(spriteSizeWorld.x, spriteSizeWorld.y));
                            ImVec2 tMin(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
                            ImVec2 tMax(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
                            drawImageRect(tMin, tMax);
                        }
                    }
                } else {
                    drawImageRect(drawMin, drawMax);
                }
            } else if (obj.ui.type == UIElementType::Slider) {
                spriteBatch.flush();
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                const bool uiWidgetInteractive = isPlaying && !uiWorldCameraActive && obj.ui.interactable;
                if (uiWidgetInteractive) {
                    ImGui::SetCursorPos(localMin);
                }
                if (obj.ui.sliderStyle == UISliderStyle::ImGui) {
                    float minValue = obj.ui.sliderMin;
                    float maxValue = obj.ui.sliderMax;
                    float range = (maxValue - minValue);
                    if (range <= 1e-6f) range = 1.0f;
                    if (uiWidgetInteractive) {
                        ImGui::PushItemWidth(drawSize.x);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, brighten(tint, 0.5f));
                        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, brighten(tint, 0.7f));
                        ImGui::PushStyleColor(ImGuiCol_SliderGrab, brighten(tint, 0.9f));
                        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, brighten(tint, 1.1f));
                        if (ImGui::SliderFloat(obj.ui.label.c_str(), &obj.ui.sliderValue, minValue, maxValue)) {
                            projectManager.currentProject.hasUnsavedChanges = true;
                        }
                        ImGui::PopStyleColor(5);
                        ImGui::PopItemWidth();
                    } else {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImU32 bg = ImGui::GetColorU32(ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                        ImU32 fill = ImGui::GetColorU32(tint);
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        float t = (obj.ui.sliderValue - minValue) / range;
                        t = std::clamp(t, 0.0f, 1.0f);
                        float rounding = 6.0f;
                        ImVec2 fillMax(drawMin.x + drawSize.x * t, drawMax.y);
                        dl->AddRectFilled(drawMin, drawMax, bg, rounding);
                        if (fillMax.x > drawMin.x) {
                            dl->AddRectFilled(drawMin, fillMax, fill, rounding);
                        }
                        dl->AddRect(drawMin, drawMax, border, rounding);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    }
                } else {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 bg = ImGui::GetColorU32(ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                    ImU32 fill = ImGui::GetColorU32(tint);
                    ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                    float minValue = obj.ui.sliderMin;
                    float maxValue = obj.ui.sliderMax;
                    float range = (maxValue - minValue);
                    if (range <= 1e-6f) range = 1.0f;
                    bool held = false;
                    if (uiWidgetInteractive) {
                        ImGui::InvisibleButton("##UISlider", drawSize);
                        held = ImGui::IsItemActive();
                    }
                    if (held && ImGui::IsMouseDown(ImGuiMouseButton_Left) && drawSize.x > 1.0f) {
                        float mouseT = (ImGui::GetIO().MousePos.x - drawMin.x) / drawSize.x;
                        mouseT = std::clamp(mouseT, 0.0f, 1.0f);
                        float newValue = minValue + mouseT * range;
                        if (newValue != obj.ui.sliderValue) {
                            obj.ui.sliderValue = newValue;
                            projectManager.currentProject.hasUnsavedChanges = true;
                        }
                    }

                    animateValue(animState.sliderValue, obj.ui.sliderValue, held);
                    float displayValue = (uiAnimationMode == UIAnimationMode::Off) ? obj.ui.sliderValue : animState.sliderValue;
                    float t = (displayValue - minValue) / range;
                    t = std::clamp(t, 0.0f, 1.0f);

                    if (obj.ui.sliderStyle == UISliderStyle::Fill) {
                        float rounding = 6.0f;
                        ImVec2 fillMax(drawMin.x + drawSize.x * t, drawMax.y);
                        dl->AddRectFilled(drawMin, drawMax, bg, rounding);
                        if (fillMax.x > drawMin.x) {
                            dl->AddRectFilled(drawMin, fillMax, fill, rounding);
                        }
                        dl->AddRect(drawMin, drawMax, border, rounding);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    } else if (obj.ui.sliderStyle == UISliderStyle::Circle) {
                        ImVec2 center((drawMin.x + drawMax.x) * 0.5f, (drawMin.y + drawMax.y) * 0.5f);
                        float radius = std::max(2.0f, std::min(drawSize.x, drawSize.y) * 0.5f - 2.0f);
                        dl->AddCircleFilled(center, radius, bg, 32);
                        float start = -IM_PI * 0.5f;
                        float end = start + t * IM_PI * 2.0f;
                        dl->PathClear();
                        dl->PathArcTo(center, radius, start, end, 32);
                        dl->PathLineTo(center);
                        dl->PathFillConvex(fill);
                        dl->AddCircle(center, radius, border, 32, 2.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    }
                }
            } else if (obj.ui.type == UIElementType::Button) {
                spriteBatch.flush();
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                obj.ui.buttonPressed = false;
                const bool uiWidgetInteractive = isPlaying && !uiWorldCameraActive && obj.ui.interactable;
                if (uiWidgetInteractive) {
                    ImGui::SetCursorPos(localMin);
                }
                if (obj.ui.buttonStyle == UIButtonStyle::ImGui) {
                    if (uiWidgetInteractive) {
                        ImGui::PushStyleColor(ImGuiCol_Button, tint);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brighten(tint, 1.1f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, brighten(tint, 1.2f));
                        obj.ui.buttonPressed = ImGui::Button(obj.ui.label.c_str(), drawSize);
                        ImGui::PopStyleColor(3);
                    } else {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImU32 fill = ImGui::GetColorU32(tint);
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        dl->AddRectFilled(drawMin, drawMax, fill, 6.0f);
                        dl->AddRect(drawMin, drawMax, border, 6.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    }
                } else if (obj.ui.buttonStyle == UIButtonStyle::Outline) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 border = ImGui::GetColorU32(tint);
                    bool hovered = false;
                    bool active = false;
                    if (uiWidgetInteractive) {
                        if (ImGui::InvisibleButton("##UIButton", drawSize)) {
                            obj.ui.buttonPressed = true;
                        }
                        hovered = ImGui::IsItemHovered();
                        active = ImGui::IsItemActive();
                    }
                    float hoverT = animateValue(animState.hover, hovered ? 1.0f : 0.0f, false);
                    float activeT = animateValue(animState.active, active ? 1.0f : 0.0f, false);
                    if (hoverT > 0.001f) {
                        ImVec4 hoverCol = brighten(tint, 0.45f);
                        hoverCol.w *= std::clamp(hoverT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(hoverCol), 6.0f);
                    }
                    if (activeT > 0.001f) {
                        ImVec4 activeCol = brighten(tint, 0.65f);
                        activeCol.w *= std::clamp(activeT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(activeCol), 6.0f);
                    }
                    dl->AddRect(drawMin, drawMax, border, 6.0f, 0, 2.0f);
                    ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                    ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                   drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                    dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                }
            } else if (obj.ui.type == UIElementType::Text) {
                spriteBatch.flush();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                float scale = std::max(0.1f, obj.ui.textScale);
                float scaleFactor = std::max(0.01f, uiWorldCamera.zoom / 100.0f);
                float fontSize = std::max(1.0f, ImGui::GetFontSize() * scale * scaleFactor);
                ImGui::PushClipRect(drawMin, drawMax, true);
                AddUITextWithFilter(dl,
                                    obj.material.textureFilter,
                                    ImGui::GetFont(),
                                    fontSize,
                                    drawMin,
                                    drawMax,
                                    ImGui::GetColorU32(tint),
                                    obj.ui.label.c_str(),
                                    obj.ui.textAutoWrap,
                                    obj.ui.textHAlign,
                                    obj.ui.textVAlign,
                                    obj.ui.textEffectFlags,
                                    obj.ui.textEffectSpeed,
                                    obj.ui.textEffectIntensity);
                ImGui::PopClipRect();
            }
            if (pushedCanvasMask) {
                spriteBatch.flush();
                ImGui::PopClipRect();
            }
            ImGui::PopID();
            if (styleApplied) ImGui::GetStyle() = savedStyle;
        }
        spriteBatch.flush();
        if (worldUiEditing) {
            light2DCompositorRanLastFrame = renderedLight2DComposite;
            light2DLightBufferHadContentLastFrame = lightBufferHadContent;
            light2DActiveCountLastFrame = activeLight2DCount;
            light2DLitSprite2DCountLastFrame = litSprite2DCount;
            light2DLitWorldImageCountLastFrame = litWorldImageCount;
            if (captureLight2DRoutingReasons) {
                light2DObjectRoutingReasonsLastFrame = std::move(light2DRoutingReasons);
            }
        }
        if (renderedLight2DComposite && showLight2DStatsOverlay) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            char lightStatsLabel[160];
            std::snprintf(lightStatsLabel,
                          sizeof(lightStatsLabel),
                          "2D Lights %d  Sprites %d  Cost %.2f ms",
                          light2DStats.visibleLights,
                          light2DStats.visibleSprites,
                          light2DStats.cpuBuildMs);
            dl->AddText(ImVec2(overlayPos.x + 12.0f, overlayPos.y + 10.0f),
                        IM_COL32(255, 232, 170, 235),
                        lightStatsLabel);
        }

        bool light2DHandleUsed = false;
        if (showSceneGizmos &&
            (gizmoShowLight2DBounds || gizmoShowLight2DShapes || gizmoShowShadowCaster2DBounds)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (const SceneObject& target : sceneObjects) {
                if (!IsObjectEnabledInHierarchy(target)) {
                    continue;
                }

                if (target.hasLight2D && target.light2D.enabled) {
                    const glm::vec2 lightWorld(target.position.x, target.position.y);
                    const ImVec2 center = worldToScreen(lightWorld);
                    const float outerRadiusPx = std::max(target.light2D.radius, target.light2D.outerRadius) * uiWorldCamera.zoom;
                    const float innerRadiusPx = target.light2D.innerRadius * uiWorldCamera.zoom;
                    const bool selectedLight = (selectedObjectId == target.id) ||
                        (std::find(selectedObjectIds.begin(), selectedObjectIds.end(), target.id) != selectedObjectIds.end());

                    if (gizmoShowLight2DBounds &&
                        (target.light2D.type == Light2DType::Point || target.light2D.type == Light2DType::Spot)) {
                        const ImU32 outerColor = selectedLight
                            ? IM_COL32(255, 238, 120, 255)
                            : IM_COL32(214, 208, 84, 180);
                        dl->AddCircle(center, std::max(2.0f, outerRadiusPx), outerColor, 64, 1.5f);
                        if (innerRadiusPx > 1.0f) {
                            dl->AddCircle(center, innerRadiusPx, IM_COL32(255, 255, 255, 85), 48, 1.0f);
                        }
                        if (target.light2D.type == Light2DType::Spot) {
                            const float dir = glm::radians(target.rotation.z);
                            const float outerHalf = glm::radians(target.light2D.outerSpotAngle) * 0.5f;
                            const glm::vec2 rayA(std::cos(dir - outerHalf), std::sin(dir - outerHalf));
                            const glm::vec2 rayB(std::cos(dir + outerHalf), std::sin(dir + outerHalf));
                            dl->AddLine(center,
                                        ImVec2(center.x + rayA.x * outerRadiusPx, center.y - rayA.y * outerRadiusPx),
                                        outerColor, 1.5f);
                            dl->AddLine(center,
                                        ImVec2(center.x + rayB.x * outerRadiusPx, center.y - rayB.y * outerRadiusPx),
                                        outerColor, 1.5f);
                        }
                    }

                    if (gizmoShowLight2DShapes &&
                        (target.light2D.type == Light2DType::Freeform || target.light2D.type == Light2DType::Sprite) &&
                        target.light2D.shapePoints.size() >= 2) {
                        const ImU32 edgeColor = selectedLight
                            ? IM_COL32(255, 244, 150, 255)
                            : IM_COL32(248, 198, 96, 190);
                        std::vector<ImVec2> screenPoints;
                        screenPoints.reserve(target.light2D.shapePoints.size());
                        for (const glm::vec2& point : target.light2D.shapePoints) {
                            screenPoints.push_back(worldToScreen(lightWorld + point));
                        }
                        for (size_t i = 0; i < screenPoints.size(); ++i) {
                            const ImVec2 a = screenPoints[i];
                            const ImVec2 b = screenPoints[(i + 1) % screenPoints.size()];
                            dl->AddLine(a, b, edgeColor, 1.8f);
                        }
                    }
                }

                if (gizmoShowShadowCaster2DBounds &&
                    target.hasShadowCaster2D &&
                    target.shadowCaster2D.enabled &&
                    target.shadowCaster2D.points.size() >= 2) {
                    const glm::vec2 casterWorld(target.position.x, target.position.y);
                    const bool selectedCaster = (selectedObjectId == target.id) ||
                        (std::find(selectedObjectIds.begin(), selectedObjectIds.end(), target.id) != selectedObjectIds.end());
                    const ImU32 edgeColor = selectedCaster
                        ? IM_COL32(120, 220, 255, 255)
                        : IM_COL32(88, 160, 220, 190);
                    std::vector<ImVec2> screenPoints;
                    screenPoints.reserve(target.shadowCaster2D.points.size());
                    for (const glm::vec2& point : target.shadowCaster2D.points) {
                        screenPoints.push_back(worldToScreen(casterWorld + point));
                    }
                    for (size_t i = 0; i < screenPoints.size(); ++i) {
                        const ImVec2 a = screenPoints[i];
                        const ImVec2 b = screenPoints[(i + 1) % screenPoints.size()];
                        dl->AddLine(a, b, edgeColor, 1.6f);
                    }
                }
            }
        }

        if (worldUiEditing && light2DShapeEditMode && light2DShapeEditingObjectId >= 0) {
            SceneObject* editObject = findObjectById(light2DShapeEditingObjectId);
            if (editObject && IsObjectEnabledInHierarchy(*editObject)) {
                std::vector<glm::vec2>* editPoints = nullptr;
                bool editingLight = false;
                if (editObject->hasLight2D &&
                    (editObject->light2D.type == Light2DType::Freeform || editObject->light2D.type == Light2DType::Sprite)) {
                    editPoints = &editObject->light2D.shapePoints;
                    editingLight = true;
                } else if (editObject->hasShadowCaster2D) {
                    editPoints = &editObject->shadowCaster2D.points;
                }

                if (editPoints && editPoints->size() >= 2) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const glm::vec2 origin(editObject->position.x, editObject->position.y);
                    std::vector<ImVec2> handlePoints;
                    handlePoints.reserve(editPoints->size());
                    for (const glm::vec2& point : *editPoints) {
                        handlePoints.push_back(worldToScreen(origin + point));
                    }

                    for (size_t i = 0; i < handlePoints.size(); ++i) {
                        dl->AddLine(handlePoints[i],
                                    handlePoints[(i + 1) % handlePoints.size()],
                                    editingLight ? IM_COL32(255, 248, 170, 255) : IM_COL32(128, 232, 255, 255),
                                    2.0f);
                    }

                    int hoveredPoint = -1;
                    float hoveredDistanceSq = 81.0f;
                    const ImVec2 mouse = ImGui::GetIO().MousePos;
                    for (size_t i = 0; i < handlePoints.size(); ++i) {
                        const float dx = mouse.x - handlePoints[i].x;
                        const float dy = mouse.y - handlePoints[i].y;
                        const float distanceSq = dx * dx + dy * dy;
                        if (distanceSq < hoveredDistanceSq) {
                            hoveredDistanceSq = distanceSq;
                            hoveredPoint = static_cast<int>(i);
                        }
                    }

                    for (size_t i = 0; i < handlePoints.size(); ++i) {
                        const bool activePoint = (light2DShapeEditingPointIndex == static_cast<int>(i));
                        const bool hotPoint = (hoveredPoint == static_cast<int>(i));
                        const ImU32 pointColor = activePoint
                            ? IM_COL32(255, 214, 86, 255)
                            : (hotPoint ? IM_COL32(255, 255, 255, 255) : IM_COL32(235, 235, 235, 220));
                        dl->AddCircleFilled(handlePoints[i], activePoint ? 6.5f : 5.0f, pointColor, 18);
                        dl->AddCircle(handlePoints[i], activePoint ? 6.5f : 5.0f, IM_COL32(20, 20, 20, 220), 18, 1.0f);
                    }

                    if (uiWorldHover && !uiWorldCameraActive && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver()) {
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            light2DHandleUsed = (hoveredPoint >= 0);
                            if (hoveredPoint >= 0) {
                                if (ImGui::GetIO().KeyAlt && editPoints->size() > 3) {
                                    editPoints->erase(editPoints->begin() + hoveredPoint);
                                    light2DShapeEditingPointIndex = -1;
                                    if (editingLight) {
                                        lighting2DRenderer.clearPolygonCache(editObject->id);
                                    }
                                    projectManager.currentProject.hasUnsavedChanges = true;
                                } else {
                                    light2DShapeEditingPointIndex = hoveredPoint;
                                }
                            } else if (ImGui::GetIO().KeyCtrl && editPoints->size() >= 2) {
                                int insertAfter = -1;
                                float bestSegmentDistanceSq = 144.0f;
                                for (size_t i = 0; i < handlePoints.size(); ++i) {
                                    const ImVec2 a = handlePoints[i];
                                    const ImVec2 b = handlePoints[(i + 1) % handlePoints.size()];
                                    const ImVec2 ab(b.x - a.x, b.y - a.y);
                                    const float denom = ab.x * ab.x + ab.y * ab.y;
                                    if (denom <= 0.0001f) {
                                        continue;
                                    }
                                    const float t = std::clamp(((mouse.x - a.x) * ab.x + (mouse.y - a.y) * ab.y) / denom, 0.0f, 1.0f);
                                    const ImVec2 closest(a.x + ab.x * t, a.y + ab.y * t);
                                    const float dx = mouse.x - closest.x;
                                    const float dy = mouse.y - closest.y;
                                    const float distanceSq = dx * dx + dy * dy;
                                    if (distanceSq < bestSegmentDistanceSq) {
                                        bestSegmentDistanceSq = distanceSq;
                                        insertAfter = static_cast<int>(i);
                                    }
                                }
                                if (insertAfter >= 0) {
                                    const glm::vec2 worldMouse = screenToWorld(mouse);
                                    editPoints->insert(editPoints->begin() + insertAfter + 1, worldMouse - origin);
                                    light2DShapeEditingPointIndex = insertAfter + 1;
                                    if (editingLight) {
                                        lighting2DRenderer.clearPolygonCache(editObject->id);
                                    }
                                    projectManager.currentProject.hasUnsavedChanges = true;
                                    light2DHandleUsed = true;
                                }
                            }
                        }

                        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                            light2DShapeEditingPointIndex >= 0 &&
                            light2DShapeEditingPointIndex < static_cast<int>(editPoints->size())) {
                            const glm::vec2 worldMouse = screenToWorld(mouse);
                            (*editPoints)[static_cast<size_t>(light2DShapeEditingPointIndex)] = worldMouse - origin;
                            if (editingLight) {
                                lighting2DRenderer.clearPolygonCache(editObject->id);
                            }
                            projectManager.currentProject.hasUnsavedChanges = true;
                            light2DHandleUsed = true;
                        }
                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                            light2DShapeEditingPointIndex = -1;
                        }
                    }
                }
            } else {
                light2DShapeEditMode = false;
                light2DShapeEditingObjectId = -1;
                light2DShapeEditingPointIndex = -1;
            }
        }

        auto drawCollider2DWorldOutline = [&](const SceneObject& target, ImU32 outlineColor, float thickness) {
            if (!IsObjectEnabledInHierarchy(target)) return;
            if (!(target.hasCollider2D && target.collider2D.enabled)) return;
            if (!isUIType(target) || target.ui.type == UIElementType::Canvas) return;
            std::vector<glm::vec2> localPoints;
            if (target.collider2D.type == Collider2DType::Box) {
                glm::vec2 half = target.collider2D.boxSize * 0.5f;
                localPoints = {
                    glm::vec2(-half.x, -half.y) + target.collider2D.offset,
                    glm::vec2( half.x, -half.y) + target.collider2D.offset,
                    glm::vec2( half.x,  half.y) + target.collider2D.offset,
                    glm::vec2(-half.x,  half.y) + target.collider2D.offset
                };
            } else {
                localPoints = target.collider2D.points;
                if (localPoints.empty() && target.collider2D.type == Collider2DType::Edge) {
                    float half = target.collider2D.boxSize.x * 0.5f;
                    localPoints = {
                        glm::vec2(-half, 0.0f),
                        glm::vec2(half, 0.0f)
                    };
                }
                for (glm::vec2& point : localPoints) {
                    point += target.collider2D.offset;
                }
            }

            if (localPoints.size() < 2) return;

            glm::vec2 parentOffset = uiSceneLookup.getWorldParentOffset(target);
            glm::vec2 pivotWorld = parentOffset + glm::vec2(target.ui.position.x, target.ui.position.y) + parallaxOffset(target);
            float angle = glm::radians(target.ui.rotation);
            float c = std::cos(angle);
            float s = std::sin(angle);
            auto rotatePoint2D = [c, s](const glm::vec2& p) {
                return glm::vec2(p.x * c - p.y * s, p.x * s + p.y * c);
            };

            std::vector<ImVec2> screenPoints;
            screenPoints.reserve(localPoints.size());
            for (const glm::vec2& point : localPoints) {
                screenPoints.push_back(worldToScreen(pivotWorld + rotatePoint2D(point)));
            }

            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (size_t i = 1; i < screenPoints.size(); ++i) {
                dl->AddLine(screenPoints[i - 1], screenPoints[i], outlineColor, thickness);
            }
            if (target.collider2D.type == Collider2DType::Box ||
                target.collider2D.type == Collider2DType::Polygon ||
                target.collider2D.closed) {
                dl->AddLine(screenPoints.back(), screenPoints.front(), outlineColor, thickness);
            }
        };
        auto drawSelectedCollider2DWorldOutlines = [&]() {
            std::vector<int> roots = selectedObjectIds;
            if (roots.empty() && selectedObjectId >= 0) {
                roots.push_back(selectedObjectId);
            }
            if (roots.empty()) {
                return;
            }

            std::unordered_set<int> rootSet(roots.begin(), roots.end());
            std::unordered_set<int> visited;
            std::vector<int> stack = roots;
            while (!stack.empty()) {
                const int id = stack.back();
                stack.pop_back();
                if (!visited.insert(id).second) {
                    continue;
                }

                SceneObject* node = findObjectById(id);
                if (!node || !IsObjectEnabledInHierarchy(*node)) {
                    continue;
                }

                if (node->hasCollider2D && node->collider2D.enabled) {
                    const bool isDirectSelected = rootSet.find(id) != rootSet.end();
                    const ImU32 color = isDirectSelected
                        ? ImGui::GetColorU32(ImVec4(0.24f, 0.95f, 1.0f, 0.95f))
                        : ImGui::GetColorU32(ImVec4(0.38f, 1.0f, 0.78f, 0.85f));
                    const float thickness = isDirectSelected ? 2.2f : 1.8f;
                    drawCollider2DWorldOutline(*node, color, thickness);
                }

                for (int childId : node->childIds) {
                    if (childId >= 0) {
                        stack.push_back(childId);
                    }
                }
            }
        };
        drawSelectedCollider2DWorldOutlines();

        bool gizmoUsed = false;
        bool mouseBlockedBySelectedGizmo = false;
        if (worldUiEditing && uiWorldHover) {
            SceneObject* selectedForGizmoHit = getSelectedObject();
            if (selectedForGizmoHit && isUIType(*selectedForGizmoHit)) {
                std::vector<int> selectedIdsForBounds;
                if (!selectedObjectIds.empty()) {
                    selectedIdsForBounds = selectedObjectIds;
                } else if (selectedObjectId >= 0) {
                    selectedIdsForBounds.push_back(selectedObjectId);
                } else {
                    selectedIdsForBounds.push_back(selectedForGizmoHit->id);
                }

                ImVec2 selectedBoundsMin(FLT_MAX, FLT_MAX);
                ImVec2 selectedBoundsMax(-FLT_MAX, -FLT_MAX);
                for (int id : selectedIdsForBounds) {
                    SceneObject* target = findObjectById(id);
                    if (!target || !IsObjectEnabledInHierarchy(*target) || !isUIType(*target)) continue;
                    ImVec2 targetMin, targetMax;
                    auto rectIt = resolvedUiRects.find(id);
                    if (rectIt != resolvedUiRects.end()) {
                        targetMin = rectIt->second.min;
                        targetMax = rectIt->second.max;
                    } else if (!resolveUIRectWorld(*target, targetMin, targetMax)) {
                        continue;
                    }
                    selectedBoundsMin.x = std::min(selectedBoundsMin.x, targetMin.x);
                    selectedBoundsMin.y = std::min(selectedBoundsMin.y, targetMin.y);
                    selectedBoundsMax.x = std::max(selectedBoundsMax.x, targetMax.x);
                    selectedBoundsMax.y = std::max(selectedBoundsMax.y, targetMax.y);
                }

                if (selectedBoundsMin.x != FLT_MAX && selectedBoundsMin.y != FLT_MAX) {
                    const float gizmoHitPadding = 18.0f;
                    ImVec2 mouse = ImGui::GetIO().MousePos;
                    mouseBlockedBySelectedGizmo =
                        mouse.x >= selectedBoundsMin.x - gizmoHitPadding &&
                        mouse.x <= selectedBoundsMax.x + gizmoHitPadding &&
                        mouse.y >= selectedBoundsMin.y - gizmoHitPadding &&
                        mouse.y <= selectedBoundsMax.y + gizmoHitPadding;
                }
            }
        }
        if (worldUiEditing && uiWorldHover && !uiWorldCameraActive && !light2DHandleUsed && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGuizmo::IsUsing() && !ImGuizmo::IsOver() && !mouseBlockedBySelectedGizmo) {
            ImVec2 mouse = ImGui::GetIO().MousePos;
            bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
            int hitId = -1;
            for (auto it = drawnUiObjects.rbegin(); it != drawnUiObjects.rend(); ++it) {
                const SceneObject& obj = *(*it);
                if (obj.ui.type == UIElementType::Canvas) continue;
                auto rectIt = resolvedUiRects.find(obj.id);
                if (rectIt == resolvedUiRects.end()) continue;
                const ImVec2& rectMin = rectIt->second.min;
                const ImVec2& rectMax = rectIt->second.max;
                if (mouse.x >= rectMin.x && mouse.x <= rectMax.x &&
                    mouse.y >= rectMin.y && mouse.y <= rectMax.y) {
                    hitId = obj.id;
                    break;
                }
            }
            if (hitId >= 0) {
                setPrimarySelection(hitId, additive);
                gizmoUsed = true;
            } else if (!additive) {
                clearSelection();
            }
        }

        SceneObject* selected = getSelectedObject();
        if (worldUiEditing && selected && isUIType(*selected)) {
            auto anchorToPivotUI = [](UIAnchor anchor, const ImVec2& size) {
                switch (anchor) {
                    case UIAnchor::TopLeft: return ImVec2(0.0f, 0.0f);
                    case UIAnchor::TopRight: return ImVec2(size.x, 0.0f);
                    case UIAnchor::BottomLeft: return ImVec2(0.0f, size.y);
                    case UIAnchor::BottomRight: return ImVec2(size.x, size.y);
                    default: return ImVec2(size.x * 0.5f, size.y * 0.5f);
                }
            };
            auto resolveUiRectCached = [&](SceneObject& target, ImVec2& outMin, ImVec2& outMax) -> bool {
                auto itRect = resolvedUiRects.find(target.id);
                if (itRect != resolvedUiRects.end()) {
                    outMin = itRect->second.min;
                    outMax = itRect->second.max;
                    return true;
                }
                return resolveUIRectWorld(target, outMin, outMax);
            };

            std::vector<int> candidateIds;
            if (!selectedObjectIds.empty()) {
                candidateIds = selectedObjectIds;
            } else if (selectedObjectId >= 0) {
                candidateIds.push_back(selectedObjectId);
            }
            if (candidateIds.empty()) {
                candidateIds.push_back(selected->id);
            }

            std::vector<int> gizmoTargets;
            gizmoTargets.reserve(candidateIds.size());
            for (int id : candidateIds) {
                SceneObject* candidate = findObjectById(id);
                if (!candidate || !IsObjectEnabledInHierarchy(*candidate)) continue;
                if (!isUIType(*candidate)) continue;
                ImVec2 candidateMin, candidateMax;
                if (!resolveUiRectCached(*candidate, candidateMin, candidateMax)) continue;
                gizmoTargets.push_back(id);
            }
            if (gizmoTargets.empty()) {
                gizmoTargets.push_back(selected->id);
            }

            std::unordered_set<int> selectedSet(gizmoTargets.begin(), gizmoTargets.end());
            auto hasSelectedAncestor = [&](int id) {
                SceneObject* current = findObjectById(id);
                int parentId = current ? current->parentId : -1;
                while (parentId != -1) {
                    if (selectedSet.count(parentId) > 0) {
                        return true;
                    }
                    SceneObject* parent = findObjectById(parentId);
                    parentId = parent ? parent->parentId : -1;
                }
                return false;
            };

            std::vector<int> gizmoRoots;
            gizmoRoots.reserve(gizmoTargets.size());
            for (int id : gizmoTargets) {
                if (!hasSelectedAncestor(id)) {
                    gizmoRoots.push_back(id);
                }
            }
            if (gizmoRoots.empty()) {
                gizmoRoots = gizmoTargets;
            }

            ImVec2 boundsMin(FLT_MAX, FLT_MAX);
            ImVec2 boundsMax(-FLT_MAX, -FLT_MAX);
            for (int id : gizmoRoots) {
                SceneObject* obj = findObjectById(id);
                if (!obj) continue;
                ImVec2 targetMin, targetMax;
                if (!resolveUiRectCached(*obj, targetMin, targetMax)) continue;
                boundsMin.x = std::min(boundsMin.x, targetMin.x);
                boundsMin.y = std::min(boundsMin.y, targetMin.y);
                boundsMax.x = std::max(boundsMax.x, targetMax.x);
                boundsMax.y = std::max(boundsMax.y, targetMax.y);
            }
            if (boundsMin.x == FLT_MAX || boundsMin.y == FLT_MAX) {
                ImVec2 fallbackMin, fallbackMax;
                if (resolveUiRectCached(*selected, fallbackMin, fallbackMax)) {
                    boundsMin = fallbackMin;
                    boundsMax = fallbackMax;
                }
            }

            ImVec2 rectSize(boundsMax.x - boundsMin.x, boundsMax.y - boundsMin.y);
            if (rectSize.x > 1.0f && rectSize.y > 1.0f) {
                ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
                if (mCurrentGizmoOperation == ImGuizmo::SCALE) {
                    op = ImGuizmo::SCALE;
                } else if (mCurrentGizmoOperation == ImGuizmo::BOUNDS) {
                    op = ImGuizmo::BOUNDS;
                } else if (mCurrentGizmoOperation == ImGuizmo::ROTATE) {
                    op = ImGuizmo::ROTATE;
                }
                glm::mat4 view(1.0f);
                glm::mat4 proj = glm::ortho(0.0f, (float)(imageMax.x - imageMin.x),
                                            (float)(imageMax.y - imageMin.y), 0.0f, -1.0f, 1.0f);
                ImVec2 rectCenter((boundsMin.x + boundsMax.x) * 0.5f - imageMin.x,
                                  (boundsMin.y + boundsMax.y) * 0.5f - imageMin.y);
                glm::vec3 gizmoScale(1.0f, 1.0f, 1.0f);
                if (op == ImGuizmo::SCALE || op == ImGuizmo::BOUNDS) {
                    gizmoScale = glm::vec3(rectSize.x, rectSize.y, 1.0f);
                }
                glm::mat4 model(1.0f);
                model = glm::translate(model, glm::vec3(rectCenter.x, rectCenter.y, 0.0f));
                model = glm::rotate(model, glm::radians(selected->ui.rotation), glm::vec3(0.0f, 0.0f, 1.0f));
                model = glm::scale(model, gizmoScale);
                const bool stableRectScale = (op == ImGuizmo::SCALE || op == ImGuizmo::BOUNDS);
                if (stableRectScale && worldUiGizmoHistoryCaptured &&
                    worldUiRectGizmoOperation == op && !worldUiRectGizmoSnapshots.empty()) {
                    model = worldUiRectGizmoModel;
                }
                const glm::mat4 originalModel = model;

                ImGuizmo::BeginFrame();
                ImGuizmo::Enable(true);
                ImGuizmo::SetOrthographic(true);
                ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
                ImGuizmo::SetRect(imageMin.x, imageMin.y, imageMax.x - imageMin.x, imageMax.y - imageMin.y);
                glm::mat4 delta(1.0f);
                float bounds[6] = { -0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.0f };
                const float* boundsPtr = (op == ImGuizmo::BOUNDS) ? bounds : nullptr;
                ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, ImGuizmo::LOCAL,
                                     glm::value_ptr(model), glm::value_ptr(delta), nullptr, boundsPtr, nullptr);
                if (ImGuizmo::IsUsing()) {
                    if (!worldUiGizmoHistoryCaptured) {
                        recordState("worldUiGizmo");
                        worldUiRectGizmoOperation = op;
                        worldUiRectGizmoModel = originalModel;
                        worldUiRectGizmoStartMouse = ImGui::GetIO().MousePos;
                        worldUiRectGizmoSnapshots.clear();
                        for (int id : gizmoRoots) {
                            SceneObject* target = findObjectById(id);
                            if (!target) continue;
                            ImVec2 targetMin, targetMax;
                            if (!resolveUiRectCached(*target, targetMin, targetMax)) continue;
                            worldUiRectGizmoSnapshots.push_back(UiRectGizmoSnapshot{
                                id,
                                target->ui.position,
                                target->ui.size,
                                target->ui.rotation,
                                targetMin,
                                targetMax
                            });
                        }
                        worldUiGizmoHistoryCaptured = true;
                    }
                    const float scaleDragDx = ImGui::GetIO().MousePos.x - worldUiRectGizmoStartMouse.x;
                    const float scaleDragDy = ImGui::GetIO().MousePos.y - worldUiRectGizmoStartMouse.y;
                    const bool allowScaleApply = !stableRectScale ||
                                                 ((scaleDragDx * scaleDragDx + scaleDragDy * scaleDragDy) >= 9.0f);
                    const bool applyPixelSnap = pixelGridSnapEnabled;
                    const float pixelStep = static_cast<float>(std::max(1, pixelGridSnapStep));
                    auto snapScreenToPixel = [&](ImVec2 p) {
                        p.x = imageMin.x + std::round((p.x - imageMin.x) / pixelStep) * pixelStep;
                        p.y = imageMin.y + std::round((p.y - imageMin.y) / pixelStep) * pixelStep;
                        return p;
                    };
                    auto findRectSnapshot = [&](int id) -> const UiRectGizmoSnapshot* {
                        for (const UiRectGizmoSnapshot& snapshot : worldUiRectGizmoSnapshots) {
                            if (snapshot.objectId == id) return &snapshot;
                        }
                        return nullptr;
                    };
                    auto extractRectFromModel = [](const glm::mat4& rectModel, ImVec2& outCenter, ImVec2& outSize) {
                        const glm::vec4 corners[4] = {
                            glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f),
                            glm::vec4( 0.5f, -0.5f, 0.0f, 1.0f),
                            glm::vec4( 0.5f,  0.5f, 0.0f, 1.0f),
                            glm::vec4(-0.5f,  0.5f, 0.0f, 1.0f)
                        };
                        ImVec2 pts[4];
                        for (int i = 0; i < 4; ++i) {
                            const glm::vec4 p = rectModel * corners[i];
                            pts[i] = ImVec2(p.x, p.y);
                        }
                        outCenter = ImVec2((pts[0].x + pts[1].x + pts[2].x + pts[3].x) * 0.25f,
                                           (pts[0].y + pts[1].y + pts[2].y + pts[3].y) * 0.25f);
                        auto distance = [](const ImVec2& a, const ImVec2& b) {
                            const float dx = b.x - a.x;
                            const float dy = b.y - a.y;
                            return std::sqrt(dx * dx + dy * dy);
                        };
                        outSize = ImVec2(std::max(0.01f, distance(pts[0], pts[1])),
                                         std::max(0.01f, distance(pts[0], pts[3])));
                    };

                    const glm::mat4 gizmoDelta = model * glm::inverse(stableRectScale ? worldUiRectGizmoModel : originalModel);
                    const bool groupRotate = (op == ImGuizmo::ROTATE && gizmoRoots.size() > 1);

                    for (int id : gizmoRoots) {
                        SceneObject* target = findObjectById(id);
                        if (!target) continue;

                        ImVec2 targetMin, targetMax;
                        float targetRotation = target->ui.rotation;
                        if (stableRectScale) {
                            const UiRectGizmoSnapshot* snapshot = findRectSnapshot(id);
                            if (!snapshot) continue;
                            targetMin = snapshot->rectMin;
                            targetMax = snapshot->rectMax;
                            targetRotation = snapshot->rotation;
                        } else {
                            if (!resolveUiRectCached(*target, targetMin, targetMax)) continue;
                        }
                        ImVec2 targetSize(targetMax.x - targetMin.x, targetMax.y - targetMin.y);
                        if (targetSize.x <= 0.01f || targetSize.y <= 0.01f) continue;
                        ImVec2 targetCenter((targetMin.x + targetMax.x) * 0.5f - imageMin.x,
                                            (targetMin.y + targetMax.y) * 0.5f - imageMin.y);

                        glm::mat4 targetModel(1.0f);
                        targetModel = glm::translate(targetModel, glm::vec3(targetCenter.x, targetCenter.y, 0.0f));
                        targetModel = glm::rotate(targetModel, glm::radians(targetRotation), glm::vec3(0.0f, 0.0f, 1.0f));
                        targetModel = glm::scale(targetModel, glm::vec3(targetSize.x, targetSize.y, 1.0f));

                        glm::mat4 targetNewModel = gizmoDelta * targetModel;
                        glm::vec3 pos, rot, scl;
                        DecomposeMatrix(targetNewModel, pos, rot, scl);
                        glm::vec3 euler = NormalizeEulerDegrees(glm::degrees(rot));
                        ImVec2 targetNewCenter(imageMin.x + pos.x, imageMin.y + pos.y);
                        ImVec2 targetNewScreenSize(targetSize.x, targetSize.y);
                        if (stableRectScale) {
                            extractRectFromModel(targetNewModel, targetNewCenter, targetNewScreenSize);
                            targetNewCenter.x += imageMin.x;
                            targetNewCenter.y += imageMin.y;
                        }
                        if (applyPixelSnap && op == ImGuizmo::TRANSLATE) {
                            targetNewCenter = snapScreenToPixel(targetNewCenter);
                        }

                        glm::vec2 parentOffset = uiSceneLookup.getWorldParentOffset(*target);
                        glm::vec2 worldCenter = screenToWorld(targetNewCenter);

                        if (op == ImGuizmo::ROTATE) {
                            target->ui.rotation = euler.z;
                            if (groupRotate) {
                                glm::vec2 worldSize = target->ui.size;
                                ImVec2 pivotOffset = anchorToPivotUI(target->ui.anchor, ImVec2(worldSize.x, worldSize.y));
                                glm::vec2 worldMin = worldCenter - worldSize * 0.5f;
                                glm::vec2 worldPivot = worldMin + glm::vec2(pivotOffset.x, pivotOffset.y);
                                target->ui.position = worldPivot - parentOffset - parallaxOffset(*target);
                            }
                        } else if (op == ImGuizmo::TRANSLATE) {
                            glm::vec2 worldSize = target->ui.size;
                            ImVec2 pivotOffset = anchorToPivotUI(target->ui.anchor, ImVec2(worldSize.x, worldSize.y));
                            glm::vec2 worldMin = worldCenter - worldSize * 0.5f;
                            glm::vec2 worldPivot = worldMin + glm::vec2(pivotOffset.x, pivotOffset.y);
                            target->ui.position = worldPivot - parentOffset - parallaxOffset(*target);
                        } else if (op == ImGuizmo::SCALE || op == ImGuizmo::BOUNDS) {
                            if (allowScaleApply) {
                                const float minUiSize = (target->ui.type == UIElementType::Image ||
                                                         target->ui.type == UIElementType::Sprite2D)
                                    ? 0.01f
                                    : 1.0f;
                                ImVec2 newSize = stableRectScale
                                    ? ImVec2(std::max(minUiSize, targetNewScreenSize.x), std::max(minUiSize, targetNewScreenSize.y))
                                    : ImVec2(std::max(minUiSize, scl.x), std::max(minUiSize, scl.y));
                                if (stableRectScale) {
                                    constexpr float kRectScaleDeadZonePx = 0.1f;
                                    if (std::abs(newSize.x - targetSize.x) < kRectScaleDeadZonePx) newSize.x = targetSize.x;
                                    if (std::abs(newSize.y - targetSize.y) < kRectScaleDeadZonePx) newSize.y = targetSize.y;
                                }
                                if (applyPixelSnap) {
                                    newSize.x = std::max(pixelStep, std::round(newSize.x / pixelStep) * pixelStep);
                                    newSize.y = std::max(pixelStep, std::round(newSize.y / pixelStep) * pixelStep);
                                }
                                glm::vec2 worldSize = glm::vec2(newSize.x, newSize.y) / uiWorldCamera.zoom;
                                ImVec2 pivotOffset = anchorToPivotUI(target->ui.anchor, ImVec2(worldSize.x, worldSize.y));
                                glm::vec2 worldMin = worldCenter - worldSize * 0.5f;
                                glm::vec2 worldPivot = worldMin + glm::vec2(pivotOffset.x, pivotOffset.y);
                                target->ui.position = worldPivot - parentOffset - parallaxOffset(*target);
                                target->ui.size = worldSize;
                            }
                        }
                    }

                    projectManager.currentProject.hasUnsavedChanges = true;
                    gizmoUsed = true;
                } else {
                    worldUiGizmoHistoryCaptured = false;
                    worldUiRectGizmoSnapshots.clear();
                    worldUiRectGizmoModel = glm::mat4(1.0f);
                    worldUiRectGizmoStartMouse = ImVec2(0.0f, 0.0f);
                }
            }
        } else {
            worldUiGizmoHistoryCaptured = false;
            worldUiRectGizmoSnapshots.clear();
            worldUiRectGizmoModel = glm::mat4(1.0f);
            worldUiRectGizmoStartMouse = ImVec2(0.0f, 0.0f);
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();

        if ((worldUiEditing && ImGui::IsAnyItemActive()) || uiWorldCameraActive || gizmoUsed) {
            blockSelection = true;
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
        bool selectedIsUiCanvas3D = selectedObj && selectedObj->hasUI &&
                                    selectedObj->ui.type == UIElementType::Canvas &&
                                    selectedObj->ui.renderIn3D;
        if (!worldUiEditing && selectedObj && !selectedObj->hasPostFX &&
            (!HasUIComponent(*selectedObj) || selectedIsUiCanvas3D)) {
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
            bool meshComponentMode = meshModeActive &&
                                     meshEditSelectionMode != MeshEditSelectionMode::Object &&
                                     !meshEditAsset.positions.empty();

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

            if (meshComponentMode) {
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
                float edgeAlpha = ((meshEditSelectionMode == MeshEditSelectionMode::Face) ||
                                   (meshEditSelectionMode == MeshEditSelectionMode::UV)) ? 0.35f : 0.6f;
                ImU32 edgeCol = ImGui::GetColorU32(ImVec4(0.6f, 0.9f, 1.0f, edgeAlpha));
                ImU32 faceSelFillCol = ImGui::GetColorU32(ImVec4(1.0f, 0.6f, 0.2f, 0.38f));
                ImU32 hoverCol = ImGui::GetColorU32(ImVec4(1.0f, 0.95f, 0.2f, 0.95f));
                ImU32 faceHoverFillCol = ImGui::GetColorU32(ImVec4(1.0f, 0.95f, 0.2f, 0.22f));

                float selectRadius = (meshEditSelectionMode == MeshEditSelectionMode::Edge) ? 8.0f : 10.0f;
                ImVec2 mouse = ImGui::GetIO().MousePos;
                bool clicked = mouseOverViewportImage && ImGui::IsMouseClicked(0) && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver();
                bool doubleClicked = mouseOverViewportImage && ImGui::IsMouseDoubleClicked(0) && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver();
                bool additiveClick = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
                bool meshSelectionChangedThisFrame = false;

                glm::mat4 invModel = glm::inverse(modelMatrix);
                glm::mat4 invViewProj = glm::inverse(proj * view);

                auto distPointToSegment = [](const ImVec2& p, const ImVec2& a, const ImVec2& b) {
                    ImVec2 ab = ImVec2(b.x - a.x, b.y - a.y);
                    float len2 = ab.x * ab.x + ab.y * ab.y;
                    if (len2 < 1e-4f) {
                        float dx = p.x - a.x;
                        float dy = p.y - a.y;
                        return std::sqrt(dx * dx + dy * dy);
                    }
                    float t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2;
                    t = std::clamp(t, 0.0f, 1.0f);
                    ImVec2 proj = ImVec2(a.x + ab.x * t, a.y + ab.y * t);
                    float dx = p.x - proj.x;
                    float dy = p.y - proj.y;
                    return std::sqrt(dx * dx + dy * dy);
                };

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
                auto findBestEdgeAtMouse = [&](const ImVec2& mousePos) {
                    int bestEdge = -1;
                    float bestDist = selectRadius;
                    float bestDepth = FLT_MAX;
                    for (size_t ei = 0; ei < edges.size(); ++ei) {
                        const auto& e = edges[ei];
                        if (e.x >= meshEditAsset.positions.size() || e.y >= meshEditAsset.positions.size()) continue;
                        glm::vec3 a = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[e.x], 1.0f));
                        glm::vec3 b = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[e.y], 1.0f));
                        auto sa = projectToScreen(a);
                        auto sb = projectToScreen(b);
                        if (!sa || !sb) continue;
                        float dist = distPointToSegment(mousePos, *sa, *sb);
                        if (dist > selectRadius) continue;
                        glm::vec3 mid = (a + b) * 0.5f;
                        glm::vec4 clip = proj * view * glm::vec4(mid, 1.0f);
                        if (clip.w <= 1e-6f) continue;
                        float depth = clip.z / clip.w;
                        if (dist < bestDist - 0.1f ||
                            (std::abs(dist - bestDist) <= 0.1f && depth < bestDepth)) {
                            bestDist = dist;
                            bestDepth = depth;
                            bestEdge = static_cast<int>(ei);
                        }
                    }
                    return bestEdge;
                };

                float baseEdgeThickness = (meshEditSelectionMode == MeshEditSelectionMode::Edge) ? 2.2f : 1.4f;
                for (size_t ei = 0; ei < edges.size(); ++ei) {
                    const auto& e = edges[ei];
                    glm::vec3 a = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[e.x], 1.0f));
                    glm::vec3 b = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[e.y], 1.0f));
                    auto sa = projectToScreen(a);
                    auto sb = projectToScreen(b);
                    if (!sa || !sb) continue;
                    bool sel = meshEditSelectionMode == MeshEditSelectionMode::Edge &&
                               std::find(meshEditSelectedEdges.begin(), meshEditSelectedEdges.end(), (int)ei) != meshEditSelectedEdges.end();
                    float thickness = sel ? baseEdgeThickness + 1.1f : baseEdgeThickness;
                    ImU32 color = sel ? selCol : edgeCol;
                    dl->AddLine(*sa, *sb, color, thickness);
                }

                if (meshEditSelectionMode == MeshEditSelectionMode::Vertex) {
                    const size_t maxDraw = std::min<size_t>(meshEditAsset.positions.size(), 2000);
                    float bestDist = selectRadius;
                    int hoveredIndex = -1;
                    for (size_t i = 0; i < maxDraw; ++i) {
                        glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[i], 1.0f));
                        auto screen = projectToScreen(world);
                        if (!screen) continue;
                        float dx = screen->x - mouse.x;
                        float dy = screen->y - mouse.y;
                        float dist = std::sqrt(dx * dx + dy * dy);
                        if (dist < bestDist) {
                            bestDist = dist;
                            hoveredIndex = static_cast<int>(i);
                        }
                    }
                    int clickedIndex = clicked ? hoveredIndex : -1;

                    for (size_t i = 0; i < maxDraw; ++i) {
                        glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[i], 1.0f));
                        auto screen = projectToScreen(world);
                        if (!screen) continue;
                        bool sel = std::find(meshEditSelectedVertices.begin(), meshEditSelectedVertices.end(), (int)i) != meshEditSelectedVertices.end();
                        bool hover = static_cast<int>(i) == hoveredIndex && !sel;
                        float radius = sel ? 6.5f : (hover ? 6.0f : 5.0f);
                        dl->AddCircleFilled(*screen, radius, sel ? selCol : (hover ? hoverCol : vertCol));
                    }

                    if (clicked) {
                        meshSelectionChangedThisFrame = true;
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
                        } else if (!additiveClick) {
                            meshEditSelectedVertices.clear();
                        }
                        meshEditSelectedEdges.clear();
                        meshEditSelectedFaces.clear();
                    }
                } else if (meshEditSelectionMode == MeshEditSelectionMode::Edge) {
                    int hoveredIndex = mouseOverViewportImage ? findBestEdgeAtMouse(mouse) : -1;
                    int clickedIndex = clicked ? hoveredIndex : -1;

                    if (hoveredIndex >= 0 && hoveredIndex < static_cast<int>(edges.size())) {
                        const auto& e = edges[hoveredIndex];
                        glm::vec3 a = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[e.x], 1.0f));
                        glm::vec3 b = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[e.y], 1.0f));
                        auto sa = projectToScreen(a);
                        auto sb = projectToScreen(b);
                        if (sa && sb) {
                            dl->AddLine(*sa, *sb, hoverCol, baseEdgeThickness + 1.8f);
                        }
                    }

                    if (clicked) {
                        meshSelectionChangedThisFrame = true;
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
                        } else if (!additiveClick) {
                            meshEditSelectedEdges.clear();
                        }
                        meshEditSelectedVertices.clear();
                        meshEditSelectedFaces.clear();
                    }
                } else if (meshEditSelectionMode == MeshEditSelectionMode::Face ||
                           meshEditSelectionMode == MeshEditSelectionMode::UV) {
                    auto computeFaceNormal = [&](const glm::u32vec3& f, glm::vec3& out) -> bool {
                        if (f.x >= meshEditAsset.positions.size() ||
                            f.y >= meshEditAsset.positions.size() ||
                            f.z >= meshEditAsset.positions.size()) {
                            return false;
                        }
                        const glm::vec3& a = meshEditAsset.positions[f.x];
                        const glm::vec3& b = meshEditAsset.positions[f.y];
                        const glm::vec3& c = meshEditAsset.positions[f.z];
                        glm::vec3 n = glm::cross(b - a, c - a);
                        float len = glm::length(n);
                        if (len < 1e-6f) {
                            return false;
                        }
                        out = n / len;
                        return true;
                    };
                    auto gatherCoplanarFaces = [&](int seed) {
                        std::vector<int> group;
                        const size_t faceCount = meshEditAsset.faces.size();
                        if (seed < 0 || seed >= (int)faceCount) return group;
                        glm::vec3 seedNormal(0.0f);
                        if (!computeFaceNormal(meshEditAsset.faces[seed], seedNormal)) {
                            group.push_back(seed);
                            return group;
                        }

                        std::unordered_map<uint64_t, std::vector<int>> edgeToFaces;
                        edgeToFaces.reserve(faceCount * 3);
                        auto edgeKey = [](uint32_t a, uint32_t b) {
                            return (static_cast<uint64_t>(std::min(a, b)) << 32) |
                                   static_cast<uint64_t>(std::max(a, b));
                        };
                        for (size_t fi = 0; fi < faceCount; ++fi) {
                            const auto& f = meshEditAsset.faces[fi];
                            uint32_t tri[3] = { f.x, f.y, f.z };
                            for (int e = 0; e < 3; ++e) {
                                edgeToFaces[edgeKey(tri[e], tri[(e + 1) % 3])].push_back((int)fi);
                            }
                        }

                        std::vector<char> visited(faceCount, 0);
                        std::vector<int> stack;
                        visited[seed] = 1;
                        stack.push_back(seed);
                        group.push_back(seed);

                        const auto& seedFace = meshEditAsset.faces[seed];
                        glm::vec3 seedPoint = meshEditAsset.positions[seedFace.x];
                        float seedD = glm::dot(seedNormal, seedPoint);
                        const float normalThreshold = 0.995f;
                        const float planeEpsilon = 1e-3f;

                        while (!stack.empty()) {
                            int current = stack.back();
                            stack.pop_back();
                            const auto& f = meshEditAsset.faces[current];
                            uint32_t tri[3] = { f.x, f.y, f.z };
                            for (int e = 0; e < 3; ++e) {
                                auto it = edgeToFaces.find(edgeKey(tri[e], tri[(e + 1) % 3]));
                                if (it == edgeToFaces.end()) continue;
                                for (int neighbor : it->second) {
                                    if (neighbor < 0 || neighbor >= (int)faceCount) continue;
                                    if (visited[neighbor]) continue;
                                    glm::vec3 n(0.0f);
                                    if (!computeFaceNormal(meshEditAsset.faces[neighbor], n)) continue;
                                    if (glm::dot(seedNormal, n) < normalThreshold) continue;
                                    const auto& nf = meshEditAsset.faces[neighbor];
                                    const glm::vec3& na = meshEditAsset.positions[nf.x];
                                    const glm::vec3& nb = meshEditAsset.positions[nf.y];
                                    const glm::vec3& nc = meshEditAsset.positions[nf.z];
                                    if (std::abs(glm::dot(seedNormal, na) - seedD) > planeEpsilon ||
                                        std::abs(glm::dot(seedNormal, nb) - seedD) > planeEpsilon ||
                                        std::abs(glm::dot(seedNormal, nc) - seedD) > planeEpsilon) {
                                        continue;
                                    }
                                    visited[neighbor] = 1;
                                    stack.push_back(neighbor);
                                    group.push_back(neighbor);
                                }
                            }
                        }
                        std::sort(group.begin(), group.end());
                        group.erase(std::unique(group.begin(), group.end()), group.end());
                        return group;
                    };
                    auto gatherFaceGroup = [&](int seed) {
                        std::vector<int> group;
                        const size_t faceCount = meshEditAsset.faces.size();
                        if (seed < 0 || seed >= (int)faceCount) return group;
                        group.push_back(seed);
                        if (meshEditTriangleSelection) {
                            return group;
                        }

                        glm::vec3 seedNormal(0.0f);
                        if (!computeFaceNormal(meshEditAsset.faces[seed], seedNormal)) {
                            return group;
                        }
                        const auto& seedFace = meshEditAsset.faces[seed];
                        const glm::vec3 seedPoint = meshEditAsset.positions[seedFace.x];
                        const float seedPlaneD = glm::dot(seedNormal, seedPoint);
                        const float positionEps2 = 1e-10f;
                        const float planeEps = 1e-3f;
                        auto sharePosition = [&](uint32_t a, uint32_t b) -> bool {
                            if (a >= meshEditAsset.positions.size() || b >= meshEditAsset.positions.size()) {
                                return false;
                            }
                            glm::vec3 d = meshEditAsset.positions[a] - meshEditAsset.positions[b];
                            return glm::dot(d, d) <= positionEps2;
                        };
                        auto edgeSharedLength = [&](const glm::u32vec3& a, const glm::u32vec3& b) {
                            uint32_t aTri[3] = { a.x, a.y, a.z };
                            uint32_t bTri[3] = { b.x, b.y, b.z };
                            float bestLen = 0.0f;
                            for (int ea = 0; ea < 3; ++ea) {
                                uint32_t a0 = aTri[ea];
                                uint32_t a1 = aTri[(ea + 1) % 3];
                                for (int eb = 0; eb < 3; ++eb) {
                                    uint32_t b0 = bTri[eb];
                                    uint32_t b1 = bTri[(eb + 1) % 3];
                                    bool sameDir = (a0 == b0 && a1 == b1) || (a0 == b1 && a1 == b0);
                                    bool samePos =
                                        (sharePosition(a0, b0) && sharePosition(a1, b1)) ||
                                        (sharePosition(a0, b1) && sharePosition(a1, b0));
                                    if (!sameDir && !samePos) continue;
                                    glm::vec3 p0 = meshEditAsset.positions[a0];
                                    glm::vec3 p1 = meshEditAsset.positions[a1];
                                    bestLen = std::max(bestLen, glm::length(p1 - p0));
                                }
                            }
                            return bestLen;
                        };

                        int bestNeighbor = -1;
                        float bestScore = -FLT_MAX;

                        for (size_t fi = 0; fi < faceCount; ++fi) {
                            if ((int)fi == seed) continue;
                            const auto& f = meshEditAsset.faces[fi];
                            glm::vec3 n(0.0f);
                            if (!computeFaceNormal(f, n)) continue;
                            const float align = std::abs(glm::dot(seedNormal, n));
                            if (align < 0.995f) continue;
                            if (std::abs(glm::dot(seedNormal, meshEditAsset.positions[f.x]) - seedPlaneD) > planeEps ||
                                std::abs(glm::dot(seedNormal, meshEditAsset.positions[f.y]) - seedPlaneD) > planeEps ||
                                std::abs(glm::dot(seedNormal, meshEditAsset.positions[f.z]) - seedPlaneD) > planeEps) {
                                continue;
                            }
                            float sharedLen = edgeSharedLength(seedFace, f);
                            if (sharedLen <= 1e-6f) continue;
                            float score = sharedLen * 1000.0f + align;
                            if (score > bestScore) {
                                bestScore = score;
                                bestNeighbor = static_cast<int>(fi);
                            }
                        }

                        if (bestNeighbor >= 0) {
                            group.push_back(bestNeighbor);
                        }
                        if (group.size() > 1) {
                            std::sort(group.begin(), group.end());
                            group.erase(std::unique(group.begin(), group.end()), group.end());
                        }
                        return group;
                    };

                    for (int fi : meshEditSelectedFaces) {
                        if (fi < 0 || fi >= (int)meshEditAsset.faces.size()) continue;
                        const auto& f = meshEditAsset.faces[fi];
                        glm::vec3 a = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[f.x], 1.0f));
                        glm::vec3 b = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[f.y], 1.0f));
                        glm::vec3 c = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[f.z], 1.0f));
                        auto sa = projectToScreen(a);
                        auto sb = projectToScreen(b);
                        auto sc = projectToScreen(c);
                        if (!sa || !sb || !sc) continue;
                        dl->AddTriangleFilled(*sa, *sb, *sc, faceSelFillCol);
                        dl->AddTriangle(*sa, *sb, *sc, selCol, 2.0f);
                    }

                    int hoveredFaceIndex = -1;
                    if (mouseOverViewportImage) {
                        auto ray = makeRay(mouse);
                        glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(ray.first, 1.0f));
                        glm::vec3 localDir = glm::normalize(glm::vec3(invModel * glm::vec4(ray.second, 0.0f)));
                        float bestT = FLT_MAX;
                        for (size_t fi = 0; fi < meshEditAsset.faces.size(); ++fi) {
                            const auto& f = meshEditAsset.faces[fi];
                            if (f.x >= meshEditAsset.positions.size() || f.y >= meshEditAsset.positions.size() || f.z >= meshEditAsset.positions.size()) continue;
                            float tHit = 0.0f;
                            if (rayTriangle(localOrigin, localDir,
                                            meshEditAsset.positions[f.x],
                                            meshEditAsset.positions[f.y],
                                            meshEditAsset.positions[f.z],
                                            tHit)) {
                                if (tHit < bestT) {
                                    bestT = tHit;
                                    hoveredFaceIndex = static_cast<int>(fi);
                                }
                            }
                        }
                    }
                    std::vector<int> hoverGroup = (hoveredFaceIndex >= 0) ? gatherFaceGroup(hoveredFaceIndex) : std::vector<int>{};
                    for (int fi : hoverGroup) {
                        if (fi < 0 || fi >= static_cast<int>(meshEditAsset.faces.size())) continue;
                        if (std::find(meshEditSelectedFaces.begin(), meshEditSelectedFaces.end(), fi) != meshEditSelectedFaces.end()) continue;
                        const auto& f = meshEditAsset.faces[fi];
                        glm::vec3 a = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[f.x], 1.0f));
                        glm::vec3 b = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[f.y], 1.0f));
                        glm::vec3 c = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[f.z], 1.0f));
                        auto sa = projectToScreen(a);
                        auto sb = projectToScreen(b);
                        auto sc = projectToScreen(c);
                        if (!sa || !sb || !sc) continue;
                        dl->AddTriangleFilled(*sa, *sb, *sc, faceHoverFillCol);
                        dl->AddTriangle(*sa, *sb, *sc, hoverCol, 1.6f);
                    }

                    if (clicked || doubleClicked) {
                        meshSelectionChangedThisFrame = true;
                        int clickedIndex = hoveredFaceIndex;
                        if (clickedIndex >= 0) {
                            std::vector<int> group = gatherFaceGroup(clickedIndex);
                            if (group.empty()) group.push_back(clickedIndex);
                            if (additiveClick) {
                                bool allSelected = true;
                                for (int fi : group) {
                                    if (std::find(meshEditSelectedFaces.begin(), meshEditSelectedFaces.end(), fi) == meshEditSelectedFaces.end()) {
                                        allSelected = false;
                                        break;
                                    }
                                }
                                if (allSelected) {
                                    for (int fi : group) {
                                        auto itSel = std::find(meshEditSelectedFaces.begin(), meshEditSelectedFaces.end(), fi);
                                        if (itSel != meshEditSelectedFaces.end()) {
                                            meshEditSelectedFaces.erase(itSel);
                                        }
                                    }
                                } else {
                                    for (int fi : group) {
                                        if (std::find(meshEditSelectedFaces.begin(), meshEditSelectedFaces.end(), fi) == meshEditSelectedFaces.end()) {
                                            meshEditSelectedFaces.push_back(fi);
                                        }
                                    }
                                }
                            } else {
                                meshEditSelectedFaces.clear();
                                meshEditSelectedFaces = std::move(group);
                            }
                        } else if (!additiveClick) {
                            meshEditSelectedFaces.clear();
                        }
                        meshEditSelectedVertices.clear();
                        meshEditSelectedEdges.clear();
                    }
                }

                // Compute affected vertices from selection
                std::vector<int> baseAffectedVerts;
                if (meshEditSelectionMode == MeshEditSelectionMode::Vertex) {
                    baseAffectedVerts = meshEditSelectedVertices;
                }
                auto pushUnique = [&](int idx) {
                    if (idx < 0) return;
                    if (std::find(baseAffectedVerts.begin(), baseAffectedVerts.end(), idx) == baseAffectedVerts.end()) {
                        baseAffectedVerts.push_back(idx);
                    }
                };
                if (meshEditSelectionMode == MeshEditSelectionMode::Edge) {
                    for (int ei : meshEditSelectedEdges) {
                        if (ei < 0 || ei >= (int)edges.size()) continue;
                        pushUnique(edges[ei].x);
                        pushUnique(edges[ei].y);
                    }
                } else if (meshEditSelectionMode == MeshEditSelectionMode::Face ||
                           meshEditSelectionMode == MeshEditSelectionMode::UV) {
                    for (int fi : meshEditSelectedFaces) {
                        if (fi < 0 || fi >= (int)meshEditAsset.faces.size()) continue;
                        const auto& f = meshEditAsset.faces[fi];
                        pushUnique(f.x);
                        pushUnique(f.y);
                        pushUnique(f.z);
                    }
                    // RMesh primitives often duplicate seam vertices per face. Move all coincident
                    // vertices together so face translation keeps adjacent geometry connected.
                    constexpr float coincidentEps2 = 1e-10f;
                    std::vector<int> seamSeeds = baseAffectedVerts;
                    for (int seedIdx : seamSeeds) {
                        if (seedIdx < 0 || seedIdx >= static_cast<int>(meshEditAsset.positions.size())) continue;
                        const glm::vec3 seedPos = meshEditAsset.positions[seedIdx];
                        for (size_t vi = 0; vi < meshEditAsset.positions.size(); ++vi) {
                            const glm::vec3 delta = meshEditAsset.positions[vi] - seedPos;
                            if (glm::dot(delta, delta) <= coincidentEps2) {
                                pushUnique(static_cast<int>(vi));
                            }
                        }
                    }
                }
                auto recalcMesh = [&]() {
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
                    if (meshEditAsset.materialSlots.empty()) {
                        meshEditAsset.materialSlots.push_back("Default");
                    }
                    if (meshEditAsset.faceMaterialIndices.size() != meshEditAsset.faces.size()) {
                        meshEditAsset.faceMaterialIndices.resize(
                            meshEditAsset.faces.size(),
                            static_cast<uint32_t>(meshEditActiveMaterialSlot));
                    }
                };

                auto ensureFaceMaterials = [&]() {
                    if (meshEditAsset.materialSlots.empty()) {
                        meshEditAsset.materialSlots.push_back("Default");
                    }
                    meshEditActiveMaterialSlot = std::clamp(
                        meshEditActiveMaterialSlot,
                        0,
                        static_cast<int>(meshEditAsset.materialSlots.size()) - 1);
                    if (meshEditAsset.faceMaterialIndices.size() != meshEditAsset.faces.size()) {
                        meshEditAsset.faceMaterialIndices.resize(
                            meshEditAsset.faces.size(),
                            static_cast<uint32_t>(meshEditActiveMaterialSlot));
                    }
                    const uint32_t maxMat = static_cast<uint32_t>(meshEditAsset.materialSlots.size() - 1);
                    for (auto& idx : meshEditAsset.faceMaterialIndices) {
                        idx = std::min(idx, maxMat);
                    }
                };

                auto ensureUvs = [&]() {
                    if (meshEditAsset.uvs.size() < meshEditAsset.positions.size()) {
                        meshEditAsset.uvs.resize(meshEditAsset.positions.size(), glm::vec2(0.0f));
                    }
                };

                auto applyPlanarUvToFace = [&](int faceIndex) -> bool {
                    if (faceIndex < 0 || faceIndex >= static_cast<int>(meshEditAsset.faces.size())) {
                        return false;
                    }
                    ensureUvs();
                    const auto& face = meshEditAsset.faces[faceIndex];
                    if (face.x >= meshEditAsset.positions.size() ||
                        face.y >= meshEditAsset.positions.size() ||
                        face.z >= meshEditAsset.positions.size()) {
                        return false;
                    }

                    const glm::vec3& a = meshEditAsset.positions[face.x];
                    const glm::vec3& b = meshEditAsset.positions[face.y];
                    const glm::vec3& c = meshEditAsset.positions[face.z];
                    glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
                    if (glm::length(n) < 1e-6f) {
                        n = glm::vec3(0.0f, 0.0f, 1.0f);
                    }

                    glm::vec2 ua(a.x, a.y), ub(b.x, b.y), uc(c.x, c.y);
                    if (std::abs(n.x) >= std::abs(n.y) && std::abs(n.x) >= std::abs(n.z)) {
                        ua = glm::vec2(a.y, a.z);
                        ub = glm::vec2(b.y, b.z);
                        uc = glm::vec2(c.y, c.z);
                    } else if (std::abs(n.y) >= std::abs(n.z)) {
                        ua = glm::vec2(a.x, a.z);
                        ub = glm::vec2(b.x, b.z);
                        uc = glm::vec2(c.x, c.z);
                    }

                    glm::vec2 minUV = glm::min(glm::min(ua, ub), uc);
                    glm::vec2 maxUV = glm::max(glm::max(ua, ub), uc);
                    glm::vec2 span = maxUV - minUV;
                    auto mapUv = [&](const glm::vec2& v) {
                        return glm::vec2(
                            span.x > 1e-6f ? (v.x - minUV.x) / span.x : 0.0f,
                            span.y > 1e-6f ? (v.y - minUV.y) / span.y : 0.0f);
                    };
                    meshEditAsset.uvs[face.x] = mapUv(ua);
                    meshEditAsset.uvs[face.y] = mapUv(ub);
                    meshEditAsset.uvs[face.z] = mapUv(uc);
                    meshEditAsset.hasUVs = true;
                    return true;
                };

                auto compactMesh = [&]() {
                    std::vector<glm::u32vec3> compactFaces;
                    std::vector<uint32_t> compactMaterials;
                    compactFaces.reserve(meshEditAsset.faces.size());
                    compactMaterials.reserve(meshEditAsset.faces.size());

                    std::vector<uint8_t> used(meshEditAsset.positions.size(), 0u);
                    for (size_t fi = 0; fi < meshEditAsset.faces.size(); ++fi) {
                        const auto& f = meshEditAsset.faces[fi];
                        if (f.x >= meshEditAsset.positions.size() ||
                            f.y >= meshEditAsset.positions.size() ||
                            f.z >= meshEditAsset.positions.size() ||
                            f.x == f.y || f.y == f.z || f.z == f.x) {
                            continue;
                        }
                        compactFaces.push_back(f);
                        uint32_t mat = 0u;
                        if (fi < meshEditAsset.faceMaterialIndices.size()) {
                            mat = meshEditAsset.faceMaterialIndices[fi];
                        }
                        compactMaterials.push_back(mat);
                        used[f.x] = 1u;
                        used[f.y] = 1u;
                        used[f.z] = 1u;
                    }

                    std::vector<uint32_t> remap(meshEditAsset.positions.size(), UINT32_MAX);
                    std::vector<glm::vec3> newPositions;
                    std::vector<glm::vec3> newNormals;
                    std::vector<glm::vec2> newUvs;
                    newPositions.reserve(meshEditAsset.positions.size());
                    newNormals.reserve(meshEditAsset.positions.size());
                    newUvs.reserve(meshEditAsset.positions.size());

                    for (size_t i = 0; i < meshEditAsset.positions.size(); ++i) {
                        if (!used[i]) continue;
                        remap[i] = static_cast<uint32_t>(newPositions.size());
                        newPositions.push_back(meshEditAsset.positions[i]);
                        if (i < meshEditAsset.normals.size()) newNormals.push_back(meshEditAsset.normals[i]);
                        else newNormals.push_back(glm::vec3(0.0f));
                        if (i < meshEditAsset.uvs.size()) newUvs.push_back(meshEditAsset.uvs[i]);
                        else newUvs.push_back(glm::vec2(0.0f));
                    }

                    for (auto& f : compactFaces) {
                        f.x = remap[f.x];
                        f.y = remap[f.y];
                        f.z = remap[f.z];
                    }

                    meshEditAsset.positions = std::move(newPositions);
                    meshEditAsset.normals = std::move(newNormals);
                    meshEditAsset.uvs = std::move(newUvs);
                    meshEditAsset.faces = std::move(compactFaces);
                    meshEditAsset.faceMaterialIndices = std::move(compactMaterials);

                    auto remapSelection = [&](std::vector<int>& selection) {
                        std::vector<int> newSel;
                        newSel.reserve(selection.size());
                        for (int idx : selection) {
                            if (idx < 0 || idx >= static_cast<int>(remap.size())) continue;
                            uint32_t mapped = remap[idx];
                            if (mapped == UINT32_MAX) continue;
                            int mappedInt = static_cast<int>(mapped);
                            if (std::find(newSel.begin(), newSel.end(), mappedInt) == newSel.end()) {
                                newSel.push_back(mappedInt);
                            }
                        }
                        selection = std::move(newSel);
                    };
                    remapSelection(meshEditSelectedVertices);
                };

                auto commitMeshEdit = [&](const char* actionName) {
                    compactMesh();
                    ensureFaceMaterials();
                    recalcMesh();
                    if (meshEditAutoUV) {
                        if (!meshEditSelectedFaces.empty()) {
                            for (int fi : meshEditSelectedFaces) {
                                applyPlanarUvToFace(fi);
                            }
                        } else {
                            for (int fi = 0; fi < static_cast<int>(meshEditAsset.faces.size()); ++fi) {
                                applyPlanarUvToFace(fi);
                            }
                        }
                    }
                    meshEditDirty = true;
                    if (selectedObj) {
                        syncMeshEditToGPU(selectedObj);
                    }
                    if (actionName && *actionName) {
                        addConsoleMessage(std::string("Mesh edit: ") + actionName, ConsoleMessageType::Info);
                    }
                };

                auto addFaceWithMaterial = [&](const glm::u32vec3& tri, uint32_t matIdx) {
                    meshEditAsset.faces.push_back(tri);
                    meshEditAsset.faceMaterialIndices.push_back(matIdx);
                };

                static bool meshEditContextRightClickLatched = false;
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    meshEditContextRightClickLatched = false;
                }
                if (mouseOverViewportImage &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
                    !meshEditContextRightClickLatched &&
                    !ImGuizmo::IsUsing() && !ImGuizmo::IsOver()) {
                    ImGui::OpenPopup("##mesh_edit_context_menu");
                    meshEditContextRightClickLatched = true;
                }

                if (ImGui::BeginPopup("##mesh_edit_context_menu")) {
                    ensureFaceMaterials();

                    auto selectedFaceMaterial = [&]() -> uint32_t {
                        if (!meshEditSelectedFaces.empty()) {
                            int fi = meshEditSelectedFaces.front();
                            if (fi >= 0 && fi < static_cast<int>(meshEditAsset.faceMaterialIndices.size())) {
                                return meshEditAsset.faceMaterialIndices[fi];
                            }
                        }
                        return static_cast<uint32_t>(meshEditActiveMaterialSlot);
                    };

                    auto computeFaceNormal = [&](const glm::u32vec3& f, glm::vec3& out) -> bool {
                        if (f.x >= meshEditAsset.positions.size() ||
                            f.y >= meshEditAsset.positions.size() ||
                            f.z >= meshEditAsset.positions.size()) {
                            return false;
                        }
                        glm::vec3 n = glm::cross(meshEditAsset.positions[f.y] - meshEditAsset.positions[f.x],
                                                 meshEditAsset.positions[f.z] - meshEditAsset.positions[f.x]);
                        if (glm::length(n) < 1e-6f) return false;
                        out = glm::normalize(n);
                        return true;
                    };

                    auto deleteSelectedFaces = [&]() {
                        if (meshEditSelectedFaces.empty()) return false;
                        std::vector<uint8_t> remove(meshEditAsset.faces.size(), 0u);
                        for (int fi : meshEditSelectedFaces) {
                            if (fi >= 0 && fi < static_cast<int>(remove.size())) remove[fi] = 1u;
                        }
                        std::vector<glm::u32vec3> nextFaces;
                        std::vector<uint32_t> nextMats;
                        nextFaces.reserve(meshEditAsset.faces.size());
                        nextMats.reserve(meshEditAsset.faces.size());
                        for (size_t fi = 0; fi < meshEditAsset.faces.size(); ++fi) {
                            if (remove[fi]) continue;
                            nextFaces.push_back(meshEditAsset.faces[fi]);
                            uint32_t mat = (fi < meshEditAsset.faceMaterialIndices.size()) ? meshEditAsset.faceMaterialIndices[fi] : 0u;
                            nextMats.push_back(mat);
                        }
                        meshEditAsset.faces = std::move(nextFaces);
                        meshEditAsset.faceMaterialIndices = std::move(nextMats);
                        meshEditSelectedFaces.clear();
                        return true;
                    };

                    auto extrudeSelectedFaces = [&](float amount, bool branchFromEdges) {
                        if (meshEditSelectedFaces.empty()) return false;
                        ensureUvs();
                        std::unordered_map<uint32_t, uint32_t> duplicateMap;
                        std::unordered_map<uint32_t, glm::vec3> moveDirs;
                        duplicateMap.reserve(meshEditSelectedFaces.size() * 3);

                        std::vector<glm::u32vec3> selectedFaces;
                        selectedFaces.reserve(meshEditSelectedFaces.size());
                        for (int fi : meshEditSelectedFaces) {
                            if (fi >= 0 && fi < static_cast<int>(meshEditAsset.faces.size())) {
                                selectedFaces.push_back(meshEditAsset.faces[fi]);
                            }
                        }
                        if (selectedFaces.empty()) return false;

                        glm::vec3 regionCenter(0.0f);
                        int regionVertCount = 0;
                        for (const auto& f : selectedFaces) {
                            regionCenter += meshEditAsset.positions[f.x];
                            regionCenter += meshEditAsset.positions[f.y];
                            regionCenter += meshEditAsset.positions[f.z];
                            regionVertCount += 3;
                        }
                        regionCenter /= std::max(1, regionVertCount);

                        auto duplicateVertex = [&](uint32_t idx) -> uint32_t {
                            auto it = duplicateMap.find(idx);
                            if (it != duplicateMap.end()) return it->second;
                            uint32_t newIdx = static_cast<uint32_t>(meshEditAsset.positions.size());
                            duplicateMap[idx] = newIdx;
                            meshEditAsset.positions.push_back(meshEditAsset.positions[idx]);
                            meshEditAsset.normals.push_back(idx < meshEditAsset.normals.size() ? meshEditAsset.normals[idx] : glm::vec3(0.0f));
                            meshEditAsset.uvs.push_back(idx < meshEditAsset.uvs.size() ? meshEditAsset.uvs[idx] : glm::vec2(0.0f));
                            return newIdx;
                        };

                        auto buildDirForVertex = [&](uint32_t oldIdx, const glm::vec3& faceNormal) {
                            glm::vec3 dir = faceNormal;
                            if (branchFromEdges) {
                                glm::vec3 radial = meshEditAsset.positions[oldIdx] - regionCenter;
                                if (glm::length(radial) > 1e-6f) {
                                    dir += glm::normalize(radial) * 0.35f;
                                }
                            }
                            if (glm::length(dir) < 1e-6f) dir = glm::vec3(0.0f, 0.0f, 1.0f);
                            return glm::normalize(dir);
                        };

                        uint32_t defaultMat = selectedFaceMaterial();
                        for (const auto& f : selectedFaces) {
                            glm::vec3 n(0.0f);
                            computeFaceNormal(f, n);
                            const uint32_t oldIdx[3] = { f.x, f.y, f.z };
                            uint32_t newIdx[3];
                            for (int i = 0; i < 3; ++i) {
                                newIdx[i] = duplicateVertex(oldIdx[i]);
                                moveDirs[newIdx[i]] += buildDirForVertex(oldIdx[i], n);
                            }

                            addFaceWithMaterial(glm::u32vec3(newIdx[0], newIdx[1], newIdx[2]), defaultMat);
                            addFaceWithMaterial(glm::u32vec3(oldIdx[0], oldIdx[1], newIdx[1]), defaultMat);
                            addFaceWithMaterial(glm::u32vec3(oldIdx[0], newIdx[1], newIdx[0]), defaultMat);
                            addFaceWithMaterial(glm::u32vec3(oldIdx[1], oldIdx[2], newIdx[2]), defaultMat);
                            addFaceWithMaterial(glm::u32vec3(oldIdx[1], newIdx[2], newIdx[1]), defaultMat);
                            addFaceWithMaterial(glm::u32vec3(oldIdx[2], oldIdx[0], newIdx[0]), defaultMat);
                            addFaceWithMaterial(glm::u32vec3(oldIdx[2], newIdx[0], newIdx[2]), defaultMat);
                        }

                        for (const auto& it : moveDirs) {
                            uint32_t idx = it.first;
                            glm::vec3 dir = it.second;
                            if (glm::length(dir) < 1e-6f) continue;
                            meshEditAsset.positions[idx] += glm::normalize(dir) * amount;
                        }

                        return true;
                    };

                    auto subdivideSelectedFaces = [&]() {
                        if (meshEditSelectedFaces.empty()) return false;
                        ensureUvs();
                        std::unordered_map<uint64_t, uint32_t> edgeMidpoints;
                        edgeMidpoints.reserve(meshEditSelectedFaces.size() * 3);
                        auto edgeKey = [](uint32_t a, uint32_t b) -> uint64_t {
                            return (static_cast<uint64_t>(std::min(a, b)) << 32) | static_cast<uint64_t>(std::max(a, b));
                        };

                        std::vector<int> selected = meshEditSelectedFaces;
                        std::sort(selected.begin(), selected.end());
                        std::reverse(selected.begin(), selected.end());

                        for (int fi : selected) {
                            if (fi < 0 || fi >= static_cast<int>(meshEditAsset.faces.size())) continue;
                            const auto f = meshEditAsset.faces[fi];
                            uint32_t mat = (fi < static_cast<int>(meshEditAsset.faceMaterialIndices.size()))
                                ? meshEditAsset.faceMaterialIndices[fi]
                                : static_cast<uint32_t>(meshEditActiveMaterialSlot);

                            auto midpoint = [&](uint32_t a, uint32_t b) -> uint32_t {
                                uint64_t key = edgeKey(a, b);
                                auto it = edgeMidpoints.find(key);
                                if (it != edgeMidpoints.end()) return it->second;
                                uint32_t idx = static_cast<uint32_t>(meshEditAsset.positions.size());
                                meshEditAsset.positions.push_back((meshEditAsset.positions[a] + meshEditAsset.positions[b]) * 0.5f);
                                meshEditAsset.normals.push_back((meshEditAsset.normals[a] + meshEditAsset.normals[b]) * 0.5f);
                                meshEditAsset.uvs.push_back((meshEditAsset.uvs[a] + meshEditAsset.uvs[b]) * 0.5f);
                                edgeMidpoints[key] = idx;
                                return idx;
                            };

                            uint32_t ab = midpoint(f.x, f.y);
                            uint32_t bc = midpoint(f.y, f.z);
                            uint32_t ca = midpoint(f.z, f.x);

                            meshEditAsset.faces.erase(meshEditAsset.faces.begin() + fi);
                            if (fi < static_cast<int>(meshEditAsset.faceMaterialIndices.size())) {
                                meshEditAsset.faceMaterialIndices.erase(meshEditAsset.faceMaterialIndices.begin() + fi);
                            }

                            addFaceWithMaterial(glm::u32vec3(f.x, ab, ca), mat);
                            addFaceWithMaterial(glm::u32vec3(ab, f.y, bc), mat);
                            addFaceWithMaterial(glm::u32vec3(ca, bc, f.z), mat);
                            addFaceWithMaterial(glm::u32vec3(ab, bc, ca), mat);
                        }
                        return true;
                    };

                    auto insetSelectedFaces = [&]() {
                        if (meshEditSelectedFaces.empty()) return false;
                        ensureUvs();
                        std::vector<int> selected = meshEditSelectedFaces;
                        std::sort(selected.begin(), selected.end());
                        std::reverse(selected.begin(), selected.end());
                        const float t = std::clamp(meshEditInsetAmount, 0.01f, 0.95f);

                        for (int fi : selected) {
                            if (fi < 0 || fi >= static_cast<int>(meshEditAsset.faces.size())) continue;
                            const auto f = meshEditAsset.faces[fi];
                            uint32_t mat = (fi < static_cast<int>(meshEditAsset.faceMaterialIndices.size()))
                                ? meshEditAsset.faceMaterialIndices[fi]
                                : static_cast<uint32_t>(meshEditActiveMaterialSlot);

                            glm::vec3 center = (meshEditAsset.positions[f.x] + meshEditAsset.positions[f.y] + meshEditAsset.positions[f.z]) / 3.0f;
                            glm::vec2 uvCenter = (meshEditAsset.uvs[f.x] + meshEditAsset.uvs[f.y] + meshEditAsset.uvs[f.z]) / 3.0f;

                            uint32_t ia = static_cast<uint32_t>(meshEditAsset.positions.size());
                            uint32_t ib = ia + 1u;
                            uint32_t ic = ia + 2u;
                            meshEditAsset.positions.push_back(glm::mix(meshEditAsset.positions[f.x], center, t));
                            meshEditAsset.positions.push_back(glm::mix(meshEditAsset.positions[f.y], center, t));
                            meshEditAsset.positions.push_back(glm::mix(meshEditAsset.positions[f.z], center, t));
                            meshEditAsset.normals.push_back(meshEditAsset.normals[f.x]);
                            meshEditAsset.normals.push_back(meshEditAsset.normals[f.y]);
                            meshEditAsset.normals.push_back(meshEditAsset.normals[f.z]);
                            meshEditAsset.uvs.push_back(glm::mix(meshEditAsset.uvs[f.x], uvCenter, t));
                            meshEditAsset.uvs.push_back(glm::mix(meshEditAsset.uvs[f.y], uvCenter, t));
                            meshEditAsset.uvs.push_back(glm::mix(meshEditAsset.uvs[f.z], uvCenter, t));

                            meshEditAsset.faces.erase(meshEditAsset.faces.begin() + fi);
                            if (fi < static_cast<int>(meshEditAsset.faceMaterialIndices.size())) {
                                meshEditAsset.faceMaterialIndices.erase(meshEditAsset.faceMaterialIndices.begin() + fi);
                            }

                            addFaceWithMaterial(glm::u32vec3(ia, ib, ic), mat);
                            addFaceWithMaterial(glm::u32vec3(f.x, f.y, ib), mat);
                            addFaceWithMaterial(glm::u32vec3(f.x, ib, ia), mat);
                            addFaceWithMaterial(glm::u32vec3(f.y, f.z, ic), mat);
                            addFaceWithMaterial(glm::u32vec3(f.y, ic, ib), mat);
                            addFaceWithMaterial(glm::u32vec3(f.z, f.x, ia), mat);
                            addFaceWithMaterial(glm::u32vec3(f.z, ia, ic), mat);
                        }
                        return true;
                    };

                    auto separateSelectedFaces = [&]() {
                        if (meshEditSelectedFaces.empty()) return false;
                        ensureUvs();
                        for (int fi : meshEditSelectedFaces) {
                            if (fi < 0 || fi >= static_cast<int>(meshEditAsset.faces.size())) continue;
                            auto& f = meshEditAsset.faces[fi];
                            uint32_t ids[3] = { f.x, f.y, f.z };
                            uint32_t outIdx[3];
                            for (int i = 0; i < 3; ++i) {
                                outIdx[i] = static_cast<uint32_t>(meshEditAsset.positions.size());
                                meshEditAsset.positions.push_back(meshEditAsset.positions[ids[i]]);
                                meshEditAsset.normals.push_back(meshEditAsset.normals[ids[i]]);
                                meshEditAsset.uvs.push_back(meshEditAsset.uvs[ids[i]]);
                            }
                            f = glm::u32vec3(outIdx[0], outIdx[1], outIdx[2]);
                        }
                        return true;
                    };

                    auto flipSelectedFaces = [&]() {
                        if (meshEditSelectedFaces.empty()) return false;
                        for (int fi : meshEditSelectedFaces) {
                            if (fi < 0 || fi >= static_cast<int>(meshEditAsset.faces.size())) continue;
                            std::swap(meshEditAsset.faces[fi].y, meshEditAsset.faces[fi].z);
                        }
                        return true;
                    };

                    auto deleteSelectedVertices = [&]() {
                        if (meshEditSelectedVertices.empty()) return false;
                        std::unordered_set<uint32_t> removeSet;
                        for (int vi : meshEditSelectedVertices) {
                            if (vi >= 0 && vi < static_cast<int>(meshEditAsset.positions.size())) {
                                removeSet.insert(static_cast<uint32_t>(vi));
                            }
                        }
                        if (removeSet.empty()) return false;
                        std::vector<glm::u32vec3> nextFaces;
                        std::vector<uint32_t> nextMats;
                        for (size_t fi = 0; fi < meshEditAsset.faces.size(); ++fi) {
                            const auto& f = meshEditAsset.faces[fi];
                            if (removeSet.count(f.x) || removeSet.count(f.y) || removeSet.count(f.z)) continue;
                            nextFaces.push_back(f);
                            uint32_t mat = (fi < meshEditAsset.faceMaterialIndices.size()) ? meshEditAsset.faceMaterialIndices[fi] : 0u;
                            nextMats.push_back(mat);
                        }
                        meshEditAsset.faces = std::move(nextFaces);
                        meshEditAsset.faceMaterialIndices = std::move(nextMats);
                        meshEditSelectedVertices.clear();
                        return true;
                    };

                    auto snapSelectedVertices = [&]() {
                        if (meshEditSelectedVertices.empty()) return false;
                        float step = std::max(0.001f, meshEditGridSnap);
                        for (int vi : meshEditSelectedVertices) {
                            if (vi < 0 || vi >= static_cast<int>(meshEditAsset.positions.size())) continue;
                            glm::vec3& p = meshEditAsset.positions[vi];
                            p.x = std::round(p.x / step) * step;
                            p.y = std::round(p.y / step) * step;
                            p.z = std::round(p.z / step) * step;
                        }
                        return true;
                    };

                    auto relaxSelectedVertices = [&]() {
                        if (meshEditSelectedVertices.empty()) return false;
                        std::unordered_map<int, std::vector<int>> neighbors;
                        neighbors.reserve(meshEditSelectedVertices.size());
                        for (const auto& f : meshEditAsset.faces) {
                            int tri[3] = { static_cast<int>(f.x), static_cast<int>(f.y), static_cast<int>(f.z) };
                            for (int i = 0; i < 3; ++i) {
                                neighbors[tri[i]].push_back(tri[(i + 1) % 3]);
                                neighbors[tri[i]].push_back(tri[(i + 2) % 3]);
                            }
                        }
                        std::unordered_map<int, glm::vec3> nextPositions;
                        for (int vi : meshEditSelectedVertices) {
                            auto it = neighbors.find(vi);
                            if (it == neighbors.end() || it->second.empty()) continue;
                            glm::vec3 avg(0.0f);
                            int count = 0;
                            for (int n : it->second) {
                                if (n < 0 || n >= static_cast<int>(meshEditAsset.positions.size())) continue;
                                avg += meshEditAsset.positions[n];
                                ++count;
                            }
                            if (count > 0) {
                                nextPositions[vi] = avg / static_cast<float>(count);
                            }
                        }
                        if (nextPositions.empty()) return false;
                        for (const auto& kv : nextPositions) {
                            meshEditAsset.positions[kv.first] = kv.second;
                        }
                        return true;
                    };

                    auto weldSelectedVertices = [&]() {
                        if (meshEditSelectedVertices.size() < 2) return false;
                        glm::vec3 center(0.0f);
                        int valid = 0;
                        for (int vi : meshEditSelectedVertices) {
                            if (vi < 0 || vi >= static_cast<int>(meshEditAsset.positions.size())) continue;
                            center += meshEditAsset.positions[vi];
                            ++valid;
                        }
                        if (valid < 2) return false;
                        center /= static_cast<float>(valid);
                        int keep = meshEditSelectedVertices.front();
                        if (keep < 0 || keep >= static_cast<int>(meshEditAsset.positions.size())) return false;
                        meshEditAsset.positions[keep] = center;
                        std::unordered_set<int> mergeSet(meshEditSelectedVertices.begin(), meshEditSelectedVertices.end());
                        for (auto& f : meshEditAsset.faces) {
                            if (mergeSet.count(static_cast<int>(f.x))) f.x = static_cast<uint32_t>(keep);
                            if (mergeSet.count(static_cast<int>(f.y))) f.y = static_cast<uint32_t>(keep);
                            if (mergeSet.count(static_cast<int>(f.z))) f.z = static_cast<uint32_t>(keep);
                        }
                        meshEditSelectedVertices = { keep };
                        return true;
                    };

                    auto splitSelectedVertex = [&]() {
                        if (meshEditSelectedVertices.size() != 1) return false;
                        int vi = meshEditSelectedVertices.front();
                        if (vi < 0 || vi >= static_cast<int>(meshEditAsset.positions.size())) return false;
                        std::vector<int> connectedFaces;
                        for (int fi = 0; fi < static_cast<int>(meshEditAsset.faces.size()); ++fi) {
                            const auto& f = meshEditAsset.faces[fi];
                            if (static_cast<int>(f.x) == vi || static_cast<int>(f.y) == vi || static_cast<int>(f.z) == vi) {
                                connectedFaces.push_back(fi);
                            }
                        }
                        if (connectedFaces.size() < 2) return false;
                        size_t splitStart = connectedFaces.size() / 2;
                        for (size_t i = splitStart; i < connectedFaces.size(); ++i) {
                            int fi = connectedFaces[i];
                            uint32_t newIdx = static_cast<uint32_t>(meshEditAsset.positions.size());
                            meshEditAsset.positions.push_back(meshEditAsset.positions[vi]);
                            meshEditAsset.normals.push_back(meshEditAsset.normals[vi]);
                            meshEditAsset.uvs.push_back(meshEditAsset.uvs[vi]);
                            auto& f = meshEditAsset.faces[fi];
                            if (static_cast<int>(f.x) == vi) f.x = newIdx;
                            if (static_cast<int>(f.y) == vi) f.y = newIdx;
                            if (static_cast<int>(f.z) == vi) f.z = newIdx;
                        }
                        return true;
                    };

                    auto deleteSelectedEdges = [&]() {
                        if (meshEditSelectedEdges.empty()) return false;
                        std::unordered_set<uint64_t> selected;
                        selected.reserve(meshEditSelectedEdges.size() * 2);
                        auto edgeKey = [](uint32_t a, uint32_t b) -> uint64_t {
                            return (static_cast<uint64_t>(std::min(a, b)) << 32) | static_cast<uint64_t>(std::max(a, b));
                        };
                        for (int ei : meshEditSelectedEdges) {
                            if (ei < 0 || ei >= static_cast<int>(edges.size())) continue;
                            selected.insert(edgeKey(edges[ei].x, edges[ei].y));
                        }
                        std::vector<glm::u32vec3> nextFaces;
                        std::vector<uint32_t> nextMats;
                        for (size_t fi = 0; fi < meshEditAsset.faces.size(); ++fi) {
                            const auto& f = meshEditAsset.faces[fi];
                            uint64_t keys[3] = {
                                edgeKey(f.x, f.y), edgeKey(f.y, f.z), edgeKey(f.z, f.x)
                            };
                            if (selected.count(keys[0]) || selected.count(keys[1]) || selected.count(keys[2])) {
                                continue;
                            }
                            nextFaces.push_back(f);
                            uint32_t mat = (fi < meshEditAsset.faceMaterialIndices.size()) ? meshEditAsset.faceMaterialIndices[fi] : 0u;
                            nextMats.push_back(mat);
                        }
                        meshEditAsset.faces = std::move(nextFaces);
                        meshEditAsset.faceMaterialIndices = std::move(nextMats);
                        meshEditSelectedEdges.clear();
                        return true;
                    };

                    auto bridgeSelectedEdges = [&]() {
                        if (meshEditSelectedEdges.size() != 2) return false;
                        ensureUvs();
                        int e0 = meshEditSelectedEdges[0];
                        int e1 = meshEditSelectedEdges[1];
                        if (e0 < 0 || e0 >= static_cast<int>(edges.size()) ||
                            e1 < 0 || e1 >= static_cast<int>(edges.size())) {
                            return false;
                        }
                        const auto a = edges[e0];
                        const auto b = edges[e1];
                        if (a.x == b.x || a.x == b.y || a.y == b.x || a.y == b.y) {
                            return false;
                        }

                        const float mapCost0 = glm::length(meshEditAsset.positions[a.x] - meshEditAsset.positions[b.x]) +
                                               glm::length(meshEditAsset.positions[a.y] - meshEditAsset.positions[b.y]);
                        const float mapCost1 = glm::length(meshEditAsset.positions[a.x] - meshEditAsset.positions[b.y]) +
                                               glm::length(meshEditAsset.positions[a.y] - meshEditAsset.positions[b.x]);

                        glm::u32vec3 tri0(0u), tri1(0u);
                        if (mapCost0 <= mapCost1) {
                            tri0 = glm::u32vec3(a.x, a.y, b.y);
                            tri1 = glm::u32vec3(a.x, b.y, b.x);
                        } else {
                            tri0 = glm::u32vec3(a.x, a.y, b.x);
                            tri1 = glm::u32vec3(a.x, b.x, b.y);
                        }

                        auto edgeKey = [](uint32_t u, uint32_t v) -> uint64_t {
                            return (static_cast<uint64_t>(std::min(u, v)) << 32) | static_cast<uint64_t>(std::max(u, v));
                        };
                        auto triNormal = [&](const glm::u32vec3& t) {
                            if (t.x >= meshEditAsset.positions.size() || t.y >= meshEditAsset.positions.size() || t.z >= meshEditAsset.positions.size()) {
                                return glm::vec3(0.0f);
                            }
                            glm::vec3 n = glm::cross(meshEditAsset.positions[t.y] - meshEditAsset.positions[t.x],
                                                     meshEditAsset.positions[t.z] - meshEditAsset.positions[t.x]);
                            float len = glm::length(n);
                            return (len > 1e-6f) ? (n / len) : glm::vec3(0.0f);
                        };

                        glm::vec3 bridgeNormal = triNormal(tri0) + triNormal(tri1);
                        if (glm::length(bridgeNormal) < 1e-6f) {
                            return false;
                        }

                        glm::vec3 adjacentNormal(0.0f);
                        const uint64_t k0 = edgeKey(a.x, a.y);
                        const uint64_t k1 = edgeKey(b.x, b.y);
                        for (const auto& f : meshEditAsset.faces) {
                            uint64_t keys[3] = { edgeKey(f.x, f.y), edgeKey(f.y, f.z), edgeKey(f.z, f.x) };
                            if (keys[0] == k0 || keys[1] == k0 || keys[2] == k0 ||
                                keys[0] == k1 || keys[1] == k1 || keys[2] == k1) {
                                adjacentNormal += triNormal(f);
                            }
                        }

                        if (glm::length(adjacentNormal) > 1e-6f &&
                            glm::dot(glm::normalize(bridgeNormal), glm::normalize(adjacentNormal)) < 0.0f) {
                            std::swap(tri0.y, tri0.z);
                            std::swap(tri1.y, tri1.z);
                        }

                        uint32_t mat = static_cast<uint32_t>(meshEditActiveMaterialSlot);
                        int faceStart = static_cast<int>(meshEditAsset.faces.size());
                        addFaceWithMaterial(tri0, mat);
                        addFaceWithMaterial(tri1, mat);
                        if (meshEditAutoUV || !meshEditAsset.hasUVs) {
                            applyPlanarUvToFace(faceStart);
                            applyPlanarUvToFace(faceStart + 1);
                            meshEditAsset.hasUVs = true;
                        }
                        meshEditSelectedFaces = { faceStart, faceStart + 1 };
                        meshEditSelectedEdges.clear();
                        meshEditSelectedVertices.clear();
                        return true;
                    };

                    auto fillSelectedEdgeBoundary = [&]() {
                        if (meshEditSelectedEdges.size() < 3) return false;
                        ensureUvs();
                        ensureFaceMaterials();

                        auto edgeKey = [](uint32_t u, uint32_t v) -> uint64_t {
                            return (static_cast<uint64_t>(std::min(u, v)) << 32) | static_cast<uint64_t>(std::max(u, v));
                        };
                        auto directedEdgeInFace = [](const glm::u32vec3& f, uint32_t a, uint32_t b) -> int {
                            if ((f.x == a && f.y == b) || (f.y == a && f.z == b) || (f.z == a && f.x == b)) return 1;
                            if ((f.x == b && f.y == a) || (f.y == b && f.z == a) || (f.z == b && f.x == a)) return -1;
                            return 0;
                        };
                        auto cross2 = [](const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) -> float {
                            return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                        };
                        auto pointInTriangle2 = [&](const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, float eps) -> bool {
                            float c1 = cross2(a, b, p);
                            float c2 = cross2(b, c, p);
                            float c3 = cross2(c, a, p);
                            bool hasNeg = (c1 < -eps) || (c2 < -eps) || (c3 < -eps);
                            bool hasPos = (c1 > eps) || (c2 > eps) || (c3 > eps);
                            return !(hasNeg && hasPos);
                        };
                        auto projectTo2 = [](const glm::vec3& p, const glm::vec3& n) -> glm::vec2 {
                            glm::vec3 an = glm::abs(n);
                            if (an.x >= an.y && an.x >= an.z) return glm::vec2(p.y, p.z);
                            if (an.y >= an.z) return glm::vec2(p.x, p.z);
                            return glm::vec2(p.x, p.y);
                        };

                        std::vector<glm::u32vec2> selectedBoundaryEdges;
                        selectedBoundaryEdges.reserve(meshEditSelectedEdges.size());
                        std::unordered_set<uint64_t> seenEdges;
                        seenEdges.reserve(meshEditSelectedEdges.size() * 2);
                        for (int ei : meshEditSelectedEdges) {
                            if (ei < 0 || ei >= static_cast<int>(edges.size())) continue;
                            const auto& e = edges[ei];
                            if (e.x == e.y) continue;
                            uint64_t key = edgeKey(e.x, e.y);
                            if (!seenEdges.insert(key).second) continue;
                            selectedBoundaryEdges.push_back(e);
                        }
                        if (selectedBoundaryEdges.size() < 3) return false;

                        const int originalFaceCount = static_cast<int>(meshEditAsset.faces.size());
                        std::unordered_map<uint64_t, std::vector<int>> edgeFaces;
                        edgeFaces.reserve(static_cast<size_t>(originalFaceCount) * 3);
                        for (int fi = 0; fi < originalFaceCount; ++fi) {
                            const auto& f = meshEditAsset.faces[fi];
                            edgeFaces[edgeKey(f.x, f.y)].push_back(fi);
                            edgeFaces[edgeKey(f.y, f.z)].push_back(fi);
                            edgeFaces[edgeKey(f.z, f.x)].push_back(fi);
                        }

                        std::unordered_map<uint32_t, std::vector<uint32_t>> adjacency;
                        adjacency.reserve(selectedBoundaryEdges.size() * 2);
                        for (const auto& e : selectedBoundaryEdges) {
                            adjacency[e.x].push_back(e.y);
                            adjacency[e.y].push_back(e.x);
                        }

                        std::unordered_set<uint32_t> visitedVerts;
                        visitedVerts.reserve(adjacency.size() * 2);
                        std::vector<int> createdFaces;
                        createdFaces.reserve(selectedBoundaryEdges.size());
                        const uint32_t mat = static_cast<uint32_t>(meshEditActiveMaterialSlot);

                        for (const auto& kv : adjacency) {
                            uint32_t startVert = kv.first;
                            if (visitedVerts.count(startVert)) continue;

                            std::vector<uint32_t> stack = { startVert };
                            std::vector<uint32_t> componentVerts;
                            visitedVerts.insert(startVert);
                            while (!stack.empty()) {
                                uint32_t v = stack.back();
                                stack.pop_back();
                                componentVerts.push_back(v);
                                const auto itAdj = adjacency.find(v);
                                if (itAdj == adjacency.end()) continue;
                                for (uint32_t n : itAdj->second) {
                                    if (visitedVerts.insert(n).second) {
                                        stack.push_back(n);
                                    }
                                }
                            }
                            if (componentVerts.size() < 3) continue;

                            std::unordered_set<uint32_t> componentSet(componentVerts.begin(), componentVerts.end());
                            std::vector<glm::u32vec2> componentEdges;
                            componentEdges.reserve(componentVerts.size() * 2);
                            for (const auto& e : selectedBoundaryEdges) {
                                if (componentSet.count(e.x) && componentSet.count(e.y)) {
                                    componentEdges.push_back(e);
                                }
                            }
                            if (componentEdges.size() < 3) continue;

                            bool validCycle = (componentEdges.size() == componentVerts.size());
                            if (!validCycle) continue;
                            for (uint32_t v : componentVerts) {
                                int degree = 0;
                                const auto& nbrs = adjacency[v];
                                for (uint32_t n : nbrs) {
                                    if (componentSet.count(n)) {
                                        ++degree;
                                    }
                                }
                                if (degree != 2) {
                                    validCycle = false;
                                    break;
                                }
                            }
                            if (!validCycle) continue;

                            bool openBoundary = true;
                            for (const auto& e : componentEdges) {
                                auto itFaces = edgeFaces.find(edgeKey(e.x, e.y));
                                if (itFaces == edgeFaces.end() || itFaces->second.size() != 1) {
                                    openBoundary = false;
                                    break;
                                }
                            }
                            if (!openBoundary) continue;

                            std::vector<uint32_t> loop;
                            loop.reserve(componentVerts.size());
                            uint32_t loopStart = *std::min_element(componentVerts.begin(), componentVerts.end());
                            uint32_t prev = UINT32_MAX;
                            uint32_t curr = loopStart;
                            for (size_t guard = 0; guard <= componentVerts.size(); ++guard) {
                                loop.push_back(curr);
                                const auto& allNbrs = adjacency[curr];
                                std::vector<uint32_t> nbrs;
                                nbrs.reserve(2);
                                for (uint32_t n : allNbrs) {
                                    if (componentSet.count(n)) nbrs.push_back(n);
                                }
                                if (nbrs.size() != 2) {
                                    loop.clear();
                                    break;
                                }
                                uint32_t next = (nbrs[0] != prev) ? nbrs[0] : nbrs[1];
                                prev = curr;
                                curr = next;
                                if (curr == loopStart) break;
                                if (std::find(loop.begin(), loop.end(), curr) != loop.end()) {
                                    loop.clear();
                                    break;
                                }
                            }
                            if (loop.size() != componentVerts.size() || curr != loopStart) continue;

                            int windingScore = 0;
                            for (size_t i = 0; i < loop.size(); ++i) {
                                uint32_t a = loop[i];
                                uint32_t b = loop[(i + 1) % loop.size()];
                                auto itFaces = edgeFaces.find(edgeKey(a, b));
                                if (itFaces == edgeFaces.end() || itFaces->second.size() != 1) continue;
                                int fi = itFaces->second[0];
                                if (fi < 0 || fi >= originalFaceCount) continue;
                                windingScore += directedEdgeInFace(meshEditAsset.faces[fi], a, b);
                            }
                            if (windingScore > 0) {
                                std::reverse(loop.begin(), loop.end());
                            }

                            glm::vec3 polyNormal(0.0f);
                            for (size_t i = 0; i < loop.size(); ++i) {
                                const glm::vec3& p = meshEditAsset.positions[loop[i]];
                                const glm::vec3& q = meshEditAsset.positions[loop[(i + 1) % loop.size()]];
                                polyNormal.x += (p.y - q.y) * (p.z + q.z);
                                polyNormal.y += (p.z - q.z) * (p.x + q.x);
                                polyNormal.z += (p.x - q.x) * (p.y + q.y);
                            }
                            float polyLen = glm::length(polyNormal);
                            if (polyLen < 1e-6f) continue;
                            polyNormal /= polyLen;

                            std::vector<glm::vec2> loop2;
                            loop2.reserve(loop.size());
                            for (uint32_t v : loop) {
                                if (v >= meshEditAsset.positions.size()) {
                                    loop2.clear();
                                    break;
                                }
                                loop2.push_back(projectTo2(meshEditAsset.positions[v], polyNormal));
                            }
                            if (loop2.size() != loop.size()) continue;

                            std::vector<int> ring(loop.size());
                            std::iota(ring.begin(), ring.end(), 0);
                            auto ringArea = [&](const std::vector<int>& idxs) {
                                float a = 0.0f;
                                for (size_t i = 0; i < idxs.size(); ++i) {
                                    const glm::vec2& p = loop2[idxs[i]];
                                    const glm::vec2& q = loop2[idxs[(i + 1) % idxs.size()]];
                                    a += (p.x * q.y) - (q.x * p.y);
                                }
                                return a * 0.5f;
                            };
                            const float area = ringArea(ring);
                            if (std::abs(area) < 1e-7f) continue;

                            std::vector<glm::u32vec3> tris;
                            tris.reserve(loop.size() - 2);
                            const float eps = 1e-6f;
                            bool triangulationFailed = false;
                            while (ring.size() > 3) {
                                bool foundEar = false;
                                for (size_t i = 0; i < ring.size(); ++i) {
                                    int iPrev = ring[(i + ring.size() - 1) % ring.size()];
                                    int iCurr = ring[i];
                                    int iNext = ring[(i + 1) % ring.size()];

                                    float turn = cross2(loop2[iPrev], loop2[iCurr], loop2[iNext]);
                                    bool convex = (area > 0.0f) ? (turn > eps) : (turn < -eps);
                                    if (!convex) continue;

                                    bool hasPointInside = false;
                                    for (int r : ring) {
                                        if (r == iPrev || r == iCurr || r == iNext) continue;
                                        if (pointInTriangle2(loop2[r], loop2[iPrev], loop2[iCurr], loop2[iNext], eps)) {
                                            hasPointInside = true;
                                            break;
                                        }
                                    }
                                    if (hasPointInside) continue;

                                    tris.emplace_back(loop[iPrev], loop[iCurr], loop[iNext]);
                                    ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
                                    foundEar = true;
                                    break;
                                }
                                if (!foundEar) {
                                    triangulationFailed = true;
                                    break;
                                }
                            }
                            if (triangulationFailed || ring.size() != 3) continue;
                            tris.emplace_back(loop[ring[0]], loop[ring[1]], loop[ring[2]]);

                            int faceStart = static_cast<int>(meshEditAsset.faces.size());
                            for (const auto& tri : tris) {
                                addFaceWithMaterial(tri, mat);
                            }
                            for (int fi = faceStart; fi < static_cast<int>(meshEditAsset.faces.size()); ++fi) {
                                applyPlanarUvToFace(fi);
                                createdFaces.push_back(fi);
                            }
                            meshEditAsset.hasUVs = true;
                        }

                        if (createdFaces.empty()) return false;
                        meshEditSelectedFaces = std::move(createdFaces);
                        meshEditSelectedEdges.clear();
                        meshEditSelectedVertices.clear();
                        return true;
                    };

                    auto splitSelectedEdges = [&]() {
                        if (meshEditSelectedEdges.empty()) return false;
                        ensureUvs();
                        std::unordered_set<uint64_t> selected;
                        selected.reserve(meshEditSelectedEdges.size() * 2);
                        auto edgeKey = [](uint32_t a, uint32_t b) -> uint64_t {
                            return (static_cast<uint64_t>(std::min(a, b)) << 32) | static_cast<uint64_t>(std::max(a, b));
                        };
                        for (int ei : meshEditSelectedEdges) {
                            if (ei < 0 || ei >= static_cast<int>(edges.size())) continue;
                            selected.insert(edgeKey(edges[ei].x, edges[ei].y));
                        }

                        bool changed = false;
                        std::vector<int> faceIndices(meshEditAsset.faces.size());
                        std::iota(faceIndices.begin(), faceIndices.end(), 0);
                        std::reverse(faceIndices.begin(), faceIndices.end());
                        for (int fi : faceIndices) {
                            if (fi < 0 || fi >= static_cast<int>(meshEditAsset.faces.size())) continue;
                            const auto f = meshEditAsset.faces[fi];
                            const uint32_t tri[3] = { f.x, f.y, f.z };
                            uint32_t mat = (fi < static_cast<int>(meshEditAsset.faceMaterialIndices.size()))
                                ? meshEditAsset.faceMaterialIndices[fi]
                                : static_cast<uint32_t>(meshEditActiveMaterialSlot);
                            for (int e = 0; e < 3; ++e) {
                                uint32_t a = tri[e];
                                uint32_t b = tri[(e + 1) % 3];
                                if (!selected.count(edgeKey(a, b))) continue;
                                uint32_t c = tri[(e + 2) % 3];
                                uint32_t mid = static_cast<uint32_t>(meshEditAsset.positions.size());
                                meshEditAsset.positions.push_back((meshEditAsset.positions[a] + meshEditAsset.positions[b]) * 0.5f);
                                meshEditAsset.normals.push_back((meshEditAsset.normals[a] + meshEditAsset.normals[b]) * 0.5f);
                                meshEditAsset.uvs.push_back((meshEditAsset.uvs[a] + meshEditAsset.uvs[b]) * 0.5f);
                                meshEditAsset.faces.erase(meshEditAsset.faces.begin() + fi);
                                if (fi < static_cast<int>(meshEditAsset.faceMaterialIndices.size())) {
                                    meshEditAsset.faceMaterialIndices.erase(meshEditAsset.faceMaterialIndices.begin() + fi);
                                }
                                addFaceWithMaterial(glm::u32vec3(a, mid, c), mat);
                                addFaceWithMaterial(glm::u32vec3(mid, b, c), mat);
                                changed = true;
                                break;
                            }
                        }
                        return changed;
                    };

                    auto transformSelectedUvs = [&](const glm::vec2& move, float scale, float rotateDeg, bool flipU, bool flipV, bool reset, bool fit) {
                        if (meshEditSelectedFaces.empty()) return false;
                        ensureUvs();
                        std::vector<uint32_t> verts;
                        verts.reserve(meshEditSelectedFaces.size() * 3);
                        auto pushUnique = [&](uint32_t v) {
                            if (std::find(verts.begin(), verts.end(), v) == verts.end()) verts.push_back(v);
                        };
                        for (int fi : meshEditSelectedFaces) {
                            if (fi < 0 || fi >= static_cast<int>(meshEditAsset.faces.size())) continue;
                            const auto& f = meshEditAsset.faces[fi];
                            pushUnique(f.x);
                            pushUnique(f.y);
                            pushUnique(f.z);
                        }
                        if (verts.empty()) return false;

                        if (reset) {
                            for (int fi : meshEditSelectedFaces) {
                                applyPlanarUvToFace(fi);
                            }
                            return true;
                        }

                        glm::vec2 pivot(0.0f);
                        for (uint32_t v : verts) pivot += meshEditAsset.uvs[v];
                        pivot /= static_cast<float>(verts.size());

                        const float radians = glm::radians(rotateDeg);
                        const float cs = std::cos(radians);
                        const float sn = std::sin(radians);

                        for (uint32_t v : verts) {
                            glm::vec2 uv = meshEditAsset.uvs[v];
                            uv -= pivot;
                            uv *= scale;
                            if (flipU) uv.x *= -1.0f;
                            if (flipV) uv.y *= -1.0f;
                            uv = glm::vec2(uv.x * cs - uv.y * sn, uv.x * sn + uv.y * cs);
                            uv += pivot + move;
                            meshEditAsset.uvs[v] = uv;
                        }

                        if (fit) {
                            glm::vec2 minUv(FLT_MAX), maxUv(-FLT_MAX);
                            for (uint32_t v : verts) {
                                minUv = glm::min(minUv, meshEditAsset.uvs[v]);
                                maxUv = glm::max(maxUv, meshEditAsset.uvs[v]);
                            }
                            glm::vec2 span = maxUv - minUv;
                            for (uint32_t v : verts) {
                                glm::vec2 uv = meshEditAsset.uvs[v];
                                uv.x = span.x > 1e-6f ? (uv.x - minUv.x) / span.x : 0.0f;
                                uv.y = span.y > 1e-6f ? (uv.y - minUv.y) / span.y : 0.0f;
                                meshEditAsset.uvs[v] = uv;
                            }
                        }
                        meshEditAsset.hasUVs = true;
                        return true;
                    };

                    bool changed = false;
                    if (meshEditSelectionMode == MeshEditSelectionMode::Face) {
                        if (ImGui::MenuItem("Extrude")) {
                            changed = extrudeSelectedFaces(std::max(0.001f, meshEditExtrudeAmount), false);
                            if (changed) commitMeshEdit("Face Extrude");
                        }
                        if (ImGui::MenuItem("Inset Face")) {
                            changed = insetSelectedFaces();
                            if (changed) commitMeshEdit("Face Inset");
                        }
                        if (ImGui::MenuItem("Subdivide")) {
                            changed = subdivideSelectedFaces();
                            if (changed) commitMeshEdit("Face Subdivide");
                        }
                        if (ImGui::MenuItem("Merge Faces")) {
                            if (meshEditSelectedFaces.size() == 2) {
                                // Lightweight merge: flip triangulation across shared quad.
                                int fa = meshEditSelectedFaces[0];
                                int fb = meshEditSelectedFaces[1];
                                if (fa >= 0 && fb >= 0 &&
                                    fa < static_cast<int>(meshEditAsset.faces.size()) &&
                                    fb < static_cast<int>(meshEditAsset.faces.size())) {
                                    auto a = meshEditAsset.faces[fa];
                                    auto b = meshEditAsset.faces[fb];
                                    std::vector<uint32_t> all = { a.x, a.y, a.z, b.x, b.y, b.z };
                                    std::sort(all.begin(), all.end());
                                    all.erase(std::unique(all.begin(), all.end()), all.end());
                                    if (all.size() == 4) {
                                        uint32_t mat = (fa < static_cast<int>(meshEditAsset.faceMaterialIndices.size()))
                                            ? meshEditAsset.faceMaterialIndices[fa]
                                            : static_cast<uint32_t>(meshEditActiveMaterialSlot);
                                        meshEditAsset.faces[fa] = glm::u32vec3(all[0], all[1], all[2]);
                                        meshEditAsset.faces[fb] = glm::u32vec3(all[0], all[2], all[3]);
                                        if (fa < static_cast<int>(meshEditAsset.faceMaterialIndices.size())) meshEditAsset.faceMaterialIndices[fa] = mat;
                                        if (fb < static_cast<int>(meshEditAsset.faceMaterialIndices.size())) meshEditAsset.faceMaterialIndices[fb] = mat;
                                        changed = true;
                                    }
                                }
                            }
                            if (changed) commitMeshEdit("Face Merge");
                        }
                        if (ImGui::MenuItem("Separate Face")) {
                            changed = separateSelectedFaces();
                            if (changed) commitMeshEdit("Face Separate");
                        }
                        if (ImGui::MenuItem("Flip Face")) {
                            changed = flipSelectedFaces();
                            if (changed) commitMeshEdit("Face Flip");
                        }
                        if (ImGui::MenuItem("Branch Face Edges")) {
                            changed = extrudeSelectedFaces(std::max(0.001f, meshEditExtrudeAmount), true);
                            if (changed) commitMeshEdit("Face Branch");
                        }
                        if (ImGui::MenuItem("Delete Face")) {
                            changed = deleteSelectedFaces();
                            if (changed) commitMeshEdit("Face Delete");
                        }
                    } else if (meshEditSelectionMode == MeshEditSelectionMode::Edge) {
                        if (ImGui::MenuItem("Bevel Edge")) {
                            changed = splitSelectedEdges();
                            if (changed) {
                                meshEditSelectionMode = MeshEditSelectionMode::Vertex;
                                snapSelectedVertices();
                                commitMeshEdit("Edge Bevel");
                            }
                        }
                        if (ImGui::MenuItem("Subdivide Edge")) {
                            changed = splitSelectedEdges();
                            if (changed) commitMeshEdit("Edge Subdivide");
                        }
                        if (ImGui::MenuItem("Bridge Edges")) {
                            changed = bridgeSelectedEdges();
                            if (changed) commitMeshEdit("Edge Bridge");
                        }
                        if (ImGui::MenuItem("Fill Face")) {
                            changed = fillSelectedEdgeBoundary();
                            if (changed) commitMeshEdit("Edge Fill");
                        }
                        if (ImGui::MenuItem("Split Edge")) {
                            changed = splitSelectedEdges();
                            if (changed) commitMeshEdit("Edge Split");
                        }
                        if (ImGui::MenuItem("Delete Edge")) {
                            changed = deleteSelectedEdges();
                            if (changed) commitMeshEdit("Edge Delete");
                        }
                    } else if (meshEditSelectionMode == MeshEditSelectionMode::Vertex) {
                        if (ImGui::MenuItem("Weld Vertices")) {
                            changed = weldSelectedVertices();
                            if (changed) commitMeshEdit("Vertex Weld");
                        }
                        if (ImGui::MenuItem("Split Vertex")) {
                            changed = splitSelectedVertex();
                            if (changed) commitMeshEdit("Vertex Split");
                        }
                        if (ImGui::MenuItem("Delete Vertex")) {
                            changed = deleteSelectedVertices();
                            if (changed) commitMeshEdit("Vertex Delete");
                        }
                        if (ImGui::MenuItem("Snap To Grid")) {
                            changed = snapSelectedVertices();
                            if (changed) commitMeshEdit("Vertex Snap");
                        }
                        if (ImGui::MenuItem("Relax Vertex")) {
                            changed = relaxSelectedVertices();
                            if (changed) commitMeshEdit("Vertex Relax");
                        }
                    } else if (meshEditSelectionMode == MeshEditSelectionMode::UV) {
                        if (ImGui::MenuItem("Move UVs")) {
                            changed = transformSelectedUvs(glm::vec2(meshEditUvMoveStep, 0.0f), 1.0f, 0.0f, false, false, false, false);
                            if (changed) commitMeshEdit("UV Move");
                        }
                        if (ImGui::MenuItem("Scale UVs")) {
                            changed = transformSelectedUvs(glm::vec2(0.0f), meshEditUvScaleStep, 0.0f, false, false, false, false);
                            if (changed) commitMeshEdit("UV Scale");
                        }
                        if (ImGui::MenuItem("Rotate UVs")) {
                            changed = transformSelectedUvs(glm::vec2(0.0f), 1.0f, meshEditUvRotateStep, false, false, false, false);
                            if (changed) commitMeshEdit("UV Rotate");
                        }
                        if (ImGui::MenuItem("Flip UVs U")) {
                            changed = transformSelectedUvs(glm::vec2(0.0f), 1.0f, 0.0f, true, false, false, false);
                            if (changed) commitMeshEdit("UV Flip U");
                        }
                        if (ImGui::MenuItem("Flip UVs V")) {
                            changed = transformSelectedUvs(glm::vec2(0.0f), 1.0f, 0.0f, false, true, false, false);
                            if (changed) commitMeshEdit("UV Flip V");
                        }
                        if (ImGui::MenuItem("Reset UVs")) {
                            changed = transformSelectedUvs(glm::vec2(0.0f), 1.0f, 0.0f, false, false, true, false);
                            if (changed) commitMeshEdit("UV Reset");
                        }
                        if (ImGui::MenuItem("Fit UVs to Region")) {
                            changed = transformSelectedUvs(glm::vec2(0.0f), 1.0f, 0.0f, false, false, false, true);
                            if (changed) commitMeshEdit("UV Fit");
                        }
                    }

                    ImGui::EndPopup();
                }

                static bool meshEditHistoryCaptured = false;
                static bool meshEditWasUsing = false;
                static bool meshEditExtruding = false;
                static bool meshEditAwaitMouseRelease = false;
                static std::vector<int> meshEditExtrudeVerts;
                static std::vector<int> meshEditExtrudeFaces;

                if (!baseAffectedVerts.empty()) {
                    glm::vec3 pivotWorld(0.0f);
                    for (int idx : baseAffectedVerts) {
                        glm::vec3 wp = glm::vec3(modelMatrix * glm::vec4(meshEditAsset.positions[idx], 1.0f));
                        pivotWorld += wp;
                    }
                    pivotWorld /= (float)baseAffectedVerts.size();

                    glm::mat4 gizmoMat = glm::translate(glm::mat4(1.0f), pivotWorld);

                    ImGuizmo::Manipulate(
                        glm::value_ptr(view),
                        glm::value_ptr(proj),
                        ImGuizmo::TRANSLATE,
                        ImGuizmo::WORLD,
                        glm::value_ptr(gizmoMat)
                    );

                    if (meshSelectionChangedThisFrame && !meshEditWasUsing && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        // Prevent a select click from being interpreted as an immediate transform start.
                        meshEditAwaitMouseRelease = true;
                    }
                    if (meshEditAwaitMouseRelease && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        meshEditAwaitMouseRelease = false;
                    }

                    const bool suppressTransformOnSelectionFrame = meshSelectionChangedThisFrame && !meshEditWasUsing;
                    const bool suppressTransformUntilRelease = meshEditAwaitMouseRelease && !meshEditWasUsing;
                    bool usingNow = (suppressTransformOnSelectionFrame || suppressTransformUntilRelease) ? false : ImGuizmo::IsUsing();
                    if (usingNow && !meshEditWasUsing) {
                        ensureFaceMaterials();
                        bool wantsExtrude = meshEditExtrudeMode || ImGui::GetIO().KeyShift;
                        bool seams = ImGui::GetIO().KeyShift && ImGui::GetIO().KeyCtrl;
                        meshEditExtruding = false;
                        meshEditExtrudeVerts.clear();
                        meshEditExtrudeFaces.clear();
                        int originalVertexCount = static_cast<int>(meshEditAsset.positions.size());
                        int originalFaceCount = static_cast<int>(meshEditAsset.faces.size());
                        int newFaceStart = -1;

                        auto duplicateVertex = [&](uint32_t idx) -> uint32_t {
                            uint32_t newIdx = static_cast<uint32_t>(meshEditAsset.positions.size());
                            meshEditAsset.positions.push_back(meshEditAsset.positions[idx]);
                            if (idx < meshEditAsset.normals.size()) {
                                meshEditAsset.normals.push_back(meshEditAsset.normals[idx]);
                            } else {
                                meshEditAsset.normals.push_back(glm::vec3(0.0f));
                            }
                            if (idx < meshEditAsset.uvs.size()) {
                                meshEditAsset.uvs.push_back(meshEditAsset.uvs[idx]);
                            } else {
                                meshEditAsset.uvs.push_back(glm::vec2(0.0f));
                            }
                            return newIdx;
                        };
                        auto rebuildAffectedVerts = [&]() {
                            baseAffectedVerts.clear();
                            if (meshEditSelectionMode == MeshEditSelectionMode::Vertex) {
                                baseAffectedVerts = meshEditSelectedVertices;
                            }
                            auto pushUnique = [&](int idx) {
                                if (idx < 0) return;
                                if (std::find(baseAffectedVerts.begin(), baseAffectedVerts.end(), idx) == baseAffectedVerts.end()) {
                                    baseAffectedVerts.push_back(idx);
                                }
                            };
                            if (meshEditSelectionMode == MeshEditSelectionMode::Edge) {
                                for (int ei : meshEditSelectedEdges) {
                                    if (ei < 0 || ei >= (int)edges.size()) continue;
                                    pushUnique(edges[ei].x);
                                    pushUnique(edges[ei].y);
                                }
                            } else if (meshEditSelectionMode == MeshEditSelectionMode::Face ||
                                       meshEditSelectionMode == MeshEditSelectionMode::UV) {
                                for (int fi : meshEditSelectedFaces) {
                                    if (fi < 0 || fi >= (int)meshEditAsset.faces.size()) continue;
                                    const auto& f = meshEditAsset.faces[fi];
                                    pushUnique(f.x);
                                    pushUnique(f.y);
                                    pushUnique(f.z);
                                }
                            }
                        };
                        auto pushExtrudeVert = [&](int idx) {
                            if (std::find(meshEditExtrudeVerts.begin(), meshEditExtrudeVerts.end(), idx) == meshEditExtrudeVerts.end()) {
                                meshEditExtrudeVerts.push_back(idx);
                            }
                        };
                        auto ensureUvs = [&]() {
                            if (meshEditAsset.uvs.size() < meshEditAsset.positions.size()) {
                                meshEditAsset.uvs.resize(meshEditAsset.positions.size(), glm::vec2(0.0f));
                            }
                        };
                        auto applyPlanarUV = [&](const glm::u32vec3& face) -> bool {
                            if (face.x >= meshEditAsset.positions.size() ||
                                face.y >= meshEditAsset.positions.size() ||
                                face.z >= meshEditAsset.positions.size()) {
                                return false;
                            }
                            const glm::vec3& a = meshEditAsset.positions[face.x];
                            const glm::vec3& b = meshEditAsset.positions[face.y];
                            const glm::vec3& c = meshEditAsset.positions[face.z];
                            glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
                            glm::vec2 ua(a.x, a.y), ub(b.x, b.y), uc(c.x, c.y);
                            if (std::abs(n.x) >= std::abs(n.y) && std::abs(n.x) >= std::abs(n.z)) {
                                ua = glm::vec2(a.y, a.z);
                                ub = glm::vec2(b.y, b.z);
                                uc = glm::vec2(c.y, c.z);
                            } else if (std::abs(n.y) >= std::abs(n.z)) {
                                ua = glm::vec2(a.x, a.z);
                                ub = glm::vec2(b.x, b.z);
                                uc = glm::vec2(c.x, c.z);
                            }
                            glm::vec2 minUV = glm::min(glm::min(ua, ub), uc);
                            glm::vec2 maxUV = glm::max(glm::max(ua, ub), uc);
                            glm::vec2 span = maxUV - minUV;
                            auto toUv = [&](const glm::vec2& v) {
                                return glm::vec2(
                                    span.x > 1e-5f ? (v.x - minUV.x) / span.x : 0.0f,
                                    span.y > 1e-5f ? (v.y - minUV.y) / span.y : 0.0f
                                );
                            };
                            meshEditAsset.uvs[face.x] = toUv(ua);
                            meshEditAsset.uvs[face.y] = toUv(ub);
                            meshEditAsset.uvs[face.z] = toUv(uc);
                            return true;
                        };

                        std::unordered_set<int> customExtrudeUvFaces;
                        auto addSideQuadWithUv = [&](uint32_t a, uint32_t b, uint32_t aNew, uint32_t bNew, uint32_t matIdx) {
                            uint32_t sideA = duplicateVertex(a);
                            uint32_t sideB = duplicateVertex(b);
                            uint32_t sideANew = duplicateVertex(aNew);
                            uint32_t sideBNew = duplicateVertex(bNew);

                            ensureUvs();
                            const float edgeLen = std::max(1e-4f, glm::length(meshEditAsset.positions[sideB] - meshEditAsset.positions[sideA]));
                            const float heightA = glm::length(meshEditAsset.positions[sideANew] - meshEditAsset.positions[sideA]);
                            const float heightB = glm::length(meshEditAsset.positions[sideBNew] - meshEditAsset.positions[sideB]);
                            const float sideHeight = std::max(1e-4f, 0.5f * (heightA + heightB));

                            meshEditAsset.uvs[sideA] = glm::vec2(0.0f, 0.0f);
                            meshEditAsset.uvs[sideB] = glm::vec2(edgeLen, 0.0f);
                            meshEditAsset.uvs[sideBNew] = glm::vec2(edgeLen, sideHeight);
                            meshEditAsset.uvs[sideANew] = glm::vec2(0.0f, sideHeight);

                            int firstFace = static_cast<int>(meshEditAsset.faces.size());
                            meshEditAsset.faces.push_back(glm::u32vec3(sideA, sideB, sideBNew));
                            meshEditAsset.faceMaterialIndices.push_back(matIdx);
                            meshEditAsset.faces.push_back(glm::u32vec3(sideA, sideBNew, sideANew));
                            meshEditAsset.faceMaterialIndices.push_back(matIdx);
                            customExtrudeUvFaces.insert(firstFace);
                            customExtrudeUvFaces.insert(firstFace + 1);
                            pushExtrudeVert(static_cast<int>(sideANew));
                            pushExtrudeVert(static_cast<int>(sideBNew));
                            meshEditAsset.hasUVs = true;
                        };

                        if (wantsExtrude && meshEditSelectionMode == MeshEditSelectionMode::Face && !meshEditSelectedFaces.empty()) {
                            newFaceStart = (int)meshEditAsset.faces.size();
                            const size_t faceCount = meshEditAsset.faces.size();
                            std::vector<glm::u32vec3> originalFaces = meshEditAsset.faces;
                            std::vector<uint32_t> originalFaceMaterials = meshEditAsset.faceMaterialIndices;
                            if (originalFaceMaterials.size() < faceCount) {
                                originalFaceMaterials.resize(faceCount, static_cast<uint32_t>(meshEditActiveMaterialSlot));
                            }
                            auto matForFace = [&](int fi) -> uint32_t {
                                if (fi >= 0 && fi < static_cast<int>(originalFaceMaterials.size())) {
                                    return originalFaceMaterials[fi];
                                }
                                return static_cast<uint32_t>(meshEditActiveMaterialSlot);
                            };
                            std::vector<bool> faceSelected(faceCount, false);
                            for (int fi : meshEditSelectedFaces) {
                                if (fi >= 0 && fi < (int)faceCount) faceSelected[fi] = true;
                            }

                            std::unordered_map<uint32_t, uint32_t> vertexMap;
                            std::unordered_map<int, glm::u32vec3> newFaceVerts;
                            std::vector<int> newFaceSelection;
                            vertexMap.reserve(meshEditSelectedFaces.size() * 3);
                            newFaceVerts.reserve(meshEditSelectedFaces.size());
                            newFaceSelection.reserve(meshEditSelectedFaces.size());
                            for (int fi : meshEditSelectedFaces) {
                                if (fi < 0 || fi >= (int)faceCount) continue;
                                const auto f = originalFaces[fi];
                                uint32_t idx[3] = { f.x, f.y, f.z };
                                uint32_t newIdx[3];
                                for (int k = 0; k < 3; ++k) {
                                    if (seams) {
                                        newIdx[k] = duplicateVertex(idx[k]);
                                    } else {
                                        auto it = vertexMap.find(idx[k]);
                                        if (it == vertexMap.end()) {
                                            uint32_t created = duplicateVertex(idx[k]);
                                            vertexMap[idx[k]] = created;
                                            newIdx[k] = created;
                                        } else {
                                            newIdx[k] = it->second;
                                        }
                                    }
                                    pushExtrudeVert((int)newIdx[k]);
                                }
                                meshEditAsset.faces.push_back(glm::u32vec3(newIdx[0], newIdx[1], newIdx[2]));
                                meshEditAsset.faceMaterialIndices.push_back(matForFace(fi));
                                int newFaceIndex = (int)meshEditAsset.faces.size() - 1;
                                newFaceVerts[fi] = glm::u32vec3(newIdx[0], newIdx[1], newIdx[2]);
                                newFaceSelection.push_back(newFaceIndex);
                            }

                            if (seams) {
                                for (int fi : meshEditSelectedFaces) {
                                    if (fi < 0 || fi >= (int)faceCount) continue;
                                    auto itFace = newFaceVerts.find(fi);
                                    if (itFace == newFaceVerts.end()) continue;
                                    const auto f = itFace->second;
                                    const auto oldF = originalFaces[fi];
                                    uint32_t oldIdx[3] = { oldF.x, oldF.y, oldF.z };
                                    uint32_t newIdx[3] = { f.x, f.y, f.z };
                                    uint32_t mat = matForFace(fi);
                                    addSideQuadWithUv(oldIdx[0], oldIdx[1], newIdx[0], newIdx[1], mat);
                                    addSideQuadWithUv(oldIdx[1], oldIdx[2], newIdx[1], newIdx[2], mat);
                                    addSideQuadWithUv(oldIdx[2], oldIdx[0], newIdx[2], newIdx[0], mat);
                                }
                            } else {
                                struct EdgeInfo { int total = 0; int selected = 0; };
                                std::unordered_map<uint64_t, EdgeInfo> edgeInfo;
                                edgeInfo.reserve(faceCount * 3);
                                auto edgeKey = [](uint32_t a, uint32_t b) {
                                    return (static_cast<uint64_t>(std::min(a,b)) << 32) | static_cast<uint64_t>(std::max(a,b));
                                };
                                for (size_t fi = 0; fi < faceCount; ++fi) {
                                    const auto& f = originalFaces[fi];
                                    uint32_t tri[3] = { f.x, f.y, f.z };
                                    for (int e = 0; e < 3; ++e) {
                                        uint32_t a = tri[e];
                                        uint32_t b = tri[(e + 1) % 3];
                                        auto& info = edgeInfo[edgeKey(a, b)];
                                        info.total += 1;
                                        if (faceSelected[fi]) info.selected += 1;
                                    }
                                }
                                for (int fi : meshEditSelectedFaces) {
                                    if (fi < 0 || fi >= (int)faceCount) continue;
                                    const auto& f = originalFaces[fi];
                                    uint32_t tri[3] = { f.x, f.y, f.z };
                                    for (int e = 0; e < 3; ++e) {
                                        uint32_t a = tri[e];
                                        uint32_t b = tri[(e + 1) % 3];
                                        auto it = edgeInfo.find(edgeKey(a, b));
                                        if (it == edgeInfo.end()) continue;
                                        uint32_t mat = matForFace(fi);
                                        if (it->second.selected == 1 && it->second.selected < it->second.total) {
                                            uint32_t aNew = vertexMap[a];
                                            uint32_t bNew = vertexMap[b];
                                            addSideQuadWithUv(a, b, aNew, bNew, mat);
                                        } else if (it->second.total == 1) {
                                            uint32_t aNew = vertexMap[a];
                                            uint32_t bNew = vertexMap[b];
                                            addSideQuadWithUv(a, b, aNew, bNew, mat);
                                        }
                                    }
                                }
                            }

                            std::vector<uint8_t> removeOriginalFaces(faceCount, 0u);
                            int removedOriginalCount = 0;
                            for (int fi : meshEditSelectedFaces) {
                                if (fi >= 0 && fi < static_cast<int>(faceCount) && !removeOriginalFaces[fi]) {
                                    removeOriginalFaces[fi] = 1u;
                                    ++removedOriginalCount;
                                }
                            }
                            if (removedOriginalCount > 0) {
                                std::vector<int> faceRemap(meshEditAsset.faces.size(), -1);
                                std::vector<glm::u32vec3> rebuiltFaces;
                                std::vector<uint32_t> rebuiltMaterials;
                                rebuiltFaces.reserve(meshEditAsset.faces.size() - static_cast<size_t>(removedOriginalCount));
                                rebuiltMaterials.reserve(meshEditAsset.faceMaterialIndices.size());

                                for (int fi = 0; fi < static_cast<int>(meshEditAsset.faces.size()); ++fi) {
                                    if (fi < static_cast<int>(faceCount) && removeOriginalFaces[fi]) {
                                        continue;
                                    }
                                    faceRemap[fi] = static_cast<int>(rebuiltFaces.size());
                                    rebuiltFaces.push_back(meshEditAsset.faces[fi]);
                                    uint32_t mat = (fi < static_cast<int>(meshEditAsset.faceMaterialIndices.size()))
                                        ? meshEditAsset.faceMaterialIndices[fi]
                                        : static_cast<uint32_t>(meshEditActiveMaterialSlot);
                                    rebuiltMaterials.push_back(mat);
                                }

                                meshEditAsset.faces = std::move(rebuiltFaces);
                                meshEditAsset.faceMaterialIndices = std::move(rebuiltMaterials);

                                auto remapFaceSelection = [&](std::vector<int>& selection) {
                                    std::vector<int> remapped;
                                    remapped.reserve(selection.size());
                                    for (int fi : selection) {
                                        if (fi < 0 || fi >= static_cast<int>(faceRemap.size())) continue;
                                        int mapped = faceRemap[fi];
                                        if (mapped < 0) continue;
                                        if (std::find(remapped.begin(), remapped.end(), mapped) == remapped.end()) {
                                            remapped.push_back(mapped);
                                        }
                                    }
                                    selection = std::move(remapped);
                                };
                                remapFaceSelection(newFaceSelection);

                                std::unordered_set<int> remappedCustomUvFaces;
                                remappedCustomUvFaces.reserve(customExtrudeUvFaces.size() * 2);
                                for (int fi : customExtrudeUvFaces) {
                                    if (fi < 0 || fi >= static_cast<int>(faceRemap.size())) continue;
                                    int mapped = faceRemap[fi];
                                    if (mapped >= 0) {
                                        remappedCustomUvFaces.insert(mapped);
                                    }
                                }
                                customExtrudeUvFaces = std::move(remappedCustomUvFaces);

                                if (newFaceStart >= 0 && newFaceStart < static_cast<int>(faceRemap.size())) {
                                    newFaceStart = faceRemap[newFaceStart];
                                } else {
                                    newFaceStart = -1;
                                }
                                if (newFaceStart < 0 && !newFaceSelection.empty()) {
                                    newFaceStart = *std::min_element(newFaceSelection.begin(), newFaceSelection.end());
                                }
                            }

                            if (!newFaceSelection.empty()) {
                                meshEditSelectedFaces = newFaceSelection;
                                meshEditSelectedVertices.clear();
                                meshEditSelectedEdges.clear();
                            }

                            meshEditExtruding = !meshEditExtrudeVerts.empty();
                        } else if (wantsExtrude && meshEditSelectionMode == MeshEditSelectionMode::Edge && !meshEditSelectedEdges.empty()) {
                            newFaceStart = (int)meshEditAsset.faces.size();
                            std::unordered_map<uint32_t, uint32_t> vertexMap;
                            if (!seams) {
                                vertexMap.reserve(meshEditSelectedEdges.size() * 2);
                            }
                            uint32_t activeMat = static_cast<uint32_t>(meshEditActiveMaterialSlot);

                            for (int ei : meshEditSelectedEdges) {
                                if (ei < 0 || ei >= (int)edges.size()) continue;
                                uint32_t a = edges[ei].x;
                                uint32_t b = edges[ei].y;
                                uint32_t aNew = 0;
                                uint32_t bNew = 0;
                                if (seams) {
                                    aNew = duplicateVertex(a);
                                    bNew = duplicateVertex(b);
                                } else {
                                    auto ita = vertexMap.find(a);
                                    if (ita == vertexMap.end()) {
                                        aNew = duplicateVertex(a);
                                        vertexMap[a] = aNew;
                                    } else {
                                        aNew = ita->second;
                                    }
                                    auto itb = vertexMap.find(b);
                                    if (itb == vertexMap.end()) {
                                        bNew = duplicateVertex(b);
                                        vertexMap[b] = bNew;
                                    } else {
                                        bNew = itb->second;
                                    }
                                }
                                pushExtrudeVert((int)aNew);
                                pushExtrudeVert((int)bNew);
                                addSideQuadWithUv(a, b, aNew, bNew, activeMat);
                            }

                            meshEditExtruding = !meshEditExtrudeVerts.empty();
                        }

                        if (newFaceStart >= 0 && newFaceStart < (int)meshEditAsset.faces.size()) {
                            meshEditExtrudeFaces.clear();
                            meshEditExtrudeFaces.reserve(static_cast<size_t>(meshEditAsset.faces.size() - newFaceStart));
                            for (int fi = newFaceStart; fi < (int)meshEditAsset.faces.size(); ++fi) {
                                meshEditExtrudeFaces.push_back(fi);
                            }
                            ensureUvs();
                            bool wroteUvs = false;
                            for (int fi = newFaceStart; fi < (int)meshEditAsset.faces.size(); ++fi) {
                                if (customExtrudeUvFaces.count(fi) != 0) {
                                    continue;
                                }
                                const auto& f = meshEditAsset.faces[fi];
                                bool shouldWrite = !meshEditAsset.hasUVs ||
                                                   f.x >= (uint32_t)originalVertexCount ||
                                                   f.y >= (uint32_t)originalVertexCount ||
                                                   f.z >= (uint32_t)originalVertexCount;
                                if (shouldWrite) {
                                    wroteUvs |= applyPlanarUV(f);
                                }
                            }
                            if (wroteUvs) {
                                meshEditAsset.hasUVs = true;
                            }
                        }
                    }

                    std::vector<int> affectedVerts = baseAffectedVerts;
                    if (meshEditExtruding && !meshEditExtrudeVerts.empty()) {
                        affectedVerts = meshEditExtrudeVerts;
                    }

                    if (usingNow) {
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

                        recalcMesh();
                        if (meshEditExtruding && meshEditAutoUV && !meshEditExtrudeFaces.empty()) {
                            bool wroteUvs = false;
                            for (int fi : meshEditExtrudeFaces) {
                                if (fi < 0 || fi >= static_cast<int>(meshEditAsset.faces.size())) continue;
                                wroteUvs |= applyPlanarUvToFace(fi);
                            }
                            if (wroteUvs) {
                                meshEditAsset.hasUVs = true;
                            }
                        }
                        meshEditDirty = true;

                        syncMeshEditToGPU(selectedObj);
                    } else {
                        meshEditHistoryCaptured = false;
                        meshEditExtruding = false;
                        meshEditExtrudeVerts.clear();
                        meshEditExtrudeFaces.clear();
                    }

                    meshEditWasUsing = usingNow;
                } else {
                    meshEditHistoryCaptured = false;
                    meshEditExtruding = false;
                    meshEditExtrudeVerts.clear();
                    meshEditExtrudeFaces.clear();
                    meshEditWasUsing = false;
                    meshEditAwaitMouseRelease = false;
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
                    case ObjectType::Plane:
                        gizmoBoundsMin = glm::vec3(-0.5f, -0.5f, -0.02f);
                        gizmoBoundsMax = glm::vec3(0.5f, 0.5f, 0.02f);
                        break;
                    case ObjectType::Mirror:
                    case ObjectType::Sprite:
                    case ObjectType::Sprite25D:
                        gizmoBoundsMin = glm::vec3(-0.5f, -0.5f, -0.02f);
                        gizmoBoundsMax = glm::vec3(0.5f, 0.5f, 0.02f);
                        break;
                    case ObjectType::Torus:
                        gizmoBoundsMin = glm::vec3(-0.5f);
                        gizmoBoundsMax = glm::vec3(0.5f);
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
                    case ObjectType::Empty:
                        gizmoBoundsMin = glm::vec3(-0.2f);
                        gizmoBoundsMax = glm::vec3(0.2f);
                        break;
                    case ObjectType::Sprite2D:
                    case ObjectType::Canvas:
                    case ObjectType::UIImage:
                    case ObjectType::UISlider:
                    case ObjectType::UIButton:
                    case ObjectType::UIText:
                        gizmoBoundsMin = glm::vec3(-0.5f, -0.5f, -0.01f);
                        gizmoBoundsMax = glm::vec3(0.5f, 0.5f, 0.01f);
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

                if (mCurrentGizmoOperation == ImGuizmo::BOUNDS) {
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
                }

                if (ImGuizmo::IsUsing()) {
                    if (!gizmoHistoryCaptured) {
                        recordState("gizmo");
                        gizmoHistoryCaptured = true;
                    }
                    glm::mat4 delta = modelMatrix * glm::inverse(originalModel);

                    auto unwrapNear = [](float angle, float reference) {
                        float result = angle;
                        while (result - reference > 180.0f) result -= 360.0f;
                        while (reference - result > 180.0f) result += 360.0f;
                        return result;
                    };
                    auto quatFromEulerXYZ = [](const glm::vec3& deg) {
                        glm::mat4 m(1.0f);
                        m = glm::rotate(m, glm::radians(deg.x), glm::vec3(1.0f, 0.0f, 0.0f));
                        m = glm::rotate(m, glm::radians(deg.y), glm::vec3(0.0f, 1.0f, 0.0f));
                        m = glm::rotate(m, glm::radians(deg.z), glm::vec3(0.0f, 0.0f, 1.0f));
                        return glm::normalize(glm::quat_cast(glm::mat3(m)));
                    };
                    auto quatFromMatrixNoScale = [](const glm::mat4& m) {
                        glm::vec3 x = glm::vec3(m[0]);
                        glm::vec3 y = glm::vec3(m[1]);
                        if (glm::length(x) < 1e-6f || glm::length(y) < 1e-6f) {
                            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                        }
                        x = glm::normalize(x);
                        y = glm::normalize(y - x * glm::dot(x, y));
                        glm::vec3 z = glm::cross(x, y);
                        if (glm::length(z) < 1e-6f) {
                            z = glm::vec3(0.0f, 0.0f, 1.0f);
                        } else {
                            z = glm::normalize(z);
                        }
                        glm::mat3 rot(1.0f);
                        rot[0] = x;
                        rot[1] = y;
                        rot[2] = z;
                        return glm::normalize(glm::quat_cast(rot));
                    };

                    auto applyDelta = [&](SceneObject& o) {
                        glm::mat4 m = compose(o);
                        glm::mat4 newM = delta * m;
                        glm::vec3 t, r, s;
                        DecomposeMatrix(newM, t, r, s);

                        glm::vec3 rotDeg = glm::degrees(r);
                        if (mCurrentGizmoOperation == ImGuizmo::ROTATE ||
                            mCurrentGizmoOperation == ImGuizmo::UNIVERSAL) {
                            glm::quat beforeQ = quatFromMatrixNoScale(m);
                            glm::quat afterQ = quatFromMatrixNoScale(newM);
                            glm::quat deltaQ = glm::normalize(afterQ * glm::inverse(beforeQ));
                            glm::quat finalQ = glm::normalize(deltaQ * quatFromEulerXYZ(o.rotation));
                            glm::vec3 tmpPos(0.0f), tmpRot(0.0f), tmpScale(1.0f);
                            DecomposeMatrix(ComposeTransform(glm::vec3(0.0f), finalQ, glm::vec3(1.0f)),
                                            tmpPos, tmpRot, tmpScale);
                            rotDeg = glm::degrees(tmpRot);
                        }

                        o.position = t;
                        if (o.parentId != -1) {
                            rotDeg.x = unwrapNear(rotDeg.x, o.rotation.x);
                            rotDeg.y = unwrapNear(rotDeg.y, o.rotation.y);
                            rotDeg.z = unwrapNear(rotDeg.z, o.rotation.z);
                            o.rotation = rotDeg;
                        } else {
                            o.rotation = NormalizeEulerDegrees(rotDeg);
                        }
                        o.scale = s;
                        syncLocalTransform(o);
                    };

                    std::vector<int> selectedRoots;
                    if (selectedObjectIds.size() <= 1) {
                        selectedRoots.push_back(selectedObj->id);
                    } else {
                        std::unordered_set<int> selectedSet(selectedObjectIds.begin(), selectedObjectIds.end());
                        auto getParentId = [&](int id) -> int {
                            auto it = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                                [id](const SceneObject& o){ return o.id == id; });
                            if (it == sceneObjects.end()) return -1;
                            return it->parentId;
                        };
                        auto hasSelectedAncestor = [&](int id) -> bool {
                            int parentId = getParentId(id);
                            while (parentId != -1) {
                                if (selectedSet.count(parentId)) {
                                    return true;
                                }
                                parentId = getParentId(parentId);
                            }
                            return false;
                        };
                        for (int id : selectedObjectIds) {
                            if (!hasSelectedAncestor(id)) {
                                selectedRoots.push_back(id);
                            }
                        }
                    }

                    if (selectedRoots.size() <= 1) {
                        applyDelta(*selectedObj);
                    } else if (mCurrentGizmoMode == ImGuizmo::LOCAL && mCurrentGizmoOperation != ImGuizmo::BOUNDS) {
                        const bool applyTranslation =
                            (mCurrentGizmoOperation == ImGuizmo::TRANSLATE || mCurrentGizmoOperation == ImGuizmo::UNIVERSAL);
                        const bool applyRotation =
                            (mCurrentGizmoOperation == ImGuizmo::ROTATE || mCurrentGizmoOperation == ImGuizmo::UNIVERSAL);
                        const bool applyScale =
                            (mCurrentGizmoOperation == ImGuizmo::SCALE || mCurrentGizmoOperation == ImGuizmo::UNIVERSAL);

                        glm::vec3 deltaTranslationWorld(delta[3][0], delta[3][1], delta[3][2]);
                        glm::quat selectedBeforeQ = quatFromEulerXYZ(selectedObj->rotation);
                        glm::vec3 selectedLocalTranslation = glm::inverse(selectedBeforeQ) * deltaTranslationWorld;

                        glm::mat4 selectedBeforeM = compose(*selectedObj);
                        glm::mat4 selectedAfterM = delta * selectedBeforeM;
                        glm::quat selectedAfterQ = quatFromMatrixNoScale(selectedAfterM);
                        glm::quat localDeltaQ = glm::normalize(glm::inverse(selectedBeforeQ) * selectedAfterQ);

                        glm::vec3 selectedAfterT(0.0f), selectedAfterR(0.0f), selectedAfterS(1.0f);
                        DecomposeMatrix(selectedAfterM, selectedAfterT, selectedAfterR, selectedAfterS);
                        auto safeScaleRatio = [](float after, float before) {
                            constexpr float kEps = 1e-5f;
                            return (std::abs(before) > kEps) ? (after / before) : 1.0f;
                        };
                        glm::vec3 scaleRatio(
                            safeScaleRatio(selectedAfterS.x, selectedObj->scale.x),
                            safeScaleRatio(selectedAfterS.y, selectedObj->scale.y),
                            safeScaleRatio(selectedAfterS.z, selectedObj->scale.z)
                        );

                        for (int id : selectedRoots) {
                            auto itObj = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                                [id](const SceneObject& o){ return o.id == id; });
                            if (itObj == sceneObjects.end()) continue;

                            SceneObject& o = *itObj;
                            bool changed = false;

                            if (applyTranslation) {
                                glm::quat objectQ = quatFromEulerXYZ(o.rotation);
                                glm::vec3 objectWorldMove = objectQ * selectedLocalTranslation;
                                o.position += objectWorldMove;
                                changed = true;
                            }

                            if (applyRotation) {
                                glm::quat objectBeforeQ = quatFromEulerXYZ(o.rotation);
                                glm::quat objectAfterQ = glm::normalize(objectBeforeQ * localDeltaQ);
                                glm::vec3 tmpPos(0.0f), tmpRot(0.0f), tmpScale(1.0f);
                                DecomposeMatrix(ComposeTransform(glm::vec3(0.0f), objectAfterQ, glm::vec3(1.0f)),
                                                tmpPos, tmpRot, tmpScale);
                                glm::vec3 rotDeg = glm::degrees(tmpRot);
                                if (o.parentId != -1) {
                                    rotDeg.x = unwrapNear(rotDeg.x, o.rotation.x);
                                    rotDeg.y = unwrapNear(rotDeg.y, o.rotation.y);
                                    rotDeg.z = unwrapNear(rotDeg.z, o.rotation.z);
                                    o.rotation = rotDeg;
                                } else {
                                    o.rotation = NormalizeEulerDegrees(rotDeg);
                                }
                                changed = true;
                            }

                            if (applyScale) {
                                o.scale.x *= scaleRatio.x;
                                o.scale.y *= scaleRatio.y;
                                o.scale.z *= scaleRatio.z;
                                changed = true;
                            }

                            if (changed) {
                                syncLocalTransform(o);
                            }
                        }
                    } else {
                        for (int id : selectedRoots) {
                            auto itObj = std::find_if(sceneObjects.begin(), sceneObjects.end(),
                                [id](const SceneObject& o){ return o.id == id; });
                            if (itObj != sceneObjects.end()) {
                                applyDelta(*itObj);
                            }
                        }
                    }

                    updateHierarchyWorldTransforms();

                    projectManager.currentProject.hasUnsavedChanges = true;
                } else {
                    gizmoHistoryCaptured = false;
                }
            }
        } else {
            gizmoHistoryCaptured = false;
        }

        const float cameraOverlayAspect = std::max(0.1f, activeGameResolutionAspect);
        const float gizmoOverlayScaleClamped = std::clamp(sceneGizmoOverlayScale, 0.4f, 3.0f);
        const float gizmoIconScaleClamped = std::clamp(sceneGizmoIconScale, 0.4f, 3.0f);

        auto drawCameraDirection = [&](const SceneObject& camObj) {
            glm::quat q = glm::quat(glm::radians(camObj.rotation));
            glm::mat3 rot = glm::mat3_cast(q);
            glm::vec3 forward = glm::normalize(rot * glm::vec3(0.0f, 0.0f, -1.0f));
            glm::vec3 upDir = glm::normalize(rot * glm::vec3(0.0f, 1.0f, 0.0f));
            glm::vec3 rightDir = glm::normalize(rot * glm::vec3(1.0f, 0.0f, 0.0f));
            if (!std::isfinite(forward.x) || !std::isfinite(upDir.x) || !std::isfinite(rightDir.x) || glm::length(forward) < 1e-3f) return;
            const float alpha = camObj.enabled ? 1.0f : 0.35f;

            auto start = projectToScreen(camObj.position);
            auto end = projectToScreen(camObj.position + forward * (1.4f * gizmoOverlayScaleClamped));
            auto upTip = projectToScreen(camObj.position + upDir * (0.6f * gizmoOverlayScaleClamped));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (start && end) {
                ImU32 lineCol = ImGui::GetColorU32(ImVec4(0.3f, 0.8f, 1.0f, 0.9f * alpha));
                ImU32 headCol = ImGui::GetColorU32(ImVec4(0.9f, 1.0f, 1.0f, 0.95f * alpha));
                dl->AddLine(*start, *end, lineCol, 2.5f * gizmoOverlayScaleClamped);
                ImVec2 dir = ImVec2(end->x - start->x, end->y - start->y);
                float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
                if (len > 1.0f) {
                    ImVec2 normDir = ImVec2(dir.x / len, dir.y / len);
                    ImVec2 left = ImVec2(-normDir.y, normDir.x);
                    float head = 10.0f * gizmoOverlayScaleClamped;
                    ImVec2 tip = *end;
                    ImVec2 p1 = ImVec2(tip.x - normDir.x * head + left.x * head * 0.6f, tip.y - normDir.y * head + left.y * head * 0.6f);
                    ImVec2 p2 = ImVec2(tip.x - normDir.x * head - left.x * head * 0.6f, tip.y - normDir.y * head - left.y * head * 0.6f);
                    dl->AddTriangleFilled(tip, p1, p2, headCol);
                }
                if (upTip) {
                    dl->AddCircleFilled(*upTip, 3.0f * gizmoOverlayScaleClamped, ImGui::GetColorU32(ImVec4(0.8f, 1.0f, 0.6f, 0.8f * alpha)));
                }
            }

            auto drawWorldLine = [&](const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness) {
                auto sa = projectToScreen(a);
                auto sb = projectToScreen(b);
                if (sa && sb) {
                    dl->AddLine(*sa, *sb, color, thickness);
                }
            };

            const bool cameraIs2D = project2DPipeline || camObj.camera.use2D;
            if (cameraIs2D) {
                const float pixelsPerUnit = std::max(1.0f, camObj.camera.pixelsPerUnit);
                const float halfWidth = static_cast<float>(activeGameResolutionWidth) / (2.0f * pixelsPerUnit);
                const float halfHeight = static_cast<float>(activeGameResolutionHeight) / (2.0f * pixelsPerUnit);
                glm::vec3 center = camObj.position;
                std::array<glm::vec3, 4> corners = {
                    center - rightDir * halfWidth + upDir * halfHeight,
                    center + rightDir * halfWidth + upDir * halfHeight,
                    center + rightDir * halfWidth - upDir * halfHeight,
                    center - rightDir * halfWidth - upDir * halfHeight
                };

                ImU32 boundsCol = ImGui::GetColorU32(ImVec4(0.28f, 0.88f, 1.0f, 0.92f * alpha));
                ImU32 diagCol = ImGui::GetColorU32(ImVec4(0.28f, 0.88f, 1.0f, 0.45f * alpha));
                for (int i = 0; i < 4; ++i) {
                    drawWorldLine(corners[i], corners[(i + 1) % 4], boundsCol, 2.0f * gizmoOverlayScaleClamped);
                }
                drawWorldLine(corners[0], corners[2], diagCol, 1.2f * gizmoOverlayScaleClamped);
                drawWorldLine(corners[1], corners[3], diagCol, 1.2f * gizmoOverlayScaleClamped);
                drawWorldLine(center, center + upDir * std::max(0.1f, halfHeight * 0.45f),
                              ImGui::GetColorU32(ImVec4(0.94f, 0.98f, 1.0f, 0.88f * alpha)),
                              2.0f * gizmoOverlayScaleClamped);

                auto labelAnchor = projectToScreen(corners[1] + upDir * (0.08f * gizmoOverlayScaleClamped));
                if (gizmoShowCameraFrustumLabels && labelAnchor) {
                    char label[96];
                    std::snprintf(label, sizeof(label), "2D %dx%d | %.2fx%.2f",
                                  activeGameResolutionWidth, activeGameResolutionHeight,
                                  halfWidth * 2.0f, halfHeight * 2.0f);
                    ImVec2 textSize = ImGui::CalcTextSize(label);
                    ImVec2 pad(4.0f * gizmoOverlayScaleClamped, 2.0f * gizmoOverlayScaleClamped);
                    ImVec2 bgMin(labelAnchor->x, labelAnchor->y - textSize.y * 0.5f - pad.y);
                    ImVec2 bgMax(bgMin.x + textSize.x + pad.x * 2.0f, bgMin.y + textSize.y + pad.y * 2.0f);
                    dl->AddRectFilled(bgMin, bgMax, IM_COL32(16, 24, 34, static_cast<int>(195.0f * alpha)), 4.0f * gizmoOverlayScaleClamped);
                    dl->AddRect(bgMin, bgMax, IM_COL32(120, 200, 240, static_cast<int>(180.0f * alpha)), 4.0f * gizmoOverlayScaleClamped, 0, 1.0f);
                    dl->AddText(ImVec2(bgMin.x + pad.x, bgMin.y + pad.y), IM_COL32(214, 242, 255, static_cast<int>(235.0f * alpha)), label);
                }
                return;
            }

            const float nearPlane = std::max(0.01f, camObj.camera.nearClip);
            const float farPlane = std::max(nearPlane + 0.05f, camObj.camera.farClip);
            const float drawDepth = std::clamp(farPlane, nearPlane + 0.05f, 14.0f * gizmoOverlayScaleClamped);
            const float fovDeg = std::clamp(camObj.camera.fov, 5.0f, 170.0f);
            const float tanHalfFov = std::tan(glm::radians(fovDeg) * 0.5f);
            const float nearHalfH = tanHalfFov * nearPlane;
            const float nearHalfW = nearHalfH * cameraOverlayAspect;
            const float farHalfH = tanHalfFov * drawDepth;
            const float farHalfW = farHalfH * cameraOverlayAspect;

            glm::vec3 nearC = camObj.position + forward * nearPlane;
            glm::vec3 farC = camObj.position + forward * drawDepth;
            std::array<glm::vec3, 4> nearCorners = {
                nearC + upDir * nearHalfH - rightDir * nearHalfW,
                nearC + upDir * nearHalfH + rightDir * nearHalfW,
                nearC - upDir * nearHalfH + rightDir * nearHalfW,
                nearC - upDir * nearHalfH - rightDir * nearHalfW
            };
            std::array<glm::vec3, 4> farCorners = {
                farC + upDir * farHalfH - rightDir * farHalfW,
                farC + upDir * farHalfH + rightDir * farHalfW,
                farC - upDir * farHalfH + rightDir * farHalfW,
                farC - upDir * farHalfH - rightDir * farHalfW
            };

            ImU32 frustumLineCol = ImGui::GetColorU32(ImVec4(0.55f, 0.9f, 1.0f, 0.78f * alpha));
            ImU32 frustumFaintCol = ImGui::GetColorU32(ImVec4(0.55f, 0.9f, 1.0f, 0.42f * alpha));
            for (int i = 0; i < 4; ++i) {
                drawWorldLine(nearCorners[i], nearCorners[(i + 1) % 4], frustumFaintCol, 1.4f * gizmoOverlayScaleClamped);
                drawWorldLine(farCorners[i], farCorners[(i + 1) % 4], frustumLineCol, 1.6f * gizmoOverlayScaleClamped);
                drawWorldLine(camObj.position, farCorners[i], frustumFaintCol, 1.25f * gizmoOverlayScaleClamped);
            }
            drawWorldLine(camObj.position, farC, frustumLineCol, 1.8f * gizmoOverlayScaleClamped);

            auto labelAnchor = projectToScreen(farC + upDir * (farHalfH + 0.05f * gizmoOverlayScaleClamped) + rightDir * (farHalfW + 0.05f * gizmoOverlayScaleClamped));
            if (gizmoShowCameraFrustumLabels && labelAnchor) {
                char label[96];
                std::snprintf(label, sizeof(label), "FOV %.0f | %.2fx%.2f @ %.1f", fovDeg, farHalfW * 2.0f, farHalfH * 2.0f, drawDepth);
                ImVec2 textSize = ImGui::CalcTextSize(label);
                ImVec2 pad(4.0f * gizmoOverlayScaleClamped, 2.0f * gizmoOverlayScaleClamped);
                ImVec2 bgMin(labelAnchor->x, labelAnchor->y - textSize.y * 0.5f - pad.y);
                ImVec2 bgMax(bgMin.x + textSize.x + pad.x * 2.0f, bgMin.y + textSize.y + pad.y * 2.0f);
                dl->AddRectFilled(bgMin, bgMax, IM_COL32(16, 24, 34, static_cast<int>(195.0f * alpha)), 4.0f * gizmoOverlayScaleClamped);
                dl->AddRect(bgMin, bgMax, IM_COL32(120, 200, 240, static_cast<int>(180.0f * alpha)), 4.0f * gizmoOverlayScaleClamped, 0, 1.0f);
                dl->AddText(ImVec2(bgMin.x + pad.x, bgMin.y + pad.y), IM_COL32(214, 242, 255, static_cast<int>(235.0f * alpha)), label);
            }
        };

        if (showSceneGizmos && gizmoShowCameraOverlays && !worldUiEditing) {
            for (const auto& obj : sceneObjects) {
                if (obj.hasCamera) {
                    drawCameraDirection(obj);
                }
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
            if (lightObj.light.type == LightType::Point) {
                auto center = projectToScreen(lightObj.position);
                glm::vec3 offset = lightObj.position + glm::vec3(lightObj.light.range, 0.0f, 0.0f);
                auto edge = projectToScreen(offset);
                if (center && edge) {
                    float r = std::sqrt((center->x - edge->x)*(center->x - edge->x) + (center->y - edge->y)*(center->y - edge->y));
                    dl->AddCircle(*center, r, faint, 48, 2.0f * gizmoOverlayScaleClamped);
                }
            } else if (lightObj.light.type == LightType::Spot) {
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
                        dl->AddLine(prev, *sp, color, 1.5f * gizmoOverlayScaleClamped);
                        prev = *sp;
                    }
                };

                auto sTip = projectToScreen(tip);
                auto sEnd = projectToScreen(end);
                if (sTip && sEnd) {
                    dl->AddLine(*sTip, *sEnd, col, 2.0f * gizmoOverlayScaleClamped);
                    drawConeRing(innerRad, col);
                    drawConeRing(outerRad, faint);
                }
            } else if (lightObj.light.type == LightType::Area) {
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
                        dl->AddLine(projected[i], projected[(i+1)%4], col, 2.0f * gizmoOverlayScaleClamped);
                    }
                    // normal indicator
                    auto cproj = projectToScreen(c);
                    auto nproj = projectToScreen(c + n * glm::max(lightObj.light.range, 0.5f));
                    if (cproj && nproj) {
                        dl->AddLine(*cproj, *nproj, col, 2.0f * gizmoOverlayScaleClamped);
                        dl->AddCircleFilled(*nproj, 4.0f * gizmoOverlayScaleClamped, col);
                    }
                }

            }
        };

        if (showSceneGizmos && gizmoShowLightOverlays && !worldUiEditing) {
            for (const auto& obj : sceneObjects) {
                if (!obj.hasLight) continue;
                if (obj.light.type == LightType::Point || obj.light.type == LightType::Spot || obj.light.type == LightType::Area) {
                    drawLightOverlays(obj);
                }
            }
        }

        auto drawArmatureOverlays = [&](const SceneObject& skinnedObj,
                                        const std::unordered_map<int, const SceneObject*>& idLookup) {
            if (!skinnedObj.hasSkeletalAnimation || !skinnedObj.skeletal.enabled) return;
            if (skinnedObj.skeletal.boneNodeIds.empty()) return;

            std::unordered_set<int> boneIds;
            for (int id : skinnedObj.skeletal.boneNodeIds) {
                if (id >= 0) boneIds.insert(id);
            }
            if (boneIds.empty()) return;

            if (boneIds.size() <= 2 && skinnedObj.skeletal.skeletonRootId >= 0) {
                std::vector<int> stack;
                stack.push_back(skinnedObj.skeletal.skeletonRootId);
                while (!stack.empty()) {
                    int currentId = stack.back();
                    stack.pop_back();
                    auto it = idLookup.find(currentId);
                    if (it == idLookup.end() || !it->second) continue;
                    const SceneObject* node = it->second;
                    if (node->type == ObjectType::Empty) {
                        boneIds.insert(node->id);
                    }
                    for (int childId : node->childIds) {
                        if (childId >= 0) {
                            stack.push_back(childId);
                        }
                    }
                }
            }

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 lineCol = ImGui::GetColorU32(ImVec4(0.55f, 0.9f, 0.8f, 0.75f));
            ImU32 nodeCol = ImGui::GetColorU32(ImVec4(0.85f, 0.95f, 0.9f, 0.9f));
            ImU32 rootCol = ImGui::GetColorU32(ImVec4(1.0f, 0.85f, 0.45f, 0.95f));

            for (int id : boneIds) {
                auto it = idLookup.find(id);
                if (it == idLookup.end() || !it->second) continue;
                const SceneObject* boneObj = it->second;
                auto boneScreen = projectToScreen(boneObj->position);
                if (!boneScreen) continue;

                bool isRoot = boneObj->parentId < 0 || boneIds.find(boneObj->parentId) == boneIds.end();
                float radius = isRoot ? 4.5f : 3.0f;
                dl->AddCircleFilled(*boneScreen, radius, isRoot ? rootCol : nodeCol);

                if (boneObj->parentId >= 0) {
                    auto parentIt = idLookup.find(boneObj->parentId);
                    if (parentIt != idLookup.end() && parentIt->second &&
                        boneIds.find(boneObj->parentId) != boneIds.end()) {
                        auto parentScreen = projectToScreen(parentIt->second->position);
                        if (parentScreen) {
                            dl->AddLine(*parentScreen, *boneScreen, lineCol, 2.0f);
                        }
                    }
                }
            }
        };

        auto resolveMainObjectType = [](const SceneObject& obj) -> ObjectType {
            if (obj.hasRenderer) {
                const ObjectType mappedType = MapEnumToObjectType(obj.renderType, kRenderTypeMainObjectMap);
                if (mappedType != ObjectType::Empty) {
                    return mappedType;
                }
            }
            if (obj.hasUI) {
                ObjectType mappedType = MapEnumToObjectType(obj.ui.type, kUiTypeMainObjectMap);
                if (mappedType == ObjectType::Sprite2D && obj.type == ObjectType::Sprite25D) {
                    mappedType = ObjectType::Sprite25D;
                }
                if (mappedType != ObjectType::Empty) {
                    return mappedType;
                }
            }
            if (obj.hasLight) {
                const ObjectType mappedType = MapEnumToObjectType(obj.light.type, kLightTypeMainObjectMap);
                if (mappedType != ObjectType::Empty) {
                    return mappedType;
                }
            }
            if (obj.hasCamera) return ObjectType::Camera;
            if (obj.hasPostFX) return ObjectType::PostFXNode;
            return ObjectType::Empty;
        };

        struct GizmoIconImage {
            ImTextureID id = static_cast<ImTextureID>(0);
            bool flipY = false;
        };

        auto getMainTypeGizmoIcon = [&](ObjectType type) -> GizmoIconImage {
            const char* iconPath = nullptr;
            switch (type) {
                case ObjectType::Camera:
                    iconPath = "Resources/Engine-Root/Gizmos/Placeholder/Camera view.png";
                    break;
                case ObjectType::DirectionalLight:
                case ObjectType::PointLight:
                case ObjectType::SpotLight:
                case ObjectType::AreaLight:
                    iconPath = "Resources/Engine-Root/Gizmos/Placeholder/Light bulb.png";
                    break;
                case ObjectType::UIText:
                    iconPath = "Resources/Engine-Root/Gizmos/Placeholder/Dynamic Text.png";
                    break;
                default:
                    return {};
            }

            if (rendererInitialized) {
                if (Texture* icon = renderer.getTexture(iconPath); icon && icon->GetID()) {
                    return { static_cast<ImTextureID>(icon->GetID()), true };
                }
                return {};
            }

            if (hasVulkanSceneTexture && vulkanRenderer) {
                ImTextureID vkIcon = vulkanRenderer->getOrCreateUIImage(iconPath);
                if (vkIcon != static_cast<ImTextureID>(0)) {
                    return { vkIcon, false };
                }
            }

            return {};
        };

        auto drawMainTypeGizmoIcon = [&](const SceneObject& obj) {
            const GizmoIconImage icon = getMainTypeGizmoIcon(resolveMainObjectType(obj));
            if (icon.id == static_cast<ImTextureID>(0)) return;

            auto screen = projectToScreen(obj.position);
            if (!screen) return;

            const bool isSelected = std::find(selectedObjectIds.begin(), selectedObjectIds.end(), obj.id) != selectedObjectIds.end();
            const float size = (isSelected ? 74.0f : 56.0f) * gizmoIconScaleClamped;
            const float half = size * 0.5f;
            const ImVec2 min(screen->x - half, screen->y - half);
            const ImVec2 max(screen->x + half, screen->y + half);
            const ImU32 tint = IsObjectEnabledInHierarchy(obj) ? IM_COL32(255, 255, 255, 232) : IM_COL32(170, 170, 170, 168);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 uvMin = icon.flipY ? ImVec2(0, 1) : ImVec2(0, 0);
            const ImVec2 uvMax = icon.flipY ? ImVec2(1, 0) : ImVec2(1, 1);
            dl->AddImage(icon.id, min, max, uvMin, uvMax, tint);
            if (isSelected) {
                dl->AddRect(min, max, IM_COL32(255, 255, 255, 170), 5.0f * gizmoIconScaleClamped, 0, 1.8f);
            }
            if (obj.hasLight && gizmoShowLightIntensityLabels) {
                char brightness[24];
                std::snprintf(brightness, sizeof(brightness), "%.3g", obj.light.intensity);
                ImVec2 textSize = ImGui::CalcTextSize(brightness);
                const ImVec2 pad(5.0f * gizmoIconScaleClamped, 2.0f * gizmoIconScaleClamped);
                ImVec2 badgeMin(max.x - (textSize.x + pad.x * 2.0f) - 1.5f * gizmoIconScaleClamped, min.y + 1.5f * gizmoIconScaleClamped);
                ImVec2 badgeMax(max.x - 1.5f, badgeMin.y + textSize.y + pad.y * 2.0f);
                int bgA = IsObjectEnabledInHierarchy(obj) ? 212 : 136;
                int fgA = IsObjectEnabledInHierarchy(obj) ? 242 : 168;
                dl->AddRectFilled(badgeMin, badgeMax, IM_COL32(24, 28, 34, bgA), 5.0f * gizmoIconScaleClamped);
                dl->AddRect(badgeMin, badgeMax, IM_COL32(255, 214, 118, fgA), 5.0f * gizmoIconScaleClamped, 0, 1.0f);
                dl->AddText(ImVec2(badgeMin.x + pad.x, badgeMin.y + pad.y), IM_COL32(255, 238, 180, fgA), brightness);
            }
        };
        auto drawWireCube = [&](const glm::mat4& worldFromLocal,
                                const glm::vec3& localMin,
                                const glm::vec3& localMax,
                                ImU32 color,
                                float thickness) {
            std::array<glm::vec3, 8> corners = {
                glm::vec3(localMin.x, localMin.y, localMin.z),
                glm::vec3(localMax.x, localMin.y, localMin.z),
                glm::vec3(localMax.x, localMax.y, localMin.z),
                glm::vec3(localMin.x, localMax.y, localMin.z),
                glm::vec3(localMin.x, localMin.y, localMax.z),
                glm::vec3(localMax.x, localMin.y, localMax.z),
                glm::vec3(localMax.x, localMax.y, localMax.z),
                glm::vec3(localMin.x, localMax.y, localMax.z)
            };
            std::array<ImVec2, 8> projected = {};
            for (size_t i = 0; i < corners.size(); ++i) {
                glm::vec3 world = glm::vec3(worldFromLocal * glm::vec4(corners[i], 1.0f));
                auto p = projectToScreen(world);
                if (!p.has_value()) return;
                projected[i] = *p;
            }
            const int edges[12][2] = {
                {0,1},{1,2},{2,3},{3,0},
                {4,5},{5,6},{6,7},{7,4},
                {0,4},{1,5},{2,6},{3,7}
            };
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (const auto& e : edges) {
                dl->AddLine(projected[e[0]], projected[e[1]], color, thickness);
            }
        };
        auto drawSelectionColliderBounds = [&](const SceneObject& obj, ImU32 color, float thickness) {
            auto composeNoScale = [](const SceneObject& o) {
                glm::mat4 m(1.0f);
                m = glm::translate(m, o.position);
                m = glm::rotate(m, glm::radians(o.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
                m = glm::rotate(m, glm::radians(o.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
                m = glm::rotate(m, glm::radians(o.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
                return m;
            };
            auto composeWithScale = [](const SceneObject& o) {
                glm::mat4 m(1.0f);
                m = glm::translate(m, o.position);
                m = glm::rotate(m, glm::radians(o.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
                m = glm::rotate(m, glm::radians(o.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
                m = glm::rotate(m, glm::radians(o.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
                m = glm::scale(m, o.scale);
                return m;
            };

            if (obj.hasCollider && obj.collider.enabled) {
                glm::vec3 localMin(-0.5f);
                glm::vec3 localMax(0.5f);
                glm::mat4 world = composeNoScale(obj);
                world = glm::translate(world, obj.collider.offset);
                switch (obj.collider.type) {
                    case ColliderType::Box:
                        world = glm::scale(world, obj.collider.boxSize);
                        break;
                    case ColliderType::Capsule:
                        localMin = glm::vec3(-0.25f, -0.75f, -0.25f);
                        localMax = glm::vec3(0.25f, 0.75f, 0.25f);
                        world = glm::scale(world, obj.collider.boxSize);
                        break;
                    case ColliderType::Mesh:
                    case ColliderType::ConvexMesh: {
                        if (obj.hasRenderer && obj.renderType == RenderType::OBJMesh && obj.meshId >= 0) {
                            if (const auto* info = g_objLoader.getMeshInfo(obj.meshId)) {
                                if (info->boundsMin.x < info->boundsMax.x) {
                                    localMin = info->boundsMin;
                                    localMax = info->boundsMax;
                                }
                            }
                        } else if (obj.hasRenderer && obj.renderType == RenderType::Model && obj.meshId >= 0) {
                            if (const auto* info = getModelLoader().getMeshInfo(obj.meshId)) {
                                if (info->boundsMin.x < info->boundsMax.x) {
                                    localMin = info->boundsMin;
                                    localMax = info->boundsMax;
                                }
                            }
                        }
                        world = glm::scale(world, obj.scale);
                        break;
                    }
                }
                drawWireCube(world, localMin, localMax, color, thickness);
            }

            if (obj.hasCollider2D && obj.collider2D.enabled) {
                glm::mat4 world = composeWithScale(obj);
                auto drawPolylineLocal = [&](const std::vector<glm::vec3>& pts, bool closed) {
                    if (pts.size() < 2) return;
                    std::vector<ImVec2> projected;
                    projected.reserve(pts.size());
                    for (const auto& p : pts) {
                        auto sp = projectToScreen(glm::vec3(world * glm::vec4(p, 1.0f)));
                        if (!sp.has_value()) return;
                        projected.push_back(*sp);
                    }
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    for (size_t i = 1; i < projected.size(); ++i) {
                        dl->AddLine(projected[i - 1], projected[i], color, thickness);
                    }
                    if (closed && projected.size() > 2) {
                        dl->AddLine(projected.back(), projected.front(), color, thickness);
                    }
                };

                if (obj.collider2D.type == Collider2DType::Box) {
                    glm::vec2 half = obj.collider2D.boxSize * 0.5f;
                    std::vector<glm::vec3> pts = {
                        glm::vec3(-half.x + obj.collider2D.offset.x, -half.y + obj.collider2D.offset.y, 0.0f),
                        glm::vec3( half.x + obj.collider2D.offset.x, -half.y + obj.collider2D.offset.y, 0.0f),
                        glm::vec3( half.x + obj.collider2D.offset.x,  half.y + obj.collider2D.offset.y, 0.0f),
                        glm::vec3(-half.x + obj.collider2D.offset.x,  half.y + obj.collider2D.offset.y, 0.0f)
                    };
                    drawPolylineLocal(pts, true);
                } else if (!obj.collider2D.points.empty()) {
                    std::vector<glm::vec3> pts;
                    pts.reserve(obj.collider2D.points.size());
                    for (const auto& p : obj.collider2D.points) {
                        pts.emplace_back(p.x + obj.collider2D.offset.x, p.y + obj.collider2D.offset.y, 0.0f);
                    }
                    drawPolylineLocal(pts, obj.collider2D.type == Collider2DType::Polygon || obj.collider2D.closed);
                }
            }
        };

        const float toolbarPadding = 6.0f;
        const float toolbarSpacing = 5.0f;
        const ImVec2 gizmoIconButtonSize(32.0f, 24.0f);
        std::unordered_map<int, const SceneObject*> idLookup;
        idLookup.reserve(sceneObjects.size());
        for (const auto& obj : sceneObjects) {
            idLookup.emplace(obj.id, &obj);
        }
        std::vector<int> colliderPreviewRoots = selectedObjectIds;
        if (colliderPreviewRoots.empty() && selectedObjectId >= 0) {
            colliderPreviewRoots.push_back(selectedObjectId);
        }
        if (!worldUiEditing && !colliderPreviewRoots.empty()) {
            for (int id : colliderPreviewRoots) {
                auto it = idLookup.find(id);
                if (it == idLookup.end() || !it->second) continue;
                const SceneObject& node = *it->second;
                if (!(node.hasCollider2D && node.collider2D.enabled)) continue;
                drawSelectionColliderBounds(node, ImGui::GetColorU32(ImVec4(0.24f, 0.95f, 1.0f, 0.95f)), 2.4f);
            }
        }
        if (collisionWireframe && !worldUiEditing) {
            std::unordered_set<int> rootSet(colliderPreviewRoots.begin(), colliderPreviewRoots.end());
            std::unordered_set<int> visited;
            std::vector<int> stack = colliderPreviewRoots;
            while (!stack.empty()) {
                int id = stack.back();
                stack.pop_back();
                if (!visited.insert(id).second) continue;
                auto it = idLookup.find(id);
                if (it == idLookup.end() || !it->second) continue;
                const SceneObject* node = it->second;

                const bool isRoot = rootSet.find(id) != rootSet.end();
                if (!isRoot) {
                    drawSelectionColliderBounds(*node,
                                                ImGui::GetColorU32(ImVec4(0.38f, 1.0f, 0.78f, 0.85f)),
                                                1.8f);
                }

                for (int childId : node->childIds) {
                    if (childId >= 0) {
                        stack.push_back(childId);
                    }
                }
            }
        }
        if (showSceneGizmos && !worldUiEditing) {
            for (const auto& obj : sceneObjects) {
                drawArmatureOverlays(obj, idLookup);
            }
            for (const auto& obj : sceneObjects) {
                drawMainTypeGizmoIcon(obj);
            }
        }

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

        if (showViewportToolbar) {
            ImGui::SetCursorScreenPos(ImVec2(toolbarRectMin.x + toolbarPadding, toolbarRectMin.y + toolbarPadding));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(toolbarSpacing, toolbarSpacing));

            ImDrawList* toolbarDrawList = ImGui::GetWindowDrawList();
            ImDrawListSplitter splitter;
            splitter.Split(toolbarDrawList, 2);
            splitter.SetCurrentChannel(toolbarDrawList, 1);

            ImGui::BeginGroup();

        auto gizmoButton = [&](const char* id, GizmoToolbar::Icon icon, ImGuizmo::OPERATION op, const char* tooltip) {
            if (GizmoToolbar::IconButton(id, icon, mCurrentGizmoOperation == op, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
                mCurrentGizmoOperation = op;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tooltip);
            }
        };
        const bool use2DGizmos = worldUiEditing;
        if (use2DGizmos) {
            if (meshEditMode) {
                meshEditMode = false;
                meshEditLoaded = false;
                meshEditPath.clear();
                meshEditDirty = false;
                meshEditExtrudeMode = false;
                meshEditSelectedVertices.clear();
                meshEditSelectedEdges.clear();
                meshEditSelectedFaces.clear();
            }
            if (mCurrentGizmoOperation != ImGuizmo::TRANSLATE &&
                mCurrentGizmoOperation != ImGuizmo::ROTATE &&
                mCurrentGizmoOperation != ImGuizmo::SCALE &&
                mCurrentGizmoOperation != ImGuizmo::BOUNDS) {
                mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
            }
            mCurrentGizmoMode = ImGuizmo::LOCAL;
        }

        gizmoButton("##gizmo_move", GizmoToolbar::Icon::Translate, ImGuizmo::TRANSLATE, "Translate");
        ImGui::SameLine(0.0f, toolbarSpacing);
        gizmoButton("##gizmo_rotate", GizmoToolbar::Icon::Rotate, ImGuizmo::ROTATE, "Rotate");
        ImGui::SameLine(0.0f, toolbarSpacing);
        gizmoButton("##gizmo_scale", GizmoToolbar::Icon::Scale, ImGuizmo::SCALE, "Scale");
        if (use2DGizmos) {
            ImGui::SameLine(0.0f, toolbarSpacing);
            gizmoButton("##gizmo_bounds_2d", GizmoToolbar::Icon::Bounds, ImGuizmo::BOUNDS, "Rect scale");
        }
        if (!use2DGizmos) {
            ImGui::SameLine(0.0f, toolbarSpacing);
            // In-viewport RMesh editing is not tied to the legacy Mesh Builder package.
            bool canMeshEdit = selectedObj && IsRawMeshPath(selectedObj->meshPath);
            ImGui::BeginDisabled(!canMeshEdit);
            if (GizmoToolbar::IconButton("##gizmo_mesh_edit", GizmoToolbar::Icon::Mesh, meshEditMode, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
                meshEditMode = !meshEditMode;
                if (!meshEditMode) {
                    meshEditLoaded = false;
                    meshEditPath.clear();
                    meshEditDirty = false;
                    meshEditExtrudeMode = false;
                    meshEditSelectedVertices.clear();
                    meshEditSelectedEdges.clear();
                    meshEditSelectedFaces.clear();
                    meshEditSelectionMode = MeshEditSelectionMode::Object;
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle RMesh edit mode");
            ImGui::EndDisabled();
            if (meshEditMode) {
                ImGui::SameLine(0.0f, toolbarSpacing);
                if (GizmoToolbar::ModeButton("Obj", meshEditSelectionMode == MeshEditSelectionMode::Object, ImVec2(44,24), baseCol, accentCol, textCol)) {
                    meshEditSelectionMode = MeshEditSelectionMode::Object;
                    meshEditSelectedVertices.clear();
                    meshEditSelectedEdges.clear();
                    meshEditSelectedFaces.clear();
                }
                ImGui::SameLine(0.0f, toolbarSpacing * 0.6f);
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
                if (meshEditSelectionMode == MeshEditSelectionMode::Face ||
                    meshEditSelectionMode == MeshEditSelectionMode::UV) {
                    ImGui::SameLine(0.0f, toolbarSpacing * 0.6f);
                    if (GizmoToolbar::ModeButton("Tri", meshEditTriangleSelection, ImVec2(38,24), baseCol, accentCol, textCol)) {
                        meshEditTriangleSelection = !meshEditTriangleSelection;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(meshEditTriangleSelection
                            ? "Triangle selection: select individual triangles"
                            : "Logical face selection: pair compatible triangles");
                    }
                }
                ImGui::SameLine(0.0f, toolbarSpacing * 0.6f);
                if (GizmoToolbar::ModeButton("UV", meshEditSelectionMode == MeshEditSelectionMode::UV, ImVec2(38,24), baseCol, accentCol, textCol)) {
                    meshEditSelectionMode = MeshEditSelectionMode::UV;
                }
                ImGui::SameLine(0.0f, toolbarSpacing * 0.6f);
                if (GizmoToolbar::ModeButton("Extrude", meshEditExtrudeMode, ImVec2(68,24), baseCol, accentCol, textCol)) {
                    meshEditExtrudeMode = !meshEditExtrudeMode;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Toggle extrude mode (Shift to extrude, Shift+Ctrl for seams)");
                }
                ImGui::SameLine(0.0f, toolbarSpacing * 0.8f);
                if (GizmoToolbar::ModeButton("AutoUV", meshEditAutoUV, ImVec2(62,24), baseCol, accentCol, textCol)) {
                    meshEditAutoUV = !meshEditAutoUV;
                }
                ImGui::SameLine(0.0f, toolbarSpacing * 0.6f);
                if (GizmoToolbar::TextButton("Mats", false, ImVec2(46,24), baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
                    ImGui::OpenPopup("##mesh_edit_material_slots");
                }
                if (ImGui::BeginPopup("##mesh_edit_material_slots")) {
                    if (meshEditAsset.materialSlots.empty()) {
                        meshEditAsset.materialSlots.push_back("Default");
                    }
                    if (meshEditAsset.faceMaterialIndices.size() != meshEditAsset.faces.size()) {
                        meshEditAsset.faceMaterialIndices.resize(meshEditAsset.faces.size(), 0u);
                    }
                    meshEditActiveMaterialSlot = std::clamp(meshEditActiveMaterialSlot, 0, static_cast<int>(meshEditAsset.materialSlots.size()) - 1);

                    ImGui::TextUnformatted("Material Slots");
                    ImGui::Separator();
                    for (size_t slot = 0; slot < meshEditAsset.materialSlots.size(); ++slot) {
                        char slotName[128];
                        std::snprintf(slotName, sizeof(slotName), "%s", meshEditAsset.materialSlots[slot].c_str());
                        ImGui::PushID(static_cast<int>(slot));
                        if (ImGui::Selectable("##slot_sel", meshEditActiveMaterialSlot == static_cast<int>(slot), 0, ImVec2(18.0f, 18.0f))) {
                            meshEditActiveMaterialSlot = static_cast<int>(slot);
                        }
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(200.0f);
                        if (ImGui::InputText("##slot_name", slotName, sizeof(slotName))) {
                            meshEditAsset.materialSlots[slot] = slotName;
                            meshEditDirty = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::ArrowButton("##up", ImGuiDir_Up) && slot > 0) {
                            std::swap(meshEditAsset.materialSlots[slot], meshEditAsset.materialSlots[slot - 1]);
                            for (auto& idx : meshEditAsset.faceMaterialIndices) {
                                if (idx == slot) idx = static_cast<uint32_t>(slot - 1);
                                else if (idx == slot - 1) idx = static_cast<uint32_t>(slot);
                            }
                            meshEditActiveMaterialSlot = static_cast<int>(slot - 1);
                            meshEditDirty = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::ArrowButton("##down", ImGuiDir_Down) && slot + 1 < meshEditAsset.materialSlots.size()) {
                            std::swap(meshEditAsset.materialSlots[slot], meshEditAsset.materialSlots[slot + 1]);
                            for (auto& idx : meshEditAsset.faceMaterialIndices) {
                                if (idx == slot) idx = static_cast<uint32_t>(slot + 1);
                                else if (idx == slot + 1) idx = static_cast<uint32_t>(slot);
                            }
                            meshEditActiveMaterialSlot = static_cast<int>(slot + 1);
                            meshEditDirty = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X") && meshEditAsset.materialSlots.size() > 1) {
                            meshEditAsset.materialSlots.erase(meshEditAsset.materialSlots.begin() + static_cast<long>(slot));
                            for (auto& idx : meshEditAsset.faceMaterialIndices) {
                                if (idx == slot) idx = 0u;
                                else if (idx > slot) idx -= 1u;
                            }
                            meshEditActiveMaterialSlot = std::clamp(meshEditActiveMaterialSlot, 0, static_cast<int>(meshEditAsset.materialSlots.size()) - 1);
                            meshEditDirty = true;
                            ImGui::PopID();
                            break;
                        }
                        ImGui::PopID();
                    }
                    if (ImGui::Button("Add Slot")) {
                        meshEditAsset.materialSlots.push_back("Material_" + std::to_string(meshEditAsset.materialSlots.size()));
                        meshEditActiveMaterialSlot = static_cast<int>(meshEditAsset.materialSlots.size()) - 1;
                        meshEditDirty = true;
                    }

                    ImGui::Spacing();
                    if (ImGui::Button("Assign Selected Faces")) {
                        if (meshEditAsset.faceMaterialIndices.size() != meshEditAsset.faces.size()) {
                            meshEditAsset.faceMaterialIndices.resize(meshEditAsset.faces.size(), 0u);
                        }
                        for (int fi : meshEditSelectedFaces) {
                            if (fi >= 0 && fi < static_cast<int>(meshEditAsset.faceMaterialIndices.size())) {
                                meshEditAsset.faceMaterialIndices[fi] = static_cast<uint32_t>(meshEditActiveMaterialSlot);
                            }
                        }
                        meshEditDirty = true;
                        if (selectedObj) {
                            syncMeshEditToGPU(selectedObj);
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Select Faces By Slot")) {
                        meshEditSelectedFaces.clear();
                        if (meshEditAsset.faceMaterialIndices.size() != meshEditAsset.faces.size()) {
                            meshEditAsset.faceMaterialIndices.resize(meshEditAsset.faces.size(), 0u);
                        }
                        for (size_t fi = 0; fi < meshEditAsset.faceMaterialIndices.size(); ++fi) {
                            if (meshEditAsset.faceMaterialIndices[fi] == static_cast<uint32_t>(meshEditActiveMaterialSlot)) {
                                meshEditSelectedFaces.push_back(static_cast<int>(fi));
                            }
                        }
                        meshEditSelectedVertices.clear();
                        meshEditSelectedEdges.clear();
                        meshEditSelectionMode = MeshEditSelectionMode::Face;
                    }
                    ImGui::EndPopup();
                }
                if (meshEditSelectionMode == MeshEditSelectionMode::UV) {
                    ImGui::SameLine(0.0f, toolbarSpacing * 0.6f);
                    if (GizmoToolbar::TextButton("UV Move", false, ImVec2(62,24), baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
                        ImGui::OpenPopup("##mesh_edit_uv_tools");
                    }
                    if (ImGui::BeginPopup("##mesh_edit_uv_tools")) {
                        ImGui::TextUnformatted("UV Tools (selected faces)");
                        ImGui::Separator();
                        ImGui::DragFloat("Move Step", &meshEditUvMoveStep, 0.01f, -10.0f, 10.0f, "%.3f");
                        ImGui::DragFloat("Scale Step", &meshEditUvScaleStep, 0.01f, 0.01f, 10.0f, "%.3f");
                        ImGui::DragFloat("Rotate Step", &meshEditUvRotateStep, 1.0f, -180.0f, 180.0f, "%.1f");
                        ImGui::TextDisabled("Right-click in UV mode also shows these tools.");
                        ImGui::EndPopup();
                    }
                }
                ImGui::SameLine(0.0f, toolbarSpacing * 0.8f);
                ImGui::BeginDisabled(!meshEditLoaded || meshEditPath.empty());
                if (GizmoToolbar::TextButton("Save", meshEditDirty, ImVec2(52,24), baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
                    std::string err;
                    if (!saveMeshEditAsset(err)) {
                        addConsoleMessage("Mesh save failed: " + err, ConsoleMessageType::Error);
                    } else {
                        addConsoleMessage("Saved mesh: " + meshEditPath, ConsoleMessageType::Success);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(meshEditDirty ? "Save edited mesh to disk" : "Mesh is up to date");
                }
                ImGui::EndDisabled();
            }
        }
        if (!use2DGizmos) {
            ImGui::SameLine(0.0f, toolbarSpacing);
            gizmoButton("##gizmo_bounds", GizmoToolbar::Icon::Bounds, ImGuizmo::BOUNDS, "Rect scale");
            ImGui::SameLine(0.0f, toolbarSpacing);
            gizmoButton("##gizmo_universal", GizmoToolbar::Icon::Universal, ImGuizmo::UNIVERSAL, "Universal");

            ImGui::SameLine(0.0f, toolbarSpacing * 1.25f);
            if (GizmoToolbar::IconButton("##mode_local", GizmoToolbar::Icon::LocalMode, mCurrentGizmoMode == ImGuizmo::LOCAL, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
                mCurrentGizmoMode = ImGuizmo::LOCAL;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Local");
            }
            ImGui::SameLine(0.0f, toolbarSpacing * 0.8f);
            if (GizmoToolbar::IconButton("##mode_world", GizmoToolbar::Icon::WorldMode, mCurrentGizmoMode == ImGuizmo::WORLD, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
                mCurrentGizmoMode = ImGuizmo::WORLD;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("World");
            }
        }

        ImGui::SameLine(0.0f, toolbarSpacing);
        bool snapActive = use2DGizmos ? pixelGridSnapEnabled : useSnap;
        if (GizmoToolbar::IconButton("##snap_toggle", GizmoToolbar::Icon::SnapToggle, snapActive, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
            if (use2DGizmos) {
                pixelGridSnapEnabled = !pixelGridSnapEnabled;
            } else {
                useSnap = !useSnap;
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(use2DGizmos ? "Pixel snap" : "Snap");
        }

        if (use2DGizmos && pixelGridSnapEnabled) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(86.0f);
            if (ImGui::DragInt("##pixelSnapStep", &pixelGridSnapStep, 0.5f, 1, 64, "%d px")) {
                pixelGridSnapStep = std::clamp(pixelGridSnapStep, 1, 64);
            }
        } else if (!use2DGizmos && useSnap) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            if (mCurrentGizmoOperation == ImGuizmo::ROTATE) {
                ImGui::DragFloat("##snapAngle", &rotationSnapValue, 1.0f, 1.0f, 90.0f, "%.0f deg");
            } else {
                ImGui::DragFloat("##snapVal", &snapValue[0], 0.1f, 0.1f, 10.0f, "%.1f");
                snapValue[1] = snapValue[2] = snapValue[0];
            }
        }

        bool toolbarEditorSettingsChanged = false;
        ImGui::SameLine(0.0f, toolbarSpacing * 1.25f);
        if (GizmoToolbar::IconButton("##gizmo_toggle", GizmoToolbar::Icon::GizmoToggle, showSceneGizmos, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
            showSceneGizmos = !showSceneGizmos;
            toolbarEditorSettingsChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle light/camera scene symbols");
        }
        ImGui::SameLine(0.0f, toolbarSpacing * 0.35f);
        if (ImGui::ArrowButton("##scene_gizmo_settings_arrow", ImGuiDir_Down)) {
            ImGui::OpenPopup("##scene_gizmo_settings_popup");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Scene gizmo settings");
        }
        if (ImGui::BeginPopup("##scene_gizmo_settings_popup")) {
            ImGui::TextUnformatted("Scene Gizmo Settings");
            ImGui::Separator();

            if (ImGui::Checkbox("Camera Overlays", &gizmoShowCameraOverlays)) {
                toolbarEditorSettingsChanged = true;
            }
            ImGui::BeginDisabled(!gizmoShowCameraOverlays);
            if (ImGui::Checkbox("Camera Frustum Labels", &gizmoShowCameraFrustumLabels)) {
                toolbarEditorSettingsChanged = true;
            }
            ImGui::EndDisabled();

            if (ImGui::Checkbox("Light Overlays", &gizmoShowLightOverlays)) {
                toolbarEditorSettingsChanged = true;
            }
            ImGui::BeginDisabled(!gizmoShowLightOverlays);
            if (ImGui::Checkbox("Light Intensity Labels", &gizmoShowLightIntensityLabels)) {
                toolbarEditorSettingsChanged = true;
            }
            ImGui::EndDisabled();

            ImGui::Separator();
            if (ImGui::Checkbox("Viewport Hint Overlay", &showViewportHintOverlay)) {
                toolbarEditorSettingsChanged = true;
            }
            if (ImGui::Checkbox("2D Light Stats Overlay", &showLight2DStatsOverlay)) {
                toolbarEditorSettingsChanged = true;
            }

            ImGui::Separator();
            if (ImGui::SliderFloat("Gizmo Icon Size", &sceneGizmoIconScale, 0.4f, 3.0f, "%.2fx")) {
                toolbarEditorSettingsChanged = true;
            }
            if (ImGui::SliderFloat("Overlay Scale", &sceneGizmoOverlayScale, 0.4f, 3.0f, "%.2fx")) {
                toolbarEditorSettingsChanged = true;
            }
            if (ImGui::Button("Reset Gizmo Defaults")) {
                gizmoShowCameraOverlays = true;
                gizmoShowCameraFrustumLabels = true;
                gizmoShowLightOverlays = true;
                gizmoShowLightIntensityLabels = true;
                showViewportHintOverlay = true;
                showLight2DStatsOverlay = true;
                sceneGizmoIconScale = 1.0f;
                sceneGizmoOverlayScale = 1.0f;
                toolbarEditorSettingsChanged = true;
            }

            ImGui::EndPopup();
        }
        ImGui::SameLine(0.0f, toolbarSpacing * 0.8f);
        if (use2DGizmos) {
            if (GizmoToolbar::IconButton("##grid_toggle_2d", GizmoToolbar::Icon::GridToggle, showUIWorldGrid, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
                showUIWorldGrid = !showUIWorldGrid;
                toolbarEditorSettingsChanged = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Toggle 2D grid");
            }
        } else {
            if (GizmoToolbar::IconButton("##grid_toggle", GizmoToolbar::Icon::GridToggle, showSceneGrid3D, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
                showSceneGrid3D = !showSceneGrid3D;
                toolbarEditorSettingsChanged = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Toggle 3D grid");
            }
        }
        if (!project2DPipeline) {
            ImGui::SameLine(0.0f, toolbarSpacing * 0.8f);
            if (GizmoToolbar::IconButton("##ui_world_toggle", GizmoToolbar::Icon::UiWorldToggle, uiWorldMode, gizmoIconButtonSize, baseBtn, hoverBtn, activeBtn, accent, iconColor)) {
                uiWorldMode = !uiWorldMode;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Toggle 2D UI world overlay");
            }
        }
        if (toolbarEditorSettingsChanged) {
            saveEditorUserSettings();
        }

            ImGui::EndGroup();

            ImVec2 groupMin = ImGui::GetItemRectMin();
            ImVec2 groupMax = ImGui::GetItemRectMax();
            ImVec2 bgMin = ImVec2(groupMin.x - toolbarPadding, groupMin.y - toolbarPadding);
            ImVec2 bgMax = ImVec2(groupMax.x + toolbarPadding, groupMax.y + toolbarPadding);

            splitter.SetCurrentChannel(toolbarDrawList, 0);
            float rounding = 10.0f;
            toolbarDrawList->AddRectFilled(bgMin, bgMax, toolbarBg, rounding, ImDrawFlags_RoundCornersAll);
            toolbarDrawList->AddRect(bgMin, bgMax, toolbarOutline, rounding, ImDrawFlags_RoundCornersAll, 1.5f);

            splitter.Merge(toolbarDrawList);

            toolbarSizeCache = ImVec2(bgMax.x - bgMin.x, bgMax.y - bgMin.y);
            toolbarRectMin = bgMin;
            toolbarRectMax = bgMax;

        if (ImGui::IsAnyItemHovered() ||
            ImGui::IsMouseHoveringRect(toolbarRectMin, toolbarRectMax)) {
            blockSelection = true;
        }
            ImGui::PopStyleVar();
        }

        if (worldUiEditing) {
            blockSelection = true;
        }
        // RMB input routing: drag/hold enters camera-look, click-release opens context/select.
        static bool viewportRightPending = false;
        static bool viewportRightConsumedByLook = false;
        static ImVec2 viewportRightPressPos(0.0f, 0.0f);
        const float rightLookDragThreshold = 6.0f;
        const bool rightPressCandidate = mouseOverViewportImage &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
            !ImGuizmo::IsUsing() && !ImGuizmo::IsOver() &&
            !blockSelection;
        if (rightPressCandidate) {
            viewportRightPending = true;
            viewportRightConsumedByLook = false;
            viewportRightPressPos = ImGui::GetMousePos();
            viewportController.setFocused(true);
        }
        if (viewportRightPending && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            ImVec2 now = ImGui::GetMousePos();
            float dx = now.x - viewportRightPressPos.x;
            float dy = now.y - viewportRightPressPos.y;
            if (!cursorLocked && (dx * dx + dy * dy) >= (rightLookDragThreshold * rightLookDragThreshold)) {
                cursorLocked = true;
                camera.firstMouse = true;
                viewportRightConsumedByLook = true;
            }
            if (cursorLocked) {
                viewportRightConsumedByLook = true;
            }
        }

        bool rightPickRelease = false;
        if (viewportRightPending && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            ImVec2 now = ImGui::GetMousePos();
            float dx = now.x - viewportRightPressPos.x;
            float dy = now.y - viewportRightPressPos.y;
            bool isClick = (dx * dx + dy * dy) < (rightLookDragThreshold * rightLookDragThreshold);
            rightPickRelease = isClick &&
                !viewportRightConsumedByLook &&
                mouseOverViewportImage &&
                !ImGuizmo::IsUsing() && !ImGuizmo::IsOver() &&
                !blockSelection &&
                !(meshEditMode && meshEditSelectionMode != MeshEditSelectionMode::Object);
            viewportRightPending = false;
        }

        // Viewport object picking (left click select, RMB click-release for context selection)
        const bool leftPickClick = mouseOverViewportImage &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGuizmo::IsUsing() && !ImGuizmo::IsOver() &&
            !blockSelection;
        bool rightPickHitObject = false;
        if (leftPickClick || rightPickRelease)
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
            glm::mat4 invView = glm::inverse(view);
            glm::vec3 cameraPos = glm::vec3(invView[3]);
            glm::vec3 cameraUp = glm::normalize(glm::vec3(invView[1]));

            for (const auto& obj : sceneObjects) {
                glm::vec3 aabbMin(-0.5f);
                glm::vec3 aabbMax(0.5f);

                glm::mat4 model(1.0f);
                model = glm::translate(model, obj.position);
                if (obj.type == ObjectType::Sprite25D || (obj.renderType == RenderType::Sprite && obj.faceCamera)) {
                    glm::vec3 forward = cameraPos - obj.position;
                    if (glm::dot(forward, forward) < 1e-6f) forward = glm::vec3(0.0f, 0.0f, 1.0f);
                    else forward = glm::normalize(forward);
                    glm::vec3 right = glm::cross(cameraUp, forward);
                    if (glm::dot(right, right) < 1e-6f) {
                        right = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), forward);
                    }
                    right = glm::normalize(right);
                    glm::vec3 up = glm::normalize(glm::cross(forward, right));
                    glm::vec3 scale = glm::max(glm::abs(obj.scale), glm::vec3(0.0001f));
                    model[0] = glm::vec4(right * scale.x * (obj.scale.x < 0.0f ? -1.0f : 1.0f), 0.0f);
                    model[1] = glm::vec4(up * scale.y * (obj.scale.y < 0.0f ? -1.0f : 1.0f), 0.0f);
                    model[2] = glm::vec4(forward * scale.z * (obj.scale.z < 0.0f ? -1.0f : 1.0f), 0.0f);
                } else {
                    model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1, 0, 0));
                    model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
                    model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0, 0, 1));
                    model = glm::scale(model, obj.scale);
                }

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
                    case ObjectType::Plane:
                        hit = rayAabb(localOrigin, localDir, glm::vec3(-0.5f, -0.5f, -0.02f), glm::vec3(0.5f, 0.5f, 0.02f), hitT);
                        break;
                    case ObjectType::Mirror:
                        hit = rayAabb(localOrigin, localDir, glm::vec3(-0.5f, -0.5f, -0.02f), glm::vec3(0.5f, 0.5f, 0.02f), hitT);
                        break;
                    case ObjectType::Sprite:
                    case ObjectType::Sprite25D:
                        hit = rayAabb(localOrigin, localDir, glm::vec3(-0.5f, -0.5f, -0.02f), glm::vec3(0.5f, 0.5f, 0.02f), hitT);
                        break;
                    case ObjectType::Torus:
                        hit = raySphere(localOrigin, localDir, 0.5f, hitT);
                        break;
                    case ObjectType::Sprite2D:
                    case ObjectType::Canvas:
                    case ObjectType::UIImage:
                    case ObjectType::UISlider:
                    case ObjectType::UIButton:
                    case ObjectType::UIText:
                        hit = false;
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
                    case ObjectType::Empty:
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
                if (leftPickClick) {
                    bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
                    setPrimarySelection(hitId, additive);
                } else {
                    setPrimarySelection(hitId, false);
                    rightPickHitObject = true;
                }
            } else if (leftPickClick) {
                clearSelection();
            }
        }

        selectedObj = getSelectedObject();
        const bool selectedMeshContextObject = selectedObj &&
            selectedObj->hasRenderer &&
            (selectedObj->renderType == RenderType::Model ||
             selectedObj->renderType == RenderType::OBJMesh ||
             IsRawMeshPath(selectedObj->meshPath)) &&
            (!meshEditMode || meshEditSelectionMode == MeshEditSelectionMode::Object);
        const bool meshObjectContextMode =
            selectedMeshContextObject;

        if (meshObjectContextMode &&
            rightPickHitObject &&
            !cursorLocked &&
            !ImGuizmo::IsUsing() && !ImGuizmo::IsOver()) {
            ImGui::OpenPopup("##mesh_object_context_menu");
        }

        if (ImGui::BeginPopup("##mesh_object_context_menu")) {
            if (selectedObj) {
                if (ImGui::MenuItem("Delete Object")) {
                    setPrimarySelection(selectedObj->id);
                    deleteSelected();
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Reset Object")) {
                    recordState("meshObjectReset");
                    selectedObj->position = glm::vec3(0.0f);
                    selectedObj->rotation = glm::vec3(0.0f);
                    selectedObj->scale = glm::vec3(1.0f);
                    updateHierarchyWorldTransforms();
                    projectManager.currentProject.hasUnsavedChanges = true;
                    addConsoleMessage("Mesh object reset", ConsoleMessageType::Success);
                }
                if (ImGui::MenuItem("Duplicate Object")) {
                    setPrimarySelection(selectedObj->id);
                    duplicateSelected();
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Convert to RMesh")) {
                    if (!selectedObj->meshPath.empty()) {
                        fs::path src = selectedObj->meshPath;
                        fs::path outPath = src;
                        outPath.replace_extension(".rmesh");
                        std::string err;
                        if (getModelLoader().exportRawMesh(src.string(), outPath.string(), err)) {
                            ModelLoadResult converted = getModelLoader().loadModel(outPath.string());
                            if (converted.success && converted.meshIndex >= 0) {
                                selectedObj->hasRenderer = true;
                                selectedObj->renderType = RenderType::Model;
                                selectedObj->type = ObjectType::Model;
                                selectedObj->meshPath = outPath.string();
                                selectedObj->meshId = converted.meshIndex;
                                meshEditPath.clear();
                                meshEditLoaded = false;
                                meshEditDirty = false;
                                fileBrowser.needsRefresh = true;
                                projectManager.currentProject.hasUnsavedChanges = true;
                                addConsoleMessage("Converted object to RMesh: " + outPath.string(), ConsoleMessageType::Success);
                            } else {
                                addConsoleMessage("Convert to RMesh failed: " + converted.errorMessage, ConsoleMessageType::Error);
                            }
                        } else {
                            addConsoleMessage("Convert to RMesh failed: " + err, ConsoleMessageType::Error);
                        }
                    }
                }
                if (ImGui::MenuItem("Rebuild Mesh Data")) {
                    if (IsRawMeshPath(selectedObj->meshPath)) {
                        RawMeshAsset rebuilt;
                        std::string err;
                        if (getModelLoader().loadRawMesh(selectedObj->meshPath, rebuilt, err)) {
                            if (selectedObj->meshId < 0) {
                                ModelLoadResult loaded = getModelLoader().loadModel(selectedObj->meshPath);
                                if (loaded.success) {
                                    selectedObj->meshId = loaded.meshIndex;
                                }
                            }
                            if (selectedObj->meshId >= 0 && getModelLoader().updateRawMesh(selectedObj->meshId, rebuilt, err)) {
                                addConsoleMessage("Rebuilt RMesh data for object", ConsoleMessageType::Success);
                            } else {
                                addConsoleMessage("Rebuild mesh data failed: " + err, ConsoleMessageType::Error);
                            }
                        } else {
                            addConsoleMessage("Rebuild mesh data failed: " + err, ConsoleMessageType::Error);
                        }
                    }
                }
            }
            ImGui::EndPopup();
        }

        if (cursorLocked && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            cursorLocked = false;
            camera.firstMouse = true;
        }
        if (cursorLocked) {
            viewportController.setFocused(true);
        }

        if (isPlaying && showViewOutput && rendererInitialized) {
            static unsigned int cachedViewOutputTex = 0;
            static int cachedViewOutputWidth = 0;
            static int cachedViewOutputHeight = 0;
            static int cachedViewOutputCameraId = -1;
            static double nextViewOutputRefreshTime = 0.0;
            constexpr double kViewOutputRefreshInterval = 1.0 / 30.0;

            std::vector<const SceneObject*> playerCams;
            for (const auto& obj : sceneObjects) {
                if (obj.hasCamera && obj.camera.type == SceneCameraType::Player) {
                    playerCams.push_back(&obj);
                }
            }

            if (playerCams.empty()) {
                previewCameraId = -1;
                cachedViewOutputTex = 0;
                cachedViewOutputCameraId = -1;
                nextViewOutputRefreshTime = 0.0;
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
                const double now = glfwGetTime();
                const bool sizeChanged =
                    previewWidth != cachedViewOutputWidth || previewHeight != cachedViewOutputHeight;
                const bool cameraChanged = previewCam->id != cachedViewOutputCameraId;
                const bool forceRefresh =
                    cachedViewOutputTex == 0 || sizeChanged || cameraChanged || now >= nextViewOutputRefreshTime;

                if (forceRefresh) {
                    cachedViewOutputTex = renderer.renderScenePreview(
                        makeCameraFromObject(*previewCam),
                        sceneObjects,
                        previewWidth,
                        previewHeight,
                        previewCam->camera.fov,
                        previewCam->camera.nearClip,
                        previewCam->camera.farClip,
                        previewCam->camera.applyPostFX
                    );
                    cachedViewOutputWidth = previewWidth;
                    cachedViewOutputHeight = previewHeight;
                    cachedViewOutputCameraId = previewCam->id;
                    nextViewOutputRefreshTime = now + kViewOutputRefreshInterval;
                }
                unsigned int previewTex = cachedViewOutputTex;

                if (previewTex != 0) {
                    ImVec2 overlaySize(previewWidth + 20.0f, previewHeight + 64.0f);
                    ImVec2 overlayPos = ImVec2(imageMax.x - overlaySize.x - 12.0f, imageMax.y - overlaySize.y - 12.0f);
                    ImVec2 winPos = ImGui::GetWindowPos();
                    ImVec2 localPos = ImVec2(overlayPos.x - winPos.x, overlayPos.y - winPos.y);
                    ImGui::SetCursorPos(localPos);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
                    ImGui::BeginChild("ViewOutputOverlay", overlaySize, true, ImGuiWindowFlags_NoScrollbar);
                    ImGui::TextDisabled("View Output");
                    ImGuiID comboId = ImGui::GetID("##ViewOutputCamera");
                    UIAnimationState& comboAnim = editorUiAnimationStates[comboId];
                    float comboAnimSpeed = 0.0f;
                    if (uiAnimationMode == UIAnimationMode::Fluid) {
                        comboAnimSpeed = 8.0f;
                    } else if (uiAnimationMode == UIAnimationMode::Snappy) {
                        comboAnimSpeed = 18.0f;
                    }
                    float comboAnimStep = (uiAnimationMode == UIAnimationMode::Off) ? 1.0f
                        : (1.0f - std::exp(-comboAnimSpeed * ImGui::GetIO().DeltaTime));
                    bool comboOpen = ImGui::IsPopupOpen(comboId, ImGuiPopupFlags_None);
                    if (uiAnimationMode == UIAnimationMode::Off) {
                        comboAnim.active = comboOpen ? 1.0f : 0.0f;
                    } else {
                        float target = comboOpen ? 1.0f : 0.0f;
                        comboAnim.active += (target - comboAnim.active) * comboAnimStep;
                    }
                    ImGui::SetNextWindowBgAlpha(0.85f * std::clamp(comboAnim.active, 0.0f, 1.0f));
                    if (ImGui::BeginCombo("##ViewOutputCamera", previewCam->name.c_str())) {
                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::clamp(comboAnim.active, 0.0f, 1.0f));
                        for (const auto* cam : playerCams) {
                            bool selected = cam->id == previewCameraId;
                            if (ImGui::Selectable(cam->name.c_str(), selected)) {
                                previewCameraId = cam->id;
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::PopStyleVar();
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

        viewportDrawList->PopClipRect();
    }

    // Draw viewport hint/status on the foreground layer so it always stays above scene sprites/gizmos.
    if (hasViewportImageRect && showViewportHintOverlay) {
        ImDrawList* fg = ImGui::GetForegroundDrawList(ImGui::GetWindowViewport());
        fg->PushClipRect(viewportImageMin, viewportImageMax, true);
        const char* hintText = worldUiEditing
            ? "MMB/Space+LMB: Pan | Wheel: Zoom | LMB: Select | Gizmo: Move/Rotate/Scale"
            : "Hold RMB: Look & Move | LMB: Select | WASD+QE: Move | ESC: Release | F11: Fullscreen";
        const ImU32 hintShadow = IM_COL32(0, 0, 0, 180);
        const ImU32 hintColor = worldUiEditing
            ? IM_COL32(228, 236, 246, 196)
            : IM_COL32(228, 236, 246, 172);
        ImVec2 hintPos(viewportImageMin.x + 10.0f, viewportImageMin.y + 10.0f);
        fg->AddText(ImVec2(hintPos.x + 1.0f, hintPos.y + 1.0f), hintShadow, hintText);
        fg->AddText(hintPos, hintColor, hintText);

        if (cursorLocked) {
            ImVec2 statusPos(viewportImageMin.x + 10.0f, viewportImageMin.y + 30.0f);
            fg->AddText(ImVec2(statusPos.x + 1.0f, statusPos.y + 1.0f), hintShadow, "Freelook Active");
            fg->AddText(statusPos, IM_COL32(120, 255, 120, 255), "Freelook Active");
        } else if (viewportController.isViewportFocused()) {
            ImVec2 statusPos(viewportImageMin.x + 10.0f, viewportImageMin.y + 30.0f);
            fg->AddText(ImVec2(statusPos.x + 1.0f, statusPos.y + 1.0f), hintShadow, "Viewport Focused");
            fg->AddText(statusPos, IM_COL32(180, 226, 255, 255), "Viewport Focused");
        }
        fg->PopClipRect();
    }

    bool windowFocused = ImGui::IsWindowFocused();
    viewportController.updateFocusFromImGui(windowFocused, cursorLocked);

    ImGui::End();

    // Run dock drawer interactions after docked windows are submitted so tab hit-rects are current.
    if (!(pendingWorkspaceReload || workspaceLayoutDirty || glfwGetTime() < workspaceLayoutStabilizeUntil)) {
        updateDockDrawerAnimations();
    }
}
#pragma endregion

#pragma region 3D UI Canvas Targets
void Engine::renderUiCanvas3DTargets() {
    if (!rendererInitialized || !projectManager.currentProject.isLoaded) return;

    ImGuiContext* mainContext = ImGui::GetCurrentContext();
    if (!mainContext) return;

    ImGuiStyle mainStyle = ImGui::GetStyle();
    ImGuiIO& mainIo = ImGui::GetIO();

    std::unordered_map<int, SceneObject*> byId;
    byId.reserve(sceneObjects.size());
    for (auto& obj : sceneObjects) {
        byId[obj.id] = &obj;
    }

    auto isUiType = [](const SceneObject& target) {
        return target.hasUI && target.ui.type != UIElementType::None;
    };

    for (auto& obj : sceneObjects) {
        if (obj.hasUI && obj.ui.type == UIElementType::Canvas && !obj.ui.renderIn3D) {
            if (obj.hasRenderer && obj.renderType == RenderType::Sprite) {
                obj.hasRenderer = false;
                obj.renderType = RenderType::None;
            }
        }
    }

    auto findCanvasRoot = [&](const SceneObject& obj) -> const SceneObject* {
        const SceneObject* current = &obj;
        const SceneObject* found = nullptr;
        while (current) {
            if (current->hasUI && current->ui.type == UIElementType::Canvas) {
                found = current;
            }
            if (current->parentId < 0) break;
            auto it = byId.find(current->parentId);
            if (it == byId.end()) break;
            current = it->second;
        }
        return found;
    };

    auto isOffscreenCanvas = [](const SceneObject& canvas) {
        return canvas.enabled &&
               canvas.hasUI &&
               canvas.ui.type == UIElementType::Canvas &&
               (canvas.ui.renderIn3D || (canvas.ui.pseudo3DEnabled && canvas.ui.pseudo3DUseOffscreenSurface));
    };

    std::unordered_set<int> activeCanvasIds;
    for (auto& canvas : sceneObjects) {
        if (!isOffscreenCanvas(canvas)) continue;
        activeCanvasIds.insert(canvas.id);

        if (canvas.ui.renderIn3D) {
            canvas.hasRenderer = true;
            canvas.renderType = RenderType::Sprite;
            canvas.material.textureMix = 1.0f;
        } else if (canvas.hasRenderer && canvas.renderType == RenderType::Sprite) {
            canvas.hasRenderer = false;
            canvas.renderType = RenderType::None;
        }
        if (canvas.ui.pseudo3DEnabled && !canvas.ui.renderIn3D) {
            canvas.faceCamera = false;
        }

        const glm::vec2 pseudoLayout = ResolvePseudo3DLayoutSize(canvas);
        const bool pseudoCanvas = !canvas.ui.renderIn3D && canvas.ui.pseudo3DEnabled;
        const float layoutWidth = pseudoCanvas ? pseudoLayout.x : std::max(1.0f, canvas.ui.size.x);
        const float layoutHeight = pseudoCanvas ? pseudoLayout.y : std::max(1.0f, canvas.ui.size.y);
        int targetWidth = (canvas.ui.renderTargetSize.x > 0) ? canvas.ui.renderTargetSize.x : static_cast<int>(layoutWidth);
        int targetHeight = (canvas.ui.renderTargetSize.y > 0) ? canvas.ui.renderTargetSize.y : static_cast<int>(layoutHeight);
        targetWidth = std::clamp(targetWidth, 16, 4096);
        targetHeight = std::clamp(targetHeight, 16, 4096);

        Renderer::UiTargetInfo target = renderer.ensureUiTarget(canvas.id, targetWidth, targetHeight);
        if (target.fbo == 0 || target.texture == 0) continue;

        UiCanvas3DContext& ctxEntry = uiCanvas3DContexts[canvas.id];
        if (!ctxEntry.context) {
            ctxEntry.context = ImGui::CreateContext();
            ImGui::SetCurrentContext(ctxEntry.context);
            ImGuiIO& io = ImGui::GetIO();
            std::string fontReport;
            ImFont* canvasFont = loadModularityUiFont(io, 15.5f, &fontReport);
            if (canvasFont) {
                io.FontDefault = canvasFont;
            } else {
                io.Fonts->AddFontDefault();
                if (!fontReport.empty()) {
                    std::cerr << "[WARN] UI canvas font load failed: " << fontReport << std::endl;
                }
            }
            ImGui_ImplOpenGL3_Init("#version 330");
            ImGui_ImplOpenGL3_CreateDeviceObjects();
            ctxEntry.backendReady = true;
        }

        ImGui::SetCurrentContext(ctxEntry.context);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(layoutWidth, layoutHeight);
        io.DisplayFramebufferScale = ImVec2(
            (layoutWidth > 0.0f) ? (static_cast<float>(targetWidth) / layoutWidth) : 1.0f,
            (layoutHeight > 0.0f) ? (static_cast<float>(targetHeight) / layoutHeight) : 1.0f
        );
        io.DeltaTime = (mainIo.DeltaTime > 0.0f) ? mainIo.DeltaTime : (1.0f / 60.0f);
        auto inputIt = uiCanvas3DInputs.find(canvas.id);
        if (inputIt != uiCanvas3DInputs.end() && inputIt->second.hasInput) {
            io.MousePos = inputIt->second.mousePos;
            io.MouseDown[0] = inputIt->second.mouseDown[0];
            io.MouseDown[1] = inputIt->second.mouseDown[1];
            io.MouseDown[2] = inputIt->second.mouseDown[2];
            io.MouseWheel = inputIt->second.mouseWheel;
        } else {
            io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
            io.MouseDown[0] = false;
            io.MouseDown[1] = false;
            io.MouseDown[2] = false;
            io.MouseWheel = 0.0f;
        }

        ImGui::GetStyle() = mainStyle;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(layoutWidth, layoutHeight));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoBackground;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin("##Canvas3D", nullptr, flags);

        auto anchorToPivot = [](UIAnchor anchor, const ImVec2& size) {
            switch (anchor) {
                case UIAnchor::Center: return ImVec2(size.x * 0.5f, size.y * 0.5f);
                case UIAnchor::TopLeft: return ImVec2(0.0f, 0.0f);
                case UIAnchor::TopRight: return ImVec2(size.x, 0.0f);
                case UIAnchor::BottomLeft: return ImVec2(0.0f, size.y);
                case UIAnchor::BottomRight: return ImVec2(size.x, size.y);
                default: return ImVec2(size.x * 0.5f, size.y * 0.5f);
            }
        };
        auto anchorToPoint = [](UIAnchor anchor, const ImVec2& min, const ImVec2& max) {
            switch (anchor) {
                case UIAnchor::Center: return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
                case UIAnchor::TopLeft: return min;
                case UIAnchor::TopRight: return ImVec2(max.x, min.y);
                case UIAnchor::BottomLeft: return ImVec2(min.x, max.y);
                case UIAnchor::BottomRight: return max;
                default: return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            }
        };
        auto resolveUIRect = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax) {
            std::vector<const SceneObject*> chain;
            const SceneObject* current = &obj;
            while (current) {
                if (isUiType(*current) && current->id != canvas.id) {
                    chain.push_back(current);
                }
                if (current->parentId < 0) break;
                auto it = byId.find(current->parentId);
                if (it == byId.end()) break;
                current = it->second;
                if (current->id == canvas.id) break;
            }
            std::reverse(chain.begin(), chain.end());

            ImVec2 regionMin = ImGui::GetWindowPos();
            ImVec2 regionMax = ImVec2(regionMin.x + layoutWidth, regionMin.y + layoutHeight);
            for (const SceneObject* node : chain) {
                glm::vec2 nodeSize = getSpriteDisplaySize(*node);
                ImVec2 size = ImVec2(std::max(1.0f, nodeSize.x),
                                     std::max(1.0f, nodeSize.y));
                ImVec2 anchorPoint = anchorToPoint(node->ui.anchor, regionMin, regionMax);
                ImVec2 pivot(anchorPoint.x + node->ui.position.x,
                             anchorPoint.y + node->ui.position.y);
                ImVec2 pivotOffset = anchorToPivot(node->ui.anchor, size);
                regionMin = ImVec2(pivot.x - pivotOffset.x, pivot.y - pivotOffset.y);
                regionMax = ImVec2(regionMin.x + size.x, regionMin.y + size.y);
            }
            outMin = regionMin;
            outMax = regionMax;
        };

        auto brighten = [](const ImVec4& c, float k) {
            return ImVec4(std::clamp(c.x * k, 0.0f, 1.0f),
                          std::clamp(c.y * k, 0.0f, 1.0f),
                          std::clamp(c.z * k, 0.0f, 1.0f),
                          c.w);
        };
        float animSpeed = 0.0f;
        if (uiAnimationMode == UIAnimationMode::Fluid) {
            animSpeed = 8.0f;
        } else if (uiAnimationMode == UIAnimationMode::Snappy) {
            animSpeed = 18.0f;
        }
        float animStep = (uiAnimationMode == UIAnimationMode::Off) ? 1.0f
            : (1.0f - std::exp(-animSpeed * ImGui::GetIO().DeltaTime));
        auto animateValue = [&](float& current, float target, bool immediate) {
            if (uiAnimationMode == UIAnimationMode::Off || immediate) {
                current = target;
            } else {
                current += (target - current) * animStep;
            }
            return current;
        };
        BatchedSpriteEmitter spriteBatch(ImGui::GetWindowDrawList());
        auto resolveCanvasMaskRectForObject = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax) -> bool {
            bool hasMask = false;
            ImVec2 maskMin(0.0f, 0.0f);
            ImVec2 maskMax(0.0f, 0.0f);
            const SceneObject* current = &obj;
            while (current && current->parentId >= 0) {
                auto parentIt = byId.find(current->parentId);
                if (parentIt == byId.end()) break;
                current = parentIt->second;
                if (!(current->hasUI && current->ui.type == UIElementType::Canvas && current->ui.maskChildren)) {
                    continue;
                }

                ImVec2 canvasMin, canvasMax;
                resolveUIRect(*current, canvasMin, canvasMax);
                if (!hasMask) {
                    maskMin = canvasMin;
                    maskMax = canvasMax;
                    hasMask = true;
                } else {
                    maskMin.x = std::max(maskMin.x, canvasMin.x);
                    maskMin.y = std::max(maskMin.y, canvasMin.y);
                    maskMax.x = std::min(maskMax.x, canvasMax.x);
                    maskMax.y = std::min(maskMax.y, canvasMax.y);
                }
            }
            if (!hasMask) return false;
            outMin = maskMin;
            outMax = maskMax;
            return (outMax.x > outMin.x) && (outMax.y > outMin.y);
        };

        for (auto& obj : sceneObjects) {
            if (!IsObjectEnabledInHierarchy(obj) || !isUiType(obj)) continue;
            const SceneObject* root = findCanvasRoot(obj);
            if (!root || root->id != canvas.id) continue;
            if (obj.ui.type == UIElementType::Canvas) continue;

            ImVec2 rectMin, rectMax;
            resolveUIRect(obj, rectMin, rectMax);
            ImVec2 rectSize(rectMax.x - rectMin.x, rectMax.y - rectMin.y);
            if (rectSize.x <= 1.0f || rectSize.y <= 1.0f) continue;

            ImGuiStyle savedStyle;
            bool styleApplied = false;
            if (!obj.ui.stylePreset.empty()) {
                if (const auto* preset = getUIStylePreset(obj.ui.stylePreset)) {
                    savedStyle = ImGui::GetStyle();
                    ImGui::GetStyle() = preset->style;
                    styleApplied = true;
                }
            }

            ImVec2 drawMin = rectMin;
            ImVec2 drawMax = rectMax;
            ImVec2 drawSize(drawMax.x - drawMin.x, drawMax.y - drawMin.y);
            ImVec2 localMin(drawMin.x - ImGui::GetWindowPos().x, drawMin.y - ImGui::GetWindowPos().y);
            bool pushedCanvasMask = false;
            if (obj.ui.type != UIElementType::Canvas) {
                ImVec2 maskMin, maskMax;
                if (resolveCanvasMaskRectForObject(obj, maskMin, maskMax)) {
                    maskMin.x = std::max(maskMin.x, ImGui::GetWindowPos().x);
                    maskMin.y = std::max(maskMin.y, ImGui::GetWindowPos().y);
                    maskMax.x = std::min(maskMax.x, ImGui::GetWindowPos().x + layoutWidth);
                    maskMax.y = std::min(maskMax.y, ImGui::GetWindowPos().y + layoutHeight);
                    if (maskMax.x <= maskMin.x || maskMax.y <= maskMin.y) {
                        if (styleApplied) ImGui::GetStyle() = savedStyle;
                        continue;
                    }
                    if (drawMax.x <= maskMin.x || drawMin.x >= maskMax.x ||
                        drawMax.y <= maskMin.y || drawMin.y >= maskMax.y) {
                        if (styleApplied) ImGui::GetStyle() = savedStyle;
                        continue;
                    }
                    spriteBatch.flush();
                    ImGui::PushClipRect(maskMin, maskMax, true);
                    pushedCanvasMask = true;
                }
            }

            ImGui::PushID(obj.id);
            UIAnimationState& animState = uiAnimationStates[obj.id];
            if (!animState.initialized) {
                animState.sliderValue = obj.ui.sliderValue;
                animState.initialized = true;
            }
            if (obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D) {
                Texture* spriteTex = nullptr;
                unsigned int texId = 0;
                if (rendererInitialized && !obj.albedoTexturePath.empty()) {
                    spriteTex = renderer.getTexture(obj.albedoTexturePath, MaterialProperties::TextureFilter::Point);
                    if (spriteTex != nullptr) {
                        texId = spriteTex->GetID();
                    }
                }
                std::array<ImVec2, 4> uvQuad = buildSpriteSheetUvs(obj);
                const int frame = resolveSpriteSheetFrame(obj);
                const ImVec2 sourceFrameSizePx = ResolveUiSourceFrameSizePx(obj, frame, spriteTex);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                const ImU32 tintColor = ImGui::GetColorU32(tint);
                float angle = glm::radians(obj.ui.rotation);
                if (DrawNineSliceSprite(spriteBatch,
                                        (ImTextureID)(intptr_t)texId,
                                        obj,
                                        drawMin,
                                        drawMax,
                                        uvQuad,
                                        sourceFrameSizePx,
                                        angle,
                                        tintColor)) {
                } else if (std::abs(angle) > 1e-4f) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 center = ImVec2((drawMin.x + drawMax.x) * 0.5f, (drawMin.y + drawMax.y) * 0.5f);
                    ImVec2 half = ImVec2(drawSize.x * 0.5f, drawSize.y * 0.5f);
                    float c = std::cos(angle);
                    float s = std::sin(angle);
                    auto rotPt = [&](float x, float y) {
                        return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
                    };
                    ImVec2 p0 = rotPt(-half.x, -half.y);
                    ImVec2 p1 = rotPt( half.x, -half.y);
                    ImVec2 p2 = rotPt( half.x,  half.y);
                    ImVec2 p3 = rotPt(-half.x,  half.y);
                    if (texId != 0) {
                        spriteBatch.push((ImTextureID)(intptr_t)texId,
                                         p0, p1, p2, p3,
                                         uvQuad[0], uvQuad[1], uvQuad[2], uvQuad[3],
                                         tintColor);
                    } else {
                        spriteBatch.flush();
                        ImU32 fill = tintColor;
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        dl->AddQuadFilled(p0, p1, p2, p3, fill);
                        dl->AddQuad(p0, p1, p2, p3, border, 2.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                        dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                    }
                } else {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    if (texId != 0) {
                        spriteBatch.push((ImTextureID)(intptr_t)texId,
                                         drawMin,
                                         ImVec2(drawMax.x, drawMin.y),
                                         drawMax,
                                         ImVec2(drawMin.x, drawMax.y),
                                         uvQuad[0],
                                         ImVec2(uvQuad[2].x, uvQuad[0].y),
                                         uvQuad[2],
                                         ImVec2(uvQuad[0].x, uvQuad[2].y),
                                         tintColor);
                    } else {
                        spriteBatch.flush();
                        ImU32 fill = tintColor;
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        dl->AddRectFilled(drawMin, drawMax, fill, 6.0f);
                        dl->AddRect(drawMin, drawMax, border, 6.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                    }
                }
            } else if (obj.ui.type == UIElementType::Slider) {
                spriteBatch.flush();
                ImGui::SetCursorPos(localMin);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                if (obj.ui.sliderStyle == UISliderStyle::ImGui) {
                    ImGui::PushItemWidth(drawSize.x);
                    ImGui::BeginDisabled(!obj.ui.interactable);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, brighten(tint, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, brighten(tint, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrab, brighten(tint, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, brighten(tint, 1.1f));
                    ImGui::SliderFloat(obj.ui.label.c_str(), &obj.ui.sliderValue, obj.ui.sliderMin, obj.ui.sliderMax);
                    ImGui::PopStyleColor(5);
                    ImGui::EndDisabled();
                    ImGui::PopItemWidth();
                } else {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 bg = ImGui::GetColorU32(ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                    ImU32 fill = ImGui::GetColorU32(tint);
                    ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                    float minValue = obj.ui.sliderMin;
                    float maxValue = obj.ui.sliderMax;
                    float range = (maxValue - minValue);
                    if (range <= 1e-6f) range = 1.0f;
                    ImGui::BeginDisabled(!obj.ui.interactable);
                    ImGui::InvisibleButton("##UISlider", drawSize);
                    bool held = obj.ui.interactable && ImGui::IsItemActive();
                    if (held && ImGui::IsMouseDown(ImGuiMouseButton_Left) && drawSize.x > 1.0f) {
                        float mouseT = (ImGui::GetIO().MousePos.x - drawMin.x) / drawSize.x;
                        mouseT = std::clamp(mouseT, 0.0f, 1.0f);
                        float newValue = minValue + mouseT * range;
                        obj.ui.sliderValue = newValue;
                    }
                    ImGui::EndDisabled();

                    animateValue(animState.sliderValue, obj.ui.sliderValue, held);
                    float displayValue = (uiAnimationMode == UIAnimationMode::Off) ? obj.ui.sliderValue : animState.sliderValue;
                    float t = (displayValue - minValue) / range;
                    t = std::clamp(t, 0.0f, 1.0f);

                    if (obj.ui.sliderStyle == UISliderStyle::Fill) {
                        float rounding = 6.0f;
                        ImVec2 fillMax(drawMin.x + drawSize.x * t, drawMax.y);
                        dl->AddRectFilled(drawMin, drawMax, bg, rounding);
                        if (fillMax.x > drawMin.x) {
                            dl->AddRectFilled(drawMin, fillMax, fill, rounding);
                        }
                        dl->AddRect(drawMin, drawMax, border, rounding);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    } else if (obj.ui.sliderStyle == UISliderStyle::Circle) {
                        ImVec2 center((drawMin.x + drawMax.x) * 0.5f, (drawMin.y + drawMax.y) * 0.5f);
                        float radius = std::max(2.0f, std::min(drawSize.x, drawSize.y) * 0.5f - 2.0f);
                        dl->AddCircleFilled(center, radius, bg, 32);
                        float start = -IM_PI * 0.5f;
                        float end = start + t * IM_PI * 2.0f;
                        dl->PathClear();
                        dl->PathArcTo(center, radius, start, end, 32);
                        dl->PathLineTo(center);
                        dl->PathFillConvex(fill);
                        dl->AddCircle(center, radius, border, 32, 2.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    }
                }
            } else if (obj.ui.type == UIElementType::Button) {
                spriteBatch.flush();
                ImGui::SetCursorPos(localMin);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                obj.ui.buttonPressed = false;
                if (obj.ui.buttonStyle == UIButtonStyle::ImGui) {
                    ImGui::PushStyleColor(ImGuiCol_Button, tint);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brighten(tint, 1.1f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, brighten(tint, 1.2f));
                    ImGui::BeginDisabled(!obj.ui.interactable);
                    obj.ui.buttonPressed = ImGui::Button(obj.ui.label.c_str(), drawSize);
                    ImGui::EndDisabled();
                    ImGui::PopStyleColor(3);
                } else if (obj.ui.buttonStyle == UIButtonStyle::Outline) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 border = ImGui::GetColorU32(tint);
                    ImGui::BeginDisabled(!obj.ui.interactable);
                    if (ImGui::InvisibleButton("##UIButton", drawSize)) {
                        obj.ui.buttonPressed = obj.ui.interactable;
                    }
                    bool hovered = ImGui::IsItemHovered();
                    bool active = ImGui::IsItemActive();
                    ImGui::EndDisabled();
                    float hoverT = animateValue(animState.hover, hovered ? 1.0f : 0.0f, false);
                    float activeT = animateValue(animState.active, active ? 1.0f : 0.0f, false);
                    if (hoverT > 0.001f) {
                        ImVec4 hoverCol = brighten(tint, 0.45f);
                        hoverCol.w *= std::clamp(hoverT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(hoverCol), 6.0f);
                    }
                    if (activeT > 0.001f) {
                        ImVec4 activeCol = brighten(tint, 0.65f);
                        activeCol.w *= std::clamp(activeT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(activeCol), 6.0f);
                    }
                    dl->AddRect(drawMin, drawMax, border, 6.0f, 0, 2.0f);
                    ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                    ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                   drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                    dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                }
            } else if (obj.ui.type == UIElementType::Text) {
                spriteBatch.flush();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                float scale = std::max(0.1f, obj.ui.textScale);
                float fontSize = std::max(1.0f, ImGui::GetFontSize() * scale);
                ImGui::PushClipRect(drawMin, drawMax, true);
                AddUITextWithFilter(dl,
                                    obj.material.textureFilter,
                                    ImGui::GetFont(),
                                    fontSize,
                                    drawMin,
                                    drawMax,
                                    ImGui::GetColorU32(tint),
                                    obj.ui.label.c_str(),
                                    obj.ui.textAutoWrap,
                                    obj.ui.textHAlign,
                                    obj.ui.textVAlign,
                                    obj.ui.textEffectFlags,
                                    obj.ui.textEffectSpeed,
                                    obj.ui.textEffectIntensity);
                ImGui::PopClipRect();
            }
            if (pushedCanvasMask) {
                spriteBatch.flush();
                ImGui::PopClipRect();
            }
            ImGui::PopID();
            if (styleApplied) ImGui::GetStyle() = savedStyle;
        }
        spriteBatch.flush();

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::Render();

        GLint prevFbo = 0;
        GLint prevViewport[4] = {};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glGetIntegerv(GL_VIEWPORT, prevViewport);

        glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
        glViewport(0, 0, target.width, target.height);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    }

    for (auto it = uiCanvas3DContexts.begin(); it != uiCanvas3DContexts.end(); ) {
        if (activeCanvasIds.find(it->first) == activeCanvasIds.end()) {
            if (it->second.context) {
                ImGui::SetCurrentContext(it->second.context);
                if (it->second.backendReady) {
                    ImGui_ImplOpenGL3_Shutdown();
                }
                ImGui::DestroyContext(it->second.context);
            }
            it = uiCanvas3DContexts.erase(it);
        } else {
            ++it;
        }
    }
    renderer.cleanupUiTargets(activeCanvasIds);

    ImGui::SetCurrentContext(mainContext);
}
#pragma endregion

#pragma region Player Viewport
void Engine::renderPlayerViewport() {
    const auto runtimeUiStart = std::chrono::steady_clock::now();
    double runtimeSpriteBatchBuildMs = 0.0;
    uint32_t runtimeVisibleObjectCount = 0;
    int runtimeRenderWidth = kRuntimeInternalWidth;
    int runtimeRenderHeight = kRuntimeInternalHeight;
    getRuntimeInternalResolution(runtimeRenderWidth, runtimeRenderHeight);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    if (playerMode && isPlaying && gameViewCursorLocked) {
        ImGui::SetNextWindowFocus();
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("PlayerViewport", nullptr, flags);
    ImGui::PopStyleVar();

    bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const ImVec2 availableSize = ImGui::GetContentRegionAvail();
    const ImVec2 imageSize = ComputeAspectFitSize(availableSize, getRuntimeInternalAspect());
    const ImVec2 cursorStart = ImGui::GetCursorPos();
    const float imageOffsetX = std::max(0.0f, (availableSize.x - imageSize.x) * 0.5f);
    const float imageOffsetY = std::max(0.0f, (availableSize.y - imageSize.y) * 0.5f);
    ImGui::SetCursorPos(ImVec2(cursorStart.x + imageOffsetX, cursorStart.y + imageOffsetY));

    if (rendererInitialized) {
        unsigned int tex = renderer.getViewportTexture();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
        ImGui::InvisibleButton("PlayerViewportFrame", imageSize);
        ImGui::PopStyleColor(3);
        ImVec2 imageMin = ImGui::GetItemRectMin();
        ImVec2 imageMax = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(imageMin, imageMax, IM_COL32(12, 14, 20, 255), 0.0f);
        ApplyNearestTextureSampling(tex);
        if (tex != 0) {
            drawList->AddImage((void*)(intptr_t)tex, imageMin, imageMax, ImVec2(0, 1), ImVec2(1, 0));
        }
        bool imageHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        bool showingStartupSplash = false;

        if (playerMode && buildSettings.splashEnabled && buildSettings.splashDurationSeconds > 0.0f) {
            if (startupSplashStartTime < 0.0) {
                startupSplashStartTime = glfwGetTime();
            }
            const double elapsed = glfwGetTime() - startupSplashStartTime;
            showingStartupSplash = elapsed < static_cast<double>(buildSettings.splashDurationSeconds);
        }

        auto updateUiCanvas3DInput = [&](const Camera& cam, float fovDeg, float nearPlane, float farPlane) {
            if (!imageHovered) return;
            ImVec2 mouse = ImGui::GetIO().MousePos;
            if (mouse.x < imageMin.x || mouse.x > imageMax.x || mouse.y < imageMin.y || mouse.y > imageMax.y) {
                return;
            }
            float width = std::max(1.0f, imageMax.x - imageMin.x);
            float height = std::max(1.0f, imageMax.y - imageMin.y);
            float ndcX = ((mouse.x - imageMin.x) / width) * 2.0f - 1.0f;
            float ndcY = 1.0f - ((mouse.y - imageMin.y) / height) * 2.0f;

            glm::mat4 view = cam.getViewMatrix();
            glm::mat4 proj = glm::perspective(glm::radians(fovDeg), width / height, nearPlane, farPlane);
            glm::mat4 inv = glm::inverse(proj * view);
            glm::vec4 nearP = inv * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
            glm::vec4 farP = inv * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
            glm::vec3 origin = glm::vec3(nearP) / nearP.w;
            glm::vec3 dir = glm::normalize(glm::vec3(farP) / farP.w - origin);

            const float eps = 1e-5f;
            for (auto& canvas : sceneObjects) {
                if (!canvas.enabled || !canvas.hasUI || canvas.ui.type != UIElementType::Canvas || !canvas.ui.renderIn3D) continue;
                glm::mat4 model(1.0f);
                model = glm::translate(model, canvas.position);
                model = glm::rotate(model, glm::radians(canvas.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
                model = glm::rotate(model, glm::radians(canvas.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
                model = glm::rotate(model, glm::radians(canvas.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
                model = glm::scale(model, canvas.scale);

                glm::vec3 planePoint = glm::vec3(model * glm::vec4(0, 0, 0, 1));
                glm::vec3 planeNormal = glm::normalize(glm::mat3(model) * glm::vec3(0, 0, 1));
                float denom = glm::dot(planeNormal, dir);
                if (std::abs(denom) < eps) continue;
                float t = glm::dot(planeNormal, planePoint - origin) / denom;
                if (t < 0.0f) continue;
                glm::vec3 hit = origin + dir * t;
                glm::vec4 local4 = glm::inverse(model) * glm::vec4(hit, 1.0f);
                glm::vec3 local = glm::vec3(local4);
                if (std::abs(local.x) > 0.5f || std::abs(local.y) > 0.5f) continue;

                float u = local.x + 0.5f;
                float v = 0.5f - local.y;
                float layoutW = std::max(1.0f, canvas.ui.size.x);
                float layoutH = std::max(1.0f, canvas.ui.size.y);
                ImVec2 canvasPos(u * layoutW, v * layoutH);

                UiCanvas3DInput& input = uiCanvas3DInputs[canvas.id];
                if (!input.hasInput || t < input.hitT) {
                    input.mousePos = canvasPos;
                    input.mouseDown[0] = ImGui::GetIO().MouseDown[0];
                    input.mouseDown[1] = ImGui::GetIO().MouseDown[1];
                    input.mouseDown[2] = ImGui::GetIO().MouseDown[2];
                    input.mouseWheel = ImGui::GetIO().MouseWheel;
                    input.hasInput = true;
                    input.hitT = t;
                }
            }
        };

        float runtimeFov = buildSettings.editorCameraFov;
        float runtimeNear = buildSettings.editorCameraNear;
        float runtimeFar = buildSettings.editorCameraFar;
        if (playerMode) {
            const SceneObject* runtimeCam = findPlayerCameraObject();
            if (runtimeCam) {
                runtimeFov = runtimeCam->camera.fov;
                runtimeNear = std::max(0.01f, runtimeCam->camera.nearClip);
                runtimeFar = std::max(runtimeNear + 0.01f, runtimeCam->camera.farClip);
            }
        }
        updateUiCanvas3DInput(camera, runtimeFov, runtimeNear, runtimeFar);

        float uiScaleX = imageSize.x / static_cast<float>(std::max(1, runtimeRenderWidth));
        float uiScaleY = imageSize.y / static_cast<float>(std::max(1, runtimeRenderHeight));

        if (showCanvasOverlay) {
            ImVec2 pad(8.0f, 8.0f);
            ImVec2 tl(imageMin.x + pad.x, imageMin.y + pad.y);
            ImVec2 br(imageMax.x - pad.x, imageMax.y - pad.y);
            drawList->AddRect(tl, br, IM_COL32(110, 170, 255, 180), 8.0f, 0, 2.0f);
        }

        bool uiInteracting = false;
        UiSceneLookupCache uiSceneLookup(sceneObjects);
        auto find3DCanvasId = [&](const SceneObject& target) -> int {
            return uiSceneLookup.find3DCanvasId(target);
        };
        auto findPseudo3DCanvasId = [&](const SceneObject& target) -> int {
            return uiSceneLookup.findPseudo3DCanvasId(target);
        };
        auto isUiOn3DCanvas = [&](const SceneObject& target) {
            return find3DCanvasId(target) >= 0;
        };
        int editCanvas3DId = -1;
        if (SceneObject* selected = getSelectedObject()) {
            editCanvas3DId = find3DCanvasId(*selected);
        }
        auto isUIType = [&](const SceneObject& target) {
            if (!target.hasUI || target.ui.type == UIElementType::None) return false;
            int canvasId = find3DCanvasId(target);
            if (!((canvasId < 0) || (canvasId == editCanvas3DId))) {
                return false;
            }
            return findPseudo3DCanvasId(target) < 0;
        };
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetCursorScreenPos(imageMin);
        ImGui::BeginChild("PlayerUIOverlay",
                          ImVec2(imageMax.x - imageMin.x, imageMax.y - imageMin.y),
                          false,
                          ImGuiWindowFlags_NoTitleBar |
                          ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoBackground);

        auto anchorToPivot = [](UIAnchor anchor, const ImVec2& size) {
            switch (anchor) {
                case UIAnchor::Center: return ImVec2(size.x * 0.5f, size.y * 0.5f);
                case UIAnchor::TopLeft: return ImVec2(0.0f, 0.0f);
                case UIAnchor::TopRight: return ImVec2(size.x, 0.0f);
                case UIAnchor::BottomLeft: return ImVec2(0.0f, size.y);
                case UIAnchor::BottomRight: return ImVec2(size.x, size.y);
                default: return ImVec2(size.x * 0.5f, size.y * 0.5f);
            }
        };
        auto anchorToPoint = [](UIAnchor anchor, const ImVec2& min, const ImVec2& max) {
            switch (anchor) {
                case UIAnchor::Center: return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
                case UIAnchor::TopLeft: return min;
                case UIAnchor::TopRight: return ImVec2(max.x, min.y);
                case UIAnchor::BottomLeft: return ImVec2(min.x, max.y);
                case UIAnchor::BottomRight: return max;
                default: return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            }
        };

        auto resolveUIRect = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax) {
            std::vector<const SceneObject*> chain;
            chain.reserve(8);
            const SceneObject* current = &obj;
            while (current) {
                if (isUIType(*current)) {
                    chain.push_back(current);
                }
                if (current->parentId < 0) break;
                current = uiSceneLookup.find(current->parentId);
                if (current == nullptr) break;
            }
            std::reverse(chain.begin(), chain.end());

            ImVec2 regionMin = ImGui::GetWindowPos();
            ImVec2 regionMax = ImVec2(regionMin.x + ImGui::GetWindowWidth(), regionMin.y + ImGui::GetWindowHeight());
            for (const SceneObject* node : chain) {
                glm::vec2 nodeSizeWorld = getSpriteDisplaySize(*node);
                ImVec2 size = ImVec2(std::max(1.0f, nodeSizeWorld.x * uiScaleX),
                                     std::max(1.0f, nodeSizeWorld.y * uiScaleY));
                ImVec2 anchorPoint = anchorToPoint(node->ui.anchor, regionMin, regionMax);
                ImVec2 pivot(anchorPoint.x + node->ui.position.x * uiScaleX,
                             anchorPoint.y + node->ui.position.y * uiScaleY);
                ImVec2 pivotOffset = anchorToPivot(node->ui.anchor, size);
                regionMin = ImVec2(pivot.x - pivotOffset.x, pivot.y - pivotOffset.y);
                regionMax = ImVec2(regionMin.x + size.x, regionMin.y + size.y);
            }
            outMin = regionMin;
            outMax = regionMax;
        };

        ImVec2 overlayPos = ImGui::GetWindowPos();
        ImVec2 overlaySize = ImGui::GetWindowSize();
        const bool project2DPipeline = isProject2DPipeline();
        bool useWorldUi = false;
        SpriteTextureResolver spriteTextureResolver(rendererInitialized ? &renderer : nullptr);
        if (playerMode) {
            const SceneObject* runtimeCam = findPlayerCameraObject();
            useWorldUi = project2DPipeline || (runtimeCam && runtimeCam->camera.use2D);
            if (runtimeCam && useWorldUi) {
                uiWorldCamera.position = glm::vec2(runtimeCam->position.x, runtimeCam->position.y);
                uiWorldCamera.zoom = std::max(1.0f, runtimeCam->camera.pixelsPerUnit);
            }
        }
        uiWorldPanning = false;
        if (useWorldUi) {
            uiWorldCamera.viewportSize = glm::vec2(overlaySize.x, overlaySize.y);
        }
        Camera projectedUiCamera = camera;
        if (playerMode) {
            if (const SceneObject* runtimeCam = findPlayerCameraObject()) {
                projectedUiCamera = makeCameraFromObject(*runtimeCam);
            }
        }
        glm::mat4 projectedUiView = projectedUiCamera.getViewMatrix();
        glm::mat4 projectedUiProj = projectedUiCamera.orthographic
            ? glm::ortho(
                -overlaySize.x / (2.0f * std::max(1.0f, projectedUiCamera.pixelsPerUnit)),
                 overlaySize.x / (2.0f * std::max(1.0f, projectedUiCamera.pixelsPerUnit)),
                -overlaySize.y / (2.0f * std::max(1.0f, projectedUiCamera.pixelsPerUnit)),
                 overlaySize.y / (2.0f * std::max(1.0f, projectedUiCamera.pixelsPerUnit)),
                 runtimeNear,
                 runtimeFar)
            : glm::perspective(glm::radians(runtimeFov),
                               std::max(0.1f, overlaySize.x / std::max(1.0f, overlaySize.y)),
                               runtimeNear,
                               runtimeFar);
        bool hasProjectedUiCamera = true;
        auto worldToScreen = [&](const glm::vec2& world) {
            glm::vec2 local = uiWorldCamera.WorldToScreen(world);
            return ImVec2(overlayPos.x + local.x, overlayPos.y + local.y);
        };
        auto parallaxOffset = [&](const SceneObject& obj) {
            if (!obj.hasParallaxLayer2D || !obj.parallaxLayer2D.enabled) return glm::vec2(0.0f);
            const float factor = std::clamp(obj.parallaxLayer2D.factor, 0.0f, 1.0f);
            return uiWorldCamera.position * (1.0f - factor);
        };
        glm::vec2 worldViewMin = useWorldUi
            ? uiWorldCamera.ScreenToWorld(glm::vec2(0.0f, overlaySize.y))
            : glm::vec2(0.0f);
        glm::vec2 worldViewMax = useWorldUi
            ? uiWorldCamera.ScreenToWorld(glm::vec2(overlaySize.x, 0.0f))
            : glm::vec2(0.0f);
        auto resolveUIRectWorld = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax) {
            if (obj.type == ObjectType::Sprite25D && hasProjectedUiCamera) {
                return ResolveProjectedSprite25DRect(obj, projectedUiView, projectedUiProj, overlayPos, overlaySize, outMin, outMax);
            }
            glm::vec2 parentOffset = uiSceneLookup.getWorldParentOffset(obj);
            glm::vec2 worldPos = parentOffset + glm::vec2(obj.ui.position.x, obj.ui.position.y) + parallaxOffset(obj);
            glm::vec2 sizeWorld = getSpriteDisplaySize(obj);
            ImVec2 pivotOffset = anchorToPivot(obj.ui.anchor, ImVec2(sizeWorld.x, sizeWorld.y));
            glm::vec2 worldMin = worldPos - glm::vec2(pivotOffset.x, pivotOffset.y);
            glm::vec2 worldMax = worldMin + sizeWorld;
            ImVec2 s0 = worldToScreen(worldMin);
            ImVec2 s1 = worldToScreen(worldMax);
            outMin = ImVec2(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
            outMax = ImVec2(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
            return true;
        };

        bool uiWorldCameraActive = false;

        auto brighten = [](const ImVec4& c, float k) {
            return ImVec4(std::clamp(c.x * k, 0.0f, 1.0f),
                          std::clamp(c.y * k, 0.0f, 1.0f),
                          std::clamp(c.z * k, 0.0f, 1.0f),
                          c.w);
        };
        float animSpeed = 0.0f;
        if (uiAnimationMode == UIAnimationMode::Fluid) {
            animSpeed = 8.0f;
        } else if (uiAnimationMode == UIAnimationMode::Snappy) {
            animSpeed = 18.0f;
        }
        float animStep = (uiAnimationMode == UIAnimationMode::Off) ? 1.0f
            : (1.0f - std::exp(-animSpeed * ImGui::GetIO().DeltaTime));
        auto animateValue = [&](float& current, float target, bool immediate) {
            if (uiAnimationMode == UIAnimationMode::Off || immediate) {
                current = target;
            } else {
                current += (target - current) * animStep;
            }
            return current;
        };
        std::vector<SceneObject*> uiDrawList;
        uiDrawList.reserve(sceneObjects.size());
        for (auto& obj : sceneObjects) {
            if (!IsObjectEnabledInHierarchy(obj) || !isUIType(obj)) continue;
            uiDrawList.push_back(&obj);
        }
        if (uiDrawList.size() > 1) {
            StableSortRuntimeUiDrawList(uiDrawList);
        }
        const auto spriteBatchBuildStart = std::chrono::steady_clock::now();
        BatchedSpriteEmitter spriteBatch(ImGui::GetWindowDrawList());
        spriteBatch.reserve(uiDrawList.size());
        struct CachedMaskRect {
            bool resolved = false;
            bool hasMask = false;
            ImVec2 min = ImVec2(0.0f, 0.0f);
            ImVec2 max = ImVec2(0.0f, 0.0f);
        };
        std::unordered_map<int, CachedMaskRect> cachedMaskRects;
        cachedMaskRects.reserve(sceneObjects.size());
        std::function<const CachedMaskRect&(const SceneObject&)> resolveCachedMaskRect =
            [&](const SceneObject& node) -> const CachedMaskRect& {
                CachedMaskRect& cached = cachedMaskRects[node.id];
                if (cached.resolved) {
                    return cached;
                }
                cached.resolved = true;

                if (node.parentId >= 0) {
                    const SceneObject* parent = uiSceneLookup.find(node.parentId);
                    if (parent) {
                        const CachedMaskRect& parentMask = resolveCachedMaskRect(*parent);
                        if (parentMask.hasMask) {
                            cached.hasMask = true;
                            cached.min = parentMask.min;
                            cached.max = parentMask.max;
                        }

                        if (parent->hasUI && parent->ui.type == UIElementType::Canvas && parent->ui.maskChildren) {
                            ImVec2 canvasMin, canvasMax;
                            bool hasCanvasRect = true;
                            if (useWorldUi || parent->type == ObjectType::Sprite25D) {
                                hasCanvasRect = resolveUIRectWorld(*parent, canvasMin, canvasMax);
                            } else {
                                resolveUIRect(*parent, canvasMin, canvasMax);
                            }
                            if (hasCanvasRect) {
                                if (!cached.hasMask) {
                                    cached.hasMask = true;
                                    cached.min = canvasMin;
                                    cached.max = canvasMax;
                                } else {
                                    cached.min.x = std::max(cached.min.x, canvasMin.x);
                                    cached.min.y = std::max(cached.min.y, canvasMin.y);
                                    cached.max.x = std::min(cached.max.x, canvasMax.x);
                                    cached.max.y = std::min(cached.max.y, canvasMax.y);
                                }
                            }
                        }
                    }
                }

                if (cached.hasMask &&
                    (cached.max.x <= cached.min.x || cached.max.y <= cached.min.y)) {
                    cached.hasMask = false;
                }
                return cached;
            };
        auto resolveCanvasMaskRectForObject = [&](const SceneObject& obj, ImVec2& outMin, ImVec2& outMax) -> bool {
            const CachedMaskRect& cached = resolveCachedMaskRect(obj);
            if (!cached.hasMask) {
                return false;
            }
            outMin = cached.min;
            outMax = cached.max;
            return true;
        };

        std::unordered_set<int> light2DRenderedObjectIds;
        bool renderedLight2DComposite = false;
        int activeLight2DCount = 0;
        int litSprite2DCount = 0;
        int litWorldImageCount = 0;
        bool lightBufferHadContent = false;
        std::unordered_map<int, std::string> light2DRoutingReasons;
        SceneObject* selectedForRoutingReasons = showInspector ? getSelectedObject() : nullptr;
        const bool captureLight2DRoutingReasons = selectedForRoutingReasons && selectedForRoutingReasons->hasUI;
        if (captureLight2DRoutingReasons) {
            light2DRoutingReasons.reserve(uiDrawList.size());
        }
        auto setLight2DRoutingReason = [&](int objectId, const char* reason) {
            if (captureLight2DRoutingReasons) {
                light2DRoutingReasons[objectId] = reason;
            }
        };
        if (useWorldUi && rendererInitialized) {
            Light2DRenderRequest lightRequest;
            lightRequest.width = std::max(1, static_cast<int>(std::round(overlaySize.x)));
            lightRequest.height = std::max(1, static_cast<int>(std::round(overlaySize.y)));
            lightRequest.clearColor = glm::vec4(0.0f);
            lightRequest.baseAmbient = glm::vec3(0.0f);
            lightRequest.lightingBufferScale = light2DLightingBufferScale;
            lightRequest.blendStyles = light2DBlendStyles;
            auto computeFlickerMultiplier = [](const Light2DFlickerSettings& flicker) {
                if (!flicker.enabled || flicker.amount <= 0.0001f) {
                    return 1.0f;
                }
                const float time = static_cast<float>(glfwGetTime());
                const float base = std::sin(time * std::max(0.01f, flicker.speed) + flicker.seed);
                const float jitter = std::sin(time * std::max(0.01f, flicker.speed * 2.173f) + flicker.seed * 1.913f);
                const float noise = 0.5f + 0.35f * base + 0.15f * jitter;
                return glm::mix(1.0f, std::max(0.0f, noise), std::clamp(flicker.amount, 0.0f, 1.0f));
            };

            int drawOrder = 0;
            for (SceneObject* objPtr : uiDrawList) {
                SceneObject& obj = *objPtr;
                if (!(obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D)) continue;
                if (obj.ui.nineSliceEnabled) {
                    setLight2DRoutingReason(obj.id, "Legacy path: nine-slice sprites are not routed through Light2D yet.");
                    continue;
                }
                if (obj.ui.unlitLighting2D) {
                    setLight2DRoutingReason(obj.id, "Legacy path: Force Unlit keeps this sprite on the legacy 2D renderer.");
                    continue;
                }
                const bool repeatX = obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.repeatX;
                const bool repeatY = obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.repeatY;
                const bool disableCulling = obj.hasParallaxLayer2D && obj.parallaxLayer2D.enabled && obj.parallaxLayer2D.disableCulling;

                ImVec2 rectMin, rectMax;
                if (!resolveUIRectWorld(obj, rectMin, rectMax)) {
                    setLight2DRoutingReason(obj.id, "Legacy path: failed to resolve a world-space sprite rect for the active viewport.");
                    continue;
                }

                Texture* spriteTex = spriteTextureResolver.resolveTexture(obj);
                if (!spriteTex || spriteTex->GetID() == 0) {
                    setLight2DRoutingReason(obj.id, "Legacy path: no sprite texture is bound for this object.");
                    continue;
                }

                std::array<ImVec2, 4> uvQuad = buildSpriteSheetUvs(obj);
                const float angle = glm::radians(obj.ui.rotation);
                const float c = std::cos(angle);
                const float s = std::sin(angle);
                ImVec2 maskMin, maskMax;
                const bool hasMaskRect = resolveCanvasMaskRectForObject(obj, maskMin, maskMax);
                auto quadOutsideOverlay = [&](const ImVec2& quadMin, const ImVec2& quadMax) {
                    return quadMax.x < overlayPos.x || quadMin.x > overlayPos.x + overlaySize.x ||
                           quadMax.y < overlayPos.y || quadMin.y > overlayPos.y + overlaySize.y;
                };
                auto appendSpriteQuad = [&](const ImVec2& quadMin, const ImVec2& quadMax) {
                    if (!disableCulling && quadOutsideOverlay(quadMin, quadMax)) {
                        return false;
                    }
                    if (hasMaskRect) {
                        const bool maskClipsSprite =
                            quadMin.x < maskMin.x || quadMax.x > maskMax.x ||
                            quadMin.y < maskMin.y || quadMax.y > maskMax.y;
                        if (maskClipsSprite) {
                            return false;
                        }
                    }

                    Light2DScreenSprite sprite;
                    sprite.objectId = obj.id;
                    sprite.layer = obj.layer;
                    sprite.drawOrder = drawOrder++;
                    sprite.textureId = spriteTex->GetID();
                    sprite.tint = obj.ui.color;
                    sprite.receiveLighting = obj.ui.receiveLighting2D;
                    sprite.unlit = obj.ui.unlitLighting2D;
                    sprite.emissiveIntensity = obj.ui.emissiveLighting2D;

                    const glm::vec2 center(
                        ((quadMin.x + quadMax.x) * 0.5f) - overlayPos.x,
                        ((quadMin.y + quadMax.y) * 0.5f) - overlayPos.y);
                    const glm::vec2 half(
                        std::max(0.5f, (quadMax.x - quadMin.x) * 0.5f),
                        std::max(0.5f, (quadMax.y - quadMin.y) * 0.5f));
                    auto rotatePoint = [&](float x, float y) {
                        return glm::vec2(center.x + x * c - y * s, center.y + x * s + y * c);
                    };
                    sprite.positions[0] = rotatePoint(-half.x, -half.y);
                    sprite.positions[1] = rotatePoint(half.x, -half.y);
                    sprite.positions[2] = rotatePoint(half.x, half.y);
                    sprite.positions[3] = rotatePoint(-half.x, half.y);
                    sprite.uvs[0] = glm::vec2(uvQuad[0].x, uvQuad[0].y);
                    sprite.uvs[1] = glm::vec2(uvQuad[1].x, uvQuad[1].y);
                    sprite.uvs[2] = glm::vec2(uvQuad[2].x, uvQuad[2].y);
                    sprite.uvs[3] = glm::vec2(uvQuad[3].x, uvQuad[3].y);
                    lightRequest.sprites.push_back(sprite);
                    return true;
                };

                bool addedAnySprite = false;
                if (repeatX || repeatY) {
                    glm::vec2 spriteSizeWorld = getSpriteDisplaySize(obj);
                    glm::vec2 spacing = obj.hasParallaxLayer2D ? obj.parallaxLayer2D.repeatSpacing : glm::vec2(0.0f);
                    float stepX = spriteSizeWorld.x + spacing.x;
                    float stepY = spriteSizeWorld.y + spacing.y;
                    ImVec2 pivotOffset = anchorToPivot(obj.ui.anchor, ImVec2(spriteSizeWorld.x, spriteSizeWorld.y));
                    glm::vec2 parentOffset = uiSceneLookup.getWorldParentOffset(obj);
                    glm::vec2 worldPos = parentOffset + glm::vec2(obj.ui.position.x, obj.ui.position.y) + parallaxOffset(obj);
                    glm::vec2 baseWorldMin = worldPos - glm::vec2(pivotOffset.x, pivotOffset.y);
                    int startX = repeatX ? static_cast<int>(std::floor((worldViewMin.x - baseWorldMin.x) / stepX)) - 1 : 0;
                    int endX = repeatX ? static_cast<int>(std::ceil((worldViewMax.x - baseWorldMin.x) / stepX)) + 1 : 0;
                    int startY = repeatY ? static_cast<int>(std::floor((worldViewMin.y - baseWorldMin.y) / stepY)) - 1 : 0;
                    int endY = repeatY ? static_cast<int>(std::ceil((worldViewMax.y - baseWorldMin.y) / stepY)) + 1 : 0;
                    for (int ix = startX; ix <= endX; ++ix) {
                        for (int iy = startY; iy <= endY; ++iy) {
                            float dx = repeatX ? static_cast<float>(ix) * stepX : 0.0f;
                            float dy = repeatY ? static_cast<float>(iy) * stepY : 0.0f;
                            glm::vec2 tileMin = baseWorldMin + glm::vec2(dx, dy);
                            ImVec2 s0 = worldToScreen(tileMin);
                            ImVec2 s1 = worldToScreen(tileMin + glm::vec2(spriteSizeWorld.x, spriteSizeWorld.y));
                            ImVec2 tileRectMin(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
                            ImVec2 tileRectMax(std::max(s0.x, s1.x), std::max(s0.y, s1.y));
                            addedAnySprite = appendSpriteQuad(tileRectMin, tileRectMax) || addedAnySprite;
                        }
                    }
                } else {
                    addedAnySprite = appendSpriteQuad(rectMin, rectMax);
                }

                if (!addedAnySprite) {
                    setLight2DRoutingReason(obj.id, hasMaskRect
                        ? "Legacy path: repeating or masked tiles still use legacy rendering when the canvas clip cuts the visible tile."
                        : "Skipped Light2D: object has no visible tiles inside the current 2D world overlay.");
                    continue;
                }

                light2DRenderedObjectIds.insert(obj.id);
                if (obj.ui.type == UIElementType::Sprite2D) {
                    if (obj.ui.receiveLighting2D && !obj.ui.unlitLighting2D) {
                        ++litSprite2DCount;
                    }
                } else if (obj.ui.receiveLighting2D && !obj.ui.unlitLighting2D) {
                    ++litWorldImageCount;
                }
                if (obj.ui.receiveLighting2D && !obj.ui.unlitLighting2D) {
                    setLight2DRoutingReason(obj.id, repeatX || repeatY
                        ? "Lit path: repeating parallax tiles are routed through the Light2D compositor."
                        : "Lit path: routed through the Light2D compositor.");
                } else if (obj.ui.unlitLighting2D) {
                    setLight2DRoutingReason(obj.id, "Lit compositor path: object is routed, but Force Unlit is enabled.");
                } else {
                    setLight2DRoutingReason(obj.id, "Lit compositor path: object is routed, but Receive Lighting is disabled.");
                }
            }

            for (const SceneObject& obj : sceneObjects) {
                if (!IsObjectEnabledInHierarchy(obj) || !obj.hasLight2D || !obj.light2D.enabled) {
                    continue;
                }
                ++activeLight2DCount;

                if (obj.light2D.type == Light2DType::Global) {
                    lightRequest.baseAmbient += glm::vec3(obj.light2D.color) * obj.light2D.intensity;
                    continue;
                }

                Light2DScreenLight light;
                light.objectId = obj.id;
                light.enabled = obj.light2D.enabled;
                light.type = obj.light2D.type;
                light.blendStyle = obj.light2D.blendStyle;
                light.lightOrder = obj.light2D.lightOrder;
                light.overlapOperation = obj.light2D.overlapOperation;
                light.targetAllLayers = obj.light2D.targetAllLayers;
                light.targetLayerMask = obj.light2D.targetLayerMask;
                light.color = obj.light2D.color;
                light.intensity = obj.light2D.intensity * computeFlickerMultiplier(obj.light2D.flicker);
                light.radius = std::max(obj.light2D.radius, obj.light2D.outerRadius) * uiWorldCamera.zoom;
                light.innerRadius = obj.light2D.innerRadius * uiWorldCamera.zoom;
                light.outerRadius = std::max(obj.light2D.innerRadius, obj.light2D.outerRadius) * uiWorldCamera.zoom;
                light.falloffStrength = obj.light2D.falloffStrength;
                light.innerSpotAngle = obj.light2D.innerSpotAngle;
                light.outerSpotAngle = obj.light2D.outerSpotAngle;
                light.shadowStrength = obj.light2D.shadowStrength;
                light.volumetricEnabled = obj.light2D.volumetricEnabled;
                light.castsShadows = obj.light2D.castsShadows;
                light.rotationRad = glm::radians(obj.rotation.z);
                light.cookieScale = obj.light2D.cookieScale;
                light.cookieRotationRad = glm::radians(obj.light2D.cookieRotation);
                light.freeformFeatherPx = obj.light2D.freeformFeather * uiWorldCamera.zoom;
                light.freeformEdgeFalloff = obj.light2D.freeformEdgeFalloff;
                if (!obj.light2D.cookieTexturePath.empty()) {
                    if (Texture* cookieTexture = renderer.getTexture(obj.light2D.cookieTexturePath, MaterialProperties::TextureFilter::Bilinear)) {
                        light.cookieTextureId = cookieTexture->GetID();
                    }
                }

                ImVec2 lightPos = worldToScreen(glm::vec2(obj.position.x, obj.position.y));
                light.position = glm::vec2(lightPos.x - overlayPos.x, lightPos.y - overlayPos.y);

                if (obj.light2D.type == Light2DType::Freeform || obj.light2D.type == Light2DType::Sprite) {
                    light.polygon.reserve(obj.light2D.shapePoints.size());
                    for (const glm::vec2& point : obj.light2D.shapePoints) {
                        ImVec2 screenPoint = worldToScreen(glm::vec2(obj.position.x + point.x, obj.position.y + point.y));
                        light.polygon.emplace_back(screenPoint.x - overlayPos.x, screenPoint.y - overlayPos.y);
                    }
                    if (!light.polygon.empty()) {
                        glm::vec2 boundsMin(FLT_MAX);
                        glm::vec2 boundsMax(-FLT_MAX);
                        for (const glm::vec2& point : light.polygon) {
                            boundsMin.x = std::min(boundsMin.x, point.x);
                            boundsMin.y = std::min(boundsMin.y, point.y);
                            boundsMax.x = std::max(boundsMax.x, point.x);
                            boundsMax.y = std::max(boundsMax.y, point.y);
                        }
                        light.boundsMin = boundsMin;
                        light.boundsMax = boundsMax;
                    }
                } else {
                    const float extent = std::max(light.radius, light.outerRadius);
                    light.boundsMin = light.position - glm::vec2(extent);
                    light.boundsMax = light.position + glm::vec2(extent);
                }

                lightRequest.lights.push_back(light);
            }

            for (const SceneObject& obj : sceneObjects) {
                if (!IsObjectEnabledInHierarchy(obj) || !obj.hasShadowCaster2D || !obj.shadowCaster2D.enabled) {
                    continue;
                }

                Light2DScreenShadowCaster caster;
                caster.objectId = obj.id;
                caster.enabled = obj.shadowCaster2D.enabled;
                caster.targetAllLayers = obj.shadowCaster2D.targetAllLayers;
                caster.targetLayerMask = obj.shadowCaster2D.targetLayerMask;
                caster.shadowStrength = obj.shadowCaster2D.shadowStrength;
                caster.polygon.reserve(obj.shadowCaster2D.points.size());
                for (const glm::vec2& point : obj.shadowCaster2D.points) {
                    ImVec2 screenPoint = worldToScreen(glm::vec2(obj.position.x + point.x, obj.position.y + point.y));
                    caster.polygon.emplace_back(screenPoint.x - overlayPos.x, screenPoint.y - overlayPos.y);
                }
                if (caster.polygon.size() >= 3) {
                    lightRequest.shadowCasters.push_back(std::move(caster));
                }
            }

            const bool hasAmbientOnly = glm::length(lightRequest.baseAmbient) > 0.0001f;
            lightBufferHadContent = hasAmbientOnly || !lightRequest.lights.empty();
            if (!lightRequest.sprites.empty() && (hasAmbientOnly || !lightRequest.lights.empty())) {
                unsigned int lightTexture = lighting2DRenderer.render(lightRequest, renderer);
                if (lightTexture != 0) {
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)(intptr_t)lightTexture,
                        overlayPos,
                        ImVec2(overlayPos.x + overlaySize.x, overlayPos.y + overlaySize.y),
                        ImVec2(0.0f, 1.0f),
                        ImVec2(1.0f, 0.0f));
                    renderedLight2DComposite = true;
                } else {
                    for (int objectId : light2DRenderedObjectIds) {
                        setLight2DRoutingReason(objectId, "Legacy path: Light2D compositor did not produce a valid output texture this frame.");
                    }
                    light2DRenderedObjectIds.clear();
                }
            } else {
                for (int objectId : light2DRenderedObjectIds) {
                    setLight2DRoutingReason(objectId, "Legacy path: no active Light2D or Global Light2D affected this frame.");
                }
                light2DRenderedObjectIds.clear();
            }
        }

        for (SceneObject* objPtr : uiDrawList) {
            SceneObject& obj = *objPtr;
            ImVec2 rectMin, rectMax;
            if (useWorldUi || obj.type == ObjectType::Sprite25D) {
                if (!resolveUIRectWorld(obj, rectMin, rectMax)) continue;
            } else {
                resolveUIRect(obj, rectMin, rectMax);
            }
            ImVec2 rectSize(rectMax.x - rectMin.x, rectMax.y - rectMin.y);
            if (rectSize.x <= 1.0f || rectSize.y <= 1.0f) continue;
            ++runtimeVisibleObjectCount;

            ImGuiStyle savedStyle;
            bool styleApplied = false;
            if (!obj.ui.stylePreset.empty()) {
                if (const auto* preset = getUIStylePreset(obj.ui.stylePreset)) {
                    savedStyle = ImGui::GetStyle();
                    ImGui::GetStyle() = preset->style;
                    styleApplied = true;
                }
            }

            if (obj.ui.type == UIElementType::Canvas) {
                spriteBatch.flush();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImU32 edgeColor = obj.ui.maskChildren ? IM_COL32(74, 228, 255, 225)
                                                            : IM_COL32(110, 170, 255, 140);
                const float thickness = obj.ui.maskChildren ? 2.4f : 1.5f;
                dl->AddRect(rectMin, rectMax, edgeColor, 6.0f, 0, thickness);
                if (obj.ui.maskChildren) {
                    const float inset = 2.0f;
                    if ((rectMax.x - rectMin.x) > inset * 2.0f && (rectMax.y - rectMin.y) > inset * 2.0f) {
                        dl->AddRect(ImVec2(rectMin.x + inset, rectMin.y + inset),
                                    ImVec2(rectMax.x - inset, rectMax.y - inset),
                                    IM_COL32(32, 190, 230, 175), 5.0f, 0, 1.0f);
                    }
                }
                if (styleApplied) ImGui::GetStyle() = savedStyle;
                continue;
            }

            ImVec2 drawMin = rectMin;
            ImVec2 drawMax = rectMax;
            ImVec2 drawSize(drawMax.x - drawMin.x, drawMax.y - drawMin.y);
            ImVec2 localMin(drawMin.x - overlayPos.x, drawMin.y - overlayPos.y);
            bool pushedCanvasMask = false;
            if (obj.ui.type != UIElementType::Canvas) {
                ImVec2 maskMin, maskMax;
                if (resolveCanvasMaskRectForObject(obj, maskMin, maskMax)) {
                    maskMin.x = std::max(maskMin.x, overlayPos.x);
                    maskMin.y = std::max(maskMin.y, overlayPos.y);
                    maskMax.x = std::min(maskMax.x, overlayPos.x + overlaySize.x);
                    maskMax.y = std::min(maskMax.y, overlayPos.y + overlaySize.y);
                    if (maskMax.x <= maskMin.x || maskMax.y <= maskMin.y) {
                        if (styleApplied) ImGui::GetStyle() = savedStyle;
                        continue;
                    }
                    if (drawMax.x <= maskMin.x || drawMin.x >= maskMax.x ||
                        drawMax.y <= maskMin.y || drawMin.y >= maskMax.y) {
                        if (styleApplied) ImGui::GetStyle() = savedStyle;
                        continue;
                    }
                    spriteBatch.flush();
                    ImGui::PushClipRect(maskMin, maskMax, true);
                    pushedCanvasMask = true;
                }
            }

            ImGui::PushID(obj.id);
            UIAnimationState& animState = uiAnimationStates[obj.id];
            if (!animState.initialized) {
                animState.sliderValue = obj.ui.sliderValue;
                animState.initialized = true;
            }
            if (obj.ui.type == UIElementType::Image || obj.ui.type == UIElementType::Sprite2D) {
                if (light2DRenderedObjectIds.find(obj.id) != light2DRenderedObjectIds.end()) {
                    if (pushedCanvasMask) {
                        ImGui::PopClipRect();
                    }
                    ImGui::PopID();
                    if (styleApplied) ImGui::GetStyle() = savedStyle;
                    continue;
                }
                Texture* spriteTex = spriteTextureResolver.resolveTexture(obj);
                unsigned int texId = (spriteTex != nullptr) ? spriteTex->GetID() : 0;
                std::array<ImVec2, 4> uvQuad = buildSpriteSheetUvs(obj);
                const int frame = resolveSpriteSheetFrame(obj);
                const ImVec2 sourceFrameSizePx = ResolveUiSourceFrameSizePx(obj, frame, spriteTex);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                const ImU32 tintColor = ImGui::GetColorU32(tint);
                float angle = glm::radians(obj.ui.rotation);
                if (DrawNineSliceSprite(spriteBatch,
                                        (ImTextureID)(intptr_t)texId,
                                        obj,
                                        drawMin,
                                        drawMax,
                                        uvQuad,
                                        sourceFrameSizePx,
                                        angle,
                                        tintColor)) {
                } else if (std::abs(angle) > 1e-4f) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 center = ImVec2((drawMin.x + drawMax.x) * 0.5f, (drawMin.y + drawMax.y) * 0.5f);
                    ImVec2 half = ImVec2(drawSize.x * 0.5f, drawSize.y * 0.5f);
                    float c = std::cos(angle);
                    float s = std::sin(angle);
                    auto rotPt = [&](float x, float y) {
                        return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
                    };
                    ImVec2 p0 = rotPt(-half.x, -half.y);
                    ImVec2 p1 = rotPt( half.x, -half.y);
                    ImVec2 p2 = rotPt( half.x,  half.y);
                    ImVec2 p3 = rotPt(-half.x,  half.y);
                    if (texId != 0) {
                        spriteBatch.push((ImTextureID)(intptr_t)texId,
                                         p0, p1, p2, p3,
                                         uvQuad[0], uvQuad[1], uvQuad[2], uvQuad[3],
                                         tintColor);
                    } else {
                        spriteBatch.flush();
                        ImU32 fill = tintColor;
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        dl->AddQuadFilled(p0, p1, p2, p3, fill);
                        dl->AddQuad(p0, p1, p2, p3, border, 2.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                        dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                    }
                } else {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    if (texId != 0) {
                        spriteBatch.push((ImTextureID)(intptr_t)texId,
                                         drawMin,
                                         ImVec2(drawMax.x, drawMin.y),
                                         drawMax,
                                         ImVec2(drawMin.x, drawMax.y),
                                         uvQuad[0],
                                         ImVec2(uvQuad[2].x, uvQuad[0].y),
                                         uvQuad[2],
                                         ImVec2(uvQuad[0].x, uvQuad[2].y),
                                         tintColor);
                    } else {
                        spriteBatch.flush();
                        ImU32 fill = tintColor;
                        ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                        dl->AddRectFilled(drawMin, drawMax, fill, 6.0f);
                        dl->AddRect(drawMin, drawMax, border, 6.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(210, 210, 220, 220), obj.ui.label.c_str());
                        ImGui::Dummy(drawSize);
                    }
                }
            } else if (obj.ui.type == UIElementType::Slider) {
                spriteBatch.flush();
                ImGui::SetCursorPos(localMin);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                if (obj.ui.sliderStyle == UISliderStyle::ImGui) {
                    ImGui::PushItemWidth(drawSize.x);
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, brighten(tint, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, brighten(tint, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrab, brighten(tint, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, brighten(tint, 1.1f));
                    if (ImGui::SliderFloat(obj.ui.label.c_str(), &obj.ui.sliderValue, obj.ui.sliderMin, obj.ui.sliderMax)) {
                        projectManager.currentProject.hasUnsavedChanges = true;
                    }
                    ImGui::PopStyleColor(5);
                    ImGui::EndDisabled();
                    ImGui::PopItemWidth();
                } else {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 bg = ImGui::GetColorU32(ImVec4(tint.x * 0.2f, tint.y * 0.2f, tint.z * 0.2f, tint.w * 0.6f));
                    ImU32 fill = ImGui::GetColorU32(tint);
                    ImU32 border = ImGui::GetColorU32(brighten(tint, 0.85f));
                    float minValue = obj.ui.sliderMin;
                    float maxValue = obj.ui.sliderMax;
                    float range = (maxValue - minValue);
                    if (range <= 1e-6f) range = 1.0f;
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    ImGui::InvisibleButton("##UISlider", drawSize);
                    bool held = obj.ui.interactable && ImGui::IsItemActive();
                    if (held && ImGui::IsMouseDown(ImGuiMouseButton_Left) && drawSize.x > 1.0f) {
                        float mouseT = (ImGui::GetIO().MousePos.x - drawMin.x) / drawSize.x;
                        mouseT = std::clamp(mouseT, 0.0f, 1.0f);
                        float newValue = minValue + mouseT * range;
                        if (newValue != obj.ui.sliderValue) {
                            obj.ui.sliderValue = newValue;
                            projectManager.currentProject.hasUnsavedChanges = true;
                        }
                    }
                    ImGui::EndDisabled();

                    animateValue(animState.sliderValue, obj.ui.sliderValue, held);
                    float displayValue = (uiAnimationMode == UIAnimationMode::Off) ? obj.ui.sliderValue : animState.sliderValue;
                    float t = (displayValue - minValue) / range;
                    t = std::clamp(t, 0.0f, 1.0f);

                    if (obj.ui.sliderStyle == UISliderStyle::Fill) {
                        float rounding = 6.0f;
                        ImVec2 fillMax(drawMin.x + drawSize.x * t, drawMax.y);
                        dl->AddRectFilled(drawMin, drawMax, bg, rounding);
                        if (fillMax.x > drawMin.x) {
                            dl->AddRectFilled(drawMin, fillMax, fill, rounding);
                        }
                        dl->AddRect(drawMin, drawMax, border, rounding);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                       drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    } else if (obj.ui.sliderStyle == UISliderStyle::Circle) {
                        ImVec2 center((drawMin.x + drawMax.x) * 0.5f, (drawMin.y + drawMax.y) * 0.5f);
                        float radius = std::max(2.0f, std::min(drawSize.x, drawSize.y) * 0.5f - 2.0f);
                        dl->AddCircleFilled(center, radius, bg, 32);
                        float start = -IM_PI * 0.5f;
                        float end = start + t * IM_PI * 2.0f;
                        dl->PathClear();
                        dl->PathArcTo(center, radius, start, end, 32);
                        dl->PathLineTo(center);
                        dl->PathFillConvex(fill);
                        dl->AddCircle(center, radius, border, 32, 2.0f);
                        ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                        ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
                        dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                    }
                }
            } else if (obj.ui.type == UIElementType::Button) {
                spriteBatch.flush();
                ImGui::SetCursorPos(localMin);
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                obj.ui.buttonPressed = false;
                if (obj.ui.buttonStyle == UIButtonStyle::ImGui) {
                    ImGui::PushStyleColor(ImGuiCol_Button, tint);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brighten(tint, 1.1f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, brighten(tint, 1.2f));
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    obj.ui.buttonPressed = ImGui::Button(obj.ui.label.c_str(), drawSize);
                    ImGui::EndDisabled();
                    ImGui::PopStyleColor(3);
                } else if (obj.ui.buttonStyle == UIButtonStyle::Outline) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 border = ImGui::GetColorU32(tint);
                    ImGui::BeginDisabled(!obj.ui.interactable || uiWorldCameraActive);
                    if (ImGui::InvisibleButton("##UIButton", drawSize)) {
                        obj.ui.buttonPressed = obj.ui.interactable;
                    }
                    bool hovered = ImGui::IsItemHovered();
                    bool active = ImGui::IsItemActive();
                    ImGui::EndDisabled();
                    float hoverT = animateValue(animState.hover, hovered ? 1.0f : 0.0f, false);
                    float activeT = animateValue(animState.active, active ? 1.0f : 0.0f, false);
                    if (hoverT > 0.001f) {
                        ImVec4 hoverCol = brighten(tint, 0.45f);
                        hoverCol.w *= std::clamp(hoverT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(hoverCol), 6.0f);
                    }
                    if (activeT > 0.001f) {
                        ImVec4 activeCol = brighten(tint, 0.65f);
                        activeCol.w *= std::clamp(activeT, 0.0f, 1.0f);
                        dl->AddRectFilled(drawMin, drawMax, ImGui::GetColorU32(activeCol), 6.0f);
                    }
                    dl->AddRect(drawMin, drawMax, border, 6.0f, 0, 2.0f);
                    ImVec2 textSize = ImGui::CalcTextSize(obj.ui.label.c_str());
                    ImVec2 textPos(drawMin.x + (drawSize.x - textSize.x) * 0.5f,
                                   drawMin.y + (drawSize.y - textSize.y) * 0.5f);
                    dl->AddText(textPos, IM_COL32(240, 240, 245, 220), obj.ui.label.c_str());
                }
            } else if (obj.ui.type == UIElementType::Text) {
                spriteBatch.flush();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec4 tint(obj.ui.color.r, obj.ui.color.g, obj.ui.color.b, obj.ui.color.a);
                float scale = std::max(0.1f, obj.ui.textScale);
                float scaleFactor = useWorldUi ? std::max(0.01f, uiWorldCamera.zoom / 100.0f)
                                               : std::min(uiScaleX, uiScaleY);
                float fontSize = std::max(1.0f, ImGui::GetFontSize() * scale * scaleFactor);
                ImGui::PushClipRect(drawMin, drawMax, true);
                AddUITextWithFilter(dl,
                                    obj.material.textureFilter,
                                    ImGui::GetFont(),
                                    fontSize,
                                    drawMin,
                                    drawMax,
                                    ImGui::GetColorU32(tint),
                                    obj.ui.label.c_str(),
                                    obj.ui.textAutoWrap,
                                    obj.ui.textHAlign,
                                    obj.ui.textVAlign,
                                    obj.ui.textEffectFlags,
                                    obj.ui.textEffectSpeed,
                                    obj.ui.textEffectIntensity);
                ImGui::PopClipRect();
            }
            if (pushedCanvasMask) {
                spriteBatch.flush();
                ImGui::PopClipRect();
            }
            ImGui::PopID();
            if (styleApplied) ImGui::GetStyle() = savedStyle;
        }
        spriteBatch.flush();
        if (useWorldUi) {
            light2DCompositorRanLastFrame = renderedLight2DComposite;
            light2DLightBufferHadContentLastFrame = lightBufferHadContent;
            light2DActiveCountLastFrame = activeLight2DCount;
            light2DLitSprite2DCountLastFrame = litSprite2DCount;
            light2DLitWorldImageCountLastFrame = litWorldImageCount;
            if (captureLight2DRoutingReasons) {
                light2DObjectRoutingReasonsLastFrame = std::move(light2DRoutingReasons);
            }
        }

        bool pseudoPanelInteracting = false;
        struct PseudoPanelDrawEntry {
            int canvasId = -1;
            unsigned int textureId = 0;
            ImVec2 layoutSize = ImVec2(1.0f, 1.0f);
            std::array<ImVec2, 4> corners;
            int depthSort = 0;
            bool allowInteraction = false;
        };
        std::vector<PseudoPanelDrawEntry> pseudoPanels;
        pseudoPanels.reserve(sceneObjects.size());

        auto resolvePseudoAnchorScreen = [&](const SceneObject& canvas, ImVec2& outScreen, float& outDistance) -> bool {
            outDistance = 1.0f;
            if (canvas.ui.pseudo3DAnchorTargetId < 0) {
                return false;
            }
            const SceneObject* anchorObj = uiSceneLookup.find(canvas.ui.pseudo3DAnchorTargetId);
            if (!anchorObj) {
                return false;
            }
            if (useWorldUi) {
                outScreen = worldToScreen(glm::vec2(anchorObj->position.x, anchorObj->position.y));
                outDistance = glm::length(
                    glm::vec2(uiWorldCamera.position.x - anchorObj->position.x,
                              uiWorldCamera.position.y - anchorObj->position.y));
                return true;
            }
            if (ProjectWorldToOverlayPoint(anchorObj->position,
                                           projectedUiView,
                                           projectedUiProj,
                                           overlayPos,
                                           overlaySize,
                                           outScreen)) {
                outDistance = glm::length(camera.position - anchorObj->position);
                return true;
            }
            return false;
        };
        auto resolvePseudoCanvasRect = [&](const SceneObject& canvas,
                                           const glm::vec2& layoutSizePx,
                                           ImVec2& outMin,
                                           ImVec2& outMax) -> bool {
            std::vector<const SceneObject*> chain;
            chain.reserve(8);
            const SceneObject* current = &canvas;
            while (current) {
                if (current->hasUI && current->ui.type != UIElementType::None) {
                    const int canvas3DId = find3DCanvasId(*current);
                    if (canvas3DId < 0 || current->id == canvas.id) {
                        chain.push_back(current);
                    }
                }
                if (current->parentId < 0) break;
                current = uiSceneLookup.find(current->parentId);
                if (!current) break;
            }
            if (chain.empty()) {
                return false;
            }
            std::reverse(chain.begin(), chain.end());

            ImVec2 regionMin = overlayPos;
            ImVec2 regionMax = ImVec2(overlayPos.x + overlaySize.x, overlayPos.y + overlaySize.y);
            for (const SceneObject* node : chain) {
                ImVec2 size(1.0f, 1.0f);
                if (node->id == canvas.id) {
                    size = ImVec2(std::max(1.0f, layoutSizePx.x * uiScaleX),
                                  std::max(1.0f, layoutSizePx.y * uiScaleY));
                } else {
                    const glm::vec2 nodeSize = getSpriteDisplaySize(*node);
                    size = ImVec2(std::max(1.0f, nodeSize.x * uiScaleX),
                                  std::max(1.0f, nodeSize.y * uiScaleY));
                }
                const ImVec2 anchorPoint = anchorToPoint(node->ui.anchor, regionMin, regionMax);
                const ImVec2 pivot(anchorPoint.x + node->ui.position.x * uiScaleX,
                                   anchorPoint.y + node->ui.position.y * uiScaleY);
                const ImVec2 pivotOffset = anchorToPivot(node->ui.anchor, size);
                regionMin = ImVec2(pivot.x - pivotOffset.x, pivot.y - pivotOffset.y);
                regionMax = ImVec2(regionMin.x + size.x, regionMin.y + size.y);
            }
            outMin = regionMin;
            outMax = regionMax;
            return true;
        };

        for (auto& canvas : sceneObjects) {
            if (!IsObjectEnabledInHierarchy(canvas) ||
                !canvas.hasUI ||
                canvas.ui.type != UIElementType::Canvas ||
                canvas.ui.renderIn3D ||
                !canvas.ui.pseudo3DEnabled ||
                !canvas.ui.pseudo3DUseOffscreenSurface) {
                continue;
            }

            const glm::vec2 layoutSizePx = ResolvePseudo3DLayoutSize(canvas);
            ImVec2 rectMin;
            ImVec2 rectMax;
            if (!resolvePseudoCanvasRect(canvas, layoutSizePx, rectMin, rectMax)) {
                continue;
            }
            const int targetWidth = std::clamp(
                (canvas.ui.renderTargetSize.x > 0) ? canvas.ui.renderTargetSize.x : static_cast<int>(layoutSizePx.x),
                16,
                4096);
            const int targetHeight = std::clamp(
                (canvas.ui.renderTargetSize.y > 0) ? canvas.ui.renderTargetSize.y : static_cast<int>(layoutSizePx.y),
                16,
                4096);
            Renderer::UiTargetInfo target = renderer.ensureUiTarget(canvas.id, targetWidth, targetHeight);
            if (target.texture == 0) {
                continue;
            }

            float distance = 1.0f;
            ImVec2 anchorScreen(0.0f, 0.0f);
            const bool anchored = resolvePseudoAnchorScreen(canvas, anchorScreen, distance);
            if (!anchored) {
                if (useWorldUi) {
                    distance = glm::length(
                        glm::vec2(uiWorldCamera.position.x - canvas.position.x,
                                  uiWorldCamera.position.y - canvas.position.y));
                } else {
                    distance = glm::length(camera.position - canvas.position);
                }
            }

            if (anchored) {
                const ImVec2 center((rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y) * 0.5f);
                const ImVec2 shift(anchorScreen.x - center.x, anchorScreen.y - center.y);
                rectMin = ImVec2(rectMin.x + shift.x, rectMin.y + shift.y);
                rectMax = ImVec2(rectMax.x + shift.x, rectMax.y + shift.y);
            }

            float distanceScale = 1.0f;
            float perspectiveFactor = 1.0f;
            bool allowInteraction = false;
            ResolvePseudo3DDistanceState(canvas.ui, distance, distanceScale, perspectiveFactor, allowInteraction);

            PseudoPanelDrawEntry entry;
            entry.canvasId = canvas.id;
            entry.textureId = target.texture;
            entry.layoutSize = ImVec2(layoutSizePx.x, layoutSizePx.y);
            entry.corners = BuildPseudo3DPanelCorners(rectMin, rectMax, canvas.ui, distanceScale, perspectiveFactor);
            entry.depthSort = canvas.ui.pseudo3DDepthSort;
            entry.allowInteraction = allowInteraction;
            pseudoPanels.push_back(entry);
        }

        if (!pseudoPanels.empty()) {
            std::stable_sort(pseudoPanels.begin(), pseudoPanels.end(),
                             [](const PseudoPanelDrawEntry& a, const PseudoPanelDrawEntry& b) {
                                 if (a.depthSort != b.depthSort) return a.depthSort < b.depthSort;
                                 return a.canvasId < b.canvasId;
                             });

            ImDrawList* panelDrawList = ImGui::GetWindowDrawList();
            for (const PseudoPanelDrawEntry& panel : pseudoPanels) {
                panelDrawList->AddImageQuad(
                    (ImTextureID)(intptr_t)panel.textureId,
                    panel.corners[0], panel.corners[1], panel.corners[2], panel.corners[3],
                    ImVec2(0.0f, 1.0f), ImVec2(1.0f, 1.0f), ImVec2(1.0f, 0.0f), ImVec2(0.0f, 0.0f),
                    IM_COL32_WHITE);
            }

            if (imageHovered && !uiWorldCameraActive) {
                const ImVec2 mousePos = ImGui::GetIO().MousePos;
                bool inputAssigned = false;
                for (auto it = pseudoPanels.rbegin(); it != pseudoPanels.rend(); ++it) {
                    ImVec2 uv(0.0f, 0.0f);
                    if (!MapPointToPseudo3DQuadUV(it->corners, mousePos, uv)) {
                        continue;
                    }

                    pseudoPanelInteracting = pseudoPanelInteracting ||
                        ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                        ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                        std::abs(ImGui::GetIO().MouseWheel) > 0.0f;
                    if (!it->allowInteraction || inputAssigned) {
                        continue;
                    }

                    UiCanvas3DInput& input = uiCanvas3DInputs[it->canvasId];
                    const float u = std::clamp(uv.x, 0.0f, 1.0f);
                    const float v = std::clamp(uv.y, 0.0f, 1.0f);
                    input.mousePos = ImVec2(
                        u * std::max(1.0f, it->layoutSize.x),
                        (1.0f - v) * std::max(1.0f, it->layoutSize.y));
                    input.mouseDown[0] = ImGui::GetIO().MouseDown[0];
                    input.mouseDown[1] = ImGui::GetIO().MouseDown[1];
                    input.mouseDown[2] = ImGui::GetIO().MouseDown[2];
                    input.mouseWheel = ImGui::GetIO().MouseWheel;
                    input.hasInput = true;
                    input.hitT = -1000.0f - static_cast<float>(it->depthSort);
                    inputAssigned = true;
                }
            }
        }
        runtimeSpriteBatchBuildMs +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - spriteBatchBuildStart).count();

        uiInteracting = ImGui::IsAnyItemActive() || uiWorldCameraActive || pseudoPanelInteracting;
        ImGui::EndChild();
        ImGui::PopStyleVar();

        if (showingStartupSplash) {
            ImDrawList* splashDraw = ImGui::GetWindowDrawList();
            splashDraw->AddRectFilled(imageMin, imageMax, IM_COL32(0, 0, 0, 230));

            fs::path splashPath = resolveSplashImagePath();
            Texture* splashTex = nullptr;
            if (!splashPath.empty() && fs::exists(splashPath)) {
                splashTex = renderer.getTexture(splashPath.string());
            }
            if (splashTex) {
                float availW = imageMax.x - imageMin.x;
                float availH = imageMax.y - imageMin.y;
                float texW = static_cast<float>(std::max(1, splashTex->GetWidth()));
                float texH = static_cast<float>(std::max(1, splashTex->GetHeight()));
                float scale = std::min(availW / texW, availH / texH);
                scale = std::min(scale, 1.0f);
                ImVec2 drawSize(texW * scale, texH * scale);
                ImVec2 drawMin(imageMin.x + (availW - drawSize.x) * 0.5f,
                               imageMin.y + (availH - drawSize.y) * 0.5f);
                ImVec2 drawMax(drawMin.x + drawSize.x, drawMin.y + drawSize.y);
                splashDraw->AddImage((ImTextureID)(intptr_t)splashTex->GetID(), drawMin, drawMax,
                                     ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
            } else {
                const char* fallback = "Loading...";
                ImVec2 textSize = ImGui::CalcTextSize(fallback);
                ImVec2 textPos((imageMin.x + imageMax.x) * 0.5f - textSize.x * 0.5f,
                               (imageMin.y + imageMax.y) * 0.5f - textSize.y * 0.5f);
                splashDraw->AddText(textPos, IM_COL32(240, 240, 240, 255), fallback);
            }

            std::string splashTitle = buildSettings.buildName;
            if (splashTitle.empty()) splashTitle = "Game";
            if (!buildSettings.version.empty()) splashTitle += " " + buildSettings.version;
            ImVec2 titleSize = ImGui::CalcTextSize(splashTitle.c_str());
            splashDraw->AddText(ImVec2((imageMin.x + imageMax.x) * 0.5f - titleSize.x * 0.5f,
                                       imageMax.y - titleSize.y - 32.0f),
                               IM_COL32(240, 240, 240, 230), splashTitle.c_str());
        }

        if (showingStartupSplash && gameViewCursorLocked) {
            gameViewCursorLocked = false;
        }
        bool clicked = imageHovered && isPlaying && !showingStartupSplash &&
                       ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !uiInteracting;
        if (clicked && !gameViewCursorLocked) {
            gameViewCursorLocked = true;
        }
        if (gameViewCursorLocked && (!isPlaying || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
            gameViewCursorLocked = false;
        }
        gameViewportFocused = windowFocused || gameViewCursorLocked;
    }

    const auto runtimeUiEnd = std::chrono::steady_clock::now();
    ModuRuntime2DProfiler_RecordUiRuntime(
        std::chrono::duration<double, std::milli>(runtimeUiEnd - runtimeUiStart).count(),
        runtimeSpriteBatchBuildMs,
        runtimeVisibleObjectCount);

    ImGui::End();
}
#pragma endregion
