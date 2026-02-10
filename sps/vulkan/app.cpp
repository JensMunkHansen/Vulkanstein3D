#include <sps/vulkan/config.h>

#include <memory>
#include <spdlog/common.h>
#include <sps/vulkan/config.h>

#include <sps/tools/cla_parser.hpp>
#include <sps/vulkan/app.h>
#include <sps/vulkan/debug_constants.h>
#include <sps/vulkan/meta.hpp>
#include <sps/vulkan/screenshot.h>

#include <fstream>
#include <sps/vulkan/vertex.h>
#include <sps/vulkan/windowsurface.h>

#include <sps/vulkan/device.h>

// Dirty-hacks
#include <sps/vulkan/commands.h>
#include <sps/vulkan/framebuffer.h>
#include <sps/vulkan/pipeline.h>
#include <sps/vulkan/shaders.h>

#include <sps/vulkan/fence.h>
#include <sps/vulkan/semaphore.h>

#include <sps/vulkan/stages/composite_stage.h>
#include <sps/vulkan/stages/debug_2d_stage.h>
#include <sps/vulkan/stages/sss_blur_stage.h>
#include <sps/vulkan/stages/raster_blend_stage.h>
#include <sps/vulkan/stages/raster_opaque_stage.h>
#include <sps/vulkan/stages/ray_tracing_stage.h>
#include <sps/vulkan/stages/ui_stage.h>

