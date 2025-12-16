#pragma once

#include "Common.h"
#include "SceneObject.h"
#include <unordered_map>
#include <vector>

#ifdef MODULARITY_ENABLE_PHYSX
#include "PxPhysicsAPI.h"
#include "cooking/PxCooking.h"
#endif

class PhysicsSystem {
public:
    bool init();
    void shutdown();
    bool isReady() const;
    bool setLinearVelocity(int id, const glm::vec3& velocity);
    bool setActorYaw(int id, float yawDegrees);
    bool getLinearVelocity(int id, glm::vec3& outVelocity) const;
    bool setActorPose(int id, const glm::vec3& position, const glm::vec3& rotationDeg);
    bool raycastClosest(const glm::vec3& origin, const glm::vec3& dir, float distance,
                        int ignoreId, glm::vec3* hitPos = nullptr,
                        glm::vec3* hitNormal = nullptr, float* hitDistance = nullptr) const;

    void onPlayStart(const std::vector<SceneObject>& objects);
    void onPlayStop();
    void simulate(float deltaTime, std::vector<SceneObject>& objects);

private:
#ifdef MODULARITY_ENABLE_PHYSX
    struct ActorRecord {
        physx::PxRigidActor* actor = nullptr;
        bool isDynamic = false;
        bool isKinematic = false;
    };

    physx::PxDefaultAllocator mAllocator;
    physx::PxDefaultErrorCallback mErrorCallback;
    physx::PxFoundation* mFoundation = nullptr;
    physx::PxPhysics* mPhysics = nullptr;
    physx::PxDefaultCpuDispatcher* mDispatcher = nullptr;
    physx::PxScene* mScene = nullptr;
    physx::PxMaterial* mDefaultMaterial = nullptr;
    physx::PxRigidStatic* mGroundPlane = nullptr;
    physx::PxCookingParams mCookParams{physx::PxTolerancesScale()};

    std::unordered_map<int, ActorRecord> mActors;

    void clearActors();
    void createGroundPlane();
    ActorRecord createActorFor(const SceneObject& obj) const;
    bool attachColliderShape(physx::PxRigidActor* actor, const SceneObject& obj, bool isDynamic) const;
    bool attachPrimitiveShape(physx::PxRigidActor* actor, const SceneObject& obj, bool isDynamic) const;
    bool gatherMeshData(const SceneObject& obj, std::vector<physx::PxVec3>& vertices, std::vector<uint32_t>& indices) const;
    physx::PxTriangleMesh* cookTriangleMesh(const std::vector<physx::PxVec3>& vertices,
                                            const std::vector<uint32_t>& indices) const;
    physx::PxConvexMesh* cookConvexMesh(const std::vector<physx::PxVec3>& vertices) const;
#endif
};
