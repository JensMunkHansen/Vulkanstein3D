#pragma once
#include <ctime>
#include <functional>
#include <memory>
#include <sps/vulkan/config.h>
#include <string>

#include <sps/vulkan/acceleration_structure.h>
#include <sps/vulkan/camera.h>
#include <sps/vulkan/command_registry.h>
#include <sps/vulkan/descriptor_builder.h>
#include <sps/vulkan/gltf_loader.h>
#include <sps/vulkan/light.h>
#include <sps/vulkan/mesh.h>
#include <sps/vulkan/raytracing_pipeline.h>
#include <sps/vulkan/render_graph.h>
#include <sps/vulkan/renderer.h>
#include <sps/vulkan/scene_manager.h>
#include <sps/vulkan/texture.h>
#include <sps/vulkan/uniform_buffer.h>
#include <vulkan/vulkan_handles.hpp>

#include <glm/glm.hpp>

namespace sps::vulkan
{
class Fence;
class Semaphore;
class CommandRegistry;
class Debug2DStage;
class RasterOpaqueStage;
class RasterBlendStage;
class RayTracingStage;
class UIStage;

/// Uniform buffer object layout (must match shader std140 layout)
/// Used for both rasterization and ray tracing
struct UniformBufferObject
{
  glm::mat4 view;          // offset 0,   size 64 (raster: view matrix)
  glm::mat4 proj;          // offset 64,  size 64 (raster: projection matrix)
  glm::mat4 viewInverse;   // offset 128, size 64 (RT: camera transform)
  glm::mat4 projInverse;   // offset 192, size 64 (RT: unproject rays)
  glm::vec4 lightPosition; // offset 256, size 16 (xyz=dir/pos, w=type: 0=dir, 1=point)
  glm::vec4 lightColor;    // offset 272, size 16 (rgb=color, a=intensity)
  glm::vec4 lightAmbient;  // offset 288, size 16 (rgb=ambient)
  glm::vec4 viewPos;       // offset 304, size 16 (camera position)
  glm::vec4
    material; // offset 320, size 16 (x=shininess, y=specStrength, z=metallicAmbient, w=aoStrength)
  glm::vec4 flags;      // offset 336, size 16 (x=useNormalMap, y=useEmissive, z=useAO, w=exposure)
  glm::vec4 ibl_params; // offset 352, size 16 (x=useIBL, y=iblIntensity, z=tonemapMode, w=reserved)
};

class Application : public VulkanRenderer
{
private:
  void load_toml_configuration_file(const std::string& file_name);
  bool m_stop_on_validation_message{ false };
  std::string m_preferred_gpu;  // From TOML: vulkan.preferred_gpu
  std::string m_geometry_source{"triangle"};  // From TOML: application.geometry.source
  std::string m_ply_file;  // From TOML: application.geometry.ply_file
  std::string m_gltf_file;  // From TOML: application.geometry.gltf_file
  std::string m_hdr_file;   // From TOML: application.geometry.hdr_file
  std::vector<std::string> m_gltf_models;  // From TOML: [glTFmodels].files
  int m_current_model_index = -1;          // Index into m_gltf_models (-1 = none)

public:
  Application(int argc, char* argv[]);
  void run();
  void make_pipeline();
  void make_pipeline(const std::string& vertex_shader, const std::string& fragment_shader);
  void finalize_setup();
  void recreate_swapchain();
  void record_draw_commands(vk::CommandBuffer, uint32_t imageIndex);

  void calculateFrameRate();

  void render();
  ~Application();

  // Accessors for external tools (ImGui, etc.)
  [[nodiscard]] VkInstance vk_instance() const;
  [[nodiscard]] VkPhysicalDevice vk_physical_device() const;
  [[nodiscard]] VkDevice vk_device() const;
  [[nodiscard]] VkQueue vk_graphics_queue() const;
  [[nodiscard]] uint32_t graphics_queue_family() const;
  [[nodiscard]] VkRenderPass vk_renderpass() const { return m_renderpass; }
  [[nodiscard]] VkCommandPool vk_command_pool() const { return m_commandPool; }
  [[nodiscard]] uint32_t swapchain_image_count() const;
  [[nodiscard]] GLFWwindow* glfw_window() const;
  [[nodiscard]] bool should_close() const;
  void poll_events();
  void wait_idle();
  void update_frame(); // Call process_input + update_uniform_buffer

