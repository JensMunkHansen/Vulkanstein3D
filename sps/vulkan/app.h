#pragma once
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>
#include <sps/vulkan/config.h>
#include <string>

#include <sps/vulkan/app_config.h>
#include <sps/vulkan/camera.h>
#include <sps/vulkan/command_registry.h>
#include <sps/vulkan/descriptor_builder.h>
#include <sps/vulkan/gltf_loader.h>
#include <sps/vulkan/light.h>
#include <sps/vulkan/mesh.h>
#include <sps/vulkan/render_graph.h>
#include <sps/vulkan/renderer.h>
#include <sps/vulkan/scene_manager.h>
#include <sps/vulkan/texture.h>
#include <sps/vulkan/uniform_buffer.h>
#include <vulkan/vulkan_handles.hpp>

#include <glm/glm.hpp>

namespace sps::vulkan
{
class CommandRegistry;
class CompositeStage;
class Debug2DStage;
class SSSBlurStage;
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
  glm::vec4 ibl_params;   // offset 352, size 16 (x=useIBL, y=iblIntensity, z=tonemapMode, w=useSSS)
  glm::vec4 clear_color;  // offset 368, size 16 (rgb=background color, a=unused)
};

class Application
{
private:
  std::unique_ptr<VulkanRenderer> m_renderer;

  static RendererConfig build_renderer_config(int argc, char** argv, AppConfig& app_config);
  void apply_config(AppConfig config);
  bool m_stop_on_validation_message{ false };
  std::string m_geometry_source{"triangle"};
  std::string m_ply_file;
  std::string m_gltf_file;
  std::string m_hdr_file;
  std::vector<std::string> m_gltf_models;
  int m_current_model_index = -1;
  std::vector<std::string> m_hdr_files;
  int m_current_hdr_index = -1;
  IBLSettings m_ibl_settings;

public:
  Application(int argc, char* argv[]);
  void run();
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
  [[nodiscard]] VkRenderPass vk_renderpass() const { return m_composite_renderpass; }
  [[nodiscard]] VkSampleCountFlagBits msaa_samples() const { return VK_SAMPLE_COUNT_1_BIT; }
  [[nodiscard]] VkCommandPool vk_command_pool() const { return m_renderer->command_pool(); }
  [[nodiscard]] uint32_t swapchain_image_count() const;
  [[nodiscard]] GLFWwindow* glfw_window() const;
  [[nodiscard]] bool should_close() const;
  void poll_events();
  void wait_idle();
  void update_frame(); // Call process_input + update_uniform_buffer

  // Light type: 0=off, 1=point, 2=directional
  int light_type() const;
  void set_light_type(int type);

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
  bool& use_sss() { return m_use_sss; }
  float& sss_scale() { return m_sss_scale; }
  bool& use_ibl() { return m_use_ibl; }
  float ibl_intensity() const { return m_scene_manager->ibl_intensity(); }
  void set_ibl_intensity(float v) { m_scene_manager->set_ibl_intensity(v); }
  int& tonemap_mode() { return m_tonemap_mode; }
  static constexpr const char* tonemap_names[] = { "None", "Reinhard", "ACES (Fast)", "ACES (Hill)",
    "ACES + Boost", "Khronos PBR Neutral" };
  bool& use_sss_blur() { return m_use_sss_blur; }
  float& sss_blur_width_r() { return m_sss_blur_width_r; }
  float& sss_blur_width_g() { return m_sss_blur_width_g; }
  float& sss_blur_width_b() { return m_sss_blur_width_b; }
  bool& show_light_indicator() { return m_show_light_indicator; }
  glm::vec3& clear_color() { return m_clear_color; }
  Camera& camera() { return m_camera; }
  bool vsync_enabled() const { return m_renderer->vsync_enabled(); }
  void set_vsync(bool enabled);

  // Model switching
  const std::vector<std::string>& gltf_models() const { return m_gltf_models; }
  int current_model_index() const { return m_current_model_index; }
  void load_model(int index);

  // HDR environment switching
  const std::vector<std::string>& hdr_files() const { return m_hdr_files; }
  int current_hdr_index() const { return m_current_hdr_index; }
  void load_hdr(int index);

  // Shader management (delegated to RasterOpaqueStage)
  const std::string& current_vertex_shader() const;
  const std::string& current_fragment_shader() const;
  void reload_shaders(const std::string& vertex_shader, const std::string& fragment_shader);
  int current_shader_mode() const;
  void apply_shader_mode(int mode);

  // Screenshot
  bool save_screenshot(const std::string& filepath);
  bool save_screenshot(); // Auto-generate filename from current model name
  void begin_screenshot_all(); // Start cycling all models (one per frame)
  void tick_screenshot_all();  // Called each frame to advance the cycle

