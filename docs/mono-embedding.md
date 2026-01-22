# Mono Embedding Setup

This project uses Mono embedding for managed (C#) scripts.

Expected layout (vendored):
`src/ThirdParty/mono/`
- `include/mono-2.0/`
- `lib/` (or `lib64/`) with `mono-2.0-sgen` library
- `etc/mono/` (config files)
- `lib/mono/4.5/` (framework assemblies)

You can override the runtime location at runtime with:
`MODU_MONO_ROOT=/path/to/mono`

Build notes:
- The CMake cache variable `MONO_ROOT` controls where headers/libs are found.
- Managed scripts target `netstandard2.0` and are built with `dotnet build`.
