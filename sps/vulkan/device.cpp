#include <sps/vulkan/device.h>
#include <sps/vulkan/exception.h>
#include <sps/vulkan/instance.h>

#include <optional>
#include <set>
#include <span>
#include <string>

#include <spdlog/spdlog.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

namespace sps::vulkan
{
constexpr float DEFAULT_QUEUE_PRIORITY = 1.0f;

QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface)
{
  QueueFamilyIndices indices;

  std::vector<vk::QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();

  spdlog::trace("There are {} queue families available on the system.", queueFamilies.size());

  int i = 0;
  for (vk::QueueFamilyProperties queueFamily : queueFamilies)
  {

    /*
    * // Provided by VK_VERSION_1_0
            typedef struct VkQueueFamilyProperties {
            VkQueueFlags    queueFlags;
            uint32_t        queueCount;
            uint32_t        timestampValidBits;
            VkExtent3D      minImageTransferGranularity;
            } VkQueueFamilyProperties;

            queueFlags is a bitmask of VkQueueFlagBits indicating
            capabilities of the queues in this queue family.

            queueCount is the unsigned integer count of queues in this
            queue family. Each queue family must support at least one queue.

            timestampValidBits is the unsigned integer count of
            meaningful bits in the timestamps written via
            vkCmdWriteTimestamp. The valid range for the count is 36..64 bits,
            or a value of 0, indicating no support for timestamps. Bits
            outside the valid range are guaranteed to be zeros.

            minImageTransferGranularity is the minimum granularity
            supported for image transfer operations on the queues in
            this queue family.
    */

    /*
    * // Provided by VK_VERSION_1_0
            typedef enum VkQueueFlagBits {
            VK_QUEUE_GRAPHICS_BIT = 0x00000001,
            VK_QUEUE_COMPUTE_BIT = 0x00000002,
            VK_QUEUE_TRANSFER_BIT = 0x00000004,
            VK_QUEUE_SPARSE_BINDING_BIT = 0x00000008,
            } VkQueueFlagBits;
    */

    if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)
    {
      indices.graphicsFamily = i;
      indices.presentFamily = i;

      spdlog::trace("Queue Family {} is suitable for graphics and presenting", i);
    }
    if (device.getSurfaceSupportKHR(i, surface))
    {
      indices.presentFamily = i;

      spdlog::trace("Queue Family {} is suitable for presenting", i);
    }

    if (indices.isComplete())
    {
      break;
    }

    i++;
  }

  return indices;
}

/// Check if a device extension is supported by a physical device
/// @param extensions The device extensions
/// @note If extensions is empty, this function returns ``false``
/// @param extension_name The extension name
/// @return ``true`` if the required device extension is supported
bool is_extension_supported(
  const std::vector<VkExtensionProperties>& extensions, const std::string& extension_name)
{
  return std::find_if(extensions.begin(), extensions.end(),
           [&](const VkExtensionProperties extension)
           { return extension.extensionName == extension_name; }) != extensions.end();
}

void log_device_properties(const vk::PhysicalDevice& device)
{
  /*
  * void vkGetPhysicalDeviceProperties(
          VkPhysicalDevice                            physicalDevice,
          VkPhysicalDeviceProperties*                 pProperties);
  */

  vk::PhysicalDeviceProperties properties = device.getProperties();

  /*
  * typedef struct VkPhysicalDeviceProperties {
          uint32_t                            apiVersion;
          uint32_t                            driverVersion;
          uint32_t                            vendorID;
          uint32_t                            deviceID;
          VkPhysicalDeviceType                deviceType;
          char                                deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
          uint8_t                             pipelineCacheUUID[VK_UUID_SIZE];
          VkPhysicalDeviceLimits              limits;
          VkPhysicalDeviceSparseProperties    sparseProperties;
          } VkPhysicalDeviceProperties;
  */

  spdlog::trace("Device name: {}", properties.deviceName);

  spdlog::trace("Device type: ");
  switch (properties.deviceType)
  {

    case (vk::PhysicalDeviceType::eCpu):
      spdlog::trace("CPU");
      break;

    case (vk::PhysicalDeviceType::eDiscreteGpu):
      spdlog::trace("Discrete GPU");
      break;

    case (vk::PhysicalDeviceType::eIntegratedGpu):
      spdlog::trace("Integrated GPU");
      break;

    case (vk::PhysicalDeviceType::eVirtualGpu):
      spdlog::trace("Virtual GPU");
      break;

    default:
      spdlog::trace("Other");
  }
}

bool checkDeviceExtensionSupport(
  const vk::PhysicalDevice& device, const std::vector<const char*>& requestedExtensions)
{

  /*
   * Check if a given physical device can satisfy a list of requested device
   * extensions.
   */

  std::set<std::string> requiredExtensions(requestedExtensions.begin(), requestedExtensions.end());

  spdlog::trace("Device can support extensions:");

  for (vk::ExtensionProperties& extension : device.enumerateDeviceExtensionProperties())
  {

    spdlog::trace("\t\"{}\"", extension.extensionName);

    // remove this from the list of required extensions (set checks for equality automatically)
    requiredExtensions.erase(extension.extensionName);
  }

  // if the set is empty then all requirements have been satisfied
  return requiredExtensions.empty();
}

