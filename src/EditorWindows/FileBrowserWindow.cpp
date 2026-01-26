#include "Engine.h"
#include "ModelLoader.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cerrno>
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
#include <shellapi.h>
#endif

#pragma region File Icons
namespace FileIcons {
    namespace {
        ImU32 BlendColor(ImU32 a, ImU32 b, float t) {
            int ar = a & 0xFF;
            int ag = (a >> 8) & 0xFF;
            int ab = (a >> 16) & 0xFF;
            int aa = (a >> 24) & 0xFF;
            int br = b & 0xFF;
            int bg = (b >> 8) & 0xFF;
            int bb = (b >> 16) & 0xFF;
            int ba = (b >> 24) & 0xFF;
            int r = ar + static_cast<int>((br - ar) * t);
            int g = ag + static_cast<int>((bg - ag) * t);
            int bch = ab + static_cast<int>((bb - ab) * t);
            int aout = aa + static_cast<int>((ba - aa) * t);
            return IM_COL32(r, g, bch, aout);
        }

        void DrawPaperSpeckles(ImDrawList* drawList, ImVec2 min, ImVec2 max, ImU32 color) {
            const ImVec2 points[] = {
                ImVec2(0.18f, 0.22f), ImVec2(0.42f, 0.36f), ImVec2(0.66f, 0.28f),
                ImVec2(0.28f, 0.62f), ImVec2(0.52f, 0.68f), ImVec2(0.74f, 0.58f),
                ImVec2(0.36f, 0.82f), ImVec2(0.82f, 0.78f)
            };
            float w = max.x - min.x;
            float h = max.y - min.y;
            float r = std::min(w, h) * 0.015f;
            for (const auto& p : points) {
                ImVec2 dot(min.x + w * p.x, min.y + h * p.y);
                drawList->AddCircleFilled(dot, r, color);
            }
        }

        struct PaperFrame {
            ImVec2 min;
            ImVec2 max;
        };

        PaperFrame DrawPaperFileBase(ImDrawList* drawList, ImVec2 pos, float size, ImU32 accentColor) {
            const ImU32 kPaperBase = IM_COL32(205, 185, 128, 255);
            const ImU32 kPaperEdge = IM_COL32(122, 106, 72, 200);
            const ImU32 kPaperFold = IM_COL32(223, 206, 150, 230);
            const ImU32 kPaperShadow = IM_COL32(110, 95, 60, 80);
            const ImU32 kPaperSpeck = IM_COL32(120, 110, 80, 55);

            float w = size * 0.78f;
            float h = size * 0.95f;
            float offsetX = (size - w) * 0.5f;
            float offsetY = (size - h) * 0.5f;
            float cornerSize = w * 0.22f;
            float rounding = size * 0.08f;
            float shadowOffset = size * 0.04f;

            ImVec2 min = ImVec2(pos.x + offsetX, pos.y + offsetY);
            ImVec2 max = ImVec2(pos.x + offsetX + w, pos.y + offsetY + h);

            drawList->AddRectFilled(ImVec2(min.x + shadowOffset, min.y + shadowOffset),
                                    ImVec2(max.x + shadowOffset, max.y + shadowOffset),
                                    kPaperShadow, rounding);

            drawList->AddRectFilled(min, max, kPaperBase, rounding);
            drawList->AddRect(min, max, kPaperEdge, rounding, 0, 1.2f);

            ImVec2 foldA(max.x - cornerSize, min.y);
            ImVec2 foldB(max.x, min.y + cornerSize);
            drawList->AddTriangleFilled(foldA, foldB, ImVec2(max.x - cornerSize, min.y + cornerSize), kPaperFold);

            ImU32 band = BlendColor(accentColor, kPaperBase, 0.65f);
            float bandH = h * 0.14f;
            drawList->AddRectFilled(ImVec2(min.x, max.y - bandH), max, band, rounding);

            DrawPaperSpeckles(drawList, min, ImVec2(max.x, max.y - bandH), kPaperSpeck);

            ImVec2 contentMin(min.x + w * 0.12f, min.y + h * 0.16f);
            ImVec2 contentMax(max.x - w * 0.12f, max.y - h * 0.22f);
            return {contentMin, contentMax};
        }

        PaperFrame DrawSheetFileBase(ImDrawList* drawList, ImVec2 pos, float size, ImU32 accentColor) {
            const ImU32 kSheetBase = IM_COL32(248, 248, 248, 255);
            const ImU32 kSheetBack = IM_COL32(238, 238, 238, 235);
            const ImU32 kSheetEdge = IM_COL32(185, 185, 185, 200);
            const ImU32 kSheetFold = IM_COL32(232, 232, 232, 255);
            const ImU32 kSheetShadow = IM_COL32(0, 0, 0, 55);
            const ImU32 kSheetInner = IM_COL32(255, 255, 255, 80);
            const ImU32 kFoldShadow = IM_COL32(0, 0, 0, 35);

            float w = size * 0.78f;
            float h = size * 0.95f;
            float offsetX = (size - w) * 0.5f;
            float offsetY = (size - h) * 0.5f;
            float cornerSize = w * 0.24f;
            float rounding = size * 0.075f;
            float shadowOffset = size * 0.04f;
            float backOffset = size * 0.02f;

            ImVec2 min = ImVec2(pos.x + offsetX, pos.y + offsetY);
            ImVec2 max = ImVec2(pos.x + offsetX + w, pos.y + offsetY + h);

            ImVec2 backMin = ImVec2(min.x + backOffset, min.y + backOffset * 0.6f);
            ImVec2 backMax = ImVec2(max.x + backOffset, max.y + backOffset * 0.6f);
            drawList->AddRectFilled(backMin, backMax, kSheetBack, rounding);

            drawList->AddRectFilled(ImVec2(min.x + shadowOffset, min.y + shadowOffset),
                                    ImVec2(max.x + shadowOffset, max.y + shadowOffset),
                                    kSheetShadow, rounding);

            drawList->AddRectFilled(min, max, kSheetBase, rounding);
            drawList->AddRect(min, max, kSheetEdge, rounding, 0, 1.2f);
            drawList->AddRect(ImVec2(min.x + 1.0f, min.y + 1.0f),
                              ImVec2(max.x - 1.0f, max.y - 1.0f),
                              kSheetInner, rounding, 0, 1.0f);

            ImVec2 foldA(max.x - cornerSize, min.y);
            ImVec2 foldB(max.x, min.y + cornerSize);
            ImVec2 foldC(max.x - cornerSize, min.y + cornerSize);
            drawList->AddTriangleFilled(ImVec2(foldA.x + 1.0f, foldA.y + 1.0f),
                                        ImVec2(foldB.x + 1.0f, foldB.y + 1.0f),
                                        ImVec2(foldC.x + 1.0f, foldC.y + 1.0f),
                                        kFoldShadow);
            drawList->AddTriangleFilled(foldA, foldB, ImVec2(max.x - cornerSize, min.y + cornerSize), kSheetFold);
            drawList->AddTriangle(foldA, foldB, foldC, kSheetEdge, 1.0f);

            ImU32 band = BlendColor(accentColor, kSheetBase, 0.55f);
            float bandH = h * 0.16f;
            drawList->AddRectFilled(ImVec2(min.x, max.y - bandH), max, band, rounding);

            ImVec2 contentMin(min.x + w * 0.12f, min.y + h * 0.16f);
            ImVec2 contentMax(max.x - w * 0.12f, max.y - h * 0.24f);
            return {contentMin, contentMax};
        }

