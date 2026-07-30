#pragma once

#include <string>
#include <functional>

// Forward declare GLFW types to avoid exposing them in public header
struct GLFWwindow;
typedef struct VkSurfaceKHR_T* VkSurfaceKHR;
typedef struct VkInstance_T* VkInstance;

namespace eden {

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const;
    void close();
    void pollEvents();

    GLFWwindow* getHandle() const { return m_window; }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    bool wasResized() const { return m_framebufferResized; }

    // Does the compositor consider this window focused? Reported rather than
    // assumed, because a frame time measured on a background surface is a
    // measurement of the compositor's throttling and nothing else.
    // Show the window, once there is a frame in it. Idempotent; the first call
    // wins and the rest are free, so the frame loop can simply call it.
    void reveal();

    bool focused() const;
    void resetResizedFlag() { m_framebufferResized = false; }

    VkSurfaceKHR createSurface(VkInstance instance);

    using ResizeCallback = std::function<void(int, int)>;
    void setResizeCallback(ResizeCallback callback) { m_resizeCallback = callback; }

private:
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    bool m_revealed = false;
    GLFWwindow* m_window = nullptr;
    int m_width;
    int m_height;
    bool m_framebufferResized = false;
    ResizeCallback m_resizeCallback;
};

} // namespace eden
