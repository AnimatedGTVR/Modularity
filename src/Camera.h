#pragma once

#include "Common.h"

// Camera's input-poll methods take a window handle so the camera can ask
// GLFW about key state on desktop. Android has no window-keyed keyboard
// polling, so the impl in Camera.cpp guards those paths with #ifdef __ANDROID__
// and the handle is only forward-declared here so this header stays
// includable in the Android NDK build (which has no GLFW headers).
struct GLFWwindow;

class Camera {
public:
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f);
    glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    float moveSpeed = 5.0f;
    float sprintSpeed = 10.0f;
    float acceleration = 15.0f;
    bool smoothMovement = true;
    float mouseSensitivity = SENSITIVITY;
    float yaw = -90.0f;
    float pitch = 0.0f;
    float speed = CAMERA_SPEED;
    float lastX = 400.0f, lastY = 300.0f;
    bool firstMouse = true;
    bool orthographic = false;
    float pixelsPerUnit = 100.0f;

    void processMouse(double xpos, double ypos);
    void processMouseDelta(double deltaX, double deltaY);
    void processKeyboard(float deltaTime, GLFWwindow* window);
    glm::mat4 getViewMatrix() const;
};

class ViewportController {
private:
    bool viewportFocused = false;
    bool manualUnfocus = false;

public:
    void updateFocusFromImGui(bool windowFocused, bool cursorLocked);
    void setFocused(bool focused);
    bool isViewportFocused() const;
    void clearManualUnfocus();
    void update(GLFWwindow* window, bool& cursorLocked);
};
