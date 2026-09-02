#pragma once

// Assemblage: structured world authoring stored as a project asset.
//
// An Assemblage is a grid of cells split into fixed-size chunks, organised into
// layers. Cells hold a compact id into a shared tile definition table, never a
// copy of the sprite data, so a 10,000 cell map costs 40 KB of ids plus one
// tileset. Chunking exists so a single edit rebuilds one chunk instead of the
// whole map.
//
// Deliberately NOT a "tilemap": the on-disk format carries a `kind` discriminator
// at both the assemblage and the layer level. v1 defines kind "grid2d" with layer
// kind "tile"; a later structured-authoring workflow (3D grids, hex, isometric,
// region/height fields) claims a new kind and bumps the format version rather
// than forking a second format. Readers refuse an unknown kind or a newer
// formatVersion outright instead of parsing it partially - the same rule
// ModuObj::LoadAsset applies.
//
// This header is the *core* of the system and deliberately does not depend on
// Engine, the renderer, or the scene serializer. It works on plain data, so it
// can be exercised headlessly (tools/assemblage-selftest) and is shared by the
// editor, the runtime, and any later tooling.
//
// File layout (.moduasm):
//
//   MODU_ASSEMBLAGE {
//       formatVersion=1;
//       assemblageId="asm-0123456789abcdef";
//       name="Cabin Floor";
//       kind="grid2d";
//       chunkSize=32;
//       cellSize=1,1;
//       origin=0,0;
//       nextLayerId=3;
//       tileset="Assets/Tiles/Cabin.modutile";
//   }
//   MODU_ASMLAYER = (id=1, name="Ground") { kind="tile"; order=0; ... }
//   MODU_ASMCHUNK = (layer=1, coord=0,0) { encoding="rle32"; cells=1024*0; }
//
// `coord` is a variable-arity list so a future 3D kind writes three components
// without breaking the block grammar. `encoding` names the cell codec so a
// binary/base64 codec can be added later without a new block type.

#include "ScriptSdkCommon.h"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace Assemblage {

// Bumped when the on-disk layout changes in a way older readers cannot handle.
// A file whose formatVersion exceeds this is rejected with a clear message
// rather than being partially parsed.
constexpr int kFormatVersion = 1;

constexpr const char* kAssetExtension = ".moduasm";
constexpr const char* kTilesetExtension = ".modutile";

// The only kinds v1 understands. Anything else is refused at load.
constexpr const char* kKindGrid2D = "grid2d";
constexpr const char* kLayerKindTile = "tile";

// Chunk edge length in cells. 32x32 = 1024 cells = 4 KB of ids per chunk, which
// keeps a rebuild cheap while staying large enough that a screenful of map is a
// handful of chunks rather than hundreds.
constexpr int kDefaultChunkSize = 32;
constexpr int kMinChunkSize = 4;
constexpr int kMaxChunkSize = 256;

// ---------------------------------------------------------------------------
// Cells
// ---------------------------------------------------------------------------

// A cell is one packed uint32:
//   bits  0-23  tile id (0 = empty, so a zeroed chunk is an empty chunk)
//   bits 24-26  transform flags
//   bits 27-31  reserved, must be written as 0
//
// Packed rather than a struct because chunk cells are the one array that scales
// with map size; four bytes per cell keeps a large map's serialized form small
// and its in-memory form cache friendly.
using Cell = uint32_t;
using TileId = uint32_t;

constexpr TileId kEmptyTile = 0;
constexpr uint32_t kTileIdMask = 0x00FFFFFFu;
constexpr uint32_t kCellFlagShift = 24;

enum CellFlags : uint32_t {
    CellFlag_None   = 0,
    CellFlag_FlipX  = 1u << 0,
    CellFlag_FlipY  = 1u << 1,
    CellFlag_Rot90  = 1u << 2
};

inline Cell MakeCell(TileId tile, uint32_t flags = CellFlag_None) {
    return (tile & kTileIdMask) | ((flags & 0x7u) << kCellFlagShift);
}
inline TileId CellTile(Cell cell) { return cell & kTileIdMask; }
inline uint32_t CellFlagsOf(Cell cell) { return (cell >> kCellFlagShift) & 0x7u; }
inline bool CellIsEmpty(Cell cell) { return CellTile(cell) == kEmptyTile; }

// ---------------------------------------------------------------------------
// Tile definitions
// ---------------------------------------------------------------------------

enum class TileCollision {
    None = 0,
    Box = 1,
    Circle = 2,
    Polygon = 3,
    Line = 4
};

