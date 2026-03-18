#ifndef WINDOW_H
#define WINDOW_H
#include "../General/GlobalHeaders.h"
#include "../Graphics/GraphicsBackend.h"
#include <glad/glad.h>
#include "../../src/ThirdParty/glfw/include/GLFW/glfw3.h"

class Window {
public:
    GLFWwindow* makeWindow(Modularity::GraphicsBackend backend);
};

#endif
