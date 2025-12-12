#include "ScriptRuntime.h"
#include "Engine.h"
#include "SceneObject.h"

#if defined(_WIN32)
    #include <Windows.h>
#else
    #include <dlfcn.h>
#endif

SceneObject* ScriptContext::FindObjectByName(const std::string& name) {
    if (!engine) return nullptr;
    return engine->findObjectByName(name);
}

SceneObject* ScriptContext::FindObjectById(int id) {
    if (!engine) return nullptr;
    return engine->findObjectById(id);
}

void ScriptContext::SetPosition(const glm::vec3& pos) {
    if (object) object->position = pos;
}

void ScriptContext::SetRotation(const glm::vec3& rot) {
    if (object) object->rotation = rot;
}

void ScriptContext::SetScale(const glm::vec3& scl) {
    if (object) object->scale = scl;
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
#if defined(_WIN32)
    mod.handle = LoadLibraryA(binaryPath.string().c_str());
    if (!mod.handle) {
        lastError = "LoadLibrary failed";
        return nullptr;
    }
    mod.inspector = reinterpret_cast<InspectorFn>(GetProcAddress(static_cast<HMODULE>(mod.handle), "Script_OnInspector"));
    mod.begin = reinterpret_cast<BeginFn>(GetProcAddress(static_cast<HMODULE>(mod.handle), "Script_Begin"));
    mod.spec = reinterpret_cast<SpecFn>(GetProcAddress(static_cast<HMODULE>(mod.handle), "Script_Spec"));
    mod.testEditor = reinterpret_cast<TestEditorFn>(GetProcAddress(static_cast<HMODULE>(mod.handle), "Script_TestEditor"));
    mod.update = reinterpret_cast<UpdateFn>(GetProcAddress(static_cast<HMODULE>(mod.handle), "Script_Update"));
    mod.tickUpdate = reinterpret_cast<TickUpdateFn>(GetProcAddress(static_cast<HMODULE>(mod.handle), "Script_TickUpdate"));
#else
    mod.handle = dlopen(binaryPath.string().c_str(), RTLD_NOW);
    if (!mod.handle) {
        const char* err = dlerror();
        if (err) lastError = err;
        return nullptr;
    }
    mod.inspector = reinterpret_cast<InspectorFn>(dlsym(mod.handle, "Script_OnInspector"));
    mod.begin = reinterpret_cast<BeginFn>(dlsym(mod.handle, "Script_Begin"));
    mod.spec = reinterpret_cast<SpecFn>(dlsym(mod.handle, "Script_Spec"));
    mod.testEditor = reinterpret_cast<TestEditorFn>(dlsym(mod.handle, "Script_TestEditor"));
    mod.update = reinterpret_cast<UpdateFn>(dlsym(mod.handle, "Script_Update"));
    mod.tickUpdate = reinterpret_cast<TickUpdateFn>(dlsym(mod.handle, "Script_TickUpdate"));
#if !defined(_WIN32)
    {
        const char* err = dlerror();
        if (err && !mod.inspector && !mod.begin && !mod.spec && !mod.testEditor
            && !mod.update && !mod.tickUpdate) {
            lastError = err;
        }
    }
#endif
#endif

    if (!mod.inspector && !mod.begin && !mod.spec && !mod.testEditor
        && !mod.update && !mod.tickUpdate) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(mod.handle));
#else
        dlclose(mod.handle);
#endif
        if (lastError.empty()) lastError = "No script exports found";
        return nullptr;
    }

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
}

void ScriptRuntime::unloadAll() {
    for (auto& kv : loaded) {
        if (!kv.second.handle) continue;
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(kv.second.handle));
#else
        dlclose(kv.second.handle);
#endif
    }
    loaded.clear();
}
