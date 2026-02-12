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

  vk::PhysicalDeviceFeatures required_features{};
  required_features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
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

  // HDR + MSAA color target created by RenderGraph::create_hdr_resources()
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

  // HDR + MSAA destroyed by RenderGraph destructor
  m_depth_stencil.reset();

  m_in_flight.reset();
  m_image_available.reset();
  m_render_finished.clear();
  m_device->device().destroyCommandPool(m_command_pool);
}
} // namespace sps::vulkan
