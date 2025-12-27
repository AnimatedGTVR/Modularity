#include "PhysicsSystem.h"

#ifdef MODULARITY_ENABLE_PHYSX
#include "PxPhysicsAPI.h"
#include "ModelLoader.h"
#include <numeric>
#include <algorithm>
#include "extensions/PxRigidBodyExt.h"

using namespace physx;

namespace {
PxVec3 ToPxVec3(const glm::vec3& v) {
    return PxVec3(v.x, v.y, v.z);
}

PxQuat ToPxQuat(const glm::vec3& eulerDeg) {
    glm::vec3 radians = glm::radians(eulerDeg);
    glm::quat q = glm::quat(radians);
    return PxQuat(q.x, q.y, q.z, q.w);
}

glm::vec3 ToGlmVec3(const PxVec3& v) {
    return glm::vec3(v.x, v.y, v.z);
}

glm::vec3 ToGlmEulerDeg(const PxQuat& q) {
    glm::quat gq(q.w, q.x, q.y, q.z);
    return glm::degrees(glm::eulerAngles(gq));
}
} // namespace

namespace {
struct IgnoreActorFilter : PxQueryFilterCallback {
    PxRigidActor* ignore = nullptr;
    explicit IgnoreActorFilter(PxRigidActor* actor) : ignore(actor) {}

    PxQueryHitType::Enum preFilter(const PxFilterData&,
                                   const PxShape* shape,
                                   const PxRigidActor* actor,
                                   PxHitFlags&) override {
        if (actor == ignore) return PxQueryHitType::eNONE;
        // Keep default blocking behaviour
        if (shape && shape->getFlags().isSet(PxShapeFlag::eTRIGGER_SHAPE)) {
            return PxQueryHitType::eNONE;
        }
        return PxQueryHitType::eBLOCK;
    }

    PxQueryHitType::Enum postFilter(const PxFilterData&,
                                    const PxQueryHit&,
                                    const PxShape*,
                                    const PxRigidActor*) override {
        return PxQueryHitType::eBLOCK;
    }
};
} // namespace

bool PhysicsSystem::init() {
    if (isReady()) return true;

    mFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, mAllocator, mErrorCallback);
    if (!mFoundation) return false;

    mPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *mFoundation, PxTolerancesScale(), true, nullptr);
    if (!mPhysics) return false;

    mDispatcher = PxDefaultCpuDispatcherCreate(2);
    if (!mDispatcher) return false;

    PxTolerancesScale scale = mPhysics->getTolerancesScale();
    mCookParams = PxCookingParams(scale);
    mCookParams.meshPreprocessParams |= PxMeshPreprocessingFlag::eDISABLE_ACTIVE_EDGES_PRECOMPUTE;
    mCookParams.meshPreprocessParams |= PxMeshPreprocessingFlag::eWELD_VERTICES;

    PxSceneDesc sceneDesc(mPhysics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
    sceneDesc.cpuDispatcher = mDispatcher;
    sceneDesc.filterShader = PxDefaultSimulationFilterShader;
    sceneDesc.flags |= PxSceneFlag::eENABLE_CCD;
    mScene = mPhysics->createScene(sceneDesc);
    if (!mScene) return false;

    mDefaultMaterial = mPhysics->createMaterial(0.9f, 0.9f, 0.0f);

    return mDefaultMaterial != nullptr;
}

bool PhysicsSystem::isReady() const {
    return mFoundation && mPhysics && mScene && mDefaultMaterial;
}

void PhysicsSystem::createGroundPlane() {
    if (!isReady()) return;
    if (mGroundPlane) {
        mScene->removeActor(*mGroundPlane);
        mGroundPlane->release();
        mGroundPlane = nullptr;
    }
    mGroundPlane = PxCreatePlane(*mPhysics, PxPlane(0.0f, 1.0f, 0.0f, 0.0f), *mDefaultMaterial);
    if (mGroundPlane) {
        mScene->addActor(*mGroundPlane);
    }
}

