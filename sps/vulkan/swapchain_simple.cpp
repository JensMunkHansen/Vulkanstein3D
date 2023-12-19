#include <sps/vulkan/device_simple.h>
#include <sps/vulkan/representation.h>
#include <sps/vulkan/swapchain_simple.h>

#include <vulkan/vulkan.hpp>

#include <spdlog/spdlog.h>

namespace sps::vulkan
{

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

SwapChainSupportDetails query_swapchain_support(
  vk::PhysicalDevice device, vk::SurfaceKHR surface, bool debug)
{
  SwapChainSupportDetails support;

  /*
  * typedef struct VkSurfaceCapabilitiesKHR {
          uint32_t                         minImageCount;
          uint32_t                         maxImageCount;
          VkExtent2D                       currentExtent;
          VkExtent2D                       minImageExtent;
          VkExtent2D                       maxImageExtent;
          uint32_t                         maxImageArrayLayers;
          VkSurfaceTransformFlagsKHR       supportedTransforms;
          VkSurfaceTransformFlagBitsKHR    currentTransform;
          VkCompositeAlphaFlagsKHR         supportedCompositeAlpha;
          VkImageUsageFlags                supportedUsageFlags;
  } VkSurfaceCapabilitiesKHR;
  */
  support.capabilities = device.getSurfaceCapabilitiesKHR(surface);
  if (debug)
  {
    spdlog::trace("Swapchain can support the following surface capabilities:");

    spdlog::trace("\tminimum image count: {}", support.capabilities.minImageCount);
    spdlog::trace("\tmaximum image count: {}", support.capabilities.maxImageCount);

    spdlog::trace("\tcurrent extent: ");
    /*typedef struct VkExtent2D {
            uint32_t    width;
            uint32_t    height;
    } VkExtent2D;
    */
    spdlog::trace("\t\twidth: {}", support.capabilities.currentExtent.width);
    spdlog::trace("\t\theight: {}", support.capabilities.currentExtent.height);

    spdlog::trace("\tminimum supported extent: ");
    spdlog::trace("\t\twidth: {}", support.capabilities.minImageExtent.width);
    spdlog::trace("\t\theight: {}", support.capabilities.minImageExtent.height);

    spdlog::trace("\tmaximum supported extent: ");
    spdlog::trace("\t\twidth: {}", support.capabilities.maxImageExtent.width);
    spdlog::trace("\t\theight: {}", support.capabilities.maxImageExtent.height);

    spdlog::trace("\tmaximum image array layers: {}", support.capabilities.maxImageArrayLayers);

    spdlog::trace("\tsupported transforms:");
    std::vector<std::string> stringList =
      utils::as_description(support.capabilities.supportedTransforms);
    for (std::string line : stringList)
    {
      spdlog::trace("\t\t {}", line);
    }

    spdlog::trace("\tcurrent transform:");
    stringList = utils::as_description(support.capabilities.currentTransform);
    for (std::string line : stringList)
    {
      spdlog::trace("\t\t {}", line);
    }

    spdlog::trace("\tsupported alpha operations:");
    stringList = utils::log_alpha_composite_bits(support.capabilities.supportedCompositeAlpha);
    for (std::string line : stringList)
    {
      spdlog::trace("\t\t{}", line);
    }

    spdlog::trace("\tsupported image usage:");
    stringList = utils::log_image_usage_bits(support.capabilities.supportedUsageFlags);
    for (std::string line : stringList)
    {
      spdlog::trace("\t\t{}", line);
    }
  }

  support.formats = device.getSurfaceFormatsKHR(surface);

  if (debug)
  {

    for (vk::SurfaceFormatKHR supportedFormat : support.formats)
    {
      /*
      * typedef struct VkSurfaceFormatKHR {
              VkFormat           format;
              VkColorSpaceKHR    colorSpace;
      } VkSurfaceFormatKHR;
      */

      spdlog::trace("supported pixel format: {}", vk::to_string(supportedFormat.format));
      spdlog::trace("supported color space: {}", vk::to_string(supportedFormat.colorSpace));
    }
  }

  support.presentModes = device.getSurfacePresentModesKHR(surface);

  for (vk::PresentModeKHR presentMode : support.presentModes)
  {
    spdlog::trace("\t {}", utils::log_present_mode(presentMode));
  }
  return support;
}

vk::SurfaceFormatKHR choose_swapchain_surface_format(std::vector<vk::SurfaceFormatKHR> formats)
{

  for (vk::SurfaceFormatKHR format : formats)
  {
    if (format.format == vk::Format::eB8G8R8A8Unorm &&
      format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
    {
      return format;
    }
  }

  return formats[0];
}

/**
        Choose a present mode.

        \param presentModes a vector of present modes supported by the device
        \returns the chosen present mode
*/
vk::PresentModeKHR choose_swapchain_present_mode(std::vector<vk::PresentModeKHR> presentModes)
{

  for (vk::PresentModeKHR presentMode : presentModes)
  {
    if (presentMode == vk::PresentModeKHR::eMailbox)
    {
      return presentMode;
    }
  }

  return vk::PresentModeKHR::eFifo;
}

/**
        Choose an extent for the swapchain.

        \param width the requested width
        \param height the requested height
        \param capabilities a struct describing the supported capabilities of the device
        \returns the chosen extent
*/
vk::Extent2D choose_swapchain_extent(
  uint32_t width, uint32_t height, vk::SurfaceCapabilitiesKHR capabilities)
{

  if (capabilities.currentExtent.width != UINT32_MAX)
  {
    return capabilities.currentExtent;
  }
  else
  {
    vk::Extent2D extent = { width, height };

    extent.width = std::min(
      capabilities.maxImageExtent.width, std::max(capabilities.minImageExtent.width, extent.width));

    extent.height = std::min(capabilities.maxImageExtent.height,
      std::max(capabilities.minImageExtent.height, extent.height));

    return extent;
  }
}

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
  vk::SurfaceKHR surface, int width, int height, bool debug)
{

  SwapChainSupportDetails support = query_swapchain_support(physicalDevice, surface, debug);

  vk::SurfaceFormatKHR format = choose_swapchain_surface_format(support.formats);

  vk::PresentModeKHR presentMode = choose_swapchain_present_mode(support.presentModes);

  vk::Extent2D extent = choose_swapchain_extent(width, height, support.capabilities);

  uint32_t imageCount = (support.capabilities.maxImageCount != 0)
    ? std::min(support.capabilities.minImageCount + 1, support.capabilities.maxImageCount)
    : std::max(support.capabilities.minImageCount + 1, support.capabilities.minImageCount);

  /*
  * VULKAN_HPP_CONSTEXPR SwapchainCreateInfoKHR(
VULKAN_HPP_NAMESPACE::SwapchainCreateFlagsKHR flags_         = {},
VULKAN_HPP_NAMESPACE::SurfaceKHR              surface_       = {},
uint32_t                                      minImageCount_ = {},
VULKAN_HPP_NAMESPACE::Format                  imageFormat_   =
VULKAN_HPP_NAMESPACE::Format::eUndefined, VULKAN_HPP_NAMESPACE::ColorSpaceKHR   imageColorSpace_  =
VULKAN_HPP_NAMESPACE::ColorSpaceKHR::eSrgbNonlinear, VULKAN_HPP_NAMESPACE::Extent2D imageExtent_ =
{}, uint32_t                              imageArrayLayers_ = {},
VULKAN_HPP_NAMESPACE::ImageUsageFlags imageUsage_       = {},
VULKAN_HPP_NAMESPACE::SharingMode     imageSharingMode_ =
VULKAN_HPP_NAMESPACE::SharingMode::eExclusive, uint32_t queueFamilyIndexCount_ = {}, const uint32_t
*                      pQueueFamilyIndices_   = {},
VULKAN_HPP_NAMESPACE::SurfaceTransformFlagBitsKHR preTransform_ =
VULKAN_HPP_NAMESPACE::SurfaceTransformFlagBitsKHR::eIdentity,
VULKAN_HPP_NAMESPACE::CompositeAlphaFlagBitsKHR compositeAlpha_ =
VULKAN_HPP_NAMESPACE::CompositeAlphaFlagBitsKHR::eOpaque,
VULKAN_HPP_NAMESPACE::PresentModeKHR presentMode_  =
VULKAN_HPP_NAMESPACE::PresentModeKHR::eImmediate, VULKAN_HPP_NAMESPACE::Bool32         clipped_ =
{}, VULKAN_HPP_NAMESPACE::SwapchainKHR   oldSwapchain_ = {} ) VULKAN_HPP_NOEXCEPT
  */
  vk::SwapchainCreateInfoKHR createInfo =
    vk::SwapchainCreateInfoKHR(vk::SwapchainCreateFlagsKHR(), //
      surface,                                                //
      imageCount,                                             //
      format.format,                                          //
      format.colorSpace,                                      //
      extent,                                                 //
      1,                                                      //
      vk::ImageUsageFlagBits::eColorAttachment);

  QueueFamilyIndices indices = findQueueFamilies(physicalDevice, surface);
  uint32_t queueFamilyIndices[] = {
    indices.graphicsFamily.value(), //
    indices.presentFamily.value()   //
  };

  if (indices.graphicsFamily != indices.presentFamily)
  {
    createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  }
  else
  {
    createInfo.imageSharingMode = vk::SharingMode::eExclusive;
  }

  createInfo.preTransform = support.capabilities.currentTransform;
  createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;

  createInfo.oldSwapchain = vk::SwapchainKHR(nullptr);

  SwapChainBundle bundle{};
  try
  {
    bundle.swapchain = logicalDevice.createSwapchainKHR(createInfo);
  }
  catch (vk::SystemError err)
  {
    throw std::runtime_error("failed to create swap chain!");
  }

  bundle.images = logicalDevice.getSwapchainImagesKHR(bundle.swapchain);
  bundle.format = format.format;
  bundle.extent = extent;

  return bundle;
}

}
