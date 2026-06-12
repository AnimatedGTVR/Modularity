#ifdef __ANDROID__

// Modularity's native Android runtime — the body that drives the engine
// when it's loaded as a NativeActivity .so. Sets up EGL against the
// ANativeWindow handed in by android_native_app_glue, pumps the Android
// event loop (lifecycle + touch input), and renders frames.
//
// Stage 2 of the Android bring-up (docs/AndroidRuntime.md): the runtime
// scaffolding is in place but the engine's main loop is not yet hooked in.
// Frames currently clear to a solid color (proof-of-life). Stage 2.5 will
// adapt Engine::run() so this loop can hand off to it.

#include "AndroidRuntime.h"
#include "../Engine.h"
#include "../../include/Platform/AssetSource.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/asset_manager.h>
#include <android/input.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <pthread.h>
#include <unistd.h>
#include <cstdio>

// AssetSourceAndroid.cpp owns the AndroidAssetSource type; expose its
// factory through the namespace so AndroidRuntime can install it without
// dragging the impl into a header.
namespace Modularity::Platform {
    std::unique_ptr<AssetSource> MakeAndroidAssetSource(AAssetManager* mgr);
}

#include <cstdint>
#include <cstdlib>
#include <memory>

#define MODU_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "Modularity", __VA_ARGS__)
#define MODU_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "Modularity", __VA_ARGS__)
#define MODU_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Modularity", __VA_ARGS__)

namespace Modularity::AndroidRuntime {

namespace {

// === stdout/stderr → logcat redirect ====================================
// std::cerr / std::cout / fprintf(stderr,...) go to a null sink on Android
// by default. Pipe both into __android_log_print so cerr lines that the
// engine code emits (window init, AssetSource misses, etc.) show up under
// the Modularity.stderr / Modularity.stdout logcat tags.

int   g_stdoutPipe[2] = {-1, -1};
int   g_stderrPipe[2] = {-1, -1};

void* PumpStdToLogcat(void* arg) {
    const int   fd     = static_cast<int>(reinterpret_cast<intptr_t>(arg) >> 1);
    const int   isErr  = static_cast<int>(reinterpret_cast<intptr_t>(arg) & 1);
    const char* tag    = isErr ? "Modularity.stderr" : "Modularity.stdout";
    const int   prio   = isErr ? ANDROID_LOG_WARN  : ANDROID_LOG_INFO;
    char        buf[512];
    ssize_t     n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        // Drop trailing newline so logcat doesn't insert a blank line.
        if (buf[n - 1] == '\n') --n;
        buf[n] = '\0';
        __android_log_print(prio, tag, "%s", buf);
    }
    return nullptr;
}

void RedirectStdioToLogcat() {
    if (g_stdoutPipe[0] >= 0) return; // already installed

    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IOLBF, 0);

    if (pipe(g_stdoutPipe) == 0) {
        dup2(g_stdoutPipe[1], STDOUT_FILENO);
        pthread_t t;
        pthread_create(&t, nullptr, PumpStdToLogcat,
                       reinterpret_cast<void*>(static_cast<intptr_t>(g_stdoutPipe[0]) << 1));
        pthread_detach(t);
    }
    if (pipe(g_stderrPipe) == 0) {
        dup2(g_stderrPipe[1], STDERR_FILENO);
        pthread_t t;
        pthread_create(&t, nullptr, PumpStdToLogcat,
                       reinterpret_cast<void*>((static_cast<intptr_t>(g_stderrPipe[0]) << 1) | 1));
        pthread_detach(t);
    }
}

struct RuntimeContext {
    android_app* app = nullptr;

    // EGL state. Display + context live for the lifetime of the process;
    // surface is torn down on APP_CMD_TERM_WINDOW and rebuilt on
    // APP_CMD_INIT_WINDOW so the runtime survives a backgrounded app.
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig  config  = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;

    int surfaceWidth = 0;
    int surfaceHeight = 0;
    bool paused = false;

    // Single-pointer touch state (primary cursor analog). Stage 2 keeps
    // multi-touch out of scope.
    bool touchActive = false;
    float touchX = 0.0f;
    float touchY = 0.0f;
};

RuntimeContext g_ctx;