bool PhysicsSystem::gatherMeshData(const SceneObject& obj, std::vector<PxVec3>& vertices, std::vector<uint32_t>& indices) const {
    const OBJLoader::LoadedMesh* meshInfo = nullptr;
    if (obj.type == ObjectType::OBJMesh && obj.meshId >= 0) {
        meshInfo = g_objLoader.getMeshInfo(obj.meshId);
    } else if (obj.type == ObjectType::Model && obj.meshId >= 0) {
        meshInfo = getModelLoader().getMeshInfo(obj.meshId);
    }
    if (!meshInfo || meshInfo->triangleVertices.empty()) {
        return false;
    }

    vertices.reserve(meshInfo->triangleVertices.size());
    indices.resize(meshInfo->triangleVertices.size());

    for (size_t i = 0; i < meshInfo->triangleVertices.size(); ++i) {
        const glm::vec3& v = meshInfo->triangleVertices[i];
        vertices.emplace_back(v.x, v.y, v.z);
        indices[i] = static_cast<uint32_t>(i);
    }

    return !vertices.empty() && (indices.size() % 3 == 0);
}

PxTriangleMesh* PhysicsSystem::cookTriangleMesh(const std::vector<PxVec3>& vertices,
                                                const std::vector<uint32_t>& indices) const {
    if (vertices.empty() || indices.size() < 3) return nullptr;

    PxTriangleMeshDesc desc;
    desc.points.count = static_cast<uint32_t>(vertices.size());
    desc.points.stride = sizeof(PxVec3);
    desc.points.data = vertices.data();
    desc.triangles.count = static_cast<uint32_t>(indices.size() / 3);
    desc.triangles.stride = 3 * sizeof(uint32_t);
    desc.triangles.data = indices.data();

    PxDefaultMemoryOutputStream buf;
    if (!PxCookTriangleMesh(mCookParams, desc, buf)) {
        return nullptr;
    }
    PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
    return mPhysics->createTriangleMesh(input);
}

PxConvexMesh* PhysicsSystem::cookConvexMesh(const std::vector<PxVec3>& vertices) const {
    if (vertices.size() < 4) return nullptr;

    PxConvexMeshDesc desc;
    desc.points.count = static_cast<uint32_t>(vertices.size());
    desc.points.stride = sizeof(PxVec3);
    desc.points.data = vertices.data();
    desc.flags = PxConvexFlag::eCOMPUTE_CONVEX | PxConvexFlag::eCHECK_ZERO_AREA_TRIANGLES;
    desc.vertexLimit = 255;

    PxDefaultMemoryOutputStream buf;
    if (!PxCookConvexMesh(mCookParams, desc, buf)) {
        return nullptr;
    }
    PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
    return mPhysics->createConvexMesh(input);
}

