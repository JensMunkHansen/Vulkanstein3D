#include <memory>
#include <sps/vulkan/config.h>

#include <sps/tools/cla_parser.hpp>
#include <sps/vulkan/app.h>
#include <sps/vulkan/meta.hpp>
#include <sps/vulkan/windowsurface.h>

// Dirty-hacks
#include <sps/vulkan/commands.h>
#include <sps/vulkan/framebuffer.h>
#include <sps/vulkan/pipeline.h>

#include <sps/vulkan/fence.h>
#include <sps/vulkan/semaphore.h>

#include <spdlog/spdlog.h>
#include <toml.hpp>

#include <iostream>

namespace sps::vulkan
{

Application::Application(int argc, char** argv)
{
  spdlog::trace("Initialising vulkan-renderer");

  // Not working
  bool enable_renderdoc_instance_layer = false;

  sps::tools::CommandLineArgumentParser cla_parser;
  cla_parser.parse_args(argc, argv);

  spdlog::trace("Application version: {}.{}.{}", APP_VERSION[0], APP_VERSION[1], APP_VERSION[2]);
  spdlog::trace(
    "Engine version: {}.{}.{}", ENGINE_VERSION[0], ENGINE_VERSION[1], ENGINE_VERSION[2]);

  // Load the configuration from the TOML file.
  load_toml_configuration_file("./vulk3D.toml");

  auto enable_renderdoc = cla_parser.arg<bool>("--renderdoc");
  if (enable_renderdoc)
  {
#ifndef SPS_DEBUG
    spdlog::warn("You can't use --renderdoc command line argument in release mode. You have to "
                 "download the code "
                 "and compile it yourself in debug mode");
#else
    if (*enable_renderdoc)
    {
      spdlog::trace("--renderdoc specified, enabling renderdoc instance layer");
      enable_renderdoc_instance_layer = true;
    }
#endif
  }

  bool enable_validation_layers = true;

  // If the user specified command line argument "--no-validation", the Khronos validation instance
  // layer will be disabled. For debug builds, this is not advisable! Always use validation layers
  // during development!
  const auto disable_validation = cla_parser.arg<bool>("--no-validation");
  if (disable_validation.value_or(false))
  {
    spdlog::warn("--no-validation specified, disabling validation layers");
    enable_validation_layers = false;
  }

  spdlog::trace("Creating Vulkan instance");

  m_window_width = 640;
  m_window_height = 480;

  m_window = std::make_unique<sps::vulkan::Window>(
    m_window_title, m_window_width, m_window_height, true, true, m_window_mode);

  m_instance = std::make_unique<sps::vulkan::Instance>(APP_NAME, ENGINE_NAME,
    VK_MAKE_API_VERSION(0, APP_VERSION[0], APP_VERSION[1], APP_VERSION[2]),
    VK_MAKE_API_VERSION(0, ENGINE_VERSION[0], ENGINE_VERSION[1], ENGINE_VERSION[2]),
    enable_validation_layers, enable_renderdoc_instance_layer);

  m_surface = std::make_unique<sps::vulkan::WindowSurface>(m_instance->instance(), m_window->get());

#ifndef NDEBUG
  if (cla_parser.arg<bool>("--stop-on-validation-message").value_or(false))
  {
    spdlog::warn("--stop-on-validation-message specified. Application will call a breakpoint after "
                 "reporting a "
                 "validation layer message");
    m_stop_on_validation_message = true;
  }

  m_instance->setup_vulkan_debug_callback();
#endif

  spdlog::trace("Creating window surface");
  auto preferred_graphics_card = cla_parser.arg<std::uint32_t>("--gpu");
  if (preferred_graphics_card)
  {
    spdlog::trace("Preferential graphics card index {} specified", *preferred_graphics_card);
  }

  const auto enable_vertical_synchronisation = cla_parser.arg<bool>("--vsync");
  if (enable_vertical_synchronisation.value_or(false))
  {
    spdlog::trace("V-sync enabled!");
    m_vsync_enabled = true;
  }
  else
  {
    spdlog::trace("V-sync disabled!");
    m_vsync_enabled = false;
  }

  bool use_distinct_data_transfer_queue = true;

  // Ignore distinct data transfer queue
  const auto forbid_distinct_data_transfer_queue = cla_parser.arg<bool>("--no-separate-data-queue");
  if (forbid_distinct_data_transfer_queue.value_or(false))
  {
    spdlog::warn("Command line argument --no-separate-data-queue specified");
    spdlog::warn(
      "This will force the application to avoid using a distinct queue for data transfer to GPU");
    spdlog::warn("Performance loss might be a result of this!");
    use_distinct_data_transfer_queue = false;
  }

  bool enable_debug_marker_device_extension = true;

  if (!enable_renderdoc_instance_layer)
  {
    // Debug markers are only available if RenderDoc is enabled.
    enable_debug_marker_device_extension = false;
  }

  // Check if Vulkan debug markers should be disabled.
  // Those are only available if RenderDoc instance layer is enabled!
  const auto no_vulkan_debug_markers = cla_parser.arg<bool>("--no-vk-debug-markers");
  if (no_vulkan_debug_markers.value_or(false))
  {
    spdlog::warn("--no-vk-debug-markers specified, disabling useful debug markers!");
    enable_debug_marker_device_extension = false;
  }

  const auto physical_devices = m_instance.get()->instance().enumeratePhysicalDevices();
  if (preferred_graphics_card && *preferred_graphics_card >= physical_devices.size())
  {
    spdlog::critical("GPU index {} out of range!", *preferred_graphics_card);
    throw std::runtime_error("Invalid GPU index");
  }

  const vk::PhysicalDeviceFeatures required_features{
    // Add required physical device features here
  };

  const vk::PhysicalDeviceFeatures optional_features{
    // Add optional physical device features here
  };

  std::vector<const char*> required_extensions{
    // Since we want to draw on a window, we need the swapchain extension
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  };

  const vk::PhysicalDevice physical_device = preferred_graphics_card
    ? physical_devices[*preferred_graphics_card]
    : Device::pick_best_physical_device(
        *m_instance, m_surface->get(), required_features, required_extensions);

  // Create physical and logical device
  m_device =
    std::make_unique<Device>(*m_instance, m_surface->get(), use_distinct_data_transfer_queue,
      physical_device, required_extensions, required_features, optional_features);

  // Create swapchain
  m_swapchain = std::make_unique<Swapchain>(
    *m_device, m_surface->get(), m_window->width(), m_window->height(), m_vsync_enabled);

  // Make pipeline
  make_pipeline();
  finalize_setup();
}

void Application::load_toml_configuration_file(const std::string& file_name)
{
  spdlog::trace("Loading TOML configuration file: {}", file_name);

  std::ifstream toml_file(file_name, std::ios::in);
  if (!toml_file)
  {
    // If you are using CLion, go to "Edit Configurations" and select "Working Directory".
    throw std::runtime_error("Could not find configuration file: " + file_name +
      "! You must set the working directory properly in your IDE");
  }

  toml_file.close();

  // Load the TOML file using toml11.
  auto renderer_configuration = toml::parse(file_name);

  // Search for the title of the configuration file and print it to debug output.
  const auto& configuration_title = toml::find<std::string>(renderer_configuration, "title");
  spdlog::trace("Title: {}", configuration_title);

  using WindowMode = sps::vulkan::Window::Mode;
  const auto& wmodestr =
    toml::find<std::string>(renderer_configuration, "application", "window", "mode");
  if (wmodestr == "windowed")
  {
    m_window_mode = WindowMode::WINDOWED;
  }
  else if (wmodestr == "windowed_fullscreen")
  {
    m_window_mode = WindowMode::WINDOWED_FULLSCREEN;
  }
  else if (wmodestr == "fullscreen")
  {
    m_window_mode = WindowMode::FULLSCREEN;
  }
  else
  {
    spdlog::warn("Invalid application window mode: {}", wmodestr);
    m_window_mode = WindowMode::WINDOWED;
  }

  m_window_width = toml::find<int>(renderer_configuration, "application", "window", "width");
  m_window_height = toml::find<int>(renderer_configuration, "application", "window", "height");
  m_window_title = toml::find<std::string>(renderer_configuration, "application", "window", "name");
  spdlog::trace("Window: {}, {} x {}", m_window_title, m_window_width, m_window_height);

  // Create device here!!

#if 0
  m_texture_files =
    toml::find<std::vector<std::string>>(renderer_configuration, "textures", "files");

  spdlog::trace("Textures:");

  for (const auto& texture_file : m_texture_files)
  {
    spdlog::trace("   - {}", texture_file);
  }

  m_gltf_model_files =
    toml::find<std::vector<std::string>>(renderer_configuration, "glTFmodels", "files");

  spdlog::trace("glTF 2.0 models:");

  for (const auto& gltf_model_file : m_gltf_model_files)
  {
    spdlog::trace("   - {}", gltf_model_file);
  }

  m_vertex_shader_files =
    toml::find<std::vector<std::string>>(renderer_configuration, "shaders", "vertex", "files");

  spdlog::trace("Vertex shaders:");

  for (const auto& vertex_shader_file : m_vertex_shader_files)
  {
    spdlog::trace("   - {}", vertex_shader_file);
  }

  m_fragment_shader_files =
    toml::find<std::vector<std::string>>(renderer_configuration, "shaders", "fragment", "files");

  spdlog::trace("Fragment shaders:");

  for (const auto& fragment_shader_file : m_fragment_shader_files)
  {
    spdlog::trace("   - {}", fragment_shader_file);
  }
#endif
}

void Application::run()
{
  spdlog::trace("Running Application");

  while (!m_window->should_close())
  {
    m_window->poll();
    render();
  }
}

void Application::record_draw_commands(vk::CommandBuffer commandBuffer, uint32_t imageIndex)
{

  vk::CommandBufferBeginInfo beginInfo = {};

  try
  {
    commandBuffer.begin(beginInfo);
  }
  catch (vk::SystemError err)
  {
    if (m_debugMode)
    {
      std::cout << "Failed to begin recording command buffer!" << std::endl;
    }
  }

  vk::RenderPassBeginInfo renderPassInfo = {};
  renderPassInfo.renderPass = m_renderpass;
  renderPassInfo.framebuffer = m_frameBuffers[imageIndex];
  renderPassInfo.renderArea.offset.x = 0;
  renderPassInfo.renderArea.offset.y = 0;
  renderPassInfo.renderArea.extent = m_swapchain->extent();

  vk::ClearValue clearColor = { std::array<float, 4>{ 1.0f, 0.5f, 0.25f, 1.0f } };
  renderPassInfo.clearValueCount = 1;
  renderPassInfo.pClearValues = &clearColor;

  commandBuffer.beginRenderPass(&renderPassInfo, vk::SubpassContents::eInline);

  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);

