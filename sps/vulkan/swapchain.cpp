#include <sps/vulkan/swapchain.h>

#include <sps/vulkan/device.h>
#include <sps/vulkan/exception.h>
#include <sps/vulkan/representation.h>

#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_structs.hpp>

#define NEW_SWAPCHAIN 1

namespace sps::vulkan
{

#ifndef NEW_SWAPCHAIN
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
#endif

std::optional<vk::CompositeAlphaFlagBitsKHR> Swapchain::choose_composite_alpha(
  const vk::CompositeAlphaFlagBitsKHR request_composite_alpha,
  const vk::CompositeAlphaFlagsKHR supported_composite_alpha)
{
  if (request_composite_alpha & supported_composite_alpha) // != 0u
  {
    return request_composite_alpha;
  }
  static const std::vector<vk::CompositeAlphaFlagBitsKHR> composite_alpha_flags{
    vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::CompositeAlphaFlagBitsKHR::eOpaque,
    vk::CompositeAlphaFlagBitsKHR::ePreMultiplied, vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
    vk::CompositeAlphaFlagBitsKHR::eInherit
  };

  for (const auto flag : composite_alpha_flags)
  {
    if (flag & supported_composite_alpha) // != 0u
    {
      spdlog::trace("Swapchain composite alpha '{}' is not supported, selecting '{}'",
        utils::as_string(request_composite_alpha), utils::as_string(flag));
      return flag;
    }
  }
  return std::nullopt;
}

vk::Extent2D Swapchain::choose_image_extent(const vk::Extent2D& requested_extent,
  const vk::Extent2D& min_extent, const vk::Extent2D& max_extent,
  const vk::Extent2D& current_extent)
{
  if (current_extent.width == std::numeric_limits<std::uint32_t>::max())
  {
    return requested_extent;
  }
  if (requested_extent.width < 1 || requested_extent.height < 1)
  {
    spdlog::trace("Swapchain image extent ({}, {}) is not supported! Selecting ({}, {})",
      requested_extent.width, requested_extent.height, current_extent.width, current_extent.height);
    return current_extent;
  }
  vk::Extent2D extent;
  extent.width = std::clamp(requested_extent.width, min_extent.width, max_extent.width);
  extent.height = std::clamp(requested_extent.height, min_extent.height, max_extent.height);
  return extent;
}

vk::PresentModeKHR Swapchain::choose_present_mode(
  const std::vector<vk::PresentModeKHR>& available_present_modes,
  const std::vector<vk::PresentModeKHR>& present_mode_priority_list, const bool vsync_enabled)
{
  assert(!available_present_modes.empty());
  assert(!present_mode_priority_list.empty());
  if (!vsync_enabled)
  {
    for (const auto requested_present_mode : present_mode_priority_list)
    {
      const auto present_mode = std::find(
        available_present_modes.begin(), available_present_modes.end(), requested_present_mode);
      if (present_mode != available_present_modes.end())
      {
        return *present_mode;
      }
    }
  }
  return vk::PresentModeKHR::eFifo;
}

std::optional<vk::SurfaceFormatKHR> Swapchain::choose_surface_format(
  const std::vector<vk::SurfaceFormatKHR>& available_formats,
  const std::vector<vk::SurfaceFormatKHR>& format_prioriy_list)
{
  assert(!available_formats.empty());

  // Try to find one of the formats in the priority list
  for (const auto requested_format : format_prioriy_list)
  {
    const auto format = std::find_if(available_formats.begin(), available_formats.end(),
      [&](const vk::SurfaceFormatKHR candidate)
      {
        return requested_format.format == candidate.format &&
          requested_format.colorSpace == candidate.colorSpace;
      });
    if (format != available_formats.end())
    {
      spdlog::trace("Selecting swapchain surface format {}", utils::as_string(*format));
      return *format;
    }
  }

  spdlog::trace("None of the surface formats of the priority list are supported");
  spdlog::trace("Selecting surface format from default list");

  static const std::vector<vk::SurfaceFormatKHR> default_surface_format_priority_list{
    { vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear },
    { vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear }
  };

  std::optional<vk::SurfaceFormatKHR> chosen_format{};

  // Try to find one of the formats in the default list
  for (const auto available_format : available_formats)
  {
    const auto format = std::find_if(default_surface_format_priority_list.begin(),
      default_surface_format_priority_list.end(),
      [&](const vk::SurfaceFormatKHR candidate)
      {
        return available_format.format == candidate.format &&
          available_format.colorSpace == candidate.colorSpace;
      });

    if (format != default_surface_format_priority_list.end())
    {
      spdlog::trace("Selecting swapchain image format {}", utils::as_string(*format));
      chosen_format = *format;
    }
  }
  // This can be std::nullopt
  return chosen_format;
}

std::vector<vk::Image> Swapchain::get_swapchain_images()
{
  return m_device.device().getSwapchainImagesKHR(m_swapchain);
}

#if 0
// For presentation
void Swapchain::present(const std::uint32_t img_index)
{
  vk::PresentInfoKHR present_info{};
  
  const auto present_info = make_info<VkPresentInfoKHR>({
    .swapchainCount = 1,
    .pSwapchains = &m_swapchain,
    .pImageIndices = &img_index,
  });
  if (const auto result = vkQueuePresentKHR(m_device.present_queue(), &present_info);
      result != VK_SUCCESS)
  {
    if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR)
    {
      // We need to recreate the swapchain
      setup_swapchain(m_extent.width, m_extent.height, m_vsync_enabled);
    }
    else
    {
      // Exception is thrown if result is not VK_SUCCESS but also not VK_SUBOPTIMAL_KHR
      throw VulkanException("Error: vkQueuePresentKHR failed!", result);
    }
  }
}
#endif

