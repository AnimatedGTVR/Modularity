#include "../../include/Skybox/Skybox.h"
#include "../../include/Shaders/Shader.h"
#include <glad/glad.h>
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
}

Skybox::~Skybox() {
    if (skyboxShader) delete skyboxShader;
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

void Skybox::draw(const float* view, const float* projection) {
    // Properly reconstruct the view matrix from the float array
    glm::mat4 viewMat = glm::make_mat4(view);
    glm::mat4 projMat = glm::make_mat4(projection);
    
    // Remove translation from view matrix (keep only rotation)
    viewMat = glm::mat4(glm::mat3(viewMat));
    
    glDepthFunc(GL_LEQUAL);
    skyboxShader->use();
    skyboxShader->setMat4("view", viewMat);
    skyboxShader->setMat4("projection", projMat);
    skyboxShader->setFloat("timeOfDay", timeOfDay);
    
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
}