bool PhysicsSystem::attachPrimitiveShape(PxRigidActor* actor, const SceneObject& obj, bool isDynamic) const {
    (void)isDynamic;
    if (!actor) return false;
    PxShape* shape = nullptr;
    auto tuneShape = [](PxShape* s, float minDim, bool /*swept*/) {
        if (!s) return;
        float contact = std::clamp(minDim * 0.2f, 0.02f, 0.2f);
        float rest = contact * 0.15f;
        s->setContactOffset(contact);
        s->setRestOffset(rest);
    };

    switch (obj.type) {
        case ObjectType::Cube: {
            PxVec3 halfExtents = ToPxVec3(glm::max(obj.scale * 0.5f, glm::vec3(0.01f)));
            shape = mPhysics->createShape(PxBoxGeometry(halfExtents), *mDefaultMaterial, true);
            tuneShape(shape, std::min({halfExtents.x, halfExtents.y, halfExtents.z}) * 2.0f, isDynamic);
            break;
        }
        case ObjectType::Sphere: {
            float radius = std::max({obj.scale.x, obj.scale.y, obj.scale.z}) * 0.5f;
            radius = std::max(radius, 0.01f);
            shape = mPhysics->createShape(PxSphereGeometry(radius), *mDefaultMaterial, true);
            tuneShape(shape, radius * 2.0f, isDynamic);
            break;
        }
        case ObjectType::Capsule: {
        float radius = std::max(obj.scale.x, obj.scale.z) * 0.5f;
        radius = std::max(radius, 0.01f);
        float cylHeight = std::max(0.05f, obj.scale.y - radius * 2.0f);
        float halfHeight = cylHeight * 0.5f;
        shape = mPhysics->createShape(PxCapsuleGeometry(radius, halfHeight), *mDefaultMaterial, true);
        if (shape) {
            // PhysX capsules default to the X axis; rotate to align with Y (character up)
            shape->setLocalPose(PxTransform(PxQuat(PxHalfPi, PxVec3(0, 0, 1))));
        }
        tuneShape(shape, std::min(radius * 2.0f, halfHeight * 2.0f), isDynamic);
        break;
    }
        case ObjectType::Plane: {
            glm::vec3 halfExtents = glm::max(obj.scale * 0.5f, glm::vec3(0.01f));
            halfExtents.z = std::max(halfExtents.z, 0.01f);
            shape = mPhysics->createShape(PxBoxGeometry(ToPxVec3(halfExtents)), *mDefaultMaterial, true);
            tuneShape(shape, std::min({halfExtents.x, halfExtents.y, halfExtents.z}) * 2.0f, isDynamic);
            break;
        }
        case ObjectType::Torus: {
            float radius = std::max({obj.scale.x, obj.scale.y, obj.scale.z}) * 0.5f;
            radius = std::max(radius, 0.01f);
            shape = mPhysics->createShape(PxSphereGeometry(radius), *mDefaultMaterial, true);
            tuneShape(shape, radius * 2.0f, isDynamic);
            break;
        }
        default:
            break;
    }

    if (!shape) return false;
    actor->attachShape(*shape);
    shape->release();
    return true;
}

bool PhysicsSystem::attachColliderShape(PxRigidActor* actor, const SceneObject& obj, bool isDynamic) const {
    if (!actor || !obj.hasCollider || !obj.collider.enabled) return false;

    PxShape* shape = nullptr;
    auto tuneShape = [](PxShape* s, float minDim, bool /*swept*/) {
        if (!s) return;
        float contact = std::clamp(minDim * 0.12f, 0.015f, 0.12f);
        float rest = contact * 0.2f;
        s->setContactOffset(contact);
        s->setRestOffset(rest);
    };
    float minDim = 0.1f;
    if (obj.collider.type == ColliderType::Box) {
        glm::vec3 half = glm::max(obj.collider.boxSize * 0.5f, glm::vec3(0.01f));
        shape = mPhysics->createShape(PxBoxGeometry(ToPxVec3(half)), *mDefaultMaterial, true);
        minDim = std::min({half.x, half.y, half.z}) * 2.0f;
    } else if (obj.collider.type == ColliderType::Capsule) {
        float radius = std::max({obj.collider.boxSize.x, obj.collider.boxSize.z}) * 0.5f;
        radius = std::max(radius, 0.01f);
        float cylHeight = std::max(0.05f, obj.collider.boxSize.y - radius * 2.0f);
        float halfHeight = cylHeight * 0.5f;
        shape = mPhysics->createShape(PxCapsuleGeometry(radius, halfHeight), *mDefaultMaterial, true);
        if (shape) {
            // Rotate capsule so its axis matches the engine's Y-up expectation
            shape->setLocalPose(PxTransform(PxQuat(PxHalfPi, PxVec3(0, 0, 1))));
        }
        minDim = std::min(radius * 2.0f, halfHeight * 2.0f);
    } else {
        std::vector<PxVec3> verts;
        std::vector<uint32_t> indices;
        if (!gatherMeshData(obj, verts, indices)) {
            return false;
        }

        bool useConvex = obj.collider.convex || obj.collider.type == ColliderType::ConvexMesh || isDynamic;
        glm::vec3 boundsMin(FLT_MAX);
        glm::vec3 boundsMax(-FLT_MAX);
        for (auto& v : verts) {
            boundsMin.x = std::min(boundsMin.x, v.x * obj.scale.x);
            boundsMin.y = std::min(boundsMin.y, v.y * obj.scale.y);
            boundsMin.z = std::min(boundsMin.z, v.z * obj.scale.z);
            boundsMax.x = std::max(boundsMax.x, v.x * obj.scale.x);
            boundsMax.y = std::max(boundsMax.y, v.y * obj.scale.y);
            boundsMax.z = std::max(boundsMax.z, v.z * obj.scale.z);
        }
        minDim = std::max(0.01f, std::min({boundsMax.x - boundsMin.x, boundsMax.y - boundsMin.y, boundsMax.z - boundsMin.z}));
        if (useConvex) {
            PxConvexMesh* convex = cookConvexMesh(verts);
            if (!convex) return false;
            PxConvexMeshGeometry geom(convex, PxMeshScale(ToPxVec3(obj.scale), PxQuat(PxIdentity)));
            shape = mPhysics->createShape(geom, *mDefaultMaterial, true);
            convex->release();
        } else {
            PxTriangleMesh* tri = cookTriangleMesh(verts, indices);
            if (!tri) return false;
            PxTriangleMeshGeometry geom(tri, PxMeshScale(ToPxVec3(obj.scale), PxQuat(PxIdentity)));
            shape = mPhysics->createShape(geom, *mDefaultMaterial, true);
            tri->release();
        }
    }

    tuneShape(shape, std::max(0.01f, minDim), isDynamic || obj.hasPlayerController);

    if (!shape) return false;
    actor->attachShape(*shape);
    shape->release();
    return true;
}

