#pragma once

// AssemblageRuntime: the engine-side owner of loaded Assemblage assets.
//
// src/Assemblage.{h,cpp} is the pure data core - formats, cells, chunks, no
// engine dependency. This is the layer above it that the editor and the runtime
// share: it loads assets on demand, keeps one authoritative mutable copy per
// asset path, resolves tile sprites through the Renderer's texture cache, and
// caches per-chunk geometry so drawing a map does not re-walk its cells every
// frame.
//
// One store, not two. The editor mutates the same Asset the renderer reads, and
// marks the affected chunks dirty; there is no separate "edit copy" to keep in
// sync. Saving writes that copy back through Assemblage::SaveAsset.
//
// Cached geometry is deliberately **world space**, not screen space: the camera
// moves every frame but the cells do not, so a chunk is rebuilt only when its
// cells change. Screen projection happens at emit time and costs two multiplies
// per corner.

#include "Assemblage.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Renderer;

class AssemblageRuntime {
public:
    // One tile's geometry, in assemblage-local world units (the owning layer
    // object's world position is added at emit time).
    struct TileQuad {
        unsigned int textureId = 0;
        glm::vec2 worldMin = glm::vec2(0.0f);
        glm::vec2 worldMax = glm::vec2(0.0f);
        glm::vec2 uvMin = glm::vec2(0.0f);
        glm::vec2 uvMax = glm::vec2(1.0f);
        glm::vec4 tint = glm::vec4(1.0f);
        // Animated tiles keep their id and their sheet's dimensions so the UV can
        // be re-resolved per frame from the shared timeline without touching the
        // texture cache again. Still tiles bake their UV once and never re-resolve.
        Assemblage::TileId animatedTile = Assemblage::kEmptyTile;
        int sheetWidth = 0;
        int sheetHeight = 0;
        uint32_t cellFlags = 0;
    };

    struct ChunkGeometry {
        std::vector<TileQuad> quads;
        glm::vec2 boundsMin = glm::vec2(0.0f);
        glm::vec2 boundsMax = glm::vec2(0.0f);
        bool hasAnimatedTiles = false;
        bool valid = false;
    };

    struct Entry {
        Assemblage::Asset asset;
        Assemblage::Tileset tileset;
        std::string tilesetSourcePath;   // what tileset was actually loaded
        bool loaded = false;
        bool hasUnsavedChanges = false;
        std::string loadError;
    };

    // Root the runtime at a project. Changing roots drops everything, since
    // asset paths are project relative.
    void setProjectRoot(const fs::path& root);
    const fs::path& projectRoot() const { return root; }

    // Resolves a project-relative (or absolute) asset path to an absolute one.
    fs::path resolvePath(const std::string& assetPath) const;
    // Inverse of resolvePath, for storing back into a scene.
    std::string makeProjectRelative(const fs::path& absolutePath) const;

    // Loads on first use. Returns nullptr and records the reason when the file
    // is missing or malformed; callers should surface entry->loadError.
    Entry* acquire(const std::string& assetPath);
    const Entry* find(const std::string& assetPath) const;

    Assemblage::Asset* asset(const std::string& assetPath);
    const Assemblage::Tileset* tileset(const std::string& assetPath);

    // Registers an asset the caller just created in memory, so a brand new
    // Assemblage is editable before it has ever been written to disk.
    Entry& adopt(const std::string& assetPath, Assemblage::Asset asset);

    // Dirty marking. markCellDirty is the one painting calls: it invalidates the
    // chunk holding the cell and, when the cell sits on a chunk edge, the
    // neighbouring chunks whose auto-tiling could depend on it.
    void markChunkDirty(const std::string& assetPath, int layerId, Assemblage::ChunkCoord coord);
    void markCellDirty(const std::string& assetPath, int layerId, int cellX, int cellY,
                       bool includeNeighbours = true);
    void markLayerDirty(const std::string& assetPath, int layerId);
    void markAssetDirty(const std::string& assetPath);

    void markUnsaved(const std::string& assetPath, bool unsaved = true);
    bool hasUnsavedChanges(const std::string& assetPath) const;
    bool anyUnsavedChanges() const;

    // Builds the chunk's geometry if it is missing or stale, then returns it.
    // Returns nullptr when the chunk holds nothing drawable.
    const ChunkGeometry* chunkGeometry(const std::string& assetPath, int layerId,
                                       Assemblage::ChunkCoord coord, Renderer& renderer);

    bool save(const std::string& assetPath, std::string& outError);
    bool saveAllDirty(std::string& outError);

    // Drops cached geometry but keeps loaded assets, e.g. after a texture
    // reload changes GL ids under us.
    void invalidateAllGeometry();
    // Drops everything, e.g. on project close.
    void clear();

    // Debug/telemetry for the 2D stats overlay.
    size_t loadedAssetCount() const { return entries.size(); }
    size_t cachedChunkCount() const { return geometry.size(); }

private:
    struct ChunkKey {
        std::string assetPath;
        int layerId = -1;
        int chunkX = 0;
        int chunkY = 0;

        bool operator==(const ChunkKey& other) const {
            return layerId == other.layerId && chunkX == other.chunkX &&
                   chunkY == other.chunkY && assetPath == other.assetPath;
        }
    };
    struct ChunkKeyHash {
        size_t operator()(const ChunkKey& key) const {
            size_t h = std::hash<std::string>()(key.assetPath);
            h ^= static_cast<size_t>(key.layerId) * 0x9E3779B9u + (h << 6) + (h >> 2);
            h ^= static_cast<size_t>(key.chunkX) * 0x85EBCA6Bu + (h << 6) + (h >> 2);
            h ^= static_cast<size_t>(key.chunkY) * 0xC2B2AE35u + (h << 6) + (h >> 2);
            return h;
        }
    };

    void ensureTileset(Entry& entry);
    void buildChunkGeometry(const Entry& entry, const Assemblage::Layer& layer,
                            Assemblage::ChunkCoord coord, Renderer& renderer,
                            ChunkGeometry& out);

    fs::path root;
    std::unordered_map<std::string, Entry> entries;
    std::unordered_map<ChunkKey, ChunkGeometry, ChunkKeyHash> geometry;
};

// Resolves the UV rect of a tile's current frame. Shared by the geometry builder
// and the per-frame animated re-resolve so both agree exactly.
bool ResolveTileUv(const Assemblage::TileDef& tile, int textureWidth, int textureHeight,
                   double timeSeconds, glm::vec2& outUvMin, glm::vec2& outUvMax);
