#include "ScriptRuntime.h"
#include "Engine.h"
#include "SceneObject.h"
#include <algorithm>
#include <unordered_map>

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

bool ScriptContext::IsObjectEnabled() const {
    return object ? object->enabled : false;
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
        MarkDirty();
    }
}

void ScriptContext::SetRotation(const glm::vec3& rot) {
    if (object) {
        object->rotation = NormalizeEulerDegrees(rot);
        MarkDirty();
        if (engine && HasRigidbody()) {
            engine->teleportPhysicsActorFromScript(object->id, object->position, object->rotation);
        }
    }
}

void ScriptContext::SetScale(const glm::vec3& scl) {
    if (object) {
        object->scale = scl;
        MarkDirty();
    }
}

bool ScriptContext::HasRigidbody() const {
    return object && object->hasRigidbody && object->rigidbody.enabled;
}

bool ScriptContext::SetRigidbodyVelocity(const glm::vec3& velocity) {
    if (!engine || !object || !HasRigidbody()) return false;
    return engine->setRigidbodyVelocityFromScript(object->id, velocity);
}

bool ScriptContext::GetRigidbodyVelocity(glm::vec3& outVelocity) const {
    if (!engine || !object || !HasRigidbody()) return false;
    return engine->getRigidbodyVelocityFromScript(object->id, outVelocity);
}

bool ScriptContext::TeleportRigidbody(const glm::vec3& pos, const glm::vec3& rotDeg) {
    if (!engine || !object) return false;
    object->position = pos;
    object->rotation = NormalizeEulerDegrees(rotDeg);
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

void ScriptContext::AutoSetting(const std::string& key, bool& value) {
    if (!script) return;
    if (autoSettings.end() != std::find_if(autoSettings.begin(), autoSettings.end(),
        [&](const AutoSettingEntry& e){ return e.key == key; })) return;

    static std::unordered_map<std::string, bool> defaults;
    std::string scriptId = (!script->path.empty()) ? script->path : std::to_string(reinterpret_cast<uintptr_t>(script));
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

void ScriptContext::AutoSetting(const std::string& key, glm::vec3& value) {
    if (!script) return;
    if (autoSettings.end() != std::find_if(autoSettings.begin(), autoSettings.end(),
        [&](const AutoSettingEntry& e){ return e.key == key; })) return;

    static std::unordered_map<std::string, glm::vec3> defaults;
    std::string scriptId = (!script->path.empty()) ? script->path : std::to_string(reinterpret_cast<uintptr_t>(script));
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
    std::string scriptId = (!script->path.empty()) ? script->path : std::to_string(reinterpret_cast<uintptr_t>(script));
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
    }
    loaded.clear();
}
