// Minimal sample script demonstrating Script_OnInspector usage.
// Build via the engine’s “Compile Script” action or:
// Linux:  g++ -std=c++20 -fPIC -O0 -g -I../src -I../include -c SampleInspector.cpp -o ../Cache/ScriptBin/SampleInspector.o
//         g++ -shared ../Cache/ScriptBin/SampleInspector.o -o ../Cache/ScriptBin/SampleInspector.so -ldl -lpthread
// Windows: cl /nologo /std:c++20 /EHsc /MD /Zi /Od /I ..\src /I ..\include /c SampleInspector.cpp /Fo ..\Cache\ScriptBin\SampleInspector.obj
//          link /nologo /DLL ..\Cache\ScriptBin\SampleInspector.obj /OUT:..\Cache\ScriptBin\SampleInspector.dll User32.lib Advapi32.lib

#include "ScriptRuntime.h"
#include "SceneObject.h"
#include "ThirdParty/imgui/imgui.h"
#include <string>
#include <algorithm>
#include <sstream>

namespace {
bool autoRotate = false;
glm::vec3 spinSpeed = glm::vec3(0.0f, 45.0f, 0.0f);
glm::vec3 offset = glm::vec3(0.0f, 1.0f, 0.0f);
char targetName[128] = "MyTarget";
int settingsLoadedForId = -1;
ScriptComponent* settingsLoadedForScript = nullptr;

void setSetting(ScriptContext& ctx, const std::string& key, const std::string& value) {
    if (!ctx.script) return;
    auto it = std::find_if(ctx.script->settings.begin(), ctx.script->settings.end(),
                           [&](const ScriptSetting& s) { return s.key == key; });
    if (it != ctx.script->settings.end()) {
        it->value = value;
    } else {
        ctx.script->settings.push_back({key, value});
    }
}

std::string getSetting(const ScriptContext& ctx, const std::string& key, const std::string& fallback = "") {
    if (!ctx.script) return fallback;
    auto it = std::find_if(ctx.script->settings.begin(), ctx.script->settings.end(),
                           [&](const ScriptSetting& s) { return s.key == key; });
    return (it != ctx.script->settings.end()) ? it->value : fallback;
}

void loadSettings(ScriptContext& ctx) {
    if (!ctx.script || !ctx.object) return;
    if (settingsLoadedForId == ctx.object->id && settingsLoadedForScript == ctx.script) return;Segmentation fault (core dumped)
    settingsLoadedForId = ctx.object->id;
    settingsLoadedForScript = ctx.script;

    auto parseBool = [](const std::string& v, bool def) {
        if (v == "1" || v == "true") return true;
        if (v == "0" || v == "false") return false;
        return def;
    };

    auto parseVec3 = [](const std::string& v, const glm::vec3& def) {
        glm::vec3 out = def;
        std::stringstream ss(v);
        std::string part;
        for (int i = 0; i < 3 && std::getline(ss, part, ','); ++i) {
            try { out[i] = std::stof(part); } catch (...) {}
        }
        return out;
    };

    autoRotate = parseBool(getSetting(ctx, "autoRotate", autoRotate ? "1" : "0"), autoRotate);
    spinSpeed = parseVec3(getSetting(ctx, "spinSpeed", ""), spinSpeed);
    offset = parseVec3(getSetting(ctx, "offset", ""), offset);
    std::string tgt = getSetting(ctx, "targetName", targetName);
    if (!tgt.empty()) {
        std::snprintf(targetName, sizeof(targetName), "%s", tgt.c_str());
    }
}

void persistSettings(ScriptContext& ctx) {
    setSetting(ctx, "autoRotate", autoRotate ? "1" : "0");
    setSetting(ctx, "spinSpeed",
               std::to_string(spinSpeed.x) + "," + std::to_string(spinSpeed.y) + "," + std::to_string(spinSpeed.z));
    setSetting(ctx, "offset",
               std::to_string(offset.x) + "," + std::to_string(offset.y) + "," + std::to_string(offset.z));
    setSetting(ctx, "targetName", targetName);
    ctx.MarkDirty();
}

void applyAutoRotate(ScriptContext& ctx, float deltaTime) {
    if (!autoRotate || !ctx.object) return;
    ctx.SetRotation(ctx.object->rotation + spinSpeed * deltaTime);
}
} // namespace

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    loadSettings(ctx);

    ImGui::TextUnformatted("SampleInspector");
    ImGui::Separator();

    if (ImGui::Checkbox("Auto Rotate", &autoRotate)) {
        persistSettings(ctx);
    }
    if (ImGui::DragFloat3("Spin Speed (deg/s)", &spinSpeed.x, 1.0f, -360.0f, 360.0f)) {
        persistSettings(ctx);
    }
    if (ImGui::DragFloat3("Offset", &offset.x, 0.1f)) {
        persistSettings(ctx);
    }

    ImGui::InputText("Target Name", targetName, sizeof(targetName));
    persistSettings(ctx);

    if (ctx.object) {
        ImGui::TextDisabled("Attached to: %s (id=%d)", ctx.object->name.c_str(), ctx.object->id);

        if (ImGui::Button("Apply Offset To Self")) {
            ctx.SetPosition(ctx.object->position + offset);
        }
    }

    if (ImGui::Button("Nudge Target")) {
        if (SceneObject* target = ctx.FindObjectByName(targetName)) {
            target->position += offset;
        }
    }
}

// New lifecycle hooks supported by the compiler wrapper. These are optional stubs demonstrating usage.
void Begin(ScriptContext& ctx, float /*deltaTime*/) {
    // Initialize per-script state here.
    loadSettings(ctx);
}

void Spec(ScriptContext& ctx, float deltaTime) {
    // Special/speculative mode logic could go here.
    applyAutoRotate(ctx, deltaTime);
}

void TestEditor(ScriptContext& ctx, float deltaTime) {
    // Editor-time behavior entry point.
    applyAutoRotate(ctx, deltaTime);
}

void TickUpdate(ScriptContext& ctx, float deltaTime) {
    applyAutoRotate(ctx, deltaTime);
}
