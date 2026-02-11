#pragma once

#include <cstdint>
#include <memory>
#include <sps/vulkan/instance.h>
#include <sps/vulkan/window.h>

namespace sps::vulkan
{
class VulkanRenderer
{
  std::uint32_t m_window_width{ 0 };
  std::uint32_t m_window_height{ 0 };
  Window::Mode m_window_mode{ Window::Mode::WINDOWED };
  std::string m_window_title;

  std::unique_ptr<sps::vulkan::Window> m_window;
  std::unique_ptr<sps::vulkan::Instance> m_instance;

public:
  VulkanRenderer() = default;
  VulkanRenderer(const VulkanRenderer&) = delete;
  VulkanRenderer(VulkanRenderer&&) = delete;
  ~VulkanRenderer();

  VulkanRenderer& operator=(const VulkanRenderer&) = delete;
  VulkanRenderer& operator=(VulkanRenderer&&) = delete;

  void set_window_title(const std::string& title) { m_window_title = title; }
  void set_window_width(std::uint32_t w) { m_window_width = w; }
  void set_window_height(std::uint32_t h) { m_window_height = h; }
  void set_window_mode(Window::Mode mode) { m_window_mode = mode; }
  void set_window(std::unique_ptr<Window> window) { m_window = std::move(window); }
  void set_instance(std::unique_ptr<Instance> instance) { m_instance = std::move(instance); }

  [[nodiscard]] const std::string& window_title() const { return m_window_title; }
  [[nodiscard]] std::uint32_t window_width() const { return m_window_width; }
  [[nodiscard]] std::uint32_t window_height() const { return m_window_height; }
  [[nodiscard]] Window::Mode window_mode() const { return m_window_mode; }
};
}
