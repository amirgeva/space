#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

#include <filesystem>

namespace space {

class Shader {
public:
    Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    void use() const;

    void set(const char* name, int value) const;
    void set(const char* name, float value) const;
    void set(const char* name, const glm::vec2& value) const;
    void set(const char* name, const glm::vec3& value) const;
    void set(const char* name, const glm::mat3& value) const;
    void set(const char* name, const glm::mat4& value) const;

private:
    GLint location(const char* name) const;

    GLuint m_program = 0;
};

} // namespace space
