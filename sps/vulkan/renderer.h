#pragma once

#include <cstdint>
#include <memory>
#include <sps/vulkan/instance.h>
#include <sps/vulkan/window.h>

namespace sps::vulkan
{
class VulkanRenderer
{
protected:
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
};
}
