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

class Instance;

struct DeviceInfo
{
  std::string name;
  vk::PhysicalDevice physical_device{ nullptr };
  vk::PhysicalDeviceType type{ VK_PHYSICAL_DEVICE_TYPE_OTHER };
  vk::DeviceSize total_device_local{ 0 };
  vk::PhysicalDeviceFeatures features{};
  std::vector<vk::ExtensionProperties> extensions;
  bool presentation_supported{ false };
  bool swapchain_supported{ false };
};

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
  /// Find a queue family index that suits a specific criteria
  /// @param criteria_lambda The lambda to sort out unsuitable queue families
  /// @return The queue family index which was found (if any), ``std::nullopt`` otherwise
  std::optional<std::uint32_t> find_queue_family_index_if(
    const std::function<bool(std::uint32_t index, const vk::QueueFamilyProperties&)>&
      criteria_lambda);

  bool is_presentation_supported(
    const vk::SurfaceKHR& surface, const std::uint32_t queue_family_index) const;

  static vk::PhysicalDevice pick_best_physical_device( //
    const Instance& inst, const vk::SurfaceKHR& surface,
    const vk::PhysicalDeviceFeatures& required_features,
    std::span<const char*> required_extensions);

  static vk::PhysicalDevice pick_best_physical_device(
    std::vector<DeviceInfo>&& physical_device_infos,
    const vk::PhysicalDeviceFeatures& required_features,
    const std::span<const char*> required_extensions);

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

  [[nodiscard]] const std::string& gpu_name() const { return m_gpu_name; }

  void wait_idle() const;

  vk::SurfaceCapabilitiesKHR surfaceCapabilities(const vk::SurfaceKHR& surface) const;

  void create_semaphore(const vk::SemaphoreCreateInfo& semaphoreCreateInfo,
    vk::Semaphore* pSemaphore, const std::string& name) const;

  void create_fence(
    const vk::FenceCreateInfo& fenceCreateInfor, vk::Fence* pFence, const std::string& name) const;

  void create_image_view(const vk::ImageViewCreateInfo& image_view_ci, vk::ImageView* image_view,
    const std::string& name) const;

  void set_debug_marker_name(
    void* object, vk::DebugReportObjectTypeEXT object_type, const std::string& name) const;

private:
  //  mutable std::vector<std::unique_ptr<CommandPool>> m_cmd_pools;

  vk::Device m_device{ VK_NULL_HANDLE };
  vk::PhysicalDevice m_physical_device{ VK_NULL_HANDLE };
  std::string m_gpu_name;

  vk::PhysicalDeviceFeatures m_enabled_features{};

  vk::Queue m_graphics_queue{ VK_NULL_HANDLE };
  vk::Queue m_present_queue{ VK_NULL_HANDLE };
  vk::Queue m_transfer_queue{ VK_NULL_HANDLE };

public:
  std::uint32_t m_present_queue_family_index{ 0 };
  std::uint32_t m_graphics_queue_family_index{ 0 };
  std::uint32_t m_transfer_queue_family_index{ 0 };

private:
  mutable std::vector<std::unique_ptr<vk::CommandPool>> m_cmd_pools;
  mutable std::mutex m_mutex;

  vk::DispatchLoaderDynamic m_dldi;

  // Debug markers
  PFN_vkDebugMarkerSetObjectTagEXT m_vk_debug_marker_set_object_tag{ nullptr };
  PFN_vkDebugMarkerSetObjectNameEXT m_vk_debug_marker_set_object_name{ nullptr };
  PFN_vkCmdDebugMarkerBeginEXT m_vk_cmd_debug_marker_begin{ nullptr };
  PFN_vkCmdDebugMarkerEndEXT m_vk_cmd_debug_marker_end{ nullptr };
  PFN_vkCmdDebugMarkerInsertEXT m_vk_cmd_debug_marker_insert{ nullptr };
  PFN_vkSetDebugUtilsObjectNameEXT m_vk_set_debug_utils_object_name{ nullptr };
};
}
