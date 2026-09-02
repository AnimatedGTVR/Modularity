#pragma once

#include "ScriptSdkCommon.h"
#include "SceneObject.h"
#include "IPhysicsBackend.h"
#include "imgui.h"
#include <unordered_map>
#include <unordered_set>

#if defined(_WIN32) && defined(DeleteFile)
// <Windows.h> may already be pulled in transitively by the time this header is parsed
// (its own #include<Windows.h> only affects .cpp files that include it before this
// header). Without this, the DeleteFile macro rewrites the declaration below to
// DeleteFileW here while a .cpp that undefs it later defines plain DeleteFile,
// producing a "not a member of ScriptContext" mismatch between the two.
#undef DeleteFile
#endif

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

// Deliberately NOT bumped for the -fno-gnu-unique store isolation that ScriptCompiler now
// applies: that changes only the *binding* of the stores inside a script object, never the
// engine-facing interface, and a flagged binary is isolated from an unflagged one in either
// load order (verified both ways). Bumping would reject every already-compiled binary at
// load with nothing in the startup scan to rebuild them, which is a worse failure than the
// stale-but-harmless stores it would avoid.
// 43: added the ScriptContext XR *interaction* queries (IsXRSelected, XRSelectEntered, ...)
// used by XR Grab Interactable scripts. Same reasoning as 42: new symbols a script may
// reference, so the version gate is what turns "old engine, new script" into a readable
// "recompile scripts" instead of an unresolved symbol at dlopen.
// 42: added the ScriptContext XR input methods behind ModuInput.XR. ScriptContext gains no
// data members, so the layout signature is unchanged and an old script would not be caught by
// that guard - but it would fail to resolve the new symbols at dlopen time, which is a far
// worse error message than "recompile scripts". Hence the version bump.
// 41: config/state stores are keyed on sizeof/alignof of their T as well as its name, so a
// script edited mid-session no longer aliases the still-mapped previous .so's store at a
// different layout. Now a backstop behind -fno-gnu-unique for toolchains that lack the
// flag; on its own it misses a layout change inside an indirect member (a std::vector is
// 24 bytes whatever it holds). Bumped because the store symbols are renamed: an ABI 40
// script and an ABI 41 script sharing a process would each hold half the state for one type.
// 40: added ScriptContext::GetPersistentDataPath. A script built against the new header
// calling into an older engine would fail to resolve the symbol at dlopen time, so the
// version gate is what turns that into a "recompile scripts" message instead.
// 39: added the Script_ResetState export. Bumped rather than treated as an optional symbol
// because a binary without it silently keeps the old behaviour (script state leaking across
// play sessions), and a silent half-fix is worse than a "recompile scripts" message.
// 44: ColliderComponent gained isTrigger and the runtime now loads/dispatches the
// collision-hook exports emitted by ModuCPP. SceneObject layout changed, so every
// native script must be rebuilt before it can safely dereference component data.
// 45: Assemblage (structured 2D authoring). SceneObject gained the Assemblage and
// Assemblage Layer components, and Collider2DComponent gained the circle radius
// plus the sprite-outline generation settings. SceneObject layout changed, so
// every native script must be rebuilt.
//
// Deliberately banked for the whole Assemblage feature rather than bumped per
// stage: painting, rendering, collision and the ModuCPP spawn API all need
// fields, and every bump invalidates every compiled script in every project.
// New fields for the remaining stages go in alongside these, under this one
// version, so users recompile once instead of once per release.
// 45 -> 46: PostFXSettings gained the 2D draw-order scope fields
// (scope2DEnabled/scope2DMode/scope2DMinOrder/scope2DMaxOrder), which grows
// SceneObject.
#define MODULARITY_NATIVE_SCRIPT_ABI_VERSION 46

