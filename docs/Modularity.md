# Modularity Engine Documentation

This document explains how the Modularity C++ engine is structured, how projects and scenes work, and how to use the editor/runtime to build content. It is written to match the current codebase.

## 1) Overview
Modularity is a native C++ engine with an integrated editor. The core is built around:
- A scene graph made of `SceneObject` instances with component-style flags/data.
- An OpenGL renderer with post-processing and UI rendering.
- A scripting system that hot-compiles C++ into shared libraries and optionally hosts managed C# via Mono.
- Optional PhysX-based 3D physics, plus a lightweight built-in 2D simulation.
- Audio via miniaudio with spatial playback and reverb zones.

## 2) Build and Run
### Primary build scripts
Use the provided scripts for a full editor + player build:
- `build.sh` (Linux/macOS) and `build.bat` (Windows).
  - Initializes git submodules.
  - Builds the editor in `build/`.
  - Builds a player-only target in `build/player-cache/`.
  - Copies built libraries into `Packages/Engine` and `Packages/ThirdParty` inside each build folder.

### CMake options
These options are defined in `CMakeLists.txt`:
- `MODULARITY_BUILD_EDITOR` (default ON): build the editor target.
- `MODULARITY_ENABLE_PHYSX` (default ON): enable PhysX integration.
- `MODULARITY_USE_MONO` (default ON): enable Mono embedding for managed scripts.
- `MONO_ROOT`: explicit Mono runtime path if not using the bundled one.

### Entry points
- Editor: `src/main.cpp`
- Player: `src/main_player.cpp`

Both set the working directory to the executable’s location before engine init.

## 3) Project Layout
New projects are created with a consistent directory structure (see `Project::create`).

```
YourProject/
  Assets/
    Scenes/
    Scripts/
      Runtime/
      Editor/
    Models/
    Shaders/
    Materials/
  Library/
    CompiledScripts/
    InstalledPackages/
    ScriptTemp/
    Temp/
  ProjectUserSettings/
    ProjectLayout/
    ScriptSettings/
  project.modu
  scripts.modu
  packages.modu
```

Key files:
- `project.modu`: project name + last opened scene.
- `scripts.modu`: script build configuration.
- `packages.modu`: script dependency manifest.
- Scenes live in `Assets/Scenes` with extension `.scene`.

## 4) Scene Format and Serialization
Scenes are stored in a text format handled by `SceneSerializer`:
- Header keys include `version`, `nextId`, `timeOfDay`, and `objectCount`.
- Each object is written as an `[Object]` block with key/value pairs.
- Transforms are stored as **local** position/rotation/scale relative to the parent.

This text format is intentionally human-readable for diffing and manual edits.

## 5) Scene Objects and Components
Every object is a `SceneObject` with a core transform and a set of component flags/data. The engine uses flags such as `hasRenderer`, `hasLight`, `hasUI`, etc., to decide which systems apply.

### Object types (high level)
Examples of built-in types:
- 3D primitives: `Cube`, `Sphere`, `Capsule`, `Plane`, `Torus`
- Imported meshes: `OBJMesh`, `Model` (Assimp-based)
- Lights: `DirectionalLight`, `PointLight`, `SpotLight`, `AreaLight`
- Cameras: `Camera`
- PostFX nodes: `PostFXNode`
- Sprites: `Sprite` (3D), `Sprite2D` (screen space)
- UI: `Canvas`, `UIImage`, `UISlider`, `UIButton`, `UIText`
- Misc: `Mirror`, `Empty`

