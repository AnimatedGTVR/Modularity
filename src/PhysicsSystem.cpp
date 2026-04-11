#include "PhysicsSystem.h"

#ifdef MODULARITY_ENABLE_PHYSX
#include "PxPhysicsAPI.h"
#include "ModelLoader.h"
#include "Rendering.h"
#include <numeric>
#include <algorithm>
#include <new>
#include "extensions/PxRigidBodyExt.h"

using namespace physx;

namespace {
PxVec3 ToPxVec3(const glm::vec3& v) {
    return PxVec3(v.x, v.y, v.z);
}

PxQuat ToPxQuat(const glm::vec3& eulerDeg) {
    glm::vec3 r = glm::radians(eulerDeg);
    glm::mat4 m(1.0f);
    m = glm::rotate(m, r.x, glm::vec3(1.0f, 0.0f, 0.0f));
    m = glm::rotate(m, r.y, glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, r.z, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::quat q = glm::quat_cast(glm::mat3(m));
    return PxQuat(q.x, q.y, q.z, q.w);
}

glm::vec3 ToGlmVec3(const PxVec3& v) {return glm::vec3(v.x, v.y, v.z);}

glm::vec3 ToGlmVec3(const glm::vec3& v) { return v; }

glm::vec3 ExtractEulerXYZ(const glm::mat3& m) {
    float T1 = std::atan2(m[2][1], m[2][2]);
    float C2 = std::sqrt(m[0][0] * m[0][0] + m[1][0] * m[1][0]);
    float T2 = std::atan2(-m[2][0], C2);
    float S1 = std::sin(T1);
    float C1 = std::cos(T1);
    float T3 = std::atan2(S1 * m[0][2] - C1 * m[0][1], C1 * m[1][1] - S1 * m[1][2]);
    return glm::vec3(-T1, -T2, -T3);
}

glm::vec3 ToGlmEulerDeg(const PxQuat& q) {
    glm::quat gq(q.w, q.x, q.y, q.z);
    glm::mat3 m = glm::mat3_cast(gq);
    return glm::degrees(ExtractEulerXYZ(m));
}

float EffectiveMassKilograms(const SceneObject& obj, const ProjectPhysicsSettings& settings) {
    const float authoredMass = std::max(0.0001f, obj.rigidbody.mass);
    const float unitScale = std::max(0.000001f, ProjectMassUnitToKilograms(settings.massUnit));
    return std::max(0.01f, authoredMass * unitScale);
}

PxVec3 SceneGravity(const ProjectPhysicsSettings& settings) {
    return PxVec3(0.0f, -9.81f * std::max(0.0f, settings.globalGravityScale), 0.0f);
}

uint32_t BuildAngularLockMask(const SceneObject& obj) {
    uint32_t mask = 0;
    if (obj.rigidbody.lockRotationX) mask |= 1u << 0u;
    if (obj.rigidbody.lockRotationY) mask |= 1u << 1u;
    if (obj.rigidbody.lockRotationZ) mask |= 1u << 2u;
    return mask;
}

PxRigidDynamicLockFlags BuildAngularLockFlags(const SceneObject& obj) {
    PxRigidDynamicLockFlags lockFlags;
    if (obj.rigidbody.lockRotationX) lockFlags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
    if (obj.rigidbody.lockRotationY) lockFlags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
    if (obj.rigidbody.lockRotationZ) lockFlags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
    return lockFlags;
}

PxMaterial* CreateShapeMaterial(PxPhysics* physics, PxMaterial* fallback,
                                const SceneObject& obj, bool& outOwned) {
    outOwned = false;
    if (!physics || !fallback) return fallback;
    if (!obj.hasCollider || !obj.collider.enabled) {
        return fallback;
    }
    const float staticFriction = std::clamp(obj.collider.staticFriction, 0.0f, 4.0f);
    const float dynamicFriction = std::clamp(obj.collider.dynamicFriction, 0.0f, 4.0f);
    const float restitution = std::clamp(obj.collider.restitution, 0.0f, 1.0f);
    PxMaterial* material = physics->createMaterial(staticFriction, dynamicFriction, restitution);
    if (!material) {
        return fallback;
    }
    outOwned = true;
    return material;
}

bool BuildTriangleMeshFromVertexStream(const std::vector<float>& source,
                                       std::vector<PxVec3>& vertices,
                                       std::vector<uint32_t>& indices) {
    constexpr size_t kStrideFloats = 8;
    if (source.size() < kStrideFloats * 3 || (source.size() % kStrideFloats) != 0) {
        return false;
    }

    const size_t vertexCount = source.size() / kStrideFloats;
    vertices.reserve(vertexCount);
    indices.resize(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        const size_t base = i * kStrideFloats;
        vertices.emplace_back(source[base + 0], source[base + 1], source[base + 2]);
        indices[i] = static_cast<uint32_t>(i);
    }

    return !vertices.empty() && (indices.size() % 3 == 0);
}

void BakeScaleIntoVertices(std::vector<PxVec3>& vertices, const glm::vec3& scale) {
    const PxVec3 bakedScale(std::abs(scale.x), std::abs(scale.y), std::abs(scale.z));
    for (PxVec3& v : vertices) {
        v.x *= bakedScale.x;
        v.y *= bakedScale.y;
        v.z *= bakedScale.z;
    }
}

void BakeTransformIntoVertices(std::vector<PxVec3>& vertices, const SceneObject& obj) {
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, obj.position);
    transform = glm::rotate(transform, glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    transform = glm::translate(transform, obj.collider.offset);
    transform = glm::scale(transform, obj.scale);

    for (PxVec3& v : vertices) {
        glm::vec4 world = transform * glm::vec4(v.x, v.y, v.z, 1.0f);
        v = PxVec3(world.x, world.y, world.z);
    }
}

glm::mat4 BuildColliderVertexTransform(const SceneObject& obj) {
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, obj.position);
    transform = glm::rotate(transform, glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    transform = glm::translate(transform, obj.collider.offset);
    transform = glm::scale(transform, obj.scale);
    return transform;
}

template <typename VertexContainer>
void ComputeColliderBounds(const VertexContainer& vertices,
                           const SceneObject& obj,
                           bool bakeWorldSpaceStaticMesh,
                           glm::vec3& boundsMin,
                           glm::vec3& boundsMax) {
    boundsMin = glm::vec3(FLT_MAX);
    boundsMax = glm::vec3(-FLT_MAX);

    const glm::mat4 transform = bakeWorldSpaceStaticMesh ? BuildColliderVertexTransform(obj) : glm::mat4(1.0f);
    for (const auto& vertex : vertices) {
        glm::vec3 p = ToGlmVec3(vertex);
        if (bakeWorldSpaceStaticMesh) {
            glm::vec4 world = transform * glm::vec4(p, 1.0f);
            p = glm::vec3(world);
        } else {
            p *= obj.scale;
        }
        boundsMin = glm::min(boundsMin, p);
        boundsMax = glm::max(boundsMax, p);
    }
}

const std::vector<float>* GetPrimitiveTriangleStream(RenderType type) {
    static const std::vector<float> cubeVerts(std::begin(vertices), std::end(vertices));
    static const std::vector<float> planeVerts(std::begin(mirrorPlaneVertices), std::end(mirrorPlaneVertices));
    static const std::vector<float> sphereVerts = generateSphere();
    static const std::vector<float> capsuleVerts = generateCapsule();
    static const std::vector<float> torusVerts = generateTorus();

    switch (type) {
        case RenderType::Cube: return &cubeVerts;
        case RenderType::Sphere: return &sphereVerts;
        case RenderType::Capsule: return &capsuleVerts;
        case RenderType::Plane: return &planeVerts;
        case RenderType::Torus: return &torusVerts;
        default: return nullptr;
    }
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
    mCookParams.meshWeldTolerance = std::max(0.0001f, 0.001f * scale.length);

    PxSceneDesc sceneDesc(mPhysics->getTolerancesScale());
    sceneDesc.gravity = SceneGravity(mProjectSettings);
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

void PhysicsSystem::setProjectSettings(const ProjectPhysicsSettings& settings) {
    mProjectSettings = settings;
    applySceneGravity();
}

void PhysicsSystem::applySceneGravity() {
    if (!mScene) return;
    mScene->setGravity(SceneGravity(mProjectSettings));
}

bool PhysicsSystem::gatherMeshData(const SceneObject& obj, std::vector<PxVec3>& vertices, std::vector<uint32_t>& indices) const {
    auto buildFromPrimitiveStream = [&]() {
        const std::vector<float>* sourceVertices = GetPrimitiveTriangleStream(obj.renderType);
        if (!sourceVertices) {
            return false;
        }
        vertices.clear();
        indices.clear();
        return BuildTriangleMeshFromVertexStream(*sourceVertices, vertices, indices);
    };

    const OBJLoader::LoadedMesh* meshInfo = nullptr;
    if (obj.hasRenderer && obj.renderType == RenderType::OBJMesh && obj.meshId >= 0) {
        meshInfo = g_objLoader.getMeshInfo(obj.meshId);
    } else if (obj.hasRenderer && obj.renderType == RenderType::Model && obj.meshId >= 0) {
        meshInfo = getModelLoader().getMeshInfo(obj.meshId);
    }
    if (!meshInfo) {
        return buildFromPrimitiveStream();
    }

    if (!meshInfo->positions.empty() && meshInfo->triangleIndices.size() >= 3) {
        vertices.reserve(meshInfo->positions.size());
        indices.reserve(meshInfo->triangleIndices.size());
        for (const auto& v : meshInfo->positions) {
            vertices.emplace_back(v.x, v.y, v.z);
        }
        indices.insert(indices.end(), meshInfo->triangleIndices.begin(), meshInfo->triangleIndices.end());
        return !vertices.empty() && (indices.size() % 3 == 0);
    }

    if (meshInfo->triangleVertices.empty()) {
        return false;
    }

    vertices.reserve(meshInfo->triangleVertices.size());
    indices.resize(meshInfo->triangleVertices.size());

    for (size_t i = 0; i < meshInfo->triangleVertices.size(); ++i) {
        const glm::vec3& v = meshInfo->triangleVertices[i];
        vertices.emplace_back(v.x, v.y, v.z);
        indices[i] = static_cast<uint32_t>(i);
    }

    if (!vertices.empty() && (indices.size() % 3 == 0)) {
        return true;
    }

    return buildFromPrimitiveStream();
}

PxTriangleMesh* PhysicsSystem::cookTriangleMesh(const std::vector<PxVec3>& vertices,
                                                const std::vector<uint32_t>& indices) const {
    if (vertices.empty() || indices.size() < 3) return nullptr;

    try {
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
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

PxConvexMesh* PhysicsSystem::cookConvexMesh(const std::vector<PxVec3>& vertices) const {
    if (vertices.size() < 4) return nullptr;

    try {
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
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

bool PhysicsSystem::attachPrimitiveShape(PxRigidActor* actor, const SceneObject& obj, bool isDynamic) const {
    (void)isDynamic;
    if (!actor) return false;
    bool ownsMaterial = false;
    PxMaterial* shapeMaterial = CreateShapeMaterial(mPhysics, mDefaultMaterial, obj, ownsMaterial);
    PxShape* shape = nullptr;
    auto tuneShape = [](PxShape* s, float minDim, bool /*swept*/) {
        if (!s) return;
        float contact = std::clamp(minDim * 0.005f, 0.0005f, 0.01f);
        s->setContactOffset(contact);
        s->setRestOffset(0.0f);
    };

    switch (obj.renderType) {
        case RenderType::Cube: {
            PxVec3 halfExtents = ToPxVec3(glm::max(obj.scale * 0.5f, glm::vec3(0.01f)));
            shape = mPhysics->createShape(PxBoxGeometry(halfExtents), *shapeMaterial, true);
            tuneShape(shape, std::min({halfExtents.x, halfExtents.y, halfExtents.z}) * 2.0f, isDynamic);
            break;
        }
        case RenderType::Sphere: {
            float radius = std::max({obj.scale.x, obj.scale.y, obj.scale.z}) * 0.5f;
            radius = std::max(radius, 0.01f);
            shape = mPhysics->createShape(PxSphereGeometry(radius), *shapeMaterial, true);
            tuneShape(shape, radius * 2.0f, isDynamic);
            break;
        }
        case RenderType::Capsule: {
        float radius = std::max(obj.scale.x, obj.scale.z) * 0.5f;
        radius = std::max(radius, 0.01f);
        float cylHeight = std::max(0.05f, obj.scale.y - radius * 2.0f);
        float halfHeight = cylHeight * 0.5f;
        shape = mPhysics->createShape(PxCapsuleGeometry(radius, halfHeight), *shapeMaterial, true);
        if (shape) {
            // PhysX capsules default to the X axis; rotate to align with Y (character up)
            shape->setLocalPose(PxTransform(PxQuat(PxHalfPi, PxVec3(0, 0, 1))));
        }
        tuneShape(shape, std::min(radius * 2.0f, halfHeight * 2.0f), isDynamic);
        break;
    }
        case RenderType::Plane: {
            glm::vec3 halfExtents = glm::max(obj.scale * 0.5f, glm::vec3(0.01f));
            halfExtents.z = std::max(halfExtents.z, 0.01f);
            shape = mPhysics->createShape(PxBoxGeometry(ToPxVec3(halfExtents)), *shapeMaterial, true);
            tuneShape(shape, std::min({halfExtents.x, halfExtents.y, halfExtents.z}) * 2.0f, isDynamic);
            break;
        }
        case RenderType::Sprite: {
            glm::vec3 halfExtents = glm::max(obj.scale * 0.5f, glm::vec3(0.01f));
            halfExtents.z = std::max(halfExtents.z, 0.01f);
            shape = mPhysics->createShape(PxBoxGeometry(ToPxVec3(halfExtents)), *shapeMaterial, true);
            tuneShape(shape, std::min({halfExtents.x, halfExtents.y, halfExtents.z}) * 2.0f, isDynamic);
            break;
        }
        case RenderType::Torus: {
            float radius = std::max({obj.scale.x, obj.scale.y, obj.scale.z}) * 0.5f;
            radius = std::max(radius, 0.01f);
            shape = mPhysics->createShape(PxSphereGeometry(radius), *shapeMaterial, true);
            tuneShape(shape, radius * 2.0f, isDynamic);
            break;
        }
        default:
            break;
    }

    if (ownsMaterial && shapeMaterial) {
        shapeMaterial->release();
    }
    if (!shape) return false;
    actor->attachShape(*shape);
    shape->release();
    return true;
}

bool PhysicsSystem::attachColliderShape(PxRigidActor* actor, const SceneObject& obj, bool isDynamic,
                                        bool bakeWorldSpaceStaticMesh) const {
    if (!actor || !obj.hasCollider || !obj.collider.enabled) return false;

    bool ownsMaterial = false;
    PxMaterial* shapeMaterial = CreateShapeMaterial(mPhysics, mDefaultMaterial, obj, ownsMaterial);
    PxShape* shape = nullptr;
    const PxVec3 localOffset = ToPxVec3(obj.collider.offset);
    auto tuneShape = [](PxShape* s, float minDim, bool /*swept*/) {
        if (!s) return;
        float contact = std::clamp(minDim * 0.005f, 0.0005f, 0.01f);
        s->setContactOffset(contact);
        s->setRestOffset(0.0f);
    };
    float minDim = 0.1f;
    if (obj.collider.type == ColliderType::Box) {
        glm::vec3 half = glm::max(obj.collider.boxSize * 0.5f, glm::vec3(0.01f));
        shape = mPhysics->createShape(PxBoxGeometry(ToPxVec3(half)), *shapeMaterial, true);
        if (shape) {
            shape->setLocalPose(PxTransform(localOffset, PxQuat(PxIdentity)));
        }
        minDim = std::min({half.x, half.y, half.z}) * 2.0f;
    } else if (obj.collider.type == ColliderType::Capsule) {
        float radius = std::max({obj.collider.boxSize.x, obj.collider.boxSize.z}) * 0.5f;
        radius = std::max(radius, 0.01f);
        float cylHeight = std::max(0.05f, obj.collider.boxSize.y - radius * 2.0f);
        float halfHeight = cylHeight * 0.5f;
        shape = mPhysics->createShape(PxCapsuleGeometry(radius, halfHeight), *shapeMaterial, true);
        if (shape) {
            // Rotate capsule so its axis matches the engine's Y-up expectation
            shape->setLocalPose(PxTransform(localOffset, PxQuat(PxHalfPi, PxVec3(0, 0, 1))));
        }
        minDim = std::min(radius * 2.0f, halfHeight * 2.0f);
    } else {
        const OBJLoader::LoadedMesh* meshInfo = nullptr;
        if (obj.hasRenderer && obj.renderType == RenderType::OBJMesh && obj.meshId >= 0) {
            meshInfo = g_objLoader.getMeshInfo(obj.meshId);
        } else if (obj.hasRenderer && obj.renderType == RenderType::Model && obj.meshId >= 0) {
            meshInfo = getModelLoader().getMeshInfo(obj.meshId);
        }
        const bool hasIndexed = meshInfo && !meshInfo->positions.empty() && meshInfo->triangleIndices.size() >= 3;
        const bool hasTriVerts = meshInfo && !meshInfo->triangleVertices.empty();
        if (meshInfo && !hasIndexed && !hasTriVerts) {
            return false;
        }

        auto makeBoundsShape = [&](const glm::vec3& boundsMin, const glm::vec3& boundsMax) {
            glm::vec3 halfExtents = glm::max((boundsMax - boundsMin) * 0.5f, glm::vec3(0.01f));
            glm::vec3 center = (boundsMax + boundsMin) * 0.5f;
            PxShape* box = mPhysics->createShape(PxBoxGeometry(ToPxVec3(halfExtents)), *shapeMaterial, true);
            if (box) {
                const PxVec3 poseCenter = bakeWorldSpaceStaticMesh ? ToPxVec3(center)
                                                                   : ToPxVec3(center) + localOffset;
                box->setLocalPose(PxTransform(poseCenter, PxQuat(PxIdentity)));
            }
            return box;
        };

        constexpr size_t kMaxCookVertices = 1000000;
        size_t cookVertices = 0;
        if (meshInfo) {
            cookVertices = hasIndexed ? meshInfo->positions.size() : meshInfo->triangleVertices.size();
        }
        if (cookVertices > kMaxCookVertices) {
            glm::vec3 boundsMin;
            glm::vec3 boundsMax;
            if (hasIndexed) {
                ComputeColliderBounds(meshInfo->positions, obj, bakeWorldSpaceStaticMesh, boundsMin, boundsMax);
            } else {
                ComputeColliderBounds(meshInfo->triangleVertices, obj, bakeWorldSpaceStaticMesh, boundsMin, boundsMax);
            }
            minDim = std::max(0.01f, std::min({boundsMax.x - boundsMin.x, boundsMax.y - boundsMin.y, boundsMax.z - boundsMin.z}));
            shape = makeBoundsShape(boundsMin, boundsMax);
        } else {
            std::vector<PxVec3> verts;
            std::vector<uint32_t> indices;
            bool hasMeshData = false;
            try {
                hasMeshData = gatherMeshData(obj, verts, indices);
            } catch (const std::bad_alloc&) {
                hasMeshData = false;
            }

            if (!hasMeshData) {
                if (!meshInfo) {
                    return false;
                }
                glm::vec3 boundsMin;
                glm::vec3 boundsMax;
                if (hasIndexed) {
                    ComputeColliderBounds(meshInfo->positions, obj, bakeWorldSpaceStaticMesh, boundsMin, boundsMax);
                } else {
                    ComputeColliderBounds(meshInfo->triangleVertices, obj, bakeWorldSpaceStaticMesh, boundsMin, boundsMax);
                }
                minDim = std::max(0.01f, std::min({boundsMax.x - boundsMin.x, boundsMax.y - boundsMin.y, boundsMax.z - boundsMin.z}));
                shape = makeBoundsShape(boundsMin, boundsMax);
            } else {
                bool useConvex = isDynamic || obj.collider.type == ColliderType::ConvexMesh;
                if (bakeWorldSpaceStaticMesh) {
                    BakeTransformIntoVertices(verts, obj);
                } else {
                    BakeScaleIntoVertices(verts, obj.scale);
                }
                glm::vec3 boundsMin;
                glm::vec3 boundsMax;
                ComputeColliderBounds(verts, obj, false, boundsMin, boundsMax);
                minDim = std::max(0.01f, std::min({boundsMax.x - boundsMin.x, boundsMax.y - boundsMin.y, boundsMax.z - boundsMin.z}));
                if (useConvex) {
                    PxConvexMesh* convex = cookConvexMesh(verts);
                    if (convex) {
                        PxConvexMeshGeometry geom(convex);
                        shape = mPhysics->createShape(geom, *shapeMaterial, true);
                        if (shape) {
                            shape->setLocalPose(PxTransform(bakeWorldSpaceStaticMesh ? PxVec3(0.0f) : localOffset,
                                                            PxQuat(PxIdentity)));
                        }
                        convex->release();
                    }
                } else {
                    PxTriangleMesh* tri = cookTriangleMesh(verts, indices);
                    if (tri) {
                        PxTriangleMeshGeometry geom(tri);
                        shape = mPhysics->createShape(geom, *shapeMaterial, true);
                        if (shape) {
                            shape->setLocalPose(PxTransform(bakeWorldSpaceStaticMesh ? PxVec3(0.0f) : localOffset,
                                                            PxQuat(PxIdentity)));
                        }
                        tri->release();
                    }
                }

                if (!shape) {
                    shape = makeBoundsShape(boundsMin, boundsMax);
                }
            }
        }
    }

    tuneShape(shape, std::max(0.01f, minDim), isDynamic || obj.hasPlayerController);

    if (ownsMaterial && shapeMaterial) {
        shapeMaterial->release();
    }
    if (!shape) return false;
    actor->attachShape(*shape);
    shape->release();
    return true;
}

PhysicsSystem::ActorRecord PhysicsSystem::createActorFor(const SceneObject& obj) const {
    ActorRecord record;

    const bool wantsDynamic = obj.hasRigidbody && obj.rigidbody.enabled;
    const bool wantsCollider = obj.hasCollider && obj.collider.enabled;
    const bool bakeWorldSpaceStaticMesh =
        !wantsDynamic && wantsCollider && obj.collider.type == ColliderType::Mesh;
    if (!wantsDynamic && !wantsCollider) {
        return record;
    }

    PxTransform transform = bakeWorldSpaceStaticMesh
        ? PxTransform(PxIdentity)
        : PxTransform(ToPxVec3(obj.position), ToPxQuat(obj.rotation));

    PxRigidActor* actor = wantsDynamic
        ? static_cast<PxRigidActor*>(mPhysics->createRigidDynamic(transform))
        : static_cast<PxRigidActor*>(mPhysics->createRigidStatic(transform));

    if (!actor) return record;

    record.actor = actor;
    record.isDynamic = wantsDynamic;
    record.isKinematic = wantsDynamic && obj.rigidbody.isKinematic;
    record.hasWorldBakedStaticMesh = bakeWorldSpaceStaticMesh;

    bool attached = false;
    if (wantsCollider) {
        attached = attachColliderShape(actor, obj, wantsDynamic, bakeWorldSpaceStaticMesh);
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
        PxRigidDynamicLockFlags lockFlags = BuildAngularLockFlags(obj);
        dyn->setRigidDynamicLockFlags(lockFlags);
        if (obj.hasPlayerController) {
            dyn->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
            dyn->setMaxDepenetrationVelocity(1.5f);
        }
        if (!obj.rigidbody.isKinematic) {
            const float massKg = EffectiveMassKilograms(obj, mProjectSettings);
            const PxVec3 com = ToPxVec3(obj.rigidbody.centerOfMass);
            const PxVec3* comPtr = obj.rigidbody.useCustomCenterOfMass ? &com : nullptr;
            PxRigidBodyExt::setMassAndUpdateInertia(*dyn, massKg, comPtr);
            record.massKg = massKg;
            record.useCustomCenterOfMass = obj.rigidbody.useCustomCenterOfMass;
            record.centerOfMass = obj.rigidbody.centerOfMass;
        }
        record.gravityDisabled = !obj.rigidbody.useGravity;
        record.linearDamping = obj.rigidbody.linearDamping;
        record.angularDamping = obj.rigidbody.angularDamping;
        record.angularLockMask = BuildAngularLockMask(obj);
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
}

void PhysicsSystem::onPlayStart(const std::vector<SceneObject>& objects) {
    if (!isReady()) return;

    clearActors();

    struct MeshCookInfo {
        std::string name;
        size_t vertices = 0;
        size_t triangles = 0;
        size_t duplicateVertices = 0;
    };
    std::vector<MeshCookInfo> cookInfos;
    cookInfos.reserve(objects.size());

    for (const auto& obj : objects) {
        if (!IsObjectEnabledInHierarchy(obj) || !obj.hasCollider || !obj.collider.enabled) continue;
        if (obj.collider.type == ColliderType::Box || obj.collider.type == ColliderType::Capsule) continue;
        const OBJLoader::LoadedMesh* meshInfo = nullptr;
        if (obj.hasRenderer && obj.renderType == RenderType::OBJMesh && obj.meshId >= 0) {
            meshInfo = g_objLoader.getMeshInfo(obj.meshId);
        } else if (obj.hasRenderer && obj.renderType == RenderType::Model && obj.meshId >= 0) {
            meshInfo = getModelLoader().getMeshInfo(obj.meshId);
        }
        if (!meshInfo) continue;
        const bool hasIndexed = !meshInfo->positions.empty() && meshInfo->triangleIndices.size() >= 3;
        const bool hasTriVerts = !meshInfo->triangleVertices.empty();
        if (!hasIndexed && !hasTriVerts) continue;
        MeshCookInfo info;
        info.name = obj.name;
        info.vertices = hasIndexed ? meshInfo->positions.size() : meshInfo->triangleVertices.size();
        info.triangles = hasIndexed ? (meshInfo->triangleIndices.size() / 3) : (meshInfo->triangleVertices.size() / 3);
        info.duplicateVertices = meshInfo->triangleVertices.size();
        cookInfos.push_back(info);
    }

    if (!cookInfos.empty()) {
        std::sort(cookInfos.begin(), cookInfos.end(),
                  [](const MeshCookInfo& a, const MeshCookInfo& b) { return a.vertices > b.vertices; });
        size_t reportCount = std::min<size_t>(cookInfos.size(), 5);
        std::cerr << "[Physics] Mesh collider stats (top " << reportCount << " by vertex count):\n";
        for (size_t i = 0; i < reportCount; ++i) {
            const auto& info = cookInfos[i];
            std::cerr << "  " << info.name
                      << " verts=" << info.vertices
                      << " tris=" << info.triangles
                      << " dupVerts=" << info.duplicateVertices
                      << "\n";
        }
    }

    for (const auto& obj : objects) {
        if (!IsObjectEnabledInHierarchy(obj)) continue;
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
                                   int ignoreId, glm::vec3* hitPos, glm::vec3* hitNormal, float* hitDistance,
                                   int* hitActorId, glm::vec3* hitActorVelocity,
                                   float* hitStaticFriction, float* hitDynamicFriction) const {
#ifdef MODULARITY_ENABLE_PHYSX
    if (!isReady() || distance <= 0.0f) return false;
    if (hitActorId) *hitActorId = -1;
    if (hitActorVelocity) *hitActorVelocity = glm::vec3(0.0f);
    if (hitStaticFriction) *hitStaticFriction = 0.9f;
    if (hitDynamicFriction) *hitDynamicFriction = 0.9f;
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
    if (hitStaticFriction || hitDynamicFriction) {
        PxShape* shape = hit.block.shape;
        if (shape) {
            PxMaterial* materials[1] = {nullptr};
            PxU16 count = shape->getMaterials(materials, 1);
            if (count > 0 && materials[0]) {
                if (hitStaticFriction) *hitStaticFriction = materials[0]->getStaticFriction();
                if (hitDynamicFriction) *hitDynamicFriction = materials[0]->getDynamicFriction();
            }
        }
    }
    if (hitActorId || hitActorVelocity) {
        PxRigidActor* hitActor = hit.block.actor;
        if (hitActor) {
            for (const auto& [id, rec] : mActors) {
                if (rec.actor != hitActor) continue;
                if (hitActorId) *hitActorId = id;
                if (hitActorVelocity && rec.isDynamic) {
                    if (const PxRigidDynamic* dyn = rec.actor->is<PxRigidDynamic>()) {
                        *hitActorVelocity = ToGlmVec3(dyn->getLinearVelocity());
                    }
                }
                break;
            }
        }
    }
    return true;
#else
    (void)origin; (void)dir; (void)distance; (void)ignoreId; (void)hitPos; (void)hitNormal; (void)hitDistance;
    (void)hitActorId; (void)hitActorVelocity; (void)hitStaticFriction; (void)hitDynamicFriction;
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
        if (!IsObjectEnabledInHierarchy(*it)) {
            rec.actor->setActorFlag(PxActorFlag::eDISABLE_SIMULATION, true);
            continue;
        } else {
            rec.actor->setActorFlag(PxActorFlag::eDISABLE_SIMULATION, false);
        }
        if (PxRigidDynamic* dyn = rec.actor->is<PxRigidDynamic>()) {
            const bool isKinematic = it->rigidbody.isKinematic;
            if (rec.isKinematic != isKinematic) {
                dyn->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, isKinematic);
                rec.isKinematic = isKinematic;
            }

            const bool gravityDisabled = !it->rigidbody.useGravity;
            if (rec.gravityDisabled != gravityDisabled) {
                dyn->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, gravityDisabled);
                rec.gravityDisabled = gravityDisabled;
            }

            if (std::abs(rec.linearDamping - it->rigidbody.linearDamping) > 0.0001f) {
                dyn->setLinearDamping(it->rigidbody.linearDamping);
                rec.linearDamping = it->rigidbody.linearDamping;
            }
            if (std::abs(rec.angularDamping - it->rigidbody.angularDamping) > 0.0001f) {
                dyn->setAngularDamping(it->rigidbody.angularDamping);
                rec.angularDamping = it->rigidbody.angularDamping;
            }

            const uint32_t angularLockMask = BuildAngularLockMask(*it);
            if (rec.angularLockMask != angularLockMask) {
                dyn->setRigidDynamicLockFlags(BuildAngularLockFlags(*it));
                rec.angularLockMask = angularLockMask;
            }

            const float massKg = EffectiveMassKilograms(*it, mProjectSettings);
            const bool useCustomCenterOfMass = it->rigidbody.useCustomCenterOfMass;
            const glm::vec3 centerOfMassDelta = rec.centerOfMass - it->rigidbody.centerOfMass;
            const bool centerOfMassChanged = glm::dot(centerOfMassDelta, centerOfMassDelta) > 0.000001f;
            const bool massChanged = std::abs(rec.massKg - massKg) > 0.0001f;
            const bool customCenterStateChanged = rec.useCustomCenterOfMass != useCustomCenterOfMass;
            if (!isKinematic && (massChanged || centerOfMassChanged || customCenterStateChanged)) {
                const PxVec3 com = ToPxVec3(it->rigidbody.centerOfMass);
                const PxVec3* comPtr = useCustomCenterOfMass ? &com : nullptr;
                PxRigidBodyExt::setMassAndUpdateInertia(*dyn, massKg, comPtr);
                rec.massKg = massKg;
                rec.useCustomCenterOfMass = useCustomCenterOfMass;
                rec.centerOfMass = it->rigidbody.centerOfMass;
            }

            if (dyn->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC)) {
                dyn->setKinematicTarget(PxTransform(ToPxVec3(it->position), ToPxQuat(it->rotation)));
            }
        } else {
            if (rec.hasWorldBakedStaticMesh) {
                continue;
            }
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
        if (it == objects.end() || !IsObjectEnabledInHierarchy(*it)) continue;

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
void PhysicsSystem::setProjectSettings(const ProjectPhysicsSettings&) {}
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