void Swapchain::setup_swapchain(
  const std::uint32_t width, const std::uint32_t height, const bool vsync_enabled)
{
  const auto caps = m_device.surfaceCapabilities(m_surface);

  // From getIntoGameDev

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
  stringList = utils::log_alpha_composite_bits(caps.supportedCompositeAlpha);
  for (std::string line : stringList)
  {
    spdlog::trace("\t\t{}", line);
  }

  spdlog::trace("\tsupported image usage:");
  stringList = utils::log_image_usage_bits(caps.supportedUsageFlags);
  for (std::string line : stringList)
  {
    spdlog::trace("\t\t{}", line);
  }

  // End getIntoGameDev

  std::vector<vk::SurfaceFormatKHR> formats =
    m_device.physicalDevice().getSurfaceFormatsKHR(m_surface);
  //  m_surface_format = choose_swapchain_surface_format(formats);
  m_surface_format = choose_surface_format(formats);

  const vk::Extent2D requested_extent{ width, height };

  auto presentModes = m_device.physicalDevice().getSurfacePresentModesKHR(m_surface);

  static const std::vector<vk::PresentModeKHR> default_present_mode_priorities{
    vk::PresentModeKHR::eMailbox, vk::PresentModeKHR::eFifoRelaxed, vk::PresentModeKHR::eFifo
  };

  const auto composite_alpha =
    choose_composite_alpha(vk::CompositeAlphaFlagBitsKHR::eOpaque, caps.supportedCompositeAlpha);

  if (!composite_alpha)
  {
    throw std::runtime_error("Error: Could not find suitable composite alpha!");
  }

  if ((caps.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment) ==
    static_cast<vk::ImageUsageFlagBits>(0u))
  {
    throw std::runtime_error("Error: Swapchain image usage flag bit "
                             "VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT is not supported!");
  }

  vk::SwapchainKHR old_swapchain = m_swapchain;

  uint32_t imageCount = (caps.maxImageCount != 0)
    ? std::min(caps.minImageCount + 1, caps.maxImageCount)
    : std::max(caps.minImageCount + 1, caps.minImageCount);

  vk::SwapchainCreateInfoKHR createInfo =
    vk::SwapchainCreateInfoKHR(vk::SwapchainCreateFlagsKHR(),                                    //
      m_surface,                                                                                 //
      imageCount,                                                                                //
      m_surface_format.value().format,                                                           //
      m_surface_format.value().colorSpace,                                                       //
      choose_image_extent(requested_extent, caps.minImageExtent, caps.maxImageExtent, m_extent), //
      1, // Image array layers
      vk::ImageUsageFlagBits::eColorAttachment);

  createInfo.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment);

  if (m_device.m_present_queue_family_index != m_device.m_graphics_queue_family_index)
  {
    createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
    createInfo.queueFamilyIndexCount = 2;
    uint32_t queueFamilyIndices[] = { m_device.m_graphics_queue_family_index,
      m_device.m_present_queue_family_index };
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  }
  else
  {
    createInfo.imageSharingMode = vk::SharingMode::eExclusive;
  }

  createInfo.preTransform = (vk::SurfaceTransformFlagBitsKHR::eIdentity & caps.supportedTransforms)
    ? vk::SurfaceTransformFlagBitsKHR::eIdentity
    : caps.currentTransform;

  createInfo.compositeAlpha = composite_alpha.value();
  createInfo.clipped = VK_TRUE;

  // Present modes
  createInfo.presentMode =
    choose_present_mode(m_device.physicalDevice().getSurfacePresentModesKHR(m_surface),
      default_present_mode_priorities, vsync_enabled);
  createInfo.oldSwapchain = old_swapchain;

  spdlog::trace("Using swapchain surface transform {}", vk::to_string(createInfo.preTransform));

  spdlog::trace("Creating swapchain");

  try
  {
    m_swapchain = m_device.device().createSwapchainKHR(createInfo);
  }
  catch (vk::SystemError err)
  {
    throw std::runtime_error("failed to create swap chain!");
  }

  if (old_swapchain != vk::SwapchainKHR(nullptr))
  {
    for (auto const img_view : m_img_views)
    {
      // An image view for each frame
      m_device.device().destroyImageView(img_view);
    }
    m_imgs.clear();
    m_img_views.clear();
    m_device.device().destroySwapchainKHR(old_swapchain);
  }

  m_extent.width = width;
  m_extent.height = height;

  m_imgs = m_device.device().getSwapchainImagesKHR(m_swapchain);
  if (m_imgs.empty())
  {
    throw std::runtime_error("Error: Swapchain image count is 0!");
  }

  spdlog::trace("Creating {} swapchain image views", m_imgs.size());

  m_img_views.resize(m_imgs.size());

  for (std::size_t i = 0; i < m_imgs.size(); i++)
  {
    vk::ImageViewCreateInfo createInfo = {};
    createInfo.image = m_imgs[i];
    createInfo.viewType = vk::ImageViewType::e2D;
    createInfo.format = m_surface_format.value().format;
    createInfo.components.r = vk::ComponentSwizzle::eIdentity;
    createInfo.components.g = vk::ComponentSwizzle::eIdentity;
    createInfo.components.b = vk::ComponentSwizzle::eIdentity;
    createInfo.components.a = vk::ComponentSwizzle::eIdentity;
    createInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    m_device.create_image_view(createInfo, &m_img_views[i], "swapchain image view");
  }
}

Swapchain::Swapchain(Device& device, const VkSurfaceKHR surface, const std::uint32_t width,
  const std::uint32_t height, const bool vsync_enabled)
  : m_device(device)
  , m_surface(surface)
  , m_vsync_enabled(vsync_enabled)
{
#ifdef NEW_SWAPCHAIN
  setup_swapchain(width, height, vsync_enabled);
#else
  m_swapchain2 =
    create_swapchain(m_device.device(), m_device.physicalDevice(), surface, width, height, false);
#endif
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
#ifdef NEW_SWAPCHAIN
  m_device.device().destroySwapchainKHR(m_swapchain);
#else
  m_device.device().destroySwapchainKHR(m_swapchain2.swapchain);
#endif
  for (auto const img_view : m_img_views)
  {
    m_device.device().destroyImageView(img_view);
  }
}
}
