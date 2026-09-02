#pragma once

// Re-export the handful of GLFW entry points that compiled scripts reference.
//
// ModuInputScriptApi.h calls glfwGetCurrentContext/glfwGetKey (the fallback path
// in IsRuntimeKeyDown, used when a script runs without a ScriptContext) and
// glfwGetGamepadState (ModuCPP::Gamepad::poll). On Linux those resolve for free:
// dlopen binds a script .so against the host process's dynamic symbol table, and
// the engine already has GLFW linked in. Windows has no equivalent - a DLL must
// import every symbol from a named module - so each script failed to link with
//     error LNK2019: unresolved external symbol glfwGetKey
// and any script touching input or a gamepad was dead on this platform.
//
// The fix is to export them from the executable. Scripts already link against the
// host import library (see ScriptCompiler's findHostImportLibrary), so once these
// names appear in Modularity.lib / ModularityPlayer.lib the existing link line
// resolves them with no per-project configuration.
//
// NOT the alternative of adding glfw3.lib to scripts.modu's win.linkLib: that
// links a second, private copy of GLFW into every script DLL, with its own global
// state. glfwGetCurrentContext() would return null inside the script because the
// window belongs to the engine's copy, so input would read as permanently idle -
// a silent wrong answer, which is worse than the link error it replaces.
//
// GLFW is statically linked into both executables, so /EXPORT finds real symbols
// here. Keep this list in sync with the glfw* calls in include/ModuInputScriptApi.h.

#if defined(_WIN32)
    #pragma comment(linker, "/EXPORT:glfwGetCurrentContext")
    #pragma comment(linker, "/EXPORT:glfwGetKey")
    #pragma comment(linker, "/EXPORT:glfwGetGamepadState")
#endif
