# Android Runtime Plan
## *Note: Written by Anémunt and Claude Code*
-# *(NEEDS UPDATE!)*
---

Status: **structural scaffold only** — no APK/AAB output yet.
OpenGL ES is already routed through [`include/Graphics/OpenGL.h`](../include/Graphics/OpenGL.h)
and `MODULARITY_USE_OPENGL_ES` in [`CMakeLists.txt`](../CMakeLists.txt) selects the
GLES headers/link path. The pieces below are what remains before the editor's
"Bake" button can actually produce an Android binary.

## What's wired up today

- `BuildPlatform::Android` exists, ships in the central
  `kBuildPlatforms` table in [`Engine.h`](../src/Engine.h), and is surfaced
  as **"Android (Experimental)"** in all build-target combos.
- Android-specific architectures (`arm64-v8a`, `armeabi-v7a`) appear in the
  architecture combo when Android is the selected platform; the desktop
  list (`x86_64`, `x86`) is shown otherwise. Switching platform resets the
  architecture to the first valid entry if needed.
- `Engine::resolveAndroidNdkPath()` in
  [`src/AndroidExport.cpp`](../src/AndroidExport.cpp) probes
  `ANDROID_NDK_ROOT`, `ANDROID_NDK_HOME`, and `ANDROID_NDK`, verifying that
  the directory contains an NDK `source.properties` file.
- Clicking **Bake** with Android selected does **not** run the desktop
  CMake pipeline. It either errors out with a useful message
  ("set `ANDROID_NDK_ROOT`...") or, if an NDK is found, warns that the APK
  packaging stage isn't implemented yet and points here. The export job is
  marked done/unsuccessful so the dialog closes cleanly.
- [`src/AndroidRuntime/`](../src/AndroidRuntime) holds the runtime entry
  point scaffolding (`AndroidRuntime.h`, `AndroidRuntime.cpp`). The folder
  is excluded from the desktop CMake source glob and only added back when
  `ANDROID` is true; the file bodies are additionally `#ifdef __ANDROID__`
  guarded so they stay inert even if the filter is bypassed.

## What's still missing

The bring-up needs to land in this rough order. None of it should change
the desktop build's behavior.

### 1. NDK toolchain plumbing in CMake

- Detect `ANDROID_NDK_ROOT` at configure time (only when targeting Android).
- Source the NDK's `android.toolchain.cmake` and forward `ANDROID_ABI`,
  `ANDROID_PLATFORM`, and `ANDROID_STL` (likely `c++_shared`).
- Force `MODULARITY_USE_OPENGL_ES=ON` and disable the GLFW desktop window
  module for Android targets.
- Pull `android_native_app_glue` from
  `${ANDROID_NDK}/sources/android/native_app_glue` into a small static
  library that `core_player` links against.
- Build `core_player` (and only `core_player`) as a shared library
  (`libModularityPlayer.so`) — Android apps load a `.so`, not an
  executable.

### 2. Runtime entry point

`src/AndroidRuntime/AndroidRuntime.cpp` already exposes `android_main` and
hands off to `Modularity::AndroidRuntime::Run(android_app*)`. `Run` needs:

- EGL display/context/surface creation against the `ANativeWindow` from
  `app->window`, then handing the context to the existing renderer setup.
- A lifecycle pump driven by `ALooper_pollAll` and `app->onAppCmd`:
  - `APP_CMD_INIT_WINDOW` -> create EGL surface, resume rendering.
  - `APP_CMD_TERM_WINDOW` -> destroy EGL surface, pause rendering.
  - `APP_CMD_PAUSE` / `APP_CMD_RESUME` -> drive engine pause state.
  - `APP_CMD_DESTROY` -> tear down engine cleanly.
- A touch input adapter that translates `AInputEvent_getSource ==
  AINPUT_SOURCE_TOUCHSCREEN` events into the engine's existing input
  events (treat the first pointer as the primary cursor for now).

### 3. Asset pipeline

`ModuPak` / file IO currently assume a real filesystem. On Android the
project's `Resources/` end up inside the APK's `assets/` directory and must
be read through `AAssetManager`. Plan:

- Add a thin `AssetSource` abstraction (one desktop impl backed by
  `std::filesystem`, one Android impl backed by `AAssetManager`).
- Route `ModuPak` and texture/scene/script loaders through it.
- Keep the desktop path on the existing code; only the Android target
  uses the asset-manager impl.

### 4. Packaging (intentionally last)

Once a runtime `.so` boots, the export pipeline needs to:

- Stage `libModularityPlayer.so` + dependent `.so`s for the chosen ABI
  under `lib/<abi>/`.
- Stage project assets under `assets/`.
- Generate an `AndroidManifest.xml` (NativeActivity-style) and an
  `application/icon.png`.
- Invoke `aapt2` to compile resources, link them, and produce a base APK;
  align with `zipalign`; sign with `apksigner` using a debug keystore by
  default.
- Optional: switch to AAB via `bundletool` once the APK path is solid.

Gradle is intentionally avoided — `aapt2` + `apksigner` directly is
sufficient for a NativeActivity app and keeps the pipeline self-contained.

## Open questions

- Minimum SDK / `ANDROID_PLATFORM` target. 24 covers a comfortable
  fraction of devices and unlocks Vulkan; 21 keeps reach but drops
  features we'd otherwise lean on.
- Whether ABIs other than `arm64-v8a` are worth shipping by default.
- Editor-side keystore management — debug keystore is fine for now, but a
  release flow needs UX for "select keystore + password".
- Whether the player should ever run as an `Activity` (Kotlin/Java
  launcher) instead of a pure `NativeActivity`. Sticking with
  NativeActivity unless we hit a blocker like in-app purchases.
