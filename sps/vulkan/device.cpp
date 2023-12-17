#include <sps/vulkan/config.h>

#include <sps/vulkan/device.h>
#include <sps/vulkan/exception.h>
#include <sps/vulkan/instance.h>
#include <sps/vulkan/representation.h>

#include <optional>
#include <set>
#include <span>
#include <string>

#include <spdlog/spdlog.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

namespace sps::vulkan
{
constexpr float DEFAULT_QUEUE_PRIORITY = 1.0f;

std::vector<VkBool32> get_device_features_as_vector(const vk::PhysicalDeviceFeatures& features)
{
  std::vector<VkBool32> comparable_features(sizeof(vk::PhysicalDeviceFeatures) / sizeof(VkBool32));
  std::memcpy(comparable_features.data(), &features, sizeof(vk::PhysicalDeviceFeatures));
  return comparable_features;
}

std::string get_physical_device_name(const vk::PhysicalDevice physical_device)
{
  vk::PhysicalDeviceProperties properties = physical_device.getProperties();
  return properties.deviceName;
}

bool Device::is_presentation_supported(
  const vk::SurfaceKHR& surface, const std::uint32_t queue_family_index) const
{
  // Default to true in this case where a surface is not passed (and therefore presentation isn't
  // cared about)

  VkBool32 supported = VK_TRUE;
  {
    if (const auto result =
          vkGetPhysicalDeviceSurfaceSupportKHR(m_physical_device, 0, surface, &supported);
        result != VK_SUCCESS)
    {
      throw VulkanException("Error: vkGetPhysicalDeviceSurfaceSupportKHR failed!", result);
    }
    return supported == VK_TRUE;
  }
}

/// Check if a device extension is supported by a physical device
/// @param extensions The device extensions
/// @note If extensions is empty, this function returns ``false``
/// @param extension_name The extension name
/// @return ``true`` if the required device extension is supported
bool is_extension_supported(
  const std::vector<vk::ExtensionProperties>& extensions, const std::string& extension_name)
{
  return std::find_if(extensions.begin(), extensions.end(),
           [&](const vk::ExtensionProperties extension)
           { return extension.extensionName == extension_name; }) != extensions.end();
}

/// A function for rating physical devices by type
/// @param info The physical device info
/// @return A number from 0 to 2 which rates the physical device (higher is better)
std::uint32_t device_type_rating(const DeviceInfo& info)
{
  switch (info.type)
  {
    case vk::PhysicalDeviceType::eDiscreteGpu:
      return 2;
    case vk::PhysicalDeviceType::eIntegratedGpu:
      return 1;
    case vk::PhysicalDeviceType::eCpu:
    default:
      return 0;
  }
}

DeviceInfo build_device_info(const vk::PhysicalDevice physical_device, const vk::SurfaceKHR surface)
{
  vk::PhysicalDeviceProperties properties = physical_device.getProperties();

  vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();

  vk::PhysicalDeviceFeatures features = physical_device.getFeatures();

  vk::DeviceSize total_device_local = 0;
  for (std::size_t i = 0; i < memory_properties.memoryHeapCount; i++)
  {
    const auto& heap = memory_properties.memoryHeaps[i];
    if ((heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal))
    {
      total_device_local += heap.size;
    }
  }

  // Default to true in this case where a surface is not passed (and therefore presentation isn't
  // cared about)
  VkBool32 presentation_supported = VK_TRUE;
  {
    if (const auto result = vkGetPhysicalDeviceSurfaceSupportKHR(
          physical_device, 0, surface, &presentation_supported);
        result != VK_SUCCESS)
    {
      throw VulkanException("Error: vkGetPhysicalDeviceSurfaceSupportKHR failed!", result);
    }
  }

  // vk::DispatchLoaderDynamic(inst.instance(), vkGetInstanceProcAddr);

  const auto extensions = physical_device.enumerateDeviceExtensionProperties();

  // physical_device.enumerateDeviceExtensionProperties();

  const bool is_swapchain_supported =
    is_extension_supported(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);

  return DeviceInfo{
    .name = properties.deviceName,
    .physical_device = physical_device,
    .type = properties.deviceType,
    .total_device_local = total_device_local,
    .features = features,
    .extensions = extensions,
    .presentation_supported = presentation_supported == VK_TRUE,
    .swapchain_supported = is_swapchain_supported,
  };
}

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
  const std::vector<const char*> requestedExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

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

bool is_device_suitable(const DeviceInfo& info, const vk::PhysicalDeviceFeatures& required_features,
  const std::span<const char*> required_extensions, const bool print_info = false)
{
  const auto comparable_required_features = get_device_features_as_vector(required_features);
  const auto comparable_available_features = get_device_features_as_vector(info.features);
  constexpr auto FEATURE_COUNT = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);