  // Mutable accessors for UI controls
  Light& light() { return *m_light; }
  float& shininess() { return m_shininess; }
  float& specular_strength() { return m_specularStrength; }
  float& metallic_ambient() { return m_metallicAmbient; }
  float& ao_strength() { return m_aoStrength; }
  float& exposure() { return m_exposure; }
  bool& use_raytracing() { return m_use_raytracing; }
  bool& use_normal_mapping() { return m_use_normal_mapping; }
  bool& use_emissive() { return m_use_emissive; }
  bool& use_ao() { return m_use_ao; }
  bool& use_ibl() { return m_use_ibl; }
  float ibl_intensity() const { return m_scene_manager->ibl_intensity(); }
  void set_ibl_intensity(float v) { m_scene_manager->set_ibl_intensity(v); }
  int& tonemap_mode() { return m_tonemap_mode; }
  static constexpr const char* tonemap_names[] = { "None", "Reinhard", "ACES (Fast)", "ACES (Hill)",
    "ACES + Boost", "Khronos PBR Neutral" };
  bool& show_light_indicator() { return m_show_light_indicator; }
  Camera& camera() { return m_camera; }
  bool vsync_enabled() const { return m_vsync_enabled; }
  void set_vsync(bool enabled);

  // Model switching
  const std::vector<std::string>& gltf_models() const { return m_gltf_models; }
  int current_model_index() const { return m_current_model_index; }
  void load_model(int index);

  // Shader management
  const std::string& current_vertex_shader() const { return m_vertex_shader_path; }
  const std::string& current_fragment_shader() const { return m_fragment_shader_path; }
  void reload_shaders(const std::string& vertex_shader, const std::string& fragment_shader);

  // Available shader modes for UI (must match main_imgui.cpp)
  static constexpr int shader_mode_count = 8;
  int& current_shader_mode() { return m_current_shader_mode; }
  void apply_shader_mode(int mode);

  // Screenshot
  bool save_screenshot(const std::string& filepath);
  bool save_screenshot(); // Auto-generate filename

  // 2D Debug mode
  bool& debug_2d_mode() { return m_debug_2d_mode; }
  int& debug_texture_index()
  {
    return m_debug_texture_index;
  } // 0=base, 1=normal, 2=metalRough, 3=emissive, 4=ao
  int& debug_channel_mode() { return m_debug_channel_mode; } // 0=RGB, 1=R, 2=G, 3=B, 4=A
  int& debug_material_index() { return m_debug_material_index; }
  int material_count() const { return m_scene_manager->material_count(); }
  float& debug_2d_zoom() { return m_debug_2d_zoom; }
  glm::vec2& debug_2d_pan() { return m_debug_2d_pan; }
  void reset_debug_2d_view()
  {
    m_debug_2d_zoom = 1.0f;
    m_debug_2d_pan = glm::vec2(0.0f);
  }
  static constexpr const char* texture_names[] = { "Base Color", "Normal", "Metal/Rough",
    "Emissive", "AO" };
  static constexpr const char* channel_names[] = { "RGB", "R", "G", "B", "A" };

  // Call after ImGui to sync uniforms before render
  void sync_uniforms() { update_uniform_buffer(); }

  // Command file for remote control
  void poll_commands();

  // Callback for injecting UI rendering (called during render pass, before endRenderPass)
  using RenderCallback = std::function<void(vk::CommandBuffer)>;
  void set_ui_render_callback(RenderCallback callback)
  {
    m_ui_render_callback = std::move(callback);
  }

private:
  void setup_camera();
  void create_depth_resources();
  void create_uniform_buffer();
  void create_debug_2d_pipeline();
  void create_light_indicator();

  // Ray tracing setup
  void create_rt_storage_image();
  void create_rt_descriptor();
  void create_rt_pipeline();
  void build_acceleration_structures();
  void update_uniform_buffer();
  void process_input();

  static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
  static void mouse_callback(GLFWwindow* window, double xpos, double ypos);
  static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

  // HACK
  bool m_debugMode = true;
  bool m_backfaceCulling = true;
  bool m_depthTestEnabled = true;
  vk::Format m_depthFormat = vk::Format::eD32Sfloat;
  double m_lastTime, m_currentTime;
  int m_numFrames;
  float m_frameTime;

  std::vector<vk::Framebuffer> m_frameBuffers;
  vk::PipelineLayout m_pipelineLayout;
  vk::RenderPass m_renderpass;
  vk::Pipeline m_pipeline;
  vk::Pipeline m_blend_pipeline; // blend on, depth write off

