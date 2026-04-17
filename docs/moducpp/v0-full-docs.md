# ModuCPP Full Documentation Export

Combined export for website/V0 use. Source files remain unchanged in `docs/moducpp/`.

## Included Sources

- `docs/moducpp/README.md`
- `docs/moducpp/manual/README.md`
- `docs/moducpp/manual/getting-started.md`
- `docs/moducpp/manual/script-structure.md`
- `docs/moducpp/manual/imports-and-modules.md`
- `docs/moducpp/manual/fields-and-inspector.md`
- `docs/moducpp/manual/methods-and-lifecycle.md`
- `docs/moducpp/manual/input-and-engine-scripting.md`
- `docs/moducpp/manual/editor-scripting.md`
- `docs/moducpp/manual/experimental-features.md`
- `docs/moducpp/manual/common-patterns.md`
- `docs/moducpp/manual/transpilation.md`
- `docs/moducpp/manual/writing-clean-scripts.md`
- `docs/moducpp/manual/troubleshooting.md`
- `docs/moducpp/api/README.md`
- `docs/moducpp/api/type-modunode-and-modubehaviour.md`
- `docs/moducpp/api/type-sceneobj.md`
- `docs/moducpp/api/type-scriptcontext.md`
- `docs/moducpp/api/type-facades-and-helpers.md`
- `docs/moducpp/api/managed-bridge.md`
- `docs/moducpp/api/inspector-reference.md`
- `docs/moducpp/api/hook-reference.md`
- `docs/moducpp/api/module-moducpp.md`
- `docs/moducpp/api/module-moduengine.md`
- `docs/moducpp/api/module-moduinput.md`
- `docs/moducpp/api/module-rmeshbuilder.md`
- `docs/moducpp/api/module-moducpp-experimental.md`
- `docs/moducpp/api/type-standalone-movement.md`
- `docs/moducpp/api/namespace-dialogueport.md`
- `docs/moducpp/api/type-shipped-example-types.md`


---

<!-- Source: docs/moducpp/README.md -->

## ModuCPP Documentation

### Overview
ModuCPP is the scripting layer used by Modularity for gameplay, tool, and inspector-facing logic. You write scripts in a C#-like syntax, but the scripting system does not run on a separate managed gameplay VM. Instead, the ModuCPP transpiler lowers supported script syntax into generated `C++`, and the result is compiled into the same native runtime as the rest of the engine.

That design shapes the way the language feels:

- scripts read like high-level engine code rather than raw `C++`
- the runtime stays native and engine-facing
- the supported syntax is intentional rather than open-ended
- imports are explicit, so each module exposes a clear area of responsibility

If you are approaching ModuCPP from Unity, Unreal Blueprints, or plain `C++`, it helps to think of it as a focused gameplay language layered on top of the existing engine. It is not trying to be a general-purpose programming environment. It exists to make game and editor scripts easier to author, inspect, and ship inside this codebase.

### Why ModuCPP Exists
Modularity already has a native runtime, native scene objects, native editor systems, and native gameplay helpers. Writing every small gameplay behavior directly in low-level engine code is possible, but it pushes everyday scripting work into a slower and more repetitive workflow.

ModuCPP exists to solve that gap.

Instead of forcing every gameplay script to manage native boilerplate, the language gives you:

- a small set of recognized base classes such as `ModuNode`
- declarative public fields that map cleanly into inspector and serialization workflows
- well-known lifecycle hooks such as `Begin()` and `Update()`
- module imports that expose only the helper surface you actually need
- a scripting style that is easier to teach, review, and iterate on than handwritten engine glue

That is why the documentation is split into a manual and an API reference. Most scripting problems start with a workflow question such as “Where should this logic live?” or “Which module am I supposed to import?” Once that part is clear, the API pages help you look up exact members and behavior.

### When To Use This Documentation
Use this documentation set in two passes.

Read the manual first when you are still learning how ModuCPP scripts are organized, when to use public fields, how modules are separated, and how runtime hooks behave over time. The manual pages are written as teaching material. They explain what a feature is for before they explain syntax.

Use the API reference when you already know what area of the scripting surface you need and you want details about exact types, helper facades, lifecycle hooks, or module-specific members.

That workflow mirrors professional engine documentation for a reason. A direct type reference is only useful once the higher-level model is already clear.

### How ModuCPP Works
At a high level, the scripting workflow looks like this:

1. You write a `.moducpp` script and import the modules it needs.
2. The transpiler recognizes supported declarations, hooks, fields, attributes, and helper access.
3. The script is lowered into generated `C++` code that matches the engine's native runtime model.
4. The generated code is compiled and used by the runtime/editor like any other native script logic.

This means there are two important constraints to keep in mind.

First, every documented feature must correspond to something the transpiler and runtime actually support. The docs in this section intentionally avoid inventing “future” language features that are not implemented.

Second, script behavior is grounded in the engine's existing systems. A public field is not magical metadata by itself. It matters because the runtime, inspector generation, and serialization pipeline know how to treat it.

### Modules At A Glance
The module system is one of the most important changes in the current ModuCPP surface.

Older documentation often read as if `add ModuCPP;` brought the whole scripting world into scope. That is no longer the right mental model. `ModuCPP` is the core module. Other engine areas are imported separately so scripts can stay explicit about what they rely on.

| Import | What it is for | Typical use |
| --- | --- | --- |
| `add ModuCPP;` | Core script base names, object/state facades, timers, math/value helpers, script context | Nearly every gameplay script |
| `add ModuEngine;` | Engine-facing helpers such as object utilities, editor widgets, movement/audio helpers, project/runtime helpers | Gameplay scripts that touch more engine systems |
| `add ModuInput;` | Input constants, direct key polling, directional input helpers | Character control, UI navigation, debug controls |
| `add RMeshBuilder;` | Reserved mesh-builder import | Future-facing or placeholder usage only |
| `add ModuCPP.Experimental;` | Experimental helpers such as object-reference serialization and extra UI/editor helpers | Tools, prototypes, unstable feature work |

The dedicated module pages go deeper on this split. The main rule is simple: import the smallest stable surface that solves the problem you have right now.

### Suggested Reading Path
If you are new to the language, read in this order:

1. [Getting Started](manual/getting-started.md)
2. [Script Structure](manual/script-structure.md)
3. [Imports and Modules](manual/imports-and-modules.md)
4. [Fields and Inspector](manual/fields-and-inspector.md)
5. [Methods and Lifecycle](manual/methods-and-lifecycle.md)
6. [Input and Engine Scripting](manual/input-and-engine-scripting.md)
7. [Common Patterns](manual/common-patterns.md)

After that, move into the API pages for the modules and types you use most often.

### Documentation Structure
#### Manual
The manual pages teach how to use ModuCPP in practice.

- [Manual Index](manual/README.md)
- [Getting Started](manual/getting-started.md)
- [Script Structure](manual/script-structure.md)
- [Imports and Modules](manual/imports-and-modules.md)
- [Fields and Inspector](manual/fields-and-inspector.md)
- [Methods and Lifecycle](manual/methods-and-lifecycle.md)
- [Input and Engine Scripting](manual/input-and-engine-scripting.md)
- [Editor Scripting](manual/editor-scripting.md)
- [Experimental Features](manual/experimental-features.md)
- [Common Patterns](manual/common-patterns.md)
- [Transpilation Overview](manual/transpilation.md)
- [Writing Clean Scripts](manual/writing-clean-scripts.md)
- [Troubleshooting](manual/troubleshooting.md)

#### API Reference
The API pages describe the script-facing types, modules, hooks, and helper surfaces in detail.

- [API Reference Index](api/README.md)
- [ModuNode and ModuBehaviour](api/type-modunode-and-modubehaviour.md)
- [SceneObj](api/type-sceneobj.md)
- [ScriptContext](api/type-scriptcontext.md)
- [Facades and Helper Types](api/type-facades-and-helpers.md)
- [Managed Bridge (`ModuCPP.cs`)](api/managed-bridge.md)
- [Inspector Reference](api/inspector-reference.md)
- [Hook Reference](api/hook-reference.md)
- [Module: ModuCPP](api/module-moducpp.md)
- [Module: ModuEngine](api/module-moduengine.md)
- [Module: ModuInput](api/module-moduinput.md)
- [Module: RMeshBuilder](api/module-rmeshbuilder.md)
- [Module: ModuCPP.Experimental](api/module-moducpp-experimental.md)
- [Standalone Movement API](api/type-standalone-movement.md)
- [DialoguePort Namespace](api/namespace-dialogueport.md)
- [Shipped Example Types](api/type-shipped-example-types.md)

### Scope
This documentation set covers the current script-facing language and helper surface that can be traced in the repository:

- the modular import system
- supported script class shapes and script markers
- public/private field behavior and inspector-facing attributes
- lifecycle and editor hooks recognized by the runtime/transpiler
- script helper facades exposed by the native headers in `include/`
- example-script data patterns that are important enough to document as real usage

It does not treat unfinished ideas as shipped language features. If a surface exists but is partial, unstable, or clearly intended for experimentation, the docs call that out explicitly.

### Notes
- Prefer the manual pages when learning a new area. They explain intent, tradeoffs, and typical workflows.
- Prefer the API pages when you already know the area and need exact names, members, or module requirements.
- Treat `ModuCPP.Experimental` as opt-in and unstable unless a page says otherwise.
- Treat `RMeshBuilder` as reserved until it exposes real script-facing helpers in the repository.

### Related Pages
- [Scripting Overview](../Scripting.md)
- [Modularity Engine Documentation](../Modularity.md)
- [Manual Index](manual/README.md)
- [API Reference Index](api/README.md)


---

<!-- Source: docs/moducpp/manual/README.md -->

## ModuCPP Manual

### Overview
The manual explains how to think in ModuCPP before you drop into item-by-item reference pages. It is written for the common scripting questions that come up during real work:

- how should a script be structured?
- which module should be imported for this feature?
- when should logic go in `Begin()` versus `Update()`?
- how should data be exposed to the inspector without turning the script into a pile of public state?
- what makes a script stable enough for gameplay versus editor-only experimentation?

If you are learning the language, the manual is the right starting point. The API reference assumes you already know the problem you are solving. The manual is where the “why” and “when” are explained.

### Why This Section Exists
Most scripting issues are not caused by missing type names. They come from unclear mental models.

A developer might know that `SceneObj` exists, for example, but still not know whether to cache state in fields, look up values every frame, expose references through the inspector, or place setup logic in `Begin()`. Those are workflow questions, not raw reference questions.

The manual exists to answer those workflow questions directly. Each page focuses on one practical area and explains the problem it solves, when you should use it, how it behaves at runtime, and what patterns tend to stay maintainable as scripts grow.

### When To Use The Manual
Use the manual when:

- you are new to ModuCPP
- you are porting an older script to the modular import system
- you know the feature you want, but not the recommended workflow
- you are writing larger gameplay scripts and want examples that look like real production code
- you are debugging behavior and need to understand order of execution or editor/runtime boundaries

Once the concept is clear, move into the API pages for exact members and syntax details.

### Suggested Reading Order
#### 1. Start with the basic scripting model
- [Getting Started](getting-started.md)
- [Script Structure](script-structure.md)

These pages explain what a ModuCPP script is, how imports and classes fit together, and how to read the language without mixing it up with plain `C++`.

#### 2. Learn the stable runtime workflow
- [Imports and Modules](imports-and-modules.md)
- [Fields and Inspector](fields-and-inspector.md)
- [Methods and Lifecycle](methods-and-lifecycle.md)

These pages cover the features you use in almost every gameplay script.

#### 3. Learn the engine-facing helpers
- [Input and Engine Scripting](input-and-engine-scripting.md)
- [Editor Scripting](editor-scripting.md)
- [Common Patterns](common-patterns.md)

These pages show how scripts interact with gameplay objects, input, inspector data, and editor tooling in more complete workflows.

#### 4. Read the advanced and maintenance-oriented pages
- [Experimental Features](experimental-features.md)
- [Transpilation Overview](transpilation.md)
- [Writing Clean Scripts](writing-clean-scripts.md)
- [Troubleshooting](troubleshooting.md)

These pages help once you are already building larger systems or debugging the edges of the scripting stack.

### Manual Pages
#### Core usage
- [Getting Started](getting-started.md): first script, first import, and the current modular model
- [Script Structure](script-structure.md): how a script is laid out and how the transpiler reads it
- [Imports and Modules](imports-and-modules.md): what each module is for and how to choose the right one
- [Fields and Inspector](fields-and-inspector.md): public data, serialized values, and inspector-facing declarations
- [Methods and Lifecycle](methods-and-lifecycle.md): runtime hooks, editor hooks, and update behavior over time

#### Engine and tool usage
- [Input and Engine Scripting](input-and-engine-scripting.md): input polling, movement helpers, runtime object control
- [Editor Scripting](editor-scripting.md): editor-only hooks, widgets, tooling workflows, and safety boundaries
- [Experimental Features](experimental-features.md): what is available but should not be treated as stable core surface

#### Practical workflows
- [Common Patterns](common-patterns.md): practical script organization and gameplay patterns
- [Transpilation Overview](transpilation.md): high-level explanation of how scripts map to generated `C++`
- [Writing Clean Scripts](writing-clean-scripts.md): maintainability guidance for larger codebases
- [Troubleshooting](troubleshooting.md): common failure modes, mismatched imports, hook issues, and debugging guidance

### How To Read These Pages
Read each page in order the first time. The pages are written to build context gradually. Later, when you already know the basics, they also work as targeted refreshers.

Each page follows the same broad pattern:

- what the feature is
- why it exists
- when to use it
- how it behaves
- realistic examples
- caveats and best practices

That structure is deliberate. It keeps the documentation useful during both onboarding and day-to-day scripting work.

### Related Pages
- [ModuCPP Overview](../README.md)
- [API Reference Index](../api/README.md)


---

<!-- Source: docs/moducpp/manual/getting-started.md -->

## Getting Started with ModuCPP

### Overview
ModuCPP is the recommended scripting layer for new Modularity gameplay scripts. You write a `.moducpp` file with a small set of high-level rules, the build pipeline transpiles that file to native `C++`, and the result runs through the same native script runtime as hand-written `C++` scripts.

That design matters. ModuCPP is not a separate virtual machine, and it is not a simplified toy language sitting next to the engine. It exists so you can write common gameplay and editor logic with less boilerplate while still keeping the native runtime model, native data structures, and the existing script loading pipeline.

If you already understand the engine but do not want to write raw `ScriptContext` code for every script, ModuCPP is the layer you should start with.

### Why This Feature Exists
Raw native scripting gives you full control, but simple scripts quickly become repetitive:

- you need to wire hook signatures yourself
- you need to bind inspector state manually
- you need to decide what should persist and what should stay runtime-only
- common tasks such as timers, FPS labels, object lists, and simple input polling need helper code every time

ModuCPP exists to solve those problems without changing the runtime model. The transpiler takes care of the repetitive structure so the script can focus on behavior.

### When to Use It
Use ModuCPP when you are writing:

- gameplay behaviors attached to scene objects
- UI behaviors such as labels, menus, and interaction prompts
- small to medium systems that benefit from inspector-driven configuration
- editor-facing tools that still fit into the normal script workflow

Use raw native `C++` instead when:

- you are extending the engine-side runtime itself
- you need a scripting feature the high-level syntax does not expose cleanly
- you are porting a native script that already uses low-level runtime APIs directly

### How It Works
The basic workflow is straightforward:

1. Create a script file under your project script folder, usually `Assets/Scripts/`.
2. Import the modules you need with `add ...;`.
3. Declare a high-level script class such as `public class MyScript : ModuNode`.
4. Add public fields for data you want exposed in the inspector and saved with the script.
5. Add private fields for runtime-only state.
6. Implement lifecycle hooks such as `Begin()` and `TickUpdate()`.
7. Compile the script through the editor or build flow.

At runtime, the script still receives the native `ScriptContext`, but high-level hook bodies already have `ctx`, `obj`, and `dt` prepared for you.

### A Small First Script
The simplest useful script is usually one that reacts every frame and updates something visible.

```cpp
add ModuCPP;
add ModuEngine;

public class FPSDisplay : ModuNode
{
    void TickUpdate()
    {
        obj.UILabel = "FPS: " + IntR(ModuEngine.FPS);
    }
}
```

This example is small, but it demonstrates the core idea:

- `add ModuCPP;` gives you the core language helpers
- `add ModuEngine;` gives you the FPS facade
- `obj.UILabel` writes to the current object's UI label through the script-facing object facade
- `IntR(...)` turns the frame FPS into a readable whole number

The script does not need to set up a custom hook signature, calculate FPS manually, or expose any inspector logic because the task is simple.

### A Better Starter Script
Most real scripts want at least a small amount of configuration. Public fields are where ModuCPP becomes more practical than a one-off native script.

```cpp
add ModuCPP;
add ModuEngine;

public class FPSDisplay : ModuNode
{
    public bool clampTo120 = false;
    public string prefix = "FPS: ";

    void Begin()
    {
        if (clampTo120) {
            ctx.SetFPSCap(true, 120.0f);
        }
    }

    void TickUpdate()
    {
        obj.UILabel = prefix + IntR(ModuEngine.FPS);
    }
}
```

This version solves a more realistic problem:

- designers can change the label prefix in the inspector
- the script can optionally clamp the runtime to 120 FPS
- setup happens once in `Begin()`
- display logic happens every frame in `TickUpdate()`

In practice, this is the shape many real ModuCPP scripts follow: a few public fields for configuration, a small amount of setup, and a clear per-frame behavior.

### Another Common First Script: Repeating Behavior
Many first gameplay scripts need to do something over time. ModuCPP’s timer shorthand exists for exactly that case.

```cpp
add ModuCPP;

public class PulseLogger : ModuNode
{
    public float interval = 0.5f;
    private float timer = 0.0f;

    void Begin() to timer.Start(interval);

    void TickUpdate()
    {
        if (!timer.Ready()) return;
        ctx.AddConsoleMessage("Pulse");
    }
}
```

This is the kind of script that would be more awkward in a raw one-frame-only mental model. `timer.Start(...)` stores the interval, and `timer.Ready()` advances using the current frame delta time until one interval has elapsed.

### What Actually Happens
Behind the scenes, the transpiler turns high-level constructs into native glue:

- the class becomes generated native support code
- public fields are placed into persisted config storage
- private fields are placed into runtime state storage
- hooks such as `Begin()` and `TickUpdate()` become native exports used by the script runtime

The important consequence is that ModuCPP should be thought of as authoring syntax, not as a separate runtime environment.

### Practical Use Cases
Typical first-week ModuCPP scripts include:

- an FPS or status label
- a UI button or slider driver
- a pickup or interactable prompt
- a timed enable/disable script
- a simple mover that reads `input.WASDNormalized()`

Those are good early projects because they teach the recurring patterns:

- explicit module imports
- public configuration vs private state
- setup in `Begin()`
- ongoing behavior in `TickUpdate()`

### Best Practices
- Start with `ModuNode` unless you are deliberately maintaining an older `ModuBehaviour` script.
- Import only the modules the script actually uses.
- Put persistent configuration in public fields and transient behavior in private fields.
- Prefer one clear behavior per script when you are learning the workflow.
- Use the shipped scripts in `Scripts/` as reference once the syntax on this page feels familiar.

### Related Pages
- [Script Structure](script-structure.md)
- [Imports and Modules](imports-and-modules.md)
- [Methods and Lifecycle](methods-and-lifecycle.md)
- [Fields and Inspector](fields-and-inspector.md)


---

<!-- Source: docs/moducpp/manual/script-structure.md -->

## Script Structure

### Overview
A ModuCPP script file is usually small, but it still has a clear structure. That structure is not just style. It reflects what the transpiler expects and how the engine separates script configuration, runtime state, helper code, and lifecycle hooks.

If you understand the shape of a typical file, the rest of the language becomes much easier to read.

### Why This Feature Exists
Without a consistent structure, high-level scripts become hard to reason about:

- it becomes unclear which parts are authoring syntax and which are native passthrough code
- inspector-facing data gets mixed with temporary runtime variables
- reusable helper code ends up duplicated inside unrelated methods
- it becomes difficult for the transpiler to generate clean support code

ModuCPP keeps the file format intentionally simple so common scripts stay readable and the generated native code stays predictable.

### When to Use This
Use the structure on this page whenever you are:

- starting a new script
- converting an older native-style script into high-level ModuCPP
- reviewing a script to see whether it is mixing too many responsibilities
- documenting or teaching ModuCPP to someone else

### How It Works
Most high-level scripts follow this shape:

1. module imports
2. optional `#include` lines for shared helpers
3. optional `public enum` and `SubScript` declarations
4. the `public class`
5. optional helper code outside the class, usually in `namespace { ... }`

Here is the most compact version of that structure:

```cpp
add ModuCPP;
add ModuInput;
add ModuEngine;

public enum FacingDirection { Down, Up, Right, Left }

SubScript FootstepBank {
    public string[] sounds;
};

public class TopDownMovement2D : ModuNode
{
    public float walkSpeed = 4.0f;
    private FacingDirection facing = FacingDirection.Down;

    void TickUpdate()
    {
        Vector2 move = input.WASDNormalized();
    }
}
```

That is enough for the transpiler to understand:

- which modules to import
- which types belong to the high-level script surface
- which fields should persist
- which fields should remain runtime-only
- which methods should become hooks

### The Core Pieces

#### Imports
Imports always come first because they define the script-facing API surface available to the rest of the file.

```cpp
add ModuCPP;
add ModuInput;
```

This keeps the file explicit. A reader can tell immediately whether the script depends on input helpers, engine helpers, or experimental helpers.

#### `public enum`
Use `public enum` when the script needs a small named set of choices that should remain readable in fields and logic.

```cpp
public enum MenuOrientation { Vertical, Horizontal }
```

Enums are especially useful when a script has a configuration choice that would otherwise become a magic integer.

#### `SubScript`
Use `SubScript` for nested data that should still be editable and serializable through the ModuCPP tooling.

```cpp
SubScript MenuAction {
    public SceneObj[] enable;
    public SceneObj[] disable;
};
```

This is the right tool when a script needs repeated structured entries, such as dialogue lines, menu actions, or interaction options.

#### The High-Level Class
The class is the actual script entry point. The current transpiler expects one of these forms:

```cpp
public class MyScript : ModuNode
```

or:

```cpp
public class MyScript : ModuBehaviour
```

For new documentation and new scripts, prefer `ModuNode`.

#### Helper Code Outside the Class
Sometimes a script needs utility functions or private support structs that do not belong as instance methods. In those cases, a small anonymous namespace is the cleanest approach.

You can see this in several shipped scripts, especially when the script needs:

- runtime caches keyed by object id
- helper drawing functions
- conversion helpers
- custom support structs that are not public script fields

### A Simple Script Layout
This is a good structure for a script with no nested data and no custom inspector:

```cpp
add ModuCPP;
add ModuEngine;

public class AutoEnable : ModuNode
{
    public bool startEnabled = true;

    void Begin()
    {
        ctx.SetObjectEnabled(startEnabled);
    }
}
```

The file is short because the behavior is short. That is a good sign. ModuCPP should let simple scripts stay simple.

### A More Realistic Layout
This example shows a more representative file shape with modules, an enum, a `SubScript`, an inspector block, and a runtime helper outside the class.

```cpp
add ModuCPP;
add ModuInput;
add ModuCPP.Experimental;

public enum InteractableType { Dialogue, ToggleObjects }

SubScript InteractionOption
{
    public string optionName = "New Option";
    public int interactionType = 0;
    public string dialogueSystemRef;
    public string[] itemsToEnable;
    public string[] itemsToDisable;
}

namespace {

bool canRunInteraction(ScriptContext& ctx)
{
    return ctx.object != nullptr;
}

}

public class InteractableObject : ModuNode
{
    public bool canInteract = true;
    public InteractionOption[] options;
    private bool prevInteractDown = false;

    inspector
    {
        Tabs {
            Tab("Basics") {
                AutoFields(canInteract);
            }
            Tab("Options") {
                AutoFields(options);
            }
        }
    }

    void TickUpdate()
    {
        if (!canRunInteraction(ctx)) return;
    }
}
```

This is the kind of file structure you should expect in production-oriented scripts: the top of the file explains the API surface, the class defines instance behavior, and helper code lives nearby but outside the class when it is not instance state.

### Special Syntax You Will See

#### One-Line `to` Methods
Use `to` when the body is genuinely a single expression or call and the short form improves readability.

```cpp
void Begin() to timer.Start(interval);
int FacingIndex(FacingDirection direction) to (int)direction;
```

This works well for tiny methods. Once a method needs meaningful branching or explanation, use a normal block body instead.

#### `ref` Parameters
Some shipped scripts use `ref` parameters for helper methods that should mutate values passed from the caller.

```cpp
void TryMove(Vector2 targetVelocity, float dt, ref Vector2 actualVelocity)
```

This is useful when a helper should return more than one piece of information without inventing a separate support type.

#### `each ... state(...)`
The `each someList.state(true);` shorthand exists for a very specific kind of repetitive script logic: applying enabled-state changes to object-reference lists.

```cpp
each enable.state(true);
each disable.state(false);
```

This is appropriate when the script already stores a list of scene object references and the behavior really is “turn everything in this list on” or “turn everything in this list off”.

### What Actually Happens
The file is not executed as-is. The transpiler reads the structure and turns it into native support code. That is why the layout matters so much:

- public data becomes generated config storage
- private data becomes generated state storage
- high-level hook methods become native exports
- inspector syntax becomes generated editor code

The clearer the file structure is, the more predictable that lowering step becomes.

