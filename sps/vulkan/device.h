#pragma once

#include <vulkan/vulkan.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#include <spdlog/spdlog.h>

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
public:
  Device(const Instance& inst, vk::SurfaceKHR surface, bool prefer_distinct_transfer_queue,
    vk::PhysicalDevice physical_device, std::span<const char*> required_extensions,
    const vk::PhysicalDeviceFeatures& required_features,
    const vk::PhysicalDeviceFeatures& optional_features = {});

  Device(const Device&) = delete;
  Device(Device&&) noexcept;
  ~Device();

  Device& operator=(const Device&) = delete;
  Device& operator=(Device&&) = delete;

  [[nodiscard]] vk::Device device() const { return m_device; }
  [[nodiscard]] vk::PhysicalDevice physicalDevice() const { return m_physical_device; }
  void wait_idle() const;

  vk::SurfaceCapabilitiesKHR surfaceCapabilities(const vk::SurfaceKHR& surface) const;

  void create_semaphore(const vk::SemaphoreCreateInfo& semaphoreCreateInfo,
    vk::Semaphore* pSemaphore, const std::string& name) const;

  void set_debug_marker_name(
    void* object, vk::DebugReportObjectTypeEXT object_type, const std::string& name) const;

private:
  mutable std::mutex m_mutex;
  //  mutable std::vector<std::unique_ptr<CommandPool>> m_cmd_pools;

  vk::Device m_device{ VK_NULL_HANDLE };
  vk::PhysicalDevice m_physical_device{ VK_NULL_HANDLE };

  // Debug markers
  PFN_vkDebugMarkerSetObjectTagEXT m_vk_debug_marker_set_object_tag{ nullptr };
  PFN_vkDebugMarkerSetObjectNameEXT m_vk_debug_marker_set_object_name{ nullptr };
  PFN_vkCmdDebugMarkerBeginEXT m_vk_cmd_debug_marker_begin{ nullptr };
  PFN_vkCmdDebugMarkerEndEXT m_vk_cmd_debug_marker_end{ nullptr };
  PFN_vkCmdDebugMarkerInsertEXT m_vk_cmd_debug_marker_insert{ nullptr };
  PFN_vkSetDebugUtilsObjectNameEXT m_vk_set_debug_utils_object_name{ nullptr };
};
}
