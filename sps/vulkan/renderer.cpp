#include <sps/vulkan/config.h>

#include <spdlog/spdlog.h>
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
}

VulkanRenderer::~VulkanRenderer()
{
  spdlog::trace("Shutting down vulkan renderer");
  if (m_device == VK_NULL_HANDLE)
  {
    return;
  }
  m_device->wait_idle();
}
} // namespace sps::vulkan
