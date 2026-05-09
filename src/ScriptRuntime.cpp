#include "ScriptRuntime.h"
#include "Engine.h"
#include "SceneObject.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <unordered_map>

#if defined(_WIN32)
    #include <Windows.h>
#   ifdef DeleteFile
#       undef DeleteFile
#   endif
#else
    #include <dlfcn.h>
#endif

namespace {
std::string makeScriptInstanceKey(const ScriptContext& ctx) {
    if (!ctx.script) return {};
    std::string key = (!ctx.script->path.empty())
        ? ctx.script->path
        : std::to_string(reinterpret_cast<uintptr_t>(ctx.script));
    if (ctx.object) {
        key += "|obj:" + std::to_string(ctx.object->id);
        auto it = std::find_if(ctx.object->scripts.begin(), ctx.object->scripts.end(),
            [&](const ScriptComponent& s) { return &s == ctx.script; });
        if (it != ctx.object->scripts.end()) {
            key += "|slot:" + std::to_string(std::distance(ctx.object->scripts.begin(), it));
        }
    }
    return key;
}

std::string trimString(const std::string& input) {
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        ++start;
    }
    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    return input.substr(start, end - start);
}

bool isUIObject(const SceneObject* obj) {
    return obj && HasUIComponent(*obj);
}

glm::vec2 moveTowardsVec2(const glm::vec2& current, const glm::vec2& target, float maxDelta) {
    if (maxDelta <= 0.0f) return current;
    glm::vec2 delta = target - current;
    float len = glm::length(delta);
    if (len <= maxDelta || len <= 1e-5f) {
        return target;
    }
    return current + (delta / len) * maxDelta;
}

glm::vec3 sanitizePlanar(const glm::vec3& value) {
    glm::vec3 out(value.x, 0.0f, value.z);
    if (!std::isfinite(out.x) || !std::isfinite(out.z)) {
        return glm::vec3(0.0f);
    }
    return out;
}

bool isGlfwKeyDownFallback(int key) {
    GLFWwindow* window = glfwGetCurrentContext();
    if (!window) return false;
    return glfwGetKey(window, key) == GLFW_PRESS;
}

bool isGlfwMouseDownFallback(int button) {
    GLFWwindow* window = glfwGetCurrentContext();
    if (!window) return false;
    return glfwGetMouseButton(window, button) == GLFW_PRESS;
}

bool isMoveKeyDown(const ScriptContext* ctx, ImGuiKey imguiKey, int glfwKey) {
    if (ImGui::IsKeyDown(imguiKey)) return true;
    if (ctx && ctx->engine && ctx->engine->isRuntimeKeyDown(glfwKey)) return true;
    return isGlfwKeyDownFallback(glfwKey);
}

bool isScriptMouseDown(const ScriptContext* ctx, int glfwButton) {
    if (ctx && ctx->engine && ctx->engine->isRuntimeMouseDown(glfwButton)) return true;
    return isGlfwMouseDownFallback(glfwButton);
}

glm::vec2 getScriptMouseDelta(const ScriptContext* ctx) {
    if (ctx && ctx->engine) {
        return ctx->engine->getRuntimeMouseDelta();
    }

    GLFWwindow* window = glfwGetCurrentContext();
    if (!window) {
        return glm::vec2(0.0f);
    }

    struct CursorCache {
        int frame = -1;
        bool hasPos = false;
        double lastX = 0.0;
        double lastY = 0.0;
        glm::vec2 delta = glm::vec2(0.0f);
    };

    static std::unordered_map<GLFWwindow*, CursorCache> cacheByWindow;
    CursorCache& cache = cacheByWindow[window];
    int frame = ImGui::GetFrameCount();
    if (cache.frame == frame) {
        return cache.delta;
    }

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);

    glm::vec2 computed(0.0f);
    if (cache.hasPos) {
        computed.x = static_cast<float>(x - cache.lastX);
        computed.y = static_cast<float>(y - cache.lastY);
    }

    cache.lastX = x;
    cache.lastY = y;
    cache.hasPos = true;
    cache.frame = frame;
    cache.delta = computed;
    return computed;
}

std::string makeRuntimeStateKey(const ScriptContext& ctx, const char* suffix) {
    std::string key = makeScriptInstanceKey(ctx);
    if (key.empty()) {
        if (ctx.object) {
            key = "obj:" + std::to_string(ctx.object->id);
        } else {
            key = "ctx:" + std::to_string(reinterpret_cast<uintptr_t>(&ctx));
        }
    }
    if (suffix && *suffix) {
        key += "|";
        key += suffix;
    }
    return key;
}

struct SpriteAlphaFadeState {
    float startAlpha = 1.0f;
    float targetAlpha = 1.0f;
    float duration = 0.0f;
    float elapsed = 0.0f;
};

enum class SpriteClipFadePhase {
    FadingOut = 0,
    FadingIn = 1
};

struct SpriteClipFadeState {
    int targetClipIndex = -1;
    float fadeOutDuration = 0.0f;
    float fadeInDuration = 0.0f;
    float baseAlpha = 1.0f;
    float phaseStartAlpha = 1.0f;
    float elapsed = 0.0f;
    SpriteClipFadePhase phase = SpriteClipFadePhase::FadingOut;
};

std::unordered_map<std::string, SpriteAlphaFadeState> gSpriteAlphaFadeStates;
std::unordered_map<std::string, SpriteClipFadeState> gSpriteClipFadeStates;

struct KeyPressedState {
    bool previousDown = false;
    int frame = -1;
    bool pressed = false;
};

std::unordered_map<std::string, KeyPressedState> gKeyPressedStates;

#if defined(_WIN32)
fs::path makeShadowScriptBinaryPath(const fs::path& binaryPath) {
    std::error_code ec;
    fs::path sourceAbsolute = fs::absolute(binaryPath, ec);
    if (ec) {
        sourceAbsolute = binaryPath;
        ec.clear();
    }

    fs::path sourceCanonical = fs::weakly_canonical(sourceAbsolute, ec);
    if (!ec) {
        sourceAbsolute = sourceCanonical;
    } else {
        ec.clear();
    }

    const auto writeTime = fs::last_write_time(sourceAbsolute, ec);
    const long long writeStamp = ec ? 0LL : static_cast<long long>(writeTime.time_since_epoch().count());
    ec.clear();

    const auto fileSize = fs::file_size(sourceAbsolute, ec);
    const unsigned long long sizeStamp = ec ? 0ULL : static_cast<unsigned long long>(fileSize);
    ec.clear();

    const std::string canonicalKey = sourceAbsolute.lexically_normal().string();
    const size_t pathHash = std::hash<std::string>{}(canonicalKey);
    const unsigned long processId = static_cast<unsigned long>(GetCurrentProcessId());

    fs::path shadowDir = sourceAbsolute.parent_path() / ".loaded";
    fs::create_directories(shadowDir, ec);

    std::ostringstream filename;
    filename << sourceAbsolute.stem().string()
             << ".pid" << processId
             << ".t" << writeStamp
             << ".s" << sizeStamp
             << ".h" << pathHash
             << sourceAbsolute.extension().string();
    return shadowDir / filename.str();
}

bool prepareShadowScriptBinary(const fs::path& binaryPath, fs::path& outShadowPath, std::string& error) {
    std::error_code ec;
    if (!fs::exists(binaryPath, ec) || ec) {
        error = "Script binary not found: " + binaryPath.string();
        return false;
    }

    outShadowPath = makeShadowScriptBinaryPath(binaryPath);
    if (outShadowPath.empty()) {
        error = "Unable to prepare shadow copy path for script binary: " + binaryPath.string();
        return false;
    }

    fs::create_directories(outShadowPath.parent_path(), ec);
    ec.clear();

    if (fs::exists(outShadowPath, ec) && !ec) {
        fs::remove(outShadowPath, ec);
        ec.clear();
    }

    fs::copy_file(binaryPath, outShadowPath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "Failed to create shadow script binary: " + outShadowPath.string();
        return false;
    }

    return true;
}
#endif
}

SceneObject* ScriptContext::FindObjectByName(const std::string& name) {
    if (!engine) return nullptr;
    return engine->findObjectByName(name);
}

SceneObject* ScriptContext::FindObjectById(int id) {
    if (!engine) return nullptr;
    return engine->findObjectById(id);
}

SceneObject* ScriptContext::ResolveObjectRef(const std::string& ref) {
    if (ref.empty()) return nullptr;
    std::string trimmed = trimString(ref);
    if (trimmed == "ObjectSelf") return object;

    const std::string namePrefix = "Object.";
    const std::string idPrefix = "Object.ID-";
    if (trimmed.rfind(idPrefix, 0) == 0) {
        std::string idStr = trimmed.substr(idPrefix.size());
        if (idStr.empty()) return nullptr;
        try {
            int id = std::stoi(idStr);
            return FindObjectById(id);
        } catch (...) {
            return nullptr;
        }
    }
    if (trimmed.rfind(namePrefix, 0) == 0) {
        std::string name = trimmed.substr(namePrefix.size());
        if (name.empty()) return nullptr;
        return FindObjectByName(name);
    }
    return nullptr;
}

int ScriptContext::GetSceneObjectCount() const {
    if (!engine) return 0;
    return static_cast<int>(engine->getSceneObjects().size());
}

int ScriptContext::GetSceneObjectIdAt(int index) const {
    if (!engine || index < 0) return -1;
    const auto& objects = engine->getSceneObjects();
    if (index >= static_cast<int>(objects.size())) return -1;
    return objects[static_cast<size_t>(index)].id;
}

