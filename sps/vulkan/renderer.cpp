#include <sps/vulkan/config.h>

#include <spdlog/spdlog.h>
#include <sps/vulkan/commands.h>
#include <sps/vulkan/meta.hpp>
#include <sps/vulkan/renderer.h>

namespace sps::vulkan
{
VulkanRenderer::VulkanRenderer(const RendererConfig& config)
  : m_window_width(config.window_width)
  , m_window_height(config.window_height)
  , m_window_mode(config.window_mode)
  , m_window_title(config.window_title)
  , m_vsync_enabled(config.vsync)
  , m_msaa_samples(config.msaa_samples)
  , m_depth_format(config.depth_format)
{
  // 1. Window
  spdlog::trace("Creating window");
  m_window = std::make_unique<Window>(
    m_window_title, m_window_width, m_window_height, true, config.resizable, m_window_mode);

  // 2. Vulkan instance
  spdlog::trace("Creating Vulkan instance");
  m_instance = std::make_unique<Instance>(APP_NAME, ENGINE_NAME,
    VK_MAKE_API_VERSION(0, APP_VERSION[0], APP_VERSION[1], APP_VERSION[2]),
    VK_MAKE_API_VERSION(0, ENGINE_VERSION[0], ENGINE_VERSION[1], ENGINE_VERSION[2]),
    config.enable_validation, config.enable_renderdoc);

#ifndef SPS_DEBUG
  m_instance->setup_vulkan_debug_callback();
#endif

  // 3. Window surface
  spdlog::trace("Creating window surface");
  m_surface = std::make_unique<WindowSurface>(m_instance->instance(), m_window->get());

  // 4. Physical device selection + logical device
  spdlog::trace("Creating device");
  const auto physical_devices = m_instance->instance().enumeratePhysicalDevices();

  if (spdlog::get_level() == spdlog::level::trace)
  {
    spdlog::trace("There are {} physical devices available on this system", physical_devices.size());
    for (vk::PhysicalDevice device : physical_devices)
    {
      Device::log_device_properties(device);
    }
  }

  if (config.preferred_gpu_index && *config.preferred_gpu_index >= physical_devices.size())
  {
    spdlog::critical("GPU index {} out of range!", *config.preferred_gpu_index);
    throw std::runtime_error("Invalid GPU index");
  }

  const vk::PhysicalDeviceFeatures required_features{};
  const vk::PhysicalDeviceFeatures optional_features{};

  std::vector<const char*> required_extensions{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  };

  const vk::PhysicalDevice physical_device = config.preferred_gpu_index
    ? physical_devices[*config.preferred_gpu_index]
    : Device::pick_best_physical_device(
        *m_instance, m_surface->get(), required_features, required_extensions, config.preferred_gpu);

  m_device = std::make_unique<Device>(*m_instance, m_surface->get(),
    config.use_distinct_data_transfer_queue,
    physical_device, required_extensions, required_features, optional_features);

  // 5. Swapchain
  std::uint32_t fb_width, fb_height;
  m_window->get_framebuffer_size(fb_width, fb_height);

  spdlog::trace("Creating swapchain");
  m_swapchain = std::make_unique<Swapchain>(*m_device, m_surface->get(), fb_width, fb_height, m_vsync_enabled);

  // 6. Command pool + sync objects
  create_sync_objects();

  // 7. Clamp MSAA to device maximum
  if (m_msaa_samples != vk::SampleCountFlagBits::e1)
  {
    auto maxSamples = m_device->max_usable_sample_count();
    if (m_msaa_samples > maxSamples)
    {
      spdlog::warn("Requested MSAA {}x exceeds device max {}x, clamping",
        static_cast<int>(m_msaa_samples), static_cast<int>(maxSamples));
      m_msaa_samples = maxSamples;
    }
    spdlog::info("MSAA enabled: {}x", static_cast<int>(m_msaa_samples));
  }

  // 8. Depth-stencil buffer
  create_depth_resources();

  // 9. HDR offscreen + optional MSAA color target
  create_hdr_resources();
  if (m_msaa_samples != vk::SampleCountFlagBits::e1)
  {
    create_msaa_color_resources();
  }
}

void VulkanRenderer::create_sync_objects()
{
  spdlog::trace("Creating command pool and sync objects");
  m_command_pool = make_command_pool(*m_device, true);
  m_main_command_buffer = make_command_buffers(*m_device, *m_swapchain, m_command_pool, m_command_buffers, true);

  m_in_flight = std::make_unique<Fence>(*m_device, "in-flight", true);
  m_image_available = std::make_unique<Semaphore>(*m_device, "image-available");
  m_render_finished.resize(m_swapchain->image_count());
  for (std::uint32_t i = 0; i < m_swapchain->image_count(); i++)
  {
    m_render_finished[i] = std::make_unique<Semaphore>(*m_device, "render-finished-" + std::to_string(i));
  }
}

void VulkanRenderer::recreate_sync_objects()
{
  m_render_finished.clear();
  m_render_finished.resize(m_swapchain->image_count());
  for (std::uint32_t i = 0; i < m_swapchain->image_count(); i++)
  {
    m_render_finished[i] = std::make_unique<Semaphore>(*m_device, "render-finished-" + std::to_string(i));
  }
}

void VulkanRenderer::create_depth_resources()
{
  vk::Extent2D extent = m_swapchain->extent();
  m_depth_stencil = std::make_unique<DepthStencilAttachment>(
    *m_device, m_depth_format, extent, m_msaa_samples);
  spdlog::trace("Created depth-stencil buffer {}x{}", extent.width, extent.height);
}

void VulkanRenderer::recreate_depth_resources()
{
  m_depth_stencil.reset();
  create_depth_resources();
}

void VulkanRenderer::create_hdr_resources()
{
  auto dev = m_device->device();
  vk::Extent2D extent = m_swapchain->extent();

  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.extent.width = extent.width;
  imageInfo.extent.height = extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = m_hdr_format;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.initialLayout = vk::ImageLayout::eUndefined;
  imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled
    | vk::ImageUsageFlagBits::eStorage;
  imageInfo.samples = vk::SampleCountFlagBits::e1;
  imageInfo.sharingMode = vk::SharingMode::eExclusive;

  m_hdr_image = dev.createImage(imageInfo);

  vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(m_hdr_image);
  vk::MemoryAllocateInfo allocInfo{};
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = m_device->find_memory_type(
    memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_hdr_image_memory = dev.allocateMemory(allocInfo);
  dev.bindImageMemory(m_hdr_image, m_hdr_image_memory, 0);

  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = m_hdr_image;
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.format = m_hdr_format;
  viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  m_hdr_image_view = dev.createImageView(viewInfo);

  if (!m_hdr_sampler)
  {
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    m_hdr_sampler = dev.createSampler(samplerInfo);
  }

  spdlog::trace("Created HDR image {}x{}", extent.width, extent.height);
}

void VulkanRenderer::create_msaa_color_resources()
{
  vk::Extent2D extent = m_swapchain->extent();

  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.extent.width = extent.width;
  imageInfo.extent.height = extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = m_hdr_format;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.initialLayout = vk::ImageLayout::eUndefined;
  imageInfo.usage =
    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment;
  imageInfo.samples = m_msaa_samples;
  imageInfo.sharingMode = vk::SharingMode::eExclusive;

  m_hdr_msaa_image = m_device->device().createImage(imageInfo);

  vk::MemoryRequirements memRequirements =
    m_device->device().getImageMemoryRequirements(m_hdr_msaa_image);

  vk::MemoryAllocateInfo allocInfo{};
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = m_device->find_memory_type(
    memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_hdr_msaa_image_memory = m_device->device().allocateMemory(allocInfo);
  m_device->device().bindImageMemory(m_hdr_msaa_image, m_hdr_msaa_image_memory, 0);

  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = m_hdr_msaa_image;
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.format = m_hdr_format;
  viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  m_hdr_msaa_image_view = m_device->device().createImageView(viewInfo);

  spdlog::trace("Created HDR MSAA color image {}x{} ({}x samples)", extent.width, extent.height,
    static_cast<int>(m_msaa_samples));
}

void VulkanRenderer::destroy_hdr_resources()
{
  auto dev = m_device->device();

  if (m_hdr_image_view)
  {
    dev.destroyImageView(m_hdr_image_view);
    m_hdr_image_view = VK_NULL_HANDLE;
  }
  if (m_hdr_image)
  {
    dev.destroyImage(m_hdr_image);
    m_hdr_image = VK_NULL_HANDLE;
  }
  if (m_hdr_image_memory)
  {
    dev.freeMemory(m_hdr_image_memory);
    m_hdr_image_memory = VK_NULL_HANDLE;
  }

  if (m_hdr_msaa_image_view)
  {
    dev.destroyImageView(m_hdr_msaa_image_view);
    m_hdr_msaa_image_view = VK_NULL_HANDLE;
  }
  if (m_hdr_msaa_image)
  {
    dev.destroyImage(m_hdr_msaa_image);
    m_hdr_msaa_image = VK_NULL_HANDLE;
  }
  if (m_hdr_msaa_image_memory)
  {
    dev.freeMemory(m_hdr_msaa_image_memory);
    m_hdr_msaa_image_memory = VK_NULL_HANDLE;
  }
}

void VulkanRenderer::recreate_hdr_resources()
{
  destroy_hdr_resources();
  create_hdr_resources();
  if (m_msaa_samples != vk::SampleCountFlagBits::e1)
  {
    create_msaa_color_resources();
  }
}

bool VulkanRenderer::save_screenshot(const std::string& filepath)
{
  m_device->wait_idle();

  auto images = m_swapchain->images();
  if (images.empty())
  {
    spdlog::error("No swapchain images available for screenshot");
    return false;
  }

  vk::Image source_image = images[0];
  vk::Format format = m_swapchain->image_format();
  vk::Extent2D extent = m_swapchain->extent();

  return sps::vulkan::save_screenshot(
    *m_device, m_command_pool, source_image, format, extent, filepath);
}

VulkanRenderer::~VulkanRenderer()
{
  spdlog::trace("Shutting down vulkan renderer");
  if (m_device == VK_NULL_HANDLE)
  {
    return;
  }
  m_device->wait_idle();

  // Destroy rendering resources before command pool and device
  destroy_hdr_resources();
  if (m_hdr_sampler)
    m_device->device().destroySampler(m_hdr_sampler);
  m_depth_stencil.reset();

  m_in_flight.reset();
  m_image_available.reset();
  m_render_finished.clear();
  m_device->device().destroyCommandPool(m_command_pool);
}
} // namespace sps::vulkan