enum class TileAnimationMode {
    Loop = 0,
    PingPong = 1,
    Once = 2
};

// A source region on a sheet. Pixel rect, matching SpritesheetDocument::rects.
struct TileFrame {
    glm::ivec4 rect = glm::ivec4(0);   // x, y, w, h in source pixels
};

// Animation lives on the tile *definition*, never on the cell. Every cell that
// references tile N therefore resolves the same frame at the same time from one
// shared definition, with no per-cell timer to update and no per-cell state to
// store. ResolveAnimationFrame() is a pure function of the definition and the
// clock, so a map with 50,000 animated cells costs exactly as much to animate as
// a map with one.
struct TileAnimation {
    float fps = 0.0f;                                  // <= 0 means "not animated"
    TileAnimationMode mode = TileAnimationMode::Loop;
    std::vector<TileFrame> frames;

    bool animated() const { return fps > 0.0f && frames.size() > 1; }
};

struct TileDef {
    TileId id = kEmptyTile;
    std::string name;
    std::string sheetPath;             // project-relative image
    TileFrame frame;                   // still frame / animation frame 0
    TileAnimation animation;

    std::vector<std::string> tags;

    TileCollision collision = TileCollision::None;
    glm::vec2 collisionSize = glm::vec2(1.0f);
    glm::vec2 collisionOffset = glm::vec2(0.0f);
    float collisionRadius = 0.5f;
    std::vector<glm::vec2> collisionPoints;

    // Default rendering hints applied when the tile is painted; a layer may
    // override them.
    int sortingOffset = 0;
    glm::vec4 tint = glm::vec4(1.0f);

    // Auto-tiling rule set this tile participates in. Empty = no auto-tiling.
    // The rule tables themselves arrive in the auto-tiling stage; the field is
    // reserved now so adding them does not change the format.
    std::string autoRuleSet;
    // Weighted random variants selected at paint time. Empty = no variants.
    std::vector<TileId> variants;

    // Free-form user metadata, preserved verbatim on round-trip.
    std::map<std::string, std::string> meta;
};

struct Tileset {
    int formatVersion = kFormatVersion;
    std::string tilesetId;
    std::string name;
    TileId nextTileId = 1;
    std::vector<TileDef> tiles;

    const TileDef* find(TileId id) const;
    TileDef* find(TileId id);
    bool valid() const { return !tilesetId.empty(); }
};

// Frame index for `tile` at `timeSeconds`. Returns 0 for a still tile. Pure:
// no state is read or written, which is what lets every cell share one timeline.
int ResolveAnimationFrame(const TileDef& tile, double timeSeconds);

// ---------------------------------------------------------------------------
// Chunks and layers
// ---------------------------------------------------------------------------

// Chunk coordinate. Stored as a variable-arity list on disk; v1 uses x/y only.
struct ChunkCoord {
    int x = 0;
    int y = 0;

    bool operator==(const ChunkCoord& other) const { return x == other.x && y == other.y; }
};

struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        return static_cast<size_t>((static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32) ^
                                   static_cast<uint32_t>(c.y));
    }
};

struct Chunk {
    ChunkCoord coord;
    std::vector<Cell> cells;   // chunkSize * chunkSize, row major

    bool empty() const;
};

using ChunkMap = std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash>;

struct Layer {
    int id = -1;
    std::string name;
    std::string kind = kLayerKindTile;

    int order = 0;             // authoring order within the assemblage
    int sortingOrder = 0;      // draw order, folded into the existing 2D sort
    float opacity = 1.0f;
    glm::vec4 tint = glm::vec4(1.0f);
    bool visible = true;
    bool locked = false;
    bool collisionEnabled = true;

    ChunkMap chunks;
};

// ---------------------------------------------------------------------------
// Asset
// ---------------------------------------------------------------------------

struct Asset {
    int formatVersion = kFormatVersion;
    std::string assemblageId;
    std::string name;
    std::string kind = kKindGrid2D;

    int chunkSize = kDefaultChunkSize;
    glm::vec2 cellSize = glm::vec2(1.0f);   // world units per cell
    glm::vec2 origin = glm::vec2(0.0f);     // world offset of cell (0,0)

    int nextLayerId = 1;
    std::string tilesetPath;                // project-relative .modutile

    std::vector<Layer> layers;

    bool valid() const { return !assemblageId.empty() && kind == kKindGrid2D; }

    const Layer* findLayer(int layerId) const;
    Layer* findLayer(int layerId);

