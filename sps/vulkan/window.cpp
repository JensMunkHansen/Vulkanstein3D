#include <cassert>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include <sps/vulkan/window.h>

namespace sps::vulkan
{

Window::Window(const std::string& title, const std::uint32_t width, const std::uint32_t height,
  const bool visible, const bool resizable, const Mode mode)
  : m_width(width)
  , m_height(height)
  , m_mode(mode)
{
  assert(!title.empty());

  // Force X11 platform for NVIDIA PRIME compatibility
  // Can be controlled via environment variable VULK3D_USE_X11=1
  if (const char* use_x11 = std::getenv("VULK3D_USE_X11"); use_x11 && std::string(use_x11) == "1")
  {
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    spdlog::trace("Forcing X11 platform");
  }

  if (glfwInit() != GLFW_TRUE)
  {
    throw std::runtime_error("Failed to initialise GLFW!");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
  glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);

  spdlog::trace("Creating window");

  GLFWmonitor* monitor = nullptr;
  if (m_mode != Mode::WINDOWED)
  {
    monitor = glfwGetPrimaryMonitor();
    if (m_mode == Mode::WINDOWED_FULLSCREEN)
    {
      const auto* video_mode = glfwGetVideoMode(monitor);
      m_width = video_mode->width;
      m_height = video_mode->height;
    }
  }

  m_window = glfwCreateWindow(
    static_cast<int>(width), static_cast<int>(height), title.c_str(), monitor, nullptr);

  if (m_window == nullptr)
  {
    throw std::runtime_error("Error: glfwCreateWindow failed for window " + title + " !");
  }
}

Window::~Window()
{
  glfwDestroyWindow(m_window);
  glfwTerminate();
}
bool Window::should_close()
{
  return glfwWindowShouldClose(m_window) == GLFW_TRUE;
}
void Window::poll()
{
  glfwPollEvents();
}

}