// layout drift guard. scripts dereference SceneObject/ScriptContext directly, so ANY size
// change silently breaks every compiled script (wrong offsets = heap corruption hours later).
// the wrapper bakes this in at script-compile time and the loader rejects mismatches, even
// when nobody remembered to bump MODULARITY_NATIVE_SCRIPT_ABI_VERSION. macro on purpose:
// it must evaluate against the headers the SCRIPT was built with, not the engine's.
#define MODULARITY_SCRIPT_LAYOUT_SIGNATURE() \
    ((static_cast<unsigned long long>(sizeof(SceneObject)) << 40) ^ \
     (static_cast<unsigned long long>(sizeof(ScriptComponent)) << 20) ^ \
     static_cast<unsigned long long>(sizeof(ScriptContext)))

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
    // Rebuild an object's local transform from its current world transform, so a
    // direct world write (e.g. from a referenced-object Transform proxy) survives the
    // per-frame hierarchy pass instead of being reverted from a stale localPosition.
    void SyncObjectLocalTransform(int objectId);
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
    // Touch input. Platform-agnostic: on Android these report the live finger
    // set from the NativeActivity runtime; on desktop the left mouse button is
    // synthesized as touch 0 so the same Touch.* script code works everywhere.
    // Coordinates are surface/framebuffer pixels with origin at the top-left.
    // NOTE: declaration-only additions here are safe - ScriptContext gains no
    // data members, so its sizeof (and the script layout signature) is unchanged.
    int GetTouchCount() const;
    bool GetTouch(int index, float& x, float& y) const;
    bool GetPrimaryTouch(float& x, float& y) const;
    bool IsTouchActive() const;
    // XR input (ModuInput.XR). Every parameter is a plain int so a compiled
    // script never sees an OpenXR handle or even an XR enum's definition across
    // the ABI; the values are Modularity::XR::XRDevice / XRButton / XRAxisKind /
    // XRAxis2DKind / XRPoseKind from include/XR/XRInputTypes.h, which both sides
    // share. All of these read a snapshot taken once per frame, so calling them
    // repeatedly in a tick costs nothing beyond an array index.
    //
    // With no XR session running every query reports "not pressed / zero / not
    // tracked" rather than failing, so a script written for VR still runs on a
    // flat build instead of needing to be guarded everywhere.
    bool IsXRActive() const;
    bool IsXRDeviceTracked(int device) const;
    int GetXRTrackingState(int device) const;
    bool GetXRButton(int device, int button) const;
    bool GetXRButtonDown(int device, int button) const;
    bool GetXRButtonUp(int device, int button) const;
    bool IsXRButtonBound(int device, int button) const;
    float GetXRAxis(int device, int axis) const;
    bool GetXRAxis2D(int device, int axis, float& x, float& y) const;
    // poseKind selects Grip or Aim; they are different transforms and are never
    // interchangeable (section 15). The head device ignores poseKind.
    bool GetXRPose(int device, int poseKind, glm::vec3& position, glm::quat& rotation) const;
    bool GetXRVelocity(int device, glm::vec3& linear, glm::vec3& angular) const;
    // amplitude 0..1, duration in seconds, frequency in Hz (0 = runtime default).
    // Queued and applied on the next XR frame; a no-op with no session.
    void XRHapticPulse(int device, float amplitude, float duration, float frequency = 0.0f) const;
    std::string GetXRInteractionProfile(int device) const;
    // XR interaction state for the script's own object, which must carry an XR
    // Grab Interactable. Polled rather than delivered as callbacks: TickUpdate
    // polling is the dominant ModuCPP idiom (the whole InputXR API works this
    // way), and it avoids bolting a second event-dispatch path onto the runtime.
    // The *Entered/*Exited/*Activated queries are true for exactly one frame.
    bool IsXRHovered() const;
    bool IsXRSelected() const;
    bool XRHoverEntered() const;
    bool XRHoverExited() const;
    bool XRSelectEntered() const;
    bool XRSelectExited() const;
    bool XRActivated() const;
    bool XRDeactivated() const;
    // Scene id of the interactor holding this object, else the one hovering it,
    // else -1. Use FindObjectById to get at it.
    int GetXRInteractorId() const;
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
    // assign a sheet-relative Sprite to any object's sprite component. same-sheet fast-paths to
    // a clip-index change, a new sheet loads its frame table first. false + log if the target
    // has no sprite component.
    bool SetObjectSprite(int objectId, const Sprite& sprite);
    // Points this object's sprite at a different image file. Scripts naturally
    // write project-relative paths ("Assets/Sprites/..."), but the renderer's
    // texture cache opens the string as given, so a relative path resolves
    // against the process working directory and silently loads nothing -- a
    // blank quad. This resolves against the project root first, matching the
    // absolute paths the editor stores when you assign a texture by hand.
    bool SetSpriteTexture(const std::string& path);
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
    bool PlayObjectAudio(int objectId);
    bool StopObjectAudio(int objectId);
    bool SetObjectAudioLoop(int objectId, bool loop);
    bool PlayObjectAudioOneShot(int objectId, const std::string& clipPath, float volumeScale = 1.0f);
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
    // Async child-process execution (for AI/agent tooling: terminal sandbox + MCP
    // servers). Mirrors the async HTTP helpers above. `command` is run through the
    // system shell. `workingDir` is resolved like other file-tool paths (empty ->
    // project root). `interactive` opens a stdin pipe so a long-lived process (e.g.
    // an MCP server speaking JSON-RPC over stdio) can be fed input via
    // WriteProcessStdin; non-interactive processes are read-only.
    // StartProcess returns a request id (>0) or 0 on failure.
    int StartProcess(const std::string& command, const std::string& workingDir = "",
                     bool interactive = false);
    // Feed data to an interactive process's stdin. Returns false if the process is
    // gone or was not started interactive.
    bool WriteProcessStdin(int processId, const std::string& data);
    // Drain the next chunk of merged stdout+stderr. outDone becomes true once the
    // process has exited and all output has been read; outExitCode carries the exit
    // status (valid when outDone). Returns false only for an unknown id.
    bool PollProcess(int processId, std::string& outChunk, bool& outDone,
                     bool& outSuccess, int& outExitCode);
    // Terminate the process (SIGKILL / TerminateProcess) and release its handle.
    void CancelProcess(int processId);
    // Blocking: run a command to completion and return merged stdout+stderr. The
    // wait (and the timeout) run on the engine thread. `workingDir` resolves like
    // other file-tool paths (empty -> project root). timeoutMs <= 0 waits forever.
    std::string RunProcess(const std::string& command, const std::string& workingDir = "",
                           int timeoutMs = 15000, int* exitCode = nullptr);
    // Blocking: read one newline-delimited line from an interactive process's
    // output (for line-based JSON-RPC / MCP stdio). Returns false on timeout or
    // once the process has ended with no more output.
    bool ReadProcessLine(int processId, std::string& outLine, int timeoutMs = 15000);
    std::string ReadFileText(const std::string& path) const;
    std::string ReadFileBase64(const std::string& path, size_t maxBytes = 16 * 1024 * 1024) const;
    bool WriteFileText(const std::string& path, const std::string& content);
    bool DeleteFile(const std::string& path);
    std::string ListFiles(const std::string& path, bool recursive = false, int maxEntries = 200) const;
    std::string SearchFiles(const std::string& root, const std::string& query, int maxResults = 50) const;
    std::string GetProgramRootPath() const;
    // A per-project, per-user directory a shipped game may write to (saves, settings).
    // Created on demand. `subFolder` is an optional relative folder inside it; absolute
    // paths and ".." are ignored. Empty string only if there is no engine to ask.
    std::string GetPersistentDataPath(const std::string& subFolder = "") const;
    std::string GetEngineDocsRootPath() const;
    ImTextureID GetUIImageTexture(const std::string& path, int* outWidth = nullptr, int* outHeight = nullptr) const;
    bool SaveProject();
    // Editor context (for AI/agent tooling): what the user currently has selected.
    std::string GetSelectedFilePath() const;     // file selected in the file browser (project-relative)
    std::string GetSelectedObjectInfo() const;    // human-readable dump of the selected scene object
    std::string GetProjectName() const;
    std::string GetCurrentSceneName() const;
    // Full scene-graph dump for AI/agent tooling: a compact, indented tree of every
    // object (name, id, type, components, attached scripts). maxObjects <= 0 means no cap.
    std::string GetSceneHierarchy(int maxObjects = 0) const;
    // Scene editing for AI/agent tooling. All address objects by id (see GetSceneHierarchy).
    // CreateSceneObject returns the new object's id, or -1 on failure (unknown type).
    int  CreateSceneObject(const std::string& type, const std::string& name, int parentId = -1);
    bool DeleteSceneObject(int objectId);
    bool RenameSceneObject(int objectId, const std::string& name);
    bool SetSceneObjectParent(int objectId, int parentId);  // parentId < 0 detaches to root
    bool SetSceneObjectTransform(int objectId, const glm::vec3& position,
                                 const glm::vec3& rotationDeg, const glm::vec3& scale);
    bool SetSceneObjectEnabled(int objectId, bool enabled);
    bool AddObjectComponent(int objectId, const std::string& component);
    bool AttachObjectScript(int objectId, const std::string& scriptPath);
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

