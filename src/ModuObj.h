#pragma once

// ModuOBJ: a reusable serialized object hierarchy stored as a project asset.
//
// A ModuOBJ is a mini-hierarchy, not a scene: it carries objects, components,
// scripts and asset references, but never scene-global state (skybox, fog,
// environment, editor camera, project settings).
//
// This header is the *core* of the system and deliberately does not depend on
// Engine. It works on plain std::vector<SceneObject>, so it can be exercised
// headlessly (tools/moduobj-selftest) and is shared by the editor, the runtime,
// ModuCPP and any later network spawning path.
//
// File layout (reuses the existing scene syntax so component serialization is
// inherited automatically):
//
//   MODU_OBJASSET {
//       formatVersion=1;
//       assetId="mobj-0123456789abcdef";
//       name="Lamp Post";
//       roots=1,7;
//       nextLocalId=12;
//   }
//   MODU_OBJNESTED {
//       localId=7;
//       assetId="mobj-fedcba9876543210";
//       lastKnownPath="Assets/Bulb.moduobj";
//   }
//   MODU_SCENESETTINGS{ ... }        <- from the scene serializer, ids are source-local
//   MODU_GAMEOBJECT = (...) { ... }  <- one per object
//
// Object ids inside the payload are *source-local* ids, not scene object ids.

#include "Common.h"
#include "SceneObject.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ModuObj {

// Bumped when the on-disk layout changes in a way older readers cannot handle.
// A file whose formatVersion exceeds this is rejected with a clear message
// rather than being partially parsed.
constexpr int kFormatVersion = 1;

// Defensive bound for nested instantiation. Cycle detection is the real
// protection; this only stops pathological-but-acyclic depth.
constexpr int kMaxNestingDepth = 16;

constexpr const char* kAssetExtension = ".moduobj";

// ---------------------------------------------------------------------------
// Asset
// ---------------------------------------------------------------------------

// A nested ModuOBJ reference held by an object inside this asset.
struct NestedRef {
    int localId = -1;             // source-local id of the object carrying it
    std::string assetId;          // stable id of the nested asset
    std::string lastKnownPath;    // project-relative, recovery metadata only
};

struct Asset {
    int formatVersion = kFormatVersion;
    std::string assetId;                 // stable identity, survives rename/move
    std::string name;
    std::vector<int> roots;              // source-local ids of root objects
    int nextLocalId = 1;
    std::vector<NestedRef> nested;

    // Objects keyed by source-local id in obj.id. parentId/childIds are also
    // source-local. Exactly the ids referenced by `roots`.
    std::vector<SceneObject> objects;

    bool valid() const { return !assetId.empty() && !objects.empty() && !roots.empty(); }
};

// Generates a fresh stable asset id. Format: "mobj-" + 16 lowercase hex chars.
// Seeded from a random device plus a clock, so ids do not collide across
// processes or a fast create loop.
std::string GenerateAssetId();

bool IsWellFormedAssetId(const std::string& id);

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

// Writes atomically: serialize to "<path>.tmp", validate the result parses,
// fsync/close, then rename over the destination. A failure leaves any previous
// asset untouched and removes the temporary file.
bool SaveAsset(const fs::path& path, const Asset& asset, std::string& outError);

// Parses and validates. Rejects (with a specific message): unreadable files,
// a missing/!MODU_OBJASSET header, an unsupported future formatVersion,
// duplicate source-local ids, roots naming objects that do not exist, and
// parent references that do not exist.
bool LoadAsset(const fs::path& path, Asset& outAsset, std::string& outError);

// Stream forms, used by SaveAsset/LoadAsset and by the tests.
bool WriteAssetStream(std::ostream& out, const Asset& asset, std::string& outError);
bool ReadAssetStream(std::istream& in, Asset& outAsset, std::string& outError);

// ---------------------------------------------------------------------------
// Authoring
// ---------------------------------------------------------------------------

// Builds an asset from a scene selection.
//
// `selectedIds` is reduced to its selected roots first, so selecting both a
// parent and its descendant does not serialize the descendant twice. Scene
// object ids are renumbered to dense source-local ids; parentId/childIds are
// rewritten through that mapping, and a parent outside the captured set becomes
// a root (parentId = -1) rather than a dangling reference.
//
// outExternalRefs receives a human-readable note for every reference that could
// not be represented (a reference to a scene object outside the captured set).
// Those are reported rather than silently nulled.
bool BuildAssetFromSelection(const std::vector<SceneObject>& sceneObjects,
                             const std::vector<int>& selectedIds,
                             const std::string& assetName,
                             Asset& outAsset,
                             std::vector<std::string>& outExternalRefs,
                             std::string& outError);

// ---------------------------------------------------------------------------
// Instantiation
// ---------------------------------------------------------------------------

// Result of materializing an asset. Carries the full mapping so callers (editor
// undo, runtime scripts, and later network spawning) can address any object in
// the instance without relying on names.
struct InstanceResult {
    bool success = false;
    std::string error;

