#include "../../include/Skybox/Skybox.h"
#include "../../include/Shaders/Shader.h"
#include "../../include/Textures/Texture.h"
#include "../../include/Graphics/OpenGL.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <string>
#include "../../src/ThirdParty/glm/glm.hpp"
#include "../../src/ThirdParty/glm/gtc/type_ptr.hpp"
#include "../../src/ThirdParty/glm/gtc/matrix_transform.hpp"

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
    releaseBakedSky();
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
}

namespace {
constexpr int kBakedSkyFaceResolution = 1024;
constexpr float kBakedSkyRefreshIntervalSec = 0.25f;
// Unit 12 stays clear of the scene shader's bindings (0-2 material, 3-6 shadow
// cubes, 7-10 directional shadows, 11 reflection cube).
constexpr int kBakedSkyTextureUnit = 12;
} // namespace

void Skybox::releaseBakedSky() {
    if (bakedFbo) {
        glDeleteFramebuffers(1, &bakedFbo);
        bakedFbo = 0;
    }
    if (bakedCube) {
        glDeleteTextures(1, &bakedCube);
        bakedCube = 0;
    }
    bakedDirtyMask = 0;
    bakedHasFullCapture = false;
    bakedLastRefreshSec = -1.0f;
}

bool Skybox::cachedSkyUsable() const {
    // Custom sky shaders don't have the sampling branch, so only the stock
    // procedural shader goes through the cache. Scrolling mode is already a
    // plain texture fetch.
    return settings.cachedSky &&
           settings.mode == SkyboxMode::Procedural &&
           fragPath == "Resources/Shaders/skybox_frag.glsl" &&
           skyboxShader && skyboxShader->ID != 0;
}

bool Skybox::ensureBakedSkyTargets() {
    if (bakedAllocFailed) return false;
    if (bakedCube != 0 && bakedFbo != 0) return true;

    glGenTextures(1, &bakedCube);
    glBindTexture(GL_TEXTURE_CUBE_MAP, bakedCube);
    for (int face = 0; face < 6; ++face) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB10_A2,
                     kBakedSkyFaceResolution, kBakedSkyFaceResolution, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    glGenFramebuffers(1, &bakedFbo);
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, bakedFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_X, bakedCube, 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    if (!complete) {
        std::cerr << "[WARN] Baked sky framebuffer incomplete; falling back to live sky\n";
        releaseBakedSky();
        bakedAllocFailed = true;
        return false;
    }
    bakedDirtyMask = 0x3F;
    bakedHasFullCapture = false;
    return true;
}

void Skybox::bakeDirtyFaces(int maxFaces) {
    if (bakedDirtyMask == 0 || maxFaces <= 0) return;

    static const glm::vec3 faceDirs[6] = {
        glm::vec3(1.0f, 0.0f, 0.0f),  glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),  glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),  glm::vec3(0.0f, 0.0f, -1.0f),
    };
    static const glm::vec3 faceUps[6] = {
        glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),  glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
    };

    GLint prevFbo = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    const GLboolean blendWas = glIsEnabled(GL_BLEND);
    const GLboolean scissorWas = glIsEnabled(GL_SCISSOR_TEST);
    if (blendWas) glDisable(GL_BLEND);
    if (scissorWas) glDisable(GL_SCISSOR_TEST);

    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glBindFramebuffer(GL_FRAMEBUFFER, bakedFbo);
    glViewport(0, 0, kBakedSkyFaceResolution, kBakedSkyFaceResolution);
    int baked = 0;
    for (int face = 0; face < 6 && baked < maxFaces; ++face) {
        if ((bakedDirtyMask & (1 << face)) == 0) continue;
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, bakedCube, 0);
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), faceDirs[face], faceUps[face]);
        applySkyUniforms(view, proj,
                         static_cast<float>(kBakedSkyFaceResolution),
                         static_cast<float>(kBakedSkyFaceResolution),
                         glm::vec2(0.0f), false);
        drawSkyTriangle();
        bakedDirtyMask &= ~(1 << face);
        ++baked;
    }
    if (bakedDirtyMask == 0) {
        bakedHasFullCapture = true;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (blendWas) glEnable(GL_BLEND);
    if (scissorWas) glEnable(GL_SCISSOR_TEST);
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
    settings.environmentReflectionIntensity = std::clamp(settings.environmentReflectionIntensity, 0.0f, 2.0f);
    settings.reflectionDistanceFadeStart = std::max(0.0f, settings.reflectionDistanceFadeStart);
    settings.reflectionDistanceFadeEnd = std::max(settings.reflectionDistanceFadeStart + 0.01f, settings.reflectionDistanceFadeEnd);
    settings.fogMode = std::clamp(settings.fogMode, 0, 2);
    settings.fogStart = std::max(0.0f, settings.fogStart);
    settings.fogEnd = std::max(settings.fogStart + 0.01f, settings.fogEnd);
    settings.fogDensity = std::clamp(settings.fogDensity, 0.0f, 1.0f);
    settings.fogHeightFalloff = std::clamp(settings.fogHeightFalloff, 0.0f, 1.0f);
    reloadTextures();
    if (!settings.cachedSky) {
        releaseBakedSky();
    } else if (bakedCube != 0) {
        // Visual settings changed; rebake all faces over the next draws while
        // still sampling the (one settings-change stale) previous capture.
        bakedDirtyMask = 0x3F;
        bakedLastRefreshSec = animationTime;
    }
    bakedAllocFailed = false;
}

