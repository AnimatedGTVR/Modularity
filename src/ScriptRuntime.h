#pragma once

#include "ScriptSdkCommon.h"
#include "SceneObject.h"
#include <unordered_set>

class Engine;

#if defined(_WIN32)
    #if defined(MODULARITY_SCRIPT_HOST)
        #define MODULARITY_SCRIPT_API __declspec(dllexport)
    #elif defined(MODULARITY_SCRIPT_IMPORTS)
        #define MODULARITY_SCRIPT_API __declspec(dllimport)
    #else
        #define MODULARITY_SCRIPT_API
    #endif
    #define MODULARITY_SCRIPT_EXPORT __declspec(dllexport)
#else
    #define MODULARITY_SCRIPT_API
    #define MODULARITY_SCRIPT_EXPORT __attribute__((visibility("default")))
#endif

#define MODULARITY_NATIVE_SCRIPT_ABI_VERSION 10

struct MODULARITY_SCRIPT_API ScriptContext {
    Engine* engine = nullptr;
    SceneObject* object = nullptr;
    ScriptComponent* script = nullptr;
    enum class AutoSettingType { Bool, Float, Int, Vec3, StringBuf, String };
    struct AutoSettingEntry {
        AutoSettingType type;
        std::string key;
        void* ptr = nullptr;
        size_t bufSize = 0;
        bool initialBool = false;
        float initialFloat = 0.0f;
        int initialInt = 0;
        glm::vec3 initialVec3 = glm::vec3(0.0f);
        std::string initialString;
    };
    std::vector<AutoSettingEntry> autoSettings;

