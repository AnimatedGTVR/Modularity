#pragma once

#include "IPhysicsBackend.h"
#include "ProjectManager.h"

#include <memory>

// Returns a fresh IPhysicsBackend implementation chosen from the project's
// physics settings. Engine.h calls this once per physics-engine startup
// (typically once at engine init; changing the selection in the editor
// takes effect on the next engine restart / re-init).
//
// PhysX is unavailable on Android — if PhysX is requested on a build that
// did not include PhysX (MODULARITY_ENABLE_PHYSX=OFF), this transparently
// falls back to Jolt so the engine always has a working backend.
std::unique_ptr<IPhysicsBackend> CreatePhysicsBackend(PhysicsBackendType type);
