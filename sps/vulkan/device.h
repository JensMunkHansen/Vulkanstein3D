#pragma once

#include <vulkan/vulkan.hpp>

namespace sps::vulkan
{
class Instance;

/*
 * Vulkan separates the concept of physical and logical devices.
 *
 * A physical device usually represents a single complete
 * implementation of Vulkan (excluding instance-level functionality)
 * available to the host, of which there are a finite number.
 *
 * A logical device represents an instance of that implementation
 * with its own state and resources independent of other logical devices.
 */
class Device
{
private:
  vk::Device m_device{ VK_NULL_HANDLE };
  vk::PhysicalDevice m_physical_device{ VK_NULL_HANDLE };
};
}
