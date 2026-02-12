#pragma once

#include <sps/vulkan/depth_stencil_attachment.h>
#include <sps/vulkan/device.h>
#include <sps/vulkan/fence.h>
#include <sps/vulkan/instance.h>
#include <sps/vulkan/screenshot.h>
#include <sps/vulkan/semaphore.h>
#include <sps/vulkan/swapchain.h>
#include <sps/vulkan/window.h>
#include <sps/vulkan/windowsurface.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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

  vk::SampleCountFlagBits msaa_samples{ vk::SampleCountFlagBits::e1 };
  vk::Format depth_format{ vk::Format::eD32SfloatS8Uint };
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

  // Depth-stencil
  [[nodiscard]] DepthStencilAttachment& depth_stencil() { return *m_depth_stencil; }
  [[nodiscard]] const DepthStencilAttachment& depth_stencil() const { return *m_depth_stencil; }
  [[nodiscard]] vk::Format depth_format() const { return m_depth_format; }
  void recreate_depth_resources();

  // HDR offscreen + MSAA color
  [[nodiscard]] vk::Image hdr_image() const { return m_hdr_image; }
  [[nodiscard]] vk::Image* hdr_image_ptr() { return &m_hdr_image; }
  [[nodiscard]] vk::ImageView hdr_image_view() const { return m_hdr_image_view; }
  [[nodiscard]] vk::Sampler hdr_sampler() const { return m_hdr_sampler; }
  [[nodiscard]] vk::ImageView hdr_msaa_image_view() const { return m_hdr_msaa_image_view; }
  [[nodiscard]] vk::Format hdr_format() const { return m_hdr_format; }
  void recreate_hdr_resources();

  // MSAA
  [[nodiscard]] vk::SampleCountFlagBits msaa_samples() const { return m_msaa_samples; }

  // Screenshot
  bool save_screenshot(const std::string& filepath);

  // Command pool + sync objects
  [[nodiscard]] vk::CommandPool command_pool() const { return m_command_pool; }
  [[nodiscard]] vk::CommandBuffer main_command_buffer() const { return m_main_command_buffer; }
  [[nodiscard]] std::vector<vk::CommandBuffer>& command_buffers() { return m_command_buffers; }
  [[nodiscard]] Fence& in_flight() { return *m_in_flight; }
  [[nodiscard]] Semaphore& image_available() { return *m_image_available; }
  [[nodiscard]] Semaphore& render_finished(std::uint32_t index) { return *m_render_finished[index]; }

  /// Recreate per-swapchain-image semaphores (call after swapchain recreate).
  void recreate_sync_objects();

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
  vk::SampleCountFlagBits m_msaa_samples{ vk::SampleCountFlagBits::e1 };
  vk::Format m_depth_format{ vk::Format::eD32SfloatS8Uint };

  std::unique_ptr<sps::vulkan::Window> m_window;
  std::unique_ptr<sps::vulkan::Instance> m_instance;
  std::unique_ptr<sps::vulkan::WindowSurface> m_surface;
  std::unique_ptr<sps::vulkan::Device> m_device;
  std::unique_ptr<sps::vulkan::Swapchain> m_swapchain;

  // Command pool + sync objects
  vk::CommandPool m_command_pool;
  vk::CommandBuffer m_main_command_buffer;
  std::vector<vk::CommandBuffer> m_command_buffers;
  std::unique_ptr<Fence> m_in_flight;
  std::unique_ptr<Semaphore> m_image_available;
  std::vector<std::unique_ptr<Semaphore>> m_render_finished;

  void create_sync_objects();

  // Depth-stencil
  std::unique_ptr<DepthStencilAttachment> m_depth_stencil;
  void create_depth_resources();

  // HDR offscreen target (single-sample, for composite sampling)
  static constexpr vk::Format m_hdr_format = vk::Format::eR16G16B16A16Sfloat;
  vk::Image m_hdr_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_hdr_image_memory{ VK_NULL_HANDLE };
  vk::ImageView m_hdr_image_view{ VK_NULL_HANDLE };
  vk::Sampler m_hdr_sampler{ VK_NULL_HANDLE };

  // HDR MSAA color target (multi-sample, resolves to m_hdr_image)
  vk::Image m_hdr_msaa_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_hdr_msaa_image_memory{ VK_NULL_HANDLE };
  vk::ImageView m_hdr_msaa_image_view{ VK_NULL_HANDLE };

  void create_hdr_resources();
  void destroy_hdr_resources();
  void create_msaa_color_resources();
};
}