### Best Practices
- Keep imports at the top and keep them explicit.
- Declare small enums when a field would otherwise become a magic integer.
- Use `SubScript` for structured repeated data, not for everything.
- Keep helper code outside the class only when it genuinely improves readability or reuse.
- Do not push every behavior into anonymous-namespace helpers just because the language allows it.
- Prefer a normal method body over `to` when the behavior deserves explanation.

### Related Pages
- [Getting Started](getting-started.md)
- [Fields and Inspector](fields-and-inspector.md)
- [Methods and Lifecycle](methods-and-lifecycle.md)
- [Transpilation Overview](transpilation.md)


---

<!-- Source: docs/moducpp/manual/imports-and-modules.md -->

## Imports and Modules

### Overview
ModuCPP uses explicit imports. A script only gets the module surfaces it asks for.

```cpp
add ModuCPP;
add ModuEngine;
add ModuInput;
add ModuCPP.Experimental;
```

That explicit split is one of the most important things to understand in the current documentation set. Older wording sometimes treated `add ModuCPP;` as if it brought in every helper the scripting layer had ever grown. That is no longer a good mental model.

### Why This Feature Exists
As the scripting surface grew, a single catch-all import stopped being useful. Different kinds of scripts need different things:

- a UI label script usually needs the core layer and maybe one engine helper
- a movement script often needs input and engine helpers
- a dialogue or interaction system may need the experimental object-reference and UI text helpers
- some future scripts may want mesh-building tools without also pulling unrelated high-level helpers into the docs

The module split exists so the docs and the scripts can stay honest about what each script depends on.

### When to Use It
Think about imports before you write the rest of the file.

Use:

- `ModuCPP` for almost every high-level script
- `ModuEngine` when the script touches FPS, sprite/audio facades, movement helpers, or engine-oriented inspector helpers
- `ModuInput` when the script polls keys or uses the `input` facade
- `ModuCPP.Experimental` when the script needs advanced object-reference, UI text, or dialogue/menu helper behavior
- `RMeshBuilder` only when you are intentionally targeting that future module surface

### How It Works
Each `add` line maps to a script API header in the transpiler:

- `add ModuCPP;` -> `ModuCPPScriptApi.h`
- `add ModuEngine;` -> `ModuEngineScriptApi.h`
- `add ModuInput;` -> `ModuInputScriptApi.h`
- `add RMeshBuilder;` -> `RMeshBuilderScriptApi.h`
- `add ModuCPP.Experimental;` -> `ModuCPPExperimentalScriptApi.h`

The practical effect is that imports are not just documentation markers. They control which helper surfaces the generated native code includes.

### The Modules in Practice

#### `ModuCPP`
`ModuCPP` is the core module. It gives you the high-level authoring foundation:

- `Vector2`, `Vector3`, and `string`
- `ctx`, `obj`, and `time`
- `Config<T>()` and `State<T>()`
- timer helpers
- rounding helpers such as `IntR(...)`
- math helpers
- `SubScript` serialization helpers

Use it when the script is primarily about:

- authoring structure
- persistent config and runtime state
- common script-facing convenience

Example:

```cpp
add ModuCPP;

public class Pulse : ModuNode
{
    public float interval = 0.5f;
    private float timer = 0.0f;

    void Begin() to timer.Start(interval);

    void TickUpdate()
    {
        if (!timer.Ready()) return;
        ctx.AddConsoleMessage("Pulse");
    }
}
```

#### `ModuEngine`
`ModuEngine` is where engine-oriented helpers live. This is the module you reach for when the script needs to talk to familiar gameplay helpers instead of writing everything through raw `ScriptContext`.

It includes:

- `ModuEngine.FPS`
- audio facade helpers
- sprite clip helpers
- 2D movement helpers
- inspector editing helpers such as `EditFloat(...)`
- project gravity helpers

Use it when the script is about:

- movement
- UI feedback
- audio feedback
- sprite animation control
- simple engine-facing configuration UIs

Example:

```cpp
add ModuCPP;
add ModuEngine;

public class FPSDisplay : ModuNode
{
    void TickUpdate()
    {
        obj.UILabel = "FPS: " + IntR(ModuEngine.FPS);
    }
}
```

#### `ModuInput`
`ModuInput` exists so common key polling stays short and readable. Without it, many gameplay scripts would keep rewriting the same input translation code.

It includes:

- key constants such as `KEY_W` and `KEY_SPACE`
- `KeyDown(...)` and `KeyPressed(...)`
- the `input` facade for movement-style input

Use it when the script needs:

- WASD movement
- simple action keys
- submit/confirm handling
- sprint or jump state polling

Example:

```cpp
add ModuCPP;
add ModuInput;

public class JumpPrompt : ModuNode
{
    void TickUpdate()
    {
        if (input.jump()) {
            ctx.AddConsoleMessage("Jump pressed");
        }
    }
}
```

#### `ModuCPP.Experimental`
This module is opt-in on purpose. It contains useful helpers, but they are not the minimal stable core the rest of the docs should assume by default.

It includes:

- object reference serialization helpers
- string parsing helpers
- advanced UI text helpers
- object-ref inspector widgets
- helpers used by the shipped dialogue, menu, and interaction examples

Use it when the script needs:

- `[ObjectRef]` or `[ObjectList]` heavy workflows
- dialogue text targeting
- object lists stored and resolved by string references
- advanced shipped-sample style editor widgets

Example:

```cpp
add ModuCPP;
add ModuCPP.Experimental;

public class ToggleMessage : ModuNode
{
    [ObjectRef] public string labelRef;
    public bool active = false;

    void TickUpdate()
    {
        SetUITextLabel(ctx, labelRef, active ? "Active" : "Idle");
    }
}
```

#### `RMeshBuilder`
`RMeshBuilder` is a real import target, but the current script-facing header does not expose an actual helper surface yet.

That means the correct documentation stance is:

- acknowledge that the import exists
- explain that it is reserved
- do not invent mesh-builder scripting APIs that are not present in the repository

### Choosing Imports by Use Case

#### A timer or state-only script
Use `ModuCPP` only.

#### A movement script
Use `ModuCPP`, `ModuInput`, and usually `ModuEngine`.

#### A menu or dialogue script
Use `ModuCPP`, then add `ModuInput`, `ModuEngine`, or `ModuCPP.Experimental` depending on what the script actually does.

#### An editor tool window
Use `ModuCPP`, and add `ModuEngine` if the window needs helper widgets or engine-facing helpers.

### What Actually Happens
When the transpiler sees `add ModuInput;`, it does not merely annotate the file. It emits the corresponding include so the generated native script can use that API surface.

That is why you should think of imports as both:

- documentation for the reader
- a build-time contract for the transpiler

### Best Practices
- Import the smallest set of modules that makes the script readable.
- Do not rely on older assumptions that `add ModuCPP;` implies input, engine, or experimental helpers.
- Treat `ModuCPP.Experimental` as explicit dependency information, not as background noise.
- Keep `RMeshBuilder` documentation conservative until the script API header grows a real surface.

### Related Pages
- [Getting Started](getting-started.md)
- [Input and Engine Scripting](input-and-engine-scripting.md)
- [Module: ModuCPP](../api/module-moducpp.md)
- [Module: ModuEngine](../api/module-moduengine.md)
- [Module: ModuInput](../api/module-moduinput.md)
- [Module: ModuCPP.Experimental](../api/module-moducpp-experimental.md)


---

<!-- Source: docs/moducpp/manual/fields-and-inspector.md -->

## Fields and Inspector

### Overview
In ModuCPP, field design is part of script design.

The repository scripts use two main authoring styles:

1. public fields plus automatic or declarative inspector generation
2. `Config<T>()` and `State<T>()` plus a manual inspector or editor window

If you understand when to use each style, the rest of the inspector system becomes much easier to reason about.

### Why This Matters
Most scripts contain two different categories of data:

- authored configuration that a designer or tool user should edit
- runtime state that only exists while the script is executing

Good ModuCPP scripts keep those categories separate.

That separation is visible all through the repository:

- `DialogueSystem.moducpp` keeps authored dialogue fields public and typing state private
- `InteractableObject.moducpp` keeps selection configuration public and previous-input booleans private
- `SampleInspector Simplified.moducpp` stores persisted tool configuration in `Config<T>()` and runtime-only details elsewhere

### Public Fields: The Normal Authoring Path

#### What Public Fields Are For
Public fields are the standard persisted script configuration path.

Use them when:

- the value belongs in the inspector
- the value should persist with the script instance
- the field shape already matches the authored data you want

Common public field shapes used in the repository include:

- `bool`, `int`, `float`
- `string`
- `Vector2`, `Vector3`, `vec3`
- enums
- `string[]`
- fixed-size arrays such as `int[4]` and `string[6]`
- `SubScript[]`
- `DialoguePort.DialogueLine[]`

#### Why Public Fields Are Usually The Best Default
They are the clearest option when the script is a normal gameplay component:

- the field declaration shows the editable configuration immediately
- the inspector can be generated automatically
- the docs and the scene data stay aligned

#### Example
```cpp
add ModuCPP;
add ModuInput;
add ModuEngine;

public class TopDownMovement2D : ModuNode
{
    [Slider(0.0f, 50.0f)]  public float walkSpeed = 4.0f;
    [Slider(0.0f, 80.0f)]  public float runSpeed = 7.0f;
    [Slider(0.0f, 200.0f)] public float acceleration = 18.0f;
    [Slider(0.0f, 200.0f)] public float drag = 8.0f;
}
```

This is a good fit for public fields because the script is primarily an authored component, not a custom editor tool.

### Private Fields: Runtime-Only State

#### What Private Fields Are For
Private fields are the default place for transient runtime values:

- timers
- cached booleans such as previous input state
- current indices
- animation phase
- temporary status flags

#### Why They Exist
Runtime-only data should not pollute the inspector or persisted scene settings.

The repository scripts use this consistently:

- `prevSubmitDown` in `DialogueSystem.moducpp`
- `prevInteractDown` in `InteractableObject.moducpp`
- `animationTime` and `lastWalkFrame` in `TopDownMovement2D.moducpp`

#### Better Than A Naive "Everything Public" Approach
This is noisy and misleading:

```cpp
public float interval = 1.0f;
public float timer = 0.0f;
public bool initialized = false;
public int currentIndex = 0;
```

This is better:

```cpp
public float interval = 1.0f;

private float timer = 0.0f;
private bool initialized = false;
private int currentIndex = 0;
```

The second version teaches the reader what is configuration and what is just runtime bookkeeping.

### `Config<T>()` And `State<T>()`: Structured Tool-Oriented Authoring

#### Overview
The repository also demonstrates a second style used by tool-like or manual-inspector scripts:

- `SampleInspector Simplified.moducpp`
- `RigidbodyTest.moducpp`
- `StandaloneMovementController.moducpp`

These scripts often avoid large top-level public field lists and instead use:

- `Config<T>()` for persisted structured configuration
- `State<T>()` for runtime-only structured state

#### Why This Pattern Exists
It is useful when:

- the inspector is mostly manual anyway
- the config is logically grouped as a struct
- you want persisted settings without exposing each value as a public field
- the script is closer to a tool or subsystem wrapper than a tiny behavior component

#### Important Behavior
`Config<T>()` gives you per-script-instance storage, but you still need to bind the individual fields through `BindSetting(...)` or another persistence path.

That is why the repository tool scripts repeatedly call helpers like:

```cpp
void bindConfig(ScriptContext& ctx, SampleInspectorSimpleConfig& config) {
    BindSetting(ctx, "autoRotate", config.autoRotate);
    BindSetting(ctx, "spinSpeed", config.spinSpeed);
    BindSetting(ctx, "offset", config.offset);
    BindSetting(ctx, "targetName", config.targetName);
}
```

#### Example
```cpp
add ModuCPP;
add ModuEngine;

public class SampleInspectorSimplified : ModuBehaviour
{
    void Script_OnInspector()
    {
        auto& config = Config<SampleInspectorSimpleConfig>();
        bindConfig(ctx, config);

        bool changed = false;
        changed |= ImGui::Checkbox("Auto Rotate", &config.autoRotate);
        changed |= ImGui::DragFloat3("Spin Speed (deg/s)", &config.spinSpeed.x, 1.0f, -360.0f, 360.0f, "%.2f");
        if (changed) {
            ctx.SaveAutoSettings();
        }
    }
}
```

#### When To Prefer This Style
Prefer `Config<T>()` and `State<T>()` when the script is:

- a manual inspector tool
- a subsystem wrapper such as standalone movement
- a script where grouped config reads more clearly than many top-level public fields

For normal gameplay components, public fields are usually still the simpler choice.

### Field Attributes

#### Overview
Field attributes are authoring metadata. They tell the inspector how a field should behave and what kind of data it represents.

Common attributes already used in the repository include:

- `[Header("Title")]`
- `[Slider(min, max)]`
- `[ObjectRef]`
- `[ObjectList]`
- `[ClipGridPair]`
- `[Separator]`
- `[SoundSet("Label")]`

#### Why They Exist
These attributes solve specific authoring problems:

- a number with a sensible range should feel like a tuned value, not raw text entry
- a string that really means "scene object reference" should be edited like one
- grouped clip fields should be edited together

#### `[ObjectRef]`
Use this when a `string` field is really one scene object reference.

Repository examples:

- `heartRef`
- `playerRef`
- `dialogueTextRef`

This matters because the inspector can then present object-reference editing instead of a plain free-text string.

#### `[ObjectList]`
Use this when a `string[]` field stores a group of scene object references.

Repository examples:

- `itemsToEnable`
- `itemsToDisable`
- `menuItemRefs`
- selection state lists

This is one of the most heavily reused field patterns in the repository, so it deserves to be treated as a first-class authoring tool, not a niche attribute.

#### `[Slider(min, max)]`
Use this when the value is tuned, not just stored.

Repository examples:

- movement speeds
- animation timing
- interaction distance

Sliders teach the intended value range much better than a bare numeric field.

#### `[ClipGridPair]`, `[Separator]`, And `[SoundSet("...")]`
These appear in `TopDownMovement2D.moducpp` and exist for more specialized authoring:

- directional clip sets
- visual grouping
- sound collections edited as a set

### Inspector Modes

#### 1. Automatic Inspector
This is the simplest path. Public fields become the inspector.

Use it when:

- the field list is already clear
- layout does not need extra structure
- the script is small

#### 2. Declarative `inspector { ... }`
This is the most common "real script" upgrade path.

Use it when:

- the script has several categories of configuration
- runtime status belongs next to the settings
- you want layout control without hand-writing all the widgets

Repository examples:

- `DialogueSystem.moducpp`
- `InteractableObject.moducpp`
- `MainMenuController.moducpp`

#### 3. Manual `Script_OnInspector()`
Use this when the inspector itself needs behavior:

- buttons that perform actions immediately
- custom control logic
- explicit `ImGui` layout
- manual use of `Config<T>()`, `State<T>()`, and `ctx.SaveAutoSettings()`

Repository examples:

- `SampleInspector.moducpp`
- `RigidbodyTest.moducpp`
- `StandaloneMovementController.moducpp`

### `AutoFields(...)` And `Run(...)`

#### `AutoFields(...)`
`AutoFields(...)` says: use the normal generated editors for these persisted public fields, but place them here.

This is the workhorse of declarative inspectors.

Example:

```cpp
Tab("Timing") {
    AutoFields(openAnimationDelay, closeAnimationDelay, spacingFactor, sizeMultiplier);
}
```

#### `Run(...)`
`Run(...)` is for inspector-time logic that is not just a field editor.

The repository uses it for:

- runtime status text
- helper methods that draw larger custom sections
- migration or validation work that should happen during inspector use

This is a useful mental split:

- use `AutoFields(...)` for authored data
- use `Run(...)` for behavior, status, or one-off custom drawing

### Manual Inspector Persistence

#### Why `ctx.SaveAutoSettings()` Matters
In a manual inspector, changing an ImGui widget does not automatically persist the value unless you use the persistence helpers correctly.

The repository’s manual inspectors normally follow this pattern:

```cpp
bool changed = false;
changed |= ImGui::Checkbox("Auto Rotate", &config.autoRotate);
changed |= EditString("Target Name", config.targetName, 128, "targetName");
if (changed) {
    ctx.SaveAutoSettings();
}
```

#### Why `BindSetting(...)` Still Matters
If the script uses `Config<T>()`, it must still bind those fields so they are loaded and saved consistently.

This is why scripts such as `SampleInspector Simplified.moducpp` and `RigidbodyTest.moducpp` call their `bindConfig(...)` helper in every relevant hook.

### Nested Data And `SubScript`

#### Overview
The repository uses `SubScript` to keep nested authored data editable and structured.

Examples:

- `MenuAction` in `MainMenuController.moducpp`
- `InteractionOption` in `InteractableObject.moducpp`

#### Why It Exists
Some data is not just one value. It is a little authored record:

- an action with enable and disable lists
- an interaction option with dialogue overrides and object consequences

`SubScript` lets you model that directly instead of flattening it into unrelated parallel arrays.

### Common Mistakes
- Making runtime-only state public.
- Expecting `AutoFields(...)` to work on private fields or config structs outside the current class.
- Forgetting `[ObjectRef]` or `[ObjectList]` on reference-like string fields.
- Using `Config<T>()` without binding settings.
- Forgetting `ctx.SaveAutoSettings()` after manual inspector changes.

### Best Practices
- Default to public fields for normal authored component configuration.
- Keep runtime-only progress private.
- Use `inspector { ... }` when the main need is layout and runtime visibility.
- Use `Config<T>()` and `State<T>()` for manual inspectors and tool-like scripts.
- Use field metadata to communicate authoring intent, not just appearance.

### Related Pages
- [Common Patterns](common-patterns.md)
- [Inspector Reference](../api/inspector-reference.md)
- [Module: ModuCPP](../api/module-moducpp.md)
- [Module: ModuCPP.Experimental](../api/module-moducpp-experimental.md)


---

<!-- Source: docs/moducpp/manual/methods-and-lifecycle.md -->

## Methods and Lifecycle

### Overview
ModuCPP scripts are driven by recognized hooks. Those hooks are not just naming conventions. They are the places where the runtime or editor gives your script control.

The shipped scripts show three major lifecycle families:

- runtime hooks such as `Begin()` and `TickUpdate()`
- inspector hooks such as `Script_OnInspector()`
- editor-window hooks such as `RenderEditorWindow()`

Understanding which job belongs in which hook is one of the biggest differences between a script that stays readable and a script that turns into one giant mixed-purpose method.

### Why This Feature Exists
A typical script needs several kinds of logic:

- one-time setup
- per-frame behavior
- per-inspector drawing
- editor-only tool drawing

Those jobs have different timing and different responsibilities. Lifecycle hooks exist so you do not need to invent your own structure for every script.

### What High-Level Hooks Provide Automatically
Inside high-level hook bodies, ModuCPP prepares the common script-facing context for you:

- `ctx` is the current `ScriptContext`
- `obj` is the current object facade
- `dt` is the current frame delta time in update-style hooks
- `time.deltaTime` exposes the same value through a named facade

That is why shipped scripts can stay small and direct. They are written as behavior code, not hook-plumbing code.

### When To Use Which Hook
Use:

- `Begin()` for one-time setup or reset
- `TickUpdate()` for frame-driven runtime behavior
- `Update()` mainly for compatibility with older naming
- `Spec()` and `TestEditor()` when the same behavior should also run in those extra modes
- `Script_OnInspector()` for full manual inspector control
- `RenderEditorWindow()` for standalone editor tools
- `ExitRenderEditorWindow()` for tool-window cleanup

### `Begin()`

#### Overview
`Begin()` is the one-time initialization hook.

#### What It Is Good For
The repository scripts use `Begin()` for:

- starting timers
- resetting runtime state
- applying initial selection visuals
- auto-opening a system once on startup
- ensuring required components exist

Examples:

- `DialogueSystem.moducpp` resets dialogue runtime state in `Begin()`
- `InteractableObject.moducpp` migrates legacy settings and applies selection state in `Begin()`
- `StandaloneMovementController.moducpp` ensures colliders and rigidbodies in `Begin()`

#### Typical Pattern
```cpp
void Begin() to timer.Start(interval);
```

Or, for more realistic setup:

```cpp
void Begin()
{
    if (!ctx.object) return;
    ResetRuntimeState();
    applySelectedState();
}
```

#### Why It Exists
Without `Begin()`, scripts often fall into a "first frame special case" pattern inside `TickUpdate()`. That is harder to read and easier to break.

#### Common Mistakes
- Do not hide permanent setup inside the first `TickUpdate()` unless there is a real reason.
- If the script depends on `ctx.object`, guard it explicitly as the shipped scripts do.

### `TickUpdate()`

#### Overview
`TickUpdate()` is the main runtime hook for gameplay behavior.

#### What It Usually Contains
This is where the shipped scripts put:

- input polling
- movement
- UI updates
- timer checks
- state-machine progress
- object enable/disable behavior

Examples:

- `TopDownMovement2D.moducpp` reads `input.WASDNormalized()` and moves every frame
- `FPSDisplay.moducpp` updates the label every frame
- `DialogueSystem.moducpp` advances typing, delays, and submit handling every frame
- `MainMenuController.moducpp` advances menu timing and selection every frame

#### A Good Mental Model
Think of `TickUpdate()` as the place where you answer:

- what is true this frame?
- what changed this frame?
- what should advance this frame?

#### Typical Pattern
```cpp
void TickUpdate()
{
    if (!ctx.object || dt <= 0.0f) return;

    Vector2 move = input.WASDNormalized();
    TryMoveRigidbody2D(ctx, move * speed, acceleration, drag, dt);
}
```

#### Why `dt` Guards Matter
Several shipped scripts explicitly return early when `dt <= 0.0f`. That is a good practical pattern for time-based logic, interpolation, or physics helpers that expect positive delta time.

### `Update()`

#### Overview
`Update()` is an alternate runtime update hook.

#### When To Use It
In current documentation and shipped high-level examples, `TickUpdate()` is the preferred default. Use `Update()` mainly when:

- you are keeping parity with an older script style
- you are maintaining existing code that already uses that hook

The main advice is consistency. New docs and new gameplay samples should normally choose `TickUpdate()`.

### `Spec()` And `TestEditor()`

#### Overview
These are extra execution-mode hooks.

#### What They Are For
The repository’s `SampleInspector.moducpp` and `SampleInspector Simplified.moducpp` run the same rotation behavior in:

- `Spec()`
- `TestEditor()`
- `TickUpdate()`

That demonstrates the intended use: a behavior can participate in additional project or editor execution modes without mixing those concerns into inspector drawing.

#### When To Use Them
Use them when:

- a runtime-like behavior should also run in spec mode
- an editor test mode should preview or validate behavior
- you want the same small runtime function to be reused across multiple hooks

#### Good Pattern
```cpp
void Spec() to ApplyAutoRotate();
void TestEditor() to ApplyAutoRotate();
void TickUpdate() to ApplyAutoRotate();
```

This is a clean example of lifecycle design: the hook bodies stay tiny because the actual behavior lives in one reusable helper.

### `Script_OnInspector()`

#### Overview
`Script_OnInspector()` is the manual inspector hook.

#### What It Solves
Use it when the declarative `inspector { ... }` layout is not enough and the inspector itself needs logic:

- custom buttons
- explicit ImGui controls
- persistent tool settings
- object actions such as "Launch Now" or "Teleport Offset"

Examples in the repository:

- `SampleInspector.moducpp`
- `SampleInspector Simplified.moducpp`
- `RigidbodyTest.moducpp`
- `StandaloneMovementController.moducpp`

#### How It Behaves
This hook is not for runtime gameplay. It is for drawing and handling the per-object editor UI.

That distinction is important. `Script_OnInspector()` may edit gameplay-facing configuration, but it is still editor-facing code.

#### Practical Pattern
```cpp
void Script_OnInspector()
{
    auto& config = Config<RigidbodyTestConfig>();
    bindConfig(ctx, config);

    bool changed = false;
    changed |= ImGui::Checkbox("Launch on Begin", &config.autoLaunch);
    changed |= ImGui::DragFloat3("Launch Velocity", &config.launchVelocity.x, 0.25f, -50.0f, 50.0f, "%.2f");
    if (changed) {
        ctx.SaveAutoSettings();
    }
}
```

#### Why Binding Happens Here And Elsewhere
With `Config<T>()`, the inspector hook is only one consumer of the config. The runtime hooks still need `bindConfig(...)` too. That is why the shipped tool scripts call the same binding helper in multiple hooks.

### `RenderEditorWindow()`

#### Overview
`RenderEditorWindow()` is the standalone editor-window hook.

#### What It Is For
Use it for tools that are not naturally just one object inspector:

- custom panels
- workflow tabs
- debugging utilities
- simple animation or scene-editing tools

Examples:

- `EditorWindowSample.moducpp`
- `AnimationWindow.moducpp`

#### What Makes It Different From `Script_OnInspector()`
`Script_OnInspector()` is object-centric.

`RenderEditorWindow()` is tool-centric.

That difference shapes how you design the UI:

- inspectors explain one script instance
- editor windows provide a workspace or workflow

#### Practical Pattern
```cpp
void RenderEditorWindow()
{
    ImGui::TextUnformatted("EditorWindowSample");
    ImGui::Separator();

    if (ImGui::Button("Log Message")) {
        ctx.AddConsoleMessage("Script tab says: " + note);
    }
}
```

### `ExitRenderEditorWindow()`

#### Overview
This hook runs when the editor window is closing or leaving its active lifecycle.

#### When To Use It
Use it when the tool needs cleanup or state reset.

The repository examples often leave it empty, which is fine. An empty `ExitRenderEditorWindow()` is still a useful statement: the tool has no special cleanup needs.

### The Real Lifecycle Pattern In Shipped Scripts
The repository scripts show a very practical structure:

1. `Begin()` performs setup, migration, or state reset.
2. `TickUpdate()` advances the live behavior.
3. `Script_OnInspector()` or `inspector { ... }` exposes configuration.
4. Editor-only tools use `RenderEditorWindow()` instead of pretending to be runtime components.

That is a healthy separation of concerns and a good default structure for new scripts.

### One-Line `to` Syntax
ModuCPP supports a short `to` form for tiny methods.

```cpp
void Begin() to timer.Start(interval);
int FacingIndex(FacingDirection direction) to (int)direction;
```

#### Why It Exists
Some helpers are clearer as a single expression than as a block.

#### When To Use It
Use it when:

- the body is truly one expression or one call
- the method is obvious from its name

Do not use it when:

- the logic contains branching
- the method carries meaningful behavior that deserves room to read

### Common Lifecycle Mistakes
- Putting first-run initialization inside `TickUpdate()` when `Begin()` would be clearer.
- Mixing editor tool code into runtime hooks.
- Forgetting to guard `ctx.object` in hooks that depend on an attached object.
- Forgetting that manual inspector persistence needs `BindSetting(...)` and `ctx.SaveAutoSettings()`.
- Mutating scene data in editor tools without `ctx.MarkDirty()`.

### Best Practices
- Put setup and reset logic in `Begin()`.
- Put frame-driven gameplay in `TickUpdate()`.
- Reuse shared helpers across hooks instead of duplicating behavior.
- Keep editor UI in inspector or editor-window hooks, not runtime hooks.
- Prefer `TickUpdate()` for new gameplay examples.

### Related Pages
- [Hook Reference](../api/hook-reference.md)
- [Fields and Inspector](fields-and-inspector.md)
- [Editor Scripting](editor-scripting.md)
- [ScriptContext](../api/type-scriptcontext.md)


---

<!-- Source: docs/moducpp/manual/input-and-engine-scripting.md -->

## Input and Engine Scripting

### Overview
Most real gameplay scripts in the repository combine `ModuCPP` with `ModuInput`, `ModuEngine`, or both.

That is not accidental. The core module gives you structure, timers, config/state helpers, and basic facades. But once a script needs to react to the player or drive engine-facing feedback, it usually reaches for:

- `ModuInput` to describe intent
- `ModuEngine` to turn that intent into movement, UI, audio, sprite, or tool behavior

This page explains those helpers the way real scripts use them.

### Why These Modules Exist
Without these modules, small scripts repeatedly have to rebuild the same logic:

- key polling for WASD or confirm
- diagonal normalization
- acceleration and drag
- one-shot audio feedback
- sprite clip switching
- FPS readout formatting

The repository scripts show the intended design: use small helpers to solve recurring gameplay problems cleanly.

### Module Requirements
Keep imports explicit in examples.

Common combinations:

```cpp
add ModuCPP;
add ModuInput;
```

```cpp
add ModuCPP;
add ModuEngine;
```

```cpp
add ModuCPP;
add ModuInput;
add ModuEngine;
```

### `ModuInput`: Express Player Intent

#### Overview
`ModuInput` is the script-facing input layer.

It gives you both low-level polling and high-level intent helpers:

- `KeyDown(...)`
- `KeyPressed(...)`
- `IsRuntimeKeyDown(...)`
- `IsSubmitDown()`
- `input.WASD()`
- `input.WASDNormalized()`
- `input.sprint()`
- `input.jump()`

#### Why The `input` Facade Exists
Most scripts do not care which exact key caused "move right". They care that the player is asking to move right.

That is why the `input` facade exists. It packages the normal gameplay meaning of the keys into a more readable surface.

### `input.WASD()` vs `input.WASDNormalized()`

#### What `input.WASD()` Does
It returns a raw direction vector from the current key state.

That is useful when:

- you want to inspect the raw axis combination
- you intend to apply your own scaling or normalization
- the script is not treating the vector as direct velocity

#### What `input.WASDNormalized()` Does
It returns the same direction intent, but normalized so diagonal movement is not faster than straight movement.

That is the normal choice for:

- top-down movement
- menu cursors
- any script where "full input strength" should mean the same speed in every direction

#### Why Normalization Matters
Without normalization, pressing `W` + `D` gives a vector longer than pressing only `W`. That makes diagonal movement faster.

For most gameplay movement, that is not the intended feel.

#### Example: Character Movement
```cpp
add ModuCPP;
add ModuInput;
add ModuEngine;

public class TopDownMover : ModuNode
{
    public float speed = 4.0f;
    public float acceleration = 18.0f;
    public float drag = 8.0f;

    void TickUpdate()
    {
        Vector2 move = input.WASDNormalized();
        TryMoveRigidbody2D(ctx, move * speed, acceleration, drag, dt);
    }
}
```

#### Example: Menu Cursor Motion
The same helper is also appropriate when a menu cursor should move in a clean four-way or eight-way pattern without diagonal speed boosts.

### `input.sprint()` And `input.jump()`

#### What They Do
These are small intent helpers for two very common gameplay actions:

- sprint
- jump

#### Why They Exist
They save a movement script from constantly repeating specific key checks and make the code read like gameplay logic instead of input plumbing.

#### Example: Sprinting
```cpp
bool running = input.sprint();
float speed = running ? runSpeed : walkSpeed;
```

This exact style appears in `TopDownMovement2D.moducpp`.

#### Example: Jump Audio Edge
```cpp
add ModuCPP;
add ModuInput;
add ModuEngine;

public class JumpSound : ModuNode
{
    public string jumpClip;
    private bool prevJump = false;

    void TickUpdate()
    {
        bool jumpNow = input.jump();
        bool jumpPressed = jumpNow && !prevJump;
        prevJump = jumpNow;

        if (jumpPressed) {
            audio.PlayOneShot(jumpClip);
        }
    }
}
```

### `KeyDown(...)`, `KeyPressed(...)`, And `IsSubmitDown()`

#### When To Use `KeyDown(...)`
Use `KeyDown(...)` for continuous held-state logic on a specific key.

Example:

```cpp
if (KeyDown(KEY_E)) {
    ctx.AddConsoleMessage("Interact");
}
```

#### When To Use `KeyPressed(...)`
Use `KeyPressed(...)` when you want a press event for a specific key rather than a continuous held state.

That is usually the direct-key version of the edge-detection pattern.

#### When To Use `IsSubmitDown()`
Use `IsSubmitDown()` when the script concept is not "Enter specifically", but "confirm/submit".

That makes dialogue and menu logic easier to read and easier to retarget if the shared submit definition changes.

#### Why Repository Scripts Still Track Previous State
`DialogueSystem.moducpp` still turns submit state into an explicit one-frame edge:

```cpp
const bool submitDown = IsSubmitDown();
const bool submitPressed = submitDown && !prevSubmitDown;
prevSubmitDown = submitDown;
```

That is the correct pattern when a held confirm key should not advance every frame.

### `TryMoveRigidbody2D(...)`: The Core 2D Movement Helper

#### Overview
This is one of the most practical helpers in `ModuEngine`.

#### What It Does
It takes:

- a target velocity
- acceleration
- drag
- frame delta time

and tries to move the current object through its `Rigidbody2D`.

#### Why It Exists
Most 2D movement scripts want the same thing:

- smooth movement toward target velocity
- damping when no input is present
- safe failure when the object is not set up correctly

That is exactly what the helper provides.

#### Simple Use
```cpp
TryMoveRigidbody2D(ctx, move * speed, acceleration, drag, dt);
```

#### Why The `outVelocity` Form Exists
The repository’s `TopDownMovement2D.moducpp` uses the overload that returns the resulting velocity through an output reference:

```cpp
Vector2 actualVelocity = targetVelocity;
TryMoveRigidbody2D(ctx, targetVelocity, acceleration, drag, dt, ref actualVelocity);
```

This is useful when animation or audio should respond to actual motion rather than only to input intent.

#### Example: Animation Driven By Real Motion
```cpp
Vector2 move = input.WASDNormalized();
Vector2 targetVelocity = move * walkSpeed;
Vector2 actualVelocity = targetVelocity;

TryMoveRigidbody2D(ctx, targetVelocity, acceleration, drag, dt, ref actualVelocity);

if (actualVelocity.Dot(actualVelocity) > 0.01f) {
    sprite.SetClip("Walk");
} else {
    sprite.SetClip("Idle");
}
```

#### Important Notes
- If the current object has no `Rigidbody2D`, the helper returns `false` and does nothing.
- If `acceleration <= 0`, the helper behaves like direct target-velocity assignment.
- Drag is mainly relevant when there is little or no target motion.

### Audio Helpers And The `audio` Facade

#### Overview
The repository uses one-shot audio heavily for feedback:

- footsteps in `TopDownMovement2D.moducpp`
- dialogue typing sounds in `DialogueSystem.moducpp`
- interaction and menu sounds in the larger samples

#### Why `audio.PlayOneShot(...)` Exists
Most gameplay scripts do not want to manage a full audio system. They just want to say:

- play confirm sound
- play footstep sound
- play typing tick

The `audio` facade makes that case short and readable.

#### Example: Footstep Playback
```cpp
audio.PlayOneShot(stepSounds[selected]);
```

#### Example: Confirm Sound On Input Edge
```cpp
if (submitPressed) {
    audio.PlayOneShot(confirmClip);
}
```

#### When To Use `ctx.PlayAudioOneShot(...)` Instead
Use the facade for the current object’s normal audio behavior.

Use `ctx.PlayAudioOneShot(...)` directly when:

- you need lower-level control
- the script is deliberately routing playback through a different object or context

### Sprite Helpers And The `sprite` Facade

#### Overview
`sprite.SetClip(...)` appears throughout the movement example for visual state changes.

#### Why It Exists
Animation-like sprite switching is a very common 2D scripting task. The facade keeps it small:

- inspect clip availability
- choose a clip
- set the clip by index or name

#### Example: Movement State
```cpp
if (running) {
    sprite.SetClip("Run");
} else {
    sprite.SetClip("Idle");
}
```

#### Example: Defensive Clip Selection
The repository’s `TopDownMovement2D.moducpp` also checks clip ranges with `ctx.GetSpriteClipCount()` before selecting clips. That is a good pattern when clips are authored as indices and missing entries are possible.

### UI And FPS Helpers

#### `ModuEngine.FPS`
This is the current frame-rate readback and is best used for:

- debug labels
- performance monitoring
- quick sanity checks while profiling or tuning

#### `IntR(...)`
Use `IntR(...)` when a float should read like a stable whole-number UI value.

The repository’s FPS display is a perfect example:

```cpp
obj.UILabel = "FPS: " + IntR(ModuEngine.FPS);
```

#### `ctx.SetFPSCap(...)`
`FPSDisplay.moducpp` also demonstrates a practical engine-setting pattern:

```cpp
ctx.SetFPSCap(clampTo120, 120.0f);
```

This is a reminder that many scripts combine display and behavior adjustment in the same update loop.

### Inspector Helpers From `ModuEngine`

#### Why They Matter
`ModuEngine` is not only runtime movement and feedback. It also provides manual inspector widgets such as:

- `EditString(...)`
- `EditFloat(...)`
- `EditVec3(...)`
- `EditBool(...)`

These are used by:

- `SampleInspector.moducpp`
- `EditorWindowSample.moducpp`

#### What Problem They Solve
They reduce repeated "load auto setting -> draw widget -> save if changed" boilerplate in manual editors.

#### Example
```cpp
EditString("Target Name", targetName, 128, "targetName");
```

This is especially useful when the script wants a manual inspector but still wants normal persisted settings behavior.

### Warning Helpers

#### Overview
`RigidbodyTest.moducpp` uses `warnOnce(...)` to avoid spamming the console every frame or every button press.

#### Why It Exists
Repeated missing-component messages are noisy. `warnOnce(...)` lets a script surface the problem without turning the console into spam.

#### Example
```cpp
if (!ctx.SetRigidbodyVelocity(vec3(0.0f))) {
    warnOnce(ctx, state.warnedMissingRb, "RigidbodyTest: zeroing velocity requires a Rigidbody");
}
```

### Common Mistakes
- Forgetting `add ModuInput;` when using `input`, `KeyDown(...)`, or `IsSubmitDown()`.
- Forgetting `add ModuEngine;` when using `TryMoveRigidbody2D(...)`, `audio`, `sprite`, `EditString(...)`, or `ModuEngine.FPS`.
- Using `input.WASD()` when normalized movement would be more correct.
- Driving one-shot actions from a held state without edge detection.
- Assuming `TryMoveRigidbody2D(...)` will work without a `Rigidbody2D`.

### Best Practices
- Use `ModuInput` for intent and `ModuEngine` for response.
- Prefer `input.WASDNormalized()` for movement unless raw diagonal magnitude is intentional.
- Treat `TryMoveRigidbody2D(...)` as the default helper for simple 2D rigidbody motion.
- Use explicit edge detection for menu confirm, interaction, and one-shot audio.
- Keep imports explicit so missing helpers are easy to diagnose.

### Related Pages
- [Common Patterns](common-patterns.md)
- [Module: ModuInput](../api/module-moduinput.md)
- [Module: ModuEngine](../api/module-moduengine.md)
- [Facades and Helper Types](../api/type-facades-and-helpers.md)


---

<!-- Source: docs/moducpp/manual/editor-scripting.md -->

## Editor Scripting

### Overview
ModuCPP can script editor workflows as well as runtime behavior.

The repository shows three practical editor-scripting styles:

1. automatic or declarative per-object inspectors for gameplay scripts
2. full manual inspectors for scripts that need buttons, custom widgets, or tool actions
3. standalone editor windows for focused workflows such as object utilities or simple animation tools

This page explains how those styles differ and why that separation matters.

### Why Editor Scripting Exists
Not every useful tool belongs in engine source. Many project-specific needs are closer to the script that uses them:

- a better inspector for a complex gameplay component
- a runtime status tab while tuning behavior
- a small rigidbody test panel
- a script-backed animation or scene-editing window

Editor scripting exists so those workflows can live with the script instead of forcing every project to modify the engine for every convenience feature.

### The Three Main Editor Paths

#### 1. Automatic Inspector
This is the default generated editor from public fields.

Use it when:

- the script is simple
- the field list already teaches the user how to configure the script

#### 2. Declarative `inspector { ... }`
This is the most common next step once a script becomes more serious.

Use it when:

- the script needs tabs or grouping
- runtime status belongs in the inspector
- the fields are still the main source of authored data

Repository examples:

- `DialogueSystem.moducpp`
- `InteractableObject.moducpp`
- `MainMenuController.moducpp`

#### 3. Manual `Script_OnInspector()`
Use this when the inspector itself needs custom behavior.

Repository examples:

- `SampleInspector.moducpp`
- `SampleInspector Simplified.moducpp`
- `RigidbodyTest.moducpp`
- `StandaloneMovementController.moducpp`

### Declarative Inspectors: When Layout Is The Main Problem

#### Why This Style Exists
Many real scripts do not need a fully custom editor. They just need the inspector to explain itself better.

That is exactly what the declarative inspector DSL is for.

#### Example
```cpp
add ModuCPP;
add ModuCPP.Experimental;

public class DialogueSystem : ModuNode
{
    [ObjectRef] public string characterNameTextRef;
    [ObjectRef] public string dialogueTextRef;
    public string characterSoundClip;
    public bool autoOpenOnBegin = false;

    private bool running = false;
    private int index = 0;

    inspector
    {
        Tabs {
            Tab("Bindings") {
                AutoFields(characterNameTextRef, dialogueTextRef);
            }
            Tab("Audio") {
                AutoFields(characterSoundClip);
            }
            Tab("Flags") {
                AutoFields(autoOpenOnBegin);
            }
            Tab("Runtime") {
                Run(ImGui::TextDisabled("Running: %s", running ? "Yes" : "No"));
                Run(ImGui::TextDisabled("Line: %d", index + 1));
            }
        }
    }
}
```

#### Why It Works
This pattern gives you the best of both worlds:

- persisted public fields remain the source of truth
- the inspector becomes easier to scan
- runtime debugging information can be shown without exposing private fields as editable settings

### Manual Inspectors: When The Inspector Needs Behavior

#### Overview
Manual inspectors are for cases where the editor UI is not just a layout problem.

Use them when the inspector needs:

- explicit buttons that perform actions immediately
- manual ImGui widgets
- direct scene operations
- grouped config stored in `Config<T>()`

#### Repository Pattern: `Config<T>()` Plus Manual Widgets
`SampleInspector Simplified.moducpp` and `RigidbodyTest.moducpp` show a repeatable pattern:

1. fetch `Config<T>()`
2. bind it with `BindSetting(...)`
3. draw widgets
4. call `ctx.SaveAutoSettings()` when something changed

#### Example
```cpp
void Script_OnInspector()
{
    auto& config = Config<RigidbodyTestConfig>();
    bindConfig(ctx, config);
    auto& state = State<RigidbodyTestState>();

    bool changed = false;
    changed |= ImGui::Checkbox("Launch on Begin", &config.autoLaunch);
    changed |= ImGui::DragFloat3("Launch Velocity", &config.launchVelocity.x, 0.25f, -50.0f, 50.0f, "%.2f");
    if (changed) {
        ctx.SaveAutoSettings();
    }

    if (ImGui::Button("Launch Now")) {
        Launch(ctx, config, state);
    }
}
```

#### Why This Pattern Exists
It is better than forcing every manual inspector to use top-level public fields for everything:

- config stays grouped logically
- runtime warning flags or debug state can live in `State<T>()`
- the inspector can behave more like a purpose-built tool

### Editor Windows: When The Script Is A Tool First

#### Overview
`RenderEditorWindow()` is the right choice when the script should behave like a tool window rather than a component inspector.

Repository examples:

- `EditorWindowSample.moducpp`
- `AnimationWindow.moducpp`

#### Why This Is Different From An Inspector
A per-object inspector explains one script instance.

An editor window provides a workflow:

- a selected target
- controls that affect more than one object
- transport controls
- utility buttons

#### Example
```cpp
add ModuCPP;
add ModuEngine;

public class EditorWindowSample : ModuBehaviour
{
    private bool toggle = false;
    private float sliderValue = 0.5f;
    private string note = "Hello from script!";

    void RenderEditorWindow()
    {
        ImGui::TextUnformatted("EditorWindowSample");
        ImGui::Separator();

        ImGui::Checkbox("Toggle", &toggle);
        ImGui::SliderFloat("Value", &sliderValue, 0.0f, 1.0f, "%.2f");
        EditString("Note", note, 128, "note");

        if (ImGui::Button("Log Message")) {
            ctx.AddConsoleMessage("Script tab says: " + note);
        }
    }

    void ExitRenderEditorWindow() {}
}
```

#### What `AnimationWindow.moducpp` Teaches
The animation sample shows that editor windows can be much richer than a few controls. They can maintain:

- current selection
- playback time
- keyframe data
- transport state

That is an important teaching point: editor windows are not second-class inspectors. They are a real tool surface.

### Persistence In Editor Tools

#### Why This Needs Care
Editor scripts often mix two very different kinds of change:

- changing the script’s own persisted configuration
- changing scene objects directly

Those two changes use different mechanisms.

#### Persisting Tool Configuration
For config values edited through ImGui widgets:

- bind settings with `BindSetting(...)` or `AutoSetting(...)`
- call `ctx.SaveAutoSettings()` when values change

#### Persisting Scene Data
For direct edits to scene objects:

- use helpers such as `ctx.SetPosition(...)`, `ctx.SetRotation(...)`, or `ctx.SetScale(...)`
- call `ctx.MarkDirty()` when the script directly changes serialized scene state

This pattern appears in both `EditorWindowSample.moducpp` and `AnimationWindow.moducpp`.

### Safe Runtime Separation

#### Why This Matters
Editor code and runtime code are different responsibilities. A clean script keeps them separated.

The repository examples model that well:

- gameplay behavior stays in `TickUpdate()`, `Begin()`, `Spec()`, or `TestEditor()`
- editor UI stays in `inspector { ... }`, `Script_OnInspector()`, or `RenderEditorWindow()`

That separation is good for readability, portability, and build safety.

### Common Mistakes
- Using a manual inspector when declarative layout would have been enough.
- Forgetting `ctx.SaveAutoSettings()` in a manual inspector.
- Editing scene objects directly in a tool without `ctx.MarkDirty()`.
- Trying to put workflow-tool UI into runtime hooks.
- Binding `Config<T>()` only inside the inspector and not in runtime hooks that also read it.

### Best Practices
- Prefer automatic or declarative inspectors until you genuinely need custom behavior.
- Use manual inspectors for custom actions and grouped tool config.
- Use editor windows when the workflow is tool-centric rather than object-centric.
- Separate tool persistence from scene-dirty tracking.
- Keep runtime behavior and editor behavior in their proper hooks.

### Related Pages
- [Methods and Lifecycle](methods-and-lifecycle.md)
- [Fields and Inspector](fields-and-inspector.md)
- [Inspector Reference](../api/inspector-reference.md)
- [Hook Reference](../api/hook-reference.md)


---

<!-- Source: docs/moducpp/manual/experimental-features.md -->

## Experimental Features

### Overview
`ModuCPP.Experimental` is an opt-in module for advanced helper behavior that is useful today but should not be mistaken for the stable minimal core of ModuCPP.

That distinction matters. A script that depends only on `ModuCPP`, `ModuInput`, and `ModuEngine` is relying on the regular documented module split. A script that imports `ModuCPP.Experimental` is making a stronger claim: it needs extra convenience helpers whose role is clear, but whose surface should still be documented carefully.

### Why This Feature Exists
Some scripting problems show up repeatedly in real projects, but they do not belong in the smallest possible core:

- serializing scene object references as strings
- resolving those references safely at runtime
- editing object reference lists in the inspector
- targeting UI text from scripts that only know an object reference string
- supporting dialogue, menu, and interaction workflows without forcing all of that into `ModuCPP`

The experimental module exists so those helpers can ship and be used, while still making it obvious that they are not the baseline every simple script should assume.

### When to Use It
Import `ModuCPP.Experimental` when a script needs:

- `[ObjectRef]` or `[ObjectList]` heavy workflows
- scene object references stored as strings
- UI text targeting through helper functions
- object list serialization or resolution
- helper widgets such as object-ref and audio-clip editors
- behavior similar to the shipped `DialogueSystem`, `MainMenuController`, or `InteractableObject` scripts

Do not import it automatically “just in case”. If a script does not need it, leaving it out keeps the dependency surface clearer.

### How It Works
Import the module explicitly:

```cpp
add ModuCPP.Experimental;
```

The module then exposes helpers in a few practical groups.

#### String and Parsing Helpers
These solve the common problem of turning script settings back into useful runtime values.

- `Trim(...)`
- `ParseInt(...)`
- `ParseFloat(...)`
- `ParseBool(...)`
- escaped-field helpers for serialization

#### Object Reference Helpers
These solve the common problem of storing scene object references in a way that survives script settings and can later be resolved again.

- `MakeObjectRef(...)`
- `SerializeObjectRefs(...)`
- `DeserializeObjectRefs(...)`
- `ResolveSceneObjectRef(...)`
- `SetObjectEnabledState(...)`
- `SetObjectsEnabledState(...)`

#### UI Text and Object Helpers
These are useful when scripts need to find a UI text target indirectly or manipulate runtime state through object-reference strings.

- `SetUITextLabel(...)`
- `SetUITextEffects(...)`
- `ResolveUITextTarget(...)`
- `GetObjectReferencePosition(...)`

#### Editor Widgets
These make object-reference-heavy scripts authorable without writing the full widget logic yourself.

- `DrawObjectRefInput(...)`
- `DrawObjectRefListEditor(...)`
- `DrawAudioClipInput(...)`

### The Problem It Solves
A plain string is a poor way to think about a scene object, but it is often how the data is stored in script settings or nested editor structures.

The experimental helpers close that gap:

- authoring stays convenient
- runtime resolution stays explicit
- complex scripts can still serialize and deserialize their object-driven data

This is why the module is heavily used by the more advanced sample scripts.

### Example: Object Reference Driven Toggle
This is one of the simplest practical uses of the module.

```cpp
add ModuCPP;
add ModuCPP.Experimental;

public class ToggleObjects : ModuNode
{
    [ObjectList] public string[] enable;
    [ObjectList] public string[] disable;

    void Begin()
    {
        SetObjectsEnabledState(ctx, enable, true);
        SetObjectsEnabledState(ctx, disable, false);
    }
}
```

This is useful because the script can expose object lists in the inspector while still applying them cleanly at runtime.

### Example: UI Text Targeting
This is another common use case: a script needs to update UI text, but it stores only a reference string.

```cpp
add ModuCPP;
add ModuCPP.Experimental;

public class ToggleMessage : ModuNode
{
    [ObjectRef] public string labelRef;
    public bool active = false;

    void TickUpdate()
    {
        SetUITextLabel(ctx, labelRef, active ? "Active" : "Idle");
    }
}
```

This solves a real runtime problem: the script does not need to know whether the text object is the exact object referenced, a child, or a related target that the helper can resolve.

### Example: Dialogue-Style Script Support
The shipped dialogue and interaction scripts rely on the experimental helpers because they need more than a simple primitive inspector.

Typical uses include:

- serializing arrays of object refs into settings
- resolving dialogue target objects at runtime
- toggling groups of objects at the end of a dialogue line or interaction
- drawing object-ref editors for complex nested data

This is where the module earns its place. It keeps advanced scripts from having to duplicate all of that support code.

### Behavior Notes
- Experimental does not mean “fake” or “unused”. It means the helpers are useful but should be documented as opt-in rather than assumed core.
- The module is script-facing and real in the current repository.
- Some of the helpers exist primarily because they support shipped example systems that are more complex than a minimal gameplay script.

### Best Practices
- Import the module only when the script actually needs it.
- Document that dependency clearly when a script relies on it.
- Prefer stable core modules when the problem can be solved cleanly without experimental helpers.
- Use the experimental helpers especially when they remove duplicated object-reference or UI-targeting logic.

