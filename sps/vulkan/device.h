#pragma once
#include <sps/vulkan/config.h>

#include <sps/vulkan/vulkan.hpp>

namespace sps::vulkan
{
class Instance;
class Device
{
private:
  VkDevice m_device{ VK_NULL_HANDLE };
  VkPhysicalDevice m_physical_device{ VK_NULL_HANDLE };

public:
  Device(const Instance& instance, VkSurfaceKHR surface, VkPhysicalDevice physicalDevice);
  Device(const Device& other) = delete;
  Device(Device&&) noexcept;
  ~Device();
  Device& operator=(const Device&) = delete;
  Device& operator=(Device&&) = delete;

  [[nodiscard]] VkDevice device() const { return m_device; }
  [[nodiscard]] VkPhysicalDevice physical_device() const { return m_physical_device; }
};
}
