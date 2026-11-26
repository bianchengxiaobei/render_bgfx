#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

struct GLFWwindow;

namespace hbg {

struct WindowKeyEvent {
    int key = 0;
    bool down = false;
    bool repeat = false;
};

struct WindowDesc {
    int width = 1280;
    int height = 720;
    std::string_view title = "hdrp-bgfx";
    bool resizable = true;
};

struct WindowInput {
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    bool mouseDown[3]{};
    float scrollX = 0.0f;
    float scrollY = 0.0f;
    bool keyF1 = false;
    bool keyF11 = false;
    bool keyF12 = false;
    bool keyShift = false;
    bool keyCtrl = false;
    bool keyAlt = false;
    std::vector<WindowKeyEvent> keyEvents;
    std::vector<std::uint32_t> chars;
};

class Window {
  public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool init(const WindowDesc& desc);
    void shutdown();

    bool pollEvents();
    bool shouldClose() const;
    void setShouldClose(bool shouldClose);
    void waitEvents(double seconds);
    bool takeResize();
    void sampleInput(WindowInput& input);

    void* nativeHandle() const;

    int width() const { return m_width; }
    int height() const { return m_height; }

  private:
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void scrollCallback(GLFWwindow* window, double xOffset, double yOffset);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void charCallback(GLFWwindow* window, unsigned int codepoint);

    GLFWwindow* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_resized = false;
    bool m_glfwInitialized = false;
    double m_pendingScrollX = 0.0;
    double m_pendingScrollY = 0.0;
    std::vector<WindowKeyEvent> m_pendingKeyEvents;
    std::vector<std::uint32_t> m_pendingChars;
};

} // namespace hbg