const SceneObject* ScriptContext::GetSceneObjectAt(int index) const {
    if (!engine || index < 0) return nullptr;
    const auto& objects = engine->getSceneObjects();
    if (index >= static_cast<int>(objects.size())) return nullptr;
    return &objects[static_cast<size_t>(index)];
}

bool ScriptContext::IsObjectEnabled() const {
    return object ? IsObjectEnabledInHierarchy(*object) : false;
}

void ScriptContext::SetObjectEnabled(bool enabled) {
    if (!object) return;
    if (object->enabled != enabled) {
        object->enabled = enabled;
        MarkDirty();
    }
}

int ScriptContext::GetLayer() const {
    return object ? object->layer : 0;
}

void ScriptContext::SetLayer(int layer) {
    if (!object) return;
    int clamped = std::clamp(layer, 0, 31);
    if (object->layer != clamped) {
        object->layer = clamped;
        MarkDirty();
    }
}

std::string ScriptContext::GetTag() const {
    return object ? object->tag : std::string();
}

void ScriptContext::SetTag(const std::string& tag) {
    if (!object) return;
    if (object->tag != tag) {
        object->tag = tag;
        MarkDirty();
    }
}

bool ScriptContext::HasTag(const std::string& tag) const {
    return object && object->tag == tag;
}

bool ScriptContext::IsInLayer(int layer) const {
    return object && object->layer == layer;
}

void ScriptContext::SetPosition(const glm::vec3& pos) {
    if (object) {
        object->position = pos;
        if (engine) {
            engine->syncLocalTransform(*object);
        } else {
            object->localPosition = object->position;
            object->localInitialized = true;
        }
        MarkDirty();
    }
}

void ScriptContext::SetPosition2D(const glm::vec2& pos) {
    if (!object) return;
    object->ui.position = pos;
    MarkDirty();
}

void ScriptContext::SetRotation(const glm::vec3& rot) {
    if (object) {
        object->rotation = NormalizeEulerDegrees(rot);
        if (engine) {
            engine->syncLocalTransform(*object);
        } else {
            object->localRotation = object->rotation;
            object->localInitialized = true;
        }
        MarkDirty();
        if (engine && HasRigidbody()) {
            engine->teleportPhysicsActorFromScript(object->id, object->position, object->rotation);
        }
    }
}

void ScriptContext::SetScale(const glm::vec3& scl) {
    if (object) {
        object->scale = scl;
        if (engine) {
            engine->syncLocalTransform(*object);
        } else {
            object->localScale = object->scale;
            object->localInitialized = true;
        }
        MarkDirty();
    }
}

