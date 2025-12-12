#pragma once

#include "Common.h"
#include "SceneObject.h"

class Engine;

struct ScriptContext {
    Engine* engine = nullptr;
    SceneObject* object = nullptr;

    // Convenience helpers for scripts
    SceneObject* FindObjectByName(const std::string& name);
    SceneObject* FindObjectById(int id);
    void SetPosition(const glm::vec3& pos);
    void SetRotation(const glm::vec3& rot);
    void SetScale(const glm::vec3& scl);
};

class ScriptRuntime {
public:
    using InspectorFn = void(*)(ScriptContext&);

    InspectorFn getInspector(const fs::path& binaryPath);
    void unloadAll();
    const std::string& getLastError() const { return lastError; }

private:
    struct Module {
        void* handle = nullptr;
        InspectorFn inspector = nullptr;
    };
    std::unordered_map<std::string, Module> loaded;
    std::string lastError;
};