  commandBuffer.draw(3, 1, 0, 0);

  commandBuffer.endRenderPass();

  try
  {
    commandBuffer.end();
  }
  catch (vk::SystemError err)
  {

    if (m_debugMode)
    {
      std::cout << "failed to record command buffer!" << std::endl;
    }
  }
}

void Application::render()
{

  m_inFlight->block();
  m_inFlight->reset();

  // acquireNextImageKHR(vk::SwapChainKHR, timeout, semaphore_to_signal, fence)
  uint32_t imageIndex{ m_device->device()
                         .acquireNextImageKHR(*m_swapchain->swapchain(), UINT64_MAX,
                           *m_imageAvailable->semaphore(), nullptr)
                         .value };

  vk::CommandBuffer commandBuffer = m_commandBuffers[imageIndex];

  commandBuffer.reset();

  record_draw_commands(commandBuffer, imageIndex);

  vk::SubmitInfo submitInfo = {};

  vk::Semaphore waitSemaphores[] = { *m_imageAvailable->semaphore() };

  vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;

  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vk::Semaphore signalSemaphores[] = { *m_renderFinished->semaphore() };
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  // Get queues from device, graphics and present queue
  try
  {
    m_device->graphics_queue().submit(submitInfo, m_inFlight->get());
  }
  catch (vk::SystemError err)
  {

    if (m_debugMode)
    {
      std::cout << "failed to submit draw command buffer!" << std::endl;
    }
  }

  vk::PresentInfoKHR presentInfo = {};
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;

  vk::SwapchainKHR swapChains[] = { *m_swapchain->swapchain() };
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;

  presentInfo.pImageIndices = &imageIndex;

  vk::Result result = m_device->present_queue().presentKHR(presentInfo);
  if (result != vk::Result::eSuccess)
  {
    if (m_debugMode)
    {
      std::cout << "Error presenting" << std::endl;
    }
  }
}