void ScriptContext::GetPlanarYawPitchVectors(float pitchDeg, float yawDeg,
                                             glm::vec3& outForward, glm::vec3& outRight) const {
    glm::quat q = glm::quat(glm::radians(glm::vec3(pitchDeg, yawDeg, 0.0f)));
    glm::vec3 forward = glm::normalize(q * glm::vec3(0.0f, 0.0f, -1.0f));
    glm::vec3 right = glm::normalize(q * glm::vec3(1.0f, 0.0f, 0.0f));
    outForward = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
    outRight = glm::normalize(glm::vec3(right.x, 0.0f, right.z));
    if (!std::isfinite(outForward.x) || glm::length(outForward) < 1e-3f) {
        outForward = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    if (!std::isfinite(outRight.x) || glm::length(outRight) < 1e-3f) {
        outRight = glm::vec3(1.0f, 0.0f, 0.0f);
    }
}

glm::vec3 ScriptContext::GetMoveInputWASD(float pitchDeg, float yawDeg) const {
    glm::vec3 forward(0.0f);
    glm::vec3 right(0.0f);
    glm::vec3 move(0.0f);
    GetPlanarYawPitchVectors(pitchDeg, yawDeg, forward, right);
    if (isMoveKeyDown(this, ImGuiKey_W, GLFW_KEY_W)) move += forward;
    if (isMoveKeyDown(this, ImGuiKey_S, GLFW_KEY_S)) move -= forward;
    if (isMoveKeyDown(this, ImGuiKey_D, GLFW_KEY_D)) move += right;
    if (isMoveKeyDown(this, ImGuiKey_A, GLFW_KEY_A)) move -= right;
    if (glm::length(move) > 0.001f) move = glm::normalize(move);
    return move;
}

bool ScriptContext::ApplyMouseLook(float& pitchDeg, float& yawDeg, float sensitivity, float maxDelta,
                                   float deltaTime, bool requireMouseButton) const {
    if (requireMouseButton &&
        !(ImGui::IsMouseDown(ImGuiMouseButton_Right) || isScriptMouseDown(this, GLFW_MOUSE_BUTTON_RIGHT))) {
        return false;
    }
    glm::vec2 delta = getScriptMouseDelta(this);
    float len = glm::length(delta);
    if (len > maxDelta) delta *= (maxDelta / len);
    yawDeg -= delta.x * 50.0f * sensitivity * deltaTime;
    pitchDeg -= delta.y * 50.0f * sensitivity * deltaTime;
    pitchDeg = std::clamp(pitchDeg, -89.0f, 89.0f);
    return true;
}

int ScriptContext::GetSelectedObjectId() const {
    if (!engine) return -1;
    return engine->getSelectedObjectId();
}

bool ScriptContext::IsSprintDown() const {
    return isMoveKeyDown(this, ImGuiKey_LeftShift, GLFW_KEY_LEFT_SHIFT) ||
           isMoveKeyDown(this, ImGuiKey_RightShift, GLFW_KEY_RIGHT_SHIFT);
}

bool ScriptContext::IsJumpDown() const {
    return isMoveKeyDown(this, ImGuiKey_Space, GLFW_KEY_SPACE);
}

bool ScriptContext::IsKeyDown(int glfwKey, ImGuiKey imguiKey) const {
    if (imguiKey != ImGuiKey_None && ImGui::IsKeyDown(imguiKey)) return true;
    if (engine && engine->isRuntimeKeyDown(glfwKey)) return true;
    return isGlfwKeyDownFallback(glfwKey);
}

bool ScriptContext::IsKeyPressed(int glfwKey, ImGuiKey imguiKey) const {
    const std::string suffix = "keyPressed:" + std::to_string(glfwKey);
    std::string key = makeRuntimeStateKey(*this, suffix.c_str());
    KeyPressedState& state = gKeyPressedStates[key];
    const int frame = ImGui::GetFrameCount();
    if (state.frame != frame) {
        const bool downNow = IsKeyDown(glfwKey, imguiKey);
        state.pressed = downNow && !state.previousDown;
        state.previousDown = downNow;
        state.frame = frame;
    }
    return state.pressed;
}

bool ScriptContext::ResolveGround(float capsuleHalf, float probeExtra, float groundSnap, float verticalVelocity,
                                  glm::vec3* outHitPos, bool* outHitGround,
                                  glm::vec3* outHitNormal, int* outHitActorId,
                                  glm::vec3* outHitActorVelocity,
                                  float* outHitStaticFriction,
                                  float* outHitDynamicFriction) const {
    if (!object) return false;
    glm::vec3 hitPos(0.0f);
    glm::vec3 hitNormal(0.0f, 1.0f, 0.0f);
    float hitDist = 0.0f;
    int hitActorId = -1;
    glm::vec3 hitActorVelocity(0.0f);
    float hitStaticFriction = 0.9f;
    float hitDynamicFriction = 0.9f;
    float probeDist = capsuleHalf + probeExtra;
    glm::vec3 rayStart = object->position + glm::vec3(0.0f, 0.1f, 0.0f);
    bool hitGround = RaycastClosestDetailed(rayStart, glm::vec3(0.0f, -1.0f, 0.0f), probeDist,
                                            &hitPos, &hitNormal, &hitDist,
                                            &hitActorId, &hitActorVelocity,
                                            &hitStaticFriction, &hitDynamicFriction);
    bool grounded = hitGround && hitNormal.y > 0.25f &&
                    hitDist <= capsuleHalf + groundSnap &&
                    verticalVelocity <= 0.35f;
    if (outHitPos) *outHitPos = hitPos;
    if (outHitGround) *outHitGround = hitGround;
    if (outHitNormal) *outHitNormal = hitNormal;
    if (outHitActorId) *outHitActorId = hitActorId;
    if (outHitActorVelocity) *outHitActorVelocity = hitActorVelocity;
    if (outHitStaticFriction) *outHitStaticFriction = hitStaticFriction;
    if (outHitDynamicFriction) *outHitDynamicFriction = hitDynamicFriction;
    return grounded;
}

void ScriptContext::ApplyVelocity(const glm::vec3& velocity, float deltaTime) {
    if (!object) return;
    if (!SetRigidbodyVelocity(velocity)) {
        object->position += velocity * deltaTime;
    }
}

void ScriptContext::BindStandaloneMovementSettings(StandaloneMovementSettings& settings) {
    AutoSetting("moveTuning", settings.moveTuning);
    AutoSetting("lookTuning", settings.lookTuning);
    AutoSetting("capsuleTuning", settings.capsuleTuning);
    AutoSetting("gravityTuning", settings.gravityTuning);
    AutoSetting("locomotionTuning", settings.locomotionTuning);
    AutoSetting("surfaceTuning", settings.surfaceTuning);
    AutoSetting("enableMouseLook", settings.enableMouseLook);
    AutoSetting("requireMouseButton", settings.requireMouseButton);
    AutoSetting("enforceCollider", settings.enforceCollider);
    AutoSetting("enforceRigidbody", settings.enforceRigidbody);
}

void ScriptContext::DrawStandaloneMovementInspector(StandaloneMovementSettings& settings, bool* showDebug) {
    BindStandaloneMovementSettings(settings);
    ImGui::TextUnformatted("Standalone Movement Controller");
    ImGui::Separator();
    ImGui::DragFloat3("Walk/Run/Jump", &settings.moveTuning.x, 0.05f, 0.0f, 25.0f, "%.2f");
    ImGui::DragFloat2("Look Sens/Clamp", &settings.lookTuning.x, 0.01f, 0.0f, 500.0f, "%.2f");
    ImGui::DragFloat3("Height/Radius/Snap", &settings.capsuleTuning.x, 0.02f, 0.0f, 5.0f, "%.2f");
    ImGui::DragFloat3("Gravity/Probe/MaxFall", &settings.gravityTuning.x, 0.05f, -50.0f, 50.0f, "%.2f");
    ImGui::DragFloat3("Ground/Air/Brake", &settings.locomotionTuning.x, 0.05f, 0.0f, 200.0f, "%.2f");
    ImGui::DragFloat3("Grip/Slide/Carry", &settings.surfaceTuning.x, 0.02f, 0.0f, 200.0f, "%.2f");
    ImGui::Checkbox("Enable Mouse Look", &settings.enableMouseLook);
    ImGui::Checkbox("Hold RMB to Look", &settings.requireMouseButton);
    ImGui::Checkbox("Force Collider", &settings.enforceCollider);
    ImGui::Checkbox("Force Rigidbody", &settings.enforceRigidbody);
    if (showDebug) {
        AutoSetting("showDebug", *showDebug);
        ImGui::Checkbox("Show Debug", showDebug);
    }
}

void ScriptContext::TickStandaloneMovement(StandaloneMovementState& state, StandaloneMovementSettings& settings,
                                           float deltaTime, StandaloneMovementDebug* debug) {
    if (!object) return;
    BindStandaloneMovementSettings(settings);
    if (settings.enforceCollider) EnsureCapsuleCollider(settings.capsuleTuning.x, settings.capsuleTuning.y);
    if (settings.enforceRigidbody) EnsureRigidbody(true, false);

    const float walkSpeed = settings.moveTuning.x;
    const float runSpeed = settings.moveTuning.y;
    const float jumpStrength = settings.moveTuning.z;
    const float lookSensitivity = settings.lookTuning.x;
    const float maxMouseDelta = glm::max(5.0f, settings.lookTuning.y);
    const float height = settings.capsuleTuning.x;
    const float groundSnap = settings.capsuleTuning.z;
    const float gravity = settings.gravityTuning.x;
    const float probeExtra = settings.gravityTuning.y;
    const float maxFall = glm::max(1.0f, settings.gravityTuning.z);
    const float groundAccel = glm::max(0.0f, settings.locomotionTuning.x);
    const float airAccel = glm::max(0.0f, settings.locomotionTuning.y);
    const float braking = glm::max(0.0f, settings.locomotionTuning.z);
    const float minSurfaceControl = std::clamp(settings.surfaceTuning.x, 0.0f, 1.0f);
    const float slideGravity = glm::max(0.0f, settings.surfaceTuning.y);
    const float platformCarry = std::clamp(settings.surfaceTuning.z, 0.0f, 3.0f);

    if (settings.enableMouseLook) {
        ApplyMouseLook(state.pitch, state.yaw, lookSensitivity, maxMouseDelta, deltaTime, settings.requireMouseButton);
    }

    glm::vec3 move = GetMoveInputWASD(state.pitch, state.yaw);
    glm::vec3 planarForward(0.0f);
    glm::vec3 planarRight(0.0f);
    GetPlanarYawPitchVectors(state.pitch, state.yaw, planarForward, planarRight);
    glm::vec2 localInput(glm::dot(move, planarRight), glm::dot(move, planarForward));
    if (glm::length(localInput) > 1.0f) {
        localInput = glm::normalize(localInput);
    }
    float targetSpeed = IsSprintDown() ? runSpeed : walkSpeed;
    glm::vec2 targetLocalVelocity = localInput * targetSpeed;
    glm::vec3 velocity(0.0f);
    float capsuleHalf = std::max(0.1f, height * 0.5f);

    glm::vec3 physVel;
    bool havePhysVel = GetRigidbodyVelocity(physVel);
    if (havePhysVel) state.verticalVelocity = physVel.y;

    glm::vec3 hitPos(0.0f);
    glm::vec3 hitNormal(0.0f, 1.0f, 0.0f);
    glm::vec3 hitActorVelocity(0.0f);
    int hitActorId = -1;
    float hitStaticFriction = 0.9f;
    float hitDynamicFriction = 0.9f;
    bool hitGround = false;
    bool grounded = ResolveGround(capsuleHalf, probeExtra, groundSnap, state.verticalVelocity,
                                  &hitPos, &hitGround, &hitNormal, &hitActorId,
                                  &hitActorVelocity, &hitStaticFriction, &hitDynamicFriction);

    (void)hitActorId;
    (void)hitStaticFriction;
    float dynamicFriction = std::clamp(hitDynamicFriction, 0.0f, 2.0f);
    float grip = grounded ? std::clamp(dynamicFriction, minSurfaceControl, 1.0f) : 1.0f;

    float accelRate = grounded ? groundAccel * grip : airAccel;
    state.localVelocity = (accelRate > 0.0f)
        ? moveTowardsVec2(state.localVelocity, targetLocalVelocity, accelRate * deltaTime)
        : targetLocalVelocity;

    if (glm::dot(localInput, localInput) < 1e-4f && braking > 0.0f) {
        float brakeScale = grounded ? (0.5f + 0.5f * grip) : 0.15f;
        float damp = std::max(0.0f, 1.0f - braking * brakeScale * deltaTime);
        state.localVelocity *= damp;
    }

    float localSpeed = glm::length(state.localVelocity);
    if (localSpeed > targetSpeed && targetSpeed > 0.0f) {
        state.localVelocity *= (targetSpeed / localSpeed);
    }

    glm::vec3 platformVelocity = sanitizePlanar(hitActorVelocity);
    if (grounded && hitGround) {
        if (state.hasGroundSample && deltaTime > 1e-5f) {
            glm::vec3 pointVelocity = sanitizePlanar((hitPos - state.lastGroundHitPos) / deltaTime);
            if (glm::dot(pointVelocity, pointVelocity) < (120.0f * 120.0f)) {
                if (glm::dot(platformVelocity, platformVelocity) < 1e-4f) {
                    platformVelocity = pointVelocity;
                } else {
                    platformVelocity = glm::mix(platformVelocity, pointVelocity, 0.35f);
                }
            }
        }
        state.lastGroundHitPos = hitPos;
        state.hasGroundSample = true;
    } else {
        state.hasGroundSample = false;
    }

    if (grounded && hitGround) {
        glm::vec3 n = glm::normalize(hitNormal);
        if (std::isfinite(n.x) && std::isfinite(n.y) && std::isfinite(n.z)) {
            glm::vec3 gravityDir(0.0f, -1.0f, 0.0f);
            glm::vec3 downSlope = gravityDir - n * glm::dot(gravityDir, n);
            float downLen = glm::length(downSlope);
            if (downLen > 1e-4f) {
                downSlope /= downLen;
                float slopeFactor = std::clamp((1.0f - n.y) * 3.0f, 0.0f, 1.5f);
                float slip = std::clamp(1.0f - dynamicFriction * 0.85f, 0.0f, 1.0f);
                float slideAccel = slideGravity * slopeFactor * (0.35f + slip);
                state.slideVelocity += downSlope * slideAccel * deltaTime;
            }
        }
        float slideDamp = std::clamp((dynamicFriction + 0.15f) * 6.0f, 0.5f, 12.0f);
        state.slideVelocity *= std::max(0.0f, 1.0f - slideDamp * deltaTime);
    } else {
        state.slideVelocity *= std::max(0.0f, 1.0f - 4.0f * deltaTime);
    }
    state.slideVelocity = sanitizePlanar(state.slideVelocity);

    if (grounded) {
        state.verticalVelocity = 0.0f;
        if (!havePhysVel && hitGround) {
            object->position.y = std::max(object->position.y, hitPos.y + capsuleHalf);
        }
        if (IsJumpDown()) {
            state.verticalVelocity = jumpStrength;
            state.hasGroundSample = false;
        }
    } else {
        state.verticalVelocity += gravity * deltaTime;
    }

    platformVelocity = grounded ? platformVelocity * platformCarry : glm::vec3(0.0f);
    glm::vec3 planarVelocity =
        planarRight * state.localVelocity.x +
        planarForward * state.localVelocity.y +
        platformVelocity +
        state.slideVelocity;

    state.verticalVelocity = std::clamp(state.verticalVelocity, -maxFall, maxFall);
    velocity.x = planarVelocity.x;
    velocity.z = planarVelocity.z;
    velocity.y = state.verticalVelocity;
    glm::vec3 rotation(state.pitch, state.yaw, 0.0f);
    SetRotation(rotation);
    SetRigidbodyRotation(rotation);
    ApplyVelocity(velocity, deltaTime);

    if (debug) {
        debug->velocity = velocity;
        debug->localVelocity = state.localVelocity;
        debug->platformVelocity = platformVelocity;
        debug->surfaceFriction = dynamicFriction;
        float slopeDegrees = 0.0f;
        if (hitGround) {
            slopeDegrees = glm::degrees(std::acos(std::clamp(hitNormal.y, -1.0f, 1.0f)));
        }
        debug->slopeDegrees = slopeDegrees;
        debug->grounded = grounded;
    }
}

bool ScriptContext::IsUIButtonPressed() const {
    return object && object->hasUI && object->ui.type == UIElementType::Button && object->ui.buttonPressed;
}

bool ScriptContext::IsUIHovered() const {
    return object && object->hasUI && object->ui.uiHovered;
}

bool ScriptContext::IsUIActive() const {
    return object && object->hasUI && object->ui.uiActive;
}

bool ScriptContext::IsUIInteractable() const {
    return object ? object->ui.interactable : false;
}

void ScriptContext::SetUIInteractable(bool interactable) {
    if (!object) return;
    if (object->ui.interactable != interactable) {
        object->ui.interactable = interactable;
        MarkDirty();
    }
}

float ScriptContext::GetUISliderValue() const {
    if (!object || !object->hasUI || object->ui.type != UIElementType::Slider) return 0.0f;
    return object->ui.sliderValue;
}

void ScriptContext::SetUISliderValue(float value) {
    if (!object || !object->hasUI || object->ui.type != UIElementType::Slider) return;
    float clamped = std::clamp(value, object->ui.sliderMin, object->ui.sliderMax);
    if (object->ui.sliderValue != clamped) {
        object->ui.sliderValue = clamped;
        MarkDirty();
    }
}

void ScriptContext::SetUISliderRange(float minValue, float maxValue) {
    if (!object || !object->hasUI || object->ui.type != UIElementType::Slider) return;
    if (maxValue < minValue) std::swap(minValue, maxValue);
    object->ui.sliderMin = minValue;
    object->ui.sliderMax = maxValue;
    object->ui.sliderValue = std::clamp(object->ui.sliderValue, minValue, maxValue);
    MarkDirty();
}

void ScriptContext::SetUILabel(const std::string& label) {
    if (!object) return;
    if (object->ui.label != label) {
        object->ui.label = label;
        MarkDirty();
    }
}

void ScriptContext::SetUIColor(const glm::vec4& color) {
    if (!object) return;
    if (object->ui.color != color) {
        object->ui.color = color;
        MarkDirty();
    }
}

int ScriptContext::GetSpriteClipCount() const {
    if (!object || !object->hasUI) return 0;
    if (object->ui.spriteCustomFramesEnabled && !object->ui.spriteCustomFrames.empty()) {
        return static_cast<int>(object->ui.spriteCustomFrames.size());
    }
    if (!object->ui.spriteSheetEnabled) return 0;
    return std::max(1, object->ui.spriteSheetColumns * object->ui.spriteSheetRows);
}

int ScriptContext::GetSpriteClipIndex() const {
    int clipCount = GetSpriteClipCount();
    if (clipCount <= 0 || !object) return -1;
    return std::clamp(object->ui.spriteSheetFrame, 0, clipCount - 1);
}

std::string ScriptContext::GetSpriteClipName() const {
    return GetSpriteClipNameAt(GetSpriteClipIndex());
}

std::string ScriptContext::GetSpriteClipNameAt(int index) const {
    if (!object || !object->hasUI || index < 0) return {};
    if (object->ui.spriteCustomFramesEnabled && !object->ui.spriteCustomFrames.empty()) {
        if (index >= static_cast<int>(object->ui.spriteCustomFrames.size())) return {};
        if (index < static_cast<int>(object->ui.spriteCustomFrameNames.size()) &&
            !object->ui.spriteCustomFrameNames[static_cast<size_t>(index)].empty()) {
            return object->ui.spriteCustomFrameNames[static_cast<size_t>(index)];
        }
        return "Clip " + std::to_string(index);
    }
    int clipCount = GetSpriteClipCount();
    if (index >= clipCount) return {};
    return "Frame " + std::to_string(index);
}

bool ScriptContext::SetSpriteClipIndex(int index) {
    if (!object || !object->hasUI) return false;
    int clipCount = GetSpriteClipCount();
    if (clipCount <= 0 || index < 0 || index >= clipCount) return false;
    if (object->ui.spriteSheetFrame != index) {
        object->ui.spriteSheetFrame = index;
        MarkDirty();
    }
    return true;
}

bool ScriptContext::SetSpriteClipName(const std::string& name) {
    if (!object || !object->hasUI) return false;
    if (!(object->ui.spriteCustomFramesEnabled && !object->ui.spriteCustomFrames.empty())) return false;
    std::string target = trimString(name);
    if (target.empty()) return false;
    for (size_t i = 0; i < object->ui.spriteCustomFrames.size(); ++i) {
        std::string clipName = (i < object->ui.spriteCustomFrameNames.size() &&
                                !object->ui.spriteCustomFrameNames[i].empty())
            ? object->ui.spriteCustomFrameNames[i]
            : ("Clip " + std::to_string(i));
        if (clipName == target) {
            return SetSpriteClipIndex(static_cast<int>(i));
        }
    }
    return false;
}

float ScriptContext::GetSpriteAlpha() const {
    if (!object || !object->hasUI) return 1.0f;
    return std::clamp(object->ui.color.a, 0.0f, 1.0f);
}

void ScriptContext::SetSpriteAlpha(float alpha) {
    if (!object || !object->hasUI) return;
    float clamped = std::clamp(alpha, 0.0f, 1.0f);
    if (std::abs(object->ui.color.a - clamped) < 1e-6f) return;
    object->ui.color.a = clamped;
    MarkDirty();
}

bool ScriptContext::FadeSpriteAlpha(float targetAlpha, float duration, float deltaTime) {
    if (!object || !object->hasUI) return false;
    std::string key = makeRuntimeStateKey(*this, "sprite_alpha_fade");
    targetAlpha = std::clamp(targetAlpha, 0.0f, 1.0f);
    duration = std::max(0.0f, duration);
    float currentAlpha = GetSpriteAlpha();

    if (duration <= 1e-5f) {
        SetSpriteAlpha(targetAlpha);
        gSpriteAlphaFadeStates.erase(key);
        return true;
    }

    auto it = gSpriteAlphaFadeStates.find(key);
    bool restart = (it == gSpriteAlphaFadeStates.end());
    if (!restart) {
        const SpriteAlphaFadeState& state = it->second;
        if (std::abs(state.targetAlpha - targetAlpha) > 1e-4f ||
            std::abs(state.duration - duration) > 1e-4f) {
            restart = true;
        }
    }

    if (restart) {
        SpriteAlphaFadeState state;
        state.startAlpha = currentAlpha;
        state.targetAlpha = targetAlpha;
        state.duration = duration;
        state.elapsed = 0.0f;
        it = gSpriteAlphaFadeStates.insert_or_assign(key, state).first;
    }

    SpriteAlphaFadeState& state = it->second;
    state.elapsed = std::min(state.duration, state.elapsed + std::max(0.0f, deltaTime));
    float t = (state.duration <= 1e-5f) ? 1.0f : std::clamp(state.elapsed / state.duration, 0.0f, 1.0f);
    SetSpriteAlpha(glm::mix(state.startAlpha, state.targetAlpha, t));
    if (t >= 1.0f - 1e-6f) {
        gSpriteAlphaFadeStates.erase(it);
        return true;
    }
    return false;
}

bool ScriptContext::FadeSpriteToClipIndex(int clipIndex, float fadeOutDuration, float fadeInDuration, float deltaTime) {
    if (!object || !object->hasUI) return false;
    const int clipCount = GetSpriteClipCount();
    if (clipCount <= 0 || clipIndex < 0 || clipIndex >= clipCount) return false;

    std::string key = makeRuntimeStateKey(*this, "sprite_clip_fade");
    auto it = gSpriteClipFadeStates.find(key);

    if (it == gSpriteClipFadeStates.end() && GetSpriteClipIndex() == clipIndex) {
        return true;
    }

    fadeOutDuration = std::max(0.0f, fadeOutDuration);
    fadeInDuration = std::max(0.0f, fadeInDuration);
    float currentAlpha = GetSpriteAlpha();

    bool restart = (it == gSpriteClipFadeStates.end());
    if (!restart) {
        const SpriteClipFadeState& state = it->second;
        if (state.targetClipIndex != clipIndex ||
            std::abs(state.fadeOutDuration - fadeOutDuration) > 1e-4f ||
            std::abs(state.fadeInDuration - fadeInDuration) > 1e-4f) {
            restart = true;
        }
    }

    if (restart) {
        gSpriteAlphaFadeStates.erase(makeRuntimeStateKey(*this, "sprite_alpha_fade"));
        SpriteClipFadeState state;
        state.targetClipIndex = clipIndex;
        state.fadeOutDuration = fadeOutDuration;
        state.fadeInDuration = fadeInDuration;
        state.baseAlpha = std::clamp(currentAlpha, 0.0f, 1.0f);
        state.phaseStartAlpha = currentAlpha;
        state.elapsed = 0.0f;
        if (fadeOutDuration <= 1e-5f) {
            if (!SetSpriteClipIndex(clipIndex)) {
                return false;
            }
            if (fadeInDuration <= 1e-5f) {
                return true;
            }
            state.phase = SpriteClipFadePhase::FadingIn;
            state.phaseStartAlpha = 0.0f;
            SetSpriteAlpha(0.0f);
        } else {
            state.phase = SpriteClipFadePhase::FadingOut;
        }
        it = gSpriteClipFadeStates.insert_or_assign(key, state).first;
    }

    SpriteClipFadeState& state = it->second;
    float dt = std::max(0.0f, deltaTime);
    if (state.phase == SpriteClipFadePhase::FadingOut) {
        state.elapsed = std::min(state.fadeOutDuration, state.elapsed + dt);
        float t = (state.fadeOutDuration <= 1e-5f) ? 1.0f : std::clamp(state.elapsed / state.fadeOutDuration, 0.0f, 1.0f);
        SetSpriteAlpha(glm::mix(state.phaseStartAlpha, 0.0f, t));
        if (t >= 1.0f - 1e-6f) {
            if (!SetSpriteClipIndex(state.targetClipIndex)) {
                gSpriteClipFadeStates.erase(it);
                return false;
            }
            if (state.fadeInDuration <= 1e-5f) {
                SetSpriteAlpha(state.baseAlpha);
                gSpriteClipFadeStates.erase(it);
                return true;
            }
            state.phase = SpriteClipFadePhase::FadingIn;
            state.phaseStartAlpha = 0.0f;
            state.elapsed = 0.0f;
        }
    }

    if (state.phase == SpriteClipFadePhase::FadingIn) {
        state.elapsed = std::min(state.fadeInDuration, state.elapsed + dt);
        float t = (state.fadeInDuration <= 1e-5f) ? 1.0f : std::clamp(state.elapsed / state.fadeInDuration, 0.0f, 1.0f);
        SetSpriteAlpha(glm::mix(state.phaseStartAlpha, state.baseAlpha, t));
        if (t >= 1.0f - 1e-6f) {
            SetSpriteAlpha(state.baseAlpha);
            gSpriteClipFadeStates.erase(it);
            return true;
        }
    }

    return false;
}

bool ScriptContext::FadeSpriteToClipName(const std::string& clipName,
                                         float fadeOutDuration, float fadeInDuration, float deltaTime) {
    std::string target = trimString(clipName);
    if (target.empty()) return false;
    const int clipCount = GetSpriteClipCount();
    for (int i = 0; i < clipCount; ++i) {
        if (GetSpriteClipNameAt(i) == target) {
            return FadeSpriteToClipIndex(i, fadeOutDuration, fadeInDuration, deltaTime);
        }
    }
    return false;
}

float ScriptContext::GetUITextScale() const {
    if (!object || !object->hasUI || object->ui.type != UIElementType::Text) return 1.0f;
    return object->ui.textScale;
}

void ScriptContext::SetUITextScale(float scale) {
    if (!object || !object->hasUI || object->ui.type != UIElementType::Text) return;
    float clamped = std::max(0.1f, scale);
    if (object->ui.textScale != clamped) {
        object->ui.textScale = clamped;
        MarkDirty();
    }
}

void ScriptContext::SetUISliderStyle(UISliderStyle style) {
    if (!object || !object->hasUI || object->ui.type != UIElementType::Slider) return;
    if (object->ui.sliderStyle != style) {
        object->ui.sliderStyle = style;
        MarkDirty();
    }
}

void ScriptContext::SetUIButtonStyle(UIButtonStyle style) {
    if (!object || !object->hasUI || object->ui.type != UIElementType::Button) return;
    if (object->ui.buttonStyle != style) {
        object->ui.buttonStyle = style;
        MarkDirty();
    }
}

void ScriptContext::SetUIStylePreset(const std::string& name) {
    if (!object || name.empty()) return;
    if (object->ui.stylePreset != name) {
        object->ui.stylePreset = name;
        MarkDirty();
    }
}

void ScriptContext::RegisterUIStylePreset(const std::string& name, const ImGuiStyle& style, bool replace) {
    if (engine) {
        engine->registerUIStylePresetFromScript(name, style, replace);
    }
}

void ScriptContext::SetFPSCap(bool enabled, float cap) {
    if (engine) {
        engine->setFrameRateCapFromScript(enabled, cap);
    }
}

bool ScriptContext::HasRigidbody() const {
    return object && object->hasRigidbody && object->rigidbody.enabled;
}

bool ScriptContext::HasRigidbody2D() const {
    return object && UsesUIOnly2DPhysics(*object) &&
           object->hasRigidbody2D && object->rigidbody2D.enabled;
}

bool ScriptContext::EnsureCapsuleCollider(float height, float radius) {
    if (!object) return false;
    bool changed = false;
    if (!object->hasCollider) {
        object->hasCollider = true;
        changed = true;
    }
    ColliderComponent& col = object->collider;
    if (!col.enabled) {
        col.enabled = true;
        changed = true;
    }
    if (col.type != ColliderType::Capsule) {
        col.type = ColliderType::Capsule;
        changed = true;
    }
    if (!col.convex) {
        col.convex = true;
        changed = true;
    }
    glm::vec3 size(radius * 2.0f, height, radius * 2.0f);
    if (col.boxSize != size) {
        col.boxSize = size;
        changed = true;
    }
    if (changed) {
        MarkDirty();
    }
    return true;
}

bool ScriptContext::EnsureRigidbody(bool useGravity, bool kinematic) {
    if (!object) return false;
    bool changed = false;
    if (!object->hasRigidbody) {
        object->hasRigidbody = true;
        changed = true;
    }
    RigidbodyComponent& rb = object->rigidbody;
    if (!rb.enabled) {
        rb.enabled = true;
        changed = true;
    }
    if (rb.useGravity != useGravity) {
        rb.useGravity = useGravity;
        changed = true;
    }
    if (rb.isKinematic != kinematic) {
        rb.isKinematic = kinematic;
        changed = true;
    }
    if (changed) {
        MarkDirty();
    }
    return true;
}

bool ScriptContext::SetRigidbody2DVelocity(const glm::vec2& velocity) {
    if (!object || !HasRigidbody2D()) return false;
    object->rigidbody2D.velocity = velocity;
    MarkDirty();
    return true;
}

bool ScriptContext::GetRigidbody2DVelocity(glm::vec2& outVelocity) const {
    if (!object || !HasRigidbody2D()) return false;
    outVelocity = object->rigidbody2D.velocity;
    return true;
}

bool ScriptContext::SetRigidbodyVelocity(const glm::vec3& velocity) {
    if (!engine || !object || !HasRigidbody()) return false;
    return engine->setRigidbodyVelocityFromScript(object->id, velocity);
}

bool ScriptContext::GetRigidbodyVelocity(glm::vec3& outVelocity) const {
    if (!engine || !object || !HasRigidbody()) return false;
    return engine->getRigidbodyVelocityFromScript(object->id, outVelocity);
}

bool ScriptContext::AddRigidbodyVelocity(const glm::vec3& deltaVelocity) {
    glm::vec3 current;
    if (!GetRigidbodyVelocity(current)) return false;
    return SetRigidbodyVelocity(current + deltaVelocity);
}

bool ScriptContext::SetRigidbodyAngularVelocity(const glm::vec3& velocity) {
    if (!engine || !object || !HasRigidbody()) return false;
    return engine->setRigidbodyAngularVelocityFromScript(object->id, velocity);
}

bool ScriptContext::GetRigidbodyAngularVelocity(glm::vec3& outVelocity) const {
    if (!engine || !object || !HasRigidbody()) return false;
    return engine->getRigidbodyAngularVelocityFromScript(object->id, outVelocity);
}

bool ScriptContext::AddRigidbodyForce(const glm::vec3& force) {
    if (!engine || !object || !HasRigidbody()) return false;
    return engine->addRigidbodyForceFromScript(object->id, force);
}

bool ScriptContext::AddRigidbodyImpulse(const glm::vec3& impulse) {
    if (!engine || !object || !HasRigidbody()) return false;
    return engine->addRigidbodyImpulseFromScript(object->id, impulse);
}

bool ScriptContext::AddRigidbodyTorque(const glm::vec3& torque) {
    if (!engine || !object || !HasRigidbody()) return false;
    return engine->addRigidbodyTorqueFromScript(object->id, torque);
}

bool ScriptContext::AddRigidbodyAngularImpulse(const glm::vec3& impulse) {
    if (!engine || !object || !HasRigidbody()) return false;
    return engine->addRigidbodyAngularImpulseFromScript(object->id, impulse);
}

bool ScriptContext::SetObjectRigidbodyVelocity(int objectId, const glm::vec3& velocity) {
    if (!engine || objectId < 0) return false;
    return engine->setRigidbodyVelocityFromScript(objectId, velocity);
}

bool ScriptContext::GetObjectRigidbodyVelocity(int objectId, glm::vec3& outVelocity) const {
    if (!engine || objectId < 0) return false;
    return engine->getRigidbodyVelocityFromScript(objectId, outVelocity);
}

bool ScriptContext::AddObjectRigidbodyImpulse(int objectId, const glm::vec3& impulse) {
    if (!engine || objectId < 0) return false;
    return engine->addRigidbodyImpulseFromScript(objectId, impulse);
}

bool ScriptContext::TeleportObjectRigidbody(int objectId, const glm::vec3& pos, const glm::vec3& rotDeg) {
    if (!engine || objectId < 0) return false;
    return engine->teleportPhysicsActorFromScript(objectId, pos, NormalizeEulerDegrees(rotDeg));
}

bool ScriptContext::SetRigidbodyYaw(float yawDegrees) {
    if (!engine || !object || !HasRigidbody()) return false;
    return engine->setRigidbodyYawFromScript(object->id, yawDegrees);
}

float ScriptContext::GetProjectGravityScale() const {
    if (!engine) return 1.0f;
    return engine->getProjectGravityScaleFromScript();
}

void ScriptContext::SetProjectGravityScale(float scale) {
    if (!engine) return;
    engine->setProjectGravityScaleFromScript(scale);
}

bool ScriptContext::RaycastClosest(const glm::vec3& origin, const glm::vec3& dir, float distance,
                                   glm::vec3* hitPos, glm::vec3* hitNormal, float* hitDistance) const {
    return RaycastClosestDetailed(origin, dir, distance, hitPos, hitNormal, hitDistance);
}

bool ScriptContext::RaycastClosestDetailed(const glm::vec3& origin, const glm::vec3& dir, float distance,
                                           glm::vec3* hitPos, glm::vec3* hitNormal, float* hitDistance,
                                           int* hitObjectId, glm::vec3* hitObjectVelocity,
                                           float* hitStaticFriction, float* hitDynamicFriction) const {
    if (!engine) return false;
    int ignoreId = object ? object->id : -1;
    return engine->raycastClosestFromScript(origin, dir, distance, ignoreId, hitPos, hitNormal, hitDistance,
                                            hitObjectId, hitObjectVelocity,
                                            hitStaticFriction, hitDynamicFriction);
}

bool ScriptContext::SetRigidbodyRotation(const glm::vec3& rotDeg) {
    if (!engine || !object || !HasRigidbody()) return false;
    object->rotation = NormalizeEulerDegrees(rotDeg);
    engine->syncLocalTransform(*object);
    MarkDirty();
    return engine->teleportPhysicsActorFromScript(object->id, object->position, object->rotation);
}

bool ScriptContext::TeleportRigidbody(const glm::vec3& pos, const glm::vec3& rotDeg) {
    if (!engine || !object) return false;
    object->position = pos;
    object->rotation = NormalizeEulerDegrees(rotDeg);
    engine->syncLocalTransform(*object);
    MarkDirty();
    return engine->teleportPhysicsActorFromScript(object->id, pos, object->rotation);
}

bool ScriptContext::HasAudioSource() const {
    return object && object->hasAudioSource && object->audioSource.enabled;
}

bool ScriptContext::PlayAudio() {
    if (!engine || !object || !object->hasAudioSource) return false;
    return engine->playAudioFromScript(object->id);
}

bool ScriptContext::StopAudio() {
    if (!engine || !object || !object->hasAudioSource) return false;
    return engine->stopAudioFromScript(object->id);
}

bool ScriptContext::SetAudioLoop(bool loop) {
    if (!engine || !object || !object->hasAudioSource) return false;
    object->audioSource.loop = loop;
    engine->markProjectDirty();
    return engine->setAudioLoopFromScript(object->id, loop);
}

bool ScriptContext::SetAudioVolume(float volume) {
    if (!engine || !object || !object->hasAudioSource) return false;
    float clamped = std::clamp(volume, 0.0f, 2.0f);
    object->audioSource.volume = clamped;
    engine->markProjectDirty();
    return engine->setAudioVolumeFromScript(object->id, clamped);
}

bool ScriptContext::SetAudioClip(const std::string& path) {
    if (!engine || !object || !object->hasAudioSource) return false;
    object->audioSource.clipPath = path;
    engine->markProjectDirty();
    return engine->setAudioClipFromScript(object->id, path);
}

bool ScriptContext::PlayAudioOneShot(const std::string& clipPath, float volumeScale) {
    if (!engine || !object || !object->hasAudioSource) return false;
    return engine->playAudioOneShotFromScript(object->id, clipPath, std::max(0.0f, volumeScale));
}

bool ScriptContext::HasAnimation() const {
    if (!engine || !object) return false;
    return engine->hasAnimationFromScript(object->id);
}

bool ScriptContext::PlayAnimation(bool restart) {
    if (!engine || !object) return false;
    return engine->playAnimationFromScript(object->id, restart);
}

bool ScriptContext::StopAnimation(bool resetTime) {
    if (!engine || !object) return false;
    return engine->stopAnimationFromScript(object->id, resetTime);
}

bool ScriptContext::PauseAnimation(bool pause) {
    if (!engine || !object) return false;
    return engine->pauseAnimationFromScript(object->id, pause);
}

bool ScriptContext::ReverseAnimation(bool restartIfStopped) {
    if (!engine || !object) return false;
    return engine->reverseAnimationFromScript(object->id, restartIfStopped);
}

bool ScriptContext::SetAnimationTime(float timeSeconds) {
    if (!engine || !object) return false;
    return engine->setAnimationTimeFromScript(object->id, timeSeconds);
}

float ScriptContext::GetAnimationTime() const {
    if (!engine || !object) return 0.0f;
    return engine->getAnimationTimeFromScript(object->id);
}

bool ScriptContext::IsAnimationPlaying() const {
    if (!engine || !object) return false;
    return engine->isAnimationPlayingFromScript(object->id);
}

bool ScriptContext::SetAnimationLoop(bool loop) {
    if (!engine || !object) return false;
    return engine->setAnimationLoopFromScript(object->id, loop);
}

bool ScriptContext::SetAnimationPlaySpeed(float speed) {
    if (!engine || !object) return false;
    return engine->setAnimationPlaySpeedFromScript(object->id, speed);
}

bool ScriptContext::SetAnimationPlayOnAwake(bool playOnAwake) {
    if (!engine || !object) return false;
    return engine->setAnimationPlayOnAwakeFromScript(object->id, playOnAwake);
}

std::string ScriptContext::GetSetting(const std::string& key, const std::string& fallback) const {
    if (!script) return fallback;
    auto it = std::find_if(script->settings.begin(), script->settings.end(),
        [&](const ScriptSetting& s){ return s.key == key; });
    return (it != script->settings.end()) ? it->value : fallback;
}

void ScriptContext::SetSetting(const std::string& key, const std::string& value) {
    if (!script) return;
    auto it = std::find_if(script->settings.begin(), script->settings.end(),
        [&](const ScriptSetting& s){ return s.key == key; });
    if (it != script->settings.end()) {
        it->value = value;
    } else {
        script->settings.push_back({key, value});
    }
    MarkDirty();
}

bool ScriptContext::GetSettingBool(const std::string& key, bool fallback) const {
    std::string v = GetSetting(key, fallback ? "1" : "0");
    if (v == "1" || v == "true" || v == "True") return true;
    if (v == "0" || v == "false" || v == "False") return false;
    return fallback;
}

void ScriptContext::SetSettingBool(const std::string& key, bool value) {
    SetSetting(key, value ? "1" : "0");
}

float ScriptContext::GetSettingFloat(const std::string& key, float fallback) const {
    std::string v = GetSetting(key, "");
    if (v.empty()) return fallback;
    try { return std::stof(v); } catch (...) {}
    return fallback;
}

void ScriptContext::SetSettingFloat(const std::string& key, float value) {
    SetSetting(key, std::to_string(value));
}

glm::vec3 ScriptContext::GetSettingVec3(const std::string& key, const glm::vec3& fallback) const {
    std::string v = GetSetting(key, "");
    if (v.empty()) return fallback;
    glm::vec3 out = fallback;
    std::stringstream ss(v);
    std::string part;
    for (int i = 0; i < 3 && std::getline(ss, part, ','); ++i) {
        try { out[i] = std::stof(part); } catch (...) {}
    }
    return out;
}

void ScriptContext::SetSettingVec3(const std::string& key, const glm::vec3& value) {
    SetSetting(key,
        std::to_string(value.x) + "," +
        std::to_string(value.y) + "," +
        std::to_string(value.z));
}

void ScriptContext::AddConsoleMessage(const std::string& message, ConsoleMessageType type) {
    if (engine) {
        engine->addConsoleMessageFromScript(message, type);
    }
}

std::string ScriptContext::HttpPost(const std::string& url,
                                    const std::string& contentType,
                                    const std::string& body,
                                    const std::string& headers) {
    if (!engine) {
        return "No engine available";
    }
    return engine->httpPostFromScript(url, contentType, body, headers);
}

int ScriptContext::StartHttpPost(const std::string& url,
                                 const std::string& contentType,
                                 const std::string& body,
                                 const std::string& headers,
                                 bool stream) {
    if (!engine) {
        return 0;
    }
    return engine->startHttpPostFromScript(url, contentType, body, headers, stream);
}

bool ScriptContext::PollHttpPost(int requestId, std::string& outChunk, bool& outDone, bool& outSuccess) {
    outChunk.clear();
    outDone = true;
    outSuccess = false;
    if (!engine || requestId <= 0) {
        return false;
    }
    return engine->pollHttpPostFromScript(requestId, outChunk, outDone, outSuccess);
}

void ScriptContext::CancelHttpPost(int requestId) {
    if (!engine || requestId <= 0) {
        return;
    }
    engine->cancelHttpPostFromScript(requestId);
}

std::string ScriptContext::ReadFileText(const std::string& path) const {
    if (!engine) {
        return {};
    }
    return engine->readFileTextFromScript(path);
}

bool ScriptContext::WriteFileText(const std::string& path, const std::string& content) {
    if (!engine) {
        return false;
    }
    return engine->writeFileTextFromScript(path, content);
}

bool ScriptContext::DeleteFile(const std::string& path) {
    if (!engine) {
        return false;
    }
    return engine->deleteFileFromScript(path);
}

std::string ScriptContext::ListFiles(const std::string& path, bool recursive, int maxEntries) const {
    if (!engine) {
        return {};
    }
    return engine->listFilesFromScript(path, recursive, maxEntries);
}

std::string ScriptContext::SearchFiles(const std::string& root,
                                       const std::string& query,
                                       int maxResults) const {
    if (!engine) {
        return {};
    }
    return engine->searchFilesFromScript(root, query, maxResults);
}

std::string ScriptContext::GetProgramRootPath() const {
    if (!engine) {
        return {};
    }
    return engine->getProgramRootPathFromScript().string();
}

std::string ScriptContext::GetEngineDocsRootPath() const {
    if (!engine) {
        return {};
    }
    return engine->getEngineDocsRootPathFromScript().string();
}

bool ScriptContext::SaveProject() {
    if (!engine) {
        return false;
    }
    return engine->saveProjectFromScript();
}

void ScriptContext::AutoSetting(const std::string& key, bool& value) {
    if (!script) return;
    if (autoSettings.end() != std::find_if(autoSettings.begin(), autoSettings.end(),
        [&](const AutoSettingEntry& e){ return e.key == key; })) return;

    static std::unordered_map<std::string, bool> defaults;
    std::string scriptId = makeScriptInstanceKey(*this);
    std::string id = scriptId + "|" + key;
    bool defaultVal = value;
    auto itDef = defaults.find(id);
    if (itDef != defaults.end()) {
        defaultVal = itDef->second;
    } else {
        defaults[id] = defaultVal; // capture first-seen initializer for this module/key
    }

    value = GetSettingBool(key, defaultVal);
    AutoSettingEntry entry;
    entry.type = AutoSettingType::Bool;
    entry.key = key;
    entry.ptr = &value;
    entry.initialBool = value;
    autoSettings.push_back(entry);
}

void ScriptContext::AutoSetting(const std::string& key, float& value) {
    if (!script) return;
    if (autoSettings.end() != std::find_if(autoSettings.begin(), autoSettings.end(),
        [&](const AutoSettingEntry& e){ return e.key == key; })) return;

    static std::unordered_map<std::string, float> defaults;
    std::string scriptId = makeScriptInstanceKey(*this);
    std::string id = scriptId + "|" + key;
    float defaultVal = value;
    auto itDef = defaults.find(id);
    if (itDef != defaults.end()) {
        defaultVal = itDef->second;
    } else {
        defaults[id] = defaultVal;
    }

    value = GetSettingFloat(key, defaultVal);
    AutoSettingEntry entry;
    entry.type = AutoSettingType::Float;
    entry.key = key;
    entry.ptr = &value;
    entry.initialFloat = value;
    autoSettings.push_back(entry);
}

void ScriptContext::AutoSetting(const std::string& key, int& value) {
    if (!script) return;
    if (autoSettings.end() != std::find_if(autoSettings.begin(), autoSettings.end(),
        [&](const AutoSettingEntry& e){ return e.key == key; })) return;

    static std::unordered_map<std::string, int> defaults;
    std::string scriptId = makeScriptInstanceKey(*this);
    std::string id = scriptId + "|" + key;
    int defaultVal = value;
    auto itDef = defaults.find(id);
    if (itDef != defaults.end()) {
        defaultVal = itDef->second;
    } else {
        defaults[id] = defaultVal;
    }

    const std::string raw = GetSetting(key, std::to_string(defaultVal));
    try {
        value = std::stoi(raw);
    } catch (...) {
        value = defaultVal;
    }
    AutoSettingEntry entry;
    entry.type = AutoSettingType::Int;
    entry.key = key;
    entry.ptr = &value;
    entry.initialInt = value;
    autoSettings.push_back(entry);
}

void ScriptContext::AutoSetting(const std::string& key, glm::vec3& value) {
    if (!script) return;
    if (autoSettings.end() != std::find_if(autoSettings.begin(), autoSettings.end(),
        [&](const AutoSettingEntry& e){ return e.key == key; })) return;

    static std::unordered_map<std::string, glm::vec3> defaults;
    std::string scriptId = makeScriptInstanceKey(*this);
    std::string id = scriptId + "|" + key;
    glm::vec3 defaultVal = value;
    auto itDef = defaults.find(id);
    if (itDef != defaults.end()) {
        defaultVal = itDef->second;
    } else {
        defaults[id] = defaultVal;
    }

    value = GetSettingVec3(key, defaultVal);
    AutoSettingEntry entry;
    entry.type = AutoSettingType::Vec3;
    entry.key = key;
    entry.ptr = &value;
    entry.initialVec3 = value;
    autoSettings.push_back(entry);
}

void ScriptContext::AutoSetting(const std::string& key, char* buffer, size_t bufferSize) {
    if (!script || !buffer || bufferSize == 0) return;
    if (autoSettings.end() != std::find_if(autoSettings.begin(), autoSettings.end(),
        [&](const AutoSettingEntry& e){ return e.key == key; })) return;

    static std::unordered_map<std::string, std::string> defaults;
    std::string scriptId = makeScriptInstanceKey(*this);
    std::string id = scriptId + "|" + key;
    std::string defaultVal = defaults.count(id) ? defaults[id] : std::string(buffer);
    defaults.try_emplace(id, defaultVal);

    std::string existing = GetSetting(key, defaultVal);
    if (!existing.empty()) {
        std::snprintf(buffer, bufferSize, "%s", existing.c_str());
    } else if (!defaultVal.empty()) {
        std::snprintf(buffer, bufferSize, "%s", defaultVal.c_str());
    }
    AutoSettingEntry entry;
    entry.type = AutoSettingType::StringBuf;
    entry.key = key;
    entry.ptr = buffer;
    entry.bufSize = bufferSize;
    entry.initialString = buffer;
    autoSettings.push_back(entry);
}

void ScriptContext::AutoSetting(const std::string& key, std::string& value) {
    if (!script) return;
    if (autoSettings.end() != std::find_if(autoSettings.begin(), autoSettings.end(),
        [&](const AutoSettingEntry& e){ return e.key == key; })) return;

    static std::unordered_map<std::string, std::string> defaults;
    std::string scriptId = makeScriptInstanceKey(*this);
    std::string id = scriptId + "|" + key;
    std::string defaultVal = value;
    auto itDef = defaults.find(id);
    if (itDef != defaults.end()) {
        defaultVal = itDef->second;
    } else {
        defaults[id] = defaultVal;
    }

    value = GetSetting(key, defaultVal);
    AutoSettingEntry entry;
    entry.type = AutoSettingType::String;
    entry.key = key;
    entry.ptr = &value;
    entry.initialString = value;
    autoSettings.push_back(entry);
}

void ScriptContext::SaveAutoSettings() {
    if (!script) return;
    bool changed = false;
    for (const auto& e : autoSettings) {
        std::string newVal;
        switch (e.type) {
            case AutoSettingType::Bool: {
                bool cur = *static_cast<bool*>(e.ptr);
                if (cur == e.initialBool) continue;
                newVal = cur ? "1" : "0";
                break;
            }
            case AutoSettingType::Float: {
                float cur = *static_cast<float*>(e.ptr);
                if (std::abs(cur - e.initialFloat) < 1e-6f) continue;
                newVal = std::to_string(cur);
                break;
            }
            case AutoSettingType::Int: {
                int cur = *static_cast<int*>(e.ptr);
                if (cur == e.initialInt) continue;
                newVal = std::to_string(cur);
                break;
            }
            case AutoSettingType::Vec3: {
                glm::vec3 cur = *static_cast<glm::vec3*>(e.ptr);
                if (glm::all(glm::epsilonEqual(cur, e.initialVec3, 1e-6f))) continue;
                newVal = std::to_string(cur.x) + "," + std::to_string(cur.y) + "," + std::to_string(cur.z);
                break;
            }
            case AutoSettingType::StringBuf: {
                const char* cur = static_cast<const char*>(e.ptr);
                if (cur && e.initialString == cur) continue;
                newVal = cur ? cur : "";
                if (!cur || newVal == e.initialString) continue;
                break;
            }
            case AutoSettingType::String: {
                const std::string& cur = *static_cast<const std::string*>(e.ptr);
                if (cur == e.initialString) continue;
                newVal = cur;
                break;
            }
        }
        changed = true;
        SetSetting(e.key, newVal);
    }
    if (changed) {
        MarkDirty();
    }
}

void ScriptContext::StartIEnum(void(*fn)(ScriptContext&, float)) {
    if (!script || !fn) return;
    auto& v = script->activeIEnums;
    if (std::find(v.begin(), v.end(), reinterpret_cast<void*>(fn)) == v.end()) {
        v.push_back(reinterpret_cast<void*>(fn));
    }
}

void ScriptContext::StopIEnum(void(*fn)(ScriptContext&, float)) {
    if (!script || !fn) return;
    auto& v = script->activeIEnums;
    auto it = std::find(v.begin(), v.end(), reinterpret_cast<void*>(fn));
    if (it != v.end()) {
        v.erase(it);
    }
}

void ScriptContext::EnsureIEnum(void(*fn)(ScriptContext&, float)) {
    if (!IsIEnumRunning(fn)) StartIEnum(fn);
}

bool ScriptContext::IsIEnumRunning(void(*fn)(ScriptContext&, float)) const {
    if (!script || !fn) return false;
    auto it = std::find(script->activeIEnums.begin(), script->activeIEnums.end(),
                        reinterpret_cast<void*>(fn));
    return it != script->activeIEnums.end();
}

void ScriptContext::StopAllIEnums() {
    if (script) script->activeIEnums.clear();
}

void ScriptContext::MarkDirty() {
    if (engine) {
        engine->markProjectDirty();
    }
}

ScriptRuntime::InspectorFn ScriptRuntime::getInspector(const fs::path& binaryPath) {
    lastError.clear();
    Module* mod = getModule(binaryPath);
    return mod ? mod->inspector : nullptr;
}

ScriptRuntime::Module* ScriptRuntime::getModule(const fs::path& binaryPath) {
    lastError.clear();
    if (binaryPath.empty()) return nullptr;
    auto key = binaryPath.string();
    auto it = loaded.find(key);
    if (it != loaded.end()) {
        return &it->second;
        // Previously loaded but missing inspector; try reloading.
#if defined(_WIN32)
        if (it->second.handle) FreeLibrary(static_cast<HMODULE>(it->second.handle));
#else
        if (it->second.handle) dlclose(it->second.handle);
#endif
        loaded.erase(it);
    }

    Module mod{};
    auto unloadModuleHandle = [&](void* handle) {
        if (!handle) return;
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
    };
    const auto __moduLoadStart = std::chrono::steady_clock::now();
#if defined(_WIN32)
    fs::path loadPath = binaryPath;
    std::string shadowError;
    if (!prepareShadowScriptBinary(binaryPath, loadPath, shadowError)) {
        lastError = shadowError;
        return nullptr;
    }

    mod.loadedPath = loadPath;
    mod.loadedFromShadowCopy = true;
    mod.handle = LoadLibraryA(loadPath.string().c_str());
    if (!mod.handle) {
        lastError = "LoadLibrary failed";
        if (mod.loadedFromShadowCopy && !mod.loadedPath.empty()) {
            std::error_code removeEc;
            fs::remove(mod.loadedPath, removeEc);
        }
        return nullptr;
    }
    AbiVersionFn abiVersionFn = reinterpret_cast<AbiVersionFn>(
        GetProcAddress(static_cast<HMODULE>(mod.handle), "Modularity_ScriptAbiVersion"));
    mod.inspector = reinterpret_cast<InspectorFn>(GetProcAddress(static_cast<HMODULE>(mod.handle), "Script_OnInspector"));
    mod.begin = reinterpret_cast<BeginFn>(GetProcAddress(static_cast<HMODULE>(mod.handle), "Script_Begin"));
    mod.spec = reinterpret_cast<SpecFn>(GetProcAddress(static_cast<HMODULE>(mod.handle), "Script_Spec"));
    mod.testEditor = reinterpret_cast<TestEditorFn>(GetProcAddress(static_cast<HMODULE>(mod.handle), "Script_TestEditor"));
    mod.update = reinterpret_cast<UpdateFn>(GetProcAddress(static_cast<HMODULE>(mod.handle), "Script_Update"));
    mod.tickUpdate = reinterpret_cast<TickUpdateFn>(GetProcAddress(static_cast<HMODULE>(mod.handle), "Script_TickUpdate"));
    mod.editorRender = reinterpret_cast<EditorRenderFn>(GetProcAddress(static_cast<HMODULE>(mod.handle), "RenderEditorWindow"));
    mod.editorExit = reinterpret_cast<EditorExitFn>(GetProcAddress(static_cast<HMODULE>(mod.handle), "ExitRenderEditorWindow"));
#else
    mod.loadedPath = binaryPath;
    mod.handle = dlopen(binaryPath.string().c_str(), RTLD_LAZY);
    if (!mod.handle) {
        const char* err = dlerror();
        if (err) lastError = err;
        return nullptr;
    }
    dlerror();
    AbiVersionFn abiVersionFn = reinterpret_cast<AbiVersionFn>(dlsym(mod.handle, "Modularity_ScriptAbiVersion"));
    mod.inspector = reinterpret_cast<InspectorFn>(dlsym(mod.handle, "Script_OnInspector"));
    mod.begin = reinterpret_cast<BeginFn>(dlsym(mod.handle, "Script_Begin"));
    mod.spec = reinterpret_cast<SpecFn>(dlsym(mod.handle, "Script_Spec"));
    mod.testEditor = reinterpret_cast<TestEditorFn>(dlsym(mod.handle, "Script_TestEditor"));
    mod.update = reinterpret_cast<UpdateFn>(dlsym(mod.handle, "Script_Update"));
    mod.tickUpdate = reinterpret_cast<TickUpdateFn>(dlsym(mod.handle, "Script_TickUpdate"));
    mod.editorRender = reinterpret_cast<EditorRenderFn>(dlsym(mod.handle, "RenderEditorWindow"));
    mod.editorExit = reinterpret_cast<EditorExitFn>(dlsym(mod.handle, "ExitRenderEditorWindow"));
#if !defined(_WIN32)
    {
        const char* err = dlerror();
        if (err && !mod.inspector && !mod.begin && !mod.spec && !mod.testEditor
            && !mod.update && !mod.tickUpdate && !mod.editorRender && !mod.editorExit) {
            lastError = err;
        }
    }
#endif
#endif

    if (!abiVersionFn) {
        unloadModuleHandle(mod.handle);
        if (mod.loadedFromShadowCopy && !mod.loadedPath.empty()) {
            std::error_code removeEc;
            fs::remove(mod.loadedPath, removeEc);
        }
        lastError = "Native script binary is incompatible with this engine build (missing ABI export). Recompile scripts.";
        return nullptr;
    }

    const int abiVersion = abiVersionFn();
    if (abiVersion != MODULARITY_NATIVE_SCRIPT_ABI_VERSION) {
        unloadModuleHandle(mod.handle);
        if (mod.loadedFromShadowCopy && !mod.loadedPath.empty()) {
            std::error_code removeEc;
            fs::remove(mod.loadedPath, removeEc);
        }
        lastError = "Native script binary ABI mismatch (expected " +
                    std::to_string(MODULARITY_NATIVE_SCRIPT_ABI_VERSION) +
                    ", got " + std::to_string(abiVersion) + "). Recompile scripts.";
        return nullptr;
    }

    if (!mod.inspector && !mod.begin && !mod.spec && !mod.testEditor
        && !mod.update && !mod.tickUpdate && !mod.editorRender && !mod.editorExit) {
        unloadModuleHandle(mod.handle);
        if (mod.loadedFromShadowCopy && !mod.loadedPath.empty()) {
            std::error_code removeEc;
            fs::remove(mod.loadedPath, removeEc);
        }
        if (lastError.empty()) lastError = "No script exports found";
        return nullptr;
    }

    const auto __moduLoadEnd = std::chrono::steady_clock::now();
    const double __moduLoadMs = std::chrono::duration<double, std::milli>(__moduLoadEnd - __moduLoadStart).count();
    std::fprintf(stderr, "[ModuTimer] dlopen+symbols %.2f ms  %s\n", __moduLoadMs, binaryPath.string().c_str());
    loaded[key] = mod;
    return &loaded[key];
}

void ScriptRuntime::tickModule(const fs::path& binaryPath, ScriptContext& ctx, float deltaTime,
                               bool runSpec, bool runTest) {
    Module* mod = getModule(binaryPath);
    if (!mod) return;

    int objId = ctx.object ? ctx.object->id : -1;
    if (objId >= 0 && mod->begin && mod->beginCalledObjects.find(objId) == mod->beginCalledObjects.end()) {
        mod->begin(ctx, deltaTime);
        mod->beginCalledObjects.insert(objId);
    }

    if (mod->tickUpdate) {
        mod->tickUpdate(ctx, deltaTime);
    } else if (mod->update) {
        mod->update(ctx, deltaTime);
    }

    if (runSpec && mod->spec) {
        mod->spec(ctx, deltaTime);
    }
    if (runTest && mod->testEditor) {
        mod->testEditor(ctx, deltaTime);
    }

    // Tick any IEnum tasks registered by the script (per ScriptComponent instance).
    if (ctx.script && !ctx.script->activeIEnums.empty()) {
        auto tasks = ctx.script->activeIEnums; // copy so tasks can modify the list
        for (void* p : tasks) {
            auto fn = reinterpret_cast<IEnumFn>(p);
            if (fn) fn(ctx, deltaTime);
        }
    }
}

void ScriptRuntime::unloadAll() {
    for (auto& kv : loaded) {
        if (!kv.second.handle) continue;
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(kv.second.handle));
#else
        dlclose(kv.second.handle);
#endif
        if (kv.second.loadedFromShadowCopy && !kv.second.loadedPath.empty()) {
            std::error_code removeEc;
            fs::remove(kv.second.loadedPath, removeEc);
        }
    }
    loaded.clear();
    gSpriteAlphaFadeStates.clear();
    gSpriteClipFadeStates.clear();
    gKeyPressedStates.clear();
    lastError.clear();
}

bool ScriptRuntime::hasEditorWindow(const fs::path& binaryPath) {
    Module* mod = getModule(binaryPath);
    return mod && mod->editorRender;
}

void ScriptRuntime::callEditorWindow(const fs::path& binaryPath, ScriptContext& ctx) {
    Module* mod = getModule(binaryPath);
    if (mod && mod->editorRender) {
        mod->editorRender(ctx);
    }
}

void ScriptRuntime::callExitEditorWindow(const fs::path& binaryPath, ScriptContext& ctx) {
    Module* mod = getModule(binaryPath);
    if (mod && mod->editorExit) {
        mod->editorExit(ctx);
    }
}