  // 2D Debug mode
  bool& debug_2d_mode() { return m_debug_2d_mode; }
  int& debug_texture_index()
  {
    return m_debug_texture_index;
  } // 0=base, 1=normal, 2=metalRough, 3=emissive, 4=ao
  int& debug_channel_mode() { return m_debug_channel_mode; } // 0=RGB, 1=R, 2=G, 3=B, 4=A
  int& debug_material_index() { return m_debug_material_index; }
  int material_count() const { return m_scene_manager->material_count(); }
  const AABB& scene_bounds() const { return m_scene_manager->bounds(); }
  float& debug_2d_zoom() { return m_debug_2d_zoom; }
  glm::vec2& debug_2d_pan() { return m_debug_2d_pan; }
  void reset_debug_2d_view()
  {
    m_debug_2d_zoom = 1.0f;
    m_debug_2d_pan = glm::vec2(0.0f);
  }
  static constexpr const char* texture_names[] = { "Base Color", "Normal", "Metal/Rough",
    "Emissive", "AO", "Iridescence", "Irid. Thickness", "Thickness" };
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
  void create_uniform_buffer();
  void create_scene_renderpass();
  void create_raster_stages();
  void create_light_indicator();

  void update_uniform_buffer();
  void process_input();

  static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
  static void mouse_callback(GLFWwindow* window, double xpos, double ypos);
  static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

  // HACK
  bool m_debugMode = true;
  bool m_backfaceCulling = true;
  bool m_depthTestEnabled = true;
  double m_lastTime, m_currentTime;
  int m_numFrames;
  float m_frameTime;

  // Scene render pass (HDR target) — pipelines owned by RasterOpaqueStage
  // Scene framebuffers owned by RenderGraph
  vk::RenderPass m_scene_renderpass;

  // Composite render pass (swapchain target — shared via RenderGraph registry)
  vk::RenderPass m_composite_renderpass;

  // SSS blur UI controls (passed as const pointers to SSSBlurStage)
  bool m_use_sss_blur{ false };
  // Per-channel blur widths for screen-space SSS (pixel scale multipliers).
  // Ratio ~5:2:1 matches skin scattering distances (Jimenez "Separable SSS"):
  //   Red ~3.67mm, Green ~1.37mm, Blue ~0.68mm
  // Actual pixel spread = offset[i] * blurWidth, where offset goes 0..12.
  float m_sss_blur_width_r{ 2.5f };
  float m_sss_blur_width_g{ 1.0f };
  float m_sss_blur_width_b{ 0.5f };


  // Camera
  Camera m_camera;

  // Scene manager (owns mesh, scene, textures, IBL, descriptors)
  std::unique_ptr<SceneManager> m_scene_manager;

  // Light indicator (procedural sphere for visualizing point light position)
  std::unique_ptr<Mesh> m_light_indicator_mesh;
  bool m_show_light_indicator{ true };

  // Uniform buffer (using wrapper)
  std::unique_ptr<UniformBuffer<UniformBufferObject>> m_uniform_buffer;

  // Lighting
  bool m_light_enabled{ true };
  std::unique_ptr<Light> m_light;
  float m_shininess{ 32.0f };
  float m_specularStrength{ 0.4f };
  float m_metallicAmbient{ 0.3f }; // Fake IBL strength for metals (0-1)
  float m_aoStrength{ 1.0f };      // AO influence (0-1)
  float m_exposure{ 1.0f };        // Exposure/brightness multiplier
  glm::vec3 m_clear_color{ 0.0f, 0.0f, 0.0f }; // Background clear color

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
  bool m_use_sss = true;            // Subsurface scattering enabled by default
  float m_sss_scale = 1.0f;        // SSS intensity multiplier (0-5)
  bool m_use_ibl = true;            // IBL enabled by default
  int m_tonemap_mode =
    5; // 0=None, 1=Reinhard, 2=ACES Fast, 3=ACES Hill, 4=ACES+Boost, 5=Khronos PBR

  // Screenshot-all state machine
  int m_screenshot_all_index = -1;       // -1 = not active, >=0 = current model to screenshot
  int m_screenshot_all_restore = -1;     // model index to restore after completion
  int m_screenshot_all_frames_wait = 0;  // frames to wait after model load before capture

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
  std::filesystem::file_time_type m_command_file_mtime = std::filesystem::file_time_type::min();


  // Render graph (stage-based command recording)
  RenderGraph m_render_graph;
  CompositeStage* m_composite_stage{ nullptr };
  SSSBlurStage* m_sss_blur_stage{ nullptr };
  Debug2DStage* m_debug_2d_stage{ nullptr };
  RasterOpaqueStage* m_raster_opaque_stage{ nullptr };
  RasterBlendStage* m_raster_blend_stage{ nullptr };
  RayTracingStage* m_ray_tracing_stage{ nullptr };
  UIStage* m_ui_stage{ nullptr };
};
}
