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

Runtime stripping rules:
- Never break standalone player rendering, scripting, or scene loading
- Treat editor code as removable from shipped builds unless proven required
- Prefer compile-time exclusion for editor/dev systems
- Preserve current shipped-game behavior
- When uncertain, mark subsystem as validation-required instead of removing it
</INSTRUCTIONS>