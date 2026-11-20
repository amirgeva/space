#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

#include <filesystem>

namespace space {

class Texture {
public:
    // Loads an image file (PNG, JPG, TGA, BMP, ...). Color textures are uploaded as sRGB.
    explicit Texture(const std::filesystem::path& path, bool srgb = true);
    // 1x1 texture of a solid color, used as a default when a material has no map.
    explicit Texture(const glm::u8vec4& color);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    void bind(GLuint unit) const;

    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    void upload(const unsigned char* pixels, int width, int height, bool srgb, bool mipmaps);

    GLuint m_id = 0;
    int m_width = 0;
    int m_height = 0;
};

} // namespace space
