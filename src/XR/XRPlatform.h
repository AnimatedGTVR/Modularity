#pragma once

// The single place that decides which OpenXR platform/graphics macros are on.
//
// <openxr/openxr_platform.h> only declares a platform's types when the matching
// XR_USE_* macro is defined *before* it is included. Get that wrong and the
// Android structs (XrLoaderInitInfoAndroidKHR, XrInstanceCreateInfoAndroidKHR,
// XrGraphicsBindingOpenGLESAndroidKHR) silently do not exist, so any
// `#ifdef XR_KHR_loader_init_android` block compiles away to nothing and the
// Android bring-up path is quietly skipped rather than failing loudly. Every
// file in src/XR/ includes this instead of the OpenXR headers directly, so that
// decision is made exactly once.
//
// Deliberately NOT defined here: XR_USE_GRAPHICS_API_VULKAN. Modularity does not
// implement a Vulkan XR binding, and leaving the macro off means the Vulkan XR
// types are not even declared - it is impossible to accidentally start depending
// on them from this side.

#if MODULARITY_HAS_OPENXR

// The OpenGL ES graphics macro follows the *engine's* GLES switch, not
// __ANDROID__. Android always builds GLES so Quest is unaffected, but Modularity
// also supports a desktop GLES build (MODULARITY_USE_OPENGL_ES), and letting the
// swapchain code compile there is the only way any of this path gets compiler
// coverage on a machine with no Android NDK. A desktop GL build defines neither
// macro and therefore gains no EGL dependency - which is what keeps the Windows
// cross-build, where no EGL headers exist, building exactly as before.
#if MODULARITY_OPENGL_ES
    #define XR_USE_GRAPHICS_API_OPENGL_ES
    #include <EGL/egl.h>
#endif

// The Android platform extensions stay strictly Android-only: they are what
// declares XrGraphicsBindingOpenGLESAndroidKHR, XrLoaderInitInfoAndroidKHR and
// XrInstanceCreateInfoAndroidKHR, all of which carry JNI types.
#if defined(__ANDROID__)
    #define XR_USE_PLATFORM_ANDROID
    #include <jni.h>
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

// True when the OpenGL ES swapchain/framebuffer types are available, so the
// swapchain and frame-loop code can be compiled.
#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
    #define MODULARITY_XR_HAS_GLES_TYPES 1
#else
    #define MODULARITY_XR_HAS_GLES_TYPES 0
#endif

// True only when a session can actually be bound to a graphics device, which
// additionally needs the platform's binding struct. Everything else in src/XR/
// (loader discovery, instance creation, diagnostics) works without it - that is
// what lets the editor probe a runtime on a desktop with no implemented binding.
#if defined(XR_USE_GRAPHICS_API_OPENGL_ES) && defined(XR_USE_PLATFORM_ANDROID)
    #define MODULARITY_XR_HAS_GRAPHICS_BINDING 1
#else
    #define MODULARITY_XR_HAS_GRAPHICS_BINDING 0
#endif

#endif // MODULARITY_HAS_OPENXR
