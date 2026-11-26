#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

struct GLFWwindow;

namespace space {

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const;
    void requestClose();
    void swapBuffers();
    void pollEvents();

    bool isKeyDown(int key) const;
    // True once per key press since the last pollEvents().
    bool wasKeyPressed(int key) const;
    bool isMouseButtonDown(int button) const;
    glm::vec2 cursorPos() const;
    // Scroll wheel movement since the last pollEvents().
    float scrollDelta() const { return m_scroll; }
    void setTitle(const std::string& title);
    int width() const { return m_width; }
    int height() const { return m_height; }
    float aspect() const { return m_height > 0 ? float(m_width) / float(m_height) : 1.0f; }

    GLFWwindow* handle() const { return m_window; }

private:
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void scrollCallback(GLFWwindow* window, double dx, double dy);

    GLFWwindow* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
    float m_scroll = 0.0f;
    std::vector<int> m_pressed;
};

} // namespace space
