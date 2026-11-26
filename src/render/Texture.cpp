#include "render/Texture.h"

#include <stb_image.h>

#include <stdexcept>

namespace space {

Texture::Texture(const std::filesystem::path& path, bool srgb)
{
    // OBJ texture coordinates have their origin at the bottom-left, like OpenGL.
    stbi_set_flip_vertically_on_load(1);

    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (!pixels)
        throw std::runtime_error("Cannot load texture " + path.string() + ": " + stbi_failure_reason());

    upload(pixels, width, height, srgb, true);
    stbi_image_free(pixels);
}

Texture::Texture(const glm::u8vec4& color)
{
    upload(&color.x, 1, 1, false, false);
}

Texture::~Texture()
{
    if (m_id)
        glDeleteTextures(1, &m_id);
}

void Texture::upload(const unsigned char* pixels, int width, int height, bool srgb, bool mipmaps)
{
    m_width = width;
    m_height = height;

    glGenTextures(1, &m_id);
    glBindTexture(GL_TEXTURE_2D, m_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        if (GLEW_EXT_texture_filter_anisotropic) {
            GLfloat maxAniso = 1.0f;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAniso);
        }
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::bind(GLuint unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_id);
}

} // namespace space
