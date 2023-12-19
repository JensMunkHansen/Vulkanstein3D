#pragma once

#include <vulkan/vulkan.hpp>

#include <optional>

namespace sps::vulkan
{

struct QueueFamilyIndices
{
  std::optional<uint32_t> graphicsFamily;
  std::optional<uint32_t> presentFamily;

  /**
     \returns whether all of the Queue family indices have been set.
  */
  bool isComplete() { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface);

vk::Device create_logical_device(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface);

}
