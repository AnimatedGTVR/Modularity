---
title: C++ Scripting
description: Hot-compiled native C++ scripts, per-object state, ImGui inspectors, and runtime/editor hooks.
---

# C++ Scripting
Scripts in Modularity are native C++ code compiled into shared libraries and loaded at runtime. They run per scene object and can optionally draw ImGui UI in the inspector and in custom editor windows.

> Notes up front:
> - Scripts are not sandboxed. They can crash the editor/game if they dereference bad pointers or do unsafe work.
> - Always null-check `ctx.object` (objects can be deleted, disabled, or scripts can be detached).

## Table of contents
- [Quickstart](#quickstart)
- [Scripts.modu](#scriptsmodu)
- [How compilation works](#how-compilation-works)
- [Lifecycle hooks](#lifecycle-hooks)
- [ScriptContext](#scriptcontext)
- [ImGui in scripts](#imgui-in-scripts)
- [Per-script settings](#per-script-settings)
- [UI scripting](#ui-scripting)
- [IEnum tasks](#ienum-tasks)
- [Logging](#logging)
- [Scripted editor windows](#scripted-editor-windows)
- [Manual compile (CLI)](#manual-compile-cli)
- [Troubleshooting](#troubleshooting)
- [Templates](#templates)

## Quickstart
1. Create a script file under `Scripts/` (e.g. `Scripts/MyScript.cpp`).
2. Select an object in the scene.
3. In the Inspector, add/enable a script component and set its path:
   - In the **Scripts** section, set `Path` OR click **Use Selection** after selecting the file in the File Browser.
4. Compile the script:
   - In the File Browser, right-click the script file and choose **Compile Script**, or
   - In the Inspector’s script component menu, choose **Compile**.
5. Implement a tick hook (`TickUpdate`) and observe behavior in play mode.

## Scripts.modu
Each project has a `Scripts.modu` file (auto-created if missing). It controls compilation.

Common keys:
- `scriptsDir` - where script source files live (default: `Scripts`)
- `outDir` - where compiled binaries go (default: `Cache/ScriptBin`)
- `includeDir=...` - add include directories (repeatable)
- `define=...` - add preprocessor defines (repeatable)
- `linux.linkLib=...` - comma-separated link libs/flags for Linux (e.g. `dl,pthread`)
- `win.linkLib=...` - comma-separated link libs for Windows (e.g. `User32,Advapi32`)
- `cppStandard` - C++ standard (e.g. `c++20`)

Example:
```ini
scriptsDir=Scripts
outDir=Cache/ScriptBin
includeDir=../src
includeDir=../include
cppStandard=c++20
linux.linkLib=dl,pthread
win.linkLib=User32,Advapi32
```

## How compilation works
Modularity compiles scripts into shared libraries and loads them by symbol name.

- Source lives under `Scripts/`.
- Output binaries are written to `Cache/ScriptBin/`.
- Binaries are platform-specific:
  - Windows: `.dll`
  - Linux: `.so`

### Wrapper generation (important)
To reduce boilerplate, Modularity auto-generates a wrapper for these hook names **if it detects them in your script**:
- `Begin`
- `TickUpdate`
- `Update`
- `Spec`
- `TestEditor`

That wrapper exports `Script_Begin`, `Script_TickUpdate`, etc. This means you can usually write plain functions like:
```cpp
void TickUpdate(ScriptContext& ctx, float dt) {
    (void)dt;
    if (!ctx.object) return;
}
```

However:
- `Script_OnInspector` is **not** wrapper-generated. If you want inspector UI, you must export it explicitly with `extern "C"`.
- Scripted editor windows (`RenderEditorWindow`, `ExitRenderEditorWindow`) are also **not** wrapper-generated; export them explicitly with `extern "C"`.

## Lifecycle hooks
All hooks are optional. If a hook is missing, it is simply not called.

Hook list:
- `Script_OnInspector(ScriptContext&)` (manual export required)
- `Script_Begin(ScriptContext&, float deltaTime)` (wrapper-generated from `Begin`)
- `Script_TickUpdate(ScriptContext&, float deltaTime)` (wrapper-generated from `TickUpdate`)
- `Script_Update(ScriptContext&, float deltaTime)` (wrapper-generated from `Update`, used only if TickUpdate missing)
- `Script_Spec(ScriptContext&, float deltaTime)` (wrapper-generated from `Spec`)
- `Script_TestEditor(ScriptContext&, float deltaTime)` (wrapper-generated from `TestEditor`)

Runtime notes:
- `Begin` runs once per object instance (per script component instance).
- `TickUpdate` runs every frame (preferred).
- `Update` runs only if `TickUpdate` is not exported.
- `Spec/TestEditor` run every frame only while their global toggles are enabled (main menu -> Scripts).

## ScriptContext
`ScriptContext` is passed into most hooks and provides access to the engine, the owning object, and helper APIs.

Fields:
- `engine` (`Engine*`) - engine pointer
- `object` (`SceneObject*`) - owning object pointer (may be null)
- `script` (`ScriptComponent*`) - owning script component (settings storage)

### Object lookup
- `FindObjectByName(const std::string&)`
- `FindObjectById(int)`

### Transform helpers
- `SetPosition(const glm::vec3&)`
- `SetRotation(const glm::vec3&)` (degrees)
- `SetScale(const glm::vec3&)`
- `SetPosition2D(const glm::vec2&)` (UI position in pixels)

### UI helpers (Buttons/Sliders)
- `IsUIButtonPressed()`
- `IsUIInteractable()`, `SetUIInteractable(bool)`
- `GetUISliderValue()`, `SetUISliderValue(float)`
- `SetUISliderRange(float min, float max)`
- `SetUILabel(const std::string&)`, `SetUIColor(const glm::vec4&)`
- `GetUITextScale()`, `SetUITextScale(float)`
- `SetUISliderStyle(UISliderStyle)`
- `SetUIButtonStyle(UIButtonStyle)`
- `SetUIStylePreset(const std::string&)`
- `RegisterUIStylePreset(const std::string& name, const ImGuiStyle& style, bool replace = false)`

### Rigidbody helpers (3D)
- `HasRigidbody()`
- `SetRigidbodyVelocity(const glm::vec3&)`, `GetRigidbodyVelocity(glm::vec3& out)`
- `SetRigidbodyAngularVelocity(const glm::vec3&)`, `GetRigidbodyAngularVelocity(glm::vec3& out)`
- `AddRigidbodyForce(const glm::vec3&)`, `AddRigidbodyImpulse(const glm::vec3&)`
- `AddRigidbodyTorque(const glm::vec3&)`, `AddRigidbodyAngularImpulse(const glm::vec3&)`
- `SetRigidbodyRotation(const glm::vec3& rotDeg)`
- `TeleportRigidbody(const glm::vec3& pos, const glm::vec3& rotDeg)`

### Rigidbody2D helpers (UI/canvas only)
- `HasRigidbody2D()`
- `SetRigidbody2DVelocity(const glm::vec2&)`, `GetRigidbody2DVelocity(glm::vec2& out)`

### Audio helpers
- `HasAudioSource()`
- `PlayAudio()`, `StopAudio()`
- `SetAudioLoop(bool)`
- `SetAudioVolume(float)`
- `SetAudioClip(const std::string& path)`

### Settings + utility
- `GetSetting(key, fallback)`, `SetSetting(key, value)`
- `GetSettingBool(key, fallback)`, `SetSettingBool(key, value)`
- `GetSettingVec3(key, fallback)`, `SetSettingVec3(key, value)`
- `AutoSetting(key, bool|glm::vec3|buffer)`, `SaveAutoSettings()`
- `AddConsoleMessage(text, type)`
- `MarkDirty()`

## ImGui in scripts
Modularity uses Dear ImGui for editor UI. Scripts can draw ImGui in two places:

### Inspector UI (per object)
Export `Script_OnInspector(ScriptContext&)`:
```cpp
#include "ScriptRuntime.h"
#include "ThirdParty/imgui/imgui.h"

static bool autoRotate = false;

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    ImGui::Checkbox("Auto Rotate", &autoRotate);
    ctx.MarkDirty();
}
```

> Tip: `Script_OnInspector` must be exported exactly with `extern "C"` (it is not wrapper-generated).
> Important: Do not call ImGui functions (e.g., `ImGui::Text`) from `TickUpdate` or other runtime hooks. Those run before the ImGui frame is active and outside any window, which can crash.

### Scripted editor windows (custom tabs)
See [Scripted editor windows](#scripted-editor-windows).

## Per-script settings
Each `ScriptComponent` owns serialized key/value strings (`ctx.script->settings`). Use them to persist state with the scene.

### Direct settings
```cpp
void TickUpdate(ScriptContext& ctx, float) {
    if (!ctx.script) return;
    ctx.SetSetting("mode", "hard");
    ctx.MarkDirty();
}
```

### AutoSetting (recommended for inspector UI)
`AutoSetting` binds a variable to a key and loads/saves automatically when you call `SaveAutoSettings()`.
```cpp
extern "C" void Script_OnInspector(ScriptContext& ctx) {
    static bool enabled = false;
    ctx.AutoSetting("enabled", enabled);
    ImGui::Checkbox("Enabled", &enabled);
    ctx.SaveAutoSettings();
}
```

## UI scripting
UI elements are scene objects (Create -> 2D/UI). They render in the **Game Viewport** overlay.

### Button clicks
`IsUIButtonPressed()` is true only on the frame the click happens.
```cpp
void TickUpdate(ScriptContext& ctx, float) {
    if (ctx.IsUIButtonPressed()) {
        ctx.AddConsoleMessage("Button clicked!");
    }
}
```

### Sliders as meters (health/ammo)
Set `Interactable` to false to make a slider read-only.
```cpp
void TickUpdate(ScriptContext& ctx, float) {
    ctx.SetUIInteractable(false);
    ctx.SetUISliderStyle(UISliderStyle::Fill);
    ctx.SetUISliderRange(0.0f, 100.0f);
    ctx.SetUISliderValue(health);
}
```

### Style presets
You can register custom ImGui style presets in code and then select them per UI element in the Inspector.
```cpp
void Begin(ScriptContext& ctx, float) {
    ImGuiStyle style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.50f, 0.90f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.60f, 1.00f, 1.00f);
    ctx.RegisterUIStylePreset("Ocean", style, true);
}
```
Then select **UI -> Style Preset** on a button or slider.

### Finding other UI objects
```cpp
void TickUpdate(ScriptContext& ctx, float) {
    if (SceneObject* other = ctx.FindObjectByName("UI Button 3")) {
        if (other->type == ObjectType::UIButton && other->ui.buttonPressed) {
            ctx.AddConsoleMessage("Other button clicked!");
        }
    }
}
```

## IEnum tasks
Modularity provides lightweight, opt-in “tasks” you can start/stop per script component instance.

Important: In this version, an IEnum task is **just a function** with signature `void(ScriptContext&, float)` that is called every frame while it’s registered.

Start/stop macros:
- `IEnum_Start(fn)` / `IEnum_Stop(fn)` / `IEnum_Ensure(fn)`

Example (toggle rotation without cluttering TickUpdate):
```cpp
static bool autoRotate = false;
static glm::vec3 speed = {0, 45, 0};

static void RotateTask(ScriptContext& ctx, float dt) {
    if (!ctx.object) return;
    ctx.SetRotation(ctx.object->rotation + speed * dt);
}

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    ImGui::Checkbox("Auto Rotate", &autoRotate);
    if (autoRotate) IEnum_Ensure(RotateTask);
    else            IEnum_Stop(RotateTask);
    ctx.MarkDirty();
}
```

Notes:
- Tasks are stored per `ScriptComponent` instance.
- Don’t spam logs every frame inside a task; use “warn once” patterns.

## Logging
Use `ctx.AddConsoleMessage(text, type)` to write to the editor console.

Typical types:
- `ConsoleMessageType::Info`
- `ConsoleMessageType::Success`
- `ConsoleMessageType::Warning`
- `ConsoleMessageType::Error`

Warn-once pattern:
```cpp
static bool warned = false;
if (!warned) {
    ctx.AddConsoleMessage("[MyScript] Something looks off", ConsoleMessageType::Warning);
    warned = true;
}
```

## Scripted editor windows
Scripts can expose ImGui-powered editor tabs by exporting:
- `RenderEditorWindow(ScriptContext& ctx)` (called every frame while tab is open)
- `ExitRenderEditorWindow(ScriptContext& ctx)` (called once when tab closes)

Example:
```cpp
#include "ScriptRuntime.h"
#include "ThirdParty/imgui/imgui.h"

extern "C" void RenderEditorWindow(ScriptContext& ctx) {
    ImGui::TextUnformatted("Hello from script!");
    if (ImGui::Button("Log")) {
        ctx.AddConsoleMessage("Editor window clicked");
    }
}

extern "C" void ExitRenderEditorWindow(ScriptContext& ctx) {
    (void)ctx;
}
```

How to open:
1. Compile the script so the binary is updated under `Cache/ScriptBin/`.
2. In the main menu, go to **View -> Scripted Windows** and toggle the entry.

## Manual compile (CLI)
Linux:
```bash
g++ -std=c++20 -fPIC -O0 -g -I../src -I../include -c SampleInspector.cpp -o ../Cache/ScriptBin/SampleInspector.o
g++ -shared ../Cache/ScriptBin/SampleInspector.o -o ../Cache/ScriptBin/SampleInspector.so -ldl -lpthread
```

Windows:
```bat
cl /nologo /std:c++20 /EHsc /MD /Zi /Od /I ..\\src /I ..\\include /c SampleInspector.cpp /Fo ..\\Cache\\ScriptBin\\SampleInspector.obj
link /nologo /DLL ..\\Cache\\ScriptBin\\SampleInspector.obj /OUT:..\\Cache\\ScriptBin\\SampleInspector.dll User32.lib Advapi32.lib
```

## Troubleshooting
- **Script not running**
  - Ensure the object is enabled and the script component is enabled.
  - Ensure the script path points to a real file and the compiled binary exists.
- **No inspector UI**
  - `Script_OnInspector` must be exported with `extern "C"` (no wrapper is generated for it).
- **Changes not saved**
  - Call `ctx.MarkDirty()` after mutating transforms/settings you want to persist.
- **Editor window not showing**
  - Ensure `RenderEditorWindow` is exported with `extern "C"` and the binary is up to date.
- **Custom UI style preset not listed**
  - Ensure `RegisterUIStylePreset(...)` ran (e.g. in `Begin`) before selecting it in the Inspector.
- **Hard crash**
  - Add null checks, avoid static pointers to scene objects, and don’t hold references across frames unless you can validate them.

## Templates
### Minimal runtime script (wrapper-based)
```cpp
#include "ScriptRuntime.h"

void TickUpdate(ScriptContext& ctx, float /*dt*/) {
    if (!ctx.object) return;
}
```

### Minimal script with inspector (manual export)
```cpp
#include "ScriptRuntime.h"
#include "ThirdParty/imgui/imgui.h"

void TickUpdate(ScriptContext& ctx, float /*dt*/) {
    if (!ctx.object) return;
}

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    ImGui::TextDisabled("Hello from inspector!");
    (void)ctx;
}
```
### Text
Use **UI Text** objects for on-screen text. Update their `label` and size from scripts:
```cpp
void TickUpdate(ScriptContext& ctx, float) {
    if (SceneObject* text = ctx.FindObjectByName("UI Text 2")) {
        if (text->type == ObjectType::UIText) {
            text->ui.label = "Speed: 12.4";
            text->ui.textScale = 1.4f;
            ctx.MarkDirty();
        }
    }
}
```

### FPS display example
Attach `Scripts/FPSDisplay.cpp` to a **UI Text** object to show FPS. The inspector exposes a checkbox to clamp FPS to 120.
