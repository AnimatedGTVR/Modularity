# Changelog

## Unreleased
- Reorganized ModuCPP documentation into manual pages and API reference pages under `docs/moducpp/`.
- Updated scripting docs to reflect the current module split instead of treating `add ModuCPP;` as a catch-all import.
- Added dedicated reference pages for `ModuNode`, `ModuBehaviour`, `ScriptContext`, module APIs, inspector DSL, lifecycle hooks, standalone movement, and the shipped `DialoguePort` helper namespace.
- ModuCPP now documents `ModuNode` as the preferred gameplay-script base while continuing to accept older `ModuBehaviour` scripts.
- Added documented shorthand for `obj.UILabel`, `ModuEngine.FPS`, `IntRD`/`IntR`/`IntRU`, and `timer.Start(...)` / `timer.Ready()`.
- Updated shipped ModuCPP and template FPS/timer examples to use the shorter script-facing helpers instead of manual FPS math and manual `dt` timer accumulation.
- Corrected managed bridge ABI documentation to match the current runtime value of `version = 7`.
