#include "core/Window.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace space {

namespace {

void glfwErrorCallback(int code, const char* description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
}

void APIENTRY glDebugCallback(GLenum, GLenum type, GLuint, GLenum severity, GLsizei,
                              const GLchar* message, const void*)
{
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
        return;
    std::fprintf(stderr, "GL %s: %s\n", type == GL_DEBUG_TYPE_ERROR ? "ERROR" : "debug", message);
}

} // namespace

Window::Window(int width, int height, const std::string& title)
{
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit())
        throw std::runtime_error("Failed to initialize GLFW");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window (OpenGL 4.3 core required)");
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // vsync

    glewExperimental = GL_TRUE;
    if (GLenum err = glewInit(); err != GLEW_OK) {
        glfwDestroyWindow(m_window);
        glfwTerminate();
        throw std::runtime_error(reinterpret_cast<const char*>(glewGetErrorString(err)));
    }

#ifndef NDEBUG
    if (GLEW_KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glDebugCallback, nullptr);
    }
#endif

    std::printf("OpenGL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetScrollCallback(m_window, scrollCallback);
    glfwGetFramebufferSize(m_window, &m_width, &m_height);
    glViewport(0, 0, m_width, m_height);
}

Window::~Window()
{
    if (m_window)
        glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool Window::shouldClose() const { return glfwWindowShouldClose(m_window); }
void Window::requestClose() { glfwSetWindowShouldClose(m_window, GLFW_TRUE); }
void Window::swapBuffers() { glfwSwapBuffers(m_window); }
void Window::pollEvents()
{
    m_pressed.clear();
    m_scroll = 0.0f;
    glfwPollEvents();
}

bool Window::isKeyDown(int key) const { return glfwGetKey(m_window, key) == GLFW_PRESS; }

bool Window::wasKeyPressed(int key) const
{
    return std::find(m_pressed.begin(), m_pressed.end(), key) != m_pressed.end();
}

bool Window::isMouseButtonDown(int button) const { return glfwGetMouseButton(m_window, button) == GLFW_PRESS; }

glm::vec2 Window::cursorPos() const
{
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(m_window, &x, &y);
    return {float(x), float(y)};
}

void Window::setTitle(const std::string& title) { glfwSetWindowTitle(m_window, title.c_str()); }

void Window::keyCallback(GLFWwindow* window, int key, int, int action, int)
{
    if (action == GLFW_PRESS)
        static_cast<Window*>(glfwGetWindowUserPointer(window))->m_pressed.push_back(key);
}

void Window::scrollCallback(GLFWwindow* window, double, double dy)
{
    static_cast<Window*>(glfwGetWindowUserPointer(window))->m_scroll += float(dy);
}

void Window::framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    self->m_width = width;
    self->m_height = height;
    glViewport(0, 0, width, height);
}

} // namespace space