void Skybox::applySkyUniforms(const glm::mat4& viewMat, const glm::mat4& projMat,
                              float viewportW, float viewportH,
                              const glm::vec2& cameraAngles, bool sampleBaked) {
    skyboxShader->use();
    skyboxShader->setMat4("view", viewMat);
    skyboxShader->setMat4("projection", projMat);
    // Precomputed here so the fragment shader doesn't run inverse() per pixel.
    skyboxShader->setMat4("uInvProjection", glm::inverse(projMat));
    skyboxShader->setMat3("uInvViewRot", glm::inverse(glm::mat3(viewMat)));
    skyboxShader->setFloat("timeOfDay", timeOfDay);
    skyboxShader->setFloat("uSkyTime", animationTime);
    skyboxShader->setInt("uSunTex", 0);
    skyboxShader->setInt("uMoonTex", 1);
    skyboxShader->setInt("uScrollTex", 2);
    skyboxShader->setInt("uBakedSkyCube", kBakedSkyTextureUnit);
    skyboxShader->setBool("uUseBakedSky", sampleBaked);
    skyboxShader->setInt("uSkyMode", static_cast<int>(settings.mode));
    skyboxShader->setVec2("uScrollRepeat", glm::vec2(settings.scrollingRepeatX, settings.scrollingRepeatY));
    skyboxShader->setFloat("uScrollLookSensitivity", settings.scrollingLookSensitivity);
    skyboxShader->setFloat("uScrollVerticalInfluence", settings.scrollingVerticalInfluence);
    skyboxShader->setBool("uHasScrollTexture", scrollingTexture != nullptr && scrollingTexture->GetID() != 0);
    skyboxShader->setVec2("uViewportSize", glm::vec2(std::max(1.0f, viewportW),
                                                     std::max(1.0f, viewportH)));
    skyboxShader->setVec2("uCameraAngles", cameraAngles);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (sunTexture && sunTexture->GetID() != 0) ? sunTexture->GetID() : 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, (moonTexture && moonTexture->GetID() != 0) ? moonTexture->GetID() : 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, (scrollingTexture && scrollingTexture->GetID() != 0) ? scrollingTexture->GetID() : 0);
    glActiveTexture(GL_TEXTURE0);
}

void Skybox::drawSkyTriangle() {
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void Skybox::draw(const float* view, const float* projection) {
    static const auto startTime = std::chrono::steady_clock::now();
    animationTime = std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime).count();

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

    bool sampleBaked = false;
    if (cachedSkyUsable() && ensureBakedSkyTargets()) {
        if (bakedLastRefreshSec < 0.0f ||
            animationTime - bakedLastRefreshSec >= kBakedSkyRefreshIntervalSec) {
            bakedDirtyMask = 0x3F;
            bakedLastRefreshSec = animationTime;
        }
        // First capture fills all six faces at once so the cube never shows
        // uninitialized memory; steady-state refresh spreads one face per draw.
        bakeDirtyFaces(bakedHasFullCapture ? 1 : 6);
        sampleBaked = bakedHasFullCapture;
    }

    glDepthFunc(GL_LEQUAL);
    applySkyUniforms(viewMat, projMat,
                     static_cast<float>(std::max(1, viewport[2])),
                     static_cast<float>(std::max(1, viewport[3])),
                     cameraAngles, sampleBaked);
    if (sampleBaked) {
        glActiveTexture(GL_TEXTURE0 + kBakedSkyTextureUnit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, bakedCube);
        glActiveTexture(GL_TEXTURE0);
    }
    drawSkyTriangle();
    if (sampleBaked) {
        glActiveTexture(GL_TEXTURE0 + kBakedSkyTextureUnit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthFunc(GL_LESS);
}
