<INSTRUCTIONS>
Modularity repository rules:
- Use . in ModuCPP script-facing member access, never ->
- Preserve runtime behavior during refactors
- Prefer declarative inspector fields and AutoFields(...) where supported
- Move reusable helpers into shared headers instead of duplicating them in scripts
- Keep code simple, readable, and conventional
- Do not introduce unnecessary abstraction layers
- Return full updated methods or full updated files when modifying code

Working style:
- Focus on advanced coding tasks, architecture, debugging, and performance
- Be concise by default
- Prefer proven engineering patterns unless a modern alternative is clearly better
- When a task cannot be completed as requested, say what could not be done and provide the closest useful alternative

Codebase navigation:
- Prefer repository file search before external search for engine/code questions
- Read existing nearby files before inventing new patterns
- Match surrounding code style, naming, and file structure
- Reuse existing engine systems before adding new ones
- Prefer extending current classes, helpers, or editor panels instead of introducing parallel systems
- Check related implementation files before changing shared behavior
- For bugs, inspect the current implementation and nearby call sites first before proposing a rewrite

Engine stack and architecture:
- Modularity is a custom C++ engine
- Rendering uses OpenGL
- Windowing and input use GLFW
- Math uses GLM
- Editor UI uses ImGui
- Transform/editor gizmos use ImGuizmo
- Audio uses miniaudio.h
- Prefer integrating with these existing libraries instead of introducing replacements
- Do not replace existing engine subsystems just to solve a local problem
- Respect the current renderer, editor, scene, scripting, serialization, and asset pipelines
- Do not invent a new rendering path, UI framework, math layer, or audio backend unless explicitly requested

Rendering rules:
- Prefer existing renderer abstractions, texture handling, and material flow over raw one-off OpenGL usage
- If raw OpenGL is required, keep it localized and compatible with the current renderer
- Reuse GPU resources where possible instead of reallocating every frame
- Avoid per-frame heap allocations in rendering code
- Keep editor rendering and runtime rendering behavior separate when needed
- Do not break standalone player rendering while fixing editor functionality

Editor and tooling rules:
- Treat editor code as removable from shipped builds unless proven required
- Prefer compile-time exclusion for editor/dev systems
- Preserve current shipped-game behavior
- Keep Inspector and editor UI code stable and defensive
- When working in ImGui, avoid invalid Begin/End or BeginTable/EndTable state
- Guard editor preview code against null pointers, invalid textures, zero sizes, and unloaded assets
- Prefer minimal editor additions that match the current UI style

Runtime stripping rules:
- Never break standalone player rendering, scripting, or scene loading
- Treat editor code as removable from shipped builds unless proven required
- Prefer compile-time exclusion for editor/dev systems
- Preserve current shipped-game behavior
- When uncertain, mark subsystem as validation-required instead of removing it

Performance and safety:
- Preserve existing runtime behavior unless a change is explicitly required
- Prefer low-overhead solutions and avoid unnecessary allocations
- Avoid blocking work in render paths or hot update loops
- Use thread-safe access patterns when background loading or async work is involved
- Fix root causes instead of hiding crashes with broad workarounds
- Add guards and validation where engine/editor code may touch resources that are not yet initialized

Code modification expectations:
- Prefer updating the existing method or class over rewriting large unrelated areas
- Keep public APIs stable unless a change is necessary
- Do not rename files, classes, methods, or fields without a strong reason
- When changing serialization, scene objects, materials, rendering, or inspector behavior, check for downstream impact
- For bug fixes, preserve existing behavior outside the failing case
- Return the closest production-ready result possible, not pseudocode.

(oh and, if modifying ModuCPP, make sure to bump up the ABI version just in case.)
</INSTRUCTIONS>