bool is_device_suitable(const vk::PhysicalDevice& device)
{
  spdlog::trace("Checking if device is suitable");

  // TODO: Accept requirements from outside
  const std::vector<const char*> requestedExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_EXT_DEBUG_MARKER_EXTENSION_NAME };

  spdlog::trace("We are requesting device extensions:");

  for (const char* extension : requestedExtensions)
  {
    spdlog::trace("\t\"{}\"", extension);

    if (bool extensionsSupported = checkDeviceExtensionSupport(device, requestedExtensions))
    {

      spdlog::trace("Device can support the requested extensions!");
    }
    else
    {
      spdlog::trace("Device can't support the requested extensions!");

      return false;
    }
    return true;
  }
  return false;
}

vk::PhysicalDevice choose_physical_device(const vk::Instance& instance)
{

  /*
   * Choose a suitable physical device from a list of candidates.
   * Note: Physical devices are neither created nor destroyed, they exist
   * independently to the program.
   */

  spdlog::trace("Choosing Physical Device");

  /*
  * ResultValueType<std::vector<PhysicalDevice, PhysicalDeviceAllocator>>::type
          Instance::enumeratePhysicalDevices( Dispatch const & d )

    std::vector<vk::PhysicalDevice> instance.enumeratePhysicalDevices( Dispatch const & d =
  static/default )
  */
  std::vector<vk::PhysicalDevice> availableDevices = instance.enumeratePhysicalDevices();

  spdlog::trace("There are {} physical devices available on this system", availableDevices.size());

  /*
   * check if a suitable device can be found
   */
  for (vk::PhysicalDevice device : availableDevices)
  {
    if (spdlog::get_level() == spdlog::level::trace)
    {
      log_device_properties(device);
    }
    if (is_device_suitable(device))
    {
      return device;
    }
  }
  return nullptr;
}