    // Convenience helpers for scripts
    /// @summary Resolve the first scene object with an exact name match.
    /// @usage Useful for cross-object links in gameplay scripts.
    /// @howto Call once (for example in Begin) and cache the id/name you need.
    /// @param name Scene object name to match exactly.
    /// @returns Matching object pointer, or nullptr when not found.
    SceneObject* FindObjectByName(const std::string& name);
    SceneObject* FindObjectById(int id);
    SceneObject* ResolveObjectRef(const std::string& ref);
    int GetSceneObjectCount() const;
    int GetSceneObjectIdAt(int index) const;
    const SceneObject* GetSceneObjectAt(int index) const;
    bool IsObjectEnabled() const;
    void SetObjectEnabled(bool enabled);
    int GetLayer() const;
    void SetLayer(int layer);
    std::string GetTag() const;
    void SetTag(const std::string& tag);
    bool HasTag(const std::string& tag) const;
    bool IsInLayer(int layer) const;
    void SetPosition(const glm::vec3& pos);
    void SetPosition2D(const glm::vec2& pos);
    void SetRotation(const glm::vec3& rot);
    void SetScale(const glm::vec3& scl);
    void GetPlanarYawPitchVectors(float pitchDeg, float yawDeg, glm::vec3& outForward, glm::vec3& outRight) const;
    glm::vec3 GetMoveInputWASD(float pitchDeg, float yawDeg) const;
    bool ApplyMouseLook(float& pitchDeg, float& yawDeg, float sensitivity, float maxDelta, float deltaTime,
                        bool requireMouseButton) const;
    int GetSelectedObjectId() const;
    bool IsSprintDown() const;
    bool IsJumpDown() const;
    bool IsKeyDown(int glfwKey, ImGuiKey imguiKey = ImGuiKey_None) const;
    bool IsKeyPressed(int glfwKey, ImGuiKey imguiKey = ImGuiKey_None) const;
    bool ResolveGround(float capsuleHalf, float probeExtra, float groundSnap, float verticalVelocity,
                       glm::vec3* outHitPos = nullptr, bool* outHitGround = nullptr,
                       glm::vec3* outHitNormal = nullptr, int* outHitActorId = nullptr,
                       glm::vec3* outHitActorVelocity = nullptr,
                       float* outHitStaticFriction = nullptr,
                       float* outHitDynamicFriction = nullptr) const;
    void ApplyVelocity(const glm::vec3& velocity, float deltaTime);
    struct StandaloneMovementSettings {
        glm::vec3 moveTuning = glm::vec3(4.5f, 7.5f, 6.5f);
        glm::vec3 lookTuning = glm::vec3(0.12f, 200.0f, 0.0f);
        glm::vec3 capsuleTuning = glm::vec3(1.8f, 0.4f, 0.2f);
        glm::vec3 gravityTuning = glm::vec3(-9.81f, 0.4f, 30.0f);
        glm::vec3 locomotionTuning = glm::vec3(24.0f, 8.0f, 16.0f);
        glm::vec3 surfaceTuning = glm::vec3(0.2f, 40.0f, 1.0f);
        bool enableMouseLook = true;
        bool requireMouseButton = false;
        bool enforceCollider = true;
        bool enforceRigidbody = true;
    };
    struct StandaloneMovementState {
        float pitch = 0.0f;
        float yaw = 0.0f;
        float verticalVelocity = 0.0f;
        glm::vec2 localVelocity = glm::vec2(0.0f);
        glm::vec3 slideVelocity = glm::vec3(0.0f);
        glm::vec3 lastGroundHitPos = glm::vec3(0.0f);
        bool hasGroundSample = false;
    };
    struct StandaloneMovementDebug {
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::vec2 localVelocity = glm::vec2(0.0f);
        glm::vec3 platformVelocity = glm::vec3(0.0f);
        float surfaceFriction = 0.0f;
        float slopeDegrees = 0.0f;
        bool grounded = false;
    };
    void BindStandaloneMovementSettings(StandaloneMovementSettings& settings);
    void DrawStandaloneMovementInspector(StandaloneMovementSettings& settings, bool* showDebug = nullptr);
    void TickStandaloneMovement(StandaloneMovementState& state, StandaloneMovementSettings& settings,
                                float deltaTime, StandaloneMovementDebug* debug = nullptr);
    // UI helpers
    bool IsUIButtonPressed() const;
    bool IsUIHovered() const;
    bool IsUIActive() const;
    bool IsUIInteractable() const;
    void SetUIInteractable(bool interactable);
    float GetUISliderValue() const;
    void SetUISliderValue(float value);
    void SetUISliderRange(float minValue, float maxValue);
    void SetUILabel(const std::string& label);
    void SetUIColor(const glm::vec4& color);
    int GetSpriteClipCount() const;
    int GetSpriteClipIndex() const;
    std::string GetSpriteClipName() const;
    std::string GetSpriteClipNameAt(int index) const;
    bool SetSpriteClipIndex(int index);
    bool SetSpriteClipName(const std::string& name);
    float GetSpriteAlpha() const;
    void SetSpriteAlpha(float alpha);
    bool FadeSpriteAlpha(float targetAlpha, float duration, float deltaTime);
    bool FadeSpriteToClipIndex(int clipIndex, float fadeOutDuration, float fadeInDuration, float deltaTime);
    bool FadeSpriteToClipName(const std::string& clipName,
                              float fadeOutDuration, float fadeInDuration, float deltaTime);
    float GetUITextScale() const;
    void SetUITextScale(float scale);
    void SetUISliderStyle(UISliderStyle style);
    void SetUIButtonStyle(UIButtonStyle style);
    void SetUIStylePreset(const std::string& name);
    void RegisterUIStylePreset(const std::string& name, const ImGuiStyle& style, bool replace = false);
    void SetFPSCap(bool enabled, float cap = 120.0f);
    bool HasRigidbody() const;
    bool HasRigidbody2D() const;
    bool EnsureCapsuleCollider(float height, float radius);
    bool EnsureRigidbody(bool useGravity = true, bool kinematic = false);
    bool SetRigidbody2DVelocity(const glm::vec2& velocity);
    bool GetRigidbody2DVelocity(glm::vec2& outVelocity) const;
    bool SetRigidbodyVelocity(const glm::vec3& velocity);
    bool GetRigidbodyVelocity(glm::vec3& outVelocity) const;
    bool AddRigidbodyVelocity(const glm::vec3& deltaVelocity);
    bool SetRigidbodyAngularVelocity(const glm::vec3& velocity);
    bool GetRigidbodyAngularVelocity(glm::vec3& outVelocity) const;
    bool AddRigidbodyForce(const glm::vec3& force);
    bool AddRigidbodyImpulse(const glm::vec3& impulse);
    bool AddRigidbodyTorque(const glm::vec3& torque);
    bool AddRigidbodyAngularImpulse(const glm::vec3& impulse);
    bool SetObjectRigidbodyVelocity(int objectId, const glm::vec3& velocity);
    bool GetObjectRigidbodyVelocity(int objectId, glm::vec3& outVelocity) const;
    bool AddObjectRigidbodyImpulse(int objectId, const glm::vec3& impulse);
    bool TeleportObjectRigidbody(int objectId, const glm::vec3& pos, const glm::vec3& rotDeg);
    bool SetRigidbodyYaw(float yawDegrees);
    float GetProjectGravityScale() const;
    void SetProjectGravityScale(float scale);
    bool RaycastClosest(const glm::vec3& origin, const glm::vec3& dir, float distance,
                        glm::vec3* hitPos = nullptr, glm::vec3* hitNormal = nullptr,
                        float* hitDistance = nullptr) const;
    bool RaycastClosestDetailed(const glm::vec3& origin, const glm::vec3& dir, float distance,
                                glm::vec3* hitPos = nullptr, glm::vec3* hitNormal = nullptr,
                                float* hitDistance = nullptr, int* hitObjectId = nullptr,
                                glm::vec3* hitObjectVelocity = nullptr,
                                float* hitStaticFriction = nullptr,
                                float* hitDynamicFriction = nullptr) const;
    bool SetRigidbodyRotation(const glm::vec3& rotDeg);
    bool TeleportRigidbody(const glm::vec3& pos, const glm::vec3& rotDeg);
    // Audio helpers
    bool HasAudioSource() const;
    bool PlayAudio();
    bool StopAudio();
    bool SetAudioLoop(bool loop);
    bool SetAudioVolume(float volume);
    bool SetAudioClip(const std::string& path);
    bool PlayAudioOneShot(const std::string& clipPath = "", float volumeScale = 1.0f);
    // Animation helpers
    bool HasAnimation() const;
    bool PlayAnimation(bool restart = true);
    bool StopAnimation(bool resetTime = true);
    bool PauseAnimation(bool pause = true);
    bool ReverseAnimation(bool restartIfStopped = true);
    bool SetAnimationTime(float timeSeconds);
    float GetAnimationTime() const;
    bool IsAnimationPlaying() const;
    bool SetAnimationLoop(bool loop);
    bool SetAnimationPlaySpeed(float speed);
    bool SetAnimationPlayOnAwake(bool playOnAwake);
    // Settings helpers (auto-mark dirty)
    std::string GetSetting(const std::string& key, const std::string& fallback = "") const;
    void SetSetting(const std::string& key, const std::string& value);
    bool GetSettingBool(const std::string& key, bool fallback = false) const;
    void SetSettingBool(const std::string& key, bool value);
    float GetSettingFloat(const std::string& key, float fallback = 0.0f) const;
    void SetSettingFloat(const std::string& key, float value);
    glm::vec3 GetSettingVec3(const std::string& key, const glm::vec3& fallback = glm::vec3(0.0f)) const;
    void SetSettingVec3(const std::string& key, const glm::vec3& value);
    // Utility I/O helpers
    std::string HttpPost(const std::string& url, const std::string& contentType,
                         const std::string& body, const std::string& headers = "");
    int StartHttpPost(const std::string& url, const std::string& contentType,
                      const std::string& body, const std::string& headers = "",
                      bool stream = false);
    bool PollHttpPost(int requestId, std::string& outChunk, bool& outDone, bool& outSuccess);
    void CancelHttpPost(int requestId);
    std::string ReadFileText(const std::string& path) const;
    bool WriteFileText(const std::string& path, const std::string& content);
    bool DeleteFile(const std::string& path);
    std::string ListFiles(const std::string& path, bool recursive = false, int maxEntries = 200) const;
    std::string SearchFiles(const std::string& root, const std::string& query, int maxResults = 50) const;
    std::string GetProgramRootPath() const;
    std::string GetEngineDocsRootPath() const;
    bool SaveProject();
    // Console helper
    void AddConsoleMessage(const std::string& message, ConsoleMessageType type = ConsoleMessageType::Info);
    // Auto-binding helpers: bind once per call, optionally load stored value.
    void AutoSetting(const std::string& key, bool& value);
    void AutoSetting(const std::string& key, float& value);
    void AutoSetting(const std::string& key, int& value);
    void AutoSetting(const std::string& key, glm::vec3& value);
    void AutoSetting(const std::string& key, char* buffer, size_t bufferSize);
    void AutoSetting(const std::string& key, std::string& value);
    void SaveAutoSettings();
    // IEnum helpers
    void StartIEnum(void(*fn)(ScriptContext&, float));
    void StopIEnum(void(*fn)(ScriptContext&, float));
    void EnsureIEnum(void(*fn)(ScriptContext&, float));
    bool IsIEnumRunning(void(*fn)(ScriptContext&, float)) const;
    void StopAllIEnums();
    void MarkDirty();
};