PhysicsSystem::ActorRecord PhysicsSystem::createActorFor(const SceneObject& obj) const {
    ActorRecord record;

    const bool wantsDynamic = obj.hasRigidbody && obj.rigidbody.enabled;
    const bool wantsCollider = obj.hasCollider && obj.collider.enabled;
    if (!wantsDynamic && !wantsCollider) {
        return record;
    }

    PxTransform transform(ToPxVec3(obj.position), ToPxQuat(obj.rotation));

    PxRigidActor* actor = wantsDynamic
        ? static_cast<PxRigidActor*>(mPhysics->createRigidDynamic(transform))
        : static_cast<PxRigidActor*>(mPhysics->createRigidStatic(transform));

    if (!actor) return record;

    record.actor = actor;
    record.isDynamic = wantsDynamic;
    record.isKinematic = wantsDynamic && obj.rigidbody.isKinematic;

    bool attached = false;
    // Keep actor facing initial yaw (ignore pitch/roll)
    if (PxRigidDynamic* dyn = actor->is<PxRigidDynamic>()) {
        PxTransform pose = dyn->getGlobalPose();
        pose.q = PxQuat(static_cast<float>(glm::radians(obj.rotation.y)), PxVec3(0, 1, 0));
        dyn->setGlobalPose(pose);
    } else {
        PxTransform pose = actor->getGlobalPose();
        pose.q = PxQuat(static_cast<float>(glm::radians(obj.rotation.y)), PxVec3(0, 1, 0));
        actor->setGlobalPose(pose);
    }
    if (wantsCollider) {
        attached = attachColliderShape(actor, obj, wantsDynamic);
    }
    if (!attached) {
        attached = attachPrimitiveShape(actor, obj, wantsDynamic);
    }

    if (!attached) {
        actor->release();
        record.actor = nullptr;
        return record;
    }

    if (PxRigidDynamic* dyn = actor->is<PxRigidDynamic>()) {
        dyn->setAngularDamping(obj.rigidbody.angularDamping);
        dyn->setLinearDamping(obj.rigidbody.linearDamping);
        dyn->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, obj.rigidbody.isKinematic);
        dyn->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !obj.rigidbody.useGravity);
        PxRigidDynamicLockFlags lockFlags;
        if (obj.rigidbody.lockRotationX) lockFlags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
        if (obj.rigidbody.lockRotationY) lockFlags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
        if (obj.rigidbody.lockRotationZ) lockFlags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
        dyn->setRigidDynamicLockFlags(lockFlags);
        if (obj.hasPlayerController) {
            dyn->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
            dyn->setMaxDepenetrationVelocity(1.5f);
        }
        if (!obj.rigidbody.isKinematic) {
            PxRigidBodyExt::updateMassAndInertia(*dyn, std::max(0.01f, obj.rigidbody.mass));
        }
    }

    return record;
}