        ImU32 InkColor() {
            return IM_COL32(92, 78, 52, 220);
        }
    }

    // Draw a folder icon
    void DrawFolderIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        const ImU32 kFolderBase = IM_COL32(198, 178, 120, 255);
        const ImU32 kFolderTab = IM_COL32(215, 197, 140, 255);
        const ImU32 kFolderEdge = IM_COL32(120, 104, 70, 200);
        const ImU32 kFolderShadow = IM_COL32(110, 95, 60, 60);

        float w = size;
        float totalH = size * 0.82f;
        float bodyY = size * 0.273f;
        float bodyH = size * 0.547f;
        float tabY = size * 0.125f;
        float tabH = size * 0.203f;
        float tabW = size * 0.43f;
        float rounding = size * 0.07f;
        float shadowOffset = size * 0.025f;
        float outline = std::max(1.2f, size * 0.06f);

        ImVec2 bodyMin(pos.x, pos.y + bodyY);
        ImVec2 bodyMax(pos.x + w, pos.y + bodyY + bodyH);
        ImVec2 tabMin(pos.x + w * 0.031f, pos.y + tabY);
        ImVec2 tabMax(pos.x + w * 0.031f + tabW, pos.y + tabY + tabH);

        drawList->AddRectFilled(ImVec2(bodyMin.x + shadowOffset, bodyMin.y + shadowOffset),
                                ImVec2(bodyMax.x + shadowOffset, bodyMax.y + shadowOffset),
                                kFolderShadow, rounding);

        drawList->AddRectFilled(bodyMin, bodyMax, kFolderBase, rounding);
        drawList->AddRectFilled(tabMin, tabMax, kFolderTab, rounding);

        ImU32 band = BlendColor(color, kFolderBase, 0.7f);
        float bandH = size * 0.094f;
        drawList->AddRectFilled(ImVec2(bodyMin.x, bodyMax.y - bandH), bodyMax, band, rounding);

