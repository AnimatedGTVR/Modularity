#pragma once
#include "Common.h"
#include "ProjectManager.h"
#include "SceneObject.h"
#include <vector>

// Interface for swappable physics backends (PhysX, Jolt). Picked per-project
// via ProjectPhysicsSettings; Jolt is the default where PhysX is unsupported.
// Keep signatures glm/STL-only.
class IPhysicsBackend {
public:
    virtual ~IPhysicsBackend() = default;
    virtual bool init() = 0;
    virtual void shutdown() = 0;
    virtual bool isReady() const = 0;
    virtual void setProjectSettings(const ProjectPhysicsSettings& settings) = 0;
    virtual bool setLinearVelocity(int id, const glm::vec3& velocity) = 0;
    virtual bool setAngularVelocity(int id, const glm::vec3& velocity) = 0;
    virtual bool setActorYaw(int id, float yawDegrees) = 0;
    virtual bool getLinearVelocity(int id, glm::vec3& outVelocity) const = 0;
    virtual bool getAngularVelocity(int id, glm::vec3& outVelocity) const = 0;
    virtual bool setActorPose(int id, const glm::vec3& position, const glm::vec3& rotationDeg) = 0;
    virtual bool addForce(int id, const glm::vec3& force) = 0;
    virtual bool addImpulse(int id, const glm::vec3& impulse) = 0;
    virtual bool addTorque(int id, const glm::vec3& torque) = 0;
    virtual bool addAngularImpulse(int id, const glm::vec3& impulse) = 0;
    virtual bool raycastClosest(const glm::vec3& origin, const glm::vec3& dir, float distance,
                                int ignoreId, glm::vec3* hitPos = nullptr,glm::vec3* hitNormal = nullptr,
                                float* hitDistance = nullptr, int* hitActorId = nullptr,
                                glm::vec3* hitActorVelocity = nullptr, float* hitStaticFriction = nullptr,
                                float* hitDynamicFriction = nullptr) const = 0;
    virtual void onPlayStart(const std::vector<SceneObject>& objects) = 0;
    virtual void onPlayStop() = 0;
    virtual void simulate(float deltaTime, std::vector<SceneObject>& objects) = 0;
};
