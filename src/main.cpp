#include "Engine.h"
#include <iostream>

int main() {
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
