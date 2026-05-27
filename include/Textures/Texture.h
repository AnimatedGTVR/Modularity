#ifndef TEXTURE_H
#define TEXTURE_H

#include <string>
#include <cstddef>
#include "../Graphics/OpenGL.h"

class Texture
{
public:
    // load from file; format and wrap/filter are optional
    Texture(const std::string& path,
            GLenum wrapS = GL_REPEAT,
            GLenum wrapT = GL_REPEAT,
            GLenum minFilter = GL_LINEAR_MIPMAP_LINEAR,
            GLenum magFilter = GL_LINEAR);
    ~Texture();

    void Bind(GLenum unit = GL_TEXTURE0) const;
    void Unbind() const;

    GLuint GetID() const { return m_ID; }
    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }
    size_t GetApproxMemoryBytes() const { return m_ApproxMemoryBytes; }
    bool HasAlphaChannel() const { return m_Channels == 4; }
    bool HasBinaryAlpha() const { return m_HasBinaryAlpha; }
    bool UsesAlphaBlending() const { return m_UsesAlphaBlending; }

private:
    GLuint m_ID = 0;
    int m_Width = 0;
    int m_Height = 0;
    int m_Channels = 0;
    GLenum m_InternalFormat = GL_RGBA;
    GLenum m_DataFormat = GL_RGBA;
    size_t m_ApproxMemoryBytes = 0;
    bool m_HasBinaryAlpha = false;
    bool m_UsesAlphaBlending = false;
};

#endif