class ScriptRuntime {
public:
    using AbiVersionFn = int(*)();
    using BeginFn = void(*)(ScriptContext&, float);
    using SpecFn = void(*)(ScriptContext&, float);
    using TestEditorFn = void(*)(ScriptContext&, float);
    using UpdateFn = void(*)(ScriptContext&, float);
    using TickUpdateFn = void(*)(ScriptContext&, float);
    using InspectorFn = void(*)(ScriptContext&);
    using EditorRenderFn = void(*)(ScriptContext&);
    using EditorExitFn = void(*)(ScriptContext&);
    using IEnumFn = void(*)(ScriptContext&, float);

    InspectorFn getInspector(const fs::path& binaryPath);
    void tickModule(const fs::path& binaryPath, ScriptContext& ctx, float deltaTime,
                    bool runSpec, bool runTest);
    void unloadAll();
    const std::string& getLastError() const { return lastError; }
    // Editor extension hooks: load RenderEditorWindow/ExitRenderEditorWindow from a script binary.
    bool hasEditorWindow(const fs::path& binaryPath);
    void callEditorWindow(const fs::path& binaryPath, ScriptContext& ctx);
    void callExitEditorWindow(const fs::path& binaryPath, ScriptContext& ctx);

private:
    struct Module {
        void* handle = nullptr;
        fs::path loadedPath;
        fs::file_time_type binaryWriteTime{};
        uintmax_t binarySize = 0;
        bool loadedFromShadowCopy = false;
        InspectorFn inspector = nullptr;
        BeginFn begin = nullptr;
        SpecFn spec = nullptr;
        TestEditorFn testEditor = nullptr;
        UpdateFn update = nullptr;
        TickUpdateFn tickUpdate = nullptr;
        EditorRenderFn editorRender = nullptr;
        EditorExitFn editorExit = nullptr;
        std::unordered_set<int> beginCalledObjects;
    };
    Module* getModule(const fs::path& binaryPath);
    std::unordered_map<std::string, Module> loaded;
    std::string lastError;
};

// Lightweight coroutine-style helpers (opt-in and no-ops unless used by scripts).
#ifndef IEnum
#define IEnum
#endif

#ifndef IEnum_Start
#define IEnum_Start(fn) ctx.StartIEnum(fn)
#endif
#ifndef IEnum_Stop
#define IEnum_Stop(fn) ctx.StopIEnum(fn)
#endif
#ifndef IEnum_Ensure
#define IEnum_Ensure(fn) ctx.EnsureIEnum(fn)
#endif

#undef MODULARITY_SCRIPT_API
