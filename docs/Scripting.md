---
title: Scripting (ModuCPP, C++, C, and C#)
description: High-level ModuCPP transpiled to native C++, plus raw C++/C and managed C# scripting with shared runtime hooks.
---

# Scripting (ModuCPP, C++, C, and C#)
Modularity supports:
- High-level `ModuCPP` authoring syntax (`.moducpp`, or `.cpp` files that use `public class ... : ModuBehaviour`) transpiled to native C++ before compile.
- Native `C++` scripts (`.cpp`, `.cc`, `.cxx`) compiled to shared libs.
- Native `C` scripts (`.c`) through `ScriptRuntimeCAPI.h` and `Modu_*` hooks.
- Managed `C#` scripts via Mono using `Scripts/Managed/ModuCPP.cs`.

Scripts run per `ScriptComponent` instance on a scene object.

## Quickstart
1. Create a script file under `Assets/Scripts/` (or whatever `scriptsDir` is in `scripts.modu`).
   - High-level syntax: use `.moducpp` (recommended), or `.cpp` with `public class ... : ModuBehaviour`.
   - Low-level native: use `.cpp` / `.cc` / `.cxx` / `.c`.
2. Add a **Script** component in the Inspector.
3. Set language and path:
   - `C++/C`: path to source file.
   - `C#`: path to `.cs`, `.csproj`, or managed `.dll`, plus a type name.
4. Compile:
   - Right-click file in File Browser -> **Compile Script**, or
   - Script component menu -> **Compile**.
5. Enter Play/Spec/Test and verify `TickUpdate` runs.

## Current scripting stack
Use this as the short version for documentation pages:

| Language | Best use | Current strengths | Current limits |
| --- | --- | --- | --- |
| `ModuCPP` | Preferred gameplay/editor authoring | High-level class syntax, generated inspectors, object-list editors, dialogue/menu/interactable inspector widgets, helper facades for input/audio/sprite/2D movement | Still compiles down to native C++, so native build toolchain is required |
| Native `C++` | Lowest-level engine-adjacent scripts | Full `ScriptContext`, direct ImGui/editor hooks, all current runtime helpers, easiest place to add new engine-side APIs | More boilerplate than ModuCPP |
| Native `C` | Small/runtime-only bridge scripts | Stable `Modu_*` hook names, basic transform/physics/animation/settings/sprite inspector helpers | Smaller surface than C++/ModuCPP; does not expose the full helper layer or the new custom inspector DSL |
| Managed `C#` | Tooling-heavy scripts or teams that want managed code | Mono-hosted `ModuCPP.Context`, auto-inspector support, object/UI/physics/audio/sprite APIs, `Vec2` and `RaycastHit`, bridge ABI `version = 6` | Requires Mono-enabled build and managed project output |

## Shipped mechanics and example systems
The current repo is no longer just a raw scripting sandbox. The sample scripts already cover a set of reusable gameplay/editor mechanics:

- `AutoEnableAndDisableListsOfObjectsAfterAmountOfTime.moducpp`: timed enable/disable toggling for lists of scene objects via `each list.state(...)`.
- `DialogueSystem.moducpp`: localized dialogue playback with typewriter timing, text effects, per-line mouth state toggles, open/close delays, end-of-dialogue object actions, audio cues, and runtime inspector status.
- `InteractableObject.moducpp`: proximity/key-driven interactions, one-time-use logic, selection-state object toggles, option lists, and dialogue handoff through serialized interaction requests.
- `MainMenuController.moducpp`: keyboard-driven menu cursor/heart movement, configurable orientation, move/select sounds, startup/input delay control, and per-item enable/disable actions.
- `TopDownMovement2D.moducpp`: 2D WASD movement, optional Rigidbody2D driving, directional idle/walk clip grids, sprint speed, acceleration/drag, and randomized footstep audio.
- `StandaloneMovementController.moducpp`: reusable grounded 3D locomotion using `ScriptContext::TickStandaloneMovement(...)`, with inspector-driven tuning for movement, look, gravity, collider, and rigidbody behavior.
- `FPSDisplay.moducpp`: UI text update + optional FPS cap control.
- `RigidbodyTest.moducpp`: launch, teleport, and live rigidbody readback examples.
- `EditorWindowSample.moducpp` and `AnimationWindow.moducpp`: scripted editor tabs, custom editor UI, and animation-authoring workflow examples.
- `SampleInspector.moducpp` and `SampleInspector Simplified.moducpp`: manual inspector patterns, `Config<T>()`/`State<T>()`, and direct migration from raw native C++ into ModuCPP.

## ModuCPP transpiler architecture (frontend only)
The ModuCPP layer is a compile-time frontend. Runtime is unchanged.

Pipeline for ModuCPP sources:
1. Parse high-level class syntax (`public class X : ModuBehaviour`).
2. Generate equivalent native C++ (`<script>.moducpp.gen.cpp`) in script build output.
3. Run the existing native hook wrapper detection/export flow (`Script_Begin`, `Script_TickUpdate`, `Script_OnInspector`, etc.).
4. Compile/link with the same shared-library build/load path used by raw native scripts.

Unchanged backend/runtime pieces:
- `ScriptContext` model and `MODU_SCRIPT(ctx)` access pattern.
- Existing native hook detection and wrappers.
- Existing `.so`/`.dll` compile/load flow.
- Inspector persistence via existing `BindSetting`/`AutoSetting` path.
- Mono/C# runtime support.

## ModuCPP syntax rules
Supported high-level rules in current transpiler:
- `public class MyScript : ModuBehaviour`
- Public persisted fields:
  - `public float/int/bool/vec3/string fieldName = ...;`
  - `public List<SceneObj*> fieldName;`
  - Optional field metadata:
    - `@range(min, max)` for numeric fields
    - `@step(value)` for `float`/`vec3` drag speed
    - Example: `public float walkSpeed = 4.0f @range(0.0f, 50.0f) @step(0.1f);`
- Private runtime-only fields:
  - `private <type> fieldName = ...;`
- Lifecycle methods:
  - `void TickUpdate(MODU_obj, float dt)` -> native `void TickUpdate(ScriptContext& ctx, float dt)` plus injected `MODU_SCRIPT(ctx)`.
  - If a method body already contains `MODU_SCRIPT(...)` and the class has no declared ModuCPP fields, transpiler skips auto prelude injection for that method (useful for direct native-port method bodies).
- List action shorthand:
  - `each someList.state(true);`
  - transpiles to null-safe iteration over resolved object refs and enable/disable application.
- Optional high-level custom inspector block:
  - Config binding:
    - `Config(Type, var);` emits `Type var = loadConfig(ctx);`
    - `AutoSave(var);` (or `Save(var);`) emits `if (changed) saveConfig(ctx, var);`
    - When `AutoSave(...)`/`Save(...)` is present, transpiler skips default `ctx.SaveAutoSettings()` fallback.
  - Layout containers:
    - `Tabs { ... }`, `Tab("Name") { ... }`, `Section("Name") { ... }`, `Group("Name") { ... }`, `Foldout("Name") { ... }`
  - Inspector widgets (auto-label or explicit label overloads):
    - `Toggle(value)` / `Toggle("Label", value)`
    - `Slider(value, min, max)` / `Slider("Label", value, min, max)`
    - `Number(value)`, `String(value)`, `ObjectRef(value)`, `ObjectList(value)`, `AudioClip(value)`, `Enum(value)` (all also support `"Label"` as first arg)
    - `DialogueLines(value)`, `InteractionOptions(value)`, `TextEffectFlags(value)` (all also support `"Label"` overload)
    - `MenuActions(menuItemRefs, actions)` / `MenuActions("Label", menuItemRefs, actions)`
    - `RuntimeDialogueStatus()`, `RuntimeInteractableStatus()`, `RuntimeMenuStatus()`, `ClipGrid(idle, walk)`, `SoundSet("Label", sounds)`, `Header("...")`, `Separator()`, `Run(expr)`
  - This remains high-level in `.moducpp`; transpiler converts it to backend ImGui/editor calls.
  - If you use custom public field types, provide `inspector { ... }` (or `Script_OnInspector`) so UI can be authored explicitly.

Migration fallback:
- `.moducpp` files without `public class ... : ModuBehaviour` are treated as legacy native C++ passthrough by the transpiler.
- This keeps old script bodies compiling while you migrate incrementally to high-level ModuCPP syntax.

Inspector and persistence behavior:
- Public fields are generated into `Script_OnInspector(ScriptContext&)`.
- Public fields bind through `BindSetting(ctx, key, value)` and persist through current native auto-settings flow.
- Private fields are placed in generated `State<T>` storage and are runtime-only.
- If you implement your own `Script_OnInspector(MODU_obj)`, that method is used and auto inspector generation is skipped.
- Default `.moducpp` authoring does not require writing ImGui calls; inspector widgets are transpiler-generated from public fields/metadata.

## `#include "ModuCPP"` helper layer
The `ModuCPP` header is not just for the transpiler. It also exposes a thin native helper layer that raw C++ scripts can use directly.

Core patterns:
- `MODU_SCRIPT(ctx)` installs a scoped thread-local context and gives you `obj` as a shorthand for `ctx.object`.
- `Config<T>()` stores persisted per-script-instance config data.
- `State<T>()` stores runtime-only per-script-instance state.
- `BindSetting(...)`, `BindArray(...)`, and `BindArray2D(...)` bind primitive, string, and fixed-size array data to inspector/settings persistence.

Inspector helpers:
- `EditBool`, `EditFloat`, `EditInt`, `EditVec3`, and `EditString` are direct native helpers for persistent inspector widgets.
- `EditDirectionalClipGrid(...)` builds a directional idle/walk sprite clip picker.
- `EditSoundSet(...)` builds a drag-drop-friendly list of audio clip slots.

Gameplay helpers:
- `input.WASD()`, `input.WASDNormalized()`, `input.sprint()`, and `input.jump()` wrap common input polling.
- `KeyDown(...)` and `KeyPressed(...)` expose direct key checks, with constants like `KEY_W`, `KEY_SPACE`, and `KEY_ENTER`.
- `TryMoveRigidbody2D(...)`, `moveRigidbody2D(...)`, and `movePosition2D(...)` cover common 2D movement flows.
- `audio.HasSource()`, `audio.Play()`, `audio.Stop()`, and `audio.PlayOneShot(...)` wrap audio source operations.
- `sprite.HasClips()`, `sprite.ClipCount()`, `sprite.ClipIndex()`, `sprite.SetClip(...)`, and `sprite.ClipNameAt(...)` wrap sprite clip control.
- `warnOnce(...)` and `warnMissingComponentOnce(...)` are convenience logging guards for missing component warnings.

Practical rule: if you need a custom native script but still want the newer ergonomic helpers, include `ModuCPP` instead of only `ScriptRuntime.h`.

## ModuCPP source + generated C++ example
Authoring source:
```cpp
#include "ModuCPP"

public class AutoEnableAndDisableListsOfObjectsAfterAmountOfTime : ModuBehaviour {
    public List<SceneObj*> enable;
    public List<SceneObj*> disable;
    public float interval = 1.0f;
    private float timer = 0.0f;

    void TickUpdate(MODU_obj, float dt) {
        timer += dt;
        if (timer < interval) return;

        each enable.state(true);
        each disable.state(false);

        timer = 0.0f;
    }
}
```

Generated native C++ shape (simplified):
```cpp
namespace ModuCPPTranspiled_AutoEnableAndDisableListsOfObjectsAfterAmountOfTime {
struct AutoEnableAndDisableListsOfObjectsAfterAmountOfTimeConfig {
    std::string enableRaw;
    std::string disableRaw;
    float interval = 1.0f;
};
struct AutoEnableAndDisableListsOfObjectsAfterAmountOfTimeState {
    float timer = 0.0f;
};
}

extern "C" void Script_OnInspector(ScriptContext& ctx) {
    MODU_SCRIPT(ctx);
    auto& config = ModuCPP::Config<ModuCPPTranspiled_AutoEnableAndDisableListsOfObjectsAfterAmountOfTime::
        AutoEnableAndDisableListsOfObjectsAfterAmountOfTimeConfig>();
    ModuCPPTranspiled_AutoEnableAndDisableListsOfObjectsAfterAmountOfTime::BindConfig(ctx, config);
    // Generated list editors + float editor, then ctx.SaveAutoSettings() on change.
}

void TickUpdate(ScriptContext& ctx, float dt) {
    MODU_SCRIPT(ctx);
    auto& config = ModuCPP::Config<ModuCPPTranspiled_AutoEnableAndDisableListsOfObjectsAfterAmountOfTime::
        AutoEnableAndDisableListsOfObjectsAfterAmountOfTimeConfig>();
    auto& state = ModuCPP::State<ModuCPPTranspiled_AutoEnableAndDisableListsOfObjectsAfterAmountOfTime::
        AutoEnableAndDisableListsOfObjectsAfterAmountOfTimeState>();
    auto& interval = config.interval;
    auto& timer = state.timer;
    const std::string& enable = config.enableRaw;
    const std::string& disable = config.disableRaw;

    timer += dt;
    if (timer < interval) return;

    for (SceneObject* _moduObj :
         ModuCPPTranspiled_AutoEnableAndDisableListsOfObjectsAfterAmountOfTime::ResolveObjectList(ctx, enable)) {
        if (!_moduObj) continue;
        ModuCPPTranspiled_AutoEnableAndDisableListsOfObjectsAfterAmountOfTime::SetResolvedObjectEnabled(ctx, _moduObj, true);
    }
    for (SceneObject* _moduObj :
         ModuCPPTranspiled_AutoEnableAndDisableListsOfObjectsAfterAmountOfTime::ResolveObjectList(ctx, disable)) {
        if (!_moduObj) continue;
        ModuCPPTranspiled_AutoEnableAndDisableListsOfObjectsAfterAmountOfTime::SetResolvedObjectEnabled(ctx, _moduObj, false);
    }

    timer = 0.0f;
}
```

Compatibility:
- Raw native C++ scripts remain supported unchanged.
- C scripts (`Modu_*`) remain supported unchanged.
- Managed C# scripts remain supported unchanged.

## Hook lifecycle
All hooks are optional.

Runtime/editor order:
- `Begin`/`Script_Begin`: first run once per object instance.
- `TickUpdate`/`Script_TickUpdate`: every frame.
- `Update`/`Script_Update`: fallback if `TickUpdate` is missing.
- `Spec` and `TestEditor`: called while those global modes are active.
- `OnInspector`: called in the Inspector panel when object is selected.
- `RenderEditorWindow` / `ExitRenderEditorWindow`: scripted editor tab open/close hooks.

## C++ scripting
For C++ scripts, the compiler auto-wraps these names when detected:
- `Begin`
- `TickUpdate`
- `Update`
- `Spec`
- `TestEditor`

Inspector/editor hooks:
- `Script_OnInspector(ScriptContext&)`
- `RenderEditorWindow(ScriptContext&)`
- `ExitRenderEditorWindow(ScriptContext&)`

Accepted signatures for the auto wrapper:
- `void Hook(ScriptContext&, float dt)`
- `void Hook(ScriptContext&)`
- `void Hook(float dt)`
- `void Hook()`

Minimal C++ example:
```cpp
#include "ScriptRuntime.h"

void TickUpdate(ScriptContext& ctx, float dt) {
    if (!ctx.object) return;
    ctx.SetRotation(ctx.object->rotation + glm::vec3(0.0f, 45.0f * dt, 0.0f));
}
```

When to use native C++ instead of ModuCPP:
- You want full control over generated symbols and do not want the transpiler involved.
- You are adding new low-level runtime/editor integration and want direct `ScriptContext` access.
- You want to mix `ScriptRuntime.h` with the convenience helpers from `#include "ModuCPP"` manually.

## C scripting (C API bridge)
Include `ScriptRuntimeCAPI.h` and implement `Modu_*` hooks.

```c
#include "ScriptRuntimeCAPI.h"

void Modu_TickUpdate(ModuScriptContext* ctx, float dt) {
    ModuVec3 pos = Modu_GetPosition(ctx);
    pos.x += dt;
    Modu_SetPosition(ctx, pos);
}
```

### C hooks
Supported names (all optional):
- `Modu_Begin`
- `Modu_TickUpdate`
- `Modu_Update`
- `Modu_Spec`
- `Modu_TestEditor`
- `Modu_OnInspector`
- `Modu_RenderEditorWindow`
- `Modu_ExitRenderEditorWindow`

Accepted signatures per hook:
- `void Hook(ModuScriptContext* ctx, float dt)`
- `void Hook(ModuScriptContext* ctx)`
- `void Hook(float dt)`
- `void Hook()`

### C API reference (from `include/ScriptRuntimeCAPI.h`)
Most `int` returns are boolean (`1` success, `0` fail).

What the C bridge currently targets:
- Core object state and transforms
- Basic rigidbody helpers
- Animation playback state
- Input/mouse-look helpers
- Settings and console logging
- Basic sprite clip control
- Lightweight inspector widgets

If you need the newer ModuCPP inspector DSL, helper facades, or the full high-level movement/dialogue/menu examples, use ModuCPP/native C++ instead of C.

Object + transform:
- `Modu_GetObjectId`
- `Modu_IsObjectEnabled`, `Modu_SetObjectEnabled`
- `Modu_GetPosition`, `Modu_GetRotation`, `Modu_GetScale`
- `Modu_SetPosition`, `Modu_SetRotation`, `Modu_SetScale`

Rigidbody + collision:
- `Modu_SetRigidbodyVelocity`, `Modu_GetRigidbodyVelocity`
- `Modu_AddRigidbodyForce`
- `Modu_SetRigidbodyRotation`
- `Modu_EnsureCapsuleCollider`, `Modu_EnsureRigidbody`

Animation:
- `Modu_HasAnimation`
- `Modu_PlayAnimation`, `Modu_StopAnimation`, `Modu_PauseAnimation`, `Modu_ReverseAnimation`
- `Modu_SetAnimationTime`, `Modu_GetAnimationTime`, `Modu_IsAnimationPlaying`
- `Modu_SetAnimationLoop`, `Modu_SetAnimationPlaySpeed`, `Modu_SetAnimationPlayOnAwake`

Input + movement:
- `Modu_IsSprintDown`, `Modu_IsJumpDown`
- `Modu_GetMoveInputWASD`
- `Modu_ApplyMouseLook`
- `Modu_RaycastClosestDetailed`

Settings + logging:
- `Modu_GetSettingFloat`, `Modu_SetSettingFloat`
- `Modu_GetSettingBool`, `Modu_SetSettingBool`
- `Modu_GetSettingString`, `Modu_SetSettingString`
- `Modu_AddConsoleMessage`

Sprite clips:
- `Modu_GetSpriteClipCount`, `Modu_GetSpriteClipIndex`
- `Modu_SetSpriteClipIndex`, `Modu_SetSpriteClipName`
- `Modu_GetSpriteClipName`, `Modu_GetSpriteClipNameAt`

Inspector UI helpers:
- `Modu_InspectorText`, `Modu_InspectorSeparator`
- `Modu_InspectorDragFloat`, `Modu_InspectorDragFloat2`, `Modu_InspectorDragFloat3`
- `Modu_InspectorCheckbox`
- `Modu_InspectorObject`

Console levels:
- `MODU_CONSOLE_INFO`
- `MODU_CONSOLE_WARNING`
- `MODU_CONSOLE_ERROR`
- `MODU_CONSOLE_SUCCESS`

### C example: inspector + persisted settings
```c
#include "ScriptRuntimeCAPI.h"

static float speed = 45.0f;
static int enabled = 1;

void Modu_Begin(ModuScriptContext* ctx) {
    speed = Modu_GetSettingFloat(ctx, "speed", speed);
    enabled = Modu_GetSettingBool(ctx, "enabled", enabled);
}

void Modu_TickUpdate(ModuScriptContext* ctx, float dt) {
    if (!enabled) return;
    ModuVec3 rot = Modu_GetRotation(ctx);
    rot.y += speed * dt;
    Modu_SetRotation(ctx, rot);
}

void Modu_OnInspector(ModuScriptContext* ctx) {
    Modu_InspectorText(ctx, "Rotate (C API)");
    if (Modu_InspectorCheckbox(ctx, "Enabled", &enabled)) {
        Modu_SetSettingBool(ctx, "enabled", enabled);
    }
    if (Modu_InspectorDragFloat(ctx, "Speed", &speed, 0.1f, 0.0f, 720.0f, "%.2f")) {
        Modu_SetSettingFloat(ctx, "speed", speed);
    }
}
```

## C# managed scripting
Managed scripts use:
- `Scripts/Managed/ModuCPP.csproj`
- `Scripts/Managed/ModuCPP.cs` (`ModuCPP.Context`, `ModuCPP.ImGui`, `ModuCPP.Inspector`)

Compile command:
```bash
dotnet build Scripts/Managed/ModuCPP.csproj -c Debug
```

In Inspector, set Script component:
- `Language = C#`
- `Assembly Path =` `.cs`, `.csproj`, or `.dll`
- `Type = Namespace.ClassName` (or class name if no namespace)

Notes:
- If path is `.cs` or `.csproj`, engine resolves the runtime assembly to managed output DLL.
- Engine expects `ModuCPP.Host.SetNativeApi` in the assembly (provided by `ModuCPP.cs`).
- If `Script_OnInspector` is missing, engine tries auto inspector via `ModuCPP.Inspector.RenderAuto`.
- Requires Mono-enabled build (`MODULARITY_USE_MONO=ON`) and a valid Mono runtime.
- Current managed bridge ABI is `version = 6`.
- `ModuCPP.cs` now includes `Vec2` and `RaycastHit`, so 2D motion and detailed raycast results are first-class in managed scripts.
- The managed wrapper keeps compatibility guards for older native layouts by checking `Api.Version` before binding newer delegates.

### C# hooks and signatures
Recognized names (with or without `Script_` prefix):
- `Begin`
- `TickUpdate`
- `Update`
- `Spec`
- `TestEditor`
- `OnInspector`

Accepted method signatures:
- `void Hook(IntPtr ctx, float deltaTime)`
- `void Hook(IntPtr ctx)`

Rules:
- `OnInspector` uses the `IntPtr ctx` form.
- Methods can be instance or static.

### C# API (`ModuCPP.Context`) full usage
Object query:
- `ObjectId`, `SelectedObjectId`, `SceneObjectCount`
- `FindObjectByName`, `FindObjectById`, `GetObjectByIndex`, `GetObjectName`

Transform:
- `Position`, `Rotation`, `Scale`
- `SetPosition2D(Vec2)`

Object state + tags/layers:
- `IsObjectEnabled`
- `Layer`
- `Tag`
- `HasTag(string)`, `IsInLayer(int)`

Input + movement:
- `IsSprintDown()`, `IsJumpDown()`
- `GetMoveInputWASD(float pitchDeg, float yawDeg)`
- `ApplyMouseLook(ref pitch, ref yaw, sensitivity, maxDelta, dt, requireMouseButton=false)`

Rigidbody / physics:
- `HasRigidbody`, `EnsureRigidbody(...)`, `EnsureCapsuleCollider(...)`
- `RigidbodyVelocity`
- `AddRigidbodyForce`, `AddRigidbodyImpulse`
- `HasRigidbody2D`, `Rigidbody2DVelocity`
- `AddRigidbodyVelocity`, `SetRigidbodyAngularVelocity`, `TryGetRigidbodyAngularVelocity`
- `AddRigidbodyTorque`, `AddRigidbodyAngularImpulse`
- `SetRigidbodyYaw`, `SetRigidbodyRotation`, `TeleportRigidbody`
- `RaycastClosestDetailed(..., out RaycastHit)`

Animation:
- `HasAnimation`
- `PlayAnimation`, `StopAnimation`, `PauseAnimation`, `ReverseAnimation`
- `SetAnimationTime`, `GetAnimationTime`, `IsAnimationPlaying`
- `SetAnimationLoop`, `SetAnimationPlaySpeed`, `SetAnimationPlayOnAwake`

UI:
- `IsUIButtonPressed()`
- `IsUIInteractable`
- `UISliderValue`, `SetUISliderRange`
- `SetUILabel`, `SetUIColor`
- `UITextScale`
- `SetUISliderStyle(int)`, `SetUIButtonStyle(int)`, `SetUIStylePreset(string)`
- `SetFPSCap(bool enabled, float cap = 120f)`

Sprite:
- `SpriteClipCount`, `SpriteClipIndex`
- `GetSpriteClipName`, `GetSpriteClipNameAt`
- `SetSpriteClipIndex`, `SetSpriteClipName`
- `SpriteAlpha`
- `FadeSpriteAlpha`, `FadeSpriteToClipIndex`, `FadeSpriteToClipName`

Audio:
- `HasAudioSource`
- `PlayAudio`, `StopAudio`
- `SetAudioLoop`, `SetAudioVolume`, `SetAudioClip`
- `PlayAudioOneShot`

Settings + console:
- `GetSettingFloat/Bool/String`
- `SetSettingFloat/Bool/String`
- `AutoSetting` overloads: `bool`, `float`, `Vec3`, `string`, `ModuObject`
- `AutoSettingsFrom(object instance, bool save = true)`
- `AddConsoleMessage(string, ConsoleMessageType)`
- `MarkDirty()`

### C# ImGui helpers (`ModuCPP.ImGui`)
- `Text`, `Separator`, `Button`, `Checkbox`
- `DragFloat`, `DragFloat3`, `InputText`
- `BeginCombo`, `EndCombo`, `Selectable`
- `AcceptSceneObjectDrop(out int id)`

### C# auto-inspector attributes (`ModuCPP`)
- `[HeadText("...")]` class/field header text
- `[Label("...")]` custom field label
- `[DragSpeed(0.25f)]` drag speed for numeric/vector fields
- `[InspectorIgnore]` skip a field

(There is also `[SettingKey]` declared in `ModuCPP.cs`, but current auto-setting path uses field names.)

### C# example: manual inspector + runtime
```csharp
using System;
using ModuCPP;

public class SpinScript {
    private bool enabled = true;
    private Vec3 speed = new Vec3(0f, 60f, 0f);

    public void TickUpdate(IntPtr ctx, float dt) {
        var c = new Context(ctx);
        if (!enabled) return;
        c.Rotation = c.Rotation + speed * dt;
    }

    public void OnInspector(IntPtr ctx) {
        var c = new Context(ctx);
        c.AutoSetting("enabled", ref enabled);
        c.AutoSetting("speed", ref speed);

        ImGui.Text("Spin Script");
        ImGui.Checkbox("Enabled", ref enabled);
        ImGui.DragFloat3("Speed", ref speed, 0.1f, -720f, 720f);
    }
}
```

### C# example: auto inspector with attributes
```csharp
using System;
using ModuCPP;

[HeadText("Auto Inspector Demo")]
public class AutoInspectorDemo {
    [Label("Auto Rotate")]
    private bool autoRotate = true;

    [DragSpeed(0.5f)]
    private Vec3 spinSpeed = new Vec3(0f, 45f, 0f);

    [InspectorIgnore]
    private float runtimeOnly;

    public void Begin(IntPtr ctx) {
        var c = new Context(ctx);
        c.AutoSettingsFrom(this, save: false);
    }

    public void TickUpdate(IntPtr ctx, float dt) {
        var c = new Context(ctx);
        if (autoRotate) {
            c.Rotation = c.Rotation + spinSpeed * dt;
        }
    }
    // No OnInspector: engine falls back to ModuCPP.Inspector.RenderAuto.
}
```

## scripts.modu
Native script compilation reads `scripts.modu` (legacy `Scripts.modu` still detected).

ModuCPP/native C++ now targets `C++26` by default, with `C++23` as the primary compatibility fallback when a toolchain is not ready for `C++26` yet. Standards below `C++20` are deprecated and may be removed in a later release.
Compiler flags are normalized per toolchain, so `c++26` maps to the closest available draft/latest mode where a compiler does not yet expose a literal `c++26` switch.

Common keys:
- `scriptsDir`
- `outDir`
- `includeDir` (repeatable)
- `define` (repeatable)
- `cppStandard`
- `linux.linkLib` / `win.linkLib` (repeatable)

Example:
```ini
# Default native script target is C++26. Use c++23 if your compiler is not ready for C++26 yet.
# ModuCPP standards below C++20 are deprecated and may be removed in a later version.
scriptsDir=Assets/Scripts
outDir=Library/CompiledScripts
includeDir=../src
includeDir=../include
cppStandard=c++26
linux.linkLib=pthread
linux.linkLib=dl
win.linkLib=User32.lib
win.linkLib=Advapi32.lib
```

## Scripted editor windows
Native:
- `RenderEditorWindow(ScriptContext&)`
- `ExitRenderEditorWindow(ScriptContext&)`

C:
- `Modu_RenderEditorWindow(ModuScriptContext*)`
- `Modu_ExitRenderEditorWindow(ModuScriptContext*)`

Open from menu: **View -> Scripted Windows**.

## IEnum tasks (C++)
Per-script function tasks, started/stopped at runtime:
- `IEnum_Start(fn)`
- `IEnum_Stop(fn)`
- `IEnum_Ensure(fn)`

`fn` signature: `void(ScriptContext&, float)`.

## Repo examples
Reference scripts in this repo:
- `Scripts/AutoEnableAndDisableListsOfObjectsAfterAmountOfTime.moducpp` (timed object enable/disable mechanic)
- `Scripts/DialogueSystem.moducpp` (localized dialogue runtime + inspector DSL example)
- `Scripts/InteractableObject.moducpp` (interaction routing, selection state, dialogue/toggle actions)
- `Scripts/MainMenuController.moducpp` (menu navigation + menu action editing)
- `Scripts/TopDownMovement2D.moducpp` (2D locomotion, sprite clips, footsteps)
- `Scripts/StandaloneMovementController.moducpp` (3D standalone movement helper integration)
- `Scripts/FPSDisplay.moducpp` (UI text + FPS cap setting)
- `Scripts/RigidbodyTest.moducpp` (physics helper usage)
- `Scripts/AnimationWindow.moducpp` (scripted editor animation tool)
- `Scripts/EditorWindowSample.moducpp` (minimal scripted editor window)
- `Scripts/SampleInspector.moducpp` (manual inspector + public-field ModuCPP pattern)
- `Scripts/SampleInspector Simplified.moducpp` (Config/State migration pattern)
- `Scripts/Managed/SampleInspector.cs` (managed hooks + auto inspector)
- `Scripts/Managed/SampleInspectorManaged.cs` (managed bridge sample)

## API reference (auto-generated)
Regenerate this section after changing `ScriptRuntime.h`, `ScriptRuntimeCAPI.h`, or `Scripts/Managed/ModuCPP.cs`:

```bash
python3 tools/generate_scripting_docs.py
```

Use `///` or `// @doc:` immediately above a declaration for generated docs.
For detailed entries, use tags in those comments:
- `@summary`
- `@usage`
- `@howto`
- `@param <name> <text>`
- `@returns`
- `@note`
- `@example` ... `@endexample`

<!-- AUTO-GEN:SCRIPTING_API:START -->

_Generated from source. Add notes with `///` or `// @doc:` above declarations. Supported tags: `@summary`, `@usage`, `@howto`, `@param`, `@returns`, `@note`, `@example`/`@endexample`._

### C++ `ScriptContext` Fields
#### General
- `Engine* engine = nullptr`
- `SceneObject* object = nullptr`
- `ScriptComponent* script = nullptr`
- `std::vector<AutoSettingEntry> autoSettings`

### C++ `ScriptContext` Methods
#### Convenience helpers for scripts
- `SceneObject* FindObjectByName(const std::string& name)`
  Summary: Resolve the first scene object with an exact name match.
  Usage: Useful for cross-object links in gameplay scripts.
  How to use: Call once (for example in Begin) and cache the id/name you need.
  Parameters: `name`: Scene object name to match exactly.
  Returns: Matching object pointer, or nullptr when not found.
- `SceneObject* FindObjectById(int id)`
- `SceneObject* ResolveObjectRef(const std::string& ref)`
- `bool IsObjectEnabled() const`
- `void SetObjectEnabled(bool enabled)`
- `int GetLayer() const`
- `void SetLayer(int layer)`
- `std::string GetTag() const`
- `void SetTag(const std::string& tag)`
- `bool HasTag(const std::string& tag) const`
- `bool IsInLayer(int layer) const`
- `void SetPosition(const glm::vec3& pos)`
- `void SetPosition2D(const glm::vec2& pos)`
- `void SetRotation(const glm::vec3& rot)`
- `void SetScale(const glm::vec3& scl)`
- `void GetPlanarYawPitchVectors(float pitchDeg, float yawDeg, glm::vec3& outForward, glm::vec3& outRight) const`
- `glm::vec3 GetMoveInputWASD(float pitchDeg, float yawDeg) const`
- `bool ApplyMouseLook(float& pitchDeg, float& yawDeg, float sensitivity, float maxDelta, float deltaTime, bool requireMouseButton) const`
- `int GetSelectedObjectId() const`
- `bool IsSprintDown() const`
- `bool IsJumpDown() const`
- `bool IsKeyDown(int glfwKey, ImGuiKey imguiKey = ImGuiKey_None) const`
- `bool IsKeyPressed(int glfwKey, ImGuiKey imguiKey = ImGuiKey_None) const`
- `bool ResolveGround(float capsuleHalf, float probeExtra, float groundSnap, float verticalVelocity, glm::vec3* outHitPos = nullptr, bool* outHitGround = nullptr, glm::vec3* outHitNormal = nullptr, int* outHitActorId = nullptr, glm::vec3* outHitActorVelocity = nullptr, float* outHitStaticFriction = nullptr, float* outHitDynamicFriction = nullptr) const`
- `void ApplyVelocity(const glm::vec3& velocity, float deltaTime)`
- `void BindStandaloneMovementSettings(StandaloneMovementSettings& settings)`
- `void DrawStandaloneMovementInspector(StandaloneMovementSettings& settings, bool* showDebug = nullptr)`
- `void TickStandaloneMovement(StandaloneMovementState& state, StandaloneMovementSettings& settings, float deltaTime, StandaloneMovementDebug* debug = nullptr)`

#### UI helpers
- `bool IsUIButtonPressed() const`
- `bool IsUIInteractable() const`
- `void SetUIInteractable(bool interactable)`
- `float GetUISliderValue() const`
- `void SetUISliderValue(float value)`
- `void SetUISliderRange(float minValue, float maxValue)`
- `void SetUILabel(const std::string& label)`
- `void SetUIColor(const glm::vec4& color)`
- `int GetSpriteClipCount() const`
- `int GetSpriteClipIndex() const`
- `std::string GetSpriteClipName() const`
- `std::string GetSpriteClipNameAt(int index) const`
- `bool SetSpriteClipIndex(int index)`
- `bool SetSpriteClipName(const std::string& name)`
- `float GetSpriteAlpha() const`
- `void SetSpriteAlpha(float alpha)`
- `bool FadeSpriteAlpha(float targetAlpha, float duration, float deltaTime)`
- `bool FadeSpriteToClipIndex(int clipIndex, float fadeOutDuration, float fadeInDuration, float deltaTime)`
- `bool FadeSpriteToClipName(const std::string& clipName, float fadeOutDuration, float fadeInDuration, float deltaTime)`
- `float GetUITextScale() const`
- `void SetUITextScale(float scale)`
- `void SetUISliderStyle(UISliderStyle style)`
- `void SetUIButtonStyle(UIButtonStyle style)`
- `void SetUIStylePreset(const std::string& name)`
- `void RegisterUIStylePreset(const std::string& name, const ImGuiStyle& style, bool replace = false)`
- `void SetFPSCap(bool enabled, float cap = 120.0f)`
- `bool HasRigidbody() const`
- `bool HasRigidbody2D() const`
- `bool EnsureCapsuleCollider(float height, float radius)`
- `bool EnsureRigidbody(bool useGravity = true, bool kinematic = false)`
- `bool SetRigidbody2DVelocity(const glm::vec2& velocity)`
- `bool GetRigidbody2DVelocity(glm::vec2& outVelocity) const`
- `bool SetRigidbodyVelocity(const glm::vec3& velocity)`
- `bool GetRigidbodyVelocity(glm::vec3& outVelocity) const`
- `bool AddRigidbodyVelocity(const glm::vec3& deltaVelocity)`
- `bool SetRigidbodyAngularVelocity(const glm::vec3& velocity)`
- `bool GetRigidbodyAngularVelocity(glm::vec3& outVelocity) const`
- `bool AddRigidbodyForce(const glm::vec3& force)`
- `bool AddRigidbodyImpulse(const glm::vec3& impulse)`
- `bool AddRigidbodyTorque(const glm::vec3& torque)`
- `bool AddRigidbodyAngularImpulse(const glm::vec3& impulse)`
- `bool SetRigidbodyYaw(float yawDegrees)`
- `bool RaycastClosest(const glm::vec3& origin, const glm::vec3& dir, float distance, glm::vec3* hitPos = nullptr, glm::vec3* hitNormal = nullptr, float* hitDistance = nullptr) const`
- `bool RaycastClosestDetailed(const glm::vec3& origin, const glm::vec3& dir, float distance, glm::vec3* hitPos = nullptr, glm::vec3* hitNormal = nullptr, float* hitDistance = nullptr, int* hitObjectId = nullptr, glm::vec3* hitObjectVelocity = nullptr, float* hitStaticFriction = nullptr, float* hitDynamicFriction = nullptr) const`
- `bool SetRigidbodyRotation(const glm::vec3& rotDeg)`
- `bool TeleportRigidbody(const glm::vec3& pos, const glm::vec3& rotDeg)`

#### Audio helpers
- `bool HasAudioSource() const`
- `bool PlayAudio()`
- `bool StopAudio()`
- `bool SetAudioLoop(bool loop)`
- `bool SetAudioVolume(float volume)`
- `bool SetAudioClip(const std::string& path)`
- `bool PlayAudioOneShot(const std::string& clipPath = "", float volumeScale = 1.0f)`

#### Animation helpers
- `bool HasAnimation() const`
- `bool PlayAnimation(bool restart = true)`
- `bool StopAnimation(bool resetTime = true)`
- `bool PauseAnimation(bool pause = true)`
- `bool ReverseAnimation(bool restartIfStopped = true)`
- `bool SetAnimationTime(float timeSeconds)`
- `float GetAnimationTime() const`
- `bool IsAnimationPlaying() const`
- `bool SetAnimationLoop(bool loop)`
- `bool SetAnimationPlaySpeed(float speed)`
- `bool SetAnimationPlayOnAwake(bool playOnAwake)`

#### Settings helpers (auto-mark dirty)
- `std::string GetSetting(const std::string& key, const std::string& fallback = "") const`
- `void SetSetting(const std::string& key, const std::string& value)`
- `bool GetSettingBool(const std::string& key, bool fallback = false) const`
- `void SetSettingBool(const std::string& key, bool value)`
- `float GetSettingFloat(const std::string& key, float fallback = 0.0f) const`
- `void SetSettingFloat(const std::string& key, float value)`
- `glm::vec3 GetSettingVec3(const std::string& key, const glm::vec3& fallback = glm::vec3(0.0f)) const`
- `void SetSettingVec3(const std::string& key, const glm::vec3& value)`

#### Console helper
- `void AddConsoleMessage(const std::string& message, ConsoleMessageType type = ConsoleMessageType::Info)`

#### Auto-binding helpers: bind once per call, optionally load stored value.
- `void AutoSetting(const std::string& key, bool& value)`
- `void AutoSetting(const std::string& key, float& value)`
- `void AutoSetting(const std::string& key, int& value)`
- `void AutoSetting(const std::string& key, glm::vec3& value)`
- `void AutoSetting(const std::string& key, char* buffer, size_t bufferSize)`
- `void AutoSetting(const std::string& key, std::string& value)`
- `void SaveAutoSettings()`

#### IEnum helpers
- `void StartIEnum(void(*fn)(ScriptContext&, float))`
- `void StopIEnum(void(*fn)(ScriptContext&, float))`
- `void EnsureIEnum(void(*fn)(ScriptContext&, float))`
- `bool IsIEnumRunning(void(*fn)(ScriptContext&, float)) const`
- `void StopAllIEnums()`
- `void MarkDirty()`

### C `Modu_*` API
#### C API Functions
- `int Modu_GetObjectId(ModuScriptContext* ctx)`
  Summary: Read the current script object id.
  Usage: Use this to compare against raycast hits or stored ids in settings.
  Returns: Object id, or -1 when the context/object is invalid.
- `int Modu_IsObjectEnabled(ModuScriptContext* ctx)`
- `void Modu_SetObjectEnabled(ModuScriptContext* ctx, int enabled)`
- `ModuVec3 Modu_GetPosition(ModuScriptContext* ctx)`
- `ModuVec3 Modu_GetRotation(ModuScriptContext* ctx)`
- `ModuVec3 Modu_GetScale(ModuScriptContext* ctx)`
- `void Modu_SetPosition(ModuScriptContext* ctx, ModuVec3 value)`
- `void Modu_SetRotation(ModuScriptContext* ctx, ModuVec3 value)`
- `void Modu_SetScale(ModuScriptContext* ctx, ModuVec3 value)`
- `int Modu_SetRigidbodyVelocity(ModuScriptContext* ctx, ModuVec3 velocity)`
- `int Modu_AddRigidbodyForce(ModuScriptContext* ctx, ModuVec3 force)`
- `int Modu_GetRigidbodyVelocity(ModuScriptContext* ctx, ModuVec3* outVelocity)`
- `int Modu_SetRigidbodyRotation(ModuScriptContext* ctx, ModuVec3 rotation)`
- `int Modu_EnsureCapsuleCollider(ModuScriptContext* ctx, float height, float radius)`
- `int Modu_EnsureRigidbody(ModuScriptContext* ctx, int useGravity, int kinematic)`
- `int Modu_HasAnimation(ModuScriptContext* ctx)`
- `int Modu_PlayAnimation(ModuScriptContext* ctx, int restart)`
- `int Modu_StopAnimation(ModuScriptContext* ctx, int resetTime)`
- `int Modu_PauseAnimation(ModuScriptContext* ctx, int pause)`
- `int Modu_ReverseAnimation(ModuScriptContext* ctx, int restartIfStopped)`
- `int Modu_SetAnimationTime(ModuScriptContext* ctx, float timeSeconds)`
- `float Modu_GetAnimationTime(ModuScriptContext* ctx)`
- `int Modu_IsAnimationPlaying(ModuScriptContext* ctx)`
- `int Modu_SetAnimationLoop(ModuScriptContext* ctx, int loop)`
- `int Modu_SetAnimationPlaySpeed(ModuScriptContext* ctx, float speed)`
- `int Modu_SetAnimationPlayOnAwake(ModuScriptContext* ctx, int playOnAwake)`
- `int Modu_IsSprintDown(ModuScriptContext* ctx)`
- `int Modu_IsJumpDown(ModuScriptContext* ctx)`
- `ModuVec3 Modu_GetMoveInputWASD(ModuScriptContext* ctx, float pitchDeg, float yawDeg)`
- `int Modu_ApplyMouseLook(ModuScriptContext* ctx, float* pitchDeg, float* yawDeg, float sensitivity, float maxDelta, float deltaTime, int requireMouseButton)`
- `int Modu_RaycastClosestDetailed(ModuScriptContext* ctx, ModuVec3 origin, ModuVec3 dir, float distance, ModuVec3* hitPos, ModuVec3* hitNormal, float* hitDistance, int* hitObjectId, ModuVec3* hitObjectVelocity, float* hitStaticFriction, float* hitDynamicFriction)`
- `void Modu_AddConsoleMessage(ModuScriptContext* ctx, const char* message, int type)`
- `float Modu_GetSettingFloat(ModuScriptContext* ctx, const char* key, float fallback)`
- `void Modu_SetSettingFloat(ModuScriptContext* ctx, const char* key, float value)`
- `int Modu_GetSettingBool(ModuScriptContext* ctx, const char* key, int fallback)`
- `void Modu_SetSettingBool(ModuScriptContext* ctx, const char* key, int value)`
- `void Modu_SetSettingString(ModuScriptContext* ctx, const char* key, const char* value)`
- `int Modu_GetSettingString(ModuScriptContext* ctx, const char* key, const char* fallback, char* outBuffer, int outBufferSize)`
- `int Modu_GetSpriteClipCount(ModuScriptContext* ctx)`
- `int Modu_GetSpriteClipIndex(ModuScriptContext* ctx)`
- `int Modu_SetSpriteClipIndex(ModuScriptContext* ctx, int index)`
- `int Modu_SetSpriteClipName(ModuScriptContext* ctx, const char* name)`
- `int Modu_GetSpriteClipName(ModuScriptContext* ctx, char* outBuffer, int outBufferSize)`
- `int Modu_GetSpriteClipNameAt(ModuScriptContext* ctx, int index, char* outBuffer, int outBufferSize)`
- `void Modu_InspectorText(ModuScriptContext* ctx, const char* text)`
- `void Modu_InspectorSeparator(ModuScriptContext* ctx)`
- `int Modu_InspectorDragFloat(ModuScriptContext* ctx, const char* label, float* value, float speed, float minValue, float maxValue, const char* format)`
- `int Modu_InspectorDragFloat2(ModuScriptContext* ctx, const char* label, float* value, float speed, float minValue, float maxValue, const char* format)`
- `int Modu_InspectorDragFloat3(ModuScriptContext* ctx, const char* label, float* value, float speed, float minValue, float maxValue, const char* format)`
- `int Modu_InspectorCheckbox(ModuScriptContext* ctx, const char* label, int* value)`
- `int Modu_InspectorObject(ModuScriptContext* ctx, const char* label, int* objectId)`

### C# `ModuCPP` API
#### C# Context Members
- `public Context(IntPtr ctx) { ... }`
- `public int ObjectId => Native.GetObjectId(handle)`
  Summary: Native object id for this script context.
  Usage: Use when saving object references in settings or logs.
  Returns: Object id, or -1 if native context is invalid.
- `public int SelectedObjectId => Native.GetSelectedObjectId(handle)`
- `public int SceneObjectCount => Native.GetSceneObjectCount(handle)`
- `public ModuObject FindObjectByName(string name) { ... }`
- `public ModuObject FindObjectById(int id) { ... }`
- `public ModuObject GetObjectByIndex(int index) { ... }`
- `public string GetObjectName(int id) { ... }`
- `public Vec3 Position { ... }`
- `public Vec3 Rotation { ... }`
- `public Vec3 Scale { ... }`
- `public bool HasRigidbody => Native.HasRigidbody(handle) != 0`
- `public bool EnsureRigidbody(bool useGravity = true, bool kinematic = false) { ... }`
- `public bool EnsureCapsuleCollider(float height, float radius) { ... }`
- `public Vec3 RigidbodyVelocity { ... }`
- `public void AddRigidbodyForce(Vec3 force) { ... }`
- `public void AddRigidbodyImpulse(Vec3 impulse) { ... }`
- `public bool HasAnimation => Native.HasAnimation(handle) != 0`
- `public bool PlayAnimation(bool restart = true) { ... }`
- `public bool StopAnimation(bool resetTime = true) { ... }`
- `public bool PauseAnimation(bool pause = true) { ... }`
- `public bool ReverseAnimation(bool restartIfStopped = true) { ... }`
- `public bool SetAnimationTime(float timeSeconds) { ... }`
- `public float GetAnimationTime() { ... }`
- `public bool IsAnimationPlaying() { ... }`
- `public bool SetAnimationLoop(bool loop) { ... }`
- `public bool SetAnimationPlaySpeed(float speed) { ... }`
- `public bool SetAnimationPlayOnAwake(bool playOnAwake) { ... }`
- `public bool IsObjectEnabled { ... }`
- `public int Layer { ... }`
- `public string Tag { ... }`
- `public bool HasTag(string tag) { ... }`
- `public bool IsInLayer(int layer) { ... }`
- `public void SetPosition2D(Vec2 value) { ... }`
- `public bool IsSprintDown() { ... }`
- `public bool IsJumpDown() { ... }`
- `public Vec3 GetMoveInputWASD(float pitchDeg, float yawDeg) { ... }`
- `public bool ApplyMouseLook(ref float pitchDeg, ref float yawDeg, float sensitivity, float maxDelta, float deltaTime, bool requireMouseButton = false) { ... }`
- `public bool HasRigidbody2D => Native.HasRigidbody2D(handle) != 0`
- `public Vec2 Rigidbody2DVelocity { ... }`
- `public bool AddRigidbodyVelocity(Vec3 deltaVelocity) { ... }`
- `public bool SetRigidbodyAngularVelocity(Vec3 velocity) { ... }`
- `public bool TryGetRigidbodyAngularVelocity(out Vec3 velocity) { ... }`
- `public bool AddRigidbodyTorque(Vec3 torque) { ... }`
- `public bool AddRigidbodyAngularImpulse(Vec3 impulse) { ... }`
- `public bool SetRigidbodyYaw(float yawDegrees) { ... }`
- `public bool SetRigidbodyRotation(Vec3 rotationDegrees) { ... }`
- `public bool TeleportRigidbody(Vec3 position, Vec3 rotationDegrees) { ... }`
- `public bool RaycastClosestDetailed(Vec3 origin, Vec3 direction, float distance, out RaycastHit hit) { ... }`
- `public bool IsUIButtonPressed() { ... }`
- `public bool IsUIInteractable { ... }`
- `public float UISliderValue { ... }`
- `public void SetUISliderRange(float minValue, float maxValue) { ... }`
- `public void SetUILabel(string label) { ... }`
- `public void SetUIColor(float r, float g, float b, float a) { ... }`
- `public float UITextScale { ... }`
- `public void SetUISliderStyle(int style) { ... }`
- `public void SetUIButtonStyle(int style) { ... }`
- `public void SetUIStylePreset(string name) { ... }`
- `public void SetFPSCap(bool enabled, float cap = 120f) { ... }`
- `public int SpriteClipCount => Native.GetSpriteClipCount(handle)`
- `public int SpriteClipIndex => Native.GetSpriteClipIndex(handle)`
- `public string GetSpriteClipName() { ... }`
- `public string GetSpriteClipNameAt(int index) { ... }`
- `public bool SetSpriteClipIndex(int index) { ... }`
- `public bool SetSpriteClipName(string name) { ... }`
- `public float SpriteAlpha { ... }`
- `public bool FadeSpriteAlpha(float targetAlpha, float duration, float deltaTime) { ... }`
- `public bool FadeSpriteToClipIndex(int clipIndex, float fadeOutDuration, float fadeInDuration, float deltaTime) { ... }`
- `public bool FadeSpriteToClipName(string clipName, float fadeOutDuration, float fadeInDuration, float deltaTime) { ... }`
- `public bool HasAudioSource => Native.HasAudioSource(handle) != 0`
- `public bool PlayAudio() { ... }`
- `public bool StopAudio() { ... }`
- `public bool SetAudioLoop(bool loop) { ... }`
- `public bool SetAudioVolume(float volume) { ... }`
- `public bool SetAudioClip(string path) { ... }`
- `public bool PlayAudioOneShot(string clipPath = "", float volumeScale = 1.0f) { ... }`
- `public void MarkDirty() { ... }`
- `public float GetSettingFloat(string key, float fallback = 0f) { ... }`
- `public bool GetSettingBool(string key, bool fallback = false) { ... }`
- `public string GetSettingString(string key, string fallback = "") { ... }`
- `public void SetSettingFloat(string key, float value) { ... }`
- `public void SetSettingBool(string key, bool value) { ... }`
- `public void SetSettingString(string key, string value) { ... }`
- `public void AddConsoleMessage(string message, ConsoleMessageType type = ConsoleMessageType.Info) { ... }`
- `public void AutoSetting(string key, ref bool value, bool save = true) { ... }`
- `public void AutoSetting(string key, ref float value, bool save = true) { ... }`
- `public void AutoSetting(string key, ref Vec3 value, bool save = true) { ... }`
- `public void AutoSetting(string key, ref string value, int bufferSize = 256, bool save = true) { ... }`
- `public void AutoSetting(string key, ref ModuObject value, bool save = true) { ... }`
- `public void AutoSettingsFrom(object instance, bool save = true) { ... }`

#### C# ImGui Members
- `public static void Text(string text) { ... }`
- `public static void Separator() { ... }`
- `public static bool Button(string label) { ... }`
- `public static bool Checkbox(string label, ref bool value) { ... }`
- `public static bool DragFloat(string label, ref float value, float speed = 0.1f, float minValue = 0.0f, float maxValue = 0.0f) { ... }`
- `public static bool DragFloat3(string label, ref Vec3 value, float speed = 0.1f, float minValue = 0.0f, float maxValue = 0.0f) { ... }`
- `public static bool InputText(string label, ref string value, int bufferSize = 256) { ... }`
- `public static bool BeginCombo(string label, string previewValue) { ... }`
- `public static void EndCombo() { ... }`
- `public static bool Selectable(string label, bool selected = false) { ... }`
- `public static bool AcceptSceneObjectDrop(out int id) { ... }`

#### C# Inspector Members
- `public static void RenderAuto(IntPtr ctx, object instance) { ... }`
- `public static void AutoInspect(Context context, object instance) { ... }`

#### C# Attributes
- `class HeadTextAttribute : Attribute`
- `class LabelAttribute : Attribute`
- `class SettingKeyAttribute : Attribute`
- `class DragSpeedAttribute : Attribute`
- `class InspectorIgnoreAttribute : Attribute`

<!-- AUTO-GEN:SCRIPTING_API:END -->

## Troubleshooting
- Script does not run:
  - Object disabled, script disabled, wrong path, or binary missing.
- C hook not found:
  - Ensure hook name is exactly `Modu_*` and returns `void`.
- C# type not found:
  - Use fully-qualified type name (e.g. `ModuCPP.SampleInspector`).
- C# inspector missing:
  - Add `OnInspector(IntPtr)` or rely on auto inspector fallback.
- Managed init failure:
  - Check Mono setup and `MODU_MONO_ROOT`.
- Build failure:
  - Native: verify `scripts.modu` include/link entries.
  - Managed: run `dotnet build Scripts/Managed/ModuCPP.csproj -c Debug` and inspect output.
- Hard crash:
  - Scripts are not sandboxed; add null checks and avoid stale object pointers.