### Related Pages
- [Imports and Modules](imports-and-modules.md)
- [Module: ModuCPP.Experimental](../api/module-moducpp-experimental.md)
- [DialoguePort Namespace](../api/namespace-dialogueport.md)


---

<!-- Source: docs/moducpp/manual/common-patterns.md -->

## Common Patterns

### Overview
This page documents the patterns that actually recur in the shipped and example scripts under `Scripts/`.

That matters. A useful scripting manual should not only list helpers in isolation. It should show how those helpers are combined in real scripts such as:

- `TopDownMovement2D.moducpp`
- `FPSDisplay.moducpp`
- `DialogueSystem.moducpp`
- `InteractableObject.moducpp`
- `MainMenuController.moducpp`
- `SampleInspector.moducpp`
- `SampleInspector Simplified.moducpp`
- `RigidbodyTest.moducpp`
- `StandaloneMovementController.moducpp`

Most day-to-day ModuCPP work is not about inventing a brand-new architecture for every script. It is about choosing a small, readable pattern that fits the job:

- a timer that repeats
- an input vector that drives movement
- an input edge that fires only once
- a UI label that mirrors state
- a list of referenced objects that are enabled or disabled together
- a declarative inspector that mixes editable fields with runtime status
- a manual inspector or editor window backed by `Config<T>()` and `State<T>()`

### Why These Patterns Exist
The repository scripts make the same larger lesson clear: most scripts are built from a few tiny helpers used together.

For example:

- `timer.Start(...)` and `timer.Ready()` are not interesting alone, but together they create reliable periodic behavior.
- `input.WASDNormalized()` and `TryMoveRigidbody2D(...)` are not just two unrelated calls; together they form the normal top-down movement pattern.
- `obj.UILabel`, `IntR(...)`, and `ModuEngine.FPS` form a common "live readout" pattern.
- `[ObjectList]`, `ResolveSceneObjectRef(...)`, and `SetObjectsEnabledState(...)` form a common "scene coordination" pattern.

This page is about those combinations.

### When To Use This Page
Use this page when:

- you know the behavior you want, but you do not know the cleanest ModuCPP structure yet
- a helper name makes sense individually, but you do not yet know what usually goes with it
- you want examples that feel like real scripts instead of isolated API fragments

### Pattern: Repeating Timer

#### Overview
This is the standard pattern for "do something every N seconds".

```cpp
add ModuCPP;

public class Pulse : ModuNode
{
    public float interval = 0.5f;
    private float timer = 0.0f;

    void Begin() to timer.Start(interval);

    void TickUpdate()
    {
        if (!timer.Ready()) return;
        ctx.AddConsoleMessage("Pulse");
    }
}
```

#### What It Does
The timer stores runtime progress privately, advances automatically from frame delta time, and becomes ready whenever the interval completes.

#### Why It Exists
Without the helper, every small timed script would need to hand-roll:

- an elapsed accumulator
- interval comparison
- reset or wraparound logic

That is boilerplate. The timer helper exists so scripts can express periodic intent directly.

#### When To Use It
Use this pattern for:

- repeated UI refresh
- delayed toggles
- periodic audio cues
- polling or heartbeat-style behavior
- typing, cooldown, or pacing loops when a full state machine would be excessive

#### What Happens Over Time
In practice the flow is:

1. `Begin()` starts the cycle once.
2. `TickUpdate()` advances the internal elapsed time every frame.
3. `timer.Ready()` returns `true` only on the frames where the interval completes.
4. The timer immediately continues into the next cycle.

That is why this pattern is good for repeated actions rather than one-shot delays.

#### Multiple Uses
Status pulse:

```cpp
add ModuCPP;
add ModuEngine;

public class StatusPulse : ModuNode
{
    public float interval = 1.0f;
    private float timer = 0.0f;
    private bool visible = false;

    void Begin() to timer.Start(interval);

    void TickUpdate()
    {
        if (!timer.Ready()) return;
        visible = !visible;
        obj.UILabel = visible ? "ONLINE" : "";
    }
}
```

Timed object group change:

```cpp
add ModuCPP;
add ModuCPP.Experimental;

public class DelayedToggle : ModuNode
{
    [ObjectList] public string[] enable;
    [ObjectList] public string[] disable;
    public float delay = 2.0f;

    private float timer = 0.0f;

    void Begin() to timer.Start(delay);

    void TickUpdate()
    {
        if (!timer.Ready()) return;
        SetObjectsEnabledState(ctx, enable, true);
        SetObjectsEnabledState(ctx, disable, false);
    }
}
```

#### Common Mistakes
- Do not make the timer public unless a designer truly needs to author the runtime progress value.
- Start the timer in `Begin()` unless you intentionally want a lazy first-use pattern.
- Remember that the timer helper is repeating. For a one-time event, gate it with additional state.

#### Related Helpers
- `Begin()`
- `TickUpdate()`
- `time.deltaTime`
- `Math::Max`

### Pattern: Live UI Label Or Status Readout

#### Overview
This is the pattern for turning runtime state into a readable UI label.

The smallest real example in the repository is `FPSDisplay.moducpp`:

```cpp
add ModuCPP;
add ModuEngine;

public class FPSDisplay : ModuBehaviour
{
    public bool clampTo120 = false;

    void TickUpdate()
    {
        ctx.SetFPSCap(clampTo120, 120.0f);
        obj.UILabel = "FPS: " + IntR(ModuEngine.FPS);
    }
}
```

#### What It Does
The script updates a text-like UI property every frame from current runtime state.

#### Why It Exists
Many scripts do not need a complex UI binding system. They just need to write a short label:

- FPS
- current mode
- currently selected item
- whether an interaction is available

`obj.UILabel` keeps that case simple.

#### When To Use It
Use it for:

- FPS counters
- short debug readouts
- selection labels
- temporary prompts

Use `SetUITextLabel(...)` instead when the label belongs to another referenced object rather than the current one.

#### Multiple Uses
Simple FPS counter:

```cpp
obj.UILabel = "FPS: " + IntR(ModuEngine.FPS);
```

Selection prompt:

```cpp
add ModuCPP;

public class InteractionPrompt : ModuNode
{
    public string idle = "Walk closer";
    public string ready = "Press Enter";
    public bool inRange = false;

    void TickUpdate()
    {
        obj.UILabel = inRange ? ready : idle;
    }
}
```

Referenced text target:

```cpp
add ModuCPP;
add ModuCPP.Experimental;

public class SelectionStatus : ModuNode
{
    [ObjectRef] public string statusTextRef;
    public string[] labels;
    private int currentIndex = 0;

    void TickUpdate()
    {
        if (currentIndex < 0 || currentIndex >= (int)labels.size()) return;
        SetUITextLabel(ctx, statusTextRef, labels[currentIndex]);
    }
}
```

#### Common Mistakes
- `obj.UILabel` only acts on the current script object. If the text you want to update lives somewhere else, use an object reference and `SetUITextLabel(...)`.
- UI display often benefits from `IntR(...)`, `IntRD(...)`, or `IntRU(...)` so the reader sees stable numbers instead of noisy floats.

#### Related Helpers
- `obj.UILabel`
- `SetUITextLabel(...)`
- `SetUITextEffects(...)`
- `IntR(...)`
- `ModuEngine.FPS`

### Pattern: Input Vector Into Movement Helper

#### Overview
This is the normal 2D movement pattern used by `TopDownMovement2D.moducpp`.

```cpp
add ModuCPP;
add ModuInput;
add ModuEngine;

public class Player2D : ModuNode
{
    public float speed = 4.0f;
    public float acceleration = 18.0f;
    public float drag = 8.0f;

    void TickUpdate()
    {
        Vector2 move = input.WASDNormalized();
        TryMoveRigidbody2D(ctx, move * speed, acceleration, drag, dt);
    }
}
```

#### What It Does
It reads player intent as a direction vector, turns that into a target velocity, and lets the engine helper handle smoothing and drag.

#### Why It Exists
Most top-down movement scripts want the same behavior:

- diagonal movement should not be faster than straight movement
- acceleration should feel smooth
- idle motion should slow down cleanly

That is exactly why `input.WASDNormalized()` and `TryMoveRigidbody2D(...)` are used together so often.

#### Why Normalized Input Matters
`input.WASD()` gives raw axis intent. `input.WASDNormalized()` rescales diagonal input back to length 1.

That means:

- pressing `W` gives full-speed forward movement
- pressing `W` + `D` still gives full-speed movement, not extra-fast diagonal movement

For character movement, menu cursors, and most author-controlled motion, normalized input is usually the correct default.

#### A More Complete Version
The repository’s `TopDownMovement2D.moducpp` also captures actual post-move velocity and uses it to drive animation:

```cpp
add ModuCPP;
add ModuInput;
add ModuEngine;

public class MovingSprite : ModuNode
{
    public float walkSpeed = 4.0f;
    public float acceleration = 18.0f;
    public float drag = 8.0f;
    public float movementThreshold = 0.15f;
    public int idleClip = 0;
    public int walkClip = 1;

    void TickUpdate()
    {
        Vector2 move = input.WASDNormalized();
        Vector2 targetVelocity = move * walkSpeed;
        Vector2 actualVelocity = targetVelocity;

        TryMoveRigidbody2D(ctx, targetVelocity, acceleration, drag, dt, ref actualVelocity);

        const bool moving = actualVelocity.Dot(actualVelocity) >
                            movementThreshold * movementThreshold;
        sprite.SetClip(moving ? walkClip : idleClip);
    }
}
```

This is a better pattern when presentation should follow actual motion instead of raw key state.

#### Another Use Case: Menu Cursor Motion
The same normalized input idea also works outside character movement. A 2D cursor or menu focus indicator can use the same pattern if it is driven by a 2D body or position helper.

#### Common Mistakes
- If the object has no `Rigidbody2D`, `TryMoveRigidbody2D(...)` fails safely and nothing moves.
- Do not use raw key checks for every axis unless you truly need a custom control scheme. `input.WASDNormalized()` already solves the common case.
- If you drive animation from motion, prefer actual resulting velocity when available instead of target velocity alone.

#### Related Helpers
- `input.WASD()`
- `input.WASDNormalized()`
- `input.sprint()`
- `TryMoveRigidbody2D(...)`
- `sprite.SetClip(...)`
- `audio.PlayOneShot(...)`

### Pattern: Input Edge For One-Shot Actions

#### Overview
Many actions should happen once when a button becomes pressed, not every frame while it stays held.

This shows up repeatedly in the repository:

- `InteractableObject.moducpp` tracks `prevInteractDown`
- `DialogueSystem.moducpp` tracks `prevSubmitDown`

#### What It Does
The script stores the previous frame’s button state and compares it with the current frame’s state.

#### Why It Exists
Continuous input is correct for movement, but wrong for:

- confirming a menu choice
- skipping dialogue once
- playing a one-shot sound
- triggering an interaction one time

#### The Pattern
```cpp
add ModuCPP;
add ModuInput;
add ModuEngine;

public class JumpSound : ModuNode
{
    public string jumpClip;
    private bool prevJump = false;

    void TickUpdate()
    {
        bool jumpNow = input.jump();
        bool jumpPressed = jumpNow && !prevJump;
        prevJump = jumpNow;

        if (jumpPressed) {
            audio.PlayOneShot(jumpClip);
        }
    }
}
```

#### When To Use It
Use this pattern when:

- a sound should fire once
- a dialogue advance should happen once
- a menu confirm should happen once
- an interaction should not retrigger every frame

#### A More General Submit Pattern
```cpp
add ModuCPP;
add ModuInput;

public class MenuConfirm : ModuNode
{
    private bool prevSubmitDown = false;

    void TickUpdate()
    {
        const bool submitDown = IsSubmitDown();
        const bool submitPressed = submitDown && !prevSubmitDown;
        prevSubmitDown = submitDown;

        if (submitPressed) {
            ctx.AddConsoleMessage("Confirmed");
        }
    }
}
```

#### Common Mistakes
- `KeyDown(...)` and `input.jump()` are level checks, not edge checks.
- `KeyPressed(...)` exists for direct key polling, but when you use higher-level helpers such as `input.jump()` or `IsSubmitDown()`, the explicit previous-frame pattern is the normal approach.

#### Related Helpers
- `input.jump()`
- `IsSubmitDown()`
- `KeyPressed(...)`
- `audio.PlayOneShot(...)`

### Pattern: Inspector Object Lists That Drive Scene State

#### Overview
This is the normal pattern for authoring scene-object groups in the inspector and toggling them at runtime.

It appears in:

- `AutoEnableAndDisableListsOfObjectsAfterAmountOfTime.moducpp`
- `InteractableObject.moducpp`
- `DialogueSystem.moducpp`
- `MainMenuController.moducpp`

#### What It Does
The script stores scene references as authorable fields, then resolves and acts on them through helpers.

#### Why It Exists
Hard-coding object ids or names in the middle of logic is brittle. The inspector list approach is better because:

- the relationships stay visible to the designer
- the logic stays short
- the same helper can enable or disable many targets

#### Simple Version
```cpp
add ModuCPP;
add ModuCPP.Experimental;

public class ToggleObjects : ModuNode
{
    [ObjectList] public string[] enable;
    [ObjectList] public string[] disable;

    void Begin()
    {
        SetObjectsEnabledState(ctx, enable, true);
        SetObjectsEnabledState(ctx, disable, false);
    }
}
```

#### Why This Pattern Is Better Than Manual Lookups
A naive script might do repeated name lookups by hand inside the logic. The inspector list pattern is better because the list is editable and the runtime call is a single readable statement.

#### Another Use Case: Selection State
`InteractableObject.moducpp` uses the same idea for selection visuals:

```cpp
SetObjectsEnabledState(ctx, selectedStateEnable, isSelected);
SetObjectsEnabledState(ctx, selectedStateDisable, !isSelected);
```

That is a good example of a tiny helper doing real work. The helper is small, but it is central to how real scene coordination scripts stay readable.

#### Common Mistakes
- Use `[ObjectList]` on `string[]` fields when those strings are meant to be scene object references.
- If only one object should be referenced, prefer `[ObjectRef]` and a single string instead of a one-element list.
- If you mutate scene objects directly without going through helper functions, remember to call `ctx.MarkDirty()` when appropriate.

#### Related Helpers
- `[ObjectRef]`
- `[ObjectList]`
- `ResolveSceneObjectRef(...)`
- `SetObjectEnabledState(...)`
- `SetObjectsEnabledState(...)`
- `GetObjectReferencePosition(...)`

### Pattern: Declarative Inspector With Runtime Status

#### Overview
The repository’s larger scripts do not stop at auto-generated inspectors. They often use `inspector { ... }` to separate configuration from runtime visibility.

`DialogueSystem.moducpp`, `InteractableObject.moducpp`, and `MainMenuController.moducpp` all follow this pattern.

```cpp
inspector {
    Tabs {
        Tab("Config") {
            AutoFields(interval, labelRef);
        }
        Tab("Runtime") {
            Run(ImGui::TextDisabled("Elapsed: %.2f", timer));
        }
    }
}
```

#### What It Does
It keeps authored settings in one place and live debugging information in another.

#### Why It Exists
Once a script grows past a few fields, a flat inspector becomes hard to scan. The declarative inspector solves that without forcing you into a fully manual editor implementation.

#### When To Use It
Use it when:

- the script has several groups of settings
- runtime state is useful to inspect while debugging
- the default field order is no longer enough
- you want custom layout, but not the full cost of `Script_OnInspector()`

#### `Run(...)` Is More Than A Status Line
The repository also uses `Run(...)` for helper calls such as legacy-data migration and runtime drawing functions.

That is an important teaching point:

- `AutoFields(...)` is for persisted fields
- `Run(...)` is for arbitrary inspector-time logic

#### Common Mistakes
- `AutoFields(...)` only works for persisted public fields on the current class.
- Runtime-only private values should be shown through `Run(...)`, not pushed into `AutoFields(...)`.

#### Related Helpers
- `Tabs`
- `Tab(...)`
- `AutoFields(...)`
- `Run(...)`
- `Header(...)`
- `Slider(...)`

### Pattern: Manual Inspector Or Tool Window Backed By `Config<T>()` And `State<T>()`

#### Overview
The repository also shows a second family of authoring pattern in `SampleInspector Simplified.moducpp`, `RigidbodyTest.moducpp`, and `StandaloneMovementController.moducpp`.

Instead of public fields, these scripts keep persisted tool configuration in `Config<T>()` and runtime-only tool state in `State<T>()`.

#### Why It Exists
This is useful when:

- the script is inspector-driven or tool-driven first
- configuration should still persist
- you want structured config without exposing everything as top-level public fields
- runtime helper state does not belong in saved script data

#### Pattern
```cpp
add ModuCPP;
add ModuEngine;

public class RigidbodyTest : ModuBehaviour
{
    void Script_OnInspector()
    {
        auto& config = Config<RigidbodyTestConfig>();
        bindConfig(ctx, config);
        auto& state = State<RigidbodyTestState>();

        bool changed = false;
        changed |= ImGui::Checkbox("Launch on Begin", &config.autoLaunch);
        changed |= ImGui::DragFloat3("Launch Velocity", &config.launchVelocity.x, 0.25f, -50.0f, 50.0f, "%.2f");
        if (changed) {
            ctx.SaveAutoSettings();
        }
    }
}
```

#### Why This Pattern Is Useful
It cleanly separates:

- persisted authoring config
- transient runtime state like warning flags or debug caches

That makes tool scripts easier to extend than a pile of public fields and ad hoc globals.

#### Common Mistakes
- `Config<T>()` does not persist by itself. You still need `BindSetting(...)` or another settings binding path.
- Call the binding function every hook where the config is used, not only in the inspector.
- Use `State<T>()` for temporary flags such as "already warned once".

#### Related Helpers
- `Config<T>()`
- `State<T>()`
- `BindSetting(...)`
- `ctx.SaveAutoSettings()`
- `warnOnce(...)`

### Pattern: Direct Scene Edits In Editor Tools

#### Overview
The editor-side scripts show one more important pattern: if a tool directly mutates object fields, it should usually call `ctx.MarkDirty()` after the change.

This appears in `EditorWindowSample.moducpp` and `AnimationWindow.moducpp`.

#### Why It Exists
Some helpers such as `SetObjectEnabledState(...)` already mark dirty internally. But when an editor tool directly changes fields like:

- `target->position += offset`
- `object->ui.label = label`

the script is responsible for marking the object as changed.

#### Example
```cpp
if (ImGui::Button("Nudge +Y"))
{
    Vector3 pos = obj->position;
    pos.y += 0.25f;
    ctx.SetPosition(pos);
    ctx.MarkDirty();
}
```

#### When To Use It
Use this pattern in:

- custom inspectors
- editor windows
- animation tools
- scripts that patch serialized scene data directly

#### Related Helpers
- `ctx.SetPosition(...)`
- `ctx.SetRotation(...)`
- `ctx.SetScale(...)`
- `ctx.MarkDirty()`

### Best Practices
- Start from a repository-shaped pattern rather than rebuilding the structure from scratch every time.
- Keep authored configuration public or in `Config<T>()`; keep runtime progress private or in `State<T>()`.
- Prefer helper combinations that express intent directly.
- When a small helper appears repeatedly in real scripts, treat it as part of the pattern, not as an unimportant detail.

### Related Pages
- [Methods and Lifecycle](methods-and-lifecycle.md)
- [Input and Engine Scripting](input-and-engine-scripting.md)
- [Fields and Inspector](fields-and-inspector.md)
- [Editor Scripting](editor-scripting.md)
- [Facades and Helper Types](../api/type-facades-and-helpers.md)


---

<!-- Source: docs/moducpp/manual/transpilation.md -->

## Transpilation Overview

### Overview
ModuCPP is a frontend, not a second runtime. A high-level `.moducpp` file is turned into native `C++`, then compiled and loaded through the same native script path used by lower-level scripts.

That is the right mental model to keep in mind while reading the rest of the docs. ModuCPP makes scripts easier to author, but it does not replace the native engine-side scripting model underneath.

### Why This Feature Exists
The goal of the transpiler is not to invent a separate runtime language. The goal is to remove repetitive native script boilerplate:

- class-like high-level syntax becomes possible
- public/private field behavior becomes predictable
- the inspector can be generated from declared fields
- helper surfaces such as `ctx`, `obj`, and timers can be used naturally

Without that compile-time layer, every script would need more manual glue and more repeated patterns.

### When to Use This
Read this page when you want to understand:

- why public fields persist
- why private fields do not
- how hooks become native exports
- what the module system really controls
- why ModuCPP features can still fail in ways that look like native script behavior

### How It Works
At a high level, the transpiler:

1. reads `add ...;` import lines and maps them to script API headers
2. parses `public class ... : ModuNode` or `ModuBehaviour`
3. collects public and private fields
4. classifies field types and field metadata
5. rewrites persisted fields into generated config storage
6. rewrites private state into generated runtime storage
7. lowers hooks such as `Begin()` and `TickUpdate()` into native script entry points
8. emits the helper setup that makes `ctx`, `obj`, and `dt` available in high-level methods

### The Problem It Solves
Consider a script with:

- a configurable interval
- a runtime timer
- a `Begin()` hook
- a `TickUpdate()` hook

In raw native code, you would need to decide:

- where to store persisted data
- where to store transient state
- how to bind the inspector
- how to export the hook names

In ModuCPP, the transpiler does that repetitive structural work for you.

### Public vs Private
This is the most important lowering rule to understand.

#### Public Fields
Public fields are treated as persisted configuration. They become part of generated config storage and are bound through the script settings path.

#### Private Fields
Private fields are treated as runtime state. They are not automatically persisted, because they represent behavior in progress rather than authoring-time configuration.

That is why the visibility split is not just style. It changes the generated code.

### Inspector Generation
The inspector path follows the same logic.

- no custom inspector: public fields generate inspector UI automatically
- `inspector { ... }`: the high-level inspector DSL is lowered into native editor code
- `Script_OnInspector()`: your manual inspector replaces the generated one

Again, these are compile-time structural decisions, not runtime magic.

### Example: High-Level Source
```cpp
add ModuCPP;

public class AutoToggle : ModuNode
{
    public float interval = 1.0f;
    private float timer = 0.0f;

    void Begin() to timer.Start(interval);

    void TickUpdate()
    {
        if (!timer.Ready()) return;
        ctx.SetObjectEnabled(!ctx.IsObjectEnabled());
    }
}
```

### What Actually Happens
The generated native shape is conceptually closer to this:

- a generated config type containing `interval`
- a generated state type containing `timer`
- a native inspector binding path for the public field
- a native `Begin(...)` wrapper that sets up the ModuCPP helper context
- a native `TickUpdate(...)` wrapper that advances runtime behavior every frame

The exact generated code is an implementation detail, but the behavior model is stable enough to reason about.

### Why This Matters in Practice
When you hit a problem in ModuCPP, the fix is often easier once you remember that the result is still native script behavior:

- missing module imports behave like missing API surfaces in generated code
- invalid field declarations fail at transpile time
- object setup issues still matter at runtime because the helpers ultimately call into the native engine

The transpiler saves you authoring work, but it does not hide how the engine actually works.

### Best Practices
- Think of ModuCPP as compile-time ergonomics layered on top of native scripting.
- Use the visibility rules intentionally so the generated config/state split matches the real behavior of the script.
- Prefer the declarative inspector path until you need more control.
- Fall back to raw native `C++` when a problem is better solved at the runtime layer than at the authoring-syntax layer.

### Related Pages
- [Script Structure](script-structure.md)
- [Fields and Inspector](fields-and-inspector.md)
- [Methods and Lifecycle](methods-and-lifecycle.md)
- [Module: ModuCPP](../api/module-moducpp.md)


---

<!-- Source: docs/moducpp/manual/writing-clean-scripts.md -->

## Writing Clean ModuCPP Scripts

### Overview
Clean ModuCPP scripts are not just shorter scripts. They are scripts whose structure makes their runtime behavior obvious.

A good script answers a few questions quickly:

- what does the designer configure?
- what does the runtime calculate?
- which modules does the script depend on?
- where does setup happen?
- where does per-frame behavior happen?

That is the standard this page is aiming for.

### Why This Feature Exists
ModuCPP makes it easy to author scripts quickly. That is useful, but it also creates a risk: if the high-level syntax is treated as a shortcut for everything, scripts can become dense and unclear instead of concise and readable.

This page exists to keep the scripting style aligned with the engine’s real runtime model:

- clear data ownership
- explicit imports
- predictable hooks
- minimal duplication

### When to Use This
Use this page when you are:

- starting a new script
- cleaning up a prototype
- porting from raw native `C++`
- reviewing whether a script is growing too many responsibilities

### How It Works
Clean ModuCPP scripts usually share a few traits:

- imports are explicit and minimal
- public fields are configuration
- private fields are runtime state
- lifecycle methods have one clear job each
- reusable helpers live in shared headers or clearly scoped support code
- editor behavior is separated from runtime behavior

### A Script That Is Too Blurry
This kind of script compiles, but it does not communicate intent well:

```cpp
public float interval = 0.5f;
public float timer = 0.0f;
public bool initialized = false;
public int counter = 0;
```

The problem is not syntax. The problem is that runtime details and configuration are mixed together. A future reader cannot tell what should be edited in the inspector and what only exists to make the logic work.

### A Cleaner Version
```cpp
add ModuCPP;

public class Pulse : ModuNode
{
    public float interval = 0.5f;

    private float timer = 0.0f;
    private int counter = 0;

    void Begin() to timer.Start(interval);

    void TickUpdate()
    {
        if (!timer.Ready()) return;
        counter++;
        ctx.AddConsoleMessage("Pulse");
    }
}
```

This version is cleaner because:

