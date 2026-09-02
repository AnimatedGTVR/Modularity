#pragma once

#include "ModelLoader.h"
#include <string>
#include <vector>

// Controlled planar carve for the Map Maker's Carve Mode.
//
// This is intentionally NOT a general CSG boolean. The operation is limited
// to cutting a convex opening (rectangle or N-gon) through a planar wall
// region of a RawMeshAsset:
//   1. gather the connected coplanar face island under the picked face
//   2. validate the opening fits fully inside that island
//   3. retriangulate the island with the opening as a hole (ear clipping
//      with hole bridging)
//   4. do the same on the opposite, parallel island when cutting through
//   5. bridge the two rims with tunnel quads (or close a pocket floor)
//   6. optionally add a flush frame ring around the opening
//
// Everything runs in mesh-local space on a caller-owned mesh copy so a
// failed carve can be discarded without touching the original data.
namespace MapCarve {

struct PlaneBasis {
    glm::vec3 origin = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 axisU = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 axisV = glm::vec3(0.0f, 1.0f, 0.0f);

    glm::vec2 toPlane(const glm::vec3& p) const {
        const glm::vec3 d = p - origin;
        return glm::vec2(glm::dot(d, axisU), glm::dot(d, axisV));
    }
    glm::vec3 toWorld(const glm::vec2& uv) const {
        return origin + axisU * uv.x + axisV * uv.y;
    }
};

// CCW polygon in plane (U,V) coordinates.
using CarveShape = std::vector<glm::vec2>;

CarveShape MakeRectShape(const glm::vec2& center, const glm::vec2& halfSize);
// segments >= 3; 4 gives a diamond, use MakeRectShape for square openings.
CarveShape MakeEllipseShape(const glm::vec2& center, const glm::vec2& halfSize, int segments);

// Offsets a convex CCW polygon outward (positive) or inward (negative).
// Returns false when the offset would invert the polygon.
bool OffsetConvexPolygon(const CarveShape& shape, float offset, CarveShape& out);

// Orthonormal right-handed basis (normal = axisU x axisV) from a face.
bool BuildPlaneBasisFromFace(const RawMeshAsset& mesh, int faceIndex,
                             PlaneBasis& outBasis, std::string* outError = nullptr);

// Connected faces coplanar with faceIndex (walks shared edges only).
std::vector<int> GatherCoplanarIsland(const RawMeshAsset& mesh, int faceIndex,
                                      float maxNormalAngleDeg = 4.0f,
                                      float maxPlaneDistance = 0.005f);

// True when every sampled point of the shape lies strictly inside the island
// and no island boundary vertex lies inside the shape. On failure outReason
// gets a human-readable explanation for the preview overlay.
bool ShapeFitsIsland(const RawMeshAsset& mesh, const std::vector<int>& islandFaces,
                     const PlaneBasis& basis, const CarveShape& shape,
                     std::string* outReason = nullptr);

struct CarveOptions {
    bool cutThrough = true;
    float pocketDepth = 0.2f;        // used when cutThrough == false
    float maxThroughDistance = 8.0f; // how far to search for the back wall
    bool createFrame = false;
    float frameThickness = 0.08f;    // flush ring width around the opening
    float surfaceOffset = 0.0f;      // push the opening along the wall normal
    uint32_t newFaceMaterial = 0;    // material slot for tunnel/frame faces
};

struct CarveOutcome {
    bool success = false;
    std::string error;
    // Front rim vertex indices of the final opening, in loop order. Useful
    // for portal placement (centroid = doorway anchor).
    std::vector<uint32_t> frontRimVerts;
    std::vector<uint32_t> backRimVerts; // empty for pockets without through-cut
    // Indices of faces appended by the carve (tunnel, rings, pocket floor).
    std::vector<int> newFaceIndices;
};

// Carves shapeUV (given in the plane basis of frontFaceIndex) into the mesh.
// The mesh reference is modified ONLY on success == true.
CarveOutcome CarveShapeIntoMesh(RawMeshAsset& mesh, int frontFaceIndex,
                                const CarveShape& shapeUV, const CarveOptions& options);

} // namespace MapCarve
