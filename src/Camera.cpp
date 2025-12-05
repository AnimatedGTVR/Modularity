#include "Camera.h"

void Camera::processMouse(double xpos, double ypos) {
    if (ImGuizmo::IsUsing() || ImGuizmo::IsOver()) {
        return;
    }
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = (xpos - lastX) * SENSITIVITY;
    float yoffset = (lastY - ypos) * SENSITIVITY;
    lastX = xpos;
    lastY = ypos;

    yaw += xoffset;
    pitch += yoffset;

    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    glm::vec3 direction;
    direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction.y = sin(glm::radians(pitch));
    direction.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(direction);
}

void Camera::processKeyboard(float deltaTime, GLFWwindow* window) {
    const float CAMERA_SPEED = 5.0f;
    const float SPRINT_SPEED = 10.0f;
    const float ACCELERATION = 15.0f;

    float currentSpeed = CAMERA_SPEED;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        currentSpeed = SPRINT_SPEED;
    }

    glm::vec3 desiredDir(0.0f);
    bool isMoving = false;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        desiredDir += front;
        isMoving = true;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        desiredDir -= front;
        isMoving = true;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        desiredDir -= glm::normalize(glm::cross(front, up));
        isMoving = true;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        desiredDir += glm::normalize(glm::cross(front, up));
        isMoving = true;
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        desiredDir -= up;
        isMoving = true;
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        desiredDir += up;
        isMoving = true;
    }
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    glm::vec3 targetVelocity(0.0f);
    if (isMoving) {
        float length = glm::length(desiredDir);
        if (length > 0.0001f) {
            desiredDir = desiredDir / length;
            targetVelocity = desiredDir * currentSpeed;
        } else {
            targetVelocity = glm::vec3(0.0f);
        }
    }

    float smoothFactor = 1.0f - std::exp(-ACCELERATION * deltaTime);
    velocity = glm::mix(velocity, targetVelocity, smoothFactor);

    position += velocity * deltaTime;
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, position + front, up);
}

// ViewportController implementation
void ViewportController::updateFocusFromImGui(bool windowFocused) {
    if (!windowFocused && viewportFocused && !manualUnfocus) {
        viewportFocused = false;
    }
}

void ViewportController::setFocused(bool focused) {
    viewportFocused = focused;
}

bool ViewportController::isViewportFocused() const {
    return viewportFocused;
}

void ViewportController::clearManualUnfocus() {
    manualUnfocus = false;
}

void ViewportController::update(GLFWwindow* window, bool& cursorLocked) {
    if (viewportFocused && glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        viewportFocused = false;
        manualUnfocus = true;
        cursorLocked = false;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}