- configuration is easy to identify
- runtime state is private
- timing behavior is expressed through built-in helpers
- the hook responsibilities are clear

### Another Common Cleanup: Over-Importing
It is easy to leave unnecessary imports in a script while prototyping.

Instead of this:

```cpp
add ModuCPP;
add ModuEngine;
add ModuInput;
add ModuCPP.Experimental;
```

use only what the script needs. If the script is only a timer and status logger, `ModuCPP` alone may be enough.

This matters because imports are dependency information, not decoration.

### Real-World Use Case: Small Gameplay Script
For a small gameplay script, cleanliness usually means one behavior, one set of clear fields, and straightforward hooks.

```cpp
add ModuCPP;
add ModuEngine;

public class StatusLight : ModuNode
{
    public string activeText = "On";
    public string inactiveText = "Off";
    public bool active = false;

    void TickUpdate()
    {
        obj.UILabel = active ? activeText : inactiveText;
    }
}
```

Why this reads well:

- the script purpose is obvious
- public data is all authoring-time data
- there is no hidden runtime complexity

### Real-World Use Case: Slightly Larger Script
As scripts grow, cleanliness becomes more about separation of concerns than about line count.

```cpp
add ModuCPP;
add ModuInput;
add ModuEngine;

public class PlayerMover2D : ModuNode
{
    public float walkSpeed = 4.0f;
    public float runSpeed = 7.0f;
    public float acceleration = 18.0f;
    public float drag = 8.0f;

    private bool running = false;

    void TickUpdate()
    {
        Vector2 move = input.WASDNormalized();
        running = input.sprint();

        float speed = running ? runSpeed : walkSpeed;
        TryMoveRigidbody2D(ctx, move * speed, acceleration, drag, dt);
    }
}
```

The script is larger, but it is still clean because:

- field names describe intent
- imports match the feature set
- private state exists only when it helps readability

### Best Practices
- Prefer `ModuNode` for new scripts.
- Keep imports explicit and minimal.
- Use public fields for authoring data and private fields for runtime state.
- Keep `Begin()` for setup and `TickUpdate()` for continuous behavior.
- Use the helper facades when they make the script easier to read.
- Move reusable helper code into shared headers instead of duplicating it across scripts.
- Keep editor-only behavior in inspector or editor-window hooks.
- Preserve runtime behavior when refactoring.

### Related Pages
- [Common Patterns](common-patterns.md)
- [Fields and Inspector](fields-and-inspector.md)
- [Troubleshooting](troubleshooting.md)


---

<!-- Source: docs/moducpp/manual/troubleshooting.md -->

## Troubleshooting

### Overview
Most ModuCPP problems fall into a few practical buckets:

- the script structure is not being recognized
- the wrong module is missing
- the inspector is editing the wrong kind of field
- a helper is correct but the scene or component setup is incomplete
- a manual inspector or editor tool changed data without persisting it correctly

This page uses the repository’s real script patterns to explain how those failures usually look.

### A Good Debugging Order
When a script is not behaving correctly, check in this order:

1. script structure and hook placement
2. module imports
3. field visibility and inspector mode
4. scene references and required components
5. persistence and dirty-state behavior
6. runtime logic in the active hook

That order usually narrows the problem quickly.

### Problem: "No ModuCPP class found"

#### What It Usually Means
The transpiler did not find a recognized high-level class declaration.

It expects a form such as:

```cpp
public class MyScript : ModuNode
```

or:

```cpp
public class MyScript : ModuBehaviour
```

#### What To Check
- Is the file meant to be a high-level ModuCPP script at all?
- Does it declare a class using `ModuNode` or `ModuBehaviour`?
- Are imports at the top of the file instead of interleaved oddly through the body?

### Problem: A Helper Or Facade Is Missing

#### What It Usually Means
The helper belongs to a module that was never imported.

#### Common Cases
- `input`, `KeyDown(...)`, and `IsSubmitDown()` need `add ModuInput;`
- `TryMoveRigidbody2D(...)`, `audio`, `sprite`, `ModuEngine.FPS`, and `EditString(...)` need `add ModuEngine;`
- `ResolveSceneObjectRef(...)`, `SetObjectsEnabledState(...)`, `SetUITextLabel(...)`, and parsing helpers such as `ParseBool(...)` need `add ModuCPP.Experimental;`

#### Why This Happens
Repository scripts are intentionally explicit about their imports because these helpers do not come from `add ModuCPP;` by default.

### Problem: `AutoFields(...)` Cannot See A Value

#### What It Usually Means
`AutoFields(...)` only works with persisted public fields declared on the current high-level class.

It does not work for:

- private runtime state
- values stored only inside `Config<T>()`
- local variables or helper structs

#### Real Fixes
- Make the value a public field if it is true authored configuration.
- Keep it private and show it through `Run(...)` if it is runtime-only.
- Use `Script_OnInspector()` if the inspector is built around `Config<T>()` instead of public fields.

### Problem: Manual Inspector Changes Do Not Persist

#### What It Usually Means
The script changed ImGui-driven values but did not save them through the auto-settings path.

#### What To Check
- Did the manual inspector call `ctx.SaveAutoSettings()` when values changed?
- If the script uses `Config<T>()`, did it also call `BindSetting(...)` or a binding helper in the active hooks?

#### Why This Shows Up In Real Scripts
`SampleInspector Simplified.moducpp` and `RigidbodyTest.moducpp` both demonstrate that `Config<T>()` is only half of the persistence story. The fields still need binding, and manual edits still need saving.

### Problem: Object Reference Does Not Resolve

#### What It Usually Means
A stored object-reference string could not be resolved as:

- an explicit object ref
- a numeric id
- an exact object name

#### What To Check
- Is the referenced object actually present?
- Was the object renamed after the setting was stored?
- Should the field be marked `[ObjectRef]` or `[ObjectList]`?
- Is the script using `ResolveSceneObjectRef(...)` when it should?

#### Related Pattern
Scripts such as `DialogueSystem.moducpp`, `InteractableObject.moducpp`, and `MainMenuController.moducpp` are built around correct object-reference resolution. If those references are stale, the helpers cannot do their job.

### Problem: UI Label Changes Do Not Appear

#### What It Usually Means
The script is writing to the wrong object or the wrong UI path.

#### What To Check
- If the code uses `obj.UILabel`, is the current script object actually the UI text object you want?
- If the target text is elsewhere, should the script use `[ObjectRef]` plus `SetUITextLabel(...)` instead?
- Does the object reference resolve to a real UI text target?

#### Common Real-World Cause
`obj.UILabel` is correct for a self-updating text object such as `FPSDisplay.moducpp`. It is not the right tool for a dialogue system that writes into separate referenced text widgets.

### Problem: 2D Movement Does Nothing

#### What It Usually Means
`TryMoveRigidbody2D(...)` is being called correctly, but the object setup is incomplete.

#### What To Check
- Does the object have a `Rigidbody2D`?
- Is `dt` greater than zero?
- Is the movement script attached to the intended object?

#### Why This Matters
The helper fails safely instead of crashing. That is good behavior, but it also means a setup problem can look like a logic problem until you inspect the object.

### Problem: Rigidbody Helpers Fail In Tool Scripts

#### What It Usually Means
The script expects a `Rigidbody`, but the selected object does not have one.

#### Real Repository Example
`RigidbodyTest.moducpp` explicitly handles this with `ctx.HasRigidbody()` and `warnOnce(...)`.

#### What To Check
- Does the object have a 3D rigidbody?
- Are you calling `SetRigidbodyVelocity(...)`, `TeleportRigidbody(...)`, or angular velocity helpers on an object that only has transform data?

### Problem: Audio Or Sprite Calls Do Nothing

#### What It Usually Means
The current object does not have the required audio or sprite setup.

#### What To Check
- Does the current object actually have an audio source when using `audio.PlayOneShot(...)`, `audio.Play()`, or `audio.Stop()`?
- Does it have sprite clips when using `sprite.SetClip(...)`?
- Is the requested clip index valid?

#### Practical Tip
Repository scripts often guard or validate clip access explicitly when clips are authored by index.

### Problem: Direct Object Changes Seem To Work But Do Not Save

#### What It Usually Means
The script mutated object data directly in an inspector or editor window but did not mark the scene object dirty.

#### What To Check
- After direct edits such as position, rotation, scale, label, or enabled-state changes, did the script call `ctx.MarkDirty()`?
- If a helper already marks dirty internally, are you duplicating the behavior or bypassing it?

#### Real Examples
`EditorWindowSample.moducpp` and `AnimationWindow.moducpp` both illustrate the pattern: after scene-editing operations, the tool marks the object dirty so the editor knows the data changed.

### Problem: A Tool Script Uses `Config<T>()`, But Runtime Behavior Reads Defaults

#### What It Usually Means
The config was edited in the inspector, but the runtime hook did not bind settings before reading it.

#### What To Check
- Is `bindConfig(ctx, config)` called in `TickUpdate()`, `Begin()`, `Spec()`, or `TestEditor()` as needed?
- Is the binding only happening inside `Script_OnInspector()`?

#### Why This Happens
`Config<T>()` gives access to structured storage, but the actual field values still need to be synchronized through the binding path in each relevant hook.

### Problem: Toggle Helpers Or UI Text Helpers Are Missing

#### What It Usually Means
The script forgot `add ModuCPP.Experimental;`.

#### Common Missing Items
- `ResolveSceneObjectRef(...)`
- `SetObjectEnabledState(...)`
- `SetObjectsEnabledState(...)`
- `SetUITextLabel(...)`
- `SetUITextEffects(...)`
- `TryPlayAnimationClipNamed(...)`
- `ParseInt(...)`, `ParseFloat(...)`, `ParseBool(...)`

### Problem: Experimental Or Shared Helpers Are Being Treated As Core

#### What It Usually Means
The script or docs are assuming a helper is universal when it is actually opt-in or shared-sample-specific.

#### Examples
- `ModuCPP.Experimental` helpers are opt-in.
- `DialoguePortShared.h` provides shared sample-system helpers, not minimal core language features.

This matters when copying code between scripts. Bring the actual dependencies with you.

### Best Practices
- Diagnose missing helpers as import problems first.
- Diagnose "nothing happens" problems as scene or component setup problems next.
- Treat persistence bugs and dirty-state bugs as separate from gameplay logic bugs.
- Keep object-reference data clearly marked with `[ObjectRef]` and `[ObjectList]`.
- Reuse the same binding helper in every hook that reads `Config<T>()`.

### Related Pages
- [Methods and Lifecycle](methods-and-lifecycle.md)
- [Fields and Inspector](fields-and-inspector.md)
- [Input and Engine Scripting](input-and-engine-scripting.md)
- [Module: ModuCPP.Experimental](../api/module-moducpp-experimental.md)


---

<!-- Source: docs/moducpp/api/README.md -->

## ModuCPP API Reference

### Overview
The API reference describes the current script-facing surface of ModuCPP in concrete terms. Use it when you already know the concept you are working with and need exact details about a type, module, hook, helper, or script-facing namespace.

Where the manual explains workflows, the API pages explain the actual pieces those workflows are built from.

For example:

- the manual explains when lifecycle hooks should be used
- the API reference explains what each hook does and what it is allowed to do
- the manual explains why the modular import split exists
- the API reference explains what each imported module actually exposes

That split is intentional. It keeps concept pages readable without turning reference pages into shallow lists of names.

### Why This Section Exists
ModuCPP is not a giant open-ended standard library. It is a bounded scripting surface backed by specific headers, runtime facades, and transpiler rules in this repository.

That means the reference pages need to do more than repeat names. They need to answer questions such as:

- which module provides this helper?
- is this safe for gameplay runtime, editor use, or both?
- does this type represent real engine state or only a script-facing façade?
- is this stable core API, example-project API, or experimental surface?

The API reference is where those distinctions are made explicit.

### When To Use The API Reference
Use the API pages when:

- you already know the concept and want exact syntax
- you need to check which module must be imported
- you want to compare similar helpers or script bases
- you are validating whether something is stable, experimental, or example-only
- you are updating older docs or scripts and need current names and behavior

If you are still trying to understand the larger scripting workflow, read the manual first. If you already know the workflow and want precise detail, stay here.

### How The Reference Is Organized
#### Module pages
The module pages explain what each `add ...;` import means and what area of responsibility it owns.

- [Module: ModuCPP](module-moducpp.md)
- [Module: ModuEngine](module-moduengine.md)
- [Module: ModuInput](module-moduinput.md)
- [Module: RMeshBuilder](module-rmeshbuilder.md)
- [Module: ModuCPP.Experimental](module-moducpp-experimental.md)

Start here if the main question is “Which import am I supposed to use?”

#### Type and area pages
These pages document the specific script-facing building blocks you use after the correct modules are in place.

- [ModuNode and ModuBehaviour](type-modunode-and-modubehaviour.md)
- [SceneObj](type-sceneobj.md)
- [ScriptContext](type-scriptcontext.md)
- [Facades and Helper Types](type-facades-and-helpers.md)
- [Managed Bridge (`ModuCPP.cs`)](managed-bridge.md)
- [Hook Reference](hook-reference.md)
- [Inspector Reference](inspector-reference.md)
- [Standalone Movement API](type-standalone-movement.md)
- [DialoguePort Namespace](namespace-dialogueport.md)
- [Shipped Example Types](type-shipped-example-types.md)

Use these pages when you need deeper detail about behavior, members, runtime rules, and realistic usage.

### Reading Tips
Start with the module page for the import you are using. That prevents a common class of scripting mistakes where a page is read in isolation and the wrong module assumptions carry into the script.

Then read the type or helper page that matches the work you are doing. For example:

- if you are structuring a new gameplay script, start with [ModuNode and ModuBehaviour](type-modunode-and-modubehaviour.md)
- if you are manipulating objects and components, start with [SceneObj](type-sceneobj.md)
- if you are exposing values to the editor, read [Inspector Reference](inspector-reference.md)
- if you are trying to understand script/environment state, read [ScriptContext](type-scriptcontext.md)

### Scope Notes
- Pages in this section document the current script-facing surface that can be traced in the repository.
- If a feature is real but unstable, the relevant page marks it as experimental.
- If a type comes from shipped example scripts rather than the stable core module set, the relevant page says so directly.
- `RMeshBuilder` is documented accurately as a reserved import with no exposed script helpers yet.

### Related Pages
- [ModuCPP Overview](../README.md)
- [Manual Index](../manual/README.md)


---

<!-- Source: docs/moducpp/api/type-modunode-and-modubehaviour.md -->

## ModuNode and ModuBehaviour

### Summary
`ModuNode` and `ModuBehaviour` are the two high-level base names recognized by the current ModuCPP transpiler.

They are less about classical inheritance and more about authoring shape: they tell the transpiler "this file is a high-level ModuCPP script class".

### Syntax
```cpp
public class MyScript : ModuNode
```

```cpp
public class MyScript : ModuBehaviour
```

### Why These Base Names Exist
When the transpiler sees one of these bases, it knows the file should be treated as a high-level script. That allows it to:

- recognize hooks such as `Begin()` and `TickUpdate()`
- collect public and private fields
- generate inspector behavior
- provide the high-level `ctx` and `obj` model

That is the real reason these bases matter.

### `ModuNode`

#### What It Represents
`ModuNode` is the preferred current documented base for new gameplay-oriented examples.

#### Why A Script Author Chooses It
Choose `ModuNode` when:

- writing a new gameplay script
- following the newer documentation style
- building a normal component with public fields, lifecycle hooks, and optional declarative inspector layout

#### Repository-Style Use
The repository’s larger gameplay-style examples use `ModuNode`:

- `DialogueSystem.moducpp`
- `InteractableObject.moducpp`
- `MainMenuController.moducpp`
- `TopDownMovement2D.moducpp`

#### Example
```cpp
add ModuCPP;

public class AutoEnable : ModuNode
{
    public bool startEnabled = true;

    void Begin()
    {
        ctx.SetObjectEnabled(startEnabled);
    }
}
```

### `ModuBehaviour`

#### What It Represents
`ModuBehaviour` remains a supported high-level base and is common in older-style or tool-oriented scripts.

#### Why A Script Author Chooses It
Choose `ModuBehaviour` when:

- maintaining or porting older content
- keeping consistency with existing scripts
- writing tool-like or inspector-heavy examples that already follow the older naming style

#### Repository-Style Use
The repository’s manual-inspector and tool samples commonly use `ModuBehaviour`:

- `SampleInspector.moducpp`
- `SampleInspector Simplified.moducpp`
- `RigidbodyTest.moducpp`
- `EditorWindowSample.moducpp`
- `AnimationWindow.moducpp`
- `StandaloneMovementController.moducpp`

#### Example
```cpp
add ModuCPP;
add ModuEngine;

public class SampleInspector : ModuBehaviour
{
    public bool autoRotate = false;

    void TickUpdate()
    {
        if (!autoRotate) return;
        ctx.AddConsoleMessage("Rotating");
    }
}
```

### Do They Expose Different Hook Sets?
In practical repository usage, no major hook split is the important point.

Both bases participate in the same general high-level hook model:

- `Begin()`
- `TickUpdate()`
- `Spec()`
- `TestEditor()`
- `Script_OnInspector()`
- `RenderEditorWindow()`
- `ExitRenderEditorWindow()`

The main difference is authoring style, compatibility, and documentation preference.

### A Useful Rule Of Thumb
Use:

- `ModuNode` for new gameplay-facing scripts
- `ModuBehaviour` when preserving continuity with existing scripts or inspector/tool-oriented examples

That rule matches the repository well.

### Common Mistakes
- Thinking these are large API-rich bases in the traditional OOP sense.
- Assuming `ModuBehaviour` is invalid just because newer docs prefer `ModuNode`.
- Treating the base choice as a substitute for good lifecycle design.

### Related APIs
- [Hook Reference](hook-reference.md)
- [Methods and Lifecycle](../manual/methods-and-lifecycle.md)
- [Getting Started](../manual/getting-started.md)


---

<!-- Source: docs/moducpp/api/type-sceneobj.md -->

## SceneObj

### Summary
`SceneObj` is the high-level ModuCPP scene-object reference type used in script-facing field declarations. It is the type you use when a script needs to store references to other scene objects as part of its configurable data.

It is especially common in `SubScript` data, object toggle lists, menus, and interaction systems.

### Syntax
```cpp
public SceneObj[] enable;
public SceneObj[] disable;
public List<SceneObj*> targets;
```

### Description
At the high-level scripting layer, `SceneObj` answers a very practical question: how should a script say “this field refers to another object in the scene”?

The answer is not “store a raw native pointer in every public field”. Public field data needs to be editable, serializable, and restorable. `SceneObj` exists so high-level scripts can describe scene-object relationships in a way that fits those authoring needs.

In practice, this means:

- the field can participate in ModuCPP persistence
- the inspector can treat it as scene-object reference data
- helper code can later resolve it into a runtime object when the script runs

### Members / Parameters
`SceneObj` is primarily a script-facing declaration type. It is not documented as a rich runtime object wrapper with its own standalone methods.

The most important thing to understand is where it is used:

- public high-level fields
- `SubScript` fields
- object lists for enable/disable behavior
- menu and interaction payloads

Forms already used in the repository include:

- `SceneObj[]`
- `List<SceneObj*>`

### Behavior Explanation
`SceneObj` exists on the authoring side of the scripting system. At runtime, scripts usually operate on resolved scene objects through helpers such as:

- `ResolveSceneObjectRef(...)`
- `SetObjectsEnabledState(...)`
- `GetObjectReferencePosition(...)`

That is the important behavioral distinction:

- `SceneObj` is how you declare object relationships in high-level script data
- resolved scene objects are how you act on those relationships at runtime

This is why `SceneObj` is especially useful in persisted data and `SubScript` structures. It gives the authoring layer a clear way to represent scene links without pretending the saved field is already a live runtime pointer.

### When to Use It
Use `SceneObj` when:

- a script field should point to one or more scene objects
- the field should be editable in the inspector
- the field should persist with the script data
- the script will later resolve those objects and act on them

Use lower-level runtime object access when:

- you already have a live object from `ctx.object`, `FindObjectById`, or a resolver helper
- the value is runtime-only rather than persisted script configuration

### Example
#### Object toggle lists
```cpp
add ModuCPP;
add ModuCPP.Experimental;

public class AutoEnable : ModuNode
{
    public SceneObj[] enable;
    public SceneObj[] disable;

    void Begin()
    {
        SetObjectsEnabledState(ctx, enable, true);
        SetObjectsEnabledState(ctx, disable, false);
    }
}
```

This is one of the most common uses of `SceneObj`: a small script that coordinates other objects without hard-coding object ids in its logic.

#### Nested menu data
```cpp
SubScript MenuAction {
    public SceneObj[] enable;
    public SceneObj[] disable;
};
```

This is a good use case because the script needs structured nested data, and each entry must still be editable and serializable.

### Remarks
- Use `SceneObj` for high-level field declarations, not as a replacement for every runtime object access path.
- When documentation or examples move from field declarations to actual runtime behavior, they will usually switch to helper functions that resolve and operate on scene objects.
- If a field is conceptually “an object reference the designer assigns”, `SceneObj` is usually the right declaration type.

### Related APIs
- [Fields and Inspector](../manual/fields-and-inspector.md)
- [Module: ModuCPP.Experimental](module-moducpp-experimental.md)
- [Script Structure](../manual/script-structure.md)


---

<!-- Source: docs/moducpp/api/type-scriptcontext.md -->

## ScriptContext

### Summary
`ScriptContext` is the engine-side runtime object behind almost every script-facing action.

Even when you write high-level code with `obj`, `audio`, `sprite`, `input`, or timer helpers, those conveniences ultimately sit on top of `ScriptContext`.

The repository scripts use `ctx` constantly for:

- scene lookups
- transform edits
- rigidbody queries and commands
- UI and sprite operations
- audio playback
- settings persistence
- editor utilities
- dirty-state tracking

### Syntax
```cpp
ScriptContext& ctx
```

In high-level hooks, `ctx` is provided automatically.

### Why `ScriptContext` Exists
The scripting layer needs a stable bridge into the engine world:

- which object is this script attached to?
- how do I find other objects?
- how do I move or query rigidbodies?
- how do I write UI or sprite state?
- how do I persist settings or mark data dirty?

`ScriptContext` is that bridge.

### Core Fields

#### `object`
The currently attached scene object.

Repository pattern:

```cpp
if (!ctx.object) return;
```

This guard appears frequently because many runtime and editor operations only make sense when a real attached object exists.

#### `script`
The current script component instance.

This becomes important when settings or script-to-script coordination operate at the script-component level rather than only the object level.

### Scene Access And Object Resolution

#### Members
- `FindObjectByName`
- `FindObjectById`
- `ResolveObjectRef`
- `IsObjectEnabled`
- `SetObjectEnabled`
- `GetSelectedObjectId`

#### Why These Matter
Real scripts rarely operate only on themselves. They often need:

- a player object
- a dialogue root
- selected objects in the editor
- targets referenced by name or id

#### Repository Uses
- `AnimationWindow.moducpp` finds target objects by id or name
- manual inspectors resolve target objects for buttons such as "Nudge Target"
- shared dialogue helpers search object hierarchies by id

#### When To Use Them
Use `ctx` scene lookups when:

- the target is not the current object
- the relationship is runtime-driven
- you need more control than a facade provides

### Transform And Scene Editing

#### Members
- `SetPosition`
- `SetPosition2D`
- `SetRotation`
- `SetScale`

#### Why They Matter
These are the normal engine-facing ways to move or edit the current object from script code.

#### Repository Uses
- `SampleInspector.moducpp` applies offsets and rotation
- `AnimationWindow.moducpp` writes interpolated position, rotation, and scale
- `EditorWindowSample.moducpp` nudges the selected object

#### Why A Script Author Chooses These Instead Of Raw Field Mutation
Because these methods express the intended operation clearly and align with engine-side behavior better than arbitrarily patching object fields in ad hoc ways.

### Physics Helpers

#### Members
- `HasRigidbody`
- `HasRigidbody2D`
- `EnsureCapsuleCollider`
- `EnsureRigidbody`
- `SetRigidbody2DVelocity`
- `GetRigidbody2DVelocity`
- `SetRigidbodyVelocity`
- `GetRigidbodyVelocity`
- `SetRigidbodyAngularVelocity`
- `TeleportRigidbody`
- `BindStandaloneMovementSettings`
- `DrawStandaloneMovementInspector`
- `TickStandaloneMovement`

#### Why These Matter
The repository uses `ctx` heavily when physics is the behavior rather than just an implementation detail.

#### 2D Rigidbody Cases
Use:

- `HasRigidbody2D`
- `GetRigidbody2DVelocity`
- `SetRigidbody2DVelocity`

when the script is handling 2D movement directly or through `TryMoveRigidbody2D(...)`.

#### 3D Rigidbody Cases
Use:

- `HasRigidbody`
- `SetRigidbodyVelocity`
- `GetRigidbodyVelocity`
- `SetRigidbodyAngularVelocity`
- `TeleportRigidbody`

when the script needs 3D rigidbody control, as shown in `RigidbodyTest.moducpp` and `SampleInspector.moducpp`.

#### Setup Enforcement
`StandaloneMovementController.moducpp` demonstrates another important `ctx` use:

```cpp
if (config.settings.enforceCollider) {
    ctx.EnsureCapsuleCollider(config.settings.capsuleTuning.x, config.settings.capsuleTuning.y);
}
if (config.settings.enforceRigidbody) {
    ctx.EnsureRigidbody(true, false);
}
```

This is a good example of `ScriptContext` as both runtime and setup utility surface.

### UI And Sprite Operations

#### Members
- `SetUILabel`
- `SetFPSCap`
- `GetSpriteClipCount`
- `GetSpriteClipIndex`
- `GetSpriteClipNameAt`
- `SetSpriteClipIndex`
- `SetSpriteClipName`