void PhysicsSystem::clearActors() {
    for (auto& [id, rec] : mActors) {
        if (rec.actor && mScene) {
            mScene->removeActor(*rec.actor);
            rec.actor->release();
        }
    }
    mActors.clear();

    if (mGroundPlane && mScene) {
        mScene->removeActor(*mGroundPlane);
        mGroundPlane->release();
        mGroundPlane = nullptr;
    }
}

void PhysicsSystem::onPlayStart(const std::vector<SceneObject>& objects) {
    if (!isReady()) return;

    clearActors();
    createGroundPlane();

    for (const auto& obj : objects) {
        if (!obj.enabled) continue;
        ActorRecord rec = createActorFor(obj);
        if (!rec.actor) continue;
        mScene->addActor(*rec.actor);
        mActors[obj.id] = rec;
    }
}

void PhysicsSystem::onPlayStop() {
    clearActors();
}

bool PhysicsSystem::setLinearVelocity(int id, const glm::vec3& velocity) {
#ifdef MODULARITY_ENABLE_PHYSX
    auto it = mActors.find(id);
    if (it == mActors.end()) return false;
    ActorRecord& rec = it->second;
    if (!rec.actor || !rec.isDynamic) return false;
    if (PxRigidDynamic* dyn = rec.actor->is<PxRigidDynamic>()) {
        dyn->setLinearVelocity(ToPxVec3(velocity));
        return true;
    }
#endif
    return false;
}

bool PhysicsSystem::setAngularVelocity(int id, const glm::vec3& velocity) {
#ifdef MODULARITY_ENABLE_PHYSX
    auto it = mActors.find(id);
    if (it == mActors.end()) return false;
    ActorRecord& rec = it->second;
    if (!rec.actor || !rec.isDynamic || rec.isKinematic) return false;
    if (PxRigidDynamic* dyn = rec.actor->is<PxRigidDynamic>()) {
        dyn->setAngularVelocity(ToPxVec3(velocity));
        return true;
    }
#endif
    return false;
}

bool PhysicsSystem::setActorYaw(int id, float yawDegrees) {
#ifdef MODULARITY_ENABLE_PHYSX
    auto it = mActors.find(id);
    if (it == mActors.end()) return false;
    ActorRecord& rec = it->second;
    if (!rec.actor) return false;
    PxTransform pose = rec.actor->getGlobalPose();
    PxQuat yawQuat(static_cast<float>(glm::radians(yawDegrees)), PxVec3(0, 1, 0));
    pose.q = yawQuat;
    rec.actor->setGlobalPose(pose);
    return true;
#endif
    return false;
}

bool PhysicsSystem::getLinearVelocity(int id, glm::vec3& outVelocity) const {
#ifdef MODULARITY_ENABLE_PHYSX
    auto it = mActors.find(id);
    if (it == mActors.end()) return false;
    const ActorRecord& rec = it->second;
    if (!rec.actor || !rec.isDynamic) return false;
    if (const PxRigidDynamic* dyn = rec.actor->is<PxRigidDynamic>()) {
        PxVec3 v = dyn->getLinearVelocity();
        outVelocity = glm::vec3(v.x, v.y, v.z);
        return true;
    }
#endif
    return false;
}

bool PhysicsSystem::getAngularVelocity(int id, glm::vec3& outVelocity) const {
#ifdef MODULARITY_ENABLE_PHYSX
    auto it = mActors.find(id);
    if (it == mActors.end()) return false;
    const ActorRecord& rec = it->second;
    if (!rec.actor || !rec.isDynamic || rec.isKinematic) return false;
    if (const PxRigidDynamic* dyn = rec.actor->is<PxRigidDynamic>()) {
        PxVec3 v = dyn->getAngularVelocity();
        outVelocity = glm::vec3(v.x, v.y, v.z);
        return true;
    }
#endif
    return false;
}

