#include "app/window.h"

#include "core/log.h"

#include <GLFW/glfw3.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include <string>
#include <utility>

namespace hbg {

Window::~Window() {
    shutdown();
}

bool Window::init(const WindowDesc& desc) {
    glfwSetErrorCallback([](int code, const char* description) {
        HBG_LOGE("GLFW error {}: {}", code, description != nullptr ? description : "unknown");
    });

    if (!glfwInit()) {
        HBG_LOGE("glfwInit failed");
        return false;
    }
    m_glfwInitialized = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);

    const std::string title(desc.title);
    m_window = glfwCreateWindow(desc.width, desc.height, title.c_str(), nullptr, nullptr);
    if (m_window == nullptr) {
        HBG_LOGE("glfwCreateWindow failed");
        shutdown();
        return false;
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
    glfwSetScrollCallback(m_window, scrollCallback);
    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetCharCallback(m_window, charCallback);

    glfwGetFramebufferSize(m_window, &m_width, &m_height);
    m_resized = false;

    HBG_LOGI("window created: {}x{} (framebuffer {}x{})", desc.width, desc.height, m_width, m_height);
    return true;
}

void Window::shutdown() {
    if (m_window != nullptr) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    if (m_glfwInitialized) {
        glfwTerminate();
        m_glfwInitialized = false;
    }
    m_width = 0;
    m_height = 0;
    m_resized = false;
    m_pendingScrollX = 0.0;
    m_pendingScrollY = 0.0;
    m_pendingKeyEvents.clear();
    m_pendingChars.clear();
}

bool Window::pollEvents() {
    if (m_window == nullptr) {
        return false;
    }

    if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
    }

    glfwPollEvents();
    return glfwWindowShouldClose(m_window) == GLFW_FALSE;
}

void Window::waitEvents(double seconds) {
    if (m_window != nullptr) {
        glfwWaitEventsTimeout(seconds);
    }
}

bool Window::takeResize() {
    const bool resized = m_resized;
    m_resized = false;
    return resized;
}

void Window::sampleInput(WindowInput& input) {
    input = WindowInput{};
    if (m_window == nullptr) {
        return;
    }

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(m_window, &mouseX, &mouseY);
    input.mouseX = static_cast<float>(mouseX);
    input.mouseY = static_cast<float>(mouseY);

    for (int button = 0; button < 3; ++button) {
        input.mouseDown[button] = glfwGetMouseButton(m_window, button) == GLFW_PRESS;
    }

    input.scrollX = static_cast<float>(m_pendingScrollX);
    input.scrollY = static_cast<float>(m_pendingScrollY);
    m_pendingScrollX = 0.0;
    m_pendingScrollY = 0.0;

    input.keyF1 = glfwGetKey(m_window, GLFW_KEY_F1) == GLFW_PRESS;
    input.keyF11 = glfwGetKey(m_window, GLFW_KEY_F11) == GLFW_PRESS;
    input.keyF12 = glfwGetKey(m_window, GLFW_KEY_F12) == GLFW_PRESS;
    input.keyShift = glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                     glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    input.keyCtrl = glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                    glfwGetKey(m_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    input.keyAlt =
        glfwGetKey(m_window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(m_window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
    input.keyEvents = std::move(m_pendingKeyEvents);
    input.chars = std::move(m_pendingChars);
    m_pendingKeyEvents.clear();
    m_pendingChars.clear();
}

void* Window::nativeHandle() const {
#if defined(_WIN32)
    return m_window != nullptr ? static_cast<void*>(glfwGetWin32Window(m_window)) : nullptr;
#else
    return nullptr;
#endif
}

void Window::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self == nullptr) {
        return;
    }

    self->m_width = width;
    self->m_height = height;
    self->m_resized = true;
}

void Window::scrollCallback(GLFWwindow* window, double xOffset, double yOffset) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self == nullptr) {
        return;
    }

    self->m_pendingScrollX += xOffset;
    self->m_pendingScrollY += yOffset;
}

void Window::keyCallback(GLFWwindow* window, int key, int, int action, int) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self == nullptr || action == GLFW_REPEAT) {
        return;
    }
    self->m_pendingKeyEvents.push_back(WindowKeyEvent{key, action == GLFW_PRESS, false});
}

void Window::charCallback(GLFWwindow* window, unsigned int codepoint) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self == nullptr) {
        return;
    }
    self->m_pendingChars.push_back(static_cast<std::uint32_t>(codepoint));
}

} // namespace hbg
