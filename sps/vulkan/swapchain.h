#pragma once

#include <spdlog/spdlog.h>
#include <vulkan/vulkan.hpp>

#include <sps/vulkan/device.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

namespace sps::vulkan
{

/**
        Holds properties of the swapchain
        capabilities: no. of images and supported sizes
        formats: eg. supported pixel formats
        present modes: available presentation modes (eg. double buffer, fifo, mailbox)
*/
struct SwapChainSupportDetails
{
  vk::SurfaceCapabilitiesKHR capabilities;
  std::vector<vk::SurfaceFormatKHR> formats;
  std::vector<vk::PresentModeKHR> presentModes;
};

/**
        Various data structures associated with the swapchain.
*/
struct SwapChainBundle
{
  vk::SwapchainKHR swapchain;
  std::vector<vk::Image> images;
  vk::Format format;
  vk::Extent2D extent;
};

#if 0
  for (vkUtil::SwapChainFrame& frame : swapchainFrames)
  {
    frame.destroy();
  }
  device.destroySwapchainKHR(swapchain);

  device.destroyDescriptorPool(frameDescriptorPool);
#endif

class Device;
class Semaphore;

class Swapchain
{
private:
  Device& m_device;

  // FIXME;
  SwapChainBundle m_swapchain; //{ VK_NULL_HANDLE };
  VkSurfaceKHR m_surface{ VK_NULL_HANDLE };
  std::optional<vk::SurfaceFormatKHR> m_surface_format{};
  // images
  // image views
  vk::Extent2D m_extent{};
  //  std::unique_ptr<Semaphore> m_img_available;
  [[nodiscard]] std::vector<vk::Image> get_swapchain_images();
  bool m_vsync_enabled{ false };

  void setup_swapchain(
    const std::uint32_t width, const std::uint32_t height, const bool vsync_enabled);

public:
  Swapchain(Device& device, VkSurfaceKHR surface, std::uint32_t width, std::uint32_t height,
    bool vsync_enabled);

  Swapchain(const Swapchain&) = delete;
  Swapchain(Swapchain&&) noexcept;

  ~Swapchain();

  Swapchain& operator=(const Swapchain&) = delete;
  Swapchain& operator=(Swapchain&&) = delete;
};
};