bool PhysicsSystem::setActorPose(int id, const glm::vec3& position, const glm::vec3& rotationDeg) {
#ifdef MODULARITY_ENABLE_PHYSX
    auto it = mActors.find(id);
    if (it == mActors.end()) return false;
    ActorRecord& rec = it->second;
    if (!rec.actor) return false;
    PxTransform pose(ToPxVec3(position), ToPxQuat(rotationDeg));
    rec.actor->setGlobalPose(pose);
    return true;
#else
    (void)id; (void)position; (void)rotationDeg;
    return false;
#endif
}

bool PhysicsSystem::addForce(int id, const glm::vec3& force) {
#ifdef MODULARITY_ENABLE_PHYSX
    auto it = mActors.find(id);
    if (it == mActors.end()) return false;
    ActorRecord& rec = it->second;
    if (!rec.actor || !rec.isDynamic || rec.isKinematic) return false;
    if (PxRigidDynamic* dyn = rec.actor->is<PxRigidDynamic>()) {
        dyn->addForce(ToPxVec3(force), PxForceMode::eFORCE);
        return true;
    }
#endif
    return false;
}

bool PhysicsSystem::addImpulse(int id, const glm::vec3& impulse) {
#ifdef MODULARITY_ENABLE_PHYSX
    auto it = mActors.find(id);
    if (it == mActors.end()) return false;
    ActorRecord& rec = it->second;
    if (!rec.actor || !rec.isDynamic || rec.isKinematic) return false;
    if (PxRigidDynamic* dyn = rec.actor->is<PxRigidDynamic>()) {
        dyn->addForce(ToPxVec3(impulse), PxForceMode::eIMPULSE);
        return true;
    }
#endif
    return false;
}

bool PhysicsSystem::addTorque(int id, const glm::vec3& torque) {
#ifdef MODULARITY_ENABLE_PHYSX
    auto it = mActors.find(id);
    if (it == mActors.end()) return false;
    ActorRecord& rec = it->second;
    if (!rec.actor || !rec.isDynamic || rec.isKinematic) return false;
    if (PxRigidDynamic* dyn = rec.actor->is<PxRigidDynamic>()) {
        dyn->addTorque(ToPxVec3(torque), PxForceMode::eFORCE);
        return true;
    }
#endif
    return false;
}

bool PhysicsSystem::addAngularImpulse(int id, const glm::vec3& impulse) {
#ifdef MODULARITY_ENABLE_PHYSX
    auto it = mActors.find(id);
    if (it == mActors.end()) return false;
    ActorRecord& rec = it->second;
    if (!rec.actor || !rec.isDynamic || rec.isKinematic) return false;
    if (PxRigidDynamic* dyn = rec.actor->is<PxRigidDynamic>()) {
        dyn->addTorque(ToPxVec3(impulse), PxForceMode::eIMPULSE);
        return true;
    }
#endif
    return false;
}

bool PhysicsSystem::raycastClosest(const glm::vec3& origin, const glm::vec3& dir, float distance,
                                   int ignoreId, glm::vec3* hitPos, glm::vec3* hitNormal, float* hitDistance) const {
#ifdef MODULARITY_ENABLE_PHYSX
    if (!isReady() || distance <= 0.0f) return false;
    PxVec3 unitDir = ToPxVec3(glm::normalize(dir));
    if (!unitDir.isFinite()) return false;

    PxRaycastBuffer hit;
    PxQueryFilterData fd(PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);
    IgnoreActorFilter cb(nullptr);

    auto it = mActors.find(ignoreId);
    if (it != mActors.end()) {
        cb.ignore = it->second.actor;
    }

    bool result = mScene->raycast(ToPxVec3(origin), unitDir, distance, hit,
                                  PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
                                  fd, cb.ignore ? &cb : nullptr);
    if (!result || !hit.hasBlock) return false;

    if (hitPos) *hitPos = ToGlmVec3(hit.block.position);
    if (hitNormal) *hitNormal = ToGlmVec3(hit.block.normal);
    if (hitDistance) *hitDistance = hit.block.distance;
    return true;
#else
    (void)origin; (void)dir; (void)distance; (void)ignoreId; (void)hitPos; (void)hitNormal; (void)hitDistance;
    return false;
#endif
}

