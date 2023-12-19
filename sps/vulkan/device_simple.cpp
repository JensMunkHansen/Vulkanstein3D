#include <sps/vulkan/device_simple.h>

#include <spdlog/spdlog.h>

namespace sps::vulkan
{

vk::Device create_logical_device(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface)
{
  /*
   * Create an abstraction around the GPU
   */

  /*
   * At time of creation, any required queues will also be created,
   * so queue create info must be passed in.
   */

  QueueFamilyIndices indices = findQueueFamilies(physicalDevice, surface);
  std::vector<uint32_t> uniqueIndices;
  uniqueIndices.push_back(indices.graphicsFamily.value());
  if (indices.graphicsFamily.value() != indices.presentFamily.value())
  {
    uniqueIndices.push_back(indices.presentFamily.value());
  }
  /*
   * VULKAN_HPP_CONSTEXPR DeviceQueueCreateInfo( VULKAN_HPP_NAMESPACE::DeviceQueueCreateFlags flags_
   = {}, uint32_t                                     queueFamilyIndex_ = {}, uint32_t queueCount_
   = {}, const float * pQueuePriorities_ = {} ) VULKAN_HPP_NOEXCEPT
  */
  std::vector<vk::DeviceQueueCreateInfo> queueCreateInfo;
  float queuePriority = 1.0f;
  for (uint32_t queueFamilyIndex : uniqueIndices)
  {
    queueCreateInfo.push_back(
      vk::DeviceQueueCreateInfo(vk::DeviceQueueCreateFlags(), queueFamilyIndex, 1, &queuePriority));
  }

  /*
   * Device features must be requested before the device is abstracted,
   * therefore we only pay for what we need.
   */

  vk::PhysicalDeviceFeatures deviceFeatures = vk::PhysicalDeviceFeatures();

  /*
   * Device extensions to be requested:
   */
  std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

  /*
   * VULKAN_HPP_CONSTEXPR DeviceCreateInfo( VULKAN_HPP_NAMESPACE::DeviceCreateFlags flags_ = {},
   uint32_t                                queueCreateInfoCount_          = {},
   const VULKAN_HPP_NAMESPACE::DeviceQueueCreateInfo * pQueueCreateInfos_ = {},
   uint32_t                                            enabledLayerCount_ = {},
   const char * const * ppEnabledLayerNames_                              = {},
   uint32_t             enabledExtensionCount_                            = {},
   const char * const * ppEnabledExtensionNames_                          = {},
   const VULKAN_HPP_NAMESPACE::PhysicalDeviceFeatures * pEnabledFeatures_ = {} )
  */
  std::vector<const char*> enabledLayers;

#ifdef SPS_DEBUG
  enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif

  vk::DeviceCreateInfo deviceInfo = vk::DeviceCreateInfo(vk::DeviceCreateFlags(),
    queueCreateInfo.size(), queueCreateInfo.data(), enabledLayers.size(), enabledLayers.data(),
    deviceExtensions.size(), deviceExtensions.data(), &deviceFeatures);

  try
  {
    vk::Device device = physicalDevice.createDevice(deviceInfo);
    spdlog::trace("GPU has been successfully abstracted!");
    return device;
  }
  catch (vk::SystemError err)
  {
    spdlog::trace("Device creation failed!");
    return nullptr;
  }
  return nullptr;
}
}
