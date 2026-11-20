#include "render/Shader.h"

#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace space {

namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open shader file: " + path.string());
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

GLuint compile(GLenum type, const std::string& source, const std::filesystem::path& path)
{
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error("Shader compile error in " + path.string() + ":\n" + log);
    }
    return shader;
}

} // namespace

Shader::Shader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath)
{
    GLuint vs = compile(GL_VERTEX_SHADER, readFile(vertexPath), vertexPath);
    GLuint fs = compile(GL_FRAGMENT_SHADER, readFile(fragmentPath), fragmentPath);

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        glDeleteProgram(m_program);
        throw std::runtime_error(std::string("Shader link error:\n") + log);
    }
}

Shader::~Shader()
{
    if (m_program)
        glDeleteProgram(m_program);
}

void Shader::use() const { glUseProgram(m_program); }

GLint Shader::location(const char* name) const { return glGetUniformLocation(m_program, name); }

void Shader::set(const char* name, int value) const { glUniform1i(location(name), value); }

void Shader::set(const char* name, float value) const { glUniform1f(location(name), value); }

void Shader::set(const char* name, const glm::vec3& value) const
{
    glUniform3fv(location(name), 1, glm::value_ptr(value));
}

void Shader::set(const char* name, const glm::mat3& value) const
{
    glUniformMatrix3fv(location(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::set(const char* name, const glm::mat4& value) const
{
    glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value));
}

} // namespace space