        drawList->AddRect(bodyMin, bodyMax, kFolderEdge, rounding, 0, outline);
        drawList->AddRect(tabMin, tabMax, kFolderEdge, rounding, 0, outline * 0.85f);
    }

    void DrawFolderIconOpen(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        const ImU32 kFolderBase = IM_COL32(198, 178, 120, 255);
        const ImU32 kFolderTab = IM_COL32(215, 197, 140, 255);
        const ImU32 kFolderEdge = IM_COL32(120, 104, 70, 200);
        const ImU32 kFolderShadow = IM_COL32(110, 95, 60, 55);
        const ImU32 kPaperFill = IM_COL32(245, 245, 245, 255);

        float w = size;
        float bodyY = size * 0.273f;
        float bodyH = size * 0.547f;
        float tabY = size * 0.125f;
        float tabH = size * 0.203f;
        float tabW = size * 0.43f;
        float rounding = size * 0.075f;
        float shadowOffset = size * 0.02f;
        float outline = std::max(1.2f, size * 0.06f);
        float openOffset = size * 0.06f;

        ImVec2 bodyMin(pos.x, pos.y + bodyY);
        ImVec2 bodyMax(pos.x + w, pos.y + bodyY + bodyH);
        ImVec2 backMin(bodyMin.x, bodyMin.y - openOffset);
        ImVec2 backMax(bodyMax.x, bodyMax.y - openOffset);
        ImVec2 tabMin(pos.x + w * 0.031f, pos.y + tabY - openOffset * 0.4f);
        ImVec2 tabMax(pos.x + w * 0.031f + tabW, pos.y + tabY + tabH - openOffset * 0.4f);

        drawList->AddRectFilled(ImVec2(bodyMin.x + shadowOffset, bodyMin.y + shadowOffset),
                                ImVec2(bodyMax.x + shadowOffset, bodyMax.y + shadowOffset),
                                kFolderShadow, rounding);

        drawList->AddRectFilled(backMin, backMax, kFolderTab, rounding);
        drawList->AddRect(backMin, backMax, kFolderEdge, rounding, 0, outline * 0.8f);

        drawList->AddRectFilled(tabMin, tabMax, kFolderTab, rounding);
        drawList->AddRect(tabMin, tabMax, kFolderEdge, rounding, 0, outline * 0.75f);

        ImVec2 paperMin(pos.x + w * 0.1f, backMin.y + bodyH * 0.08f);
        ImVec2 paperMax(pos.x + w * 0.9f, backMin.y + bodyH * 0.62f);
        float paperRadius = rounding * 0.8f;
        float paperOutline = outline * 0.6f;

        ImVec2 p1Min = ImVec2(paperMin.x + w * 0.02f, paperMin.y - size * 0.04f);
        ImVec2 p1Max = ImVec2(paperMax.x - w * 0.02f, paperMax.y - size * 0.04f);
        ImVec2 p2Min = ImVec2(paperMin.x + w * 0.04f, paperMin.y - size * 0.02f);
        ImVec2 p2Max = ImVec2(paperMax.x - w * 0.04f, paperMax.y - size * 0.02f);
        ImVec2 p3Min = paperMin;
        ImVec2 p3Max = paperMax;

        drawList->AddRectFilled(p1Min, p1Max, kPaperFill, paperRadius);
        drawList->AddRect(p1Min, p1Max, kFolderEdge, paperRadius, 0, paperOutline);
        drawList->AddRectFilled(p2Min, p2Max, kPaperFill, paperRadius);
        drawList->AddRect(p2Min, p2Max, kFolderEdge, paperRadius, 0, paperOutline);
        drawList->AddRectFilled(p3Min, p3Max, kPaperFill, paperRadius);
        drawList->AddRect(p3Min, p3Max, kFolderEdge, paperRadius, 0, paperOutline);

        ImVec2 frontMin(bodyMin.x, bodyMin.y + openOffset);
        ImVec2 frontMax(bodyMax.x, bodyMax.y);

        drawList->AddRectFilled(frontMin, frontMax, kFolderBase, rounding);
        ImU32 band = BlendColor(color, kFolderBase, 0.7f);
        float bandH = size * 0.094f;
        drawList->AddRectFilled(ImVec2(frontMin.x, frontMax.y - bandH), frontMax, band, rounding);
        drawList->AddRect(frontMin, frontMax, kFolderEdge, rounding, 0, outline);
    }
    
    // Draw a scene/document icon
    void DrawSceneIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        PaperFrame frame = DrawSheetFileBase(drawList, pos, size, color);
        ImU32 ink = IM_COL32(90, 90, 90, 230);
        float w = frame.max.x - frame.min.x;
        float h = frame.max.y - frame.min.y;
        ImVec2 center(frame.min.x + w * 0.55f, frame.min.y + h * 0.55f);
        float tri = std::min(w, h) * 0.35f;
        drawList->AddTriangleFilled(
            ImVec2(center.x - tri * 0.4f, center.y - tri * 0.55f),
            ImVec2(center.x - tri * 0.4f, center.y + tri * 0.55f),
            ImVec2(center.x + tri * 0.6f, center.y),
            ink
        );
    }
    
    // Draw a 3D model icon (cube wireframe)
    void DrawModelIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        PaperFrame frame = DrawSheetFileBase(drawList, pos, size, color);
        ImU32 ink = IM_COL32(90, 90, 90, 230);
        ImVec2 min = frame.min;
        ImVec2 max = frame.max;
        float w = max.x - min.x;
        float h = max.y - min.y;
        float s = std::min(w, h) * 0.6f;
        ImVec2 origin(min.x + w * 0.25f, min.y + h * 0.25f);
        float depth = s * 0.35f;

        ImVec2 f1(origin.x, origin.y + depth);
        ImVec2 f2(origin.x + s, origin.y + depth);
        ImVec2 f3(origin.x + s, origin.y + s + depth);
        ImVec2 f4(origin.x, origin.y + s + depth);

        ImVec2 b1(f1.x + depth, f1.y - depth);
        ImVec2 b2(f2.x + depth, f2.y - depth);
        ImVec2 b3(f3.x + depth, f3.y - depth);
        ImVec2 b4(f4.x + depth, f4.y - depth);

        drawList->AddLine(f1, f2, ink, 1.4f);
        drawList->AddLine(f2, f3, ink, 1.4f);
        drawList->AddLine(f3, f4, ink, 1.4f);
        drawList->AddLine(f4, f1, ink, 1.4f);
        drawList->AddLine(b1, b2, ink, 1.2f);
        drawList->AddLine(b2, b3, ink, 1.2f);
        drawList->AddLine(b3, b4, ink, 1.2f);
        drawList->AddLine(b4, b1, ink, 1.2f);
        drawList->AddLine(f1, b1, ink, 1.2f);
        drawList->AddLine(f2, b2, ink, 1.2f);
        drawList->AddLine(f3, b3, ink, 1.2f);
        drawList->AddLine(f4, b4, ink, 1.2f);
    }
    
    // Draw a texture/image icon
    void DrawTextureIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        PaperFrame frame = DrawSheetFileBase(drawList, pos, size, color);
        ImU32 ink = IM_COL32(90, 90, 90, 230);
        ImVec2 min = frame.min;
        ImVec2 max = frame.max;
        float w = max.x - min.x;
        float h = max.y - min.y;

        ImVec2 frameMin(min.x + w * 0.08f, min.y + h * 0.12f);
        ImVec2 frameMax(max.x - w * 0.08f, max.y - h * 0.1f);
        drawList->AddRect(frameMin, frameMax, ink, 3.0f, 0, 1.2f);

        float midY = frameMax.y - h * 0.28f;
        drawList->AddTriangleFilled(
            ImVec2(frameMin.x + w * 0.1f, frameMax.y),
            ImVec2(frameMin.x + w * 0.35f, midY),
            ImVec2(frameMin.x + w * 0.6f, frameMax.y),
            ink
        );
        drawList->AddTriangleFilled(
            ImVec2(frameMin.x + w * 0.45f, frameMax.y),
            ImVec2(frameMin.x + w * 0.7f, midY + h * 0.1f),
            ImVec2(frameMin.x + w * 0.9f, frameMax.y),
            ink
        );

        float sunR = std::min(w, h) * 0.12f;
        drawList->AddCircleFilled(ImVec2(frameMax.x - w * 0.18f, frameMin.y + h * 0.2f), sunR, ink);
    }
    
    // Draw a shader icon (code brackets)
    void DrawShaderIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        PaperFrame frame = DrawSheetFileBase(drawList, pos, size, color);
        ImU32 ink = IM_COL32(90, 90, 90, 230);
        ImVec2 min = frame.min;
        ImVec2 max = frame.max;
        float w = max.x - min.x;
        float h = max.y - min.y;

        float lineH = h * 0.12f;
        float lineY = min.y + h * 0.2f;
        drawList->AddRectFilled(ImVec2(min.x + w * 0.15f, lineY), ImVec2(min.x + w * 0.75f, lineY + lineH), ink);
        lineY += h * 0.22f;
        drawList->AddRectFilled(ImVec2(min.x + w * 0.2f, lineY), ImVec2(min.x + w * 0.85f, lineY + lineH), ink);
        lineY += h * 0.22f;
        drawList->AddRectFilled(ImVec2(min.x + w * 0.15f, lineY), ImVec2(min.x + w * 0.55f, lineY + lineH), ink);
    }
    
    // Draw an audio icon (speaker/waveform)
    void DrawAudioIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        PaperFrame frame = DrawSheetFileBase(drawList, pos, size, color);
        ImU32 ink = IM_COL32(90, 90, 90, 230);
        ImVec2 min = frame.min;
        ImVec2 max = frame.max;
        float w = max.x - min.x;
        float h = max.y - min.y;
        float spkW = w * 0.25f;
        float spkH = h * 0.28f;
        float cx = min.x + w * 0.35f;
        float cy = min.y + h * 0.55f;

        drawList->AddRectFilled(
            ImVec2(cx - spkW * 0.5f, cy - spkH * 0.5f),
            ImVec2(cx + spkW * 0.5f, cy + spkH * 0.5f),
            ink
        );
        drawList->AddTriangleFilled(
            ImVec2(cx + spkW * 0.5f, cy - spkH * 0.5f),
            ImVec2(cx + spkW * 0.5f, cy + spkH * 0.5f),
            ImVec2(cx + spkW * 1.1f, cy + spkH * 0.9f),
            ink
        );
        drawList->AddTriangleFilled(
            ImVec2(cx + spkW * 0.5f, cy - spkH * 0.5f),
            ImVec2(cx + spkW * 1.1f, cy - spkH * 0.9f),
            ImVec2(cx + spkW * 1.1f, cy + spkH * 0.9f),
            ink
        );

        float waveX = cx + spkW * 1.4f;
        drawList->AddBezierQuadratic(
            ImVec2(waveX, cy - h * 0.12f),
            ImVec2(waveX + w * 0.12f, cy),
            ImVec2(waveX, cy + h * 0.12f),
            ink, 1.6f
        );
        waveX += w * 0.12f;
        drawList->AddBezierQuadratic(
            ImVec2(waveX, cy - h * 0.2f),
            ImVec2(waveX + w * 0.14f, cy),
            ImVec2(waveX, cy + h * 0.2f),
            ink, 1.6f
        );
    }
    
    // Draw a generic file icon
    void DrawFileIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        DrawSheetFileBase(drawList, pos, size, color);
    }
    
    // Draw a script/code icon
    void DrawScriptIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        PaperFrame frame = DrawSheetFileBase(drawList, pos, size, color);
        ImU32 ink = IM_COL32(90, 90, 90, 230);
        ImVec2 min = frame.min;
        ImVec2 max = frame.max;
        float w = max.x - min.x;
        float h = max.y - min.y;
        float cx = min.x + w * 0.5f;
        float cy = min.y + h * 0.5f;
        float bSize = std::min(w, h) * 0.28f;

        drawList->AddLine(ImVec2(cx - bSize * 0.5f, cy - bSize), ImVec2(cx - bSize * 1.3f, cy), ink, 1.8f);
        drawList->AddLine(ImVec2(cx - bSize * 1.3f, cy), ImVec2(cx - bSize * 0.5f, cy + bSize), ink, 1.8f);
        drawList->AddLine(ImVec2(cx + bSize * 0.5f, cy - bSize), ImVec2(cx + bSize * 1.3f, cy), ink, 1.8f);
        drawList->AddLine(ImVec2(cx + bSize * 1.3f, cy), ImVec2(cx + bSize * 0.5f, cy + bSize), ink, 1.8f);
    }
    
    // Draw a text icon
    void DrawTextIcon(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
        PaperFrame frame = DrawSheetFileBase(drawList, pos, size, color);
        ImU32 ink = IM_COL32(90, 90, 90, 230);
        ImVec2 min = frame.min;
        ImVec2 max = frame.max;
        float w = max.x - min.x;
        float h = max.y - min.y;
        float lineY = min.y + h * 0.25f;
        float lineH = h * 0.12f;
        float spacing = h * 0.2f;

        for (int i = 0; i < 3; ++i) {
            float lineW = (i == 1) ? w * 0.55f : w * 0.75f;
            drawList->AddRectFilled(ImVec2(min.x + w * 0.12f, lineY),
                                    ImVec2(min.x + w * 0.12f + lineW, lineY + lineH),
                                    ink);
            lineY += spacing;
        }
    }
    
    void DrawIcon(ImDrawList* drawList, FileCategory category, ImVec2 pos, float size, ImU32 color, bool folderHasItems) {
        switch (category) {
            case FileCategory::Folder:
                if (folderHasItems) {
                    DrawFolderIconOpen(drawList, pos, size, color);
                } else {
                    DrawFolderIcon(drawList, pos, size, color);
                }
                break;
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
#pragma endregion

#pragma region File Actions
namespace {
    enum class CreateKind {
        Folder,
        CppScript,
        Header,
        Text,
        Json,
        Shader
    };

    bool FolderHasVisibleItems(const fs::path& path, bool showHiddenFiles) {
        std::error_code ec;
        for (fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::directory_iterator();
             ++it) {
            const std::string name = it->path().filename().string();
            if (!showHiddenFiles && !name.empty() && name[0] == '.') {
                continue;
            }
            return true;
        }
        return false;
    }

    fs::path makeUniquePath(const fs::path& basePath) {
        if (!fs::exists(basePath)) {
            return basePath;
        }
        std::string stem = basePath.stem().string();
        std::string ext = basePath.extension().string();
        for (int i = 1; i < 10000; ++i) {
            fs::path candidate = basePath.parent_path() / (stem + std::to_string(i) + ext);
            if (!fs::exists(candidate)) {
                return candidate;
            }
        }
        return basePath;
    }

    bool writeFileContents(const fs::path& path, const std::string& contents) {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            return false;
        }
        out << contents;
        return static_cast<bool>(out);
    }

    bool openPathInShell(const fs::path& path) {
        #ifdef _WIN32
        std::wstring widePath = path.wstring();
        HINSTANCE result = ShellExecuteW(nullptr, L"open", widePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
        #elif __linux__
        std::string cmd = "xdg-open \"" + path.string() + "\"";
        return std::system(cmd.c_str()) == 0;
        #else
        return false;
        #endif
    }

    void openPathInFileManager(const fs::path& path) {
        #ifdef _WIN32
        std::wstring widePath = path.wstring();
        if (fs::is_directory(path)) {
            ShellExecuteW(nullptr, L"open", widePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else {
            std::wstring args = L"/select,\"" + widePath + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        }
        #elif __linux__
        fs::path folder = fs::is_directory(path) ? path : path.parent_path();
        std::string cmd = "xdg-open \"" + folder.string() + "\"";
        std::system(cmd.c_str());
        #endif
    }

    void openPathWithDialog(const fs::path& path) {
        #ifdef _WIN32
        std::wstring widePath = path.wstring();
        std::wstring args = L"shell32.dll,OpenAs_RunDLL \"" + widePath + L"\"";
        ShellExecuteW(nullptr, nullptr, L"rundll32.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        #else
        openPathInShell(path);
        #endif
    }
}
#pragma endregion

#pragma region File Browser Panel
// Uses FileBrowser state for navigation, selection, and drag-drop.
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

    static bool showDeletePopup = false;
    static bool showRenamePopup = false;
    static bool triggerDeletePopup = false;
    static bool triggerRenamePopup = false;
    static fs::path pendingDeletePath;
    static fs::path pendingRenamePath;
    static char renameName[256] = "";
    bool settingsDirty = false;

    auto openEntry = [&](const fs::directory_entry& entry) {
        if (entry.is_directory()) {
            fileBrowser.navigateTo(entry.path());
            return;
        }
        if (fileBrowser.isModelFile(entry)) {
            bool isObj = fileBrowser.isOBJFile(entry);
            std::string defaultName = entry.path().stem().string();
            if (isObj) {
                pendingOBJPath = entry.path().string();
                strncpy(importOBJName, defaultName.c_str(), sizeof(importOBJName) - 1);
                importOBJName[sizeof(importOBJName) - 1] = '\0';
                showImportOBJDialog = true;
            } else {
                pendingModelPath = entry.path().string();
                strncpy(importModelName, defaultName.c_str(), sizeof(importModelName) - 1);
                importModelName[sizeof(importModelName) - 1] = '\0';
                showImportModelDialog = true;
            }
            return;
        }
        if (fileBrowser.getFileCategory(entry) == FileCategory::Material) {
            if (SceneObject* sel = getSelectedObject()) {
                sel->materialPath = entry.path().string();
                loadMaterialFromFile(*sel);
            }
            return;
        }
        if (fileBrowser.isSceneFile(entry)) {
            std::string sceneName = entry.path().stem().string();
            loadScene(sceneName);
            logToConsole("Loaded scene: " + sceneName);
            return;
        }
        if (fileBrowser.getFileCategory(entry) == FileCategory::Script) {
            openScriptInEditor(entry.path());
            return;
        }
        openPathInShell(entry.path());
    };

    auto createEntry = [&](const fs::path& dir, CreateKind kind, const std::string& name) {
        fs::path baseDir = dir.empty() ? fileBrowser.currentPath : dir;
        fs::path target = baseDir / name;
        if (kind != CreateKind::Folder && target.extension().empty()) {
            switch (kind) {
                case CreateKind::CppScript: target += ".cpp"; break;
                case CreateKind::Header: target += ".h"; break;
                case CreateKind::Text: target += ".txt"; break;
                case CreateKind::Json: target += ".json"; break;
                case CreateKind::Shader: target += ".glsl"; break;
                default: break;
            }
        }
        target = makeUniquePath(target);

        std::error_code ec;
        if (!baseDir.empty() && !fs::exists(baseDir)) {
            fs::create_directories(baseDir, ec);
        }
        if (!baseDir.empty() && !fs::exists(baseDir)) {
            addConsoleMessage("Create failed: target folder missing " + baseDir.string(),
                              ConsoleMessageType::Error);
            return false;
        }

        bool created = false;
        if (kind == CreateKind::Folder) {
            created = fs::create_directories(target, ec);
        } else {
            std::string contents;
            switch (kind) {
                case CreateKind::CppScript:
                    contents =
                        "#include \"ScriptRuntime.h\"\n"
                        "#include \"SceneObject.h\"\n"
                        "#include \"ThirdParty/imgui/imgui.h\"\n"
                        "\n"
                        "extern \"C\" void Script_OnInspector(ScriptContext& ctx) {\n"
                        "    ImGui::TextUnformatted(\"New Script\");\n"
                        "}\n"
                        "\n"
                        "void Begin(ScriptContext& ctx, float /*deltaTime*/) {\n"
                        "}\n"
                        "\n"
                        "void TickUpdate(ScriptContext& ctx, float /*deltaTime*/) {\n"
                        "}\n";
                    break;
                case CreateKind::Header:
                    contents = "#pragma once\n";
                    break;
                case CreateKind::Json:
                    contents = "{\n}\n";
                    break;
                case CreateKind::Shader:
                    contents =
                        "// Shader entry point\n"
                        "void main() {\n"
                        "}\n";
                    break;
                case CreateKind::Text:
                default:
                    contents.clear();
                    break;
            }
            created = writeFileContents(target, contents);
        }

        if (created) {
            fileBrowser.selectedFile = target;
            fileBrowser.refresh();
            addConsoleMessage("Created: " + target.string(), ConsoleMessageType::Success);
            return true;
        }

        std::string reason = ec ? ec.message() : std::strerror(errno);
        if (kind == CreateKind::Folder) {
            addConsoleMessage("Create failed: unable to create folder " + target.string() + " (" + reason + ")",
                              ConsoleMessageType::Error);
        } else {
            addConsoleMessage("Create failed: unable to write " + target.string() + " (" + reason + ")",
                              ConsoleMessageType::Error);
        }
        return false;
    };

    auto normalizePath = [](const fs::path& path) {
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(path, ec);
        if (!ec) {
            return canonical;
        }
        return path.lexically_normal();
    };
    
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
        if (ImGui::SliderFloat("##IconScale", &fileBrowserIconScale, 0.6f, 2.0f, "%.1fx")) {
            settingsDirty = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Icon Size: %.1fx", fileBrowserIconScale);
        ImGui::SameLine();
    }

    if (ImGui::Button(isGridMode ? "Grid" : "List", ImVec2(54, 0))) {
        fileBrowser.viewMode = isGridMode ? FileBrowserViewMode::List : FileBrowserViewMode::Grid;
        settingsDirty = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(isGridMode ? "Switch to List View" : "Switch to Grid View");
    ImGui::SameLine();
    if (ImGui::Button(showFileBrowserSidebar ? "Side" : "Side", ImVec2(52, 0))) {
        showFileBrowserSidebar = !showFileBrowserSidebar;
        settingsDirty = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle sidebar");

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
    if (showFileBrowserSidebar) {
        float minSidebarWidth = 160.0f;
        float maxSidebarWidth = std::max(minSidebarWidth, ImGui::GetContentRegionAvail().x * 0.5f);
        fileBrowserSidebarWidth = std::clamp(fileBrowserSidebarWidth, minSidebarWidth, maxSidebarWidth);

        ImGui::BeginChild("FileSidebar", ImVec2(fileBrowserSidebarWidth, 0), true);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
        ImGui::TextDisabled("Favorites");
        ImGui::SameLine();
        if (ImGui::SmallButton("+")) {
            fs::path current = normalizePath(fileBrowser.currentPath);
            bool exists = false;
            for (const auto& fav : fileBrowserFavorites) {
                if (normalizePath(fav) == current) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                fileBrowserFavorites.push_back(current);
                settingsDirty = true;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add current folder");

        fs::path baseRoot = fileBrowser.projectRoot.empty()
            ? projectManager.currentProject.projectPath
            : fileBrowser.projectRoot;
        fs::path normalizedCurrent = normalizePath(fileBrowser.currentPath);

        for (size_t i = 0; i < fileBrowserFavorites.size(); ++i) {
            fs::path fav = fileBrowserFavorites[i];
            std::string label;
            std::error_code ec;
            fs::path rel = fs::relative(fav, baseRoot, ec);
            std::string relStr = rel.generic_string();
            if (!ec && !rel.empty() && relStr.find("..") != 0) {
                label = relStr;
                if (label.empty() || label == ".") {
                    label = "Project";
                }
            } else {
                label = fav.filename().string();
                if (label.empty()) {
                    label = fav.string();
                }
            }

            bool exists = fs::exists(fav);
            ImGui::PushID(static_cast<int>(i));
            if (!exists) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Selectable(label.c_str(), normalizePath(fav) == normalizedCurrent)) {
                if (exists) {
                    fileBrowser.navigateTo(fav);
                }
            }
            if (!exists) {
                ImGui::EndDisabled();
            }
            if (ImGui::BeginPopupContextItem("FavContext")) {
                if (ImGui::MenuItem("Remove")) {
                    fileBrowserFavorites.erase(fileBrowserFavorites.begin() + static_cast<int>(i));
                    settingsDirty = true;
                    ImGui::EndPopup();
                    ImGui::PopID();
                    break;
                }
                if (exists && ImGui::MenuItem("Open in File Explorer")) {
                    openPathInFileManager(fav);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::TextDisabled("Folders");
        ImGui::BeginChild("FolderTree", ImVec2(0, 0), false);

        auto drawFolderTree = [&](auto&& self, const fs::path& path) -> void {
            if (!fs::exists(path)) {
                return;
            }
            std::string name = path.filename().string();
            if (name.empty()) {
                name = "Project";
            }
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                       ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                       ImGuiTreeNodeFlags_SpanFullWidth;
            if (fileBrowser.currentPath == path) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            ImGui::PushID(path.string().c_str());
            bool open = ImGui::TreeNodeEx(name.c_str(), flags);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                fileBrowser.navigateTo(path);
            }
            if (open) {
                std::vector<fs::path> dirs;
                std::error_code ec;
                for (const auto& entry : fs::directory_iterator(path, ec)) {
                    if (ec) {
                        break;
                    }
                    if (!entry.is_directory()) {
                        continue;
                    }
                    std::string dirName = entry.path().filename().string();
                    if (!fileBrowser.showHiddenFiles && !dirName.empty() && dirName[0] == '.') {
                        continue;
                    }
                    dirs.push_back(entry.path());
                }
                std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
                    return a.filename().string() < b.filename().string();
                });
                for (const auto& dir : dirs) {
                    self(self, dir);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        };

        if (!baseRoot.empty()) {
            drawFolderTree(drawFolderTree, baseRoot);
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::EndChild();

        ImGui::SameLine();
        float splitterHeight = ImGui::GetContentRegionAvail().y;
        if (splitterHeight < 1.0f) {
            splitterHeight = 1.0f;
        }
        ImGui::InvisibleButton("SidebarSplitter", ImVec2(4.0f, splitterHeight));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (ImGui::IsItemActive()) {
            fileBrowserSidebarWidth += ImGui::GetIO().MouseDelta.x;
            fileBrowserSidebarWidth = std::clamp(fileBrowserSidebarWidth, minSidebarWidth, maxSidebarWidth);
            settingsDirty = true;
        }
        ImGui::SameLine();
    }

    ImGui::BeginChild("FileMain", ImVec2(0, 0), false);

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
                bool folderHasItems = category == FileCategory::Folder &&
                                      entry.is_directory() &&
                                      FolderHasVisibleItems(entry.path(), fileBrowser.showHiddenFiles);

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
                FileIcons::DrawIcon(drawList, category, iconPos, iconSize, getCategoryColor(category), folderHasItems);

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
                    openEntry(entry);
                }

                // Context menu
                if (ImGui::BeginPopupContextItem("FileContextMenu")) {
                    if (ImGui::MenuItem("Open")) {
                        openEntry(entry);
                    }
                    #ifdef _WIN32
                    if (!entry.is_directory() && ImGui::MenuItem("Open With...")) {
                        openPathWithDialog(entry.path());
                    }
                    #endif
                    if (entry.is_directory() && ImGui::BeginMenu("New")) {
                        if (ImGui::MenuItem("Folder")) {
                            createEntry(entry.path(), CreateKind::Folder, "New Folder");
                        }
                        if (ImGui::MenuItem("C++ Script")) {
                            createEntry(entry.path(), CreateKind::CppScript, "NewScript.cpp");
                        }
                        if (ImGui::MenuItem("Header")) {
                            createEntry(entry.path(), CreateKind::Header, "NewHeader.h");
                        }
                        if (ImGui::MenuItem("Text File")) {
                            createEntry(entry.path(), CreateKind::Text, "NewFile.txt");
                        }
                        if (ImGui::MenuItem("JSON File")) {
                            createEntry(entry.path(), CreateKind::Json, "NewData.json");
                        }
                        if (ImGui::MenuItem("Shader")) {
                            createEntry(entry.path(), CreateKind::Shader, "NewShader.glsl");
                        }
                        if (ImGui::MenuItem("Material")) {
                            fs::path target = entry.path() / "NewMaterial.mat";
                            int counter = 1;
                            while (fs::exists(target)) {
                                target = entry.path() / ("NewMaterial" + std::to_string(counter++) + ".mat");
                            }
                            SceneObject temp("Material", ObjectType::Cube, -1);
                            temp.materialPath = target.string();
                            saveMaterialToFile(temp);
                            fileBrowser.needsRefresh = true;
                        }
                        ImGui::EndMenu();
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
                    if (ImGui::MenuItem("Open in File Explorer")) {
                        openPathInFileManager(entry.path());
                    }
                    if (ImGui::MenuItem("Rename")) {
                        pendingRenamePath = entry.path();
                        std::string baseName = pendingRenamePath.filename().string();
                        std::strncpy(renameName, baseName.c_str(), sizeof(renameName) - 1);
                        renameName[sizeof(renameName) - 1] = '\0';
                        showRenamePopup = true;
                        triggerRenamePopup = true;
                        addConsoleMessage("Rename request: " + pendingRenamePath.string(), ConsoleMessageType::Info);
                    }
                    if (ImGui::MenuItem(entry.is_directory() ? "Delete Folder" : "Delete File")) {
                        pendingDeletePath = entry.path();
                        showDeletePopup = true;
                        triggerDeletePopup = true;
                        addConsoleMessage("Delete request: " + pendingDeletePath.string(), ConsoleMessageType::Info);
                    }
                    ImGui::EndPopup();
                }

                if (!entry.is_directory() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    std::string payloadPath = entry.path().string();
                    ImGui::SetDragDropPayload("FILE_PATH", payloadPath.c_str(), payloadPath.size() + 1);
                    ImGui::Text("%s", filename.c_str());
                    ImGui::EndDragDropSource();
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
            bool folderHasItems = category == FileCategory::Folder &&
                                  entry.is_directory() &&
                                  FolderHasVisibleItems(entry.path(), fileBrowser.showHiddenFiles);

            ImGui::PushID(i);

            // Selectable row
            if (ImGui::Selectable("##row", isSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 20))) {
                fileBrowser.selectedFile = entry.path();

                if (ImGui::IsMouseDoubleClicked(0)) {
                    openEntry(entry);
                }
            }

            // Context menu
            if (ImGui::BeginPopupContextItem("FileContextMenu")) {
                if (ImGui::MenuItem("Open")) {
                    openEntry(entry);
                }
                #ifdef _WIN32
                if (!entry.is_directory() && ImGui::MenuItem("Open With...")) {
                    openPathWithDialog(entry.path());
                }
                #endif
                if (entry.is_directory() && ImGui::BeginMenu("New")) {
                    if (ImGui::MenuItem("Folder")) {
                        createEntry(entry.path(), CreateKind::Folder, "New Folder");
                    }
                    if (ImGui::MenuItem("C++ Script")) {
                        createEntry(entry.path(), CreateKind::CppScript, "NewScript.cpp");
                    }
                    if (ImGui::MenuItem("Header")) {
                        createEntry(entry.path(), CreateKind::Header, "NewHeader.h");
                    }
                    if (ImGui::MenuItem("Text File")) {
                        createEntry(entry.path(), CreateKind::Text, "NewFile.txt");
                    }
                    if (ImGui::MenuItem("JSON File")) {
                        createEntry(entry.path(), CreateKind::Json, "NewData.json");
                    }
                    if (ImGui::MenuItem("Shader")) {
                        createEntry(entry.path(), CreateKind::Shader, "NewShader.glsl");
                    }
                    if (ImGui::MenuItem("Material")) {
                        fs::path target = entry.path() / "NewMaterial.mat";
                        int counter = 1;
                        while (fs::exists(target)) {
                            target = entry.path() / ("NewMaterial" + std::to_string(counter++) + ".mat");
                        }
                        SceneObject temp("Material", ObjectType::Cube, -1);
                        temp.materialPath = target.string();
                        saveMaterialToFile(temp);
                        fileBrowser.needsRefresh = true;
                    }
                    ImGui::EndMenu();
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
                if (ImGui::MenuItem("Open in File Explorer")) {
                    openPathInFileManager(entry.path());
                }
                if (ImGui::MenuItem("Rename")) {
                    pendingRenamePath = entry.path();
                    std::string baseName = pendingRenamePath.filename().string();
                    std::strncpy(renameName, baseName.c_str(), sizeof(renameName) - 1);
                    renameName[sizeof(renameName) - 1] = '\0';
                    showRenamePopup = true;
                    triggerRenamePopup = true;
                    addConsoleMessage("Rename request: " + pendingRenamePath.string(), ConsoleMessageType::Info);
                }
                if (ImGui::MenuItem(entry.is_directory() ? "Delete Folder" : "Delete File")) {
                    pendingDeletePath = entry.path();
                    showDeletePopup = true;
                    triggerDeletePopup = true;
                    addConsoleMessage("Delete request: " + pendingDeletePath.string(), ConsoleMessageType::Info);
                }
                ImGui::EndPopup();
            }

            if (!entry.is_directory() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                std::string payloadPath = entry.path().string();
                ImGui::SetDragDropPayload("FILE_PATH", payloadPath.c_str(), payloadPath.size() + 1);
                ImGui::Text("%s", filename.c_str());
                ImGui::EndDragDropSource();
            }

            // Draw icon inline
            ImGui::SameLine(4);
            ImVec2 iconPos = ImGui::GetCursorScreenPos();
            iconPos.y -= 2;
            FileIcons::DrawIcon(drawList, category, iconPos, 16, getCategoryColor(category), folderHasItems);

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

    if (ImGui::BeginPopupContextWindow("FileBrowserEmptyContext", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItem("Open in File Explorer")) {
            openPathInFileManager(fileBrowser.currentPath);
        }
        if (ImGui::MenuItem("Refresh")) {
            fileBrowser.needsRefresh = true;
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("New")) {
            if (ImGui::MenuItem("Folder")) {
                createEntry(fileBrowser.currentPath, CreateKind::Folder, "New Folder");
            }
            if (ImGui::MenuItem("C++ Script")) {
                createEntry(fileBrowser.currentPath, CreateKind::CppScript, "NewScript.cpp");
            }
            if (ImGui::MenuItem("Header")) {
                createEntry(fileBrowser.currentPath, CreateKind::Header, "NewHeader.h");
            }
            if (ImGui::MenuItem("Text File")) {
                createEntry(fileBrowser.currentPath, CreateKind::Text, "NewFile.txt");
            }
            if (ImGui::MenuItem("JSON File")) {
                createEntry(fileBrowser.currentPath, CreateKind::Json, "NewData.json");
            }
            if (ImGui::MenuItem("Shader")) {
                createEntry(fileBrowser.currentPath, CreateKind::Shader, "NewShader.glsl");
            }
            if (ImGui::MenuItem("Material")) {
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
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (settingsDirty) {
        saveEditorUserSettings();
    }

    if (triggerDeletePopup) {
        ImGui::OpenPopup("Confirm Delete");
        triggerDeletePopup = false;
    }
    if (ImGui::BeginPopupModal("Confirm Delete", &showDeletePopup, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete %s?", pendingDeletePath.filename().string().c_str());
        ImGui::TextDisabled("%s", pendingDeletePath.string().c_str());
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            std::error_code ec;
            fs::remove_all(pendingDeletePath, ec);
            if (fileBrowser.selectedFile == pendingDeletePath) {
                fileBrowser.selectedFile.clear();
            }
            fileBrowser.needsRefresh = true;
            if (ec) {
                addConsoleMessage("Delete failed: " + pendingDeletePath.string() + " (" + ec.message() + ")",
                                  ConsoleMessageType::Error);
            } else {
                addConsoleMessage("Deleted: " + pendingDeletePath.string(), ConsoleMessageType::Success);
            }
            showDeletePopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showDeletePopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (triggerRenamePopup) {
        ImGui::OpenPopup("Rename Item");
        triggerRenamePopup = false;
    }
    if (ImGui::BeginPopupModal("Rename Item", &showRenamePopup, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Rename %s", pendingRenamePath.filename().string().c_str());
        ImGui::Spacing();
        ImGui::InputText("New Name", renameName, sizeof(renameName));
        bool canRename = std::strlen(renameName) > 0;
        ImGui::BeginDisabled(!canRename);
        if (ImGui::Button("Rename", ImVec2(120, 0))) {
            fs::path newPath = pendingRenamePath.parent_path() / renameName;
            if (newPath == pendingRenamePath) {
                addConsoleMessage("Rename skipped: name unchanged", ConsoleMessageType::Info);
            } else if (fs::exists(newPath)) {
                addConsoleMessage("Rename failed: target exists " + newPath.string(), ConsoleMessageType::Error);
            } else {
                std::error_code ec;
                fs::rename(pendingRenamePath, newPath, ec);
                if (ec) {
                    addConsoleMessage("Rename failed: " + pendingRenamePath.string() + " (" + ec.message() + ")",
                                      ConsoleMessageType::Error);
                } else {
                    if (fileBrowser.selectedFile == pendingRenamePath) {
                        fileBrowser.selectedFile = newPath;
                    }
                    fileBrowser.needsRefresh = true;
                    addConsoleMessage("Renamed to: " + newPath.string(), ConsoleMessageType::Success);
                }
            }
            showRenamePopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showRenamePopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}
#pragma endregion
