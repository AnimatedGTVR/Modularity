#pragma once

#include "ModelLoader.h"
#include "Render25D/MMesh.h"

namespace Modularity::Render25D {

// RawMeshAsset <-> MMeshAsset bridges so the RMesh edit tooling can work on .mmesh assets.
MMeshAsset BuildMMeshFromRawMesh(const RawMeshAsset& raw);
RawMeshAsset BuildRawMeshFromMMesh(const MMeshAsset& asset);

} // namespace Modularity::Render25D
