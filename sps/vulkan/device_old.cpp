
#include <sps/vulkan/device_old.h>

#include <spdlog/spdlog.h>

#include <set>
#include <string>
#include <vector>

namespace sps::vulkan
{

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

  spdlog::trace("\tDevice name: {}", properties.deviceName);

  switch (properties.deviceType)
  {
    case (vk::PhysicalDeviceType::eCpu):
      spdlog::trace("\tDevice type: CPU");
      break;

    case (vk::PhysicalDeviceType::eDiscreteGpu):
      spdlog::trace("\tDevice type: Discrete GPU");
      break;

    case (vk::PhysicalDeviceType::eIntegratedGpu):
      spdlog::trace("\tDevice type: Integrated GPU");
      break;

    case (vk::PhysicalDeviceType::eVirtualGpu):
      spdlog::trace("\tDevice type: Virtual GPU");
      break;

    default:
      spdlog::trace("\tDevice type: Other");
  }
}

}