void Application::make_pipeline()
{
  sps::vulkan::GraphicsPipelineInBundle specification = {};
  specification.device = m_device->device();
  specification.vertexFilepath = "../sps/vulkan/shaders/vertex.spv";
  specification.fragmentFilepath = "../sps/vulkan/shaders/fragment.spv";
  specification.swapchainExtent = m_swapchain->extent();
  specification.swapchainImageFormat = m_swapchain->image_format();

  sps::vulkan::GraphicsPipelineOutBundle output =
    sps::vulkan::create_graphics_pipeline(specification, true);

  m_pipelineLayout = output.layout;
  m_renderpass = output.renderpass;
  m_pipeline = output.pipeline;
}

Application::~Application()
{
  spdlog::trace("Destroying Application");

  m_device->wait_idle();

  m_inFlight.reset(nullptr);
  m_imageAvailable.reset(nullptr);
  m_renderFinished.reset(nullptr);

  m_device->device().destroyCommandPool(m_commandPool);

  m_device->device().destroyPipeline(m_pipeline);
  m_device->device().destroyPipelineLayout(m_pipelineLayout);
  m_device->device().destroyRenderPass(m_renderpass);

  for (auto framebuffer : m_frameBuffers)
  {
    m_device->device().destroyFramebuffer(framebuffer);
  }

  // Swapchain destroyed in renderer
  // Surface ..
  // Instance
  // glfw terminated in renderer
}

void Application::finalize_setup()
{
  sps::vulkan::framebufferInput frameBufferInput;
  frameBufferInput.device = m_device->device();
  frameBufferInput.renderpass = m_renderpass;
  frameBufferInput.swapchainExtent = m_swapchain->extent();

  m_frameBuffers = sps::vulkan::make_framebuffers(frameBufferInput, *m_swapchain, m_debugMode);

  m_commandPool = sps::vulkan::make_command_pool(*m_device, m_debugMode);

  m_mainCommandBuffer = sps::vulkan::make_command_buffers(
    *m_device, *m_swapchain, m_commandPool, m_commandBuffers, true);

  m_inFlight = std::make_unique<Fence>(*m_device, "in-flight", true);
  m_imageAvailable = std::make_unique<Semaphore>(*m_device, "image-available");
  m_renderFinished = std::make_unique<Semaphore>(*m_device, "render-finished");
}
}
