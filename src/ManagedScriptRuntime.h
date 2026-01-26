#pragma once

#include "ManagedBindings.h"
#include "ScriptRuntime.h"
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

class ManagedScriptRuntime {
public:
    ~ManagedScriptRuntime();

    bool hasInspector(const fs::path& assemblyPath, const std::string& typeName);
    bool invokeInspector(const fs::path& assemblyPath, const std::string& typeName, ScriptContext& ctx);
    void tickModule(const fs::path& assemblyPath, const std::string& typeName,
                    ScriptContext& ctx, float deltaTime, bool runSpec, bool runTest);
    void unloadAll();
    const std::string& getLastError() const { return lastError; }

    struct Module {
        struct MethodSlot {
            void* method = nullptr;
            bool hasDelta = false;
            bool isStatic = true;
        };
        fs::path assemblyPath;
        std::string typeName;
        MethodSlot inspector;
        MethodSlot begin;
        MethodSlot spec;
        MethodSlot testEditor;
        MethodSlot update;
        MethodSlot tickUpdate;
        void* instance = nullptr;
        uint32_t instanceHandle = 0;
        void* autoInspectorMethod = nullptr;
        std::unordered_set<int> beginCalledObjects;
    };

    struct MonoState;
    struct MonoStateDeleter {
        void operator()(MonoState* state) const;
    };

private:
    Module* getModule(const fs::path& assemblyPath, const std::string& typeName);
    bool ensureHost(const fs::path& assemblyPath);
    bool ensureApiInjected(const fs::path& assemblyPath);
    bool loadModuleMethods(Module& mod, const fs::path& assemblyPath, const std::string& typeName);

    std::unordered_map<std::string, Module> modules;
    std::string lastError;
    std::unique_ptr<MonoState, MonoStateDeleter> monoState;
    bool apiInjected = false;
    ManagedNativeApi api = BuildManagedNativeApi();
};