#### Why These Matter
These are the lower-level engine operations behind helpers such as:

- `obj.UILabel`
- `sprite.SetClip(...)`
- FPS display logic

#### Repository Uses
- `FPSDisplay.moducpp` uses `ctx.SetFPSCap(...)`
- `TopDownMovement2D.moducpp` checks `ctx.GetSpriteClipCount()` while validating clip indices

#### When To Use Them Directly
Use these methods directly when:

- the helper facade is too limited for the job
- the script needs explicit validation or lower-level clip inspection

### Audio And Animation

#### Audio Members
- `HasAudioSource`
- `PlayAudio`
- `StopAudio`
- `PlayAudioOneShot`

#### Animation Members
- `HasAnimation`
- `PlayAnimation`
- `StopAnimation`
- `PauseAnimation`
- `ReverseAnimation`
- `SetAnimationTime`
- `GetAnimationTime`
- `IsAnimationPlaying`

#### Why These Matter
The repository’s larger scripts use audio and animation as state feedback, not only as manual authoring data.

Repository examples:

- `DialogueSystem.moducpp` plays open, close, skip, and typing sounds
- menu and interaction samples play move/select/trigger sounds

Use `ctx` directly when:

- you need explicit clip playback rather than the current-object `audio` facade
- the script coordinates animation transitions directly

### Settings And Persistence

#### Members
- `GetSetting`
- `SetSetting`
- `GetSettingBool`
- `SetSettingBool`
- `GetSettingFloat`
- `SetSettingFloat`
- `GetSettingVec3`
- `SetSettingVec3`
- `AutoSetting`
- `SaveAutoSettings`

#### Why These Matter
Scripts need both author-facing persistence and script-to-script protocol storage.

Repository uses include:

- legacy-data migration in `MainMenuController.moducpp`
- interaction requests in `DialogueSystem.moducpp`
- manual inspector persistence in `SampleInspector.moducpp`

#### `AutoSetting(...)`
This is the basis of many manual-inspector persistence helpers.

Why a script author chooses it:

- the value should persist like a normal setting
- the script is drawing the widget manually

#### `SaveAutoSettings()`
This commits manual-inspector auto-setting changes after widgets report a change.

This is why repository manual inspectors almost always follow:

```cpp
if (changed) {
    ctx.SaveAutoSettings();
}
```

### Dirty-State Tracking

#### `MarkDirty()`
This is one of the most important practical `ScriptContext` methods for editor and authoring workflows.

#### Why It Exists
Some helper methods already mark dirty internally when they modify scene data. But if a script directly changes serialized object data, it should mark the object dirty so the editor knows something meaningful changed.

#### Repository Uses
- `EditorWindowSample.moducpp`
- `AnimationWindow.moducpp`
- migration code in `MainMenuController.moducpp`
- interaction-setting updates in `InteractableObject.moducpp`

#### When To Use It
Use `ctx.MarkDirty()` when:

- the script directly mutates scene or script data
- the change should be considered a real authored modification

### Console And Tool Utilities

#### `AddConsoleMessage(...)`
Use this when the script should communicate runtime or tool feedback clearly.

Repository uses:

- missing-action warnings
- missing-dialogue-target warnings
- tool-window log messages
- validation messages

#### Standalone Movement Utilities
The standalone movement helper surface exposed by `ctx` is documented separately, but it is worth noting here because the repository uses it as a complete subsystem pattern:

- bind settings
- draw built-in inspector
- tick runtime movement

### What `ctx` Looks Like In Real Scripts
The repository shows a useful rule of thumb:

- use facades when the job is simple and local
- use `ctx` when the script needs explicit engine-side control

That is why even high-level scripts still use `ctx` constantly. It is the real runtime surface, and the facades are just the most common shortcuts.

### Common Mistakes
- Forgetting to guard `ctx.object` before object-dependent operations.
- Editing config in a manual inspector without `SaveAutoSettings()`.
- Mutating scene data directly without `MarkDirty()`.
- Treating helper-safe failures, such as missing rigidbodies, like proof that the logic is wrong.

### Related APIs
- [Facades and Helper Types](type-facades-and-helpers.md)
- [Module: ModuEngine](module-moduengine.md)
- [Standalone Movement API](type-standalone-movement.md)
- [Methods and Lifecycle](../manual/methods-and-lifecycle.md)


---

<!-- Source: docs/moducpp/api/type-facades-and-helpers.md -->

## Facades and Helper Types

### Summary
ModuCPP exposes a small set of facades and shorthand helpers so common script code reads like script code rather than engine glue.

These helpers are important because the repository scripts rely on them constantly:

- `obj` for current-object work
- `time.deltaTime` and `dt` for frame-driven behavior
- `input` for gameplay intent
- `audio` and `sprite` for lightweight feedback
- `ModuEngine.FPS` for display and tuning
- timer, math, and integer-formatting helpers for recurring utility logic

The point is not that the helpers are short. The point is that they match how real scripts are written.

### Why These Helpers Exist
Without these facades, small scripts would keep repeating lower-level calls for jobs that are conceptually simple:

- update my own label
- read movement intent
- play a one-shot clip
- choose a sprite clip
- round a float for UI

The helpers sit on top of `ScriptContext`. They do not replace it. They make the most common script-facing operations easier to express.

### Aliases

#### `vec2` / `Vector2`
These are the standard 2D vector aliases.

They appear throughout the repository in movement code such as:

- `input.WASDNormalized()`
- `TryMoveRigidbody2D(...)`
- animation motion checks

#### `vec3` / `Vector3`
These are the standard 3D vector aliases.

They appear in:

- transform offsets
- rigidbody velocity helpers
- standalone movement settings

#### `string`
`string` is the script-facing alias for `std::string`.

This matters because many high-level scripts use string-based object refs, audio clip paths, labels, and serialized settings values.

### `obj`

#### Overview
`obj` is the current object facade.

#### What It Does
It gives the current script a lightweight way to access its attached object without spelling out `ctx.object` constantly.

#### Why It Exists
Most scripts are primarily acting on the object they are attached to:

- reading transform or component state
- updating their own UI label
- checking whether they are attached at all

That is why `obj` is a facade rather than a rare convenience.

#### Common Uses
Self UI label:

```cpp
obj.UILabel = "FPS: " + IntR(ModuEngine.FPS);
```

Direct scene-object checks:

```cpp
if (!obj) return;
```

Inspector display:

```cpp
if (obj) {
    ImGui::TextDisabled("Attached to: %s (id=%d)", obj->name.c_str(), obj->id);
}
```

#### Important Notes
- `obj` is only for the current attached object.
- If another object should be changed, resolve it explicitly.
- `obj.UILabel` is appropriate for the current UI text object. If the target text lives elsewhere, use `SetUITextLabel(...)`.

### `time`

#### Overview
`time` is the frame-timing facade.

#### Main Member
- `time.deltaTime`

#### Why It Exists
Time-based logic reads more clearly when the value is named semantically instead of passed around as a raw floating-point variable.

#### Repository Uses
The repository uses `time.deltaTime` in manual/tool-style scripts such as:

- `SampleInspector.moducpp`
- `SampleInspector Simplified.moducpp`
- `StandaloneMovementController.moducpp`

while high-level gameplay scripts often use the shorter injected `dt`.

#### Example
```cpp
float dt = time.deltaTime;
ctx.SetRotation(obj->rotation + spinSpeed * dt);
```

### `input`

#### Overview
`input` is the movement-oriented facade from `ModuInput`.

#### Members
- `WASD()`
- `WASDNormalized()`
- `sprint()`
- `jump()`

#### Why It Exists
The repository’s gameplay scripts typically care about intent, not key plumbing:

- which direction should the character move?
- is the player sprinting?
- is jump currently held?

The facade answers those questions directly.

#### `input.WASD()`
Returns raw movement intent.

Use it when:

- you want the directional axis values
- you will normalize or scale the vector yourself

#### `input.WASDNormalized()`
Returns normalized movement intent.

Use it when:

- diagonal speed should match straight-line speed
- the vector will be used directly for movement or cursor velocity

This is the normal repository pattern for 2D movement.

#### `input.sprint()`
Returns whether sprint input is currently held.

Typical use:

```cpp
bool running = input.sprint();
float speed = running ? runSpeed : walkSpeed;
```

#### `input.jump()`
Returns whether jump input is currently held.

Typical use:

```cpp
bool jumpNow = input.jump();
```

When the script needs a one-shot jump event, pair it with previous-frame tracking.

### `audio`

#### Overview
`audio` is the current-object audio facade from `ModuEngine`.

#### Members
- `HasSource()`
- `PlayOneShot(path, volumeScale)`
- `Play()`
- `Stop()`

#### Why It Exists
Many scripts need small audio responses:

- footsteps
- confirm sounds
- typing sounds
- trigger sounds

The facade keeps those responses short and readable.

#### Common Uses
```cpp
audio.PlayOneShot(stepSounds[selected]);
```

```cpp
if (audio.HasSource()) {
    audio.Play();
}
```

#### Important Notes
- The facade is about the current object’s audio setup.
- If playback should come from another object or needs more explicit context handling, use `ctx.PlayAudioOneShot(...)`.

### `sprite`

#### Overview
`sprite` is the current-object sprite clip facade from `ModuEngine`.

#### Members
- `HasClips()`
- `ClipCount()`
- `ClipIndex()`
- `SetClip(index)`
- `SetClip(name)`
- `ClipNameAt(index)`

#### Why It Exists
Movement and feedback scripts often need to switch clips, but should not need to write clip-management boilerplate each time.

#### Repository Uses
`TopDownMovement2D.moducpp` relies heavily on clip selection for:

- idle clip selection
- walking clip selection
- defensive fallback when some clips are missing

#### Example
```cpp
if (running) {
    sprite.SetClip("Run");
} else {
    sprite.SetClip("Idle");
}
```

#### Important Notes
- Name-based clips are easier to read.
- Index-based clips are common when the inspector stores fixed clip grids.
- If clips are authored by index, validating indices with `ctx.GetSpriteClipCount()` is a good defensive pattern.

### `ModuEngine`

#### Overview
`ModuEngine` is the engine facade value exposed by `ModuEngine`.

#### Most Used Member
- `FPS`

#### Why It Exists
Scripts often need frame-rate readback for:

- simple UI
- performance labels
- tuning and debugging

#### Example
```cpp
obj.UILabel = "FPS: " + IntR(ModuEngine.FPS);
```

### Numeric Helpers

#### `IntRD(float)`
Rounds down.

#### `IntR(float)`
Rounds to nearest.

#### `IntRU(float)`
Rounds up.

#### Why They Exist
The repository’s UI scripts demonstrate the need clearly: many values are calculated as floats, but should be displayed as stable whole numbers.

#### Common Uses
```cpp
obj.UILabel = "FPS: " + IntR(ModuEngine.FPS);
```

```cpp
ctx.AddConsoleMessage("Elapsed: " + IntRD(timer));
```

### Math Helpers

#### Members
- `Math::Max`
- `Math::Min`
- `Math::Clamp`
- `Math::Abs`

#### Why They Exist
These helpers keep simple numeric logic readable in the high-level scripting layer.

Repository examples include:

- clamping selected option indices
- guarding minimum speeds
- comparing directional magnitudes
- forcing non-negative timing or distance

#### Example
```cpp
const int optionIndex = Math.Clamp(selectedOptionIndex, 0, (int)options.size() - 1);
```

### Timer Helpers

#### Script-Facing Forms
- `StartTimer(float&, interval)`
- `TimerReady(float&)`
- `TimerReady(float&, interval)`
- high-level shorthand: `timer.Start(interval)` and `timer.Ready()`

#### Why They Exist
Periodic behavior is extremely common and should not require each script to maintain its own timing boilerplate.

#### Repository Pattern
```cpp
void Begin() to timer.Start(interval);

void TickUpdate()
{
    if (!timer.Ready()) return;
    ctx.AddConsoleMessage("Pulse");
}
```

#### Important Notes
- The timer helper is ideal for repeating behavior.
- Store the timer value as runtime state, usually in a private field.

### How These Helpers Fit Together
The repository scripts repeatedly combine these helpers in a predictable way:

- `input` decides intent
- `TryMoveRigidbody2D(...)` or other engine helpers apply behavior
- `sprite` and `audio` present the state
- `obj.UILabel` or `SetUITextLabel(...)` expose feedback
- math and timer helpers keep the surrounding logic small

That combination is the real value of the facade layer.

### Common Mistakes
- Using `obj.UILabel` for a UI text object that is not the current object.
- Treating `input.jump()` or `IsSubmitDown()` as one-shot events without edge detection.
- Assuming `audio` or `sprite` work when the current object lacks those components.
- Using raw `input.WASD()` when normalized intent was the correct movement input.

### Related APIs
- [Module: ModuCPP](module-moducpp.md)
- [Module: ModuInput](module-moduinput.md)
- [Module: ModuEngine](module-moduengine.md)
- [Common Patterns](../manual/common-patterns.md)


---

<!-- Source: docs/moducpp/api/managed-bridge.md -->

## Managed Bridge (`ModuCPP.cs`)

### Summary
`Scripts/Managed/ModuCPP.cs` is the managed bridge used by Mono-hosted `C#` scripts. It mirrors part of the native script API through managed types such as `Context`, `ImGui`, and `Inspector`.

This page is included in the ModuCPP docs because the managed bridge is part of the current scripting surface shipped in the repository, even though `.moducpp` authoring and managed `C#` authoring are different workflows.

### Syntax
```csharp
using ModuCPP;

public class SampleInspectorManaged
{
    public void TickUpdate(IntPtr ctx, float dt)
    {
        var context = new Context(ctx);
        context.AddConsoleMessage("Tick");
    }
}
```

### Description
The managed bridge exists so the engine can expose a script-facing API to Mono-hosted `C#` code without making managed scripts call directly into native details by hand.

It gives managed scripts:

- vector and hit data structures
- object and scene lookup
- transform and physics access
- UI, audio, and sprite helpers
- settings helpers
- reflection-based inspector support

In other words, it plays a role for managed scripts similar to the role the high-level modules play for `.moducpp` scripts: it gives authors a practical scripting surface that sits on top of the native runtime.

### Members

#### Core Data Types
- `Vec2`
- `Vec3`
- `RaycastHit`
- `ConsoleMessageType`
- `ModuObject`

#### Entry and Hosting Types
- `Host.SetNativeApi(...)`
- `Context`
- `ImGui`
- `Inspector`

#### `Context` highlights
- object and scene lookup
- transform access
- rigidbody and Rigidbody2D helpers
- animation helpers
- audio helpers
- UI helpers
- sprite helpers
- settings get/set helpers
- `AutoSetting(...)`
- `AutoSettingsFrom(...)`
- `MarkDirty()`

#### `ImGui` highlights
- `Text(...)`
- `Separator()`
- `Button(...)`
- `Checkbox(...)`
- `DragFloat(...)`
- `DragFloat3(...)`
- `InputText(...)`
- `BeginCombo(...)`
- `EndCombo()`
- `Selectable(...)`
- `AcceptSceneObjectDrop(...)`

#### Inspector Attributes
- `HeadTextAttribute`
- `LabelAttribute`
- `SettingKeyAttribute`
- `DragSpeedAttribute`
- `InspectorIgnoreAttribute`

### Behavior Explanation
The managed bridge is most useful when you think of it in layers.

#### Native API Binding
`Host.SetNativeApi(...)` connects the managed side to the native function table. That is the low-level bridge step.

#### `Context`
`Context` is the everyday script-side entry point. It exists so a managed script can do the same kinds of work a high-level native script does:

- read and write object state
- query settings
- play audio
- control animation
- drive UI

#### `ImGui`
`ImGui` exists so managed scripts can still build manual tool or inspector UI without directly talking to the native ABI.

#### `Inspector`
`Inspector` and the related attributes solve the same general problem as high-level ModuCPP public fields and metadata: they reduce manual editor boilerplate.

### Multiple Examples
#### Runtime update
```csharp
using System;
using ModuCPP;

public class SpinScript
{
    public void TickUpdate(IntPtr ctx, float dt)
    {
        var context = new Context(ctx);
        context.AddConsoleMessage("Tick");
    }
}
```

#### Auto settings
```csharp
using System;
using ModuCPP;

public class SampleInspectorManaged
{
    [Label("Rotation Speed")]
    public Vec3 SpinSpeed = new Vec3(0.0f, 45.0f, 0.0f);

    public void Begin(IntPtr ctx, float deltaTime)
    {
        var context = new Context(ctx);
        context.AutoSettingsFrom(this, save: false);
    }
}
```

#### Manual UI
```csharp
ImGui.Text("Managed Tool");
ImGui.Separator();
```

### Remarks
- The current managed bridge ABI version is documented as `7`.
- Managed scripts are a supported part of the repository, but they are a separate authoring path from `.moducpp`.
- This page stays focused on the script-facing shape of the bridge. For runtime setup and embedding details, use the Mono setup docs.

### Related APIs
- [Mono Embedding Setup](../../mono-embedding.md)
- [Scripting Overview](../../Scripting.md)


---

<!-- Source: docs/moducpp/api/inspector-reference.md -->

## Inspector Reference

### Summary
This page documents the declarative ModuCPP inspector system as it is actually used in the repository.

The key idea is simple: the inspector DSL is for scripts that still want public fields to be the source of authored data, but need more structure, status, or focused editors than a flat generated inspector can provide.

### Syntax
```cpp
inspector {
    Tabs {
        Tab("General") {
            AutoFields(speed, targetRef);
        }
    }
}
```

### Why This System Exists
Complex scripts need two things at once:

- stable, persisted authored fields
- an inspector that teaches the user how the script is meant to be configured

The repository’s larger scripts demonstrate this well:

- `DialogueSystem.moducpp`
- `InteractableObject.moducpp`
- `MainMenuController.moducpp`

Those scripts do not need fully manual inspectors. They need structured, teaching-oriented inspectors.

### Field Attributes

| Attribute | What it is for | Repository-style use |
| --- | --- | --- |
| `[Header("Title")]` | visually group fields in automatic inspectors | sectioning gameplay values |
| `[Slider(min, max)]` | edit a tuned numeric range as a slider | speed, timing, distance |
| `[ObjectRef]` | treat a `string` as one object reference | player, heart, text target refs |
| `[ObjectList]` | treat a `string[]` as multiple object refs | enable/disable lists, menu items |
| `[DialogueLines]` | use dialogue-line editing for dialogue arrays | dialogue authoring |
| `[ClipGridPair]` | edit related directional clip arrays together | top-down movement clip grids |
| `[Separator]` | insert visual separation | clip and sound grouping |
| `[SoundSet("Label")]` | use a sound-set editor for string arrays | footstep or effect sounds |
| `@range(min, max)` | numeric metadata for range-based editing | tuned fields |
| `@step(value)` | drag-speed metadata | fine-grained editing |

#### Why Attributes Matter
Attributes are not cosmetic. They tell the inspector what the data really means.

For example:

- `[ObjectRef]` says "this string is not arbitrary text"
- `[ObjectList]` says "this array is scene coordination data"
- `[Slider(...)]` says "this number is tuned within a known range"

That is why real scripts become easier to configure when the metadata is honest.

### Layout Containers

#### `Tabs { ... }`
Use tabs when the script has several distinct categories of configuration.

This is the dominant repository pattern for larger scripts.

#### `Tab("Name") { ... }`
Use tabs to split responsibilities clearly, such as:

- `Bindings`
- `Timing`
- `Audio`
- `Flags`
- `Runtime`

#### `Section("Name")`, `Group("Name")`, `Foldout("Name")`
Use these for lighter structure when a full tabbed layout is unnecessary.

### Core Inspector Statements

#### `AutoFields(fieldA, fieldB, ...)`

##### What It Does
Places normal generated editors for the listed persisted public fields at the current point in the layout.

##### Why A Script Author Chooses It
Because the fields are already correct, but the default order or grouping is no longer good enough.

##### Repository Pattern
```cpp
Tab("Audio") {
    AutoFields(characterSoundClip, triggerSoundClip, enterSoundClip, exitSoundClip, skipSoundClip);
}
```

##### Important Note
`AutoFields(...)` only works for persisted public fields on the current high-level class.

#### `Run(expression)`

##### What It Does
Runs arbitrary inspector-time code inside the declarative inspector.

##### Why It Exists
Not everything shown in an inspector is a persisted field. Sometimes you need:

- runtime status text
- helper methods that draw custom sections
- validation or migration calls

##### Repository Patterns
Runtime status:

```cpp
Run(ImGui::TextDisabled("Running: %s", running ? "Yes" : "No"));
```

Custom helper call:

```cpp
Run(DrawRuntimeStatus());
```

Migration helper:

```cpp
Run(MigrateLegacySettingsIfNeeded());
```

This is an important teaching point: `Run(...)` is not only for text labels. It is the escape hatch for inspector-time logic while still staying inside the declarative layout system.

#### `Header("Title")`
Use this for a prominent section heading inside the declarative inspector.

#### `Separator()`
Use this for visual separation when a section should read as two smaller groups.

#### `Slider(...)`, `Enum(...)`, `ObjectRef(...)`, `ObjectList(...)`, `AudioClip(...)`
Use these when one field needs a specific inline editor rather than going through `AutoFields(...)`.

This is useful when:

- the label should be customized
- only one field from a larger group needs special placement

#### Specialized Statements
The repository and shipped helpers also support more specialized editors such as:

- `DialogueLines(...)`
- `InteractionOptions(...)`
- `TextEffectFlags(...)`
- `MenuActions(...)`
- `RuntimeDialogueStatus()`
- `RuntimeInteractableStatus()`
- `RuntimeMenuStatus()`
- `ClipGrid(...)`
- `SoundSet(...)`

These exist because some authored data is too rich for plain primitive widgets.

### A Practical Mental Model
Use the declarative inspector as a layered tool:

1. `Tabs` and containers define the information architecture
2. `AutoFields(...)` places the core authored values
3. specialized statements improve editors for richer data shapes
4. `Run(...)` adds runtime visibility or custom logic

That mental model matches the repository scripts closely.

### Common Repository Patterns

#### Configuration Plus Runtime Tab
```cpp
inspector {
    Tabs {
        Tab("Config") {
            AutoFields(interval, targetRef);
        }
        Tab("Runtime") {
            Run(ImGui::TextDisabled("Elapsed: %.2f", timer));
        }
    }
}
```

Why a script author chooses this:

- configuration and runtime status both matter
- private runtime state should be visible but not editable

#### Mixed Auto Fields And Custom Status Helpers
```cpp
Tab("Runtime") {
    Run(DrawRuntimeStatus());
}
```

Why a script author chooses this:

- the runtime view is more complex than one or two `ImGui::TextDisabled(...)` lines

#### Focused Special Editors
```cpp
Tab("Targets") {
    ObjectRef("Dialogue Root", dialogueSystemRef);
    ObjectList("Objects To Enable", itemsToEnable);
}
```

Why a script author chooses this:

- the field is a reference-like value and should read that way in the UI

### Common Mistakes
- Expecting `AutoFields(...)` to work with private fields.
- Using a fully manual inspector when tabs plus `Run(...)` would have been simpler.
- Forgetting that `Run(...)` can perform logic, not just draw text.
- Treating object-reference strings like plain text instead of using `[ObjectRef]` or `[ObjectList]`.

### Related APIs
- [Fields and Inspector](../manual/fields-and-inspector.md)
- [Editor Scripting](../manual/editor-scripting.md)
- [Hook Reference](hook-reference.md)
- [Module: ModuCPP.Experimental](module-moducpp-experimental.md)


---

<!-- Source: docs/moducpp/api/hook-reference.md -->

## Hook Reference

### Summary
Hooks are the named methods through which a high-level ModuCPP script participates in runtime behavior, inspector behavior, and editor-tool behavior.

They are compile-time recognized entry points. That is why their names matter.

### Syntax
```cpp
void Begin()
void TickUpdate()
void Script_OnInspector()
void RenderEditorWindow()
```

### Why Hooks Exist
Scripts need different places for different jobs:

- setup should run once
- gameplay logic should run every frame
- inspector UI should only run while the inspector is being drawn
- editor-window tools should run in editor windows, not in gameplay hooks

The repository scripts follow this separation closely, and that is the best way to read the hook system.

### Runtime Hooks

#### `Begin()`
One-time setup hook.

Use it for:

- starting timers
- resetting runtime state
- one-time migration
- setup validation
- ensuring required components

Repository-style examples:

- `void Begin() to timer.Start(interval);`
- resetting state in `DialogueSystem.moducpp`
- ensuring collider/rigidbody setup in `StandaloneMovementController.moducpp`

#### `TickUpdate()`
Primary frame-driven gameplay hook.

Use it for:

- input polling
- movement
- timer checks
- UI updates
- state-machine progress

Repository-style examples:

- `TopDownMovement2D.moducpp`
- `DialogueSystem.moducpp`
- `InteractableObject.moducpp`
- `FPSDisplay.moducpp`

#### `Update()`
Alternate update-style runtime hook.

Use it mainly for:

- older compatibility-oriented scripts
- codebases that intentionally prefer the older naming

Current docs and shipped high-level examples generally prefer `TickUpdate()`.

#### `Spec()`
Extra runtime hook for spec-mode behavior.

Use it when:

- the same behavior should run in spec mode

Repository-style example:

- `SampleInspector.moducpp` reuses the same rotation helper in `Spec()`

#### `TestEditor()`
Extra runtime hook for editor test-mode behavior.

Use it when:

- behavior should run in editor test execution without being part of inspector drawing

Repository-style example:

- `SampleInspector.moducpp`

### Inspector Hook

#### `Script_OnInspector()`
Manual per-object inspector hook.

Use it when:

- the declarative inspector DSL is not enough
- the inspector needs buttons, custom widgets, or immediate actions
- config is stored through `Config<T>()` plus `BindSetting(...)`

Repository-style examples:

- `SampleInspector.moducpp`
- `RigidbodyTest.moducpp`
- `StandaloneMovementController.moducpp`

### Editor Window Hooks

#### `RenderEditorWindow()`
Standalone editor-tool drawing hook.

Use it when:

- the script acts like a tool window instead of a component inspector

Repository-style examples:

- `EditorWindowSample.moducpp`
- `AnimationWindow.moducpp`

#### `ExitRenderEditorWindow()`
Editor-window cleanup or close hook.

Use it when:

- the tool needs cleanup or state reset when the window closes

An empty implementation is fine when no cleanup is needed.

### What Hooks Get In High-Level Scripts
In high-level scripts, the recognized hooks automatically receive the common script-facing environment:

- `ctx`
- `obj`
- `dt` in update-style hooks
- `time.deltaTime`

That is why hook bodies can stay focused on behavior.

### One-Line `to` Form
Hooks and helper methods can use a one-line `to` form:

```cpp
void Begin() to timer.Start(interval);
```

Use it when the body is truly tiny and obvious.

### Common Mistakes
- Putting initialization into `TickUpdate()` when it belongs in `Begin()`.
- Mixing editor-tool drawing into runtime hooks.
- Treating held input as one-shot behavior inside `TickUpdate()` without edge detection.
- Forgetting that manual inspectors need explicit persistence helpers.

### Related APIs
- [Methods and Lifecycle](../manual/methods-and-lifecycle.md)
- [Editor Scripting](../manual/editor-scripting.md)
- [ScriptContext](type-scriptcontext.md)


---

<!-- Source: docs/moducpp/api/module-moducpp.md -->

## Module: ModuCPP

### Summary
`ModuCPP` is the core high-level scripting module.

It provides the authoring foundation used by almost every script in the repository:

- type aliases
- automatic access to `ctx`, `obj`, and frame timing
- `Config<T>()` and `State<T>()`
- `BindSetting(...)`, `BindArray(...)`, and `BindArray2D(...)`
- timer helpers
- math and integer-formatting helpers
- `SubScript` serialization helpers

If `ModuInput` and `ModuEngine` are "intent" and "response" layers, `ModuCPP` is the structure layer they sit on top of.

### Syntax
```cpp
add ModuCPP;
```

### Why This Module Exists
The core module solves several very common script-authoring problems:

- how to write readable high-level types
- how to separate persisted config from runtime state
- how to keep small timed logic from becoming boilerplate
- how to store nested authored data

The repository scripts use all of these patterns extensively, so this module is not optional background theory. It is the basis of how real ModuCPP scripts are structured.

### Types And Aliases

#### `vec2`, `vec3`, `Vector2`, `Vector3`, `string`
These are the standard high-level aliases used throughout the docs and repository examples.

They exist so a script can read like gameplay logic instead of C++-heavy scaffolding.

### Automatic Access Helpers

#### `ctx`
The current `ScriptContext` is injected into high-level hooks automatically.

#### `obj`
The current object facade is also injected, making current-object code shorter and easier to read.

#### `time.deltaTime`
Frame delta time is available through the time facade, and update-style hooks also typically expose the shorter `dt`.

These helpers are part of why high-level ModuCPP scripts stay small.

### `Config<T>()` And `State<T>()`

#### Overview
These two helpers are among the most important authoring tools in the module.

They solve the core script-structure question:

- what data should persist as authored configuration?
- what data should only exist while the script is running?

#### `Config<T>()`
Use this for persisted structured configuration.

Repository uses:

- `SampleInspector Simplified.moducpp`
- `RigidbodyTest.moducpp`
- `StandaloneMovementController.moducpp`

Why a script author chooses it:

- the config is logically a struct
- the script is manual-inspector-heavy
- top-level public fields would be noisier than grouped config

#### `State<T>()`
Use this for runtime-only structured state.

Repository uses:

- warning flags in tool scripts
- standalone movement runtime state and debug data

Why a script author chooses it:

- the state does not belong in saved scene data
- the runtime behavior needs grouped transient memory

#### Important Behavior
`Config<T>()` and `State<T>()` provide storage. They do not automatically bind individual config fields to saved settings values by themselves.

That is why repository scripts still use binding helpers such as:

```cpp
void bindConfig(ScriptContext& ctx, SampleInspectorSimpleConfig& config) {
    BindSetting(ctx, "autoRotate", config.autoRotate);
    BindSetting(ctx, "spinSpeed", config.spinSpeed);
    BindSetting(ctx, "offset", config.offset);
    BindSetting(ctx, "targetName", config.targetName);
}
```

#### Example: Manual Inspector Config
```cpp
add ModuCPP;
add ModuEngine;

public class SampleInspectorSimplified : ModuBehaviour
{
    void Script_OnInspector()
    {
        auto& config = Config<SampleInspectorSimpleConfig>();
        bindConfig(ctx, config);

        bool changed = false;
        changed |= ImGui::Checkbox("Auto Rotate", &config.autoRotate);
        if (changed) {
            ctx.SaveAutoSettings();
        }
    }
}
```

#### Example: Runtime State
```cpp
auto& state = State<RigidbodyTestState>();
if (!ctx.SetRigidbodyVelocity(vec3(0.0f))) {
    warnOnce(ctx, state.warnedMissingRb, "RigidbodyTest: zeroing velocity requires a Rigidbody");
}
```

### `BindSetting(...)`

#### Overview
`BindSetting(...)` connects a config value to the script settings path.

#### Why It Exists
When a script stores config in `Config<T>()` instead of public fields, individual fields still need to participate in persistence.

That is what `BindSetting(...)` is for.

#### When To Use It
Use it when:

- configuration lives in a custom struct
- a manual inspector or editor window edits the value
- runtime hooks need the same saved config values

#### Important Note
Bind config in every hook that reads it, not only in the inspector. This is one of the main patterns visible in the repository.

### `BindArray(...)` And `BindArray2D(...)`

#### Overview
These are array-shaped versions of the same persistence idea.

#### Why They Exist
Some authored data is naturally stored as fixed-size clip grids or structured arrays, such as the directional clip data patterns visible in movement examples.

Use them when a custom config struct contains fixed-size arrays that should be bound systematically rather than field by field.

### Timer Helpers

#### Members
- `SetFrameDeltaTime(...)`
- `StartTimer(...)`
- `TimerReady(...)`
- shorthand `timer.Start(...)` and `timer.Ready()`

#### Why They Exist
Periodic behavior is common and should not require each script to rebuild the timing logic.

#### What Problem They Solve
Without these helpers, a simple repeating task would need:

- elapsed accumulation
- interval comparison
- wraparound or reset logic

#### Repository Pattern
```cpp
public float interval = 1.0f;
private float timer = 0.0f;

void Begin() to timer.Start(interval);

void TickUpdate()
{
    if (!timer.Ready()) return;
    ctx.AddConsoleMessage("Pulse");
}
```

#### Important Notes
- Store timers as runtime state.
- The helper is ideal for repeating behavior.
- Pair it with `Begin()` for clean lifecycle design.

### Math And Formatting Helpers

#### `Math::Max`, `Math::Min`, `Math::Clamp`, `Math::Abs`
These are used repeatedly in repository scripts for:

- clamping indices
- enforcing minimum speeds or timings
- selecting directional motion
- keeping distances or delays non-negative

#### `IntRD(...)`, `IntR(...)`, `IntRU(...)`
These exist for clean integer-style presentation in UI and debug text.

Repository example:

```cpp
obj.UILabel = "FPS: " + IntR(ModuEngine.FPS);
```

### `SubScript` Serialization Helpers

#### Members
- `SerializeSubScript(...)`
- `DeserializeSubScript(...)`
- `SerializeSubScriptArray(...)`
- `DeserializeSubScriptArray(...)`
- `EditSubScript(...)`
- `EditSubScriptArray(...)`

#### Why They Exist
Many real scripts need nested authored data, not just flat fields.

Repository examples:

- `MenuAction[]` in `MainMenuController.moducpp`
- `InteractionOption[]` in `InteractableObject.moducpp`

Those scripts need nested arrays that still serialize cleanly and stay editable in the inspector. That is the job of the sub-script helpers.

#### Example
```cpp
SubScript MenuAction {
    public SceneObj[] enable;
    public SceneObj[] disable;
};
```

#### Why A Script Author Would Choose This
Because it is better than maintaining several parallel arrays that must stay in sync manually.

### What This Module Looks Like In Real Scripts
The repository tends to use `ModuCPP` in one of two ways:

1. public fields plus private runtime fields in a normal gameplay component
2. `Config<T>()` plus `State<T>()` in a manual inspector or subsystem wrapper

Both approaches are part of the core module’s intended usage.

### Common Mistakes
- Using `Config<T>()` without binding the fields.
- Treating runtime-only state as persisted config.
- Reimplementing timer logic manually when the timer helpers already fit the job.
- Flattening nested authored data instead of using `SubScript`.

### Related APIs
- [Facades and Helper Types](type-facades-and-helpers.md)
- [Module: ModuEngine](module-moduengine.md)
- [Module: ModuInput](module-moduinput.md)
- [Fields and Inspector](../manual/fields-and-inspector.md)


---

<!-- Source: docs/moducpp/api/module-moduengine.md -->

## Module: ModuEngine

### Summary
`ModuEngine` is the engine-helper module.

It exposes the script-facing helpers that turn small decisions into engine-side behavior:

- FPS readback
- 2D movement helpers
- audio and sprite facades
- manual inspector widget helpers
- project gravity helpers
- warning helpers

The repository scripts use this module heavily because it removes repeated gameplay and tooling boilerplate without hiding what the script is doing.

### Syntax
```cpp
add ModuEngine;
```

### Why This Module Exists
Once a script moves beyond pure state and timing, it usually needs engine-facing behavior:

- move through a rigidbody
- play a sound
- switch a sprite clip
- edit a string in a tool inspector
- display the current FPS

Those are recurring problems, and `ModuEngine` exists so scripts can solve them consistently.

### Global And Project Helpers

#### `ModuEngine.FPS`
This is the current frame-rate readback.

Why a script author chooses it:

- to show a debug FPS label
- to monitor performance while tuning
- to keep the script simple instead of calculating FPS manually

Repository example:

```cpp
obj.UILabel = "FPS: " + IntR(ModuEngine.FPS);
```

#### `GetProjectGravityScale()` / `SetProjectGravityScale(scale)`
These expose project-level gravity tuning to scripts.

Use them when:

- a tool or gameplay script needs to inspect or temporarily adjust the global gravity scale

### Inspector Widget Helpers

#### Members
- `EditFloat(...)`
- `EditVec3(...)`
- `EditBool(...)`
- `EditInt(...)`
- `EditString(...)`
- `EditClipSelector(...)`
- `EditDirectionalClipGrid(...)`
- `EditSoundSet(...)`

#### Why They Exist
Manual inspectors and editor windows often need the same pattern:

1. read or bind a value
2. draw a widget
3. save the setting if it changed

The widget helpers package that pattern for common field types.

#### Repository Uses
- `SampleInspector.moducpp`
- `EditorWindowSample.moducpp`

#### Example
```cpp
EditString("Target Name", targetName, 128, "targetName");
```

Why a script author chooses it:

- the script needs a manual inspector
- the value should still behave like a persisted setting

### Rigidbody2D Helpers

#### Members
- `hasRigidbody2D(...)`
- `getRigidbody2DVelocity(...)`
- `setRigidbody2DVelocity(...)`
- `TryMoveRigidbody2D(...)`
- `moveRigidbody2D(...)`
- `movePosition2D(...)`
- `moveTowards(...)`

#### `TryMoveRigidbody2D(...)`

##### Overview
This is one of the most important gameplay helpers in the module.

##### What It Does
It tries to move the current object toward a target 2D velocity using:

- acceleration
- drag
- frame delta time

##### Why It Exists
Top-down movement and similar 2D behaviors repeat the same logic constantly:

- move toward input velocity
- ease into motion
- slow down when input stops

The helper centralizes that pattern.

##### Repository Use
`TopDownMovement2D.moducpp` is the clearest example:

```cpp
Vector2 move = input.WASDNormalized();
Vector2 targetVelocity = move * speed;
TryMoveRigidbody2D(ctx, targetVelocity, acceleration, drag, dt);
```

##### Why The Output-Velocity Overload Matters
The repository also uses the overload that returns actual resulting velocity:

```cpp
Vector2 actualVelocity = targetVelocity;
TryMoveRigidbody2D(ctx, targetVelocity, acceleration, drag, dt, ref actualVelocity);
```

A script author chooses this form when animation or presentation should follow actual movement instead of raw intent.

##### Important Notes
- The helper returns `false` if the object has no `Rigidbody2D`.
- That safe failure is useful, but it also means setup bugs can look like logic bugs if you do not verify the object.

#### `moveTowards(...)`
This is the scalar/vector-style easing primitive behind smooth movement changes.

Use it when:

- you want to move a value toward a target by a maximum delta
- you need the same "approach the target smoothly" behavior outside full rigidbody movement

#### `movePosition2D(...)`
This is a position-edit helper for 2D movement-style workflows.

Use it when:

- the job is direct 2D positional adjustment rather than velocity-based rigidbody motion

### Warning Helpers

#### Members
- `warnOnce(...)`
- `warnMissingComponentOnce(...)`

#### Why They Exist
Scripts that validate setup should tell the user what is wrong without spamming every frame.

Repository example:

```cpp
if (!ctx.SetRigidbodyVelocity(vec3(0.0f))) {
    warnOnce(ctx, state.warnedMissingRb, "RigidbodyTest: zeroing velocity requires a Rigidbody");
}
```

These are especially useful in:

- setup validation
- editor tools
- optional-component scripts

### Audio Helpers

#### Low-Level Functions
- `hasAudioSource(...)`
- `playSound(...)`

#### `audio` Facade
- `audio.HasSource()`
- `audio.PlayOneShot(...)`
- `audio.Play()`
- `audio.Stop()`

#### Why They Exist
The repository’s scripts use audio for lightweight feedback:

- footsteps
- menu movement
- menu select
- trigger sounds
- dialogue typing sounds

That is exactly the kind of behavior the audio facade is designed for.

#### Example
```cpp
audio.PlayOneShot(stepSounds[selected]);
```

#### When To Use The Facade
Use `audio` when:

- the current object is the intended audio source
- you want the simplest readable script

Use `ctx.PlayAudioOneShot(...)` directly when:

- the script needs more explicit control
- playback is intentionally being routed through a different object or context

### Sprite Helpers

#### `sprite` Facade Members
- `HasClips()`
- `ClipCount()`
- `ClipIndex()`
- `SetClip(index)`
- `SetClip(name)`
- `ClipNameAt(index)`

#### Why They Exist
Small animation and feedback scripts constantly need to switch sprite clips. The facade keeps that readable.

#### Repository Use
`TopDownMovement2D.moducpp` uses sprite clips for:

- idle state
- walk-cycle frames
- directional selection
- graceful fallback when some clips are missing

#### Example
```cpp
if (running) {
    sprite.SetClip("Run");
} else {
    sprite.SetClip("Idle");
}
```

### What This Module Looks Like In Real Scripts
The repository tends to use `ModuEngine` in three main ways:

1. gameplay movement and feedback
2. UI or FPS readback
3. manual inspector and editor-window widgets

That is why this module is broader than a single "movement helpers" page.

### Common Mistakes
- Forgetting `add ModuEngine;`.
- Assuming `TryMoveRigidbody2D(...)` implies the presence of a `Rigidbody2D`.
- Using held input for one-shot audio without edge detection.
- Assuming `audio` or `sprite` will work on an object that lacks those components.
- Rebuilding manual widget persistence by hand when `EditString(...)` or related helpers already match the job.

### Related APIs
- [Facades and Helper Types](type-facades-and-helpers.md)
- [ScriptContext](type-scriptcontext.md)
- [Input and Engine Scripting](../manual/input-and-engine-scripting.md)
- [Standalone Movement API](type-standalone-movement.md)


---

<!-- Source: docs/moducpp/api/module-moduinput.md -->

## Module: ModuInput

### Summary
`ModuInput` is the script-facing input module.

It provides both direct polling and higher-level intent helpers used throughout the repository:

- key constants
- `KeyDown(...)`
- `KeyPressed(...)`
- `IsRuntimeKeyDown(...)`
- `IsSubmitDown()`
- the `input` facade

This module exists so scripts can express gameplay intent clearly instead of rebuilding low-level input plumbing for every component.

### Syntax
```cpp
add ModuInput;
```

### Why This Module Exists
Most gameplay scripts do not really want to ask "is GLFW key X down?".

They want to ask:

- is the player moving?
- is the player sprinting?
- is jump currently held?
- was confirm pressed?

`ModuInput` gives the scripting layer a vocabulary for those questions.

### Key Constants

#### Available Repository-Visible Constants
- `KEY_W`
- `KEY_A`
- `KEY_S`
- `KEY_D`
- `KEY_E`
- `KEY_UP`
- `KEY_DOWN`
- `KEY_LEFT`
- `KEY_RIGHT`
- `KEY_SHIFT_LEFT`
- `KEY_SHIFT_RIGHT`
- `KEY_SPACE`
- `KEY_ENTER`
- `KEY_KP_ENTER`

#### Why These Matter
They make direct input code readable when a script genuinely depends on a specific key, such as an interaction shortcut.

### `KeyDown(...)`

#### What It Does
Returns whether a specific key is currently held.

#### Why It Exists
Some scripts truly want direct key ownership rather than a generalized intent helper.

#### When To Use It
Use it for:

- interaction keys such as `E`
- one-off shortcuts
- tool-mode or debug-mode controls

#### Example
```cpp
if (KeyDown(KEY_E)) {
    ctx.AddConsoleMessage("Interact");
}
```

### `KeyPressed(...)`

#### What It Does
Returns a press event for a specific key instead of a continuous held state.

#### Why It Exists
Some logic is specifically key-driven rather than intent-driven, and should only react once when the key is pressed.

#### When To Use It
Use it for:

- one-shot shortcuts
- toggle buttons
- debug actions

If your script is already built around a higher-level helper such as `input.jump()` or `IsSubmitDown()`, an explicit previous-frame edge pattern may still read better.

### `IsRuntimeKeyDown(...)`

#### What It Does
Checks key state across the runtime/editor input path.

#### Why It Exists
Repository scripts such as `MainMenuController.moducpp` use it when they need explicit control over exact key behavior and want a helper that still works cleanly in the runtime/editor context.

#### When To Use It
Use it when:

- you need exact key control
- the `input` facade is too opinionated for the task
- you are building custom submit or navigation logic

### `IsSubmitDown()`

#### What It Does
Returns the shared submit/confirm held state.

#### Why It Exists
Dialogue and menu scripts often care about the concept of "submit" more than any one individual key.

That is why the repository uses it in `DialogueSystem.moducpp`.

#### When To Use It
Use it for:

- dialogue advance
- menu confirm
- interaction confirm

#### Important Note
`IsSubmitDown()` is still a held-state helper. If the action should happen only once, turn it into an edge:

```cpp
const bool submitDown = IsSubmitDown();
const bool submitPressed = submitDown && !prevSubmitDown;
prevSubmitDown = submitDown;
```

### The `input` Facade

#### Overview
The `input` facade is the most common `ModuInput` surface in gameplay scripts.

#### `input.WASD()`
Returns raw 2D movement intent.

Use it when:

- you want direct axis values
- you will normalize or scale the vector yourself

#### `input.WASDNormalized()`
Returns normalized 2D movement intent.

Why a script author chooses it:

- diagonal movement should not be faster than straight movement
- the vector will directly feed a movement helper

Repository example:

```cpp
Vector2 move = input.WASDNormalized();
TryMoveRigidbody2D(ctx, move * speed, acceleration, drag, dt);
```

This is the normal pattern in `TopDownMovement2D.moducpp`.

#### `input.sprint()`
Returns whether sprint is currently held.

Repository example:

```cpp
bool running = input.sprint();
float speed = running ? runSpeed : walkSpeed;
```

#### `input.jump()`
Returns whether jump is currently held.

Repository example:

```cpp
bool jumpNow = input.jump();
```

This is often paired with previous-frame tracking when the script needs a one-shot jump event.

### Two Common Repository Patterns

#### 1. Movement Intent
```cpp
add ModuCPP;
add ModuInput;
add ModuEngine;

public class PlayerMover : ModuNode
{
    public float speed = 4.0f;
    public float acceleration = 18.0f;
    public float drag = 8.0f;

    void TickUpdate()
    {
        Vector2 move = input.WASDNormalized();
        TryMoveRigidbody2D(ctx, move * speed, acceleration, drag, dt);
    }
}
```

#### 2. One-Shot Submit Or Action
```cpp
add ModuCPP;
add ModuInput;

public class ConfirmExample : ModuNode
{
    private bool prevSubmitDown = false;

    void TickUpdate()
    {
        const bool submitDown = IsSubmitDown();
        const bool submitPressed = submitDown && !prevSubmitDown;
        prevSubmitDown = submitDown;

        if (submitPressed) {
            ctx.AddConsoleMessage("Confirmed");
        }
    }
}
```

### Common Mistakes
- Forgetting `add ModuInput;`.
- Using `input.WASD()` when normalized movement was intended.
- Treating held-state helpers as one-shot events.
- Rebuilding WASD logic manually when the facade already matches the behavior.

### Related APIs
- [Facades and Helper Types](type-facades-and-helpers.md)
- [Module: ModuEngine](module-moduengine.md)
- [Input and Engine Scripting](../manual/input-and-engine-scripting.md)


---

<!-- Source: docs/moducpp/api/module-rmeshbuilder.md -->

## Module: RMeshBuilder

### Summary
`RMeshBuilder` is a recognized ModuCPP import target, but the current repository does not expose an actual script-facing helper surface for it yet.

That makes it a real documented module name with a placeholder-level API.

### Syntax
```cpp
add RMeshBuilder;
```

### Description
The important thing to document here is what exists today, not what might exist later.

The module name is recognized by the transpiler and appears in the editor-facing scripting identifiers. However, the current `include/RMeshBuilderScriptApi.h` file does not expose script-facing functions, types, or helper facades beyond a placeholder comment.

In practice, that means:

- the import is valid
- the module is reserved for mesh-builder script helpers
- there is no current public script API to teach or depend on

### Members
No current script-facing members are exposed in the repository’s `RMeshBuilderScriptApi.h`.

### Behavior Explanation
This module behaves differently from the other documented modules because the meaningful behavior right now is absence of an exposed script surface.

That is still useful to document. It tells the reader:

- the module name is not a typo
- the docs are not missing a hidden API section
- if a script imports it today, that import is effectively a reservation for future functionality rather than an active helper dependency

### Example
#### Placeholder import
```cpp
add ModuCPP;
add RMeshBuilder;

public class MeshBuilderPlaceholder : ModuNode
{
    void Begin()
    {
        ctx.AddConsoleMessage("RMeshBuilder currently has no script-facing helpers.");
    }
}
```

#### When not to use it
If a script needs:

- object references
- timers
- input polling
- movement helpers
- editor widgets

then the needed modules are almost certainly `ModuCPP`, `ModuEngine`, `ModuInput`, or `ModuCPP.Experimental`, not `RMeshBuilder`.

### Remarks
- Treat the module as reserved API surface.
- Do not invent mesh-building helpers in project docs until the header actually exposes them.
- If the mesh-builder module grows later, this page should expand from “placeholder import” into a normal module reference page.

### Related APIs
- [Imports and Modules](../manual/imports-and-modules.md)
- [ModuCPP Overview](../README.md)


---

<!-- Source: docs/moducpp/api/module-moducpp-experimental.md -->

## Module: ModuCPP.Experimental

### Summary
`ModuCPP.Experimental` is the shared helper module for advanced script-facing workflows that are visible in the repository’s larger sample systems.

It covers:

- parsing and conversion helpers
- script-setting helpers
- object-reference serialization and resolution
- object and UI state helpers
- a set of editor widgets for reference-heavy authoring

This module is especially important for the repository’s more complex scripts:

- `DialogueSystem.moducpp`
- `InteractableObject.moducpp`
- `MainMenuController.moducpp`

### Syntax
```cpp
add ModuCPP.Experimental;
```

### Why This Module Exists
Some problems show up often in real scripts, but not in every tiny script:

- storing object references as data
- resolving those references safely at runtime
- serializing structured arrays into script settings
- writing UI labels to referenced text objects
- toggling groups of referenced objects

Those workflows are too common to leave to copy-paste boilerplate, but also too specialized to treat as minimal core language features. That is the role of the experimental module.

### Parsing And Conversion Helpers

#### Members
- `Trim(...)`
- `ParseInt(...)`
- `ParseFloat(...)`
- `ParseBool(...)`
- `EscapeField(...)`
- `UnescapeField(...)`
- `SplitEscaped(...)`
- `JoinEscaped(...)`

#### Why They Exist
The larger repository scripts read and write structured values through settings strings.

Examples:

- legacy data migration in `MainMenuController.moducpp`
- interaction request handling in `DialogueSystem.moducpp`
- sub-data migration in `InteractableObject.moducpp`

#### When To Use Them
Use these helpers when:

- a value must be read from `ctx.GetSetting(...)`
- structured data is stored as an encoded string
- user-authored or older saved data needs safe parsing with fallback behavior

#### Example
```cpp
const int interactionSerial = ParseInt(ctx.GetSetting(kInteractionRequestSerial, "0"), 0);
const bool interactionPending = ParseBool(ctx.GetSetting(kInteractionRequestPending, "0"), false);
```

This is a good example of why these helpers matter. They keep settings-driven logic tolerant and readable.

### Script Setting Helpers