// Why a load failed, for callers that want to react rather than just print. The two
// mismatch kinds mean "this .so was built by a different engine": recompiling the script
// fixes them, and nothing else will.
enum class ScriptLoadFailure {
    None,
    Unreadable,      // missing/unstattable binary, or dlopen/LoadLibrary refused it
    AbiMismatch,     // Modularity_ScriptAbiVersion disagrees (or is absent entirely)
    LayoutMismatch,  // built against a different SceneObject/ScriptContext layout
    NoExports
};

class ScriptRuntime {
public:
    using AbiVersionFn = int(*)();
    using LayoutSignatureFn = unsigned long long(*)();
    using BeginFn = void(*)(ScriptContext&, float);
    using SpecFn = void(*)(ScriptContext&, float);
    using TestEditorFn = void(*)(ScriptContext&, float);
    using UpdateFn = void(*)(ScriptContext&, float);
    using TickUpdateFn = void(*)(ScriptContext&, float);
    using InspectorFn = void(*)(ScriptContext&);
    using EditorRenderFn = void(*)(ScriptContext&);
    using EditorExitFn = void(*)(ScriptContext&);
    using IEnumFn = void(*)(ScriptContext&, float);
    using CollisionFn = void(*)(ScriptContext&, SceneObject*);
    using CollisionHoldDurationFn = float(*)();
    // Clears the script's ModuCPP config/state/timer stores. Absent on native C/C++ scripts
    // and on binaries compiled before the export existed, hence always null-checked.
    using ResetStateFn = void(*)();

