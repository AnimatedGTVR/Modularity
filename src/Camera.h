#pragma once

#include "Common.h"
#include <GLFW/glfw3.h>

class Camera {
public:
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f);
    glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    float yaw = -90.0f;
    float pitch = 0.0f;
    float speed = CAMERA_SPEED;
    float lastX = 400.0f, lastY = 300.0f;
    bool firstMouse = true;

    void processMouse(double xpos, double ypos);
    void processKeyboard(float deltaTime, GLFWwindow* window);
    glm::mat4 getViewMatrix() const;
};

class ViewportController {
private:
    bool viewportFocused = false;
    bool manualUnfocus = false;

public:
    void updateFocusFromImGui(bool windowFocused);
    void setFocused(bool focused);
    bool isViewportFocused() const;
    void clearManualUnfocus();
    void update(GLFWwindow* window, bool& cursorLocked);
};