    std::string instanceId;                 // unique per instantiation
    std::string assetId;
    std::vector<int> rootObjectIds;         // scene ids of the instance roots
    std::unordered_map<int, int> localToScene;  // source-local id -> scene id
    std::vector<SceneObject> objects;       // fully built, not yet in the scene
};

// Materializes `asset` into freestanding SceneObjects.
//
// Staged deliberately (allocate ids -> rewrite hierarchy -> stamp instance
// metadata) and it never touches a live scene vector, so a failure cannot leave
// a partially created hierarchy behind: the caller only appends `objects` after
// checking `success`. `nextObjectId` is advanced by the number of objects
// created, matching how the scene allocates ids elsewhere.
//
// `parentId` is applied to every root (-1 for none). The instance placement
// transform is applied to roots only; child local transforms are untouched.
InstanceResult Instantiate(const Asset& asset,
                           int& nextObjectId,
                           int parentId = -1,
                           const glm::vec3* position = nullptr,
                           const glm::vec3* rotation = nullptr,
                           const glm::vec3* scale = nullptr);

// ---------------------------------------------------------------------------
// Parsed-asset cache
// ---------------------------------------------------------------------------

// Caches parsed assets so repeated spawns do not reparse the file.
//
// Stores only immutable parsed template data: never scene objects, never scene
// ids, never pointers into a scene vector. Every instance deep-copies out of the
// template, so a spawned instance can never write back into the cache.
class AssetCache {
public:
    // Returns nullptr and fills outError on a parse failure. The entry is
    // reloaded when the file's modification time changes, so an edit in the
    // editor (or an external tool) is picked up without an explicit invalidate.
    const Asset* get(const fs::path& path, std::string& outError);

    void invalidate(const fs::path& path);
    void clear();
    size_t size() const { return entries.size(); }

private:
    struct Entry {
        Asset asset;
        fs::file_time_type modified{};
    };
    std::unordered_map<std::string, Entry> entries;
};

// ---------------------------------------------------------------------------
// Scene operations
// ---------------------------------------------------------------------------

// These take the scene vector by reference rather than reaching into Engine, so
// the editor, the runtime, ModuCPP and any future network spawn path all share
// one implementation, and so they stay headlessly testable.
//
// All of them address objects by id. None of them retain a SceneObject*, which
// would dangle the moment the scene vector reallocates.

// Instantiates and appends to the scene in one transaction: on failure nothing is
// appended and nextObjectId is not advanced.
InstanceResult SpawnIntoScene(const Asset& asset,
                              std::vector<SceneObject>& sceneObjects,
                              int& nextObjectId,
                              int parentId = -1,
                              const glm::vec3* position = nullptr,
                              const glm::vec3* rotation = nullptr,
                              const glm::vec3* scale = nullptr,
                              bool spawnDisabled = false);

// Removes every object belonging to `instanceId`, including any nested instance
// objects underneath it. Returns the number of objects removed. Also detaches the
// removed ids from any surviving parent's childIds so no dangling child ids remain.
int DestroyInstance(const std::string& instanceId, std::vector<SceneObject>& sceneObjects);

// Scene object ids of the instance's roots, in asset root order.
std::vector<int> GetInstanceRoots(const std::string& instanceId,
                                  const std::vector<SceneObject>& sceneObjects);

// Scene object id for a source-local id inside an instance, or -1.
// This is the name-independent lookup callers should prefer.
int FindInstanceObject(const std::string& instanceId,
                       int sourceLocalId,
                       const std::vector<SceneObject>& sceneObjects);

// Convenience only: first object in the instance whose name matches.
int FindInstanceObjectByName(const std::string& instanceId,
                             const std::string& name,
                             const std::vector<SceneObject>& sceneObjects);

// Source asset id backing an instance, or empty if the instance is not present.
std::string GetInstanceAssetId(const std::string& instanceId,
                               const std::vector<SceneObject>& sceneObjects);

// True when every object of the instance is present and its hierarchy resolves.
bool ValidateInstance(const std::string& instanceId,
                      const std::vector<SceneObject>& sceneObjects,
                      std::string& outError);

// Every distinct instance id present in the scene, in first-appearance order.
std::vector<std::string> CollectInstanceIds(const std::vector<SceneObject>& sceneObjects);

// ---------------------------------------------------------------------------
// Nesting / cycles
// ---------------------------------------------------------------------------

// Resolves nested asset ids to files. Implemented by the caller so the core
// stays free of any project/asset-database dependency.
using AssetResolver = std::function<bool(const std::string& assetId, fs::path& outPath)>;

// Walks the nested graph from `rootAssetId` and reports the first cycle found.
// Detection is by asset id, never by filename, so renaming a file cannot hide a
// cycle. Returns true when a cycle exists and fills outChain with the dependency
// chain (e.g. "A -> B -> A") for a useful error message.
bool DetectCycle(const std::string& rootAssetId,
                 const AssetResolver& resolver,
                 std::string& outChain);

}  // namespace ModuObj
