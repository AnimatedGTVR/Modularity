// Placeholder Android runtime entry point. This file is structurally in place
// so the Android bring-up plan in docs/AndroidRuntime.md can be implemented
// incrementally without touching the desktop build. It is excluded from
// non-Android builds by the regex filter in CMakeLists.txt, and additionally
// guarded with __ANDROID__ so it stays inert if the filter is ever removed.

#ifdef __ANDROID__

#include "AndroidRuntime.h"

#include <android/log.h>
#include <android_native_app_glue.h>

namespace Modularity::AndroidRuntime {

void Run(android_app* app) {
    (void)app;
    // TODO: EGL context creation, AAssetManager-backed file IO, touch input
    // adapter, lifecycle hooks (APP_CMD_INIT_WINDOW / TERM_WINDOW /
    // APP_CMD_PAUSE / APP_CMD_RESUME), and dispatch into the engine update
    // loop. See docs/AndroidRuntime.md.
    __android_log_print(ANDROID_LOG_INFO, "Modularity",
                        "AndroidRuntime::Run is a placeholder; not yet implemented.");
}

} // namespace Modularity::AndroidRuntime

// android_native_app_glue requires the host app to expose android_main as the
// real entry point. Defining it here keeps the runtime startup discoverable
// even though the body just hands off to the placeholder Run().
extern "C" void android_main(struct android_app* app) {
    Modularity::AndroidRuntime::Run(app);
}

#endif // __ANDROID__