const char* EglErrorString(EGLint err) {
    switch (err) {
        case EGL_SUCCESS:             return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED:     return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS:          return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC:           return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE:       return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONFIG:          return "EGL_BAD_CONFIG";
        case EGL_BAD_CONTEXT:         return "EGL_BAD_CONTEXT";
        case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY:         return "EGL_BAD_DISPLAY";
        case EGL_BAD_MATCH:           return "EGL_BAD_MATCH";
        case EGL_BAD_NATIVE_PIXMAP:   return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW:   return "EGL_BAD_NATIVE_WINDOW";
        case EGL_BAD_PARAMETER:       return "EGL_BAD_PARAMETER";
        case EGL_BAD_SURFACE:         return "EGL_BAD_SURFACE";
        case EGL_CONTEXT_LOST:        return "EGL_CONTEXT_LOST";
        default:                      return "EGL_<unknown>";
    }
}

bool InitEGLDisplay() {
    if (g_ctx.display != EGL_NO_DISPLAY) return true;

    g_ctx.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_ctx.display == EGL_NO_DISPLAY) {
        MODU_LOGE("eglGetDisplay returned EGL_NO_DISPLAY");
        return false;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(g_ctx.display, &major, &minor)) {
        MODU_LOGE("eglInitialize failed: %s", EglErrorString(eglGetError()));
        g_ctx.display = EGL_NO_DISPLAY;
        return false;
    }
    MODU_LOGI("EGL %d.%d initialized", major, minor);

    // Ask for an ES3-capable, 8-bit RGB(A), 24-bit depth, 8-bit stencil
    // window-renderable config. Stage 2 keeps it simple; depth/stencil
    // sizes will likely need tweaking once the real renderer is wired in.
    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      24,
        EGL_STENCIL_SIZE,    8,
        EGL_NONE
    };
    EGLint configCount = 0;
    if (!eglChooseConfig(g_ctx.display, configAttribs, &g_ctx.config, 1, &configCount) || configCount < 1) {
        MODU_LOGE("eglChooseConfig failed: %s", EglErrorString(eglGetError()));
        eglTerminate(g_ctx.display);
        g_ctx.display = EGL_NO_DISPLAY;
        return false;
    }

    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    g_ctx.context = eglCreateContext(g_ctx.display, g_ctx.config, EGL_NO_CONTEXT, contextAttribs);
    if (g_ctx.context == EGL_NO_CONTEXT) {
        MODU_LOGE("eglCreateContext failed: %s", EglErrorString(eglGetError()));
        eglTerminate(g_ctx.display);
        g_ctx.display = EGL_NO_DISPLAY;
        g_ctx.config = nullptr;
        return false;
    }
    return true;
}

bool CreateEGLSurface(ANativeWindow* window) {
    if (!window || g_ctx.display == EGL_NO_DISPLAY || g_ctx.context == EGL_NO_CONTEXT) {
        return false;
    }

    // Match the native window's buffer format to the EGL config so the
    // BufferQueue producer/consumer agree on layout (otherwise SurfaceFlinger
    // composites the wrong thing — or refuses outright on some devices).
    EGLint nativeVisualId = 0;
    eglGetConfigAttrib(g_ctx.display, g_ctx.config, EGL_NATIVE_VISUAL_ID, &nativeVisualId);
    ANativeWindow_setBuffersGeometry(window, 0, 0, nativeVisualId);

    g_ctx.surface = eglCreateWindowSurface(g_ctx.display, g_ctx.config, window, nullptr);
    if (g_ctx.surface == EGL_NO_SURFACE) {
        MODU_LOGE("eglCreateWindowSurface failed: %s", EglErrorString(eglGetError()));
        return false;
    }

    if (!eglMakeCurrent(g_ctx.display, g_ctx.surface, g_ctx.surface, g_ctx.context)) {
        MODU_LOGE("eglMakeCurrent failed: %s", EglErrorString(eglGetError()));
        eglDestroySurface(g_ctx.display, g_ctx.surface);
        g_ctx.surface = EGL_NO_SURFACE;
        return false;
    }

    eglQuerySurface(g_ctx.display, g_ctx.surface, EGL_WIDTH,  &g_ctx.surfaceWidth);
    eglQuerySurface(g_ctx.display, g_ctx.surface, EGL_HEIGHT, &g_ctx.surfaceHeight);
    MODU_LOGI("EGL surface ready: %dx%d", g_ctx.surfaceWidth, g_ctx.surfaceHeight);
    MODU_LOGI("GL_VENDOR=%s",   glGetString(GL_VENDOR));
    MODU_LOGI("GL_RENDERER=%s", glGetString(GL_RENDERER));
    MODU_LOGI("GL_VERSION=%s",  glGetString(GL_VERSION));
    return true;
}

