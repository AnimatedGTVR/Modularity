// Assemblage viewport interaction: the grid overlay, the cell cursor, and the
// mouse/keyboard handling for the painting tools.
//
// Split out of ViewportWindows.cpp deliberately. Engine::renderViewport is
// already ~9,500 lines; adding a tool mode inline would make it worse, and this
// code needs none of that function's local state beyond the camera mapping,
// which arrives as parameters.
//
// Editor-only.

#include "Engine.h"
#include "EditorWindows/Panels/ViewportRenderHelpers.h"

#include <algorithm>

namespace {

ImU32 WithAlpha(const glm::vec4& color, float alpha) {
    return ImGui::GetColorU32(ImVec4(color.r, color.g, color.b, color.a * alpha));
}

}  // namespace

// Draws the grid for the active Assemblage and runs the painting tools.
// `worldToScreen` / `screenToWorld` come from the caller's 2D camera, so this
// works identically in whichever viewport hosts it.
void Engine::renderAssemblageViewportOverlay(const ImVec2& overlayPos,
                                             const ImVec2& overlaySize,
                                             bool viewportHovered) {
    if (world2DEditMode != World2DEditMode::Assemblage) return;

    SceneObject* root = getActiveAssemblageRoot();
    if (root == nullptr) return;

    const std::string assetPath = root->assemblage.assetPath;
    AssemblageRuntime::Entry* entry = assemblageRuntime.acquire(assetPath);
    if (entry == nullptr) return;

    const Assemblage::Asset& asset = entry->asset;
    if (asset.cellSize.x <= 0.0f || asset.cellSize.y <= 0.0f) return;

    // The active layer object places the grid; fall back to the root when the
    // layer object is missing so the grid still lands somewhere sensible.
    glm::vec2 layerOffset(root->position.x, root->position.y);
    for (const SceneObject& obj : sceneObjects) {
        if (obj.hasAssemblageLayer && obj.parentId == root->id &&
            obj.assemblageLayer.layerId == assemblageActiveLayerId) {
            layerOffset = glm::vec2(obj.position.x, obj.position.y);
            break;
        }
    }

    auto worldToScreen = [&](const glm::vec2& world) {
        const glm::vec2 screen = uiWorldCamera.WorldToScreen(world);
        return ImVec2(overlayPos.x + screen.x, overlayPos.y + screen.y);
    };
    auto screenToWorld = [&](const ImVec2& screen) {
        return uiWorldCamera.ScreenToWorld(
            glm::vec2(screen.x - overlayPos.x, screen.y - overlayPos.y));
    };
    auto worldToCell = [&](const glm::vec2& world) {
        const glm::vec2 local = world - layerOffset;
        int cx = 0, cy = 0;
        Assemblage::WorldToCell(asset, local, cx, cy);
        return glm::ivec2(cx, cy);
    };
    auto cellToWorldMin = [&](const glm::ivec2& cell) {
        return Assemblage::CellToWorld(asset, cell.x, cell.y) + layerOffset;
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const glm::vec2 viewMin = screenToWorld(ImVec2(overlayPos.x, overlayPos.y + overlaySize.y));
    const glm::vec2 viewMax = screenToWorld(ImVec2(overlayPos.x + overlaySize.x, overlayPos.y));

    // -- Grid -----------------------------------------------------------------
    if (assemblageShowGridOverlay && root->assemblage.showGrid) {
        const float cellPixels = asset.cellSize.x * uiWorldCamera.zoom;
        // Below a few pixels per cell the grid is noise, not information.
        if (cellPixels >= 4.0f) {
            const glm::ivec2 first = worldToCell(viewMin);
            const glm::ivec2 last = worldToCell(viewMax);
            const int minX = std::min(first.x, last.x) - 1;
            const int maxX = std::max(first.x, last.x) + 1;
            const int minY = std::min(first.y, last.y) - 1;
            const int maxY = std::max(first.y, last.y) + 1;

            // Cap the line count so a zoomed-out view cannot stall the frame.
            if (static_cast<long long>(maxX - minX) + (maxY - minY) < 4000ll) {
                const ImU32 lineColor = WithAlpha(root->assemblage.gridColor, 1.0f);
                const ImU32 chunkColor = WithAlpha(root->assemblage.gridColor, 2.4f);
                const int chunkSize = std::max(Assemblage::kMinChunkSize, asset.chunkSize);

                for (int x = minX; x <= maxX; ++x) {
                    const ImVec2 a = worldToScreen(cellToWorldMin(glm::ivec2(x, minY)));
                    const ImVec2 b = worldToScreen(cellToWorldMin(glm::ivec2(x, maxY)));
                    const bool chunkEdge = (((x % chunkSize) + chunkSize) % chunkSize) == 0;
                    drawList->AddLine(a, b, chunkEdge ? chunkColor : lineColor,
                                      chunkEdge ? 1.6f : 1.0f);
                }
                for (int y = minY; y <= maxY; ++y) {
                    const ImVec2 a = worldToScreen(cellToWorldMin(glm::ivec2(minX, y)));
                    const ImVec2 b = worldToScreen(cellToWorldMin(glm::ivec2(maxX, y)));
                    const bool chunkEdge = (((y % chunkSize) + chunkSize) % chunkSize) == 0;
                    drawList->AddLine(a, b, chunkEdge ? chunkColor : lineColor,
                                      chunkEdge ? 1.6f : 1.0f);
                }
            }
        }
    }

    // -- Selection ------------------------------------------------------------
    auto drawCellRect = [&](const glm::ivec2& a, const glm::ivec2& b, ImU32 color,
                            float thickness) {
        const glm::ivec2 lo(std::min(a.x, b.x), std::min(a.y, b.y));
        const glm::ivec2 hi(std::max(a.x, b.x) + 1, std::max(a.y, b.y) + 1);
        const ImVec2 p0 = worldToScreen(cellToWorldMin(lo));
        const ImVec2 p1 = worldToScreen(cellToWorldMin(hi));
        drawList->AddRect(ImVec2(std::min(p0.x, p1.x), std::min(p0.y, p1.y)),
                          ImVec2(std::max(p0.x, p1.x), std::max(p0.y, p1.y)), color, 0.0f, 0,
                          thickness);
    };

    if (assemblageHasSelection) {
        drawCellRect(assemblageSelectionMin, assemblageSelectionMax,
                     IM_COL32(120, 200, 255, 220), 2.0f);
    }

    if (!viewportHovered) return;

    // -- Input ----------------------------------------------------------------
    const ImVec2 mouse = ImGui::GetMousePos();
    const glm::ivec2 hoverCell = worldToCell(screenToWorld(mouse));

    const Assemblage::Layer* activeLayer = asset.findLayer(assemblageActiveLayerId);
    const bool layerLocked = activeLayer == nullptr || activeLayer->locked;

    // Cursor cell, so it is always obvious which cell an action will hit.
    drawCellRect(hoverCell, hoverCell,
                 layerLocked ? IM_COL32(255, 120, 110, 200) : IM_COL32(255, 235, 130, 230), 2.0f);

    if (assemblageDragActive) {
        // Preview reads hoverCell directly. It must NOT write assemblageDragCurrent:
        // the freehand tools below use that as the previous frame's cell to
        // interpolate from, and clobbering it here would collapse every drag step
        // to zero length and put the fast-drag holes back.
        if (assemblageTool == AssemblageTool::Rectangle ||
            assemblageTool == AssemblageTool::Select) {
            drawCellRect(assemblageDragStart, hoverCell, IM_COL32(160, 220, 255, 200), 1.6f);
        } else if (assemblageTool == AssemblageTool::Line) {
            const ImVec2 a = worldToScreen(cellToWorldMin(assemblageDragStart) +
                                           asset.cellSize * 0.5f);
            const ImVec2 b =
                worldToScreen(cellToWorldMin(hoverCell) + asset.cellSize * 0.5f);
            drawList->AddLine(a, b, IM_COL32(160, 220, 255, 200), 2.0f);
        }
    }

    const bool ctrl = ImGui::GetIO().KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) assemblageCopySelection();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) assemblagePasteAt(hoverCell);

    if (layerLocked) return;

    const uint32_t brush = resolveAssemblageBrushCell();

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        switch (assemblageTool) {
            case AssemblageTool::Paint:
            case AssemblageTool::Erase:
                // Freehand tools open a stroke and keep appending until release,
                // so the whole drag is one undo entry.
                beginAssemblageStroke(assemblageTool == AssemblageTool::Erase ? "Erase" : "Paint");
                applyAssemblageCell(hoverCell.x, hoverCell.y, brush);
                assemblageDragActive = true;
                assemblageDragStart = hoverCell;
                // Seed the interpolation cursor too, or the first drag frame
                // interpolates from wherever the *previous* stroke ended and
                // paints a line across the map.
                assemblageDragCurrent = hoverCell;
                break;
            case AssemblageTool::Rectangle:
            case AssemblageTool::Line:
            case AssemblageTool::Select:
                assemblageDragActive = true;
                assemblageDragStart = hoverCell;
                assemblageDragCurrent = hoverCell;
                break;
            case AssemblageTool::Fill:
                assemblageFloodFill(hoverCell.x, hoverCell.y, brush);
                break;
            case AssemblageTool::Picker: {
                const uint32_t cell =
                    Assemblage::GetCell(asset, assemblageActiveLayerId, hoverCell.x, hoverCell.y);
                const uint32_t picked = Assemblage::CellTile(cell);
                if (picked != Assemblage::kEmptyTile) {
                    assemblageActiveTile = picked;
                    assemblageTool = AssemblageTool::Paint;
                }
                break;
            }
        }
    } else if (assemblageDragActive && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (assemblageTool == AssemblageTool::Paint || assemblageTool == AssemblageTool::Erase) {
            // Interpolate across the frame gap: a fast drag otherwise leaves
            // holes wherever the cursor jumped more than a cell between frames.
            const glm::ivec2 previous = assemblageDragCurrent;
            assemblageDragCurrent = hoverCell;
            int x0 = previous.x, y0 = previous.y;
            const int x1 = hoverCell.x, y1 = hoverCell.y;
            const int dx = std::abs(x1 - x0);
            const int dy = -std::abs(y1 - y0);
            const int sx = x0 < x1 ? 1 : -1;
            const int sy = y0 < y1 ? 1 : -1;
            int err = dx + dy;
            for (int guard = 0; guard < 4096; ++guard) {
                applyAssemblageCell(x0, y0, brush);
                if (x0 == x1 && y0 == y1) break;
                const int e2 = 2 * err;
                if (e2 >= dy) {
                    err += dy;
                    x0 += sx;
                }
                if (e2 <= dx) {
                    err += dx;
                    y0 += sy;
                }
            }
        }
    } else if (assemblageDragActive && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        assemblageDragActive = false;
        switch (assemblageTool) {
            case AssemblageTool::Paint:
            case AssemblageTool::Erase:
                endAssemblageStroke();
                break;
            case AssemblageTool::Rectangle:
                assemblageFillRect(assemblageDragStart, hoverCell, brush);
                break;
            case AssemblageTool::Line:
                assemblageDrawLine(assemblageDragStart, hoverCell, brush);
                break;
            case AssemblageTool::Select:
                assemblageHasSelection = true;
                assemblageSelectionMin =
                    glm::ivec2(std::min(assemblageDragStart.x, hoverCell.x),
                               std::min(assemblageDragStart.y, hoverCell.y));
                assemblageSelectionMax =
                    glm::ivec2(std::max(assemblageDragStart.x, hoverCell.x),
                               std::max(assemblageDragStart.y, hoverCell.y));
                break;
            default:
                break;
        }
    }
}