    InspectorFn getInspector(const fs::path& binaryPath);
    void tickModule(const fs::path& binaryPath, ScriptContext& ctx, float deltaTime,
                    bool runSpec, bool runTest);
    void dispatchCollision(const fs::path& binaryPath, ScriptContext& ctx,
                           SceneObject* other, PhysicsCollisionPhase phase, float deltaTime);
    void unloadAll();
    const std::string& getLastError() const { return lastError; }
    // Paired with getLastError: set by the same load attempt, cleared by the same reset.
    ScriptLoadFailure getLastFailure() const { return lastFailure; }
    // Editor extension hooks: load RenderEditorWindow/ExitRenderEditorWindow from a script binary.
    bool hasEditorWindow(const fs::path& binaryPath);
    void callEditorWindow(const fs::path& binaryPath, ScriptContext& ctx);
    void callExitEditorWindow(const fs::path& binaryPath, ScriptContext& ctx);

    // Android only: tell the loader the compiled-scripts output root so sonames derive from the
    // same relative paths the packager used. no-op on desktop. see src/AndroidScript.h.
    void setCompiledScriptsRoot(const fs::path& root) { compiledScriptsRoot_ = root; }

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
        ResetStateFn resetState = nullptr;
        CollisionFn collideEnter = nullptr;
        CollisionFn collideHold = nullptr;
        CollisionFn collideExit = nullptr;
        CollisionHoldDurationFn collideHoldDuration = nullptr;
        struct CollisionHoldState {
            float elapsed = 0.0f;
            bool fired = false;
        };
        std::unordered_map<uint64_t, CollisionHoldState> collisionHoldStates;
        std::unordered_set<int> beginCalledObjects;
    };
    Module* getModule(const fs::path& binaryPath);
    std::unordered_map<std::string, Module> loaded;
    std::string lastError;
    ScriptLoadFailure lastFailure = ScriptLoadFailure::None;
    fs::path compiledScriptsRoot_; // Android soname derivation anchor; empty on desktop.
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
