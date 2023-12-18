#include <sps/vulkan/swapchain.h>

#include <sps/vulkan/device.h>
#include <sps/vulkan/exception.h>
#include <sps/vulkan/representation.h>
#include <vulkan/vulkan_enums.hpp>

namespace sps::vulkan
{

/**
        Extract the alpha composite blend modes from the given bitmask.

        \param bits a bitmask describing a combination of alpha composite options.
        \returns a vector of strings describing the options.
*/
std::vector<std::string> log_alpha_composite_bits(vk::CompositeAlphaFlagsKHR bits)
{
  std::vector<std::string> result;

  /*
          typedef enum VkCompositeAlphaFlagBitsKHR {
                  VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR = 0x00000001,
                  VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR = 0x00000002,
                  VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR = 0x00000004,
                  VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR = 0x00000008,
          } VkCompositeAlphaFlagBitsKHR;
  */
  if (bits & vk::CompositeAlphaFlagBitsKHR::eOpaque)
  {
    result.push_back("opaque (alpha ignored)");
  }
  if (bits & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
  {
    result.push_back("pre multiplied (alpha expected to already be multiplied in image)");
  }
  if (bits & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied)
  {
    result.push_back("post multiplied (alpha will be applied during composition)");
  }
  if (bits & vk::CompositeAlphaFlagBitsKHR::eInherit)
  {
    result.push_back("inherited");
  }

  return result;
}

/**
        Extract image usage options.

        \param bits a bitmask describing various image usages
        \returns a vector of strings describing the image usages
*/
std::vector<std::string> log_image_usage_bits(vk::ImageUsageFlags bits)
{
  std::vector<std::string> result;

  /*
          typedef enum VkImageUsageFlagBits {
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT = 0x00000001,
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT = 0x00000002,
                  VK_IMAGE_USAGE_SAMPLED_BIT = 0x00000004,
                  VK_IMAGE_USAGE_STORAGE_BIT = 0x00000008,
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT = 0x00000010,
                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT = 0x00000020,
                  VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT = 0x00000040,
                  VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT = 0x00000080,
                  #ifdef VK_ENABLE_BETA_EXTENSIONS
                          // Provided by VK_KHR_video_decode_queue
                          VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR = 0x00000400,
                  #endif
                  #ifdef VK_ENABLE_BETA_EXTENSIONS
                          // Provided by VK_KHR_video_decode_queue
                          VK_IMAGE_USAGE_VIDEO_DECODE_SRC_BIT_KHR = 0x00000800,
                  #endif
                  #ifdef VK_ENABLE_BETA_EXTENSIONS
                          // Provided by VK_KHR_video_decode_queue
                          VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR = 0x00001000,
                  #endif
                  // Provided by VK_EXT_fragment_density_map
                  VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT = 0x00000200,
                  // Provided by VK_KHR_fragment_shading_rate
                  VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR = 0x00000100,
                  #ifdef VK_ENABLE_BETA_EXTENSIONS
                          // Provided by VK_KHR_video_encode_queue
                          VK_IMAGE_USAGE_VIDEO_ENCODE_DST_BIT_KHR = 0x00002000,
                  #endif
                  #ifdef VK_ENABLE_BETA_EXTENSIONS
                          // Provided by VK_KHR_video_encode_queue
                          VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR = 0x00004000,
                  #endif
                  #ifdef VK_ENABLE_BETA_EXTENSIONS
                          // Provided by VK_KHR_video_encode_queue
                          VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR = 0x00008000,
                  #endif
                  // Provided by VK_HUAWEI_invocation_mask
                  VK_IMAGE_USAGE_INVOCATION_MASK_BIT_HUAWEI = 0x00040000,
                  // Provided by VK_NV_shading_rate_image
                  VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV =
  VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR, } VkImageUsageFlagBits;
  */
  if (bits & vk::ImageUsageFlagBits::eTransferSrc)
  {
    result.push_back("transfer src: image can be used as the source of a transfer command.");
  }
  if (bits & vk::ImageUsageFlagBits::eTransferDst)
  {
    result.push_back("transfer dst: image can be used as the destination of a transfer command.");
  }
  if (bits & vk::ImageUsageFlagBits::eSampled)
  {
    result.push_back("sampled: image can be used to create a VkImageView suitable for occupying a \
VkDescriptorSet slot either of type VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE or \
VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, and be sampled by a shader.");
  }
  if (bits & vk::ImageUsageFlagBits::eStorage)
  {
    result.push_back("storage: image can be used to create a VkImageView suitable for occupying a \
VkDescriptorSet slot of type VK_DESCRIPTOR_TYPE_STORAGE_IMAGE.");
  }
  if (bits & vk::ImageUsageFlagBits::eColorAttachment)
  {
    result.push_back(
      "color attachment: image can be used to create a VkImageView suitable for use as \
a color or resolve attachment in a VkFramebuffer.");
  }
  if (bits & vk::ImageUsageFlagBits::eDepthStencilAttachment)
  {
    result.push_back("depth/stencil attachment: image can be used to create a VkImageView \
suitable for use as a depth/stencil or depth/stencil resolve attachment in a VkFramebuffer.");
  }
  if (bits & vk::ImageUsageFlagBits::eTransientAttachment)
  {
    result.push_back("transient attachment: implementations may support using memory allocations \
with the VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT to back an image with this usage. This \
bit can be set for any image that can be used to create a VkImageView suitable for use as \
a color, resolve, depth/stencil, or input attachment.");
  }
  if (bits & vk::ImageUsageFlagBits::eInputAttachment)
  {
    result.push_back("input attachment: image can be used to create a VkImageView suitable for \
occupying VkDescriptorSet slot of type VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT; be read from \
a shader as an input attachment; and be used as an input attachment in a framebuffer.");
  }
  if (bits & vk::ImageUsageFlagBits::eFragmentDensityMapEXT)
  {
    result.push_back("fragment density map: image can be used to create a VkImageView suitable \
for use as a fragment density map image.");
  }
  if (bits & vk::ImageUsageFlagBits::eFragmentShadingRateAttachmentKHR)
  {
    result.push_back("fragment shading rate attachment: image can be used to create a VkImageView \
suitable for use as a fragment shading rate attachment or shading rate image");
  }
  return result;
}

/**
        \returns a string description of the given present mode.
*/
std::string log_present_mode(vk::PresentModeKHR presentMode)
{
  /*
  * // Provided by VK_KHR_surface
  typedef enum VkPresentModeKHR {
          VK_PRESENT_MODE_IMMEDIATE_KHR = 0,
          VK_PRESENT_MODE_MAILBOX_KHR = 1,
          VK_PRESENT_MODE_FIFO_KHR = 2,
          VK_PRESENT_MODE_FIFO_RELAXED_KHR = 3,
          // Provided by VK_KHR_shared_presentable_image
          VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR = 1000111000,
          // Provided by VK_KHR_shared_presentable_image
          VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR = 1000111001,
  } VkPresentModeKHR;
  */

  if (presentMode == vk::PresentModeKHR::eImmediate)
  {
    return "immediate: the presentation engine does not wait for a vertical blanking period \
to update the current image, meaning this mode may result in visible tearing. No internal \
queuing of presentation requests is needed, as the requests are applied immediately.";
  }
  if (presentMode == vk::PresentModeKHR::eMailbox)
  {
    return "mailbox: the presentation engine waits for the next vertical blanking period \
to update the current image. Tearing cannot be observed. An internal single-entry queue is \
used to hold pending presentation requests. If the queue is full when a new presentation \
request is received, the new request replaces the existing entry, and any images associated \
with the prior entry become available for re-use by the application. One request is removed \
from the queue and processed during each vertical blanking period in which the queue is non-empty.";
  }
  if (presentMode == vk::PresentModeKHR::eFifo)
  {
    return "fifo: the presentation engine waits for the next vertical blanking \
period to update the current image. Tearing cannot be observed. An internal queue is used to \
hold pending presentation requests. New requests are appended to the end of the queue, and one \
request is removed from the beginning of the queue and processed during each vertical blanking \
period in which the queue is non-empty. This is the only value of presentMode that is required \
to be supported.";
  }
  if (presentMode == vk::PresentModeKHR::eFifoRelaxed)
  {
    return "relaxed fifo: the presentation engine generally waits for the next vertical \
blanking period to update the current image. If a vertical blanking period has already passed \
since the last update of the current image then the presentation engine does not wait for \
another vertical blanking period for the update, meaning this mode may result in visible tearing \
in this case. This mode is useful for reducing visual stutter with an application that will \
mostly present a new image before the next vertical blanking period, but may occasionally be \
late, and present a new image just after the next vertical blanking period. An internal queue \
is used to hold pending presentation requests. New requests are appended to the end of the queue, \
and one request is removed from the beginning of the queue and processed during or after each \
vertical blanking period in which the queue is non-empty.";
  }
  if (presentMode == vk::PresentModeKHR::eSharedDemandRefresh)
  {
    return "shared demand refresh: the presentation engine and application have \
concurrent access to a single image, which is referred to as a shared presentable image. \
The presentation engine is only required to update the current image after a new presentation \
request is received. Therefore the application must make a presentation request whenever an \
update is required. However, the presentation engine may update the current image at any point, \
meaning this mode may result in visible tearing.";
  }
  if (presentMode == vk::PresentModeKHR::eSharedContinuousRefresh)
  {
    return "shared continuous refresh: the presentation engine and application have \
concurrent access to a single image, which is referred to as a shared presentable image. The \
presentation engine periodically updates the current image on its regular refresh cycle. The \
application is only required to make one initial presentation request, after which the \
presentation engine must update the current image without any need for further presentation \
requests. The application can indicate the image contents have been updated by making a \
presentation request, but this does not guarantee the timing of when it will be updated. \
This mode may result in visible tearing if rendering to the image is not timed correctly.";
  }
  return "none/undefined";
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
    stringList = log_alpha_composite_bits(support.capabilities.supportedCompositeAlpha);
    for (std::string line : stringList)
    {
      spdlog::trace("\t\t{}", line);
    }

    spdlog::trace("\tsupported image usage:");
    stringList = log_image_usage_bits(support.capabilities.supportedUsageFlags);
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
    spdlog::trace("\t {}", log_present_mode(presentMode));
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

void Swapchain::setup_swapchain(
  const std::uint32_t width, const std::uint32_t height, const bool vsync_enabled)
{
  const auto caps = m_device.surfaceCapabilities(m_surface);

  spdlog::trace("Swapchain can support the following surface capabilities:");

  spdlog::trace("\tminimum image count: {}", caps.minImageCount);
  spdlog::trace("\tmaximum image count: {}", caps.maxImageCount);

  spdlog::trace("\tcurrent extent: ");
  /*typedef struct VkExtent2D {
          uint32_t    width;
          uint32_t    height;
  } VkExtent2D;
  */
  spdlog::trace("\t\twidth: {}", caps.currentExtent.width);
  spdlog::trace("\t\theight: {}", caps.currentExtent.height);

  spdlog::trace("\tminimum supported extent: ");
  spdlog::trace("\t\twidth: {}", caps.minImageExtent.width);
  spdlog::trace("\t\theight: {}", caps.minImageExtent.height);

  spdlog::trace("\tmaximum supported extent: ");
  spdlog::trace("\t\twidth: {}", caps.maxImageExtent.width);
  spdlog::trace("\t\theight: {}", caps.maxImageExtent.height);

  spdlog::trace("\tmaximum image array layers: {}", caps.maxImageArrayLayers);

  spdlog::trace("\tsupported transforms:");
  std::vector<std::string> stringList = utils::as_description(caps.supportedTransforms);
  for (std::string line : stringList)
  {
    spdlog::trace("\t\t {}", line);
  }

  spdlog::trace("\tcurrent transform:");

  stringList = utils::as_description(caps.currentTransform);
  for (std::string line : stringList)
  {
    spdlog::trace("\t\t {}", line);
  }

  spdlog::trace("\tsupported alpha operations:");
  stringList = log_alpha_composite_bits(caps.supportedCompositeAlpha);
  for (std::string line : stringList)
  {
    spdlog::trace("\t\t{}", line);
  }

  spdlog::trace("\tsupported image usage:");
  stringList = log_image_usage_bits(caps.supportedUsageFlags);
  for (std::string line : stringList)
  {
    spdlog::trace("\t\t{}", line);
  }

  auto formats = m_device.physicalDevice().getSurfaceFormatsKHR(m_surface);
  m_surface_format = choose_swapchain_surface_format(formats);

  const vk::Extent2D requested_extent{ width, height };

  auto presentModes = m_device.physicalDevice().getSurfacePresentModesKHR(m_surface);

  static const std::vector<vk::PresentModeKHR> default_present_mode_priorities{
    vk::PresentModeKHR::eMailbox, vk::PresentModeKHR::eFifoRelaxed, vk::PresentModeKHR::eFifo
  };

#if 0
  

  const auto composite_alpha =
    choose_composite_alpha(VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, caps.supportedCompositeAlpha);

  if (!composite_alpha)
  {
    throw std::runtime_error("Error: Could not find suitable composite alpha!");
  }

  if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0u)
  {
    throw std::runtime_error("Error: Swapchain image usage flag bit "
                             "VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT is not supported!");
  }

  const VkSwapchainKHR old_swapchain = m_swapchain;

  const auto swapchain_ci = make_info<VkSwapchainCreateInfoKHR>({
    .surface = m_surface,
    .minImageCount = (caps.maxImageCount != 0)
      ? std::min(caps.minImageCount + 1, caps.maxImageCount)
      : std::max(caps.minImageCount + 1, caps.minImageCount),
    .imageFormat = m_surface_format.value().format,
    .imageColorSpace = m_surface_format.value().colorSpace,
    .imageExtent =
      choose_image_extent(requested_extent, caps.minImageExtent, caps.maxImageExtent, m_extent),
    .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .preTransform = ((VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR & caps.supportedTransforms) != 0u)
      ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
      : caps.currentTransform,
    .compositeAlpha = composite_alpha.value(),
    .presentMode = choose_present_mode(
      vk_tools::get_surface_present_modes(m_device.physical_device(), m_surface),
      default_present_mode_priorities, vsync_enabled),
    .clipped = VK_TRUE,
    .oldSwapchain = old_swapchain,
  });

  spdlog::trace(
    "Using swapchain surface transform {}", vk_tools::as_string(swapchain_ci.preTransform));

  spdlog::trace("Creating swapchain");

  if (const auto result =
        vkCreateSwapchainKHR(m_device.device(), &swapchain_ci, nullptr, &m_swapchain);
      result != VK_SUCCESS)
  {
    throw VulkanException("Error: vkCreateSwapchainKHR failed!", result);
  }

  // We need to destroy the old swapchain if specified
  if (old_swapchain != VK_NULL_HANDLE)
  {
    for (auto* const img_view : m_img_views)
    {
      // An image view for each frame
      m_device.device().destroyImageView(img_view);
    }
    m_imgs.clear();
    m_img_views.clear();
    vkDestroySwapchainKHR(m_device.device(), old_swapchain, nullptr);
  }

  m_extent.width = width;
  m_extent.height = height;

  m_imgs = get_swapchain_images();

  if (m_imgs.empty())
  {
    throw std::runtime_error("Error: Swapchain image count is 0!");
  }

  spdlog::trace("Creating {} swapchain image views", m_imgs.size());

  m_img_views.resize(m_imgs.size());

  for (std::size_t img_index = 0; img_index < m_imgs.size(); img_index++)
  {
    const auto img_view_ci = make_info<VkImageViewCreateInfo>({
      .image = m_imgs[img_index],
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = m_surface_format.value().format,
      .components{
        .r = VK_COMPONENT_SWIZZLE_IDENTITY,
        .g = VK_COMPONENT_SWIZZLE_IDENTITY,
        .b = VK_COMPONENT_SWIZZLE_IDENTITY,
        .a = VK_COMPONENT_SWIZZLE_IDENTITY,
      },
      .subresourceRange{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
    });

    m_device.create_image_view(img_view_ci, &m_img_views[img_index], "swapchain image view");
  }
#endif
}

Swapchain::Swapchain(Device& device, const VkSurfaceKHR surface, const std::uint32_t width,
  const std::uint32_t height, const bool vsync_enabled)
  : m_device(device)
  , m_surface(surface)
  , m_vsync_enabled(vsync_enabled)
{
  // m_img_available = std::make_unique<Semaphore>(m_device, "Swapchain image available");
  m_swapchain =
    create_swapchain(m_device.device(), m_device.physicalDevice(), surface, width, height, false);

  // setup_swapchain(width, height, vsync_enabled);
}

Swapchain::Swapchain(Swapchain&& other) noexcept
  : m_device(other.m_device)
{
  // FIXME!!!!
  //  m_swapchain = std::exchange(other.m_swapchain, VK_NULL_HANDLE);
  m_surface = std::exchange(other.m_surface, VK_NULL_HANDLE);
  m_surface_format = other.m_surface_format;
  m_imgs = std::move(other.m_imgs);
  m_img_views = std::move(other.m_img_views);
  m_extent = other.m_extent;
  // m_img_available = std::exchange(other.m_img_available, nullptr);
  m_vsync_enabled = other.m_vsync_enabled;
}

Swapchain::~Swapchain()
{
  m_device.device().destroySwapchainKHR(m_swapchain.swapchain);
}

}