  // Loop through all physical device features and check if a feature is required but not
  // supported
  for (std::size_t i = 0; i < FEATURE_COUNT; i++)
  {
    if (comparable_required_features[i] == VK_TRUE && comparable_available_features[i] == VK_FALSE)
    {
      if (print_info)
      {
        spdlog::info("Physical device {} does not support {}!", info.name,
          sps::vulkan::utils::get_device_feature_description(i));
      }
      return false;
    }
  }
  // Loop through all device extensions and check if an extension is required but not supported
  for (const auto& extension : required_extensions)
  {
    if (!is_extension_supported(info.extensions, extension))
    {
      if (print_info)
      {
        spdlog::info("Physical device {} does not support extension {}!", info.name, extension);
      }
      return false;
    }
  }
  return info.presentation_supported && info.swapchain_supported;
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

// Don't call this
vk::Queue get_queue(vk::PhysicalDevice physicalDevice, vk::Device device)
{
  QueueFamilyIndices indices = findQueueFamilies(physicalDevice, vk::SurfaceKHR{});

  return device.getQueue(indices.graphicsFamily.value(), 0);
}

bool compare_physical_devices(const vk::PhysicalDeviceFeatures& required_features,
  const std::span<const char*> required_extensions, const DeviceInfo& lhs, const DeviceInfo& rhs)
{
  if (!is_device_suitable(rhs, required_features, required_extensions))
  {
    return true;
  }
  if (!is_device_suitable(lhs, required_features, required_extensions))
  {
    return false;
  }
  if (device_type_rating(lhs) > device_type_rating(rhs))
  {
    return true;
  }
  if (device_type_rating(lhs) < device_type_rating(rhs))
  {
    return false;
  }
  // Device types equal, compare total amount of DEVICE_LOCAL memory
  return lhs.total_device_local >= rhs.total_device_local;
}

vk::PhysicalDevice Device::pick_best_physical_device( //
  std::vector<DeviceInfo>&& physical_device_infos,
  const vk::PhysicalDeviceFeatures& required_features,
  const std::span<const char*> required_extensions)
{
  if (physical_device_infos.empty())
  {
    throw std::runtime_error("Error: There are no physical devices available!");
  }
  std::sort(physical_device_infos.begin(), physical_device_infos.end(),
    [&](const auto& lhs, const auto& rhs)
    { return compare_physical_devices(required_features, required_extensions, lhs, rhs); });
  if (!is_device_suitable(
        physical_device_infos.front(), required_features, required_extensions, true))
  {
    throw std::runtime_error("Error: Could not determine a suitable physical device!");
  }
  return physical_device_infos.front().physical_device;
}

vk::PhysicalDevice Device::pick_best_physical_device( //
  const Instance& inst, const vk::SurfaceKHR& surface,
  const vk::PhysicalDeviceFeatures& required_features, std::span<const char*> required_extensions)
{
  std::vector<vk::PhysicalDevice> availableDevices = inst.instance().enumeratePhysicalDevices();

  std::vector<DeviceInfo> infos(availableDevices.size());
  std::transform(availableDevices.begin(), availableDevices.end(), infos.begin(),
    [&](const vk::PhysicalDevice physical_device)
    { return build_device_info(physical_device, surface); });
  return Device::pick_best_physical_device(
    std::move(infos), required_features, required_extensions);
}

std::optional<std::uint32_t> Device::find_queue_family_index_if(
  const std::function<bool(std::uint32_t index, const vk::QueueFamilyProperties&)>& criteria_lambda)
{
  for (std::uint32_t index = 0;
       const auto queue_family : m_physical_device.getQueueFamilyProperties())
  {
    if (criteria_lambda(index, queue_family))
    {
      return index;
    }
    index++;
  }
  return std::nullopt;
}

Device::Device(const Instance& inst, vk::SurfaceKHR surface, bool prefer_distinct_transfer_queue,
  vk::PhysicalDevice physical_device,
  std::span<const char*> required_extensions, // contains swapchain
  const vk::PhysicalDeviceFeatures& required_features,
  const vk::PhysicalDeviceFeatures& optional_features)
  : m_physical_device(physical_device)
{
  if (!is_device_suitable(
        build_device_info(physical_device, surface), required_features, required_extensions))
  {
    throw std::runtime_error("Error: The chosen physical device {} is not suitable!");
  }

  m_gpu_name = get_physical_device_name(m_physical_device);
  spdlog::trace("Creating device using graphics card: {}", m_gpu_name);

  spdlog::trace("Creating Vulkan device queues");
  std::vector<vk::DeviceQueueCreateInfo> queues_to_create;

  if (prefer_distinct_transfer_queue)
  {
    spdlog::trace(
      "The application will try to use a distinct data transfer queue if it is available");
  }
  else
  {
    spdlog::warn("The application is forced not to use a distinct data transfer queue!");
  }
#if 0
  // vk::PhysicalDevice physicalDevice = choose_physical_device(inst.instance());
  m_device = create_logical_device(physical_device, surface);
#else
  // Check if there is one queue family which can be used for both graphics and presentation
  auto queue_candidate = find_queue_family_index_if(
    [&](const std::uint32_t index, const vk::QueueFamilyProperties& queue_family)
    {
      return is_presentation_supported(surface, index) &&
        (queue_family.queueFlags & vk::QueueFlagBits::eGraphics);
    });

  if (!queue_candidate)
  {
    throw std::runtime_error("Error: Could not find a queue for both graphics and presentation!");
  }

  spdlog::trace("One queue for both graphics and presentation will be used");

  m_graphics_queue_family_index = *queue_candidate;
  m_present_queue_family_index = m_graphics_queue_family_index;

  // In this case, there is one queue family which can be used for both graphics and presentation
  queues_to_create.push_back(vk::DeviceQueueCreateInfo(
    vk::DeviceQueueCreateFlags(), *queue_candidate, 1, &sps::vulkan::DEFAULT_QUEUE_PRIORITY));

  // Add another device queue just for data transfer
  queue_candidate = find_queue_family_index_if(
    [&](const std::uint32_t index, const vk::QueueFamilyProperties& queue_family)
    {
      return is_presentation_supported(surface, index) &&
        // No graphics bit, only transfer bit
        ((queue_family.queueFlags & vk::QueueFlagBits::eGraphics) == (vk::QueueFlagBits)0) &&
        (queue_family.queueFlags & vk::QueueFlagBits::eTransfer);
    });

  bool use_distinct_data_transfer_queue = false;

  if (queue_candidate && prefer_distinct_transfer_queue)
  {
    m_transfer_queue_family_index = *queue_candidate;

    spdlog::trace("A separate queue will be used for data transfer.");

    // We have the opportunity to use a separated queue for data transfer!
    use_distinct_data_transfer_queue = true;

    queues_to_create.push_back(vk::DeviceQueueCreateInfo(vk::DeviceQueueCreateFlags(),
      m_transfer_queue_family_index, 1, &sps::vulkan::DEFAULT_QUEUE_PRIORITY));
  }
  else
  {
    // We don't have the opportunity to use a separated queue for data transfer!
    // Do not create a new queue, use the graphics queue instead.
    use_distinct_data_transfer_queue = false;
  }

  if (!use_distinct_data_transfer_queue)
  {
    spdlog::warn("The application is forced to avoid distinct data transfer queues");
    spdlog::warn("Because of this, the graphics queue will be used for data transfer");

    m_transfer_queue_family_index = m_graphics_queue_family_index;
  }

  // TODO(Use C++)

  //  VkPhysicalDeviceFeatures available_features{};
  //  vkGetPhysicalDeviceFeatures(physical_device, &available_features);

  vk::PhysicalDeviceFeatures available_features = vk::PhysicalDeviceFeatures();

  const auto comparable_required_features = get_device_features_as_vector(required_features);
  const auto comparable_optional_features = get_device_features_as_vector(optional_features);
  const auto comparable_available_features = get_device_features_as_vector(available_features);

  constexpr auto FEATURE_COUNT = sizeof(vk::PhysicalDeviceFeatures) / sizeof(VkBool32);

  spdlog::trace("Number of features {}", FEATURE_COUNT);

  std::vector<VkBool32> features_to_enable(FEATURE_COUNT, VK_FALSE);

  for (std::size_t i = 0; i < FEATURE_COUNT; i++)
  {
    if (comparable_required_features[i] == VK_TRUE)
    {
      features_to_enable[i] = VK_TRUE;
    }
    if (comparable_optional_features[i] == VK_TRUE)
    {
      if (comparable_available_features[i] == VK_TRUE)
      {
        features_to_enable[i] = VK_TRUE;
      }
      else
      {
        spdlog::warn("The physical device {} does not support {}!",
          get_physical_device_name(physical_device),
          sps::vulkan::utils::get_device_feature_description(i));
      }
    }
  }

  spdlog::trace("Number of features enabled {}", features_to_enable.size());

  std::memcpy(&m_enabled_features, features_to_enable.data(), features_to_enable.size());

  spdlog::trace("Creating physical device");

  std::vector<const char*> enabledLayers;

#ifdef SPS_DEBUG
  enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif

  // Why not layers and then extenstions???
  vk::DeviceCreateInfo deviceInfo = vk::DeviceCreateInfo(vk::DeviceCreateFlags(), //
    queues_to_create.size(), queues_to_create.data(),                             //
    enabledLayers.size(), enabledLayers.data(),                                   //
    required_extensions.size(), required_extensions.data(),                       //
    &m_enabled_features);

  try
  {
    m_device = m_physical_device.createDevice(deviceInfo);
    spdlog::trace("GPU has been successfully abstracted!");
  }
  catch (vk::SystemError err)
  {
    spdlog::trace("Device creation failed!");
    throw;
  }

#endif
  // Consider using volkLoadDevice (bypass Vulkan loader dispatch code)
  // TODO: Use VK_EXT_debug_utils

  const bool enable_debug_markers =
    std::find_if(required_extensions.begin(), required_extensions.end(),
      [&](const char* extension) {
        return std::string(extension) == std::string(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
      }) != required_extensions.end();

  m_dldi = vk::DispatchLoaderDynamic(inst.instance(), vkGetInstanceProcAddr);

#ifdef SPS_DEBUG
  spdlog::trace("debug markers enabled {}", enable_debug_markers);

  if (enable_debug_markers)
  {
    spdlog::trace("Initializing Vulkan debug markers");
    // The debug marker extension is not part of the core, so function pointers need to be loaded
    // manually. TODO: Figure this out
  }
#endif

  spdlog::trace("transfer queue family index: {}", m_transfer_queue_family_index);
  spdlog::trace("graphics queue family index: {}", m_graphics_queue_family_index);
  spdlog::trace("present queue family index: {}", m_present_queue_family_index);
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

  if (const auto result = m_device.debugMarkerSetObjectNameEXT(&nameInfo, m_dldi);
      result != vk::Result::eSuccess)
  {
    throw VulkanException("Failed to assign Vulkan debug marker name " + name + "!", result);
  }
#endif
}
}
