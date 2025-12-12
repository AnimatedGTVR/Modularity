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

ScriptRuntime::InspectorFn ScriptRuntime::getInspector(const fs::path& binaryPath) {
    lastError.clear();
    if (binaryPath.empty()) return nullptr;
    auto key = binaryPath.string();
    auto it = loaded.find(key);
    if (it != loaded.end()) {
        if (it->second.inspector) return it->second.inspector;
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
#else
    mod.handle = dlopen(binaryPath.string().c_str(), RTLD_NOW);
    if (!mod.handle) {
        const char* err = dlerror();
        if (err) lastError = err;
        return nullptr;
    }
    mod.inspector = reinterpret_cast<InspectorFn>(dlsym(mod.handle, "Script_OnInspector"));
#if !defined(_WIN32)
    {
        const char* err = dlerror();
        if (err && !mod.inspector) lastError = err;
    }
#endif
#endif

    if (!mod.inspector) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(mod.handle));
#else
        dlclose(mod.handle);
#endif
        if (lastError.empty()) lastError = "Script_OnInspector not found";
        return nullptr;
    }

    loaded[key] = mod;
    return mod.inspector;
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
