#pragma once

#include "Common.h"
#include "SceneObject.h"
#include <unordered_set>

class Engine;

struct ScriptContext {
    Engine* engine = nullptr;
    SceneObject* object = nullptr;
    ScriptComponent* script = nullptr;

    // Convenience helpers for scripts
    SceneObject* FindObjectByName(const std::string& name);
    SceneObject* FindObjectById(int id);
    void SetPosition(const glm::vec3& pos);
    void SetRotation(const glm::vec3& rot);
    void SetScale(const glm::vec3& scl);
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

    InspectorFn getInspector(const fs::path& binaryPath);
    void tickModule(const fs::path& binaryPath, ScriptContext& ctx, float deltaTime,
                    bool runSpec, bool runTest);
    void unloadAll();
    const std::string& getLastError() const { return lastError; }

private:
    struct Module {
        void* handle = nullptr;
        InspectorFn inspector = nullptr;
        BeginFn begin = nullptr;
        SpecFn spec = nullptr;
        TestEditorFn testEditor = nullptr;
        UpdateFn update = nullptr;
        TickUpdateFn tickUpdate = nullptr;
        std::unordered_set<int> beginCalledObjects;
    };
    Module* getModule(const fs::path& binaryPath);
    std::unordered_map<std::string, Module> loaded;
    std::string lastError;
};
