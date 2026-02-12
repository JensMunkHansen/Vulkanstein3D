#include <sps/vulkan/config.h>

#include <memory>
#include <spdlog/common.h>

#include <sps/tools/cla_parser.hpp>
#include <sps/vulkan/app.h>
#include <sps/vulkan/debug_constants.h>
#include <sps/vulkan/meta.hpp>
#include <sps/vulkan/screenshot.h>

#include <fstream>
#include <sps/vulkan/vertex.h>

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

RendererConfig Application::build_renderer_config(int argc, char** argv, AppConfig& app_config)
{
  sps::tools::CommandLineArgumentParser cla_parser;
  cla_parser.parse_args(argc, argv);

  spdlog::trace("Application version: {}.{}.{}", APP_VERSION[0], APP_VERSION[1], APP_VERSION[2]);
  spdlog::trace(
    "Engine version: {}.{}.{}", ENGINE_VERSION[0], ENGINE_VERSION[1], ENGINE_VERSION[2]);

  // Load the configuration from the TOML file.
  app_config = parse_toml("./vulk3D.toml");

  RendererConfig config;
  config.window_title = app_config.window_title;
  config.window_width = app_config.window_width;
  config.window_height = app_config.window_height;
  config.window_mode = app_config.window_mode;
  config.preferred_gpu = app_config.preferred_gpu;
  config.msaa_samples = app_config.msaa_samples;

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
      config.enable_renderdoc = true;
    }
#endif
  }

  const auto disable_validation = cla_parser.arg<bool>("--no-validation");
  if (disable_validation.value_or(false))
  {
    spdlog::warn("--no-validation specified, disabling validation layers");
    config.enable_validation = false;
  }

  const auto disable_vsync = cla_parser.arg<bool>("--no-vsync");
  if (disable_vsync.value_or(false))
  {
    spdlog::trace("V-sync disabled!");
    config.vsync = false;
  }

  const auto no_separate_queue = cla_parser.arg<bool>("--no-separate-data-queue");
  if (no_separate_queue.value_or(false))
  {
    spdlog::warn("Command line argument --no-separate-data-queue specified");
    config.use_distinct_data_transfer_queue = false;
  }

  auto preferred_graphics_card = cla_parser.arg<std::uint32_t>("--gpu");
  if (preferred_graphics_card)
  {
    spdlog::trace("Preferential graphics card index {} specified", *preferred_graphics_card);
    config.preferred_gpu_index = *preferred_graphics_card;
  }

  return config;
}