#include <spdlog/spdlog.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace sps::vulkan
{

Application::Application(int argc, char** argv)
{
  m_lastTime = glfwGetTime();

  spdlog::trace("Initialising vulkan-renderer");

  // Not working
  bool enable_renderdoc_instance_layer = false;

  sps::tools::CommandLineArgumentParser cla_parser;
  cla_parser.parse_args(argc, argv);

  spdlog::trace("Application version: {}.{}.{}", APP_VERSION[0], APP_VERSION[1], APP_VERSION[2]);
  spdlog::trace(
    "Engine version: {}.{}.{}", ENGINE_VERSION[0], ENGINE_VERSION[1], ENGINE_VERSION[2]);

  // Load the configuration from the TOML file.
  apply_config(parse_toml("./vulk3D.toml"));

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

  m_window_width = 800;
  m_window_height = 600;

  const bool resizeable = true;
  m_window = std::make_unique<sps::vulkan::Window>(
    m_window_title, m_window_width, m_window_height, true, resizeable, m_window_mode);

  m_instance = std::make_unique<sps::vulkan::Instance>(APP_NAME, ENGINE_NAME,
    VK_MAKE_API_VERSION(0, APP_VERSION[0], APP_VERSION[1], APP_VERSION[2]),
    VK_MAKE_API_VERSION(0, ENGINE_VERSION[0], ENGINE_VERSION[1], ENGINE_VERSION[2]),
    enable_validation_layers, enable_renderdoc_instance_layer);

  m_surface = std::make_unique<sps::vulkan::WindowSurface>(m_instance->instance(), m_window->get());

#ifndef SPS_DEBUG
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

  // V-sync defaults to ON (prevents tearing), use --no-vsync to disable
  const auto disable_vertical_synchronisation = cla_parser.arg<bool>("--no-vsync");
  if (disable_vertical_synchronisation.value_or(false))
  {
    spdlog::trace("V-sync disabled!");
    m_vsync_enabled = false;
  }
  else
  {
    spdlog::trace("V-sync enabled!");
    m_vsync_enabled = true;
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

  if (spdlog::get_level() == spdlog::level::trace)
  {
    spdlog::trace(
      "There are {} physical devices available on this system", physical_devices.size());
    /*
     * check if a suitable device can be found
     */
    for (vk::PhysicalDevice device : physical_devices)
    {
      Device::log_device_properties(device);
    }
  }

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
        *m_instance, m_surface->get(), required_features, required_extensions, m_preferred_gpu);

  // Create physical and logical device
  m_device =
    std::make_unique<Device>(*m_instance, m_surface->get(), use_distinct_data_transfer_queue,
      physical_device, required_extensions, required_features, optional_features);

  // Setup resize callback BEFORE creating swapchain
  m_window->set_user_ptr(m_window.get());
  m_window->set_resize_callback(
    [](GLFWwindow* window, int width, int height)
    {
      auto* win = static_cast<Window*>(glfwGetWindowUserPointer(window));
      win->set_resize_pending(
        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    });

  // Get actual framebuffer size (may differ from requested size)
  std::uint32_t fb_width, fb_height;
  m_window->get_framebuffer_size(fb_width, fb_height);

  // Create swapchain with actual framebuffer size
  m_swapchain =
    std::make_unique<Swapchain>(*m_device, m_surface->get(), fb_width, fb_height, m_vsync_enabled);

  // Setup camera
  setup_camera();

  // Clamp MSAA to device maximum
  if (m_msaaSamples != vk::SampleCountFlagBits::e1)
  {
    auto maxSamples = m_device->max_usable_sample_count();
    if (m_msaaSamples > maxSamples)
    {
      spdlog::warn("Requested MSAA {}x exceeds device max {}x, clamping",
        static_cast<int>(m_msaaSamples), static_cast<int>(maxSamples));
      m_msaaSamples = maxSamples;
    }
    spdlog::info("MSAA enabled: {}x", static_cast<int>(m_msaaSamples));
  }

  // Create scene manager and load initial scene
  m_scene_manager = std::make_unique<SceneManager>(*m_device);
  m_scene_manager->set_ibl_settings(m_ibl_settings);
  m_scene_manager->create_defaults(m_hdr_file);
  auto load_result = m_scene_manager->load_initial_scene(m_geometry_source, m_gltf_file, m_ply_file);

  // Create depth buffer
  create_depth_resources();

  // Create MSAA color image (if MSAA enabled)
  if (m_msaaSamples != vk::SampleCountFlagBits::e1)
  {
    create_msaa_color_resources();
  }

  // Create uniform buffer and descriptor
  create_uniform_buffer();

  m_scene_manager->create_descriptors(m_uniform_buffer->buffer());

  // Make pipeline (needs descriptor layout and vertex format)
  make_pipeline();

  // Reset camera to frame loaded scene
  if (load_result.success && load_result.bounds.valid())
  {
    float bounds[6];
    load_result.bounds.to_bounds(bounds);
    m_camera.reset_camera(bounds);
  }

  finalize_setup();

  // Setup input callbacks
  glfwSetWindowUserPointer(m_window->get(), this);
  glfwSetKeyCallback(m_window->get(), key_callback);
  glfwSetCursorPosCallback(m_window->get(), mouse_callback);
  glfwSetScrollCallback(m_window->get(), scroll_callback);
}

void Application::apply_config(AppConfig config)
{
  m_preferred_gpu = std::move(config.preferred_gpu);
  m_window_mode = config.window_mode;
  m_window_width = config.window_width;
  m_window_height = config.window_height;
  m_window_title = std::move(config.window_title);
  m_backfaceCulling = config.backface_culling;
  m_msaaSamples = config.msaa_samples;
  m_use_raytracing = config.use_raytracing;
  m_geometry_source = std::move(config.geometry_source);
  m_ply_file = std::move(config.ply_file);
  m_gltf_file = std::move(config.gltf_file);
  m_hdr_file = std::move(config.hdr_file);
  m_gltf_models = std::move(config.gltf_models);
  m_current_model_index = config.current_model_index;
  m_hdr_files = std::move(config.hdr_files);
  m_current_hdr_index = config.current_hdr_index;
  m_ibl_settings = config.ibl_settings;
  m_shininess = config.shininess;
  m_specularStrength = config.specular_strength;
  m_light = std::move(config.light);
}

void Application::setup_camera()
{
  // Position camera to look at the triangle
  m_camera.set_position(0.0f, 0.0f, 2.0f);
  m_camera.set_focal_point(0.0f, 0.0f, 0.0f);
  m_camera.set_view_up(0.0f, 1.0f, 0.0f);
  m_camera.set_view_angle(45.0f);
  m_camera.set_clipping_range(0.1f, 100.0f);

  std::uint32_t width, height;
  m_window->get_framebuffer_size(width, height);
  m_camera.set_aspect_ratio(static_cast<float>(width) / static_cast<float>(height));
}

void Application::create_depth_resources()
{
  vk::Extent2D extent = m_swapchain->extent();

  // Create depth image
  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.extent.width = extent.width;
  imageInfo.extent.height = extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = m_depthFormat;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.initialLayout = vk::ImageLayout::eUndefined;
  imageInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
  imageInfo.samples = m_msaaSamples;
  imageInfo.sharingMode = vk::SharingMode::eExclusive;

  m_depthImage = m_device->device().createImage(imageInfo);

  // Allocate memory
  vk::MemoryRequirements memRequirements =
    m_device->device().getImageMemoryRequirements(m_depthImage);

  vk::MemoryAllocateInfo allocInfo{};
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = m_device->find_memory_type(
    memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_depthImageMemory = m_device->device().allocateMemory(allocInfo);
  m_device->device().bindImageMemory(m_depthImage, m_depthImageMemory, 0);

  // Create image view
  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = m_depthImage;
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.format = m_depthFormat;
  viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  m_depthImageView = m_device->device().createImageView(viewInfo);

  spdlog::trace("Created depth buffer {}x{}", extent.width, extent.height);
}

void Application::create_msaa_color_resources()
{
  vk::Extent2D extent = m_swapchain->extent();

  // MSAA color target uses HDR format (resolves to m_hdrImage)
  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.extent.width = extent.width;
  imageInfo.extent.height = extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = m_hdrFormat;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.initialLayout = vk::ImageLayout::eUndefined;
  imageInfo.usage =
    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment;
  imageInfo.samples = m_msaaSamples;
  imageInfo.sharingMode = vk::SharingMode::eExclusive;

  m_hdrMsaaImage = m_device->device().createImage(imageInfo);

  vk::MemoryRequirements memRequirements =
    m_device->device().getImageMemoryRequirements(m_hdrMsaaImage);

  vk::MemoryAllocateInfo allocInfo{};
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = m_device->find_memory_type(
    memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_hdrMsaaImageMemory = m_device->device().allocateMemory(allocInfo);
  m_device->device().bindImageMemory(m_hdrMsaaImage, m_hdrMsaaImageMemory, 0);

  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = m_hdrMsaaImage;
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.format = m_hdrFormat;
  viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  m_hdrMsaaImageView = m_device->device().createImageView(viewInfo);

  spdlog::trace("Created HDR MSAA color image {}x{} ({}x samples)", extent.width, extent.height,
    static_cast<int>(m_msaaSamples));
}

void Application::create_uniform_buffer()
{
  m_uniform_buffer =
    std::make_unique<UniformBuffer<UniformBufferObject>>(*m_device, "camera uniform buffer");
  spdlog::trace("Created uniform buffer");
}

void Application::create_hdr_resources()
{
  auto dev = m_device->device();
  vk::Extent2D extent = m_swapchain->extent();

  // Single-sample HDR image (resolve target for MSAA, or direct render target)
  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.extent.width = extent.width;
  imageInfo.extent.height = extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = m_hdrFormat;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.initialLayout = vk::ImageLayout::eUndefined;
  imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled
    | vk::ImageUsageFlagBits::eStorage;
  imageInfo.samples = vk::SampleCountFlagBits::e1;
  imageInfo.sharingMode = vk::SharingMode::eExclusive;

  m_hdrImage = dev.createImage(imageInfo);

  vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(m_hdrImage);
  vk::MemoryAllocateInfo allocInfo{};
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = m_device->find_memory_type(
    memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_hdrImageMemory = dev.allocateMemory(allocInfo);
  dev.bindImageMemory(m_hdrImage, m_hdrImageMemory, 0);

  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = m_hdrImage;
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.format = m_hdrFormat;
  viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  m_hdrImageView = dev.createImageView(viewInfo);

  // Create sampler for composite pass to sample the HDR image
  if (!m_hdrSampler)
  {
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    m_hdrSampler = dev.createSampler(samplerInfo);
  }

  spdlog::trace("Created HDR image {}x{}", extent.width, extent.height);
}

void Application::destroy_hdr_resources()
{
  auto dev = m_device->device();

  if (m_hdrImageView)
  {
    dev.destroyImageView(m_hdrImageView);
    m_hdrImageView = VK_NULL_HANDLE;
  }
  if (m_hdrImage)
  {
    dev.destroyImage(m_hdrImage);
    m_hdrImage = VK_NULL_HANDLE;
  }
  if (m_hdrImageMemory)
  {
    dev.freeMemory(m_hdrImageMemory);
    m_hdrImageMemory = VK_NULL_HANDLE;
  }

  if (m_hdrMsaaImageView)
  {
    dev.destroyImageView(m_hdrMsaaImageView);
    m_hdrMsaaImageView = VK_NULL_HANDLE;
  }
  if (m_hdrMsaaImage)
  {
    dev.destroyImage(m_hdrMsaaImage);
    m_hdrMsaaImage = VK_NULL_HANDLE;
  }
  if (m_hdrMsaaImageMemory)
  {
    dev.freeMemory(m_hdrMsaaImageMemory);
    m_hdrMsaaImageMemory = VK_NULL_HANDLE;
  }
}

void Application::create_composite_pipeline()
{
  auto dev = m_device->device();

  // Descriptor set layout: single sampler for the HDR buffer
  vk::DescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
  binding.descriptorCount = 1;
  binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

  vk::DescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &binding;
  m_composite_descriptor_layout = dev.createDescriptorSetLayout(layoutInfo);

  // Descriptor pool
  vk::DescriptorPoolSize poolSize{};
  poolSize.type = vk::DescriptorType::eCombinedImageSampler;
  poolSize.descriptorCount = 1;

  vk::DescriptorPoolCreateInfo poolInfo{};
  poolInfo.maxSets = 1;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  m_composite_descriptor_pool = dev.createDescriptorPool(poolInfo);

  // Allocate descriptor set
  vk::DescriptorSetAllocateInfo allocInfo{};
  allocInfo.descriptorPool = m_composite_descriptor_pool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &m_composite_descriptor_layout;
  m_composite_descriptor_set = dev.allocateDescriptorSets(allocInfo)[0];

  // Update descriptor with HDR image
  vk::DescriptorImageInfo imageInfo{};
  imageInfo.sampler = m_hdrSampler;
  imageInfo.imageView = m_hdrImageView;
  imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

  vk::WriteDescriptorSet write{};
  write.dstSet = m_composite_descriptor_set;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
  write.pImageInfo = &imageInfo;
  dev.updateDescriptorSets(write, {});

  // Pipeline layout: descriptor set + push constants (exposure + tonemapMode)
  vk::PushConstantRange pcRange{};
  pcRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
  pcRange.offset = 0;
  pcRange.size = 8; // float exposure + int tonemapMode

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &m_composite_descriptor_layout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pcRange;
  m_composite_pipelineLayout = dev.createPipelineLayout(pipelineLayoutInfo);

  // Create pipeline using existing infrastructure
  sps::vulkan::GraphicsPipelineInBundle specification{};
  specification.device = dev;
  specification.vertexFilepath = SHADER_DIR "fullscreen_quad.spv";
  specification.fragmentFilepath = SHADER_DIR "composite.spv";
  specification.swapchainExtent = m_swapchain->extent();
  specification.swapchainImageFormat = m_swapchain->image_format();
  specification.backfaceCulling = false;
  specification.existingRenderPass = m_composite_renderpass;
  specification.existingPipelineLayout = m_composite_pipelineLayout;
  specification.depthTestEnabled = false;
  // No MSAA for composite pass
  specification.msaaSamples = vk::SampleCountFlagBits::e1;

  auto output = sps::vulkan::create_graphics_pipeline(specification, true);
  m_composite_pipeline = output.pipeline;

  spdlog::info("Created composite pipeline");
}

void Application::create_composite_framebuffers()
{
  auto dev = m_device->device();
  vk::Extent2D extent = m_swapchain->extent();
  const auto& imageViews = m_swapchain->image_views();

  m_composite_framebuffers.resize(imageViews.size());
  for (size_t i = 0; i < imageViews.size(); i++)
  {
    vk::ImageView attachments[] = { imageViews[i] };

    vk::FramebufferCreateInfo fbInfo{};
    fbInfo.renderPass = m_composite_renderpass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = attachments;
    fbInfo.width = extent.width;
    fbInfo.height = extent.height;
    fbInfo.layers = 1;

    m_composite_framebuffers[i] = dev.createFramebuffer(fbInfo);
  }
}

void Application::create_blur_resources()
{
  auto dev = m_device->device();
  vk::Extent2D extent = m_swapchain->extent();
  m_blur_extent = extent;

  // Create ping image (intermediate for separable blur)
  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.extent.width = extent.width;
  imageInfo.extent.height = extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = m_hdrFormat;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.initialLayout = vk::ImageLayout::eUndefined;
  imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
  imageInfo.samples = vk::SampleCountFlagBits::e1;
  imageInfo.sharingMode = vk::SharingMode::eExclusive;

  m_blurPingImage = dev.createImage(imageInfo);

  vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(m_blurPingImage);
  vk::MemoryAllocateInfo allocInfo{};
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = m_device->find_memory_type(
    memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_blurPingImageMemory = dev.allocateMemory(allocInfo);
  dev.bindImageMemory(m_blurPingImage, m_blurPingImageMemory, 0);

  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = m_blurPingImage;
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.format = m_hdrFormat;
  viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  m_blurPingImageView = dev.createImageView(viewInfo);

  // Transition ping image to General layout using a one-shot command buffer
  {
    vk::CommandBufferAllocateInfo allocCmdInfo{};
    allocCmdInfo.commandPool = m_commandPool;
    allocCmdInfo.level = vk::CommandBufferLevel::ePrimary;
    allocCmdInfo.commandBufferCount = 1;
    auto cmd = dev.allocateCommandBuffers(allocCmdInfo)[0];
    cmd.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_blurPingImage;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    cmd.pipelineBarrier(
      vk::PipelineStageFlagBits::eTopOfPipe,
      vk::PipelineStageFlagBits::eComputeShader,
      {}, {}, {}, barrier);

    cmd.end();
    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    m_device->graphics_queue().submit(submitInfo);
    m_device->graphics_queue().waitIdle();
    dev.freeCommandBuffers(m_commandPool, cmd);
  }

  // Create descriptor set layout (2 storage images)
  if (!m_sss_blur_descriptor_layout)
  {
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = vk::DescriptorType::eStorageImage;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;
    bindings[1].binding = 1;
    bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    m_sss_blur_descriptor_layout = dev.createDescriptorSetLayout(layoutInfo);
  }

  // Create descriptor pool (2 sets, 4 storage images total)
  if (!m_sss_blur_descriptor_pool)
  {
    vk::DescriptorPoolSize poolSize{};
    poolSize.type = vk::DescriptorType::eStorageImage;
    poolSize.descriptorCount = 4;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    m_sss_blur_descriptor_pool = dev.createDescriptorPool(poolInfo);
  }
  else
  {
    dev.resetDescriptorPool(m_sss_blur_descriptor_pool);
  }

  // Allocate 2 descriptor sets
  std::array<vk::DescriptorSetLayout, 2> layouts = { m_sss_blur_descriptor_layout, m_sss_blur_descriptor_layout };
  vk::DescriptorSetAllocateInfo dsAllocInfo{};
  dsAllocInfo.descriptorPool = m_sss_blur_descriptor_pool;
  dsAllocInfo.descriptorSetCount = 2;
  dsAllocInfo.pSetLayouts = layouts.data();
  auto sets = dev.allocateDescriptorSets(dsAllocInfo);
  m_sss_blur_h_descriptor = sets[0]; // H pass: read HDR, write ping
  m_sss_blur_v_descriptor = sets[1]; // V pass: read ping, write HDR

  // Update H descriptor: binding 0 = HDR (read), binding 1 = ping (write)
  vk::DescriptorImageInfo hdrInfo{};
  hdrInfo.imageView = m_hdrImageView;
  hdrInfo.imageLayout = vk::ImageLayout::eGeneral;

  vk::DescriptorImageInfo pingInfo{};
  pingInfo.imageView = m_blurPingImageView;
  pingInfo.imageLayout = vk::ImageLayout::eGeneral;

  std::array<vk::WriteDescriptorSet, 4> writes{};
  writes[0].dstSet = m_sss_blur_h_descriptor;
  writes[0].dstBinding = 0;
  writes[0].descriptorCount = 1;
  writes[0].descriptorType = vk::DescriptorType::eStorageImage;
  writes[0].pImageInfo = &hdrInfo;

  writes[1].dstSet = m_sss_blur_h_descriptor;
  writes[1].dstBinding = 1;
  writes[1].descriptorCount = 1;
  writes[1].descriptorType = vk::DescriptorType::eStorageImage;
  writes[1].pImageInfo = &pingInfo;

  writes[2].dstSet = m_sss_blur_v_descriptor;
  writes[2].dstBinding = 0;
  writes[2].descriptorCount = 1;
  writes[2].descriptorType = vk::DescriptorType::eStorageImage;
  writes[2].pImageInfo = &pingInfo;

  writes[3].dstSet = m_sss_blur_v_descriptor;
  writes[3].dstBinding = 1;
  writes[3].descriptorCount = 1;
  writes[3].descriptorType = vk::DescriptorType::eStorageImage;
  writes[3].pImageInfo = &hdrInfo;

  dev.updateDescriptorSets(writes, {});

  // Create pipeline layout
  if (!m_sss_blur_pipelineLayout)
  {
    vk::PushConstantRange pcRange{};
    pcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pcRange.offset = 0;
    pcRange.size = 8; // float blurWidth + int direction

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_sss_blur_descriptor_layout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pcRange;
    m_sss_blur_pipelineLayout = dev.createPipelineLayout(pipelineLayoutInfo);
  }

  // Create compute pipeline
  if (!m_sss_blur_pipeline)
  {
    auto shaderModule = sps::vulkan::createModule(SHADER_DIR "sss_blur.spv", dev, true);

    vk::PipelineShaderStageCreateInfo stageInfo{};
    stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = m_sss_blur_pipelineLayout;

    m_sss_blur_pipeline = dev.createComputePipeline(nullptr, pipelineInfo).value;

    dev.destroyShaderModule(shaderModule);
  }

  spdlog::info("Created SSS blur resources {}x{}", extent.width, extent.height);
}

void Application::destroy_blur_resources()
{
  auto dev = m_device->device();

  if (m_blurPingImageView)
  {
    dev.destroyImageView(m_blurPingImageView);
    m_blurPingImageView = VK_NULL_HANDLE;
  }
  if (m_blurPingImage)
  {
    dev.destroyImage(m_blurPingImage);
    m_blurPingImage = VK_NULL_HANDLE;
  }
  if (m_blurPingImageMemory)
  {
    dev.freeMemory(m_blurPingImageMemory);
    m_blurPingImageMemory = VK_NULL_HANDLE;
  }
}

void Application::create_rt_storage_image()
{
  auto dev = m_device->device();
  vk::Extent2D extent = m_swapchain->extent();

  // Create storage image for RT output
  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.format = vk::Format::eR8G8B8A8Unorm;
  imageInfo.extent.width = extent.width;
  imageInfo.extent.height = extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = vk::SampleCountFlagBits::e1;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc;
  imageInfo.initialLayout = vk::ImageLayout::eUndefined;

  m_rt_image = dev.createImage(imageInfo);

  vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(m_rt_image);

  vk::MemoryAllocateInfo allocInfo{};
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex =
    m_device->find_memory_type(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_rt_image_memory = dev.allocateMemory(allocInfo);
  dev.bindImageMemory(m_rt_image, m_rt_image_memory, 0);

  // Create image view
  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = m_rt_image;
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.format = vk::Format::eR8G8B8A8Unorm;
  viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  m_rt_image_view = dev.createImageView(viewInfo);

  spdlog::trace("Created RT storage image {}x{}", extent.width, extent.height);
}

void Application::create_rt_descriptor()
{
  auto dev = m_device->device();

  // Create descriptor pool
  std::vector<vk::DescriptorPoolSize> poolSizes = {
    { vk::DescriptorType::eAccelerationStructureKHR, 1 }, { vk::DescriptorType::eStorageImage, 1 },
    { vk::DescriptorType::eUniformBuffer, 1 },
    { vk::DescriptorType::eStorageBuffer, 2 } // vertex + index buffers
  };

  vk::DescriptorPoolCreateInfo poolInfo{};
  poolInfo.maxSets = 1;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();

  m_rt_descriptor_pool = dev.createDescriptorPool(poolInfo);

  // Create descriptor set layout
  std::vector<vk::DescriptorSetLayoutBinding> bindings = { // Binding 0: TLAS
    { 0, vk::DescriptorType::eAccelerationStructureKHR, 1,
      vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR },
    // Binding 1: Storage image
    { 1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR },
    // Binding 2: Uniform buffer
    { 2, vk::DescriptorType::eUniformBuffer, 1,
      vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR },
    // Binding 3: Vertex buffer (for vertex colors/normals in closesthit)
    { 3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR },
    // Binding 4: Index buffer (for triangle lookup in closesthit)
    { 4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR }
  };

  vk::DescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  m_rt_descriptor_layout = dev.createDescriptorSetLayout(layoutInfo);

  // Allocate descriptor set
  vk::DescriptorSetAllocateInfo allocInfo{};
  allocInfo.descriptorPool = m_rt_descriptor_pool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &m_rt_descriptor_layout;

  auto sets = dev.allocateDescriptorSets(allocInfo);
  m_rt_descriptor_set = sets[0];

  // Update descriptor set
  vk::WriteDescriptorSetAccelerationStructureKHR asWrite{};
  asWrite.accelerationStructureCount = 1;
  vk::AccelerationStructureKHR tlas = m_tlas->handle();
  asWrite.pAccelerationStructures = &tlas;

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.imageView = m_rt_image_view;
  imageInfo.imageLayout = vk::ImageLayout::eGeneral;

  vk::DescriptorBufferInfo bufferInfo{};
  bufferInfo.buffer = m_uniform_buffer->buffer();
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(UniformBufferObject);

  vk::DescriptorBufferInfo vertexBufferInfo{};
  vertexBufferInfo.buffer = m_scene_manager->mesh()->vertex_buffer();
  vertexBufferInfo.offset = 0;
  vertexBufferInfo.range = VK_WHOLE_SIZE;

  vk::DescriptorBufferInfo indexBufferInfo{};
  indexBufferInfo.buffer = m_scene_manager->mesh()->index_buffer();
  indexBufferInfo.offset = 0;
  indexBufferInfo.range = VK_WHOLE_SIZE;

  std::vector<vk::WriteDescriptorSet> writes(5);

  // TLAS
  writes[0].dstSet = m_rt_descriptor_set;
  writes[0].dstBinding = 0;
  writes[0].descriptorCount = 1;
  writes[0].descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
  writes[0].pNext = &asWrite;

  // Storage image
  writes[1].dstSet = m_rt_descriptor_set;
  writes[1].dstBinding = 1;
  writes[1].descriptorCount = 1;
  writes[1].descriptorType = vk::DescriptorType::eStorageImage;
  writes[1].pImageInfo = &imageInfo;

  // Uniform buffer
  writes[2].dstSet = m_rt_descriptor_set;
  writes[2].dstBinding = 2;
  writes[2].descriptorCount = 1;
  writes[2].descriptorType = vk::DescriptorType::eUniformBuffer;
  writes[2].pBufferInfo = &bufferInfo;

  // Vertex buffer
  writes[3].dstSet = m_rt_descriptor_set;
  writes[3].dstBinding = 3;
  writes[3].descriptorCount = 1;
  writes[3].descriptorType = vk::DescriptorType::eStorageBuffer;
  writes[3].pBufferInfo = &vertexBufferInfo;

  // Index buffer
  writes[4].dstSet = m_rt_descriptor_set;
  writes[4].dstBinding = 4;
  writes[4].descriptorCount = 1;
  writes[4].descriptorType = vk::DescriptorType::eStorageBuffer;
  writes[4].pBufferInfo = &indexBufferInfo;

  dev.updateDescriptorSets(writes, {});

  spdlog::trace("Created RT descriptor set");
}

void Application::create_rt_pipeline()
{
  m_rt_pipeline = std::make_unique<RayTracingPipeline>(*m_device);
  m_rt_pipeline->create(
    SHADER_DIR "raygen.spv",
    SHADER_DIR "miss.spv",
    SHADER_DIR "closesthit.spv",
    m_rt_descriptor_layout);
}

void Application::build_acceleration_structures()
{
  if (!m_device->supports_ray_tracing() || !m_scene_manager->mesh())
  {
    spdlog::warn("Cannot build acceleration structures: RT not supported or no mesh");
    return;
  }

  // Create command buffer for AS building
  vk::CommandBufferAllocateInfo allocInfo{};
  allocInfo.commandPool = m_commandPool;
  allocInfo.level = vk::CommandBufferLevel::ePrimary;
  allocInfo.commandBufferCount = 1;

  auto cmdBuffers = m_device->device().allocateCommandBuffers(allocInfo);
  vk::CommandBuffer cmd = cmdBuffers[0];

  vk::CommandBufferBeginInfo beginInfo{};
  beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
  cmd.begin(beginInfo);

  // Build BLAS
  m_blas = std::make_unique<AccelerationStructure>(*m_device, "mesh BLAS");
  m_blas->build_blas(cmd, *m_scene_manager->mesh());

  // Build TLAS
  m_tlas = std::make_unique<AccelerationStructure>(*m_device, "scene TLAS");
  std::vector<std::pair<const AccelerationStructure*, glm::mat4>> instances;
  instances.push_back({ m_blas.get(), glm::mat4(1.0f) });
  m_tlas->build_tlas(cmd, instances);

  cmd.end();

  // Submit and wait
  vk::SubmitInfo submitInfo{};
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;

  m_device->graphics_queue().submit(submitInfo, nullptr);
  m_device->wait_idle();

  m_device->device().freeCommandBuffers(m_commandPool, cmd);

  spdlog::trace("Built acceleration structures");
}

void Application::update_uniform_buffer()
{
  // Keep clipping range in sync with camera distance to scene
  if (m_scene_manager->bounds().valid())
  {
    float bounds[6];
    m_scene_manager->bounds().to_bounds(bounds);
    m_camera.reset_clipping_range(bounds);
  }

  UniformBufferObject ubo{};

  // Rasterization matrices (with Vulkan Y-flip)
  ubo.view = m_camera.view_matrix();
  ubo.proj = m_camera.projection_matrix();

  // Ray tracing needs inverse matrices for ray generation
  ubo.viewInverse = glm::inverse(ubo.view);
  ubo.projInverse = glm::inverse(ubo.proj);

  // Light setup from light object
  if (m_light_enabled)
  {
    ubo.lightPosition = m_light->position_or_direction();
    ubo.lightColor = m_light->color_with_intensity();
    ubo.lightAmbient = m_light->ambient_vec4();
  }
  else
  {
    ubo.lightPosition = glm::vec4(0.0f);
    ubo.lightColor = glm::vec4(0.0f);
    ubo.lightAmbient = glm::vec4(0.0f);
  }

  // Camera position for specular calculation
  ubo.viewPos = glm::vec4(m_camera.position(), 1.0f);

  if (m_debug_2d_mode)
  {
    // 2D mode: repurpose uniforms for texture viewing
    // viewPos: xy = pan offset, z = zoom level
    ubo.viewPos = glm::vec4(m_debug_2d_pan.x, m_debug_2d_pan.y, m_debug_2d_zoom, 0.0f);

    // material.z = textureIndex (0=baseColor, 1=normal, 2=metalRough, 3=emissive, 4=ao)
    ubo.material = glm::vec4(
      m_shininess, m_specularStrength, static_cast<float>(m_debug_texture_index), m_aoStrength);

    // flags.x = channelMode (0=RGB, 1=R, 2=G, 3=B, 4=A)
    ubo.flags = glm::vec4(static_cast<float>(m_debug_channel_mode), 0.0f, 0.0f, 0.0f);
  }
  else
  {
    // 3D mode: normal material parameters
    // Material parameters: x=shininess, y=specStrength, z=metallicAmbient, w=aoStrength
    ubo.material = glm::vec4(m_shininess, m_specularStrength, m_metallicAmbient, m_aoStrength);

    // Rendering flags: x=useNormalMap, y=useEmissive, z=useAO, w=exposure
    ubo.flags = glm::vec4(m_use_normal_mapping ? 1.0f : 0.0f, m_use_emissive ? 1.0f : 0.0f,
      m_use_ao ? 1.0f : 0.0f, m_exposure);

    // IBL parameters: x=useIBL, y=intensity, z=tonemapMode, w=useSSS
    ubo.ibl_params = glm::vec4(m_use_ibl ? 1.0f : 0.0f, m_scene_manager->ibl_intensity(),
      static_cast<float>(m_tonemap_mode), m_use_sss ? m_sss_scale : 0.0f);
  }

  m_uniform_buffer->update(ubo);
}

void Application::process_input()
{
  const float cameraSpeed = 0.05f;
  const float rotateSpeed = 2.0f;

  // WASD for panning
  if (m_keys[GLFW_KEY_W])
    m_camera.pan(0.0f, cameraSpeed);
  if (m_keys[GLFW_KEY_S])
    m_camera.pan(0.0f, -cameraSpeed);
  if (m_keys[GLFW_KEY_A])
    m_camera.pan(-cameraSpeed, 0.0f);
  if (m_keys[GLFW_KEY_D])
    m_camera.pan(cameraSpeed, 0.0f);

  // QE for dolly
  if (m_keys[GLFW_KEY_Q])
    m_camera.dolly(1.02f);
  if (m_keys[GLFW_KEY_E])
    m_camera.dolly(0.98f);

  // Arrow keys for orbit
  if (m_keys[GLFW_KEY_LEFT])
    m_camera.azimuth(rotateSpeed);
  if (m_keys[GLFW_KEY_RIGHT])
    m_camera.azimuth(-rotateSpeed);
  if (m_keys[GLFW_KEY_UP])
    m_camera.elevation(rotateSpeed);
  if (m_keys[GLFW_KEY_DOWN])
    m_camera.elevation(-rotateSpeed);

  // C to reset camera
  if (m_keys[GLFW_KEY_C])
  {
    setup_camera();
    m_keys[GLFW_KEY_C] = false; // Prevent continuous reset
  }
}

void Application::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
  auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
  if (key >= 0 && key < 512)
  {
    if (action == GLFW_PRESS)
      app->m_keys[key] = true;
    else if (action == GLFW_RELEASE)
      app->m_keys[key] = false;
  }

  // ESC to close window
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    glfwSetWindowShouldClose(window, GLFW_TRUE);

  // R to toggle ray tracing / rasterization
  if (key == GLFW_KEY_R && action == GLFW_PRESS)
  {
    app->m_use_raytracing = !app->m_use_raytracing;
    spdlog::info("Rendering mode: {}", app->m_use_raytracing ? "Ray Tracing" : "Rasterization");
  }

  // F11 to toggle fullscreen
  if (key == GLFW_KEY_F11 && action == GLFW_PRESS)
  {
    GLFWmonitor* monitor = glfwGetWindowMonitor(window);
    if (monitor)
    {
      glfwSetWindowMonitor(window, nullptr, 100, 100,
        static_cast<int>(app->m_window_width), static_cast<int>(app->m_window_height), 0);
    }
    else
    {
      GLFWmonitor* primary = glfwGetPrimaryMonitor();
      const GLFWvidmode* mode = glfwGetVideoMode(primary);
      glfwSetWindowMonitor(window, primary, 0, 0, mode->width, mode->height, mode->refreshRate);
    }
  }

  // F12 to save screenshot
  if (key == GLFW_KEY_F12 && action == GLFW_PRESS)
  {
    if (mods & GLFW_MOD_SHIFT)
      app->begin_screenshot_all(); // Shift+F12: screenshot all models
    else
      app->save_screenshot(); // F12: screenshot current model
  }
}

void Application::mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
  auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));

  if (app->m_firstMouse)
  {
    app->m_lastMouseX = xpos;
    app->m_lastMouseY = ypos;
    app->m_firstMouse = false;
    return;
  }

  double xoffset = xpos - app->m_lastMouseX;
  double yoffset = ypos - app->m_lastMouseY;
  app->m_lastMouseX = xpos;
  app->m_lastMouseY = ypos;

  // 2D mode: pan with left mouse button drag
  if (app->m_debug_2d_mode)
  {
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
    {
      // Pan speed scales with zoom (pan faster when zoomed out)
      const float panSpeed = 0.001f / app->m_debug_2d_zoom;
      app->m_debug_2d_pan.x -= static_cast<float>(xoffset) * panSpeed;
      app->m_debug_2d_pan.y += static_cast<float>(yoffset) * panSpeed;
    }
    return;
  }

  // 3D mode: rotate when right mouse button is pressed
  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
  {
    const float sensitivity = 0.3f;
    app->m_camera.azimuth(static_cast<float>(-xoffset) * sensitivity);
    app->m_camera.elevation(static_cast<float>(-yoffset) * sensitivity);
  }

  // Pan when middle mouse button is pressed
  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
  {
    const float panSensitivity = 0.005f;
    app->m_camera.pan(
      static_cast<float>(-xoffset) * panSensitivity, static_cast<float>(yoffset) * panSensitivity);
  }
}

void Application::scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
  auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));

  // 2D mode: scroll to zoom
  if (app->m_debug_2d_mode)
  {
    const float zoomFactor = 1.15f;
    if (yoffset > 0)
      app->m_debug_2d_zoom *= zoomFactor;
    else if (yoffset < 0)
      app->m_debug_2d_zoom /= zoomFactor;

    // Clamp zoom to reasonable range
    app->m_debug_2d_zoom = glm::clamp(app->m_debug_2d_zoom, 0.1f, 50.0f);
    return;
  }

  // 3D mode: scroll to dolly in/out
  if (yoffset > 0)
    app->m_camera.dolly(1.1f);
  else if (yoffset < 0)
    app->m_camera.dolly(0.9f);
}

