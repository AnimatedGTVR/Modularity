// Anchor translation unit for the Android editor shared library (libModularity.so).
//
// The actual entry point (android_main / ANativeActivity_onCreate) and the whole
// editor live in the `core` static lib, which this .so links with --whole-archive
// so those symbols survive --gc-sections. CMake needs at least one source file to
// create the SHARED target, so this is it. Keep it empty on purpose.
//
// This is the "Modularity editor on Android" target: same Engine + NativeActivity
// bring-up as the player, but built from `core` (MODULARITY_RUNTIME_ONLY=0) so the
// full editor UI renders. See CMakeLists.txt (the ANDROID editor block).

#ifdef __ANDROID__
namespace {
// A defined symbol so the TU isn't completely empty on every toolchain.
[[maybe_unused]] int modularity_android_editor_entry_anchor = 0;
}
#endif