#### Members
- `GetScriptSetting(...)`
- `SetScriptSetting(...)`

#### Why They Exist
Sometimes a script needs to read or modify another script component’s settings rather than its own field data.

That is exactly what `InteractableObject.moducpp` does when sending a request into a target `DialogueSystem` script.

#### When To Use Them
Use them when:

- one script is coordinating another script instance
- the target is a `ScriptComponent*` rather than the current `ctx`
- a shared protocol is implemented through script settings

### Object Reference Helpers

#### Members
- `DeserializeObjectRefs(...)`
- `SerializeObjectRefs(...)`
- `MakeObjectRef(...)`
- `ResolveSceneObjectRef(...)`
- `ResolveUITextTarget(...)`
- `IsAllDigits(...)`

#### Why They Exist
Object references are one of the main recurring data shapes in the repository.

Scripts store them as:

- single string refs
- string arrays
- serialized object-ref lists in settings blobs

These helpers exist so those references can move cleanly between inspector data, serialized settings, and live runtime objects.

#### `ResolveSceneObjectRef(...)`

##### What It Does
It tries to resolve a script-facing object reference string into a live scene object.

##### Why A Script Author Chooses It
Because object refs are often stored as strings for authoring reasons, but runtime logic needs actual objects.

##### Repository Use
This helper appears everywhere in the larger samples:

- dialogue target resolution
- player distance checks
- menu item lookups
- mouth object lookups

#### `ResolveUITextTarget(...)`

##### What It Does
It resolves a reference into the most useful UI text object target rather than only the exact object.

##### Why It Exists
Many real UI scripts want to say "write to the text associated with this object hierarchy", not manually traverse parents and children every time.

That is why higher-level helpers such as `SetUITextLabel(...)` and `SetUITextEffects(...)` are built on top of it.

### Object And UI State Helpers

#### Members
- `SetObjectEnabledState(...)`
- `SetObjectsEnabledState(...)`
- `GetObjectReferencePosition(...)`
- `GetCurrentObjectName(...)`
- `TryPlayAnimationClipNamed(...)`
- `SetUITextLabel(...)`
- `SetUITextEffects(...)`
- `SetRigidbody2DSimulated(...)`

#### `SetObjectEnabledState(...)` And `SetObjectsEnabledState(...)`

##### What They Do
They enable or disable one resolved object or a group of references, and mark dirty when a real change occurs.

##### Why They Exist
The repository’s gameplay scripts repeatedly coordinate scene object groups:

- enable these objects
- disable those objects
- flip selection visuals
- apply dialogue consequences

That is common enough to deserve dedicated helpers.

##### Repository Uses
- selection visuals in `InteractableObject.moducpp`
- dialogue line object toggles in `DialogueSystem.moducpp`
- menu actions in `MainMenuController.moducpp`

#### `GetObjectReferencePosition(...)`

##### What It Does
Returns a useful world-space-like position for a referenced object, including UI-aware handling.

##### Why It Exists
Scripts such as `InteractableObject.moducpp` need distance checks between authored references, and some of those references may be UI objects rather than plain world objects.

This helper keeps that logic centralized.

#### `SetUITextLabel(...)`

##### What It Does
Writes a label to a referenced UI text target and marks dirty if the value actually changed.

##### Why A Script Author Chooses It
Because the target text object is not always `ctx.object`.

##### Repository Use
`DialogueSystem.moducpp` uses this pattern extensively for character name and dialogue text widgets.

#### `SetUITextEffects(...)`

##### What It Does
Writes text effect flags and tuning values to a referenced UI text target.

##### Why It Exists
The dialogue sample needs visual text effects as part of authored dialogue behavior, not just raw label changes.

#### `TryPlayAnimationClipNamed(...)`

##### What It Does
Attempts to play an animation clip on the current object by name.

##### Repository Use
`DialogueSystem.moducpp` uses it for named dialogue open and close animation transitions.

#### `SetRigidbody2DSimulated(...)`

##### What It Does
Finds a referenced object and toggles its 2D rigidbody simulation state.

##### Repository Use
The dialogue sample uses this helper to temporarily disable player movement while dialogue is active.

### Editor Widgets

#### Members
- `DrawStdStringInput(...)`
- `DrawObjectRefInput(...)`
- `DrawAudioClipInput(...)`
- `DrawObjectRefListEditor(...)`
- `IsAudioClipPath(...)`

#### Why They Exist
Advanced reference-heavy inspectors need more than primitive text boxes.

These helpers exist so custom editors can reuse consistent object-ref and clip-path editing behavior rather than rebuilding it in every sample system.

### What This Module Looks Like In Real Scripts
The repository’s larger scripts show a common pattern:

1. authored refs and settings are stored as strings or structured arrays
2. experimental helpers serialize, parse, or resolve them
3. runtime helpers act on the resulting objects or UI targets

That is the real teaching model for this module.

### Common Mistakes
- Forgetting the import and assuming these helpers come from `ModuCPP`.
- Using free-text strings for object refs without `[ObjectRef]` or `[ObjectList]`.
- Reimplementing object-group toggling or UI-text resolution by hand.
- Treating shared sample helpers such as `DialoguePort` as if they were part of the core module surface.

### Related APIs
- [SceneObj](type-sceneobj.md)
- [DialoguePort Namespace](namespace-dialogueport.md)
- [Common Patterns](../manual/common-patterns.md)
- [Experimental Features](../manual/experimental-features.md)


---

<!-- Source: docs/moducpp/api/type-standalone-movement.md -->

## Standalone Movement API

### Summary
The standalone movement API is the built-in movement-controller helper exposed through `ScriptContext`. It is intended for scripts that want a reusable grounded movement solution without hand-writing the full controller logic from scratch.

It is one of the clearest examples of `ScriptContext` exposing a larger engine-side helper rather than just a single small utility function.

### Syntax
```cpp
ctx.BindStandaloneMovementSettings(settings);
ctx.DrawStandaloneMovementInspector(settings, &showDebug);
ctx.TickStandaloneMovement(state, settings, dt, &debug);
```

### Description
This API exists because grounded player movement is a large enough problem that it benefits from shared runtime support:

- movement tuning
- look tuning
- capsule and gravity tuning
- friction and slope handling
- debug output

The shipped `StandaloneMovementController.moducpp` script demonstrates the intended pattern:

- persisted movement settings
- runtime-only movement state
- optional debug readback

### Members

#### Types
- `ScriptContext::StandaloneMovementSettings`
- `ScriptContext::StandaloneMovementState`
- `ScriptContext::StandaloneMovementDebug`

#### Methods
- `BindStandaloneMovementSettings(settings)`
- `DrawStandaloneMovementInspector(settings, showDebug)`
- `TickStandaloneMovement(state, settings, deltaTime, debug)`

#### `StandaloneMovementSettings` fields
- `moveTuning`
- `lookTuning`
- `capsuleTuning`
- `gravityTuning`
- `locomotionTuning`
- `surfaceTuning`
- `enableMouseLook`
- `requireMouseButton`
- `enforceCollider`
- `enforceRigidbody`

#### `StandaloneMovementState` fields
- `pitch`
- `yaw`
- `verticalVelocity`
- `localVelocity`
- `slideVelocity`
- `lastGroundHitPos`
- `hasGroundSample`

#### `StandaloneMovementDebug` fields
- `velocity`
- `localVelocity`
- `platformVelocity`
- `surfaceFriction`
- `slopeDegrees`
- `grounded`

### Behavior Explanation
The API is designed around a useful separation of responsibilities.

#### Settings
Settings are the authored tuning values. They answer questions such as:

- how fast should the character move?
- how sensitive should look input feel?
- how strong should gravity or grounding behavior be?

#### State
State is the runtime memory of the controller. It tracks what the controller is currently doing and what it needs to remember between frames.

#### Debug
Debug data exposes the controller’s current interpretation of motion and ground state. This is valuable when tuning movement, especially in a larger project where “it feels wrong” needs more specific visibility.

#### Tick
`TickStandaloneMovement(...)` is where the controller actually advances. The other methods exist to make the controller authorable and inspectable.

### Multiple Examples
#### Draw the built-in inspector
```cpp
void Script_OnInspector()
{
    auto& config = Config<StandaloneMovementControllerConfig>();
    bindConfig(ctx, config);
    ctx.DrawStandaloneMovementInspector(config.settings, &config.showDebug);
}
```

This is the normal way to expose the controller tuning without manually rebuilding all of its UI.

#### Advance runtime state
```cpp
void TickUpdate()
{
    auto& config = Config<StandaloneMovementControllerConfig>();
    auto& state = State<StandaloneMovementControllerState>();
    ctx.TickStandaloneMovement(state.movement, config.settings, dt, &state.debug);
}
```

This is the runtime heart of the controller pattern.

#### One-time setup
```cpp
void Begin()
{
    if (config.settings.enforceCollider) {
        ctx.EnsureCapsuleCollider(config.settings.capsuleTuning.x, config.settings.capsuleTuning.y);
    }
    if (config.settings.enforceRigidbody) {
        ctx.EnsureRigidbody(true, false);
    }
}
```

This is where the controller script ensures the object setup matches the expected runtime model.

### Remarks
- The standalone movement API is larger than a typical helper function because it is solving a larger gameplay problem.
- The intended usage pattern is strongly aligned with `Config<T>()` for settings and `State<T>()` for runtime state.
- This API is especially useful when a project wants a reusable baseline controller rather than many one-off movement scripts.

### Related APIs
- [ScriptContext](type-scriptcontext.md)
- [Module: ModuEngine](module-moduengine.md)
- [Methods and Lifecycle](../manual/methods-and-lifecycle.md)


---

<!-- Source: docs/moducpp/api/namespace-dialogueport.md -->

## DialoguePort Namespace

### Summary
`DialoguePort` is the shipped shared helper namespace defined in `Scripts/DialoguePortShared.h`.

It is not minimal core language surface. It is a reusable script-side support layer for the repository’s advanced dialogue, interaction, and menu samples.

It exists because those systems repeatedly need the same things:

- structured dialogue data
- settings serialization helpers
- shared target-resolution logic
- shared editor widgets
- shared UI-effect helpers

### Syntax
```cpp
#include "DialoguePortShared.h"
using namespace DialoguePort;
```

### Why This Namespace Exists
The repository’s larger sample systems are related:

- `DialogueSystem.moducpp`
- `InteractableObject.moducpp`
- `MainMenuController.moducpp`

Without a shared helper namespace, they would duplicate the same support logic over and over:

- localized line data
- line serialization
- interaction request keys
- dialogue-system search
- text-effect helpers

`DialoguePort` packages that reusable layer in one place.

### Enums

#### `Language`
Represents which localized sentence field should be used.

Repository use:

- `DialogueSystem.moducpp` exposes `currentLanguage`

Why it exists:

- dialogue content often needs multiple language fields without the runtime script reimplementing the lookup logic

#### `TextEffectType`
Represents text-effect flags such as wave, shake, bounce, rotate, and fade.

Why it exists:

- dialogue text may need authored presentation effects, not only raw string output

### Constants

#### Interaction Request Keys
- `kInteractionRequestPending`
- `kInteractionOverrideLines`
- `kInteractionEndEnable`
- `kInteractionEndDisable`
- `kInteractionPlayerRef`
- `kInteractionRequestSerial`

#### Why They Exist
These keys form the shared settings-based protocol between `InteractableObject.moducpp` and `DialogueSystem.moducpp`.

That is an important repository pattern: one script writes a request into another script’s settings, and the receiving script consumes it next frame.

### Types

#### `DialogueLine`
This is the central authored data type for dialogue content.

It includes:

- character name
- localized sentence fields
- per-line typing sound override
- typing speed
- text effect and tuning
- mouth-object refs
- line-level enable and disable lists

#### Why It Exists
A real dialogue line is more than plain text. It is authored presentation and state-transition data.

That is why `DialogueLine` deserves a dedicated type instead of being flattened into loose strings and parallel arrays.

#### `DialogueScriptTarget`
Represents the result of finding a dialogue system script target in an object hierarchy.

Why it exists:

- interaction scripts need to locate the actual target script and object cleanly

### Serialization And Text Helpers

#### Members
- `SerializeDialogueLines(...)`
- `DeserializeDialogueLines(...)`
- `GetSentenceForLanguage(...)`
- `ParseSentenceForDisplay(...)`
- `LanguageLabel(...)`

#### `SerializeDialogueLines(...)` / `DeserializeDialogueLines(...)`

##### What They Do
Convert dialogue-line arrays to and from a storable settings representation.

##### Why They Exist
The repository’s interaction flow sometimes overrides dialogue data through settings instead of only through public fields. That requires reliable serialization.

##### Repository Use
`InteractableObject.moducpp` serializes override dialogue lines when pushing an interaction request to the dialogue system.

#### `GetSentenceForLanguage(...)`

##### What It Does
Selects the most appropriate sentence field for the requested language.

##### Why A Script Author Chooses It
Because a dialogue runtime should focus on flow and timing, not language-field branching every time it starts a line.

#### `ParseSentenceForDisplay(...)`

##### What It Does
Strips authoring tags or markup-like payloads down to the displayable sentence.

##### Why It Exists
Dialogue data may contain extra authoring syntax that should not appear in the final on-screen string.

##### Repository Use
`DialogueSystem.moducpp` uses it before revealing text character by character.

### Search And Target Helpers

#### Members
- `IsDialogueSystemScript(...)`
- `FindDialogueSystemScriptOnObject(...)`
- `FindDialogueScriptTarget(...)`

#### Why They Exist
Interaction scripts often do not know whether the dialogue system is:

- on the exact referenced object
- on a child
- on a parent

The search helpers solve that problem once so the sample scripts do not have to reimplement hierarchy traversal every time.

#### Repository Use
`InteractableObject.moducpp` uses `FindDialogueScriptTarget(...)` before writing interaction request settings.

### UI And Runtime Editor Helpers

#### Members
- `DrawLanguageCombo(...)`
- `DrawTextEffectFlagsEditor(...)`
- `DrawDialogueLineToolbar(...)`
- `DrawDialogueLineList(...)`
- `DrawDialogueLineEditor(...)`
- `DrawDialogueRuntimeStatus(...)`

#### Why They Exist
Dialogue-heavy inspectors need richer editors than a flat list of primitive fields.

These helpers keep:

- language selection
- effect-flag editing
- line list editing
- runtime status views

consistent across related scripts.

### A Real Repository Pattern: Interaction Requests Into Dialogue
One of the best reasons to understand `DialoguePort` is the way it helps two independent scripts communicate.

The pattern is:

1. `InteractableObject.moducpp` resolves a dialogue-system target.
2. It serializes override dialogue lines and end-state object lists.
3. It writes those values into the dialogue script’s settings.
4. `DialogueSystem.moducpp` detects the request and opens the dialogue with those overrides.

That pattern is only readable because `DialoguePort` centralizes the shared constants, data types, and serialization logic.

### Common Mistakes
- Treating `DialoguePort` as universal core API instead of shipped shared helper code.
- Reimplementing dialogue-line serialization or target search manually.
- Forgetting that it depends on the underlying `ModuCPP.Experimental` helper layer.

### Related APIs
- [Module: ModuCPP.Experimental](module-moducpp-experimental.md)
- [Shipped Example Types](type-shipped-example-types.md)
- [Editor Scripting](../manual/editor-scripting.md)
- [Common Patterns](../manual/common-patterns.md)


---

<!-- Source: docs/moducpp/api/type-shipped-example-types.md -->

## Shipped Example Types

### Summary
This page documents the script-defined enums and `SubScript` data blocks that ship with the example ModuCPP scripts in this repository.

These types are not universal built-in engine primitives. They matter anyway, because they show how real gameplay scripts are expected to model state, inspector data, and configuration in practice. A good scripting manual should not stop at the smallest built-in core surface. It should also explain the patterns the shipped scripts actually use when they grow beyond trivial examples.

### Syntax
```cpp
public enum InteractableType { Dialogue, ToggleObjects }

SubScript InteractionOption {
    public string optionName;
    public InteractableType interactionType;
    public SceneObj[] itemsToEnable;
    public SceneObj[] itemsToDisable;
};
```

```cpp
public enum FacingDirection { Down, Up, Right, Left }
```

### Description
Large scripts need a vocabulary of their own.

Once a script does more than flip a single object on or off, raw booleans and loosely related arrays stop being enough. You need names for modes, stable containers for repeated data, and readable ways to express gameplay intent. That is the role these types serve in the shipped examples.

There are two important ideas on this page:

`enum` types solve the problem of meaning. Instead of asking readers to remember that `0` means dialogue and `1` means toggle mode, the script says `InteractableType.Dialogue` or `InteractableType.ToggleObjects`.

`SubScript` types solve the problem of structured repeated data. Instead of maintaining several parallel arrays that must always stay in sync, the script groups related fields into one nested inspector-editable block.

Those are not just style preferences. They change how easy the script is to configure, debug, and extend.

### Members
#### `FacingDirection`
Declared in `TopDownMovement2D.moducpp`.

Values:

- `Down`
- `Up`
- `Right`
- `Left`

This enum represents the last meaningful direction a top-down character is facing. It exists because movement input and facing state are related, but they are not the same thing.

For example, when a player stops moving, the current movement vector becomes zero. That does not mean the character has no facing direction. Animation, interaction prompts, and directional attacks often still need to know whether the player was last facing up, down, left, or right.

Use `FacingDirection` when:

- animation should keep an idle pose consistent with the last movement direction
- interactions should happen “in front of” the character
- directional sprites or indicators must remain stable when input stops

#### `MenuOrientation`
Declared in `MainMenuController.moducpp`.

Values:

- `Vertical`
- `Horizontal`

This enum describes how a menu is navigated. It exists because menu layout is design data, not an implementation accident.

When a menu is authored as vertical, navigation logic should read up/down input. When it is authored as horizontal, navigation logic should read left/right input. Encoding that choice as a named enum makes the script easier to configure and easier to understand during review.

Use `MenuOrientation` when:

- the same menu controller should work for multiple layouts
- the inspector should make orientation obvious to a designer
- navigation rules should branch by intent rather than by arbitrary integer flags

#### `MenuAction`
Declared as a `SubScript` in `MainMenuController.moducpp`.

Fields:

- `enable`
- `disable`

`MenuAction` groups the scene objects that should be enabled or disabled when a menu item is activated. It exists because a menu action is larger than a single boolean. One selection may need to show one group of objects while hiding another.

Without a nested data block, a script often drifts toward several parallel arrays such as `enableTargets[]`, `disableTargets[]`, and `buttonNames[]`. That structure is fragile because every array must always be edited in lockstep. `MenuAction` avoids that by keeping one action's data together.

Use `MenuAction` when:

- each menu entry carries its own object-toggle payload
- the inspector should expose grouped action data clearly
- the script must stay readable as menu content grows

#### `InteractableType`
Declared in `InteractableObject.moducpp`.

Values:

- `Dialogue`
- `ToggleObjects`

`InteractableType` describes what kind of interaction a configured option performs. It solves a very common gameplay problem: one interaction system often needs to support more than one authored outcome.

For example, an interactable object might sometimes start a dialogue sequence and sometimes toggle scene objects on or off. Both are “interactions”, but they are not handled the same way. The enum makes that difference explicit.

Use `InteractableType` when:

- one interactable script must support several distinct outcomes
- the inspector should communicate interaction intent clearly
- the runtime should branch by named behavior instead of hard-coded numeric assumptions

#### `InteractionOption`
Declared as a `SubScript` in `InteractableObject.moducpp`.

Fields:

- `optionName`
- `interactionType`
- `dialogueSystemRef`
- `dialogueLines`
- `dialogueItemsToEnableOnEnd`
- `dialogueItemsToDisableOnEnd`
- `itemsToEnable`
- `itemsToDisable`

`InteractionOption` is the strongest example on this page of why `SubScript` is useful. An interactable object may expose several possible actions, and each action needs a substantial payload of related data. Some options configure dialogue. Others configure object toggles. All of them still belong to one list of authored interaction choices.

This type keeps each choice self-contained. That makes the inspector easier to reason about and the runtime logic easier to branch on cleanly.

Use `InteractionOption` when:

- a single object exposes multiple interaction choices
- each choice needs several related fields
- you want one array of coherent data records rather than multiple loosely coupled arrays

#### `MouthState`
Declared in `DialogueSystem.moducpp`.

Values:

- `TalkingOpen`
- `TalkingClosed`
- `NotTalking`

`MouthState` models dialogue presentation state for a speaking portrait or object. It exists because dialogue display often changes over time instead of switching instantly between only “playing” and “stopped”.

Use `MouthState` when:

- a dialogue system swaps visual mouth states while text is being presented
- the script needs clear names for temporary presentation states
- animation/presentation logic should be separated from raw string display rules

### Behavior Explanation
The most important takeaway from these example types is not that a handful of enums exist. The important takeaway is how they shape runtime behavior and authoring behavior together.

#### Enums make branching readable
When a script branches on `InteractableType` or `MenuOrientation`, the branch reads like gameplay intent. A reader can see why the branch exists without first tracing magic numbers or comments.

That matters even more over time. As scripts grow, readable branching logic is one of the easiest ways to keep features maintainable.

#### `SubScript` keeps repeated data coherent
Repeated game data almost always starts simple and then grows. One menu action needs two object lists. One dialogue option needs lines, targets, and follow-up objects to toggle. Once that happens, parallel arrays become a liability.

`SubScript` avoids that problem by storing the related fields together as one reusable nested shape. The result is easier to serialize, easier to expose to the inspector, and much less error-prone to maintain.

#### Example-defined types are still worth documenting
Even though these types are not universal engine built-ins, they represent real supported scripting patterns. They show how the current language surface is meant to be used in larger scripts.

That is why they belong in the documentation. They teach script authors how to design their own enums and nested data structures instead of flattening everything into primitive fields.

### Multiple Examples
#### Example 1: Remembering facing direction for idle animation
```cpp
add ModuCPP;
add ModuInput;

public enum FacingDirection { Down, Up, Right, Left }

public class TopDownFacing : ModuNode
{
    private FacingDirection facing = FacingDirection.Down;

    void Update()
    {
        if (input.Up()) facing = FacingDirection.Up;
        else if (input.Down()) facing = FacingDirection.Down;
        else if (input.Left()) facing = FacingDirection.Left;
        else if (input.Right()) facing = FacingDirection.Right;

        if (!input.Up() && !input.Down() && !input.Left() && !input.Right())
        {
            ctx.SetTagText("Facing", facing.ToString());
        }
    }
}
```

This pattern separates movement intent from persistent facing state. The character can stop moving while still keeping a stable idle direction.

#### Example 2: Using `InteractableType` to branch between dialogue and object toggles
```cpp
add ModuCPP;

public enum InteractableType { Dialogue, ToggleObjects }

SubScript InteractionOption {
    public string optionName;
    public InteractableType interactionType;
    public SceneObj[] itemsToEnable;
    public SceneObj[] itemsToDisable;
};

public class SimpleInteractable : ModuNode
{
    public InteractionOption[] options;

    void TriggerOption(int index)
    {
        if (index < 0 || index >= options.Count())
        {
            return;
        }

        InteractionOption selected = options[index];

        if (selected.interactionType == InteractableType.ToggleObjects)
        {
            for (SceneObj item : selected.itemsToEnable)
            {
                item.SetEnabled(true);
            }

            for (SceneObj item : selected.itemsToDisable)
            {
                item.SetEnabled(false);
            }
        }
    }
}
```

This pattern makes the interaction branch self-explanatory. The script can later grow new interaction modes without collapsing into unreadable boolean combinations.

#### Example 3: Grouping menu side effects with `MenuAction`
```cpp
add ModuCPP;

SubScript MenuAction {
    public SceneObj[] enable;
    public SceneObj[] disable;
};

public class MenuStateSwitcher : ModuNode
{
    public MenuAction playAction;
    public MenuAction optionsAction;

    void ApplyAction(MenuAction action)
    {
        for (SceneObj item : action.enable)
        {
            item.SetEnabled(true);
        }

        for (SceneObj item : action.disable)
        {
            item.SetEnabled(false);
        }
    }
}
```

This keeps each authored action coherent. The alternative would usually be a harder-to-maintain collection of unrelated arrays.

#### Example 4: Using `MouthState` to drive simple dialogue visuals
```cpp
add ModuCPP;

public enum MouthState { TalkingOpen, TalkingClosed, NotTalking }

public class DialoguePortrait : ModuNode
{
    private MouthState mouth = MouthState.NotTalking;
    private float switchTimer = 0.0f;

    void Update()
    {
        if (ctx.GetBool("DialogueActive"))
        {
            switchTimer += ctx.GetDeltaTime();

            if (switchTimer > 0.12f)
            {
                mouth = mouth == MouthState.TalkingOpen ? MouthState.TalkingClosed : MouthState.TalkingOpen;
                switchTimer = 0.0f;
            }
        }
        else
        {
            mouth = MouthState.NotTalking;
        }
    }
}
```

This pattern gives the presentation layer clear states instead of leaving the behavior implicit in several booleans.

### Remarks
- These types are real script-facing examples from shipped scripts, but they are not guaranteed global engine primitives.
- They are best read as reference patterns for authoring your own game-specific enums and nested data blocks.
- `InteractableType` is particularly useful as a model for branching by named gameplay intent instead of numeric modes.
- `SubScript` examples such as `InteractionOption` and `MenuAction` show how to expose repeated structured data to the inspector without relying on parallel arrays.
- When your own script starts accumulating several related fields for one repeated concept, that is usually a sign to introduce a `SubScript`.

### Related APIs
- [SceneObj](type-sceneobj.md)
- [DialoguePort Namespace](namespace-dialogueport.md)
- [Fields and Inspector](../manual/fields-and-inspector.md)
- [Common Patterns](../manual/common-patterns.md)

