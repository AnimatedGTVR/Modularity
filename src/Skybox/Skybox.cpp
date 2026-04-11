#include "../../include/Skybox/Skybox.h"
#include "../../include/Shaders/Shader.h"
#include "../../include/Textures/Texture.h"
#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include "../../src/ThirdParty/glm/glm.hpp"
#include "../../src/ThirdParty/glm/gtc/type_ptr.hpp"

// Skybox cube vertices (positions only, no normals/UVs needed)
float skyboxVertices[] = {
    // positions          
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,

    -1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,

     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,

    -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,

    -1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f, -1.0f,

    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f
};

Skybox::Skybox() {
    skyboxShader = new Shader(vertPath.c_str(), fragPath.c_str());
    setupMesh();
    reloadTextures();
}

Skybox::~Skybox() {
    if (skyboxShader) delete skyboxShader;
    if (sunTexture) delete sunTexture;
    if (moonTexture) delete moonTexture;
    if (scrollingTexture) delete scrollingTexture;
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
}

void Skybox::setupMesh() {
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    
    glBindVertexArray(0);
}

void Skybox::setTimeOfDay(float time) {
    timeOfDay = time;
}

bool Skybox::reloadShader() {
    if (skyboxShader) {
        delete skyboxShader;
        skyboxShader = nullptr;
    }
    skyboxShader = new Shader(vertPath.c_str(), fragPath.c_str());
    return skyboxShader && skyboxShader->ID != 0;
}

void Skybox::reloadTextures() {
    if (sunTexture) {
        delete sunTexture;
        sunTexture = nullptr;
    }
    if (moonTexture) {
        delete moonTexture;
        moonTexture = nullptr;
    }
    if (scrollingTexture) {
        delete scrollingTexture;
        scrollingTexture = nullptr;
    }

    if (!settings.sunTexturePath.empty()) {
        sunTexture = new Texture(settings.sunTexturePath,
                                 GL_CLAMP_TO_EDGE,
                                 GL_CLAMP_TO_EDGE,
                                 GL_LINEAR_MIPMAP_LINEAR,
                                 GL_LINEAR);
        if (sunTexture && sunTexture->GetID() == 0) {
            delete sunTexture;
            sunTexture = nullptr;
        }
    }

    if (!settings.moonTexturePath.empty()) {
        moonTexture = new Texture(settings.moonTexturePath,
                                  GL_CLAMP_TO_EDGE,
                                  GL_CLAMP_TO_EDGE,
                                  GL_LINEAR_MIPMAP_LINEAR,
                                  GL_LINEAR);
        if (moonTexture && moonTexture->GetID() == 0) {
            delete moonTexture;
            moonTexture = nullptr;
        }
    }

    if (!settings.scrollingTexturePath.empty()) {
        scrollingTexture = new Texture(settings.scrollingTexturePath,
                                       GL_REPEAT,
                                       GL_REPEAT,
                                       GL_NEAREST,
                                       GL_NEAREST);
        if (scrollingTexture && scrollingTexture->GetID() == 0) {
            delete scrollingTexture;
            scrollingTexture = nullptr;
        } else if (scrollingTexture) {
            glBindTexture(GL_TEXTURE_2D, scrollingTexture->GetID());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
}

void Skybox::setShaderPaths(const std::string& vert, const std::string& frag) {
    if (!vert.empty()) vertPath = vert;
    if (!frag.empty()) fragPath = frag;
    if (!reloadShader()) {
        std::cerr << "Failed to reload skybox shader, reverting to defaults\n";
        vertPath = "Resources/Shaders/skybox_vert.glsl";
        fragPath = "Resources/Shaders/skybox_frag.glsl";
        reloadShader();
    }
}

void Skybox::setSettings(const SkyboxSettings& newSettings) {
    settings = newSettings;
    settings.scrollingRepeatX = std::max(0.01f, settings.scrollingRepeatX);
    settings.scrollingRepeatY = std::max(0.01f, settings.scrollingRepeatY);
    settings.scrollingLookSensitivity = std::max(0.0f, settings.scrollingLookSensitivity);
    settings.scrollingVerticalInfluence = std::clamp(settings.scrollingVerticalInfluence, 0.0f, 1.0f);
    reloadTextures();
}

void Skybox::draw(const float* view, const float* projection) {
    // Properly reconstruct the view matrix from the float array
    glm::mat4 viewMat = glm::make_mat4(view);
    glm::mat4 projMat = glm::make_mat4(projection);
    
    // Remove translation from view matrix (keep only rotation)
    viewMat = glm::mat4(glm::mat3(viewMat));
    glm::vec3 cameraForward = glm::normalize(glm::transpose(glm::mat3(viewMat)) * glm::vec3(0.0f, 0.0f, -1.0f));
    glm::vec2 cameraAngles(
        std::atan2(cameraForward.x, -cameraForward.z) / (2.0f * 3.14159265359f),
        std::asin(std::clamp(cameraForward.y, -1.0f, 1.0f))
    );
    GLint viewport[4] = {0, 0, 1, 1};
    glGetIntegerv(GL_VIEWPORT, viewport);
    
    glDepthFunc(GL_LEQUAL);
    skyboxShader->use();
    skyboxShader->setMat4("view", viewMat);
    skyboxShader->setMat4("projection", projMat);
    skyboxShader->setFloat("timeOfDay", timeOfDay);
    skyboxShader->setInt("uSunTex", 0);
    skyboxShader->setInt("uMoonTex", 1);
    skyboxShader->setInt("uScrollTex", 2);
    skyboxShader->setInt("uSkyMode", static_cast<int>(settings.mode));
    skyboxShader->setVec2("uScrollRepeat", glm::vec2(settings.scrollingRepeatX, settings.scrollingRepeatY));
    skyboxShader->setFloat("uScrollLookSensitivity", settings.scrollingLookSensitivity);
    skyboxShader->setFloat("uScrollVerticalInfluence", settings.scrollingVerticalInfluence);
    skyboxShader->setBool("uHasScrollTexture", scrollingTexture != nullptr && scrollingTexture->GetID() != 0);
    skyboxShader->setVec2("uViewportSize", glm::vec2(static_cast<float>(std::max(1, viewport[2])),
                                                     static_cast<float>(std::max(1, viewport[3]))));
    skyboxShader->setVec2("uCameraAngles", cameraAngles);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (sunTexture && sunTexture->GetID() != 0) ? sunTexture->GetID() : 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, (moonTexture && moonTexture->GetID() != 0) ? moonTexture->GetID() : 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, (scrollingTexture && scrollingTexture->GetID() != 0) ? scrollingTexture->GetID() : 0);
    
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthFunc(GL_LESS);
}
