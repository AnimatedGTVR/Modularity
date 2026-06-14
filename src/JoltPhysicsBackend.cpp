#include "JoltPhysicsBackend.h"
#ifdef MODULARITY_ENABLE_JOLT
#include "ModelLoader.h"
#include "Rendering.h"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Body/AllowedDOFs.h>
#include <Jolt/Physics/Body/MotionQuality.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/PhysicsMaterial.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <iostream>
#include <new>

// Jolt initializes its global Factory + default types lazily; multiple
// JoltPhysicsBackend instances in the same process (unlikely but possible
// during editor scene reloads) must not register twice or shut down the
// factory while another instance is alive. Refcounted.
namespace {
    int g_joltGlobalsRefcount = 0;

    void EnsureJoltGlobals() {
        if (g_joltGlobalsRefcount++ != 0) return;
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }

    void ReleaseJoltGlobals() {
        if (--g_joltGlobalsRefcount != 0) return;
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }

    // Jolt's optional asserts/trace are wired to engine logging only when
    // built with JPH_ENABLE_ASSERTS / JPH_PROFILE_ENABLED. The vendored
    // build has them off in release, so this is a no-op normally.
}

namespace JoltLayers {
    constexpr JPH::ObjectLayer NON_MOVING = 0;
    constexpr JPH::ObjectLayer MOVING = 1;
    constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

namespace JoltBPLayers {
    constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    constexpr JPH::BroadPhaseLayer MOVING(1);
    constexpr JPH::uint NUM_LAYERS = 2;
}

// Two broad-phase layers (static, dynamic). Static objects do not collide
// with each other in the broad phase, which is the standard pattern Jolt's
// HelloWorld documents and matches the engine's existing behaviour where
// static-vs-static interaction is irrelevant.
class JoltBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    JoltBroadPhaseLayerInterface() {
        mObjectToBroadPhase[JoltLayers::NON_MOVING] = JoltBPLayers::NON_MOVING;
        mObjectToBroadPhase[JoltLayers::MOVING] = JoltBPLayers::MOVING;
    }
    JPH::uint GetNumBroadPhaseLayers() const override { return JoltBPLayers::NUM_LAYERS; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        return mObjectToBroadPhase[inLayer];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        // JPH::BroadPhaseLayer::GetValue() isn't constexpr in Jolt 5.5, so
        // a switch on these constants doesn't compile. if/else is fine here,
        // this is only ever called by the profiler.
        if (inLayer == JoltBPLayers::NON_MOVING) return "NON_MOVING";
        if (inLayer == JoltBPLayers::MOVING) return "MOVING";
        return "INVALID";
    }
#endif
private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[JoltLayers::NUM_LAYERS];
};

class JoltObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
            case JoltLayers::NON_MOVING: return inLayer2 == JoltBPLayers::MOVING;
            case JoltLayers::MOVING: return true;
            default: return false;
        }
    }
};

class JoltObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        switch (a) {
            case JoltLayers::NON_MOVING: return b == JoltLayers::MOVING;
            case JoltLayers::MOVING: return true;
            default: return false;
        }
    }
};

// Bundled together so the header only needs an opaque forward declaration.
struct JoltPhysicsBackend::JoltFilters {
    JoltBroadPhaseLayerInterface bpInterface;
    JoltObjectVsBroadPhaseLayerFilter objVsBpFilter;
    JoltObjectLayerPairFilter objLayerPairFilter;
};

namespace {
    using namespace JPH;

    Vec3 ToJVec3(const glm::vec3& v) { return Vec3(v.x, v.y, v.z); }
    glm::vec3 FromJVec3(Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }

