# Website Script Tab Samples

## Purpose
This file provides four tab-ready sample scripts for the Modularity website.

They all demonstrate the same idea:

- a simple 2D movement system test
- WASD movement
- sprint changes speed
- current script updates a visible status label or otherwise shows the active test

The point is not to make the four tabs look identical. The point is to show the real difference in authoring style between:

1. `ModuCPP`
2. native `C++`
3. managed `C#`
4. the native `C` runtime bridge

## Recommended Tab Labels
- `ModuCPP`
- `Native C++`
- `Managed C#`
- `C Runtime Bridge`

## Shared Framing
All four samples are intentionally the same feature:

- top-down 2D movement
- normalized input
- sprint-aware speed
- minimal readable implementation

That gives the website a clean comparison:

- `ModuCPP` is the shortest and most gameplay-oriented
- native `C++` is lower-level and more explicit
- managed `C#` uses the Mono bridge and `Context`
- the `C` bridge is the most manual and lowest-level

---

## 1. ModuCPP

### What This Shows
- the recommended high-level scripting path
- module imports instead of raw header management
- built-in `input` facade
- built-in `TryMoveRigidbody2D(...)`
- direct current-object UI label update with `obj.UILabel`

### Sample
```cpp
add ModuCPP;
add ModuInput;
add ModuEngine;

public class Movement2DTest : ModuNode
{
    public float walkSpeed = 4.0f;
    public float runSpeed = 7.0f;
    public float acceleration = 18.0f;
    public float drag = 8.0f;

    void TickUpdate()
    {
        Vector2 move = input.WASDNormalized();
        bool running = input.sprint();
        float speed = running ? runSpeed : walkSpeed;

        TryMoveRigidbody2D(ctx, move * speed, acceleration, drag, dt);
        obj.UILabel = "ModuCPP 2D Speed: " + IntR(speed);
    }
}
```

### Why It Feels Different
This is the most compact version because it is written at the gameplay-authoring layer.

---

## 2. Native C++

### What This Shows
- direct `ScriptContext` usage
- explicit normalization and velocity setup
- lower-level control than ModuCPP
- still native and still script-facing, but less ergonomic

### Sample
```cpp
#include "ModuCPP"
#include <cmath>
#include <string>

namespace {
float walkSpeed = 4.0f;
float runSpeed = 7.0f;
}

void TickUpdate(ScriptContext& ctx, float dt)
{
    if (!ctx.object || dt <= 0.0f || !ctx.HasRigidbody2D()) return;

    glm::vec3 move3 = ctx.GetMoveInputWASD(0.0f, 0.0f);
    glm::vec2 move(move3.x, move3.z);

    const float len = glm::length(move);
    if (len > 0.0001f) {
        move /= len;
    }

    const float speed = ctx.IsSprintDown() ? runSpeed : walkSpeed;
    ctx.SetRigidbody2DVelocity(move * speed);
    ctx.SetUILabel(std::string("Native C++ 2D Speed: ") + ModuCPP::IntR(speed));
}
```

### Why It Feels Different
This version is still clean, but you can already see more of the runtime plumbing:

- direct `ScriptContext`
- explicit input vector conversion
- explicit normalization
- explicit rigidbody check

---

## 3. Managed C#

### What This Shows
- Mono-hosted scripting through `Scripts/Managed/ModuCPP.cs`
- `Context` object as the main bridge into the native runtime
- explicit managed-side normalization and velocity assignment

### Sample
```csharp
using System;

namespace ModuCPP;

public class Movement2DManaged
{
    private float walkSpeed = 4.0f;
    private float runSpeed = 7.0f;

    public void Begin(IntPtr ctx, float deltaTime)
    {
        var context = new Context(ctx);
        context.AutoSettingsFrom(this, save: false);
    }

    public void TickUpdate(IntPtr ctx, float deltaTime)
    {
        var context = new Context(ctx);
        context.AutoSettingsFrom(this, save: false);

        if (deltaTime <= 0.0f || !context.HasRigidbody2D) return;

        Vec3 move3 = context.GetMoveInputWASD(0.0f, 0.0f);
        float length = MathF.Sqrt((move3.X * move3.X) + (move3.Z * move3.Z));

        Vec2 move = length > 0.0001f
            ? new Vec2(move3.X / length, move3.Z / length)
            : new Vec2(0.0f, 0.0f);

        float speed = context.IsSprintDown() ? runSpeed : walkSpeed;
        context.Rigidbody2DVelocity = new Vec2(move.X * speed, move.Y * speed);
        context.SetUILabel($"Managed C# 2D Speed: {speed:0}");
    }
}
```

### Why It Feels Different
This is the managed-bridge version:

- the script is authored in C#
- the runtime still flows through the engine’s native API bridge
- `Context` is the entry point instead of high-level ModuCPP facades

---

## 4. C Runtime Bridge

### What This Shows
- the lowest-level script-facing bridge
- raw C functions from `ScriptRuntimeCAPI.h`
- manual normalization
- transform stepping instead of dedicated 2D helper sugar

### Sample
```c
#include "ScriptRuntimeCAPI.h"
#include <math.h>

static float g_walkSpeed = 4.0f;
static float g_runSpeed = 7.0f;

void Modu_Begin(ModuScriptContext* ctx) {
    if (!ctx) return;
    Modu_AddConsoleMessage(ctx, "C bridge 2D movement test ready.", MODU_CONSOLE_INFO);
}

void Modu_TickUpdate(ModuScriptContext* ctx, float dt) {
    if (!ctx || dt <= 0.0f) return;

    ModuVec3 move = Modu_GetMoveInputWASD(ctx, 0.0f, 0.0f);
    float len = sqrtf((move.x * move.x) + (move.z * move.z));
    if (len > 0.0001f) {
        move.x /= len;
        move.z /= len;
    } else {
        move.x = 0.0f;
        move.z = 0.0f;
    }

    float speed = Modu_IsSprintDown(ctx) ? g_runSpeed : g_walkSpeed;

    ModuVec3 pos = Modu_GetPosition(ctx);
    pos.x += move.x * speed * dt;
    pos.z += move.z * speed * dt;
    Modu_SetPosition(ctx, pos);
}
```

### Why It Feels Different
This tab should feel clearly lower-level than the others.

That is a good thing. It teaches the real distinction:

- the `C` bridge is a raw integration surface
- it is capable, but it is not the ergonomic default for gameplay scripts

---

## Suggested Website Comparison Copy

### ModuCPP
High-level gameplay scripting with modules, lifecycle hooks, generated inspector support, and engine helpers like `input` and `TryMoveRigidbody2D(...)`.

### Native C++
Direct `ScriptContext` scripting for lower-level engine-adjacent logic with full native control and more explicit runtime plumbing.

### Managed C#
Mono-hosted scripting through `ModuCPP.cs`, using the managed `Context` bridge to access movement, UI, physics, and settings helpers.

### C Runtime Bridge
Low-level C integration through `ScriptRuntimeCAPI.h`, best for raw bridge-style scripts and explicit runtime control.

## Recommended Section Heading Copy
### Heading
`Choose Your Scripting Surface`

### Subheading
`All four options can drive the same gameplay test. The difference is how much engine plumbing each layer exposes to the script author.`

## Important Website Note
For the website, avoid claiming that these are just interchangeable "languages" with identical ergonomics.

They are different scripting surfaces with different tradeoffs:

- `ModuCPP` is the recommended high-level authoring path
- native `C++` is more explicit and engine-adjacent
- managed `C#` is a bridge-based scripting path
- the `C` bridge is the lowest-level runtime surface

That is the real comparison the site should teach.