void PhysicsSystem::simulate(float deltaTime, std::vector<SceneObject>& objects) {
    if (!isReady() || deltaTime <= 0.0f) return;

    // Sync actors to authoring transforms before stepping
    for (auto& [id, rec] : mActors) {
        if (!rec.actor) continue;
        auto it = std::find_if(objects.begin(), objects.end(), [id](const SceneObject& o) { return o.id == id; });
        if (it == objects.end()) continue;
        if (!it->enabled) {
            rec.actor->setActorFlag(PxActorFlag::eDISABLE_SIMULATION, true);
            continue;
        } else {
            rec.actor->setActorFlag(PxActorFlag::eDISABLE_SIMULATION, false);
        }
        if (PxRigidDynamic* dyn = rec.actor->is<PxRigidDynamic>()) {
            if (dyn->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC)) {
                dyn->setKinematicTarget(PxTransform(ToPxVec3(it->position), ToPxQuat(it->rotation)));
            }
        } else {
            // Static actors follow their authoring transform so scripted moves/rotations take effect
            rec.actor->setGlobalPose(PxTransform(ToPxVec3(it->position), ToPxQuat(it->rotation)));
        }
    }

    mScene->simulate(deltaTime);
    mScene->fetchResults(true);

    for (auto& [id, rec] : mActors) {
        if (!rec.actor || !rec.isDynamic || rec.isKinematic) continue;
        PxTransform pose = rec.actor->getGlobalPose();
        auto it = std::find_if(objects.begin(), objects.end(), [id](const SceneObject& o) { return o.id == id; });
        if (it == objects.end() || !it->enabled) continue;

        it->position = ToGlmVec3(pose.p);
        if (it->hasPlayerController && it->playerController.enabled) {
            continue;
        }
        glm::vec3 euler = ToGlmEulerDeg(pose.q);
        if (PxRigidDynamic* dyn = rec.actor->is<PxRigidDynamic>()) {
            PxRigidDynamicLockFlags lockFlags = dyn->getRigidDynamicLockFlags();
            bool lockX = lockFlags.isSet(PxRigidDynamicLockFlag::eLOCK_ANGULAR_X);
            bool lockY = lockFlags.isSet(PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y);
            bool lockZ = lockFlags.isSet(PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z);
            if (lockX && lockZ && !lockY) {
                it->rotation.y = euler.y;
            } else {
                it->rotation = euler;
            }
        } else {
            it->rotation.y = euler.y;
        }
    }
}

void PhysicsSystem::shutdown() {
    clearActors();

    if (mScene) { mScene->release(); mScene = nullptr; }
    if (mDispatcher) { mDispatcher->release(); mDispatcher = nullptr; }
    if (mPhysics) { mPhysics->release(); mPhysics = nullptr; }
    if (mFoundation) { mFoundation->release(); mFoundation = nullptr; }
    mDefaultMaterial = nullptr;
}

#else // MODULARITY_ENABLE_PHYSX

bool PhysicsSystem::init() { return false; }
void PhysicsSystem::shutdown() {}
bool PhysicsSystem::isReady() const { return false; }
bool PhysicsSystem::setLinearVelocity(int, const glm::vec3&) { return false; }
bool PhysicsSystem::setAngularVelocity(int, const glm::vec3&) { return false; }
bool PhysicsSystem::setActorYaw(int, float) { return false; }
bool PhysicsSystem::getLinearVelocity(int, glm::vec3&) const { return false; }
bool PhysicsSystem::getAngularVelocity(int, glm::vec3&) const { return false; }
bool PhysicsSystem::setActorPose(int, const glm::vec3&, const glm::vec3&) { return false; }
bool PhysicsSystem::addForce(int, const glm::vec3&) { return false; }
bool PhysicsSystem::addImpulse(int, const glm::vec3&) { return false; }
bool PhysicsSystem::addTorque(int, const glm::vec3&) { return false; }
bool PhysicsSystem::addAngularImpulse(int, const glm::vec3&) { return false; }
void PhysicsSystem::onPlayStart(const std::vector<SceneObject>&) {}
void PhysicsSystem::onPlayStop() {}
void PhysicsSystem::simulate(float, std::vector<SceneObject>&) {}

#endif
