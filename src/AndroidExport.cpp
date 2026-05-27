#include "Engine.h"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// Desktop-side helper used by the editor's export pipeline to validate that an
// Android NDK is reachable before attempting an Android build. The actual
// runtime entry points live under src/AndroidRuntime/ and are only compiled
// when targeting Android (see CMakeLists.txt).
std::string Engine::resolveAndroidNdkPath() {
    const char* envVars[] = {
        "ANDROID_NDK_ROOT",
        "ANDROID_NDK_HOME",
        "ANDROID_NDK",
    };
    for (const char* var : envVars) {
        const char* value = std::getenv(var);
        if (!value || !*value) continue;
        std::error_code ec;
        fs::path candidate(value);
        if (!fs::is_directory(candidate, ec)) continue;
        // Cheap sanity check: a real NDK has a source.properties file at its
        // root. Without this we'd happily accept any directory the user set.
        if (fs::exists(candidate / "source.properties", ec)) {
            return candidate.string();
        }
    }
    return {};
}