void Application::run()
{
  spdlog::trace("Running Application");

  while (!m_window->should_close())
  {
    m_window->poll();
    process_input();
    update_uniform_buffer();
    render();
    calculateFrameRate();
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

  // Build frame context
  FrameContext ctx{};
  ctx.command_buffer = commandBuffer;
  ctx.image_index = imageIndex;
  ctx.extent = m_swapchain->extent();
  ctx.scene_render_pass = m_scene_renderpass;
  ctx.scene_framebuffer = m_scene_framebuffers[imageIndex];
  ctx.pipeline_layout = m_pipelineLayout;
  ctx.composite_render_pass = m_composite_renderpass;
  ctx.composite_framebuffer = m_composite_framebuffers[imageIndex];
  ctx.mesh = m_scene_manager->mesh();
  ctx.scene = m_scene_manager->scene();
  ctx.camera = &m_camera;
  ctx.default_descriptor = m_scene_manager->default_descriptor();
  ctx.material_descriptors = &m_scene_manager->material_descriptors();
  ctx.swapchain = m_swapchain.get();
  ctx.clear_color = m_clear_color;

  m_render_graph.record(ctx);

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

void Application::calculateFrameRate()
{
  m_currentTime = glfwGetTime();
  double delta = m_currentTime - m_lastTime;

  if (delta >= 1)
  {
    int framerate{ std::max(1, int(m_numFrames / delta)) };
    std::stringstream title;
    title << "Running at " << framerate << " fps.";
    glfwSetWindowTitle(m_window->get(), title.str().c_str());
    m_lastTime = m_currentTime;
    m_numFrames = -1;
    m_frameTime = float(1000.0 / framerate);
  }
  m_numFrames++;
}

void Application::recreate_swapchain()
{
  // 1. Wait for valid size (not 0×0)
  std::uint32_t width, height;
  m_window->get_framebuffer_size(width, height);
  while (width == 0 || height == 0)
  {
    m_window->wait_for_focus();
    m_window->get_framebuffer_size(width, height);
  }

  // 2. Wait for GPU to finish using old resources
  m_device->wait_idle();

  // 3. Destroy framebuffers (reverse order of creation)
  for (auto fb : m_scene_framebuffers)
    m_device->device().destroyFramebuffer(fb);
  m_scene_framebuffers.clear();

  for (auto fb : m_composite_framebuffers)
    m_device->device().destroyFramebuffer(fb);
  m_composite_framebuffers.clear();

  // 4. Destroy old depth resources
  m_device->device().destroyImageView(m_depthImageView);
  m_device->device().destroyImage(m_depthImage);
  m_device->device().freeMemory(m_depthImageMemory);

  // 4b. Destroy old blur resources (depends on HDR image views)
  destroy_blur_resources();

  // 4c. Destroy old HDR resources (includes MSAA)
  destroy_hdr_resources();

  // 5. Recreate semaphores (old ones may still be referenced by old swapchain presentation)
  m_renderFinished.clear();

  // 6. Recreate swapchain (handles its own image views internally)
  m_swapchain->recreate(width, height);

  // 7. Recreate per-swapchain-image semaphores
  m_renderFinished.resize(m_swapchain->image_count());
  for (std::uint32_t i = 0; i < m_swapchain->image_count(); i++)
  {
    m_renderFinished[i] = std::make_unique<Semaphore>(*m_device, "render-finished-" + std::to_string(i));
  }

  // 8. Recreate depth resources for new size
  create_depth_resources();

  // 8a. Recreate HDR resources
  create_hdr_resources();
  if (m_msaaSamples != vk::SampleCountFlagBits::e1)
  {
    create_msaa_color_resources();
  }

  // 8b. Recreate RT storage image if RT is enabled
  if (m_rt_image)
  {
    m_device->device().destroyImageView(m_rt_image_view);
    m_device->device().destroyImage(m_rt_image);
    m_device->device().freeMemory(m_rt_image_memory);
    m_rt_image_view = VK_NULL_HANDLE;
    m_rt_image = VK_NULL_HANDLE;
    m_rt_image_memory = VK_NULL_HANDLE;
    create_rt_storage_image();

    // Update RT descriptor with new image view
    vk::DescriptorImageInfo imageInfo{};
    imageInfo.imageView = m_rt_image_view;
    imageInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::WriteDescriptorSet write{};
    write.dstSet = m_rt_descriptor_set;
    write.dstBinding = 1;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eStorageImage;
    write.pImageInfo = &imageInfo;

    m_device->device().updateDescriptorSets(write, {});
  }

  // 9. Create new scene framebuffers (HDR target)
  {
    vk::Extent2D extent = m_swapchain->extent();
    m_scene_framebuffers.resize(m_swapchain->image_count());
    for (uint32_t i = 0; i < m_swapchain->image_count(); i++)
    {
      std::vector<vk::ImageView> attachments;
      if (m_msaaSamples != vk::SampleCountFlagBits::e1)
      {
        // MSAA: [hdrMsaa, depth, hdrResolve]
        attachments = { m_hdrMsaaImageView, m_depthImageView, m_hdrImageView };
      }
      else
      {
        // No MSAA: [hdr, depth]
        attachments = { m_hdrImageView, m_depthImageView };
      }
      vk::FramebufferCreateInfo fbInfo{};
      fbInfo.renderPass = m_scene_renderpass;
      fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
      fbInfo.pAttachments = attachments.data();
      fbInfo.width = extent.width;
      fbInfo.height = extent.height;
      fbInfo.layers = 1;
      m_scene_framebuffers[i] = m_device->device().createFramebuffer(fbInfo);
    }
  }

  // 9b. Create new composite framebuffers
  create_composite_framebuffers();

  // 9c. Update composite descriptor set with new HDR image view
  {
    vk::DescriptorImageInfo imageInfo{};
    imageInfo.sampler = m_hdrSampler;
    imageInfo.imageView = m_hdrImageView;
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::WriteDescriptorSet write{};
    write.dstSet = m_composite_descriptor_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo = &imageInfo;
    m_device->device().updateDescriptorSets(write, {});
  }

  // 9d. Recreate blur resources (needs HDR image + command pool)
  create_blur_resources();
  if (m_sss_blur_stage)
  {
    m_sss_blur_stage->set_descriptors(m_sss_blur_h_descriptor, m_sss_blur_v_descriptor);
  }

  // Update camera aspect ratio
  m_camera.set_aspect_ratio(static_cast<float>(width) / static_cast<float>(height));

  // Clear resize flag if set
  if (m_window->has_pending_resize())
  {
    std::uint32_t w, h;
    m_window->get_pending_resize(w, h);
  }

  // Notify render stages of resize
  m_render_graph.on_swapchain_resize(*m_device, m_swapchain->extent());

  spdlog::trace(
    "Swapchain recreated: {}x{}", m_swapchain->extent().width, m_swapchain->extent().height);
}

void Application::render()
{
  // Wait for previous frame to complete
  m_inFlight->block();

  // Acquire next image
  uint32_t imageIndex;
  try
  {
    imageIndex = m_device->device()
                   .acquireNextImageKHR(
                     *m_swapchain->swapchain(), UINT64_MAX, *m_imageAvailable->semaphore(), nullptr)
                   .value;
  }
  catch (const vk::OutOfDateKHRError&)
  {
    // Swapchain out of date - recreate and skip this frame
    // Don't reset fence - it's still signaled, next frame can proceed
    recreate_swapchain();
    return;
  }

  // Reset fence only after successful acquire, before submit
  m_inFlight->reset();

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

  vk::Semaphore signalSemaphores[] = { *m_renderFinished[imageIndex]->semaphore() };
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  m_device->graphics_queue().submit(submitInfo, m_inFlight->get());

  // Present
  vk::PresentInfoKHR presentInfo = {};
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;
  vk::SwapchainKHR swapChains[] = { *m_swapchain->swapchain() };
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;
  presentInfo.pImageIndices = &imageIndex;

  vk::Result presentResult;
  try
  {
    presentResult = m_device->present_queue().presentKHR(presentInfo);
  }
  catch (const vk::OutOfDateKHRError&)
  {
    presentResult = vk::Result::eErrorOutOfDateKHR;
  }

  // Check if we need to recreate (out of date, suboptimal, or resize requested)
  if (presentResult == vk::Result::eErrorOutOfDateKHR ||
    presentResult == vk::Result::eSuboptimalKHR || m_window->has_pending_resize())
  {
    recreate_swapchain();
  }
}

void Application::make_pipeline()
{
  make_pipeline(SHADER_DIR "vertex.spv", SHADER_DIR "fragment.spv");
}

void Application::make_pipeline(
  const std::string& vertex_shader, const std::string& fragment_shader)
{
  m_vertex_shader_path = vertex_shader;
  m_fragment_shader_path = fragment_shader;

  // Create scene render pass (HDR target)
  m_scene_renderpass = sps::vulkan::make_scene_renderpass(
    m_device->device(), m_hdrFormat, m_depthFormat, true, m_msaaSamples);

  sps::vulkan::GraphicsPipelineInBundle specification = {};
  specification.device = m_device->device();
  specification.vertexFilepath = vertex_shader;
  specification.fragmentFilepath = fragment_shader;
  specification.swapchainExtent = m_swapchain->extent();
  specification.swapchainImageFormat = m_hdrFormat; // HDR target format
  specification.descriptorSetLayout = m_scene_manager->default_descriptor()->layout();

  // Set vertex input format
  auto binding = Vertex::binding_description();
  auto attributes = Vertex::attribute_descriptions();
  specification.vertexBindings = { binding };
  specification.vertexAttributes = { attributes.begin(), attributes.end() };

  // Rasterizer options
  specification.backfaceCulling = m_backfaceCulling;
  specification.dynamicCullMode = true;

  // Depth testing
  specification.depthTestEnabled = m_depthTestEnabled;
  specification.depthFormat = m_depthFormat;

  // MSAA
  specification.msaaSamples = m_msaaSamples;

  // Use the scene render pass
  specification.existingRenderPass = m_scene_renderpass;

  // Push constant: 128 bytes
  vk::PushConstantRange pcRange{
    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, 128
  };
  specification.pushConstantRanges = { pcRange };

  // Pipeline 1: opaque (no blend, depth write on)
  specification.blendEnabled = false;
  specification.depthWriteEnabled = true;

  sps::vulkan::GraphicsPipelineOutBundle output =
    sps::vulkan::create_graphics_pipeline(specification, true);

  m_pipelineLayout = output.layout;
  m_pipeline = output.pipeline;

  // Pipeline 2: blend (alpha blend on, depth write off, reuse layout + renderpass)
  specification.blendEnabled = true;
  specification.depthWriteEnabled = false;
  specification.existingPipelineLayout = m_pipelineLayout;

  sps::vulkan::GraphicsPipelineOutBundle blendOutput =
    sps::vulkan::create_graphics_pipeline(specification, true);

  m_blend_pipeline = blendOutput.pipeline;
}

void Application::create_debug_2d_pipeline()
{
  sps::vulkan::GraphicsPipelineInBundle specification = {};
  specification.device = m_device->device();
  specification.vertexFilepath = SHADER_DIR "fullscreen_quad.spv";
  specification.fragmentFilepath = SHADER_DIR "debug_texture2d.spv";
  specification.swapchainExtent = m_swapchain->extent();
  specification.swapchainImageFormat = m_swapchain->image_format();
  specification.descriptorSetLayout = m_scene_manager->default_descriptor()->layout();

  // No vertex input - fullscreen quad generates vertices in shader
  // vertexBindings and vertexAttributes left empty

  // Rasterizer options
  specification.backfaceCulling = false;

  // Debug 2D renders in the composite pass (swapchain target, no depth, no MSAA)
  specification.existingRenderPass = m_composite_renderpass;
  specification.depthTestEnabled = false;
  specification.msaaSamples = vk::SampleCountFlagBits::e1;

  sps::vulkan::GraphicsPipelineOutBundle output =
    sps::vulkan::create_graphics_pipeline(specification, true);

  m_debug_2d_pipelineLayout = output.layout;
  m_debug_2d_pipeline = output.pipeline;

  spdlog::info("Created 2D debug pipeline");
}

void Application::create_light_indicator()
{
  // Generate procedural UV sphere centered at origin
  // Model matrix will position it at the light location
  const int stacks = 8;
  const int slices = 16;
  const float radius = 1.0f; // Unit sphere, scaled by model matrix

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  // Get light color for the sphere
  glm::vec3 lightColor = m_light ? glm::vec3(m_light->color()) : glm::vec3(1.0f, 1.0f, 0.0f);

  // Generate vertices
  for (int i = 0; i <= stacks; ++i)
  {
    float phi = static_cast<float>(i) / stacks * 3.14159265359f;
    float y = std::cos(phi) * radius;
    float r = std::sin(phi) * radius;

    for (int j = 0; j <= slices; ++j)
    {
      float theta = static_cast<float>(j) / slices * 2.0f * 3.14159265359f;
      float x = r * std::cos(theta);
      float z = r * std::sin(theta);

      Vertex v;
      v.position = glm::vec3(x, y, z);
      v.normal = glm::normalize(v.position);
      v.color = lightColor; // Use light color
      v.texCoord = glm::vec2(static_cast<float>(j) / slices, static_cast<float>(i) / stacks);
      vertices.push_back(v);
    }
  }

  // Generate indices
  for (int i = 0; i < stacks; ++i)
  {
    for (int j = 0; j < slices; ++j)
    {
      int first = i * (slices + 1) + j;
      int second = first + slices + 1;

      indices.push_back(first);
      indices.push_back(second);
      indices.push_back(first + 1);

      indices.push_back(second);
      indices.push_back(second + 1);
      indices.push_back(first + 1);
    }
  }

  m_light_indicator_mesh = std::make_unique<Mesh>(*m_device, "light_indicator", vertices, indices);

  // No separate pipeline needed - reuse main pipeline with different model matrix
  spdlog::info("Created light indicator sphere ({} vertices)", vertices.size());
}

void Application::reload_shaders(
  const std::string& vertex_shader, const std::string& fragment_shader)
{
  m_device->wait_idle();

  // Destroy old pipelines
  m_device->device().destroyPipeline(m_pipeline);
  m_device->device().destroyPipeline(m_blend_pipeline);
  m_device->device().destroyPipelineLayout(m_pipelineLayout);
  m_device->device().destroyRenderPass(m_scene_renderpass);

  make_pipeline(vertex_shader, fragment_shader);

  // Update stage pipeline handles
  if (m_raster_opaque_stage)
    m_raster_opaque_stage->set_pipeline(m_pipeline);
  if (m_raster_blend_stage)
    m_raster_blend_stage->set_pipeline(m_blend_pipeline);

  spdlog::info("Reloaded shaders: {} + {}", vertex_shader, fragment_shader);
}

bool Application::save_screenshot(const std::string& filepath)
{
  m_device->wait_idle();

  // Get current swapchain image
  auto images = m_swapchain->images();
  if (images.empty())
  {
    spdlog::error("No swapchain images available for screenshot");
    return false;
  }

  // Use the first swapchain image (most recently presented)
  vk::Image source_image = images[0];
  vk::Format format = m_swapchain->image_format();
  vk::Extent2D extent = m_swapchain->extent();

  return sps::vulkan::save_screenshot(
    *m_device, m_commandPool, source_image, format, extent, filepath);
}

bool Application::save_screenshot()
{
  // Use model name as prefix if a model is loaded
  std::string prefix = "screenshot";
  if (m_current_model_index >= 0 && m_current_model_index < static_cast<int>(m_gltf_models.size()))
  {
    prefix = std::filesystem::path(m_gltf_models[m_current_model_index]).stem().string();
  }

  std::filesystem::create_directories("screenshots");
  std::string filename = "screenshots/" + sps::vulkan::generate_screenshot_filename(prefix, ".png");
  return save_screenshot(filename);
}

void Application::begin_screenshot_all()
{
  if (m_gltf_models.empty())
  {
    spdlog::warn("No models configured for screenshots");
    return;
  }

  std::filesystem::create_directories("screenshots");
  m_screenshot_all_restore = m_current_model_index;
  m_screenshot_all_index = 0;
  m_screenshot_all_frames_wait = 2; // load first model, wait 2 frames before capture
  load_model(0);
  spdlog::info("Screenshot all: starting ({} models)", m_gltf_models.size());
}

void Application::tick_screenshot_all()
{
  if (m_screenshot_all_index < 0)
    return; // not active

  if (m_screenshot_all_frames_wait > 0)
  {
    --m_screenshot_all_frames_wait;
    return; // wait for rendered frame to settle
  }

  // Capture current model
  save_screenshot();
  spdlog::info("Screenshot all: saved {}/{}",
    m_screenshot_all_index + 1, static_cast<int>(m_gltf_models.size()));

  // Advance to next model
  ++m_screenshot_all_index;
  if (m_screenshot_all_index < static_cast<int>(m_gltf_models.size()))
  {
    load_model(m_screenshot_all_index);
    m_screenshot_all_frames_wait = 2; // wait for new model to render
  }
  else
  {
    // Done — restore original model
    spdlog::info("Screenshot all: complete");
    m_screenshot_all_index = -1;
    if (m_screenshot_all_restore >= 0)
      load_model(m_screenshot_all_restore);
  }
}

void Application::poll_commands()
{
  // Initialize command registry on first call
  if (!m_command_registry)
  {
    m_command_registry = std::make_unique<CommandRegistry>();

    // Register "set" command for variables
    m_command_registry->add("set", "Set a variable", "<name> <value>",
      [this](const std::vector<std::string>& args)
      {
        if (args.size() < 2)
          return;
        const std::string& name = args[0];
        float value = std::stof(args[1]);

        if (name == "metallic_ambient")
          m_metallicAmbient = value;
        else if (name == "ao_strength")
          m_aoStrength = value;
        else if (name == "shininess")
          m_shininess = value;
        else if (name == "specular")
          m_specularStrength = value;
        else if (name == "normal_mapping")
          m_use_normal_mapping = value > 0.5f;
        else if (name == "emissive")
          m_use_emissive = value > 0.5f;
        else if (name == "ao")
          m_use_ao = value > 0.5f;
        else if (name == "texture")
          m_debug_texture_index = static_cast<int>(value);
        else if (name == "channel")
          m_debug_channel_mode = static_cast<int>(value);
        else if (name == "2d")
          m_debug_2d_mode = value > 0.5f;
        else
          spdlog::warn("Unknown variable: {}", name);
      });

    // Register "shader" command
    m_command_registry->add("shader", "Switch shader mode", "<index|name>",
      [this](const std::vector<std::string>& args)
      {
        if (args.empty())
          return;
        int mode = std::stoi(args[0]);
        if (mode >= 0 && mode < sps::vulkan::debug::SHADER_COUNT)
        {
          reload_shaders(
            sps::vulkan::debug::vertex_shaders[mode], sps::vulkan::debug::fragment_shaders[mode]);
          m_current_shader_mode = mode;
        }
      });

    // Register "screenshot" command
    m_command_registry->add("screenshot", "Save screenshot", "[filename]",
      [this](const std::vector<std::string>& args)
      {
        if (args.empty())
        {
          save_screenshot();
        }
        else
        {
          save_screenshot(args[0]);
        }
      });

    // Register "screenshot_all" command
    m_command_registry->add("screenshot_all", "Screenshot all models", "",
      [this](const std::vector<std::string>&)
      {
        begin_screenshot_all();
      });

    // Register "fullscreen" command
    m_command_registry->add("fullscreen", "Toggle fullscreen mode", "",
      [this](const std::vector<std::string>&)
      {
        GLFWwindow* win = glfw_window();
        GLFWmonitor* monitor = glfwGetWindowMonitor(win);
        if (monitor)
        {
          // Currently fullscreen -> go windowed
          glfwSetWindowMonitor(win, nullptr, 100, 100,
            static_cast<int>(m_window_width), static_cast<int>(m_window_height), 0);
          spdlog::info("Switched to windowed mode");
        }
        else
        {
          // Currently windowed -> go fullscreen
          GLFWmonitor* primary = glfwGetPrimaryMonitor();
          const GLFWvidmode* mode = glfwGetVideoMode(primary);
          glfwSetWindowMonitor(win, primary, 0, 0, mode->width, mode->height, mode->refreshRate);
          spdlog::info("Switched to fullscreen {}x{}", mode->width, mode->height);
        }
      });

    // Register "mode" command for 2D/3D toggle
    m_command_registry->add("mode", "Switch 2D/3D mode", "<2d|3d>",
      [this](const std::vector<std::string>& args)
      {
        if (args.empty())
          return;
        m_debug_2d_mode = (args[0] == "2d");
      });

    spdlog::info("Command file: {}", std::filesystem::absolute(m_command_file_path).string());
  }

  // Check if command file was modified
  if (!std::filesystem::exists(m_command_file_path))
  {
    // Create empty file
    std::ofstream file(m_command_file_path);
    file << "# Commands: set <var> <val>, shader <idx>, screenshot [file], mode <2d|3d>\n";
    file.close();
    return;
  }

  auto file_time = std::filesystem::last_write_time(m_command_file_path);
  if (file_time <= m_command_file_mtime)
    return;
  spdlog::info("Command file changed, processing...");
  m_command_file_mtime = file_time;

  // Read and execute commands
  std::ifstream file(m_command_file_path);
  std::string line;
  std::vector<std::string> commands;
  while (std::getline(file, line))
  {
    if (line.empty() || line[0] == '#')
      continue;
    commands.push_back(line);
  }
  file.close();

  for (const auto& cmd : commands)
  {
    spdlog::info("Executing: {}", cmd);
    if (!m_command_registry->execute(cmd))
    {
      spdlog::warn("Unknown command: {}", cmd);
    }
  }

  // Clear the file and update mtime to avoid re-reading the cleared file
  std::ofstream clear_file(m_command_file_path);
  clear_file << "# Commands: set <var> <val>, shader <idx>, screenshot [file], mode <2d|3d>\n";
  clear_file << "# Variables: metallic_ambient, ao_strength, shininess, specular\n";
  clear_file << "# Toggles: normal_mapping, emissive, ao, 2d (0 or 1)\n";
  clear_file << "# texture: 0=base, 1=normal, 2=metalRough, 3=emissive, 4=ao\n";
  clear_file << "# channel: 0=RGB, 1=R, 2=G, 3=B, 4=A\n";
  clear_file.close();
  m_command_file_mtime = std::filesystem::last_write_time(m_command_file_path);
}

void Application::load_model(int index)
{
  if (index < 0 || index >= static_cast<int>(m_gltf_models.size()))
  {
    spdlog::warn("Invalid model index: {}", index);
    return;
  }
  if (index == m_current_model_index)
    return;

  m_device->wait_idle();
  auto result = m_scene_manager->load_model(m_gltf_models[index], m_uniform_buffer->buffer());
  if (!result.success)
    return;

  // Camera reset
  if (result.bounds.valid())
  {
    float bounds[6];
    result.bounds.to_bounds(bounds);
    m_camera.reset_camera(bounds);
  }

  // RT rebuild
  if (m_device->supports_ray_tracing() && m_blas)
  {
    m_blas.reset();
    m_tlas.reset();
    build_acceleration_structures();
  }

  m_current_model_index = index;
}

void Application::load_hdr(int index)
{
  if (index < 0 || index >= static_cast<int>(m_hdr_files.size()))
  {
    spdlog::warn("Invalid HDR index: {}", index);
    return;
  }
  if (index == m_current_hdr_index)
    return;

  m_device->wait_idle();
  m_scene_manager->load_hdr(m_hdr_files[index], m_uniform_buffer->buffer());
  m_current_hdr_index = index;
}

int Application::light_type() const
{
  if (!m_light_enabled)
    return 0;
  if (dynamic_cast<DirectionalLight*>(m_light.get()))
    return 2;
  return 1; // PointLight (default)
}

void Application::set_light_type(int type)
{
  // Preserve common properties
  glm::vec3 color = m_light->color();
  float intensity = m_light->intensity();
  glm::vec3 ambient = m_light->ambient();

  if (type == 0)
  {
    // Off — keep existing light but disable it
    m_light_enabled = false;
    return;
  }

  m_light_enabled = true;

  if (type == 2)
  {
    // Directional
    auto dir_light = std::make_unique<DirectionalLight>();
    // If switching from point, use position as direction hint
    if (auto* point = dynamic_cast<PointLight*>(m_light.get()))
      dir_light->set_direction(glm::normalize(point->position()));
    else if (auto* old_dir = dynamic_cast<DirectionalLight*>(m_light.get()))
      dir_light->set_direction(old_dir->direction());
    m_light = std::move(dir_light);
  }
  else
  {
    // Point
    auto point_light = std::make_unique<PointLight>();
    if (auto* old_dir = dynamic_cast<DirectionalLight*>(m_light.get()))
      point_light->set_position(old_dir->direction() * 3.0f);
    else if (auto* old_point = dynamic_cast<PointLight*>(m_light.get()))
      point_light->set_position(old_point->position());
    m_light = std::move(point_light);
  }

  m_light->set_color(color);
  m_light->set_intensity(intensity);
  m_light->set_ambient(ambient);
}

void Application::apply_shader_mode(int mode)
{
  // Must match main_imgui.cpp shader arrays
  static const char* vertex_shaders[] = {
    SHADER_DIR "vertex.spv",  // PBR
    SHADER_DIR "vertex.spv",  // Blinn-Phong
    SHADER_DIR "vertex.spv",  // Debug UV
    SHADER_DIR "vertex.spv",  // Debug Normals
    SHADER_DIR "vertex.spv",  // Debug Base Color
    SHADER_DIR "vertex.spv",  // Debug Metallic/Roughness
    SHADER_DIR "vertex.spv",  // Debug AO
    SHADER_DIR "vertex.spv",  // Debug Emissive
    SHADER_DIR "vertex.spv",  // Debug Thickness
    SHADER_DIR "vertex.spv"   // Debug SSS
  };
  static const char* fragment_shaders[] = {
    SHADER_DIR "fragment.spv",
    SHADER_DIR "blinn_phong.spv",
    SHADER_DIR "debug_uv.spv",
    SHADER_DIR "debug_normals.spv",
    SHADER_DIR "debug_basecolor.spv",
    SHADER_DIR "debug_metallic_roughness.spv",
    SHADER_DIR "debug_ao.spv",
    SHADER_DIR "debug_emissive.spv",
    SHADER_DIR "debug_thickness.spv",
    SHADER_DIR "debug_sss.spv"
  };

  constexpr int num_shaders = sizeof(fragment_shaders) / sizeof(fragment_shaders[0]);
  if (mode >= 0 && mode < num_shaders)
  {
    m_current_shader_mode = mode;
    reload_shaders(vertex_shaders[mode], fragment_shaders[mode]);
  }
}

Application::~Application()
{
  spdlog::trace("Destroying Application");

  m_device->wait_idle();

  m_inFlight.reset(nullptr);
  m_imageAvailable.reset(nullptr);
  m_renderFinished.clear();

  // Destroy resources before device
  m_scene_manager.reset();
  m_uniform_buffer.reset();

  m_device->device().destroyCommandPool(m_commandPool);

  // Destroy scene pipelines
  m_device->device().destroyPipeline(m_pipeline);
  m_device->device().destroyPipeline(m_blend_pipeline);
  m_device->device().destroyPipelineLayout(m_pipelineLayout);
  m_device->device().destroyRenderPass(m_scene_renderpass);

  // Destroy 2D debug pipeline
  m_device->device().destroyPipeline(m_debug_2d_pipeline);
  m_device->device().destroyPipelineLayout(m_debug_2d_pipelineLayout);

  // Destroy composite pipeline
  m_device->device().destroyPipeline(m_composite_pipeline);
  m_device->device().destroyPipelineLayout(m_composite_pipelineLayout);
  m_device->device().destroyRenderPass(m_composite_renderpass);
  if (m_composite_descriptor_pool)
    m_device->device().destroyDescriptorPool(m_composite_descriptor_pool);
  if (m_composite_descriptor_layout)
    m_device->device().destroyDescriptorSetLayout(m_composite_descriptor_layout);

  // Destroy framebuffers
  for (auto fb : m_scene_framebuffers)
    m_device->device().destroyFramebuffer(fb);
  for (auto fb : m_composite_framebuffers)
    m_device->device().destroyFramebuffer(fb);

  // Destroy depth resources
  m_device->device().destroyImageView(m_depthImageView);
  m_device->device().destroyImage(m_depthImage);
  m_device->device().freeMemory(m_depthImageMemory);

  // Destroy HDR resources (includes MSAA)
  destroy_hdr_resources();
  if (m_hdrSampler)
    m_device->device().destroySampler(m_hdrSampler);

  // Destroy SSS blur resources
  destroy_blur_resources();
  if (m_sss_blur_pipeline)
    m_device->device().destroyPipeline(m_sss_blur_pipeline);
  if (m_sss_blur_pipelineLayout)
    m_device->device().destroyPipelineLayout(m_sss_blur_pipelineLayout);
  if (m_sss_blur_descriptor_pool)
    m_device->device().destroyDescriptorPool(m_sss_blur_descriptor_pool);
  if (m_sss_blur_descriptor_layout)
    m_device->device().destroyDescriptorSetLayout(m_sss_blur_descriptor_layout);

  // Destroy RT resources
  m_rt_pipeline.reset();
  m_tlas.reset();
  m_blas.reset();

  if (m_rt_image_view)
    m_device->device().destroyImageView(m_rt_image_view);
  if (m_rt_image)
    m_device->device().destroyImage(m_rt_image);
  if (m_rt_image_memory)
    m_device->device().freeMemory(m_rt_image_memory);
  if (m_rt_descriptor_pool)
    m_device->device().destroyDescriptorPool(m_rt_descriptor_pool);
  if (m_rt_descriptor_layout)
    m_device->device().destroyDescriptorSetLayout(m_rt_descriptor_layout);

  // Swapchain destroyed in renderer
  // Surface ..
  // Instance
  // glfw terminated in renderer
}

void Application::finalize_setup()
{
  // Create HDR offscreen resources
  create_hdr_resources();
  if (m_msaaSamples != vk::SampleCountFlagBits::e1)
  {
    create_msaa_color_resources();
  }

  // Create scene framebuffers (HDR target)
  {
    vk::Extent2D extent = m_swapchain->extent();
    m_scene_framebuffers.resize(m_swapchain->image_count());
    for (uint32_t i = 0; i < m_swapchain->image_count(); i++)
    {
      std::vector<vk::ImageView> attachments;
      if (m_msaaSamples != vk::SampleCountFlagBits::e1)
      {
        attachments = { m_hdrMsaaImageView, m_depthImageView, m_hdrImageView };
      }
      else
      {
        attachments = { m_hdrImageView, m_depthImageView };
      }
      vk::FramebufferCreateInfo fbInfo{};
      fbInfo.renderPass = m_scene_renderpass;
      fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
      fbInfo.pAttachments = attachments.data();
      fbInfo.width = extent.width;
      fbInfo.height = extent.height;
      fbInfo.layers = 1;
      m_scene_framebuffers[i] = m_device->device().createFramebuffer(fbInfo);
    }
  }

  // Create composite render pass and framebuffers
  m_composite_renderpass = sps::vulkan::make_composite_renderpass(
    m_device->device(), m_swapchain->image_format(), true);
  create_composite_framebuffers();

  // Create composite pipeline (tone mapping + gamma)
  create_composite_pipeline();

  m_commandPool = sps::vulkan::make_command_pool(*m_device, m_debugMode);

  m_mainCommandBuffer = sps::vulkan::make_command_buffers(
    *m_device, *m_swapchain, m_commandPool, m_commandBuffers, true);

  m_inFlight = std::make_unique<Fence>(*m_device, "in-flight", true);
  m_imageAvailable = std::make_unique<Semaphore>(*m_device, "image-available");
  m_renderFinished.resize(m_swapchain->image_count());
  for (std::uint32_t i = 0; i < m_swapchain->image_count(); i++)
  {
    m_renderFinished[i] = std::make_unique<Semaphore>(*m_device, "render-finished-" + std::to_string(i));
  }

  // Create SSS blur resources (needs command pool for one-shot layout transition)
  create_blur_resources();

  // Create 2D debug pipeline (uses composite render pass)
  create_debug_2d_pipeline();

#if 1 // Enable ray tracing
  spdlog::info("RT init check: supports_rt={}", m_device->supports_ray_tracing());
  if (m_device->supports_ray_tracing())
  {
    spdlog::info("Starting RT initialization...");
    build_acceleration_structures();
    create_rt_storage_image();
    create_rt_descriptor();
    create_rt_pipeline();
    spdlog::info("Ray tracing enabled");
  }
#endif

  // Register render stages
  // Order within each phase doesn't matter — the render graph groups by phase.
  m_ray_tracing_stage = m_render_graph.add<RayTracingStage>(
    &m_use_raytracing, m_device.get(), m_rt_pipeline.get(), &m_rt_image, &m_rt_descriptor_set);
  m_raster_opaque_stage = m_render_graph.add<RasterOpaqueStage>(
    &m_use_raytracing, &m_debug_2d_mode, m_pipeline);
  m_raster_blend_stage = m_render_graph.add<RasterBlendStage>(
    &m_use_raytracing, &m_debug_2d_mode, m_blend_pipeline);
  m_sss_blur_stage = m_render_graph.add<SSSBlurStage>(
    &m_use_sss_blur, &m_sss_blur_width,
    m_sss_blur_pipeline, m_sss_blur_pipelineLayout,
    m_sss_blur_h_descriptor, m_sss_blur_v_descriptor,
    &m_hdrImage, &m_blur_extent);
  m_composite_stage = m_render_graph.add<CompositeStage>(
    m_composite_pipeline, m_composite_pipelineLayout,
    m_composite_descriptor_set, &m_exposure, &m_tonemap_mode);
  m_debug_2d_stage = m_render_graph.add<Debug2DStage>(
    &m_debug_2d_mode, &m_debug_material_index, m_debug_2d_pipeline, m_debug_2d_pipelineLayout);
  m_ui_stage = m_render_graph.add<UIStage>(&m_ui_render_callback);
}

VkInstance Application::vk_instance() const
{
  return m_instance->instance();
}

VkPhysicalDevice Application::vk_physical_device() const
{
  return m_device->physicalDevice();
}

VkDevice Application::vk_device() const
{
  return m_device->device();
}

VkQueue Application::vk_graphics_queue() const
{
  return m_device->graphics_queue();
}

uint32_t Application::graphics_queue_family() const
{
  return m_device->m_graphics_queue_family_index;
}

uint32_t Application::swapchain_image_count() const
{
  return static_cast<uint32_t>(m_swapchain->images().size());
}

GLFWwindow* Application::glfw_window() const
{
  return m_window->get();
}

bool Application::should_close() const
{
  return m_window->should_close();
}

void Application::poll_events()
{
  m_window->poll();
}

void Application::wait_idle()
{
  m_device->wait_idle();
}

void Application::update_frame()
{
  process_input();
  update_uniform_buffer();
}

void Application::set_vsync(bool enabled)
{
  if (m_vsync_enabled != enabled)
  {
    m_vsync_enabled = enabled;
    m_swapchain->set_vsync(enabled);
    recreate_swapchain();
  }
}
}
