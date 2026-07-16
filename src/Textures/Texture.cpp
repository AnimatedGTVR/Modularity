#include "../../include/Textures/Texture.h"
#include "../../include/Platform/AssetSource.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../../include/ThirdParty/stb_image.h"

#include <iostream>
#include <vector>

Texture::Texture(const std::string& path,
                 GLenum wrapS,
                 GLenum wrapT,
                 GLenum minFilter,
                 GLenum magFilter,
                 TextureFormatPolicy formatPolicy)
    : m_FormatPolicy(formatPolicy)
{
    stbi_set_flip_vertically_on_load(1);
    // read pixel bytes through AssetSource so desktop and APK assets both work; stb decodes
    // the in-memory buffer the same either way.
    std::vector<uint8_t> fileBytes =
        Modularity::Platform::GetAssetSource().ReadAll(path);
    if (fileBytes.empty()) {
        std::cerr << "Failed to load texture: " << path << "\n";
        return;
    }
    unsigned char* data = stbi_load_from_memory(fileBytes.data(),
                                                static_cast<int>(fileBytes.size()),
                                                &m_Width, &m_Height, &m_Channels, 0);
    if (!data) {
        std::cerr << "Failed to decode texture: " << path << "\n";
        return;
    }

    if (m_Channels == 4) {
        const size_t pixelCount = static_cast<size_t>(m_Width) * static_cast<size_t>(m_Height);
        for (size_t i = 0; i < pixelCount; ++i) {
            const unsigned char alpha = data[i * 4 + 3];
            if (alpha == 0 || alpha == 255) {
                if (alpha == 0) {
                    m_HasBinaryAlpha = true;
                }
                continue;
            }

            m_UsesAlphaBlending = true;
            break;
        }
    }

    // upload format follows the decoded channel count; the GPU storage format is chosen
    // separately below. OpenGL repacks the 8-bit source into whatever internal format we ask,
    // so the 16bpp paths need no manual conversion.
    if (m_Channels == 1) {
        m_DataFormat = GL_RED;
    } else if (m_Channels == 3) {
        m_DataFormat = GL_RGB;
    } else {
        m_DataFormat = GL_RGBA;
    }

    // resolve the policy to a concrete internal format. Auto picks the smallest the content
    // allows: no alpha bits on opaque textures, 4-bit alpha only when it actually blends.
    const bool hasAlpha = (m_Channels == 4);
    auto autoFormat = [&]() -> GLenum {
        if (m_Channels == 1) return GL_R8;                 // single-channel stays 8bpp
        if (!hasAlpha) return GL_RGB565;                   // opaque RGB
        if (m_UsesAlphaBlending) return GL_RGBA4;          // smooth alpha
        if (m_HasBinaryAlpha) return GL_RGB5_A1;           // cutout alpha
        return GL_RGB565;                                  // RGBA source, fully opaque
    };
    switch (m_FormatPolicy) {
        case TextureFormatPolicy::Full:
            m_InternalFormat = (m_Channels == 1) ? GL_R8
                             : (m_Channels == 3) ? GL_RGB8
                                                 : GL_RGBA8;
            break;
        case TextureFormatPolicy::RGB565:  m_InternalFormat = GL_RGB565;  break;
        case TextureFormatPolicy::RGB5_A1: m_InternalFormat = GL_RGB5_A1; break;
        case TextureFormatPolicy::RGBA4:   m_InternalFormat = GL_RGBA4;   break;
        case TextureFormatPolicy::Auto:
        default:
            m_InternalFormat = autoFormat();
            break;
    }

#ifdef __ANDROID__
    // GLES demands glTexImage2D's format share the internal format's base (RGB vs RGBA).
    // desktop GL silently drops alpha, GLES throws GL_INVALID_OPERATION and the texture comes
    // up black ( that's why file icons didn't render on device ). promote RGB internals to an
    // RGBA equivalent whenever the decoded data carries alpha.
    if (m_DataFormat == GL_RGBA) {
        if (m_InternalFormat == GL_RGB565)    m_InternalFormat = GL_RGB5_A1;
        else if (m_InternalFormat == GL_RGB8) m_InternalFormat = GL_RGBA8;
    }
#endif

    glGenTextures(1, &m_ID);
    glBindTexture(GL_TEXTURE_2D, m_ID);

    // upload
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, m_InternalFormat, m_Width, m_Height, 0, m_DataFormat, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    // Account for the GPU storage format actually used, not the source channels:
    // the 16bpp formats are 2 bytes/pixel, so the cache budget sees the real win.
    int bytesPerPixel;
    switch (m_InternalFormat) {
        case GL_R8:                       bytesPerPixel = 1; break;
        case GL_RGB565:
        case GL_RGB5_A1:
        case GL_RGBA4:                    bytesPerPixel = 2; break;
        case GL_RGB8:                     bytesPerPixel = 3; break;
        default:                          bytesPerPixel = 4; break;
    }
    m_ApproxMemoryBytes = static_cast<size_t>(m_Width) *
                          static_cast<size_t>(m_Height) *
                          static_cast<size_t>(bytesPerPixel);
    // Mip chains for 2D textures are roughly 33% extra memory.
    m_ApproxMemoryBytes += m_ApproxMemoryBytes / 3;

    // params
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
}

Texture::~Texture()
{
    if (m_ID) {
        glDeleteTextures(1, &m_ID);
        m_ID = 0;
    }
}

void Texture::Bind(GLenum unit) const
{
    glActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, m_ID);
}

void Texture::Unbind() const
{
    glBindTexture(GL_TEXTURE_2D, 0);
}
