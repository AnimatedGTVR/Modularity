# Modularity C++ Scripting Quickstart

## Project setup
- Scripts live under `Scripts/` (configurable via `Scripts.modu`).
- The engine generates a wrapper per script when compiling. It exports fixed entry points with `extern "C"` linkage:
  - `Script_OnInspector(ScriptContext&)`
  - `Script_Begin(ScriptContext&, float deltaTime)`
  - `Script_Spec(ScriptContext&, float deltaTime)`
  - `Script_TestEditor(ScriptContext&, float deltaTime)`
  - `Script_Update(ScriptContext&, float deltaTime)` (fallback if TickUpdate is absent)
  - `Script_TickUpdate(ScriptContext&, float deltaTime)`
- Build config file: `Scripts.modu` (auto-created per project). Keys:
  - `scriptsDir`, `outDir`, `includeDir=...`, `define=...`, `linux.linkLib`, `win.linkLib`, `cppStandard`.

## Lifecycle hooks
- **Inspector**: `Script_OnInspector(ScriptContext&)` is called when the script is inspected in the UI.
- **Begin**: `Script_Begin` runs once per object instance before ticking.
- **Spec/Test**: `Script_Spec` and `Script_TestEditor` run every frame when the global “Spec Mode” / “Test Mode” toggles are enabled (Scripts menu).
- **Tick**: `Script_TickUpdate` runs every frame for each script; `Script_Update` is a fallback if TickUpdate is missing.
- All tick-style hooks receive `deltaTime` (seconds) and the `ScriptContext`.

## ScriptContext helpers
Available methods:
- `FindObjectByName`, `FindObjectById`
- `SetPosition`, `SetRotation`, `SetScale`
- `HasRigidbody`
- `SetRigidbodyVelocity`, `GetRigidbodyVelocity`
- `SetRigidbodyAngularVelocity`, `GetRigidbodyAngularVelocity`
- `AddRigidbodyForce`, `AddRigidbodyImpulse`
- `AddRigidbodyTorque`, `AddRigidbodyAngularImpulse`
- `SetRigidbodyRotation`, `TeleportRigidbody`
- `MarkDirty` (flags the project as having unsaved changes)
Fields:
- `engine`: pointer to the Engine
- `object`: pointer to the owning `SceneObject`
- `script`: pointer to the owning `ScriptComponent` (gives access to per-script `settings`)

## Persisting per-script settings
- Each `ScriptComponent` has `settings` (key/value strings) serialized with the scene.
- You can read/write them via `ctx.script->settings` or helper functions in your script.
- After mutating settings or object transforms, call `ctx.MarkDirty()` so Ctrl+S captures changes.

## Example pattern (simplified)
```cpp
static bool autoRotate = false;
static glm::vec3 speed = {0, 45, 0};

void Script_OnInspector(ScriptContext& ctx) {
    ImGui::Checkbox("Auto Rotate", &autoRotate);
    ImGui::DragFloat3("Speed", &speed.x, 1.f, -360.f, 360.f);
    ctx.MarkDirty();
}

void Script_Begin(ScriptContext& ctx, float) {
    ctx.MarkDirty(); // ensure initial state is saved
}

void Script_TickUpdate(ScriptContext& ctx, float dt) {
    if (autoRotate && ctx.object) {
        ctx.SetRotation(ctx.object->rotation + speed * dt);
    }
}
```

## Runtime behavior
- Scripts tick for all objects every frame, even if not selected.
- Spec/Test toggles are global (main menu → Scripts).
- Compile scripts via the UI “Compile Script” button or run the build command; wrapper generation is automatic.

## Rigidbody helper usage
- `SetRigidbodyAngularVelocity(vec3)` sets angular velocity in radians/sec for dynamic, non-kinematic bodies.
```cpp
ctx.SetRigidbodyAngularVelocity({0.0f, 3.0f, 0.0f});
```
- `GetRigidbodyAngularVelocity(out vec3)` reads current angular velocity into `out`. Returns false if unavailable.
```cpp
glm::vec3 angVel;
if (ctx.GetRigidbodyAngularVelocity(angVel)) {
    ctx.AddConsoleMessage("AngVel Y: " + std::to_string(angVel.y));
}
```
- `AddRigidbodyForce(vec3)` applies continuous force (mass-aware).
```cpp
ctx.AddRigidbodyForce({0.0f, 0.0f, 25.0f});
```
- `AddRigidbodyImpulse(vec3)` applies an instant impulse (mass-aware).
```cpp
ctx.AddRigidbodyImpulse({0.0f, 6.5f, 0.0f});
```
- `AddRigidbodyTorque(vec3)` applies continuous torque.
```cpp
ctx.AddRigidbodyTorque({0.0f, 15.0f, 0.0f});
```
- `AddRigidbodyAngularImpulse(vec3)` applies an instant angular impulse.
```cpp
ctx.AddRigidbodyAngularImpulse({0.0f, 4.0f, 0.0f});
```
- `SetRigidbodyRotation(vec3 degrees)` teleports the rigidbody rotation.
```cpp
ctx.SetRigidbodyRotation({0.0f, 90.0f, 0.0f});
```
Notes:
- These return false if the object has no enabled rigidbody or is kinematic.
- Use force/torque for continuous input and impulses for bursty actions.
- `SetRigidbodyRotation` is authoritative; use it sparingly during gameplay.

## Manual compile (CLI)
Linux example:
```bash
g++ -std=c++20 -fPIC -O0 -g -I../src -I../include -c SampleInspector.cpp -o ../Cache/ScriptBin/SampleInspector.o
g++ -shared ../Cache/ScriptBin/SampleInspector.o -o ../Cache/ScriptBin/SampleInspector.so -ldl -lpthread
```
Windows example:
```bat
cl /nologo /std:c++20 /EHsc /MD /Zi /Od /I ..\\src /I ..\\include /c SampleInspector.cpp /Fo ..\\Cache\\ScriptBin\\SampleInspector.obj
link /nologo /DLL ..\\Cache\\ScriptBin\\SampleInspector.obj /OUT:..\\Cache\\ScriptBin\\SampleInspector.dll User32.lib Advapi32.lib
```