    // Adds a layer with a fresh id and returns it. Never invalidates existing
    // layer ids.
    Layer& addLayer(const std::string& layerName);
};

// Generates a fresh stable id. Format: "<prefix>-" + 16 lowercase hex chars,
// matching ModuObj::GenerateAssetId's shape.
std::string GenerateAssemblageId();
std::string GenerateTilesetId();
bool IsWellFormedId(const std::string& id, const char* prefix);

// ---------------------------------------------------------------------------
// Cell access
// ---------------------------------------------------------------------------

// Cell -> chunk coordinate and index within that chunk. Uses floor division, so
// negative cell coordinates work (a map may grow in any direction from origin).
ChunkCoord CellToChunk(int cellX, int cellY, int chunkSize);
int CellIndexInChunk(int cellX, int cellY, int chunkSize);

// Reads a cell. Missing chunks read as empty rather than allocating, so probing
// a sparse map is free.
Cell GetCell(const Asset& asset, int layerId, int cellX, int cellY);

// Writes a cell, allocating the chunk on first non-empty write. Returns true
// when the value actually changed, and fills outChunk with the chunk that needs
// rebuilding. A write that clears the last non-empty cell of a chunk drops the
// chunk, so erasing a region reclaims its memory.
bool SetCell(Asset& asset, int layerId, int cellX, int cellY, Cell value,
             ChunkCoord* outChunk = nullptr);

// World <-> cell mapping. `origin` and `cellSize` come from the asset; the
// owning SceneOBJ's transform is applied by the caller, so this stays
// Engine-free.
glm::vec2 CellToWorld(const Asset& asset, int cellX, int cellY);
void WorldToCell(const Asset& asset, const glm::vec2& world, int& outCellX, int& outCellY);

// Inclusive cell bounds of every non-empty chunk in a layer, or false when the
// layer has no chunks.
bool LayerCellBounds(const Asset& asset, int layerId,
                     int& outMinX, int& outMinY, int& outMaxX, int& outMaxY);

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

// Writes atomically: serialize, validate the result parses back, write to
// "<path>.tmp", then rename over the destination. A failure leaves any previous
// asset untouched and removes the temporary file.
bool SaveAsset(const fs::path& path, const Asset& asset, std::string& outError);
bool LoadAsset(const fs::path& path, Asset& outAsset, std::string& outError);

bool WriteAssetStream(std::ostream& out, const Asset& asset, std::string& outError);
bool ReadAssetStream(std::istream& in, Asset& outAsset, std::string& outError);

bool SaveTileset(const fs::path& path, const Tileset& tileset, std::string& outError);
bool LoadTileset(const fs::path& path, Tileset& outTileset, std::string& outError);

bool WriteTilesetStream(std::ostream& out, const Tileset& tileset, std::string& outError);
bool ReadTilesetStream(std::istream& in, Tileset& outTileset, std::string& outError);

// ---------------------------------------------------------------------------
// Parsed-asset caches
// ---------------------------------------------------------------------------

// Caches parsed assets so repeated loads do not reparse the file. Mirrors
// ModuObj::AssetCache: stores only immutable parsed data, reloads when the
// file's modification time changes.
template <typename T>
class ParsedCache {
public:
    using Loader = bool (*)(const fs::path&, T&, std::string&);

    explicit ParsedCache(Loader loader) : loader(loader) {}

    const T* get(const fs::path& path, std::string& outError) {
        outError.clear();
        const std::string key = path.string();
        std::error_code ec;
        const fs::file_time_type modified = fs::last_write_time(path, ec);
        if (ec) {
            outError = "Cannot stat " + key;
            return nullptr;
        }

        auto it = entries.find(key);
        if (it != entries.end() && it->second.modified == modified) {
            return &it->second.value;
        }

        Entry entry;
        if (!loader(path, entry.value, outError)) {
            return nullptr;
        }
        entry.modified = modified;
        Entry& slot = entries[key];
        slot = std::move(entry);
        return &slot.value;
    }

    void invalidate(const fs::path& path) { entries.erase(path.string()); }
    void clear() { entries.clear(); }
    size_t size() const { return entries.size(); }

private:
    struct Entry {
        T value;
        fs::file_time_type modified{};
    };
    Loader loader = nullptr;
    std::unordered_map<std::string, Entry> entries;
};

using AssetCache = ParsedCache<Asset>;
using TilesetCache = ParsedCache<Tileset>;

AssetCache MakeAssetCache();
TilesetCache MakeTilesetCache();

}  // namespace Assemblage
