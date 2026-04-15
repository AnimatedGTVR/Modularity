#pragma once

#include "Common.h"
#include "ProjectManager.h"
#include "SceneObject.h"
#include <cstdint>
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
    void setProjectSettings(const ProjectPhysicsSettings& settings);
    bool setLinearVelocity(int id, const glm::vec3& velocity);
    bool setAngularVelocity(int id, const glm::vec3& velocity);
    bool setActorYaw(int id, float yawDegrees);
    bool getLinearVelocity(int id, glm::vec3& outVelocity) const;
    bool getAngularVelocity(int id, glm::vec3& outVelocity) const;
    bool setActorPose(int id, const glm::vec3& position, const glm::vec3& rotationDeg);
    bool addForce(int id, const glm::vec3& force);
    bool addImpulse(int id, const glm::vec3& impulse);
    bool addTorque(int id, const glm::vec3& torque);
    bool addAngularImpulse(int id, const glm::vec3& impulse);
    bool raycastClosest(const glm::vec3& origin, const glm::vec3& dir, float distance,
                        int ignoreId, glm::vec3* hitPos = nullptr,
                        glm::vec3* hitNormal = nullptr, float* hitDistance = nullptr,
                        int* hitActorId = nullptr, glm::vec3* hitActorVelocity = nullptr,
                        float* hitStaticFriction = nullptr, float* hitDynamicFriction = nullptr) const;

    void onPlayStart(const std::vector<SceneObject>& objects);
    void onPlayStop();
    void simulate(float deltaTime, std::vector<SceneObject>& objects);

private:
#ifdef MODULARITY_ENABLE_PHYSX
    struct ActorRecord {
        physx::PxRigidActor* actor = nullptr;
        bool isDynamic = false;
        bool isKinematic = false;
        bool hasWorldBakedStaticMesh = false;
        bool simulationDisabled = false;
        bool hasAuthoringPose = false;
        bool gravityDisabled = false;
        float linearDamping = 0.0f;
        float angularDamping = 0.0f;
        float massKg = 0.0f;
        bool useCustomCenterOfMass = false;
        glm::vec3 centerOfMass = glm::vec3(0.0f);
        glm::vec3 lastAuthoringPosition = glm::vec3(0.0f);
        glm::vec3 lastAuthoringRotation = glm::vec3(0.0f);
        uint32_t angularLockMask = 0;
    };

    physx::PxDefaultAllocator mAllocator;
    physx::PxDefaultErrorCallback mErrorCallback;
    physx::PxFoundation* mFoundation = nullptr;
    physx::PxPhysics* mPhysics = nullptr;
    physx::PxDefaultCpuDispatcher* mDispatcher = nullptr;
    physx::PxScene* mScene = nullptr;
    physx::PxMaterial* mDefaultMaterial = nullptr;
    physx::PxCookingParams mCookParams{physx::PxTolerancesScale()};
    ProjectPhysicsSettings mProjectSettings;

    std::unordered_map<int, ActorRecord> mActors;
    std::unordered_map<const physx::PxRigidActor*, int> mActorIdsByPtr;

    void clearActors();
    void applySceneGravity();
    ActorRecord createActorFor(const SceneObject& obj) const;
    bool attachColliderShape(physx::PxRigidActor* actor, const SceneObject& obj, bool isDynamic,
                             bool bakeWorldSpaceStaticMesh) const;
    bool attachPrimitiveShape(physx::PxRigidActor* actor, const SceneObject& obj, bool isDynamic) const;
    bool gatherMeshData(const SceneObject& obj, std::vector<physx::PxVec3>& vertices, std::vector<uint32_t>& indices) const;
    physx::PxTriangleMesh* cookTriangleMesh(const std::vector<physx::PxVec3>& vertices,
                                            const std::vector<uint32_t>& indices) const;
    physx::PxConvexMesh* cookConvexMesh(const std::vector<physx::PxVec3>& vertices) const;
#endif
};
