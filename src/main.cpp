#include "Engine.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
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

int main() {
  if (auto exeDir = getExecutableDir(); !exeDir.empty()) {
    std::error_code ec;
    std::filesystem::current_path(exeDir, ec);
    if (ec) {
      std::cerr << "[WARN] Failed to set working dir to executable: "
                << ec.message() << std::endl;
    }
  }
  std::cerr << "[DEBUG] Starting engine initialization..." << std::endl;
  Engine engine;

  std::cerr << "[DEBUG] Calling engine.init()..." << std::endl;
  if (!engine.init()) {
    std::cerr << "[DEBUG] Engine init failed!" << std::endl;
    return -1;
  }

  std::cerr << "[DEBUG] Engine init succeeded, starting run loop..."
            << std::endl;
  engine.run();

  std::cerr << "[DEBUG] Run loop ended, shutting down..." << std::endl;
  engine.shutdown();

  return 0;
}
