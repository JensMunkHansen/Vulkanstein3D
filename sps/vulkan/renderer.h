#pragma once

#include <sps/vulkan/device.h>
#include <sps/vulkan/instance.h>
#include <sps/vulkan/swapchain.h>
#include <sps/vulkan/window.h>
#include <sps/vulkan/windowsurface.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace sps::vulkan
{

struct RendererConfig
{
  std::string window_title{ "Vulkan renderer example" };
  std::uint32_t window_width{ 800 };
  std::uint32_t window_height{ 600 };
  Window::Mode window_mode{ Window::Mode::WINDOWED };
  bool resizable{ true };

  bool enable_validation{ true };
  bool enable_renderdoc{ false };
  bool vsync{ true };

  std::string preferred_gpu;
  std::optional<std::uint32_t> preferred_gpu_index;
  bool use_distinct_data_transfer_queue{ true };
};

class VulkanRenderer
{
public:
  VulkanRenderer() = default;
  explicit VulkanRenderer(const RendererConfig& config);
  VulkanRenderer(const VulkanRenderer&) = delete;
  VulkanRenderer(VulkanRenderer&&) = delete;
  ~VulkanRenderer();

  VulkanRenderer& operator=(const VulkanRenderer&) = delete;
  VulkanRenderer& operator=(VulkanRenderer&&) = delete;

  // Getters
  [[nodiscard]] Window& window() { return *m_window; }
  [[nodiscard]] const Window& window() const { return *m_window; }
  [[nodiscard]] Instance& instance() { return *m_instance; }
  [[nodiscard]] const Instance& instance() const { return *m_instance; }
  [[nodiscard]] Device& device() { return *m_device; }
  [[nodiscard]] const Device& device() const { return *m_device; }
  [[nodiscard]] WindowSurface& surface() { return *m_surface; }
  [[nodiscard]] const WindowSurface& surface() const { return *m_surface; }
  [[nodiscard]] Swapchain& swapchain() { return *m_swapchain; }
  [[nodiscard]] const Swapchain& swapchain() const { return *m_swapchain; }

  // Mutable access to config values stored here
  bool& vsync_enabled() { return m_vsync_enabled; }
  [[nodiscard]] bool vsync_enabled() const { return m_vsync_enabled; }

  std::uint32_t& window_width() { return m_window_width; }
  std::uint32_t& window_height() { return m_window_height; }
  Window::Mode& window_mode() { return m_window_mode; }
  std::string& window_title() { return m_window_title; }

private:
  std::uint32_t m_window_width{ 0 };
  std::uint32_t m_window_height{ 0 };
  Window::Mode m_window_mode{ Window::Mode::WINDOWED };
  std::string m_window_title;

  bool m_vsync_enabled{ true };

  std::unique_ptr<sps::vulkan::Window> m_window;
  std::unique_ptr<sps::vulkan::Instance> m_instance;
  std::unique_ptr<sps::vulkan::WindowSurface> m_surface;
  std::unique_ptr<sps::vulkan::Device> m_device;
  std::unique_ptr<sps::vulkan::Swapchain> m_swapchain;
};
}
