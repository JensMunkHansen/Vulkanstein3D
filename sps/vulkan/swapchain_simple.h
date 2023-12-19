#pragma once

#include <sps/vulkan/config.h>

#include <vulkan/vulkan.hpp>

#include <optional>

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

SwapChainSupportDetails query_swapchain_support(
  vk::PhysicalDevice device, vk::SurfaceKHR surface, bool debug);
vk::SurfaceFormatKHR choose_swapchain_surface_format(std::vector<vk::SurfaceFormatKHR> formats);

/**
        Choose a present mode.

        \param presentModes a vector of present modes supported by the device
        \returns the chosen present mode
*/
vk::PresentModeKHR choose_swapchain_present_mode(std::vector<vk::PresentModeKHR> presentModes);

/**
        Choose an extent for the swapchain.

        \param width the requested width
        \param height the requested height
        \param capabilities a struct describing the supported capabilities of the device
        \returns the chosen extent
*/
vk::Extent2D choose_swapchain_extent(
  uint32_t width, uint32_t height, vk::SurfaceCapabilitiesKHR capabilities);

/**
        Create a swapchain

        \param logicalDevice the logical device
        \param physicalDevice the physical device
        \param surface the window surface to use the swapchain with
        \param width the requested width
        \param height the requested height
        \param debug whether the system is running in debug mode
        \returns a struct holding the swapchain and other associated data structures
*/
SwapChainBundle create_swapchain(vk::Device logicalDevice, vk::PhysicalDevice physicalDevice,
  vk::SurfaceKHR surface, int width, int height, bool debug);

}