vk::Device create_logical_device(vk::PhysicalDevice physicalDevice)
{

  /*
   * Create an abstraction around the GPU
   */

  /*
   * At time of creation, any required queues will also be created,
   * so queue create info must be passed in.
   */

  QueueFamilyIndices indices = findQueueFamilies(physicalDevice, vk::SurfaceKHR{});
  float queuePriority = 1.0f;
  /*
  * VULKAN_HPP_CONSTEXPR DeviceQueueCreateInfo( VULKAN_HPP_NAMESPACE::DeviceQueueCreateFlags flags_
  = {},
  uint32_t queueFamilyIndex_ = {},
  uint32_t queueCount_ = {},
  const float * pQueuePriorities_ = {} ) VULKAN_HPP_NOEXCEPT
  */
  vk::DeviceQueueCreateInfo queueCreateInfo = vk::DeviceQueueCreateInfo(
    vk::DeviceQueueCreateFlags(), indices.graphicsFamily.value(), 1, &queuePriority);

  /*
   * Device features must be requested before the device is abstracted,
   * therefore we only pay for what we need.
   */

  vk::PhysicalDeviceFeatures deviceFeatures = vk::PhysicalDeviceFeatures();

  /*
  * VULKAN_HPP_CONSTEXPR DeviceCreateInfo( VULKAN_HPP_NAMESPACE::DeviceCreateFlags flags_ = {},
  uint32_t                                queueCreateInfoCount_ = {},
  const VULKAN_HPP_NAMESPACE::DeviceQueueCreateInfo * pQueueCreateInfos_
  = {},
  uint32_t                                            enabledLayerCount_ = {},
  const char *
  const * ppEnabledLayerNames_                              = {},
  uint32_t enabledExtensionCount_ = {},
  const char * const * ppEnabledExtensionNames_ = {}, const

  VULKAN_HPP_NAMESPACE::PhysicalDeviceFeatures * pEnabledFeatures_ = {} )
  */
  std::vector<const char*> enabledLayers;
#ifdef SPS_DEBUG
  enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif
  vk::DeviceCreateInfo deviceInfo = vk::DeviceCreateInfo(vk::DeviceCreateFlags(), 1,
    &queueCreateInfo, enabledLayers.size(), enabledLayers.data(), 0, nullptr, &deviceFeatures);

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

vk::Queue get_queue(vk::PhysicalDevice physicalDevice, vk::Device device)
{

  QueueFamilyIndices indices = findQueueFamilies(physicalDevice, vk::SurfaceKHR{});

  return device.getQueue(indices.graphicsFamily.value(), 0);
}

Device::Device(const Instance& inst, vk::SurfaceKHR surface, bool prefer_distinct_transfer_queue,
  vk::PhysicalDevice physical_device, std::span<const char*> required_extensions,
  const vk::PhysicalDeviceFeatures& required_features,
  const vk::PhysicalDeviceFeatures& optional_features)
{
  // No need to clean this up - verify
  vk::PhysicalDevice physicalDevice = choose_physical_device(inst.instance());

  m_device = create_logical_device(physicalDevice);

  const bool enable_debug_markers =
    std::find_if(required_extensions.begin(), required_extensions.end(),
      [&](const char* extension) {
        return std::string(extension) == std::string(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
      }) != required_extensions.end();

#ifdef SPS_DEBUG
  spdlog::trace("debug markers enabled {}", enable_debug_markers);

  if (enable_debug_markers)
  {
    spdlog::trace("Initializing Vulkan debug markers");
    // The debug marker extension is not part of the core, so function pointers need to be loaded
    // manually.
    m_vk_debug_marker_set_object_tag = reinterpret_cast<PFN_vkDebugMarkerSetObjectTagEXT>( // NOLINT
      vkGetDeviceProcAddr(m_device, "vkDebugMarkerSetObjectTagEXT"));
    assert(m_vk_debug_marker_set_object_tag);

    m_vk_debug_marker_set_object_name =
      reinterpret_cast<PFN_vkDebugMarkerSetObjectNameEXT>( // NOLINT
        vkGetDeviceProcAddr(m_device, "vkDebugMarkerSetObjectNameEXT"));
    assert(m_vk_debug_marker_set_object_name);

    m_vk_cmd_debug_marker_begin = reinterpret_cast<PFN_vkCmdDebugMarkerBeginEXT>( // NOLINT
      vkGetDeviceProcAddr(m_device, "vkCmdDebugMarkerBeginEXT"));
    assert(m_vk_cmd_debug_marker_begin);

    m_vk_cmd_debug_marker_end = reinterpret_cast<PFN_vkCmdDebugMarkerEndEXT>( // NOLINT
      vkGetDeviceProcAddr(m_device, "vkCmdDebugMarkerEndEXT"));
    assert(m_vk_cmd_debug_marker_end);

    m_vk_cmd_debug_marker_insert = reinterpret_cast<PFN_vkCmdDebugMarkerInsertEXT>( // NOLINT
      vkGetDeviceProcAddr(m_device, "vkCmdDebugMarkerInsertEXT"));
    assert(m_vk_cmd_debug_marker_insert);

    m_vk_set_debug_utils_object_name = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>( // NOLINT
      vkGetDeviceProcAddr(m_device, "vkSetDebugUtilsObjectNameEXT"));
    assert(m_vk_set_debug_utils_object_name);
  }
#endif

  // Create queues
  // TODO(clean up)
  auto graphicsQueue = get_queue(physicalDevice, m_device);
}

Device::~Device()
{
  std::scoped_lock locker(m_mutex);

  // Because the device handle must be valid for the destruction of the command pools in the
  // CommandPool destructor, we must destroy the command pools manually here in order to ensure
  // the right order of destruction m_cmd_pools.clear();

  // Now that we destroyed the command pools, we can destroy the allocator and finally the device
  // itself
  // vmaDestroyAllocator(m_allocator);
  vkDestroyDevice(m_device, nullptr);
}

vk::SurfaceCapabilitiesKHR Device::surfaceCapabilities(const vk::SurfaceKHR& surface) const
{
  // TODO: May throw
  return m_physical_device.getSurfaceCapabilitiesKHR(surface);
}

void Device::wait_idle() const
{
  try
  {
    m_device.waitIdle();
  }
  catch (vk::SystemError err)
  {
    spdlog::trace("wait_idle: {}", err.what());
    throw;
  }
}

void Device::create_semaphore(const vk::SemaphoreCreateInfo& semaphoreCreateInfo,
  vk::Semaphore* pSemaphore, const std::string& name) const
{
  try
  {
    *pSemaphore = m_device.createSemaphore(semaphoreCreateInfo);
    set_debug_marker_name(pSemaphore, vk::DebugReportObjectTypeEXT::eSemaphore, name);
  }
  catch (vk::SystemError err)
  {
    spdlog::trace("Failed to create semaphore");
    pSemaphore = nullptr;
    throw;
  }
}

void Device::set_debug_marker_name(
  void* object, vk::DebugReportObjectTypeEXT object_type, const std::string& name) const
{
#ifdef SPS_DEBUG
  if (m_vk_debug_marker_set_object_name == nullptr)
  {
    return;
  }

  assert(object);
  assert(!name.empty());
  assert(m_vk_debug_marker_set_object_name);

  const vk::DebugMarkerObjectNameInfoEXT nameInfo =
    vk::DebugMarkerObjectNameInfoEXT()
      .setObjectType(object_type)
      .setObject(reinterpret_cast<std::uint64_t>(object))
      .setPObjectName(name.c_str());
#if 0
  // TODO: Fixed undefined reference
  if (const auto result = m_device.debugMarkerSetObjectNameEXT(&nameInfo);
      result != vk::Result::eSuccess)
  {
    throw VulkanException("Failed to assign Vulkan debug marker name " + name + "!", result);
  }
#endif
#endif
}
}