### Core components
- **Transform**: position/rotation/scale, plus local transform and parent/child relationships.
- **Renderer**: mesh type + material + textures + shader paths.
- **Light**: light type, color, intensity, range, and light-specific parameters.
- **Camera**: FOV, near/far, 2D settings, post-FX toggle.
- **PostFX**: global effects settings (bloom, color adjust, motion blur, vignette, chromatic aberration, AO).
- **Scripts**: one or more script components (C++ or managed C#).

### Physics components
3D (PhysX, optional):
- `Rigidbody` (mass, damping, gravity, kinematic, lock rotation)
- `Collider` (box, mesh, convex mesh, capsule)

2D (built-in):
- `Rigidbody2D` (velocity, gravity)
- `Collider2D` (box, polygon, edge)
- `ParallaxLayer2D`
- `CameraFollow2D`

### Audio components
- `AudioSource` (clip path, volume, spatial, rolloff)
- `ReverbZone` (shape, blend distance, preset/custom parameters)

### Animation components
- `Animation` (keyframe clip, interpolation)
- `SkeletalAnimation` (GPU skinning, clip index, bone data)

### UI components
The UI component represents Canvas + UI elements:
- Anchoring, positioning in pixels, size, color, text, and interaction state.
- UI elements can optionally render in 3D to a texture target.

## 6) Rendering System
The renderer is OpenGL-based and includes:
- GPU meshes for primitives, imported models, and raw mesh assets.
- Texture and shader caching.
- Skybox rendering.
- Render targets for game view and preview.
- Optional post-processing pipeline.

Default shader paths are defined in `Renderer`:
- `Resources/Shaders/vert.glsl`
- `Resources/Shaders/frag.glsl`
- `Resources/Shaders/skinned_vert.glsl`
- Post-FX shaders under `Resources/Shaders/`

### Post-processing features
- Bloom
- Exposure/contrast/saturation/color filter
- Motion blur (history buffer)
- Vignette
- Chromatic aberration
- Ambient occlusion (screen-space)

Post-FX can be enabled per camera and configured in `PostFXSettings`.

## 7) Materials and Textures
Each renderable object has:
- `MaterialProperties` (color, ambient/specular strength, shininess, texture mix)
- Optional `materialPath` for external material assets
- Albedo/overlay/normal map textures
- Per-object vertex/fragment shader overrides

## 8) Models and Meshes
Mesh sources:
- **OBJ** via `OBJLoader` (includes triangle data for picking and physics).
- **Assimp** via `ModelLoader` for formats like FBX/GLTF, with node hierarchies and animation data.

Raw meshes:
- Editable mesh assets are stored as `.rmesh` and can be loaded/saved via `ModelLoader`.

## 9) Scripting
### Native scripting (C++ and C)
Native scripts are compiled to shared libraries and hot-loaded at runtime. A project-specific `scripts.modu` controls build settings and include paths.

Key concepts:
- C++ hooks like `Begin`, `TickUpdate`, and `Update`.
- C hooks via the C API bridge (`Modu_*` hook names in `.c` scripts).
- Auto-generated wrappers export `Script_Begin`, `Script_TickUpdate`, etc., when hook names are detected.

For full details, see:
- `docs/Scripting.md`

### Managed C# scripting (experimental)
Modularity can host managed scripts using Mono, with a minimal API surface.

For setup and caveats, see:
- `docs/Scripting.md`
- `docs/mono-embedding.md`

## 10) Physics
### 3D (PhysX)
PhysX integration is optional and controlled by `MODULARITY_ENABLE_PHYSX`. The system:
- Creates rigid bodies/colliders on play start.
- Steps simulation during play/spec modes.
- Supports raycasts and force/impulse APIs.

### 2D
The engine includes a simple 2D simulation for UI and 2D elements, with:
- Rigidbody2D movement and damping
- 2D colliders
- Camera follow and parallax layers

## 11) Audio
Audio is handled by `AudioSystem` (miniaudio):
- Spatialized sources with rolloff settings.
- Play/stop/loop controls.
- Preview playback from the editor.
- Reverb zones with blending based on listener position.

## 12) Animation
Two animation paths exist:
- **Keyframe animation** on transform (position/rotation/scale).
- **Skeletal animation** for imported skinned meshes, with optional GPU skinning.

## 13) Editor and Workflow
### Core editor areas
The editor uses Dear ImGui + ImGuizmo. Typical workflow:
- **Launcher**: create or open a project.
- **Hierarchy**: scene object list, parent/child relationships.
- **Inspector**: edit components and run script inspectors.
- **File Browser**: browse assets and run context actions (compile scripts, import models, etc.).
- **Viewport**: scene editing with gizmos.

### Play, Spec, Test
The engine differentiates between edit mode and runtime simulation:
- Scripts run only in Play/Spec/Test to avoid edit-time side effects.
- PhysX simulation runs when playing or in spec mode.
  - 2D simulation can run in play/spec/test.

### Script compilation
In the editor, scripts can be compiled from:
- File browser context menu
- Script component menu in the inspector

## 14) Packages (Script Dependencies)
The package manager is designed for script build dependencies:
- Registry-based with built-in and optional external packages.
- Installed packages contribute include directories, defines, and link libs.
- Per-project manifest is stored in `packages.modu`.

## 15) Engine Loop (High Level)
From `Engine::run`, the core flow is:
1. Poll events and check for project/scene loads.
2. Update viewport focus and camera controls.
3. Update scripts (Play/Spec/Test only).
4. Update player controller + 2D physics + camera follow.
5. Update animations and world transforms.
6. Step PhysX if enabled and active.
7. Render editor and game views.

This loop keeps edit-time changes isolated from runtime simulation.

## 16) Extending the Engine
Common extension points:
- Add new components in `SceneObject.h` + serialization in `ProjectManager.cpp`.
- Add new rendering features in `Rendering.*`.
- Add new script APIs in `ScriptRuntime.*` and managed bindings in `ManagedBindings.*`.
- Add custom editor windows via script exports.

## 17) References in This Repo
- `docs/Scripting.md` — full scripting guide and API details.
- `docs/mono-embedding.md` — Mono embedding requirements.
- `src/Engine.*` — main loop and editor control.
- `src/SceneObject.h` — component definitions.
- `src/Rendering.*` — renderer and post-processing.
- `src/PhysicsSystem.*` — PhysX integration.
- `src/AudioSystem.*` — audio playback and reverb.
- `src/ProjectManager.*` — project + scene serialization.