    Quat ToJQuat(const glm::vec3& eulerDeg) {
        glm::vec3 r = glm::radians(eulerDeg);
        glm::mat4 m(1.0f);
        m = glm::rotate(m, r.x, glm::vec3(1.0f, 0.0f, 0.0f));
        m = glm::rotate(m, r.y, glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::rotate(m, r.z, glm::vec3(0.0f, 0.0f, 1.0f));
        glm::quat q = glm::quat_cast(glm::mat3(m));
        return Quat(q.x, q.y, q.z, q.w);
    }

    glm::vec3 ExtractEulerXYZ(const glm::mat3& m) {
        float T1 = std::atan2(m[2][1], m[2][2]);
        float C2 = std::sqrt(m[0][0] * m[0][0] + m[1][0] * m[1][0]);
        float T2 = std::atan2(-m[2][0], C2);
        float S1 = std::sin(T1);
        float C1 = std::cos(T1);
        float T3 = std::atan2(S1 * m[0][2] - C1 * m[0][1], C1 * m[1][1] - S1 * m[1][2]);
        return glm::vec3(-T1, -T2, -T3);
    }
    glm::vec3 FromJQuatEulerDeg(QuatArg q) {
        glm::quat gq(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
        glm::mat3 m = glm::mat3_cast(gq);
        return glm::degrees(ExtractEulerXYZ(m));
    }

    float EffectiveMassKilograms(const SceneObject& obj, const ProjectPhysicsSettings& s) {
        const float authoredMass = std::max(0.0001f, obj.rigidbody.mass);
        const float unitScale = std::max(0.000001f, ProjectMassUnitToKilograms(s.massUnit));
        return std::max(0.01f, authoredMass * unitScale);
    }

    Vec3 SceneGravity(const ProjectPhysicsSettings& s) {
        return Vec3(0.0f, -9.81f * std::max(0.0f, s.globalGravityScale), 0.0f);
    }

    uint32_t BuildAngularLockMask(const SceneObject& obj) {
        uint32_t mask = 0;
        if (obj.rigidbody.lockRotationX) mask |= 1u << 0u;
        if (obj.rigidbody.lockRotationY) mask |= 1u << 1u;
        if (obj.rigidbody.lockRotationZ) mask |= 1u << 2u;
        return mask;
    }

    EAllowedDOFs BuildAllowedDOFs(const SceneObject& obj) {
        // Default all 6 DOFs, then strip the locked rotation axes.
        EAllowedDOFs dofs = EAllowedDOFs::All;
        if (obj.rigidbody.lockRotationX) dofs &= ~EAllowedDOFs::RotationX;
        if (obj.rigidbody.lockRotationY) dofs &= ~EAllowedDOFs::RotationY;
        if (obj.rigidbody.lockRotationZ) dofs &= ~EAllowedDOFs::RotationZ;
        return dofs;
    }

    bool PoseChangedSignificantly(const glm::vec3& prevPos, const glm::vec3& prevRot,
                                  const glm::vec3& nextPos, const glm::vec3& nextRot) {
        const glm::vec3 dp = nextPos - prevPos;
        const glm::vec3 dr = nextRot - prevRot;
        return glm::dot(dp, dp) > 1e-8f || glm::dot(dr, dr) > 1e-8f;
    }

    // === Mesh gathering ====================================================
    // Mirrors the helpers in PhysicsSystem.cpp so Jolt can build hull /
    // mesh shapes from the same source data.

    void BakeScaleIntoVertices(std::vector<Float3>& vertices, const glm::vec3& scale) {
        const glm::vec3 abs = glm::abs(scale);
        for (Float3& v : vertices) { v.x *= abs.x; v.y *= abs.y; v.z *= abs.z; }
    }

    glm::mat4 BuildColliderVertexTransform(const SceneObject& obj) {
        glm::mat4 t(1.0f);
        t = glm::translate(t, obj.position);
        t = glm::rotate(t, glm::radians(obj.rotation.x), glm::vec3(1, 0, 0));
        t = glm::rotate(t, glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
        t = glm::rotate(t, glm::radians(obj.rotation.z), glm::vec3(0, 0, 1));
        t = glm::translate(t, obj.collider.offset);
        t = glm::scale(t, obj.scale);
        return t;
    }

    void BakeTransformIntoVertices(std::vector<Float3>& vertices, const SceneObject& obj) {
        const glm::mat4 m = BuildColliderVertexTransform(obj);
        for (Float3& v : vertices) {
            glm::vec4 w = m * glm::vec4(v.x, v.y, v.z, 1.0f);
            v.x = w.x; v.y = w.y; v.z = w.z;
        }
    }

    void ComputeBounds(const std::vector<Float3>& verts, glm::vec3& outMin, glm::vec3& outMax) {
        outMin = glm::vec3(FLT_MAX);
        outMax = glm::vec3(-FLT_MAX);
        for (const Float3& f : verts) {
            outMin = glm::min(outMin, glm::vec3(f.x, f.y, f.z));
            outMax = glm::max(outMax, glm::vec3(f.x, f.y, f.z));
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

    // Returns true and fills verts/tris on success. tris is empty for
    // convex-hull use (the hull builder doesn't need triangles).
    bool GatherMeshData(const SceneObject& obj, std::vector<Float3>& verts, std::vector<IndexedTriangle>& tris) {
        verts.clear();
        tris.clear();
        const OBJLoader::LoadedMesh* meshInfo = nullptr;
        if (obj.hasRenderer && obj.renderType == RenderType::OBJMesh && obj.meshId >= 0) {
            meshInfo = g_objLoader.getMeshInfo(obj.meshId);
        } else if (obj.hasRenderer && obj.renderType == RenderType::Model && obj.meshId >= 0) {
            meshInfo = getModelLoader().getMeshInfo(obj.meshId);
        }

        auto pushFromStream = [&](const std::vector<float>& stream) {
            constexpr size_t kStride = 8; // pos(3) + normal(3) + uv(2)
            if (stream.size() < kStride * 3 || stream.size() % kStride != 0) return false;
            const size_t vCount = stream.size() / kStride;
            verts.reserve(vCount);
            for (size_t i = 0; i < vCount; ++i) {
                const size_t b = i * kStride;
                verts.emplace_back(stream[b + 0], stream[b + 1], stream[b + 2]);
            }
            if (vCount % 3 != 0) return false;
            tris.reserve(vCount / 3);
            for (uint32_t i = 0; i < (uint32_t)vCount; i += 3) {
                tris.emplace_back(i, i + 1, i + 2, 0);
            }
            return true;
        };

        if (meshInfo && !meshInfo->positions.empty() && meshInfo->triangleIndices.size() >= 3) {
            verts.reserve(meshInfo->positions.size());
            for (const auto& p : meshInfo->positions) verts.emplace_back(p.x, p.y, p.z);
            tris.reserve(meshInfo->triangleIndices.size() / 3);
            for (size_t i = 0; i + 2 < meshInfo->triangleIndices.size(); i += 3) {
                tris.emplace_back(meshInfo->triangleIndices[i],
                                  meshInfo->triangleIndices[i + 1],
                                  meshInfo->triangleIndices[i + 2], 0);
            }
            return !verts.empty() && !tris.empty();
        }
        if (meshInfo && !meshInfo->triangleVertices.empty()) {
            const size_t n = meshInfo->triangleVertices.size();
            verts.reserve(n);
            for (const auto& v : meshInfo->triangleVertices) verts.emplace_back(v.x, v.y, v.z);
            if (n % 3 == 0) {
                tris.reserve(n / 3);
                for (uint32_t i = 0; i < (uint32_t)n; i += 3) tris.emplace_back(i, i + 1, i + 2, 0);
                return true;
            }
            verts.clear();
        }
        const std::vector<float>* prim = GetPrimitiveTriangleStream(obj.renderType);
        if (!prim) return false;
        return pushFromStream(*prim);
    }
} // namespace

JoltPhysicsBackend::JoltPhysicsBackend() {
    mFilters = std::make_unique<JoltFilters>();
}

JoltPhysicsBackend::~JoltPhysicsBackend() {
    shutdown();
}

bool JoltPhysicsBackend::init() {
    if (mReady) return true;
    EnsureJoltGlobals();

    mTempAllocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
    mJobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
        std::max<int>(1, (int)std::thread::hardware_concurrency() - 1));

    constexpr JPH::uint cMaxBodies = 65536;
    constexpr JPH::uint cNumBodyMutexes = 0; // 0 = autodetect
    constexpr JPH::uint cMaxBodyPairs = 65536;
    constexpr JPH::uint cMaxContactConstraints = 10240;

    mPhysicsSystem = std::make_unique<JPH::PhysicsSystem>();
    mPhysicsSystem->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
                         mFilters->bpInterface, mFilters->objVsBpFilter, mFilters->objLayerPairFilter);
    mPhysicsSystem->SetGravity(SceneGravity(mProjectSettings));

    mReady = true;
    return true;
}

void JoltPhysicsBackend::shutdown() {
    if (!mReady) return;
    clearBodies();
    mPhysicsSystem.reset();
    mJobSystem.reset();
    mTempAllocator.reset();
    mReady = false;
    ReleaseJoltGlobals();
}

bool JoltPhysicsBackend::isReady() const {
    return mReady;
}

void JoltPhysicsBackend::setProjectSettings(const ProjectPhysicsSettings& settings) {
    mProjectSettings = settings;
    applySceneGravity();
    if (mPhysicsSystem) {
        // Mirror the project's solver iteration counts. Without this, Jolt
        // uses its own defaults (10 velocity / 1 position) regardless of
        // what the user authored in the Physics tab. Matching PhysX
        // behaviour also makes A/B comparisons between the two backends
        // fair instead of comparing different solver tunings.
        JPH::PhysicsSettings sim = mPhysicsSystem->GetPhysicsSettings();
        sim.mNumVelocitySteps = (JPH::uint)std::clamp(settings.solverIterations, 1, 64);
        sim.mNumPositionSteps = std::max(1u, sim.mNumVelocitySteps / 4u);
        mPhysicsSystem->SetPhysicsSettings(sim);
    }
}

void JoltPhysicsBackend::applySceneGravity() {
    if (!mPhysicsSystem) return;
    mPhysicsSystem->SetGravity(SceneGravity(mProjectSettings));
}

namespace {
    using namespace JPH;

    // Build the collision shape for a SceneObject. Returns nullptr on
    // failure (caller should fall back to a box approximating render bounds).
    ShapeRefC BuildShapeForObject(const SceneObject& obj, bool isDynamic, bool bakeWorldSpaceStaticMesh) {
        const bool wantsCollider = obj.hasCollider && obj.collider.enabled;
        const Vec3 localOffset = ToJVec3(obj.collider.offset);

        auto wrapWithOffset = [&](ShapeRefC inner, bool addOffset) -> ShapeRefC {
            if (!addOffset) return inner;
            if (obj.collider.offset == glm::vec3(0.0f)) return inner;
            RotatedTranslatedShapeSettings rts(localOffset, Quat::sIdentity(), inner);
            ShapeSettings::ShapeResult r = rts.Create();
            if (!r.IsValid()) return inner;
            return r.Get();
        };

        // ---- Explicit collider component ----
        if (wantsCollider && obj.collider.type == ColliderType::Box) {
            glm::vec3 half = glm::max(obj.collider.boxSize * 0.5f, glm::vec3(0.01f));
            BoxShapeSettings s(ToJVec3(half));
            s.SetEmbedded();
            ShapeSettings::ShapeResult r = s.Create();
            if (!r.IsValid()) return ShapeRefC();
            return wrapWithOffset(r.Get(), true);
        }
        if (wantsCollider && obj.collider.type == ColliderType::Capsule) {
            float radius = std::max({obj.collider.boxSize.x, obj.collider.boxSize.z}) * 0.5f;
            radius = std::max(radius, 0.01f);
            float cylHeight = std::max(0.05f, obj.collider.boxSize.y - radius * 2.0f);
            CapsuleShapeSettings s(cylHeight * 0.5f, radius); // Jolt capsules are Y-axis
            s.SetEmbedded();
            ShapeSettings::ShapeResult r = s.Create();
            if (!r.IsValid()) return ShapeRefC();
            return wrapWithOffset(r.Get(), true);
        }

        // ---- Mesh / convex collider ----
        if (wantsCollider && (obj.collider.type == ColliderType::Mesh || obj.collider.type == ColliderType::ConvexMesh)) {
            std::vector<Float3> verts;
            std::vector<IndexedTriangle> tris;
            bool ok = false;
            try { ok = GatherMeshData(obj, verts, tris); }
            catch (const std::bad_alloc&) { ok = false; }

            auto fallbackBoxFromBounds = [&](const std::vector<Float3>& v) -> ShapeRefC {
                glm::vec3 bmin, bmax;
                ComputeBounds(v, bmin, bmax);
                glm::vec3 half = glm::max((bmax - bmin) * 0.5f, glm::vec3(0.01f));
                glm::vec3 center = (bmax + bmin) * 0.5f;
                BoxShapeSettings bs(ToJVec3(half));
                bs.SetEmbedded();
                ShapeSettings::ShapeResult r = bs.Create();
                if (!r.IsValid()) return ShapeRefC();
                RotatedTranslatedShapeSettings rts(ToJVec3(bakeWorldSpaceStaticMesh ? center : center + glm::vec3(obj.collider.offset)),
                                                   Quat::sIdentity(), r.Get());
                ShapeSettings::ShapeResult r2 = rts.Create();
                return r2.IsValid() ? r2.Get() : r.Get();
            };

            if (!ok || verts.empty()) return ShapeRefC();

            constexpr size_t kMaxCookVerts = 1000000;
            if (verts.size() > kMaxCookVerts) return fallbackBoxFromBounds(verts);

            if (bakeWorldSpaceStaticMesh) BakeTransformIntoVertices(verts, obj);
            else BakeScaleIntoVertices(verts, obj.scale);

            const bool useConvex = isDynamic || obj.collider.type == ColliderType::ConvexMesh;
            if (useConvex) {
                Array<Vec3> hullPoints;
                hullPoints.reserve(verts.size());
                for (const Float3& f : verts) hullPoints.push_back(Vec3(f.x, f.y, f.z));
                ConvexHullShapeSettings s(hullPoints, 0.0f);
                s.SetEmbedded();
                ShapeSettings::ShapeResult r = s.Create();
                if (!r.IsValid()) return fallbackBoxFromBounds(verts);
                return wrapWithOffset(r.Get(), !bakeWorldSpaceStaticMesh);
            } else {
                // Static triangle mesh. Jolt expects its own Array type
                // (not std::vector) and consumes the buffers by value.
                VertexList jVerts;
                jVerts.reserve(verts.size());
                for (const Float3& f : verts) jVerts.push_back(f);
                IndexedTriangleList jTris;
                jTris.reserve(tris.size());
                for (const IndexedTriangle& t : tris) jTris.push_back(t);
                MeshShapeSettings s(std::move(jVerts), std::move(jTris));
                s.SetEmbedded();
                ShapeSettings::ShapeResult r = s.Create();
                if (!r.IsValid()) return ShapeRefC();
                return wrapWithOffset(r.Get(), !bakeWorldSpaceStaticMesh);
            }
        }

        // ---- No collider component: derive from render primitive ----
        switch (obj.renderType) {
            case RenderType::Cube:
            case RenderType::Plane:
            case RenderType::Sprite: {
                glm::vec3 half = glm::max(obj.scale * 0.5f, glm::vec3(0.01f));
                BoxShapeSettings s(ToJVec3(half));
                s.SetEmbedded();
                ShapeSettings::ShapeResult r = s.Create();
                return r.IsValid() ? r.Get() : ShapeRefC();
            }
            case RenderType::Sphere:
            case RenderType::Torus: {
                float radius = std::max({obj.scale.x, obj.scale.y, obj.scale.z}) * 0.5f;
                radius = std::max(radius, 0.01f);
                SphereShapeSettings s(radius);
                s.SetEmbedded();
                ShapeSettings::ShapeResult r = s.Create();
                return r.IsValid() ? r.Get() : ShapeRefC();
            }
            case RenderType::Capsule: {
                float radius = std::max(obj.scale.x, obj.scale.z) * 0.5f;
                radius = std::max(radius, 0.01f);
                float cylHeight = std::max(0.05f, obj.scale.y - radius * 2.0f);
                CapsuleShapeSettings s(cylHeight * 0.5f, radius);
                s.SetEmbedded();
                ShapeSettings::ShapeResult r = s.Create();
                return r.IsValid() ? r.Get() : ShapeRefC();
            }
            default: return ShapeRefC();
        }
    }

    float ColliderFriction(const SceneObject& obj) {
        if (!obj.hasCollider || !obj.collider.enabled) return 0.9f;
        return std::clamp(0.5f * (obj.collider.staticFriction + obj.collider.dynamicFriction), 0.0f, 4.0f);
    }
    float ColliderRestitution(const SceneObject& obj) {
        if (!obj.hasCollider || !obj.collider.enabled) return 0.0f;
        return std::clamp(obj.collider.restitution, 0.0f, 1.0f);
    }
} // namespace

JoltPhysicsBackend::BodyRecord JoltPhysicsBackend::createBodyFor(const SceneObject& obj) {
    BodyRecord rec;
    const bool wantsDynamic = obj.hasRigidbody && obj.rigidbody.enabled;
    const bool wantsCollider = obj.hasCollider && obj.collider.enabled;
    const bool bakeWorldStaticMesh = !wantsDynamic && wantsCollider && obj.collider.type == ColliderType::Mesh;
    if (!wantsDynamic && !wantsCollider) return rec;

    JPH::ShapeRefC shape = BuildShapeForObject(obj, wantsDynamic, bakeWorldStaticMesh);
    if (!shape) return rec;

    const JPH::Vec3 pos = bakeWorldStaticMesh ? JPH::Vec3::sZero() : ToJVec3(obj.position);
    const JPH::Quat rot = bakeWorldStaticMesh ? JPH::Quat::sIdentity() : ToJQuat(obj.rotation);
    const JPH::ObjectLayer layer = wantsDynamic ? JoltLayers::MOVING : JoltLayers::NON_MOVING;
    const JPH::EMotionType motionType = wantsDynamic
        ? (obj.rigidbody.isKinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic)
        : JPH::EMotionType::Static;

    JPH::BodyCreationSettings settings(shape, pos, rot, motionType, layer);
    settings.mFriction = ColliderFriction(obj);
    settings.mRestitution = ColliderRestitution(obj);

    if (wantsDynamic) {
        settings.mLinearDamping = std::max(0.0f, obj.rigidbody.linearDamping);
        settings.mAngularDamping = std::max(0.0f, obj.rigidbody.angularDamping);
        settings.mGravityFactor = obj.rigidbody.useGravity ? 1.0f : 0.0f;
        settings.mAllowedDOFs = BuildAllowedDOFs(obj);
        if (!obj.rigidbody.isKinematic) {
            const float massKg = EffectiveMassKilograms(obj, mProjectSettings);
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = massKg;
            rec.massKg = massKg;
            rec.useCustomCenterOfMass = obj.rigidbody.useCustomCenterOfMass;
            rec.centerOfMass = obj.rigidbody.centerOfMass;
        }
        if (obj.hasPlayerController) {
            settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
        }
    }

    JPH::BodyInterface& bi = mPhysicsSystem->GetBodyInterfaceNoLock();
    JPH::BodyID bodyId = bi.CreateAndAddBody(settings,
        wantsDynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    if (bodyId.IsInvalid()) return rec;

    rec.bodyId = bodyId;
    rec.isDynamic = wantsDynamic;
    rec.isKinematic = wantsDynamic && obj.rigidbody.isKinematic;
    rec.hasWorldBakedStaticMesh = bakeWorldStaticMesh;
    rec.hasAuthoringPose = true;
    rec.lastAuthoringPosition = obj.position;
    rec.lastAuthoringRotation = obj.rotation;
    rec.linearDamping = obj.rigidbody.linearDamping;
    rec.angularDamping = obj.rigidbody.angularDamping;
    rec.gravityDisabled = !obj.rigidbody.useGravity;
    rec.angularLockMask = BuildAngularLockMask(obj);
    // Seed interpolation snapshots so the first render frame before any
    // physics step finishes still has matching prev/curr (no jump to origin).
    rec.prevPos = pos;
    rec.currPos = pos;
    rec.prevRot = rot;
    rec.currRot = rot;
    return rec;
}

void JoltPhysicsBackend::clearBodies() {
    if (mPhysicsSystem) {
        JPH::BodyInterface& bi = mPhysicsSystem->GetBodyInterfaceNoLock();
        for (auto& [id, rec] : mBodies) {
            if (!rec.bodyId.IsInvalid()) {
                bi.RemoveBody(rec.bodyId);
                bi.DestroyBody(rec.bodyId);
            }
        }
    }
    mBodies.clear();
    mIdsByBody.clear();
}

void JoltPhysicsBackend::onPlayStart(const std::vector<SceneObject>& objects) {
    if (!isReady()) return;
    clearBodies();
    mTimeAccumulator = 0.0f;
    mBodies.reserve(objects.size());
    mIdsByBody.reserve(objects.size());
    for (const auto& obj : objects) {
        if (!IsObjectEnabledInHierarchy(obj)) continue;
        BodyRecord rec = createBodyFor(obj);
        if (rec.bodyId.IsInvalid()) continue;
        mBodies[obj.id] = rec;
        mIdsByBody[rec.bodyId.GetIndexAndSequenceNumber()] = obj.id;
    }
    if (mPhysicsSystem) mPhysicsSystem->OptimizeBroadPhase();
}

void JoltPhysicsBackend::onPlayStop() {
    clearBodies();
    mTimeAccumulator = 0.0f;
}

bool JoltPhysicsBackend::setLinearVelocity(int id, const glm::vec3& velocity) {
    auto it = mBodies.find(id);
    if (it == mBodies.end()) return false;
    BodyRecord& rec = it->second;
    if (rec.bodyId.IsInvalid() || !rec.isDynamic) return false;
    JPH::BodyInterface& bi = mPhysicsSystem->GetBodyInterfaceNoLock();
    bi.ActivateBody(rec.bodyId);
    bi.SetLinearVelocity(rec.bodyId, ToJVec3(velocity));
    return true;
}

bool JoltPhysicsBackend::setAngularVelocity(int id, const glm::vec3& velocity) {
    auto it = mBodies.find(id);
    if (it == mBodies.end()) return false;
    BodyRecord& rec = it->second;
    if (rec.bodyId.IsInvalid() || !rec.isDynamic || rec.isKinematic) return false;
    JPH::BodyInterface& bi = mPhysicsSystem->GetBodyInterfaceNoLock();
    bi.SetAngularVelocity(rec.bodyId, ToJVec3(velocity));
    return true;
}

bool JoltPhysicsBackend::setActorYaw(int id, float yawDegrees) {
    auto it = mBodies.find(id);
    if (it == mBodies.end()) return false;
    BodyRecord& rec = it->second;
    if (rec.bodyId.IsInvalid()) return false;
    JPH::BodyInterface& bi = mPhysicsSystem->GetBodyInterfaceNoLock();
    JPH::Vec3 pos = bi.GetPosition(rec.bodyId);
    JPH::Quat q = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), glm::radians(yawDegrees));
    bi.SetPositionAndRotation(rec.bodyId, pos, q, JPH::EActivation::Activate);
    rec.hasAuthoringPose = true;
    rec.lastAuthoringPosition = FromJVec3(pos);
    rec.lastAuthoringRotation = FromJQuatEulerDeg(q);
    return true;
}

bool JoltPhysicsBackend::getLinearVelocity(int id, glm::vec3& outVelocity) const {
    auto it = mBodies.find(id);
    if (it == mBodies.end()) return false;
    const BodyRecord& rec = it->second;
    if (rec.bodyId.IsInvalid() || !rec.isDynamic) return false;
    JPH::BodyInterface& bi = mPhysicsSystem->GetBodyInterfaceNoLock();
    outVelocity = FromJVec3(bi.GetLinearVelocity(rec.bodyId));
    return true;
}

bool JoltPhysicsBackend::getAngularVelocity(int id, glm::vec3& outVelocity) const {
    auto it = mBodies.find(id);
    if (it == mBodies.end()) return false;
    const BodyRecord& rec = it->second;
    if (rec.bodyId.IsInvalid() || !rec.isDynamic || rec.isKinematic) return false;
    JPH::BodyInterface& bi = mPhysicsSystem->GetBodyInterfaceNoLock();
    outVelocity = FromJVec3(bi.GetAngularVelocity(rec.bodyId));
    return true;
}

bool JoltPhysicsBackend::setActorPose(int id, const glm::vec3& position, const glm::vec3& rotationDeg) {
    auto it = mBodies.find(id);
    if (it == mBodies.end()) return false;
    BodyRecord& rec = it->second;
    if (rec.bodyId.IsInvalid()) return false;
    JPH::BodyInterface& bi = mPhysicsSystem->GetBodyInterfaceNoLock();
    bi.SetPositionAndRotation(rec.bodyId, ToJVec3(position), ToJQuat(rotationDeg), JPH::EActivation::Activate);
    rec.hasAuthoringPose = true;
    rec.lastAuthoringPosition = position;
    rec.lastAuthoringRotation = rotationDeg;
    return true;
}

bool JoltPhysicsBackend::addForce(int id, const glm::vec3& force) {
    auto it = mBodies.find(id);
    if (it == mBodies.end()) return false;
    BodyRecord& rec = it->second;
    if (rec.bodyId.IsInvalid() || !rec.isDynamic || rec.isKinematic) return false;
    mPhysicsSystem->GetBodyInterfaceNoLock().AddForce(rec.bodyId, ToJVec3(force));
    return true;
}

bool JoltPhysicsBackend::addImpulse(int id, const glm::vec3& impulse) {
    auto it = mBodies.find(id);
    if (it == mBodies.end()) return false;
    BodyRecord& rec = it->second;
    if (rec.bodyId.IsInvalid() || !rec.isDynamic || rec.isKinematic) return false;
    mPhysicsSystem->GetBodyInterfaceNoLock().AddImpulse(rec.bodyId, ToJVec3(impulse));
    return true;
}

bool JoltPhysicsBackend::addTorque(int id, const glm::vec3& torque) {
    auto it = mBodies.find(id);
    if (it == mBodies.end()) return false;
    BodyRecord& rec = it->second;
    if (rec.bodyId.IsInvalid() || !rec.isDynamic || rec.isKinematic) return false;
    mPhysicsSystem->GetBodyInterfaceNoLock().AddTorque(rec.bodyId, ToJVec3(torque));
    return true;
}

bool JoltPhysicsBackend::addAngularImpulse(int id, const glm::vec3& impulse) {
    auto it = mBodies.find(id);
    if (it == mBodies.end()) return false;
    BodyRecord& rec = it->second;
    if (rec.bodyId.IsInvalid() || !rec.isDynamic || rec.isKinematic) return false;
    mPhysicsSystem->GetBodyInterfaceNoLock().AddAngularImpulse(rec.bodyId, ToJVec3(impulse));
    return true;
}

namespace {
    using namespace JPH;

    // Drops any hit that maps to the ignored body. Both broad and narrow
    // phase consult this so the engine matches PhysX's IgnoreActorFilter.
    class IgnoreBodyFilter final : public BodyFilter {
    public:
        BodyID ignore;
        bool ShouldCollide(const BodyID& inBodyID) const override {
            return inBodyID != ignore;
        }
        bool ShouldCollideLocked(const Body& inBody) const override {
            return inBody.GetID() != ignore;
        }
    };
} // namespace

bool JoltPhysicsBackend::raycastClosest(const glm::vec3& origin,
                                        const glm::vec3& dir,
                                        float distance,
                                        int ignoreId,
                                        glm::vec3* hitPos,
                                        glm::vec3* hitNormal,
                                        float* hitDistance,
                                        int* hitActorId,
                                        glm::vec3* hitActorVelocity,
                                        float* hitStaticFriction,
                                        float* hitDynamicFriction) const {
    if (!isReady() || distance <= 0.0f) return false;
    if (hitActorId) *hitActorId = -1;
    if (hitActorVelocity) *hitActorVelocity = glm::vec3(0.0f);
    if (hitStaticFriction) *hitStaticFriction = 0.9f;
    if (hitDynamicFriction) *hitDynamicFriction = 0.9f;

    glm::vec3 normDir = glm::normalize(dir);
    if (!std::isfinite(normDir.x) || !std::isfinite(normDir.y) || !std::isfinite(normDir.z)) return false;

    JPH::RRayCast ray(ToJVec3(origin), ToJVec3(normDir * distance));
    JPH::RayCastResult result;

    IgnoreBodyFilter bodyFilter;
    auto it = mBodies.find(ignoreId);
    if (it != mBodies.end()) bodyFilter.ignore = it->second.bodyId;

    const JPH::NarrowPhaseQuery& q = mPhysicsSystem->GetNarrowPhaseQuery();
    if (!q.CastRay(ray, result, {}, {}, bodyFilter)) return false;

    JPH::BodyLockRead lock(mPhysicsSystem->GetBodyLockInterface(), result.mBodyID);
    if (!lock.Succeeded()) return false;
    const JPH::Body& body = lock.GetBody();

    const JPH::Vec3 hitPoint = ray.GetPointOnRay(result.mFraction);
    if (hitPos) *hitPos = FromJVec3(hitPoint);
    if (hitNormal) {
        JPH::Vec3 n = body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, hitPoint);
        *hitNormal = FromJVec3(n);
    }
    if (hitDistance) *hitDistance = result.mFraction * distance;

    const float friction = body.GetFriction();
    if (hitStaticFriction) *hitStaticFriction = friction;
    if (hitDynamicFriction) *hitDynamicFriction = friction;

    if (hitActorId || hitActorVelocity) {
        auto idIt = mIdsByBody.find(result.mBodyID.GetIndexAndSequenceNumber());
        if (idIt != mIdsByBody.end()) {
            if (hitActorId) *hitActorId = idIt->second;
            if (hitActorVelocity && body.IsDynamic()) {
                *hitActorVelocity = FromJVec3(body.GetLinearVelocity());
            }
        }
    }
    return true;
}

void JoltPhysicsBackend::simulate(float deltaTime, std::vector<SceneObject>& objects) {
    if (!isReady() || deltaTime <= 0.0f) return;

    std::unordered_map<int, SceneObject*> objectsById;
    objectsById.reserve(objects.size());
    for (SceneObject& obj : objects) objectsById[obj.id] = &obj;

    JPH::BodyInterface& bi = mPhysicsSystem->GetBodyInterfaceNoLock();

    // 0th pass: lazy spawn for objects that became hierarchy-enabled mid-play.
    for (SceneObject& obj : objects) {
        if (mBodies.find(obj.id) != mBodies.end()) continue;
        if (!IsObjectEnabledInHierarchy(obj)) continue;
        const bool wantsDyn = obj.hasRigidbody && obj.rigidbody.enabled;
        const bool wantsCol = obj.hasCollider && obj.collider.enabled;
        if (!wantsDyn && !wantsCol) continue;
        BodyRecord rec = createBodyFor(obj);
        if (rec.bodyId.IsInvalid()) continue;
        mBodies[obj.id] = rec;
        mIdsByBody[rec.bodyId.GetIndexAndSequenceNumber()] = obj.id;
    }

    // 1st pass: rebuild bodies whose dynamic/static mode no longer matches.
    {
        std::vector<int> idsToRecreate;
        for (auto& [id, rec] : mBodies) {
            if (rec.bodyId.IsInvalid()) continue;
            auto objIt = objectsById.find(id);
            if (objIt == objectsById.end()) continue;
            const bool wantsDynamicNow = objIt->second->hasRigidbody && objIt->second->rigidbody.enabled;
            if (rec.isDynamic != wantsDynamicNow) idsToRecreate.push_back(id);
        }
        for (int id : idsToRecreate) {
            auto recIt = mBodies.find(id);
            if (recIt == mBodies.end()) continue;
            BodyRecord& rec = recIt->second;
            if (!rec.bodyId.IsInvalid()) {
                mIdsByBody.erase(rec.bodyId.GetIndexAndSequenceNumber());
                bi.RemoveBody(rec.bodyId);
                bi.DestroyBody(rec.bodyId);
                rec.bodyId = JPH::BodyID();
            }
            BodyRecord fresh = createBodyFor(*objectsById[id]);
            if (!fresh.bodyId.IsInvalid()) {
                mIdsByBody[fresh.bodyId.GetIndexAndSequenceNumber()] = id;
                recIt->second = fresh;
            } else {
                mBodies.erase(recIt);
            }
        }
    }

    // 2nd pass: sync runtime changes from SceneObject → body (hierarchy toggle,
    // kinematic flag, gravity, damping, locks, mass, kinematic targets,
    // static-body pose follow).
    for (auto& [id, rec] : mBodies) {
        if (rec.bodyId.IsInvalid()) continue;
        auto objIt = objectsById.find(id);
        if (objIt == objectsById.end()) continue;
        SceneObject& obj = *objIt->second;
        const bool enabledInHierarchy = IsObjectEnabledInHierarchy(obj);
        if (enabledInHierarchy == rec.simulationDisabled) {
            // Jolt has no per-body "disable simulation" flag; adding/removing
            // from the simulation is the equivalent toggle. Body memory is
            // preserved.
            if (enabledInHierarchy) bi.AddBody(rec.bodyId, JPH::EActivation::Activate);
            else bi.RemoveBody(rec.bodyId);
            rec.simulationDisabled = !enabledInHierarchy;
        }
        if (!enabledInHierarchy) continue;

        if (rec.isDynamic) {
            const bool isKinematic = obj.rigidbody.isKinematic;
            if (rec.isKinematic != isKinematic) {
                bi.SetMotionType(rec.bodyId,
                    isKinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic,
                    JPH::EActivation::Activate);
                rec.isKinematic = isKinematic;
            }
            const bool gravityDisabled = !obj.rigidbody.useGravity;
            if (rec.gravityDisabled != gravityDisabled) {
                bi.SetGravityFactor(rec.bodyId, gravityDisabled ? 0.0f : 1.0f);
                rec.gravityDisabled = gravityDisabled;
            }
            // Damping changes need a body lock, because Jolt's BodyInterface doesn't
            // expose damping setters in the convenience API.
            const bool dampingChanged =
                std::abs(rec.linearDamping - obj.rigidbody.linearDamping) > 0.0001f ||
                std::abs(rec.angularDamping - obj.rigidbody.angularDamping) > 0.0001f;
            const uint32_t newLockMask = BuildAngularLockMask(obj);
            const bool locksChanged = rec.angularLockMask != newLockMask;
            const float massKg = EffectiveMassKilograms(obj, mProjectSettings);
            const bool massChanged = !isKinematic && std::abs(rec.massKg - massKg) > 0.0001f;
            if (dampingChanged || locksChanged || massChanged) {
                JPH::BodyLockWrite lock(mPhysicsSystem->GetBodyLockInterface(), rec.bodyId);
                if (lock.Succeeded()) {
                    JPH::Body& body = lock.GetBody();
                    if (dampingChanged) {
                        if (auto* mp = body.GetMotionPropertiesUnchecked()) {
                            mp->SetLinearDamping(std::max(0.0f, obj.rigidbody.linearDamping));
                            mp->SetAngularDamping(std::max(0.0f, obj.rigidbody.angularDamping));
                        }
                        rec.linearDamping = obj.rigidbody.linearDamping;
                        rec.angularDamping = obj.rigidbody.angularDamping;
                    }
                    if (massChanged) {
                        if (auto* mp = body.GetMotionPropertiesUnchecked()) {
                            // ScaleToMass adjusts both mass AND inertia proportionally.
                            // it's the only correct way to do a live mass change without
                            // rebuilding the whole body. future me: do NOT "simplify" this
                            // into just setting the mass, the inertia tensor desyncs and your
                            // boxes start spinning like they're possessed. trust me on this one.
                            mp->ScaleToMass(std::max(0.01f, massKg));
                        }
                        rec.massKg = massKg;
                    }
                    // Angular locks change the body's allowed DOFs. Jolt
                    // requires re-setting the mass properties to update
                    // these; for now we just record the desired mask and
                    // let the next createBodyFor pick it up if the body
                    // is recreated. Live update is a follow-up.
                    rec.angularLockMask = newLockMask;
                    (void)locksChanged;
                }
            }
            if (rec.isKinematic) {
                const bool poseChanged = !rec.hasAuthoringPose ||
                    PoseChangedSignificantly(rec.lastAuthoringPosition, rec.lastAuthoringRotation, obj.position, obj.rotation);
                if (poseChanged) {
                    bi.MoveKinematic(rec.bodyId, ToJVec3(obj.position), ToJQuat(obj.rotation), deltaTime);
                    rec.hasAuthoringPose = true;
                    rec.lastAuthoringPosition = obj.position;
                    rec.lastAuthoringRotation = obj.rotation;
                }
            }
        } else {
            if (rec.hasWorldBakedStaticMesh) continue;
            const bool poseChanged = !rec.hasAuthoringPose ||
                PoseChangedSignificantly(rec.lastAuthoringPosition, rec.lastAuthoringRotation, obj.position, obj.rotation);
            if (poseChanged) {
                bi.SetPositionAndRotation(rec.bodyId, ToJVec3(obj.position), ToJQuat(obj.rotation), JPH::EActivation::DontActivate);
                rec.hasAuthoringPose = true;
                rec.lastAuthoringPosition = obj.position;
                rec.lastAuthoringRotation = obj.rotation;
            }
        }
    }

    // Fixed-step physics with render-time interpolation between ticks.
    // without interpolation, render frames falling between two physics
    // steps would see no pose change, so the body appears to freeze for a
    // few render frames then jump on the next step. we snapshot prev/curr
    // around the step(s) and lerp between them based on how far into the
    // *next* step the accumulator has progressed.
    // future me: the prev/curr snapshot is the whole trick here. rip out the lerp and run physics
    // straight off the render dt and everything gets jittery as hell on high-refresh monitors. don't.
    const float fixedDt = std::clamp(mProjectSettings.fixedTimestep, 0.001f, 0.1f);
    mTimeAccumulator += deltaTime;
    constexpr int kMaxSubsteps = 4;
    constexpr int kCollisionSteps = 1;

    const bool willStep = mTimeAccumulator >= fixedDt;
    if (willStep) {
        // Roll prev forward to the start of this frame's substeps. After
        // the step completes, currPos/currRot will hold the new pose; the
        // interpolation alpha walks from prev → curr as accumulator grows.
        for (auto& [id, rec] : mBodies) {
            if (rec.bodyId.IsInvalid() || !rec.isDynamic || rec.isKinematic) continue;
            rec.prevPos = rec.currPos;
            rec.prevRot = rec.currRot;
        }
    }

    int substeps = 0;
    while (mTimeAccumulator >= fixedDt && substeps < kMaxSubsteps) {
        mPhysicsSystem->Update(fixedDt, kCollisionSteps, mTempAllocator.get(), mJobSystem.get());
        mTimeAccumulator -= fixedDt;
        ++substeps;
    }
    if (substeps == kMaxSubsteps) mTimeAccumulator = 0.0f;

    if (substeps > 0) {
        for (auto& [id, rec] : mBodies) {
            if (rec.bodyId.IsInvalid() || !rec.isDynamic || rec.isKinematic) continue;
            rec.currPos = bi.GetPosition(rec.bodyId);
            rec.currRot = bi.GetRotation(rec.bodyId);
        }
    }

    const float alpha = std::clamp(mTimeAccumulator / fixedDt, 0.0f, 1.0f);

    // Writeback: dynamic-only, mirror PhysX's lock-aware rotation handling.
    // Dynamic bodies use the interpolated render pose; kinematic bodies
    // read straight from the body interface (MoveKinematic already smooths
    // their motion across the step).
    for (auto& [id, rec] : mBodies) {
        if (rec.bodyId.IsInvalid() || !rec.isDynamic || rec.isKinematic) continue;
        auto objIt = objectsById.find(id);
        if (objIt == objectsById.end() || !IsObjectEnabledInHierarchy(*objIt->second)) continue;
        SceneObject& obj = *objIt->second;

        JPH::Vec3 renderPos = rec.prevPos * (1.0f - alpha) + rec.currPos * alpha;
        JPH::Quat renderRot = rec.prevRot.SLERP(rec.currRot, alpha);
        obj.position = FromJVec3(renderPos);
        if (obj.hasPlayerController && obj.playerController.enabled) continue;
        glm::vec3 euler = FromJQuatEulerDeg(renderRot);
        const bool lockX = (rec.angularLockMask & (1u << 0u)) != 0;
        const bool lockY = (rec.angularLockMask & (1u << 1u)) != 0;
        const bool lockZ = (rec.angularLockMask & (1u << 2u)) != 0;
        if (lockX && lockZ && !lockY) obj.rotation.y = euler.y;
        else obj.rotation = euler;
    }
}

#endif // MODULARITY_ENABLE_JOLT