  // 2D debug pipeline (fullscreen quad)
  vk::Pipeline m_debug_2d_pipeline;
  vk::PipelineLayout m_debug_2d_pipelineLayout;

  // Depth buffer
  vk::Image m_depthImage;
  vk::DeviceMemory m_depthImageMemory;
  vk::ImageView m_depthImageView;
  vk::CommandPool m_commandPool;
  vk::CommandBuffer m_mainCommandBuffer;
  std::vector<vk::CommandBuffer> m_commandBuffers{};

  std::unique_ptr<Semaphore> m_imageAvailable;
  std::vector<std::unique_ptr<Semaphore>> m_renderFinished;
  std::unique_ptr<Fence> m_inFlight;

  // Camera
  Camera m_camera;

  // Scene manager (owns mesh, scene, textures, IBL, descriptors)
  std::unique_ptr<SceneManager> m_scene_manager;

  // Light indicator (procedural sphere for visualizing point light position)
  std::unique_ptr<Mesh> m_light_indicator_mesh;
  bool m_show_light_indicator{ true };

  // Uniform buffer (using wrapper)
  std::unique_ptr<UniformBuffer<UniformBufferObject>> m_uniform_buffer;

  // Ray tracing resources
  std::unique_ptr<AccelerationStructure> m_blas;
  std::unique_ptr<AccelerationStructure> m_tlas;
  std::unique_ptr<RayTracingPipeline> m_rt_pipeline;

  // RT storage image (render target)
  vk::Image m_rt_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_rt_image_memory{ VK_NULL_HANDLE };
  vk::ImageView m_rt_image_view{ VK_NULL_HANDLE };

  // RT descriptor set
  vk::DescriptorPool m_rt_descriptor_pool{ VK_NULL_HANDLE };
  vk::DescriptorSetLayout m_rt_descriptor_layout{ VK_NULL_HANDLE };
  vk::DescriptorSet m_rt_descriptor_set{ VK_NULL_HANDLE };

  // Lighting
  std::unique_ptr<Light> m_light;
  float m_shininess{ 32.0f };
  float m_specularStrength{ 0.4f };
  float m_metallicAmbient{ 0.3f }; // Fake IBL strength for metals (0-1)
  float m_aoStrength{ 1.0f };      // AO influence (0-1)
  float m_exposure{ 1.0f };        // Exposure/brightness multiplier

  // Input state
  bool m_keys[512] = { false };
  double m_lastMouseX = 0.0;
  double m_lastMouseY = 0.0;
  bool m_firstMouse = true;
  bool m_mousePressed = false;

  // Rendering mode toggles
  bool m_use_raytracing = false;    // Start with rasterization (R key to switch)
  bool m_use_normal_mapping = true; // Normal mapping enabled by default
  bool m_use_emissive = true;       // Emissive texture enabled by default
  bool m_use_ao = true;             // Ambient occlusion enabled by default
  bool m_use_ibl = false;           // IBL disabled by default (use direct lighting)
  int m_tonemap_mode =
    5; // 0=None, 1=Reinhard, 2=ACES Fast, 3=ACES Hill, 4=ACES+Boost, 5=Khronos PBR
  int m_current_shader_mode = 0; // Index into shader_modes[]

  // 2D Debug mode
  bool m_debug_2d_mode = false;
  int m_debug_texture_index = 0;              // Which texture to display
  int m_debug_channel_mode = 0;               // Which channel(s) to display
  int m_debug_material_index = 0;             // Which material to inspect
  float m_debug_2d_zoom = 1.0f;               // Zoom level (1.0 = 100%)
  glm::vec2 m_debug_2d_pan = glm::vec2(0.0f); // Pan offset

  // UI render callback (for ImGui etc.)
  RenderCallback m_ui_render_callback;

  // Command file remote control
  std::unique_ptr<CommandRegistry> m_command_registry;
  std::string m_command_file_path{ "./commands.txt" };
  time_t m_command_file_mtime{ 0 };

  // Current shader paths
  std::string m_vertex_shader_path;
  std::string m_fragment_shader_path;

  // Render graph (stage-based command recording)
  RenderGraph m_render_graph;
  Debug2DStage* m_debug_2d_stage{ nullptr };
  RasterOpaqueStage* m_raster_opaque_stage{ nullptr };
  RasterBlendStage* m_raster_blend_stage{ nullptr };
  RayTracingStage* m_ray_tracing_stage{ nullptr };
  UIStage* m_ui_stage{ nullptr };
};
}
