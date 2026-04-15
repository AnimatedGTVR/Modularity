# Changelog

## Unreleased
- ModuCPP now documents `ModuNode` as the preferred gameplay-script base while continuing to accept older `ModuBehaviour` scripts.
- Added documented shorthand for `obj.UILabel`, `ModuEngine.FPS`, `IntRD`/`IntR`/`IntRU`, and `timer.Start(...)` / `timer.Ready()`.
- Updated shipped ModuCPP and template FPS/timer examples to use the shorter script-facing helpers instead of manual FPS math and manual `dt` timer accumulation.
- Corrected managed bridge ABI documentation to match the current runtime value of `version = 7`.
