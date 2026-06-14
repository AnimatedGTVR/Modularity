#pragma once

// Modu2DStats: lightweight per-frame counters for the immediate-mode 2D /
// editor-viewport rendering path.
//
// The normal GL scene renderer feeds the profiler via Renderer::RenderStats,
// but 2D content is emitted through ImGui draw lists (see BatchedSpriteEmitter
// and the player/world-UI viewport paths), so it is invisible to those stats.
// These counters close that gap. They are deliberately minimal: plain global
// accumulators incremented on the main render thread, reset at the start of
// each frame and snapshotted at the end for the Profiler.
//
// Phase A (instrumentation only): no behavior or visual output changes. Some
// counters are placeholders that stay at zero until later optimization phases
// (redraw-skipping, cached-room rendering) actually populate them.

#include <cstdint>

namespace Modu2DStats {

struct Snapshot {
    uint64_t spriteQuads = 0;       // 2D sprite/UI quads emitted into ImGui draw lists
    uint64_t spriteBatches = 0;     // BatchedSpriteEmitter flushes that drew >=1 quad
    uint64_t postFxPasses = 0;      // fullscreen post-processing passes executed
    uint64_t viewportRedraws = 0;   // scene-bearing viewport panels that drew this frame
    uint64_t skippedRedraws = 0;    // placeholder: viewports that reused a cached result
    uint64_t cachedLayerReuses = 0; // placeholder: cached static-layer reuses (cached-room)
    uint64_t uiDirScans = 0;        // editor folder-tree fs::directory_iterator scans this frame
    uint64_t uiWindowsSkipped = 0;  // editor panels that early-outed on a hidden/collapsed Begin()
};

// Increment helpers. Main-render-thread only; not synchronized.
void CountSpriteQuads(uint64_t count = 1);
void CountSpriteBatch();
void CountPostFxPass();
void CountViewportRedraw();
void CountSkippedRedraw();      // placeholder until redraw-skipping exists
void CountCachedLayerReuse();   // placeholder until cached-room rendering exists
void CountDirScan(uint64_t count = 1); // editor folder-tree filesystem scans
void CountWindowSkipped();             // editor panel skipped via Begin()==false early-out

void Reset();      // call once at the start of each frame
Snapshot Read();   // call once near the end of each frame

} // namespace Modu2DStats
