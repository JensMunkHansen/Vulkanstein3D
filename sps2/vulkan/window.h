#pragma once

#include <GLFW/glfw3.h>

#include <cstdint>
#include <string>

namespace sps::vulkan {
class Window {
public:
    enum class Mode { WINDOWED, FULLSCREEN, WINDOWED_FULLSCREEN };
    
private:
    std::uint32_t m_width;
    std::uint32_t m_height;
    Mode m_mode;
    GLFWwindow *m_window{nullptr};
 public:
    Window(const std::string &title, std::uint32_t width, std::uint32_t height, bool visible, bool resizable,
           Mode mode);
    Window(const Window &) = delete;
    Window(Window &&) = delete;
    ~Window();

    Window &operator=(const Window &) = delete;
    Window &operator=(Window &&) = delete;

    [[nodiscard]] GLFWwindow *get() const {
        return m_window;
    }

    [[nodiscard]] std::uint32_t width() const {
        return m_width;
    }

    [[nodiscard]] std::uint32_t height() const {
        return m_height;
    }

    [[nodiscard]] Mode mode() const {
        return m_mode;
    }    
};   
}
