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
- Project-managed C# sources/build files live under `<ProjectRoot>/Scripts/Managed/`.
- Default managed output is `<ProjectRoot>/Scripts/Managed/bin/Debug/netstandard2.0/ModuCPP.dll`.

Runtime notes:
- Managed script loading uses the Script component `Assembly Path` + `Type` fields.
- If `Assembly Path` is a `.cs` or `.csproj`, the engine resolves to the managed output DLL.
- If `<ProjectRoot>/Scripts/Managed/ModuCPP.csproj` is missing, the editor attempts to bootstrap it on first managed compile.
- Managed scripts require the `ModuCPP.Host` type with `SetNativeApi(IntPtr)` (provided by `Scripts/Managed/ModuCPP.cs`).
- The current native/managed bridge ABI is `version = 7`.
- `Scripts/Managed/ModuCPP.cs` binds newer API blocks conditionally via `Api.Version`, so older native layouts fail soft instead of crashing when newer delegates are absent.
