#include "../../../include/Shaders/Shader.h"
#include "../../../include/Graphics/OpenGL.h"
#include "../../ThirdParty/glm/glm.hpp"
#include "../../ThirdParty/glm/gtc/matrix_transform.hpp"
#include "../../ThirdParty/glm/gtc/type_ptr.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#if MODULARITY_OPENGL_ES
#include <regex>
#endif

namespace
{
#if MODULARITY_OPENGL_ES
    void replaceAll(std::string& text, const std::string& from, const std::string& to)
    {
        if (from.empty()) return;

        size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::string::npos)
        {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
#endif

    std::string prepareShaderSourceForBackend(const std::string& source)
    {
#if MODULARITY_OPENGL_ES
        std::string result = source;
        const std::string esHeader = "#version 300 es\nprecision highp float;\nprecision highp int;\n";

        if (result.rfind("#version", 0) == 0)
        {
            size_t lineEnd = result.find('\n');
            if (lineEnd == std::string::npos)
            {
                result = esHeader;
            }
            else
            {
                result.replace(0, lineEnd + 1, esHeader);
            }
        }
        else
        {
            result.insert(0, esHeader);
        }

        replaceAll(result, "noperspective ", "");
        const std::regex uniformInitializer(
            R"((uniform\s+(?:bool|int|float|vec2|vec3|vec4|mat3|mat4|sampler2D|samplerCube)\s+[A-Za-z_][A-Za-z0-9_]*(?:\s*\[[^\]]+\])?)\s*=\s*[^;]+;)");
        return std::regex_replace(result, uniformInitializer, "$1;");
#else
        return source;
#endif
    }
}

Shader::Shader(const char* vertexPath, const char* fragmentPath)
{
    std::string vertexCode = readShaderFile(vertexPath);
    std::string fragmentCode = readShaderFile(fragmentPath);
    
    compileShaders(vertexCode.c_str(), fragmentCode.c_str());
}

std::string Shader::readShaderFile(const char* filePath)
{
    std::ifstream shaderFile;
    shaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    
    try
    {
        shaderFile.open(filePath);
        std::stringstream shaderStream;
        shaderStream << shaderFile.rdbuf();
        shaderFile.close();
        return shaderStream.str();
    }
    catch (std::ifstream::failure& e)
    {
        std::cerr << "ERROR: Shader file not found: " << filePath << std::endl;
        return "";
    }
}

void Shader::compileShaders(const char* vertexSource, const char* fragmentSource)
{
    std::string preparedVertexSource = prepareShaderSourceForBackend(vertexSource ? vertexSource : "");
    std::string preparedFragmentSource = prepareShaderSourceForBackend(fragmentSource ? fragmentSource : "");
    const char* preparedVertexCStr = preparedVertexSource.c_str();
    const char* preparedFragmentCStr = preparedFragmentSource.c_str();

    unsigned int vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &preparedVertexCStr, NULL);
    glCompileShader(vertex);
    checkCompileErrors(vertex, "VERTEX");
    
    unsigned int fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &preparedFragmentCStr, NULL);
    glCompileShader(fragment);
    checkCompileErrors(fragment, "FRAGMENT");

    int success = 0;
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
    if (!success) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        ID = 0;
        return;
    }
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
    if (!success) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        ID = 0;
        return;
    }
    
    ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    glLinkProgram(ID);
    checkCompileErrors(ID, "PROGRAM");

    glGetProgramiv(ID, GL_LINK_STATUS, &success);
    if (!success) {
        glDeleteProgram(ID);
        ID = 0;
    }
    
    glDeleteShader(vertex);
    glDeleteShader(fragment);
}

void Shader::use()
{
    glUseProgram(ID);
}

int Shader::getUniformLocation(const std::string& name) const
{
    auto it = uniformLocationCache.find(name);
    if (it != uniformLocationCache.end()) {
        return it->second;
    }
    int location = glGetUniformLocation(ID, name.c_str());
    uniformLocationCache.emplace(name, location);
    return location;
}

void Shader::checkCompileErrors(unsigned int shader, std::string type)
{
    int success;
    char infoLog[1024];
    
    if (type != "PROGRAM")
    {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            glGetShaderInfoLog(shader, 1024, NULL, infoLog);
            std::cerr << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << std::endl;
        }
    }
    else
    {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success)
        {
            glGetProgramInfoLog(shader, 1024, NULL, infoLog);
            std::cerr << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << std::endl;
        }
    }
}

void Shader::setBool(const std::string &name, bool value) const
{
    glUniform1i(getUniformLocation(name), (int)value);
}

void Shader::setInt(const std::string &name, int value) const
{
    glUniform1i(getUniformLocation(name), value);
}

void Shader::setFloat(const std::string &name, float value) const
{
    glUniform1f(getUniformLocation(name), value);
}

void Shader::setVec2(const std::string &name, const glm::vec2 &value) const
{
    glUniform2fv(getUniformLocation(name), 1, &value[0]);
}

void Shader::setVec3(const std::string &name, const glm::vec3 &value) const
{
    glUniform3fv(getUniformLocation(name), 1, &value[0]);
}

void Shader::setVec4(const std::string &name, const glm::vec4 &value) const
{
    glUniform4fv(getUniformLocation(name), 1, &value[0]);
}

void Shader::setMat4(const std::string &name, const glm::mat4 &mat) const
{
    glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(mat));
}

void Shader::setMat4Array(const std::string &name, const glm::mat4 *data, int count) const
{
    if (count <= 0 || !data) return;
    glUniformMatrix4fv(getUniformLocation(name), count, GL_FALSE, glm::value_ptr(data[0]));
}
