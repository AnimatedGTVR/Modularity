#pragma once

#include "SceneObject.h"
#include "ModelLoader.h"
#include <vector>

// Shared helpers for the Map Maker (sectors, transitions, portals).
// These operate on the plain scene object list so both the editor windows
// and runtime systems can resolve map references without holding pointers
// across scene reloads. All cross references use the stable string ids
// stored in the components; helpers re-resolve them on demand.
namespace MapMaker {

// Generates a stable unique id like "sec_1a2b3c4d5e6f7081" from a prefix
// ("map", "sec", "trn", "prt"). Uses a random 64-bit payload; collisions are
// practically impossible and duplicates are still caught by validation.
std::string GenerateId(const char* prefix);

const char* TransitionKindLabel(MapTransitionKind kind);
constexpr int kMapTransitionKindCount = 7;

// -- Scene queries (never cache the returned pointers across frames) --------
SceneObject* FindMapRoot(std::vector<SceneObject>& objects);
const SceneObject* FindMapRoot(const std::vector<SceneObject>& objects);
SceneObject* FindSectorById(std::vector<SceneObject>& objects, const std::string& sectorId);
const SceneObject* FindSectorById(const std::vector<SceneObject>& objects, const std::string& sectorId);
SceneObject* FindTransitionById(std::vector<SceneObject>& objects, const std::string& transitionId);
SceneObject* FindPortalById(std::vector<SceneObject>& objects, const std::string& portalId);
const SceneObject* FindPortalById(const std::vector<SceneObject>& objects, const std::string& portalId);
std::vector<SceneObject*> GatherSectors(std::vector<SceneObject>& objects);
std::vector<const SceneObject*> GatherSectors(const std::vector<SceneObject>& objects);
std::vector<SceneObject*> GatherTransitions(std::vector<SceneObject>& objects);
std::vector<const SceneObject*> GatherTransitions(const std::vector<SceneObject>& objects);

// Nearest ancestor (including the object itself) carrying a sector component.
const SceneObject* FindOwningSector(const std::vector<SceneObject>& objects, const SceneObject& obj);

// True when the transition references the sector on either end.
inline bool TransitionTouchesSector(const MapTransitionComponent& t, const std::string& sectorId) {
    return !sectorId.empty() &&
           (t.sourceSectorId == sectorId || t.destinationSectorId == sectorId);
}

// Sector ids adjacent to sectorId through enabled transitions.
std::vector<std::string> GatherAdjacentSectorIds(const std::vector<SceneObject>& objects,
                                                 const std::string& sectorId);

// -- Copy / duplicate support ----------------------------------------------
// Single-object duplicate: fresh ids for owned map components; portal and
// transition cross links are cleared because the counterparts still point at
// the originals. Sector graph nodes get a small offset so they don't stack.
void PrepareDuplicatedObjectMapComponents(SceneObject& obj);

// Group paste: regenerates every map id owned by the pasted objects and
// rewrites references among them consistently, so pasting a whole sector
// subtree (or map root) yields an intact copy. References to ids outside the
// pasted group (for example a transition to an existing sector) are kept.
void RemapMapIdsForPastedObjects(std::vector<SceneObject*>& pastedObjects);

// -- Validation -------------------------------------------------------------
enum class MapIssueSeverity {
    Info = 0,
    Warning = 1,
    Error = 2
};

struct MapValidationIssue {
    MapIssueSeverity severity = MapIssueSeverity::Warning;
    std::string message;
    int objectId = -1; // click-to-focus target; -1 when no object applies
};

// Full structural validation of the map data in the scene (duplicate ids,
// broken references, unreachable sectors, portal/transition mismatches).
std::vector<MapValidationIssue> ValidateMap(const std::vector<SceneObject>& objects);

// Topology/data validation for an editable mesh: out-of-range indices,
// non-finite coordinates, zero-area (degenerate) faces, invalid normals,
// duplicate face entries. Returns human-readable problem descriptions;
// empty means the mesh is safe to edit and render.
std::vector<std::string> ValidateMeshAsset(const RawMeshAsset& mesh);

} // namespace MapMaker
