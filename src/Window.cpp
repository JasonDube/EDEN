#include <eden/Window.hpp>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <cstdlib>
#include <cstdio>

namespace eden {

Window::Window(int width, int height, const std::string& title)
    : m_width(width), m_height(height)
{
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    // Born hidden. Revealed by the first frame that has something in it.
    //
    // Startup is four seconds -- three of them the default level -- and the window
    // used to exist for all of it with nothing drawn in it. A maximized, empty,
    // focused surface is something a tiling compositor will happily place against
    // your other windows, which is exactly what it looked like: a split screen on
    // launch. Nothing was wrong with the render; there simply was not one yet.
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    // What the displays actually are, said once.
    //
    // FIFO waits for a vertical blank, so the refresh rate IS the frame budget --
    // and "why am I not getting sixty" is unanswerable until you know whether the
    // panel does sixty. Printed for every monitor, because with two outputs the
    // one your window lands on decides your ceiling.
    {
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        for (int i = 0; i < count; ++i) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (!mode) continue;
            std::printf("[display] %s%s: %dx%d @ %d Hz%s\n",
                        glfwGetMonitorName(monitors[i]) ? glfwGetMonitorName(monitors[i]) : "?",
                        monitors[i] == glfwGetPrimaryMonitor() ? " (primary)" : "",
                        mode->width, mode->height, mode->refreshRate,
                        mode->refreshRate > 0 && mode->refreshRate < 58
                            ? "   <- under 60; FIFO can never beat this" : "");
        }
        std::fflush(stdout);
    }

    // EDEN_FULLSCREEN=1 asks for a real fullscreen window on the primary monitor.
    //
    // Not a cosmetic option. A windowed surface is composited: the compositor takes
    // your buffer, draws it into a larger scene, and hands THAT to the display. A
    // fullscreen surface exactly matching the mode can instead be handed straight to
    // the display -- direct scanout -- which skips the composite and the latency that
    // comes with it. Whether it happens is the compositor's decision, not ours, so
    // this is a thing to MEASURE rather than to assume.
    GLFWmonitor* monitor = nullptr;
    const char* wantFull = std::getenv("EDEN_FULLSCREEN");
    if (wantFull && wantFull[0] == '1') {
        monitor = glfwGetPrimaryMonitor();
        if (monitor) {
            // The monitor's own mode, so the surface matches the display exactly.
            // A fullscreen window at the wrong size has to be scaled, and a scaled
            // surface is one no compositor will scan out directly.
            if (const GLFWvidmode* mode = glfwGetVideoMode(monitor)) {
                width = mode->width;
                height = mode->height;
                glfwWindowHint(GLFW_RED_BITS, mode->redBits);
                glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
                glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
                glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
            }
        }
    }

    m_window = glfwCreateWindow(width, height, title.c_str(), monitor, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    // The maximized window won't match the requested size
    glfwGetWindowSize(m_window, &m_width, &m_height);

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
}

void Window::reveal() {
    if (!m_window || m_revealed) return;
    m_revealed = true;
    glfwShowWindow(m_window);

    // Focus goes with the reveal, not with the creation.
    //
    // Wayland compositors throttle unfocused surfaces, so a performance number
    // taken on a background window measures the throttling rather than the
    // program -- that is why this is asked for at all. Whether it is granted is
    // the compositor's business, which is what `focused()` is for.
    glfwFocusWindow(m_window);
}

bool Window::focused() const {
    return m_window && glfwGetWindowAttrib(m_window, GLFW_FOCUSED) == GLFW_TRUE;
}

Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::close() {
    glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void Window::pollEvents() {
    glfwPollEvents();
}

VkSurfaceKHR Window::createSurface(VkInstance instance) {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, m_window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface");
    }
    return surface;
}

void Window::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto* app = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    app->m_framebufferResized = true;
    app->m_width = width;
    app->m_height = height;
    if (app->m_resizeCallback) {
        app->m_resizeCallback(width, height);
    }
}

} // namespace eden
