#pragma once

#include "Common.h"
#include "SceneObject.h"
#include <unordered_set>

class Engine;

struct ScriptContext {
    Engine* engine = nullptr;
    SceneObject* object = nullptr;
    ScriptComponent* script = nullptr;
    enum class AutoSettingType { Bool, Vec3, StringBuf };
    struct AutoSettingEntry {
        AutoSettingType type;
        std::string key;
        void* ptr = nullptr;
        size_t bufSize = 0;
        bool initialBool = false;
        glm::vec3 initialVec3 = glm::vec3(0.0f);
        std::string initialString;
    };
    std::vector<AutoSettingEntry> autoSettings;

    // Convenience helpers for scripts
    SceneObject* FindObjectByName(const std::string& name);
    SceneObject* FindObjectById(int id);
    bool IsObjectEnabled() const;
    void SetObjectEnabled(bool enabled);
    int GetLayer() const;
    void SetLayer(int layer);
    std::string GetTag() const;
    void SetTag(const std::string& tag);
    bool HasTag(const std::string& tag) const;
    bool IsInLayer(int layer) const;
    void SetPosition(const glm::vec3& pos);
    void SetRotation(const glm::vec3& rot);
    void SetScale(const glm::vec3& scl);
    bool HasRigidbody() const;
    bool SetRigidbodyVelocity(const glm::vec3& velocity);
    bool GetRigidbodyVelocity(glm::vec3& outVelocity) const;
    bool SetRigidbodyAngularVelocity(const glm::vec3& velocity);
    bool GetRigidbodyAngularVelocity(glm::vec3& outVelocity) const;
    bool AddRigidbodyForce(const glm::vec3& force);
    bool AddRigidbodyImpulse(const glm::vec3& impulse);
    bool AddRigidbodyTorque(const glm::vec3& torque);
    bool AddRigidbodyAngularImpulse(const glm::vec3& impulse);
    bool SetRigidbodyRotation(const glm::vec3& rotDeg);
    bool TeleportRigidbody(const glm::vec3& pos, const glm::vec3& rotDeg);
    // Audio helpers
    bool HasAudioSource() const;
    bool PlayAudio();
    bool StopAudio();
    bool SetAudioLoop(bool loop);
    bool SetAudioVolume(float volume);
    bool SetAudioClip(const std::string& path);
    // Settings helpers (auto-mark dirty)
    std::string GetSetting(const std::string& key, const std::string& fallback = "") const;
    void SetSetting(const std::string& key, const std::string& value);
    bool GetSettingBool(const std::string& key, bool fallback = false) const;
    void SetSettingBool(const std::string& key, bool value);
    glm::vec3 GetSettingVec3(const std::string& key, const glm::vec3& fallback = glm::vec3(0.0f)) const;
    void SetSettingVec3(const std::string& key, const glm::vec3& value);
    // Console helper
    void AddConsoleMessage(const std::string& message, ConsoleMessageType type = ConsoleMessageType::Info);
    // Auto-binding helpers: bind once per call, optionally load stored value, then SaveAutoSettings() writes back on change.
    void AutoSetting(const std::string& key, bool& value);
    void AutoSetting(const std::string& key, glm::vec3& value);
    void AutoSetting(const std::string& key, char* buffer, size_t bufferSize);
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