Application::Application(int argc, char** argv)
{
  m_lastTime = glfwGetTime();

  spdlog::trace("Initialising vulkan-renderer");

  // Build renderer config from TOML + CLI, then construct renderer
  AppConfig app_config;
  RendererConfig renderer_config = build_renderer_config(argc, argv, app_config);
  m_renderer = std::make_unique<VulkanRenderer>(renderer_config);

  // Apply app-specific config (geometry, lighting, etc.)
  apply_config(std::move(app_config));

#ifndef SPS_DEBUG
  {
    sps::tools::CommandLineArgumentParser cla_parser;
    cla_parser.parse_args(argc, argv);
    if (cla_parser.arg<bool>("--stop-on-validation-message").value_or(false))
    {
      spdlog::warn("--stop-on-validation-message specified. Application will call a breakpoint after "
                   "reporting a "
                   "validation layer message");
      m_stop_on_validation_message = true;
    }
  }
#endif

  // Setup camera
  setup_camera();

  // Create scene manager and load initial scene
  m_scene_manager = std::make_unique<SceneManager>(m_renderer->device());
  m_scene_manager->set_ibl_settings(m_ibl_settings);
  m_scene_manager->create_defaults(m_hdr_file);
  auto load_result = m_scene_manager->load_initial_scene(m_geometry_source, m_gltf_file, m_ply_file);

  // Create uniform buffer and descriptor
  create_uniform_buffer();

  m_scene_manager->create_descriptors(m_uniform_buffer->buffer());

  // Create scene render pass (pipelines created by RasterOpaqueStage in finalize_setup)
  create_scene_renderpass();

  // Reset camera to frame loaded scene
  if (load_result.success && load_result.bounds.valid())
  {
    float bounds[6];
    load_result.bounds.to_bounds(bounds);
    m_camera.reset_camera(bounds);
  }

  finalize_setup();

  // Setup resize and input callbacks
  glfwSetWindowUserPointer(m_renderer->window().get(), this);
  glfwSetFramebufferSizeCallback(m_renderer->window().get(),
    [](GLFWwindow* window, int width, int height)
    {
      auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
      app->m_renderer->window().set_resize_pending(
        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    });
  glfwSetKeyCallback(m_renderer->window().get(), key_callback);
  glfwSetCursorPosCallback(m_renderer->window().get(), mouse_callback);
  glfwSetScrollCallback(m_renderer->window().get(), scroll_callback);
}

void Application::apply_config(AppConfig config)
{
  m_backfaceCulling = config.backface_culling;
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
  m_renderer->window().get_framebuffer_size(width, height);
  m_camera.set_aspect_ratio(static_cast<float>(width) / static_cast<float>(height));
}

void Application::create_uniform_buffer()
{
  m_uniform_buffer =
    std::make_unique<UniformBuffer<UniformBufferObject>>(m_renderer->device(), "camera uniform buffer");
  spdlog::trace("Created uniform buffer");
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

  ubo.clear_color = glm::vec4(m_clear_color, 0.0f);

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
        static_cast<int>(app->m_renderer->window_width()), static_cast<int>(app->m_renderer->window_height()), 0);
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

  while (!m_renderer->window().should_close())
  {
    m_renderer->window().poll();
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
  ctx.extent = m_renderer->swapchain().extent();
  ctx.composite_framebuffer = m_composite_stage->framebuffer(imageIndex);
  ctx.mesh = m_scene_manager->mesh();
  ctx.scene = m_scene_manager->scene();
  ctx.camera = &m_camera;
  ctx.default_descriptor = m_scene_manager->default_descriptor();
  ctx.material_descriptors = &m_scene_manager->material_descriptors();
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
    glfwSetWindowTitle(m_renderer->window().get(), title.str().c_str());
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
  m_renderer->window().get_framebuffer_size(width, height);
  while (width == 0 || height == 0)
  {
    m_renderer->window().wait_for_focus();
    m_renderer->window().get_framebuffer_size(width, height);
  }

  // 2. Wait for GPU to finish using old resources
  m_renderer->device().wait_idle();

  // 3. Scene framebuffers destroyed by RenderGraph::recreate_scene_framebuffers()
  // Composite framebuffers destroyed by CompositeStage::on_swapchain_resize()
  // SSS blur ping image + descriptors destroyed by SSSBlurStage::on_swapchain_resize()

  // 4. Recreate swapchain (handles its own image views internally)
  m_renderer->swapchain().recreate(width, height);

  // 6. Recreate per-swapchain-image semaphores
  m_renderer->recreate_sync_objects();

  // 7. Recreate renderer-owned resources (depth-stencil, HDR, MSAA)
  m_renderer->recreate_depth_resources();
  m_renderer->recreate_hdr_resources();

  // RT storage image handled by RayTracingStage::on_swapchain_resize()

  // Update shared image registry before recreating framebuffers and notifying stages
  m_render_graph.image_registry().set("hdr",
    { m_renderer->hdr_image(), m_renderer->hdr_image_view(), {}, m_renderer->hdr_format() });
  m_render_graph.image_registry().set("depth_stencil",
    { m_renderer->depth_stencil().image(), m_renderer->depth_stencil().stencil_view(),
      {}, m_renderer->depth_format() });

  // Recreate scene framebuffers (uses registry images + scene render pass)
  m_render_graph.recreate_scene_framebuffers();

  // Composite framebuffers + descriptor handled by CompositeStage::on_swapchain_resize()
  // SSS blur ping + descriptors handled by SSSBlurStage::on_swapchain_resize()

  // Update camera aspect ratio
  m_camera.set_aspect_ratio(static_cast<float>(width) / static_cast<float>(height));

  // Clear resize flag if set
  if (m_renderer->window().has_pending_resize())
  {
    std::uint32_t w, h;
    m_renderer->window().get_pending_resize(w, h);
  }

  // Notify render stages of resize
  m_render_graph.on_swapchain_resize(m_renderer->device(), m_renderer->swapchain().extent());

  spdlog::trace(
    "Swapchain recreated: {}x{}", m_renderer->swapchain().extent().width, m_renderer->swapchain().extent().height);
}

void Application::render()
{
  // Wait for previous frame to complete
  m_renderer->in_flight().block();

  // Acquire next image
  uint32_t imageIndex;
  try
  {
    imageIndex = m_renderer->device().device()
                   .acquireNextImageKHR(
                     *m_renderer->swapchain().swapchain(), UINT64_MAX, *m_renderer->image_available().semaphore(), nullptr)
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
  m_renderer->in_flight().reset();

  vk::CommandBuffer commandBuffer = m_renderer->command_buffers()[imageIndex];
  commandBuffer.reset();
  record_draw_commands(commandBuffer, imageIndex);

  vk::SubmitInfo submitInfo = {};
  vk::Semaphore waitSemaphores[] = { *m_renderer->image_available().semaphore() };
  vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vk::Semaphore signalSemaphores[] = { *m_renderer->render_finished(imageIndex).semaphore() };
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  m_renderer->device().graphics_queue().submit(submitInfo, m_renderer->in_flight().get());

  // Present
  vk::PresentInfoKHR presentInfo = {};
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;
  vk::SwapchainKHR swapChains[] = { *m_renderer->swapchain().swapchain() };
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;
  presentInfo.pImageIndices = &imageIndex;

  vk::Result presentResult;
  try
  {
    presentResult = m_renderer->device().present_queue().presentKHR(presentInfo);
  }
  catch (const vk::OutOfDateKHRError&)
  {
    presentResult = vk::Result::eErrorOutOfDateKHR;
  }

  // Check if we need to recreate (out of date, suboptimal, or resize requested)
  if (presentResult == vk::Result::eErrorOutOfDateKHR ||
    presentResult == vk::Result::eSuboptimalKHR || m_renderer->window().has_pending_resize())
  {
    recreate_swapchain();
  }
}

void Application::create_scene_renderpass()
{
  m_scene_renderpass = sps::vulkan::make_scene_renderpass(
    m_renderer->device().device(), m_renderer->hdr_format(), m_renderer->depth_format(),
    true, m_renderer->msaa_samples());
}

/// Create the opaque and blend raster stages as a pair.
///
/// These two stages share a single pipeline layout and are always created together.
/// RasterOpaqueStage owns the pipeline layout, the opaque pipeline, and the blend
/// pipeline. RasterBlendStage holds a const reference to the opaque stage and
/// queries blend_pipeline() / pipeline_layout() each frame — so shader hot-reload
/// is transparent (no stale handles).
void Application::create_raster_stages()
{
  m_raster_opaque_stage = m_render_graph.add<RasterOpaqueStage>(
    *m_renderer, m_scene_renderpass, m_scene_manager->default_descriptor()->layout(),
    std::string(SHADER_DIR "vertex.spv"), std::string(SHADER_DIR "fragment.spv"),
    &m_use_raytracing, &m_debug_2d_mode);
  m_raster_blend_stage = m_render_graph.add<RasterBlendStage>(
    *m_raster_opaque_stage, &m_use_raytracing, &m_debug_2d_mode);
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

  m_light_indicator_mesh = std::make_unique<Mesh>(m_renderer->device(), "light_indicator", vertices, indices);

  // No separate pipeline needed - reuse main pipeline with different model matrix
  spdlog::info("Created light indicator sphere ({} vertices)", vertices.size());
}

const std::string& Application::current_vertex_shader() const
{
  return m_raster_opaque_stage->current_vertex_shader();
}

const std::string& Application::current_fragment_shader() const
{
  return m_raster_opaque_stage->current_fragment_shader();
}

int Application::current_shader_mode() const
{
  return m_raster_opaque_stage->current_shader_mode();
}

void Application::reload_shaders(
  const std::string& vertex_shader, const std::string& fragment_shader)
{
  m_renderer->device().wait_idle();
  m_raster_opaque_stage->reload_shaders(vertex_shader, fragment_shader);
}

void Application::apply_shader_mode(int mode)
{
  m_renderer->device().wait_idle();
  m_raster_opaque_stage->apply_shader_mode(mode);
}

bool Application::save_screenshot(const std::string& filepath)
{
  return m_renderer->save_screenshot(filepath);
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
        apply_shader_mode(mode);
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
            static_cast<int>(m_renderer->window_width()), static_cast<int>(m_renderer->window_height()), 0);
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

  m_renderer->device().wait_idle();
  auto result = m_scene_manager->load_model(m_gltf_models[index], m_uniform_buffer->buffer());
  if (!result.success)
    return;

  // Camera + light reset
  if (result.bounds.valid())
  {
    float bounds[6];
    result.bounds.to_bounds(bounds);
    m_camera.reset_camera(bounds);

    // Place point light well outside the model (2x bounding sphere radius)
    if (auto* point = dynamic_cast<PointLight*>(m_light.get()))
    {
      glm::vec3 center = (result.bounds.min + result.bounds.max) * 0.5f;
      float radius = glm::length(result.bounds.max - result.bounds.min) * 0.5f;
      point->set_position(center + glm::normalize(glm::vec3(1, 1, 0.5f)) * radius * 2.0f);
    }
  }

  // RT rebuild (delegated to self-contained stage)
  if (m_ray_tracing_stage && m_scene_manager->mesh())
    m_ray_tracing_stage->on_mesh_changed(*m_scene_manager->mesh(), m_scene_manager->scene(), m_scene_manager->ibl());

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

  m_renderer->device().wait_idle();
  m_scene_manager->load_hdr(m_hdr_files[index], m_uniform_buffer->buffer());
  m_current_hdr_index = index;

  // Update RT environment cubemap
  if (m_ray_tracing_stage && m_scene_manager->ibl())
    m_ray_tracing_stage->update_environment(*m_scene_manager->ibl());
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

Application::~Application()
{
  spdlog::trace("Destroying Application");

  m_renderer->device().wait_idle();

  // Destroy resources before device
  m_scene_manager.reset();
  m_uniform_buffer.reset();

  // Scene pipelines + layout destroyed by RasterOpaqueStage (via RenderGraph)
  m_renderer->device().device().destroyRenderPass(m_scene_renderpass);

  // Debug2D pipeline destroyed by Debug2DStage (via RenderGraph)
  // Composite pipeline, descriptors, framebuffers destroyed by CompositeStage (via RenderGraph)
  // Scene framebuffers destroyed by RenderGraph destructor
  m_renderer->device().device().destroyRenderPass(m_composite_renderpass);

  // Depth-stencil, HDR, MSAA destroyed by renderer
  // SSS blur resources destroyed by SSSBlurStage (via RenderGraph)
  // RT resources destroyed by RayTracingStage (via RenderGraph)

  // Swapchain destroyed in renderer
  // Surface ..
  // Instance
  // glfw terminated in renderer
}

void Application::finalize_setup()
{
  // Register render passes with graph
  m_render_graph.set_render_pass(Phase::ScenePass, m_scene_renderpass);
  m_composite_renderpass = sps::vulkan::make_composite_renderpass(
    m_renderer->device().device(), m_renderer->swapchain().image_format(), true);
  m_render_graph.set_render_pass(Phase::CompositePass, m_composite_renderpass);

  // Populate shared image registry (before framebuffer and stage construction)
  m_render_graph.set_renderer(*m_renderer);
  m_render_graph.image_registry().set("hdr",
    { m_renderer->hdr_image(), m_renderer->hdr_image_view(), {}, m_renderer->hdr_format() });
  m_render_graph.image_registry().set("depth_stencil",
    { m_renderer->depth_stencil().image(), m_renderer->depth_stencil().stencil_view(),
      {}, m_renderer->depth_format() });

  // Create scene framebuffers (uses registry images + scene render pass)
  m_render_graph.create_scene_framebuffers();

  // Register render stages
  // Order within each phase doesn't matter — the render graph groups by phase.
  m_ray_tracing_stage = m_render_graph.add<RayTracingStage>(
    *m_renderer, m_render_graph, &m_use_raytracing, m_uniform_buffer->buffer());
  if (m_renderer->device().supports_ray_tracing() && m_scene_manager->mesh())
    m_ray_tracing_stage->on_mesh_changed(*m_scene_manager->mesh(), m_scene_manager->scene(), m_scene_manager->ibl());
  create_raster_stages();
  m_sss_blur_stage = m_render_graph.add<SSSBlurStage>(
    *m_renderer, m_render_graph,
    &m_use_sss_blur, &m_sss_blur_width_r, &m_sss_blur_width_g, &m_sss_blur_width_b);
  m_composite_stage = m_render_graph.add<CompositeStage>(
    *m_renderer, m_composite_renderpass, &m_exposure, &m_tonemap_mode);
  m_debug_2d_stage = m_render_graph.add<Debug2DStage>(
    *m_renderer, m_composite_renderpass, m_scene_manager->default_descriptor()->layout(),
    &m_debug_2d_mode, &m_debug_material_index);
  m_ui_stage = m_render_graph.add<UIStage>(&m_ui_render_callback);
}

VkInstance Application::vk_instance() const
{
  return m_renderer->instance().instance();
}

VkPhysicalDevice Application::vk_physical_device() const
{
  return m_renderer->device().physicalDevice();
}

VkDevice Application::vk_device() const
{
  return m_renderer->device().device();
}

VkQueue Application::vk_graphics_queue() const
{
  return m_renderer->device().graphics_queue();
}

uint32_t Application::graphics_queue_family() const
{
  return m_renderer->device().m_graphics_queue_family_index;
}

uint32_t Application::swapchain_image_count() const
{
  return static_cast<uint32_t>(m_renderer->swapchain().images().size());
}

GLFWwindow* Application::glfw_window() const
{
  return m_renderer->window().get();
}

bool Application::should_close() const
{
  return m_renderer->window().should_close();
}

void Application::poll_events()
{
  m_renderer->window().poll();
}

void Application::wait_idle()
{
  m_renderer->device().wait_idle();
}

void Application::update_frame()
{
  process_input();
  update_uniform_buffer();
}

void Application::set_vsync(bool enabled)
{
  if (m_renderer->vsync_enabled() != enabled)
  {
    m_renderer->vsync_enabled() = enabled;
    m_renderer->swapchain().set_vsync(enabled);
    recreate_swapchain();
  }
}
}