void DestroyEGLSurface() {
    if (g_ctx.display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(g_ctx.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_ctx.surface != EGL_NO_SURFACE) {
        eglDestroySurface(g_ctx.display, g_ctx.surface);
        g_ctx.surface = EGL_NO_SURFACE;
    }
    g_ctx.surfaceWidth = 0;
    g_ctx.surfaceHeight = 0;
}

void DestroyEGL() {
    DestroyEGLSurface();
    if (g_ctx.context != EGL_NO_CONTEXT) {
        eglDestroyContext(g_ctx.display, g_ctx.context);
        g_ctx.context = EGL_NO_CONTEXT;
    }
    if (g_ctx.display != EGL_NO_DISPLAY) {
        eglTerminate(g_ctx.display);
        g_ctx.display = EGL_NO_DISPLAY;
    }
    g_ctx.config = nullptr;
}

// Drain the ALooper. timeoutMs == -1 blocks indefinitely until an event
// arrives; 0 polls non-blocking. Returns false if app shutdown requested.
bool DrainEvents(int timeoutMs) {
    int events = 0;
    android_poll_source* source = nullptr;
    while (ALooper_pollOnce(timeoutMs, nullptr, &events,
                            reinterpret_cast<void**>(&source)) >= 0) {
        if (source) source->process(g_ctx.app, source);
        if (g_ctx.app && g_ctx.app->destroyRequested) return false;
        // After the first event, switch to non-blocking polls so we don't
        // block on a single event delivery when more are pending.
        timeoutMs = 0;
    }
    return !(g_ctx.app && g_ctx.app->destroyRequested);
}

void HandleAppCmd(android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                if (!InitEGLDisplay() || !CreateEGLSurface(app->window)) {
                    MODU_LOGE("Failed to bring up EGL on INIT_WINDOW");
                }
            }
            break;
        case APP_CMD_TERM_WINDOW:
            DestroyEGLSurface();
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            if (g_ctx.surface != EGL_NO_SURFACE && g_ctx.display != EGL_NO_DISPLAY) {
                eglQuerySurface(g_ctx.display, g_ctx.surface, EGL_WIDTH,  &g_ctx.surfaceWidth);
                eglQuerySurface(g_ctx.display, g_ctx.surface, EGL_HEIGHT, &g_ctx.surfaceHeight);
                MODU_LOGI("Surface resized to %dx%d", g_ctx.surfaceWidth, g_ctx.surfaceHeight);
            }
            break;
        case APP_CMD_GAINED_FOCUS:
        case APP_CMD_RESUME:
            g_ctx.paused = false;
            break;
        case APP_CMD_LOST_FOCUS:
        case APP_CMD_PAUSE:
            g_ctx.paused = true;
            break;
        case APP_CMD_DESTROY:
            DestroyEGL();
            break;
        default: break;
    }
}

int32_t HandleInputEvent(android_app* /*app*/, AInputEvent* event) {
    const int32_t type = AInputEvent_getType(event);
    if (type != AINPUT_EVENT_TYPE_MOTION) return 0;

    const int32_t source = AInputEvent_getSource(event);
    if ((source & AINPUT_SOURCE_TOUCHSCREEN) == 0) return 0;

    const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    // Primary pointer only — multi-touch is intentionally out of scope.
    const size_t pointerIndex = 0;
    const float x = AMotionEvent_getX(event, pointerIndex);
    const float y = AMotionEvent_getY(event, pointerIndex);

    switch (action) {
        case AMOTION_EVENT_ACTION_DOWN:
            g_ctx.touchActive = true;
            g_ctx.touchX = x;
            g_ctx.touchY = y;
            // Stage 2.5 will translate this into the engine's pointer
            // event queue (analogous to GLFW's mouse-button callbacks).
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            g_ctx.touchX = x;
            g_ctx.touchY = y;
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            g_ctx.touchActive = false;
            break;
        default: break;
    }
    return 1; // consumed
}

} // namespace

