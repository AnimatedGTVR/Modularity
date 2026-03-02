#include "../../include/Window/Window.h"
#include "../../include/ThirdParty/stb_image.h"

#include <cstdlib>
#include <iostream>

int width, height, channels;

namespace
{
    constexpr int kAnyProfile = 0;

    void glfwErrorCallback(int code, const char* description)
    {
        std::cerr << "GLFW error (" << code << "): "
                  << (description ? description : "unknown")
                  << "\n";
    }

    GLFWwindow* tryCreateWindow(int major, int minor, int profile)
    {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);

        if (profile != kAnyProfile)
        {
            glfwWindowHint(GLFW_OPENGL_PROFILE, profile);
        }

        return glfwCreateWindow(1000, 800, "Modularity", nullptr, nullptr);
    }

    GLFWwindow* tryCreateVulkanWindow()
    {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        return glfwCreateWindow(1000, 800, "Modularity", nullptr, nullptr);
    }

} // namespace


GLFWwindow* Window::makeWindow(Modularity::GraphicsBackend backend)
{
    unsigned char* pixels = stbi_load(
        "Resources/Engine-Root/Modu-Logo.png",
        &width,
        &height,
        &channels,
        4
    );

#if defined(__linux__)
    const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
    const char* x11_display     = std::getenv("DISPLAY");

    if (wayland_display && *wayland_display &&
        glfwPlatformSupported(GLFW_PLATFORM_WAYLAND))
    {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
    }
    else if (x11_display && *x11_display &&
             glfwPlatformSupported(GLFW_PLATFORM_X11))
    {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    }
    else
    {
        glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
    }
#endif

    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return nullptr;
    }

    GLFWwindow* window = nullptr;

    if (backend == Modularity::GraphicsBackend::Vulkan)
    {
        window = tryCreateVulkanWindow();

        if (!window)
        {
            std::cerr << "Failed to create GLFW window (Vulkan no-API)\n";
        }
    }
    else
    {
        window = tryCreateWindow(3, 3, GLFW_OPENGL_CORE_PROFILE);

        if (!window)
        {
            std::cerr << "Failed to create GLFW window (OpenGL 3.3 core). Retrying...\n";
            window = tryCreateWindow(3, 3, kAnyProfile);
        }

        if (!window)
        {
            std::cerr << "Failed to create GLFW window (OpenGL 3.3 any). Retrying...\n";
            window = tryCreateWindow(3, 0, kAnyProfile);
        }

        if (!window)
        {
            std::cerr << "Failed to create GLFW window (OpenGL 3.0 any). Retrying...\n";
            window = tryCreateWindow(2, 1, kAnyProfile);
        }
    }

    if (!window)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return nullptr;
    }

    if (backend == Modularity::GraphicsBackend::OpenGL)
    {
        glfwMakeContextCurrent(window);
        glfwSwapInterval(0);

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            std::cerr << "Failed to initialize GLAD\n";
            return nullptr;
        }

        std::cout << "OpenGL: " << glGetString(GL_VERSION) << "\n";
    }

    if (pixels)
    {
        GLFWimage icon;
        icon.width  = width;
        icon.height = height;
        icon.pixels = pixels;

        glfwSetWindowIcon(window, 1, &icon);
        stbi_image_free(pixels);
    }

    return window;
}