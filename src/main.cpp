#include "Engine.h"
#include "CrashReporter.h"
#include "ModularityLspServer.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if defined(_MSC_VER)
extern int __argc;
extern char** __argv;
#endif
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

static std::filesystem::path getExecutableDir() {
#if defined(_WIN32)
  char pathBuf[MAX_PATH] = {};
  DWORD len = GetModuleFileNameA(nullptr, pathBuf, MAX_PATH);
  if (len == 0 || len == MAX_PATH) {
    return {};
  }
  return std::filesystem::path(pathBuf).parent_path();
#elif defined(__APPLE__)
  uint32_t size = 0;
  if (_NSGetExecutablePath(nullptr, &size) != -1 || size == 0) {
    return {};
  }
  std::string buf(size, '\0');
  if (_NSGetExecutablePath(buf.data(), &size) != 0) {
    return {};
  }
  return std::filesystem::path(buf).lexically_normal().parent_path();
#else
  std::vector<char> buf(4096, '\0');
  ssize_t len = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
  if (len <= 0) {
    return {};
  }
  buf[static_cast<size_t>(len)] = '\0';
  return std::filesystem::path(buf.data()).parent_path();
#endif
}

static void logStartupDebug(const char* message) {
#if defined(NDEBUG)
  (void)message;
#else
  std::cerr << message << std::endl;
#endif
}

static bool isLspMode(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = (argv && argv[i]) ? argv[i] : "";
    if (arg == "--lsp") {
      return true;
    }
  }
  return false;
}

static int ModularityMain(int argc, char** argv) {
  if (isLspMode(argc, argv)) {
    if (auto exeDir = getExecutableDir(); !exeDir.empty()) {
      std::error_code ec;
      std::filesystem::current_path(exeDir, ec);
    }

    ModularityLspServer lspServer;
    return lspServer.run(argc, argv);
  }

  if (Modularity::CrashReporter::HandleCrashReporterMode(argc, argv)) {
    return 0;
  }

  if (auto exeDir = getExecutableDir(); !exeDir.empty()) {
    std::error_code ec;
    std::filesystem::current_path(exeDir, ec);
    if (ec) {
      std::cerr << "[WARN] Failed to set working dir to executable: "
                << ec.message() << std::endl;
    }
  }
  const std::string executablePath = (argc > 0 && argv && argv[0]) ? argv[0] : "";
  Modularity::CrashReporter::Initialize("Modularity", executablePath);

  return Modularity::CrashReporter::RunProtected([argc, argv]() -> int {
    logStartupDebug("[DEBUG] Starting engine initialization...");
    Engine engine;

    logStartupDebug("[DEBUG] Calling engine.init()...");
    if (!engine.init()) {
      logStartupDebug("[DEBUG] Engine init failed!");
      return -1;
    }

    logStartupDebug("[DEBUG] Engine init succeeded, starting run loop...");
    engine.run();

    logStartupDebug("[DEBUG] Run loop ended, shutting down...");
    engine.shutdown();
    return 0;
  });
}

int main(int argc, char** argv) {
  return ModularityMain(argc, argv);
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
#if defined(_MSC_VER)
  return ModularityMain(__argc, __argv);
#else
  return ModularityMain(0, nullptr);
#endif
}
#endif