// === Public hooks for Engine.cpp ========================================

bool PollEvents() {
    if (!g_ctx.app || g_ctx.app->destroyRequested) return false;
    // Non-blocking drain — Engine.cpp calls this every frame, so any
    // pending lifecycle / input events get delivered, then the engine
    // immediately falls through to its own tick + render.
    return DrainEvents(0);
}

bool PresentFrame() {
    if (g_ctx.display == EGL_NO_DISPLAY || g_ctx.surface == EGL_NO_SURFACE) {
        return false;
    }
    if (!eglSwapBuffers(g_ctx.display, g_ctx.surface)) {
        EGLint err = eglGetError();
        if (err == EGL_BAD_SURFACE) {
            MODU_LOGW("eglSwapBuffers: surface invalid, releasing");
            DestroyEGLSurface();
        } else {
            MODU_LOGE("eglSwapBuffers failed: %s", EglErrorString(err));
        }
        return false;
    }
    return true;
}

bool HasRenderSurface() {
    return g_ctx.surface != EGL_NO_SURFACE && !g_ctx.paused;
}

void GetSurfaceSize(int* outWidth, int* outHeight) {
    if (outWidth)  *outWidth  = g_ctx.surfaceWidth;
    if (outHeight) *outHeight = g_ctx.surfaceHeight;
}

const char* GetInternalDataPath() {
    if (g_ctx.app && g_ctx.app->activity) {
        return g_ctx.app->activity->internalDataPath;
    }
    return nullptr;
}

void Run(android_app* app) {
    // Install the stdout/stderr → logcat redirect before any engine code
    // runs, so cerr lines from Engine::init() and friends actually appear
    // in `adb logcat -s Modularity.stderr:V Modularity.stdout:V`.
    RedirectStdioToLogcat();

    g_ctx = RuntimeContext{};
    g_ctx.app = app;
    app->userData = &g_ctx;
    app->onAppCmd = HandleAppCmd;
    app->onInputEvent = HandleInputEvent;

    MODU_LOGI("Modularity Android runtime starting");

    // Block until we have a render surface (the NativeActivity sends
    // APP_CMD_INIT_WINDOW once the activity is on screen). Constructing
    // the engine before EGL is up means GL resources would fail to
    // allocate on init.
    while (!app->destroyRequested && g_ctx.surface == EGL_NO_SURFACE) {
        if (!DrainEvents(-1)) break;
    }
    if (app->destroyRequested) {
        MODU_LOGI("Destroy requested before EGL came up");
        DestroyEGL();
        return;
    }

    // Install the APK-backed AssetSource before constructing the Engine
    // so any read during Engine::init() can resolve via AAssetManager.
    if (app->activity && app->activity->assetManager) {
        Modularity::Platform::SetAssetSource(
            Modularity::Platform::MakeAndroidAssetSource(app->activity->assetManager));
        MODU_LOGI("AssetSource installed (AAssetManager)");
    } else {
        MODU_LOGW("No AAssetManager available; engine will fall back to filesystem reads");
    }

    // Hand off to the existing Engine bootstrap. The engine's own main
    // loop now polls AndroidRuntime::PollEvents() each iteration and
    // presents via AndroidRuntime::PresentFrame() (see Engine::run() —
    // the Android paths are gated with #ifdef __ANDROID__).
    {
        Engine engine;
        if (!engine.init()) {
            MODU_LOGE("Engine::init() failed on Android");
        } else {
            MODU_LOGI("Engine initialized; entering main loop");
            engine.run();
            engine.shutdown();
        }
    }

    MODU_LOGI("Modularity Android runtime exiting");
    DestroyEGL();
}

} // namespace Modularity::AndroidRuntime

extern "C" void android_main(struct android_app* app) {
    Modularity::AndroidRuntime::Run(app);
}

#endif // __ANDROID__
