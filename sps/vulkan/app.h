#pragma once
#include <sps/vulkan/config.h>
#include <string>

#include <sps/vulkan/camera.h>
#include <sps/vulkan/light.h>
#include <sps/vulkan/descriptor_builder.h>
#include <sps/vulkan/mesh.h>
#include <sps/vulkan/renderer.h>
#include <sps/vulkan/uniform_buffer.h>
#include <sps/vulkan/acceleration_structure.h>
#include <sps/vulkan/raytracing_pipeline.h>
#include <vulkan/vulkan_handles.hpp>

#include <glm/glm.hpp>

namespace sps::vulkan
{
class Fence;
class Semaphore;

/// Uniform buffer object layout (must match shader std140 layout)
/// Used for both rasterization and ray tracing
struct UniformBufferObject
{
  glm::mat4 view;           // offset 0,   size 64 (raster: view matrix)
  glm::mat4 proj;           // offset 64,  size 64 (raster: projection matrix)
  glm::mat4 viewInverse;    // offset 128, size 64 (RT: camera transform)
  glm::mat4 projInverse;    // offset 192, size 64 (RT: unproject rays)
  glm::vec4 lightPosition;  // offset 256, size 16 (xyz=dir/pos, w=type)
  glm::vec4 lightColor;     // offset 272, size 16 (rgb=color, a=intensity)
  glm::vec4 lightAmbient;   // offset 288, size 16 (rgb=ambient)
  glm::vec4 viewPos;        // offset 304, size 16 (camera position)
  glm::vec4 material;       // offset 320, size 16 (x=shininess, yzw=unused)
};

class Application : public VulkanRenderer
{
private:
  void load_toml_configuration_file(const std::string& file_name);
  bool m_stop_on_validation_message{ false };
  std::string m_preferred_gpu;  // From TOML: vulkan.preferred_gpu
  std::string m_geometry_source{"triangle"};  // From TOML: application.geometry.source
  std::string m_ply_file;  // From TOML: application.geometry.ply_file

public:
  Application(int argc, char* argv[]);
  void run();
  void make_pipeline();
  void finalize_setup();
  void recreate_swapchain();
  void record_draw_commands(vk::CommandBuffer, uint32_t imageIndex);

  void calculateFrameRate();

  void render();
  ~Application();

private:
  void setup_camera();
  void create_default_mesh();
  void create_depth_resources();
  void create_uniform_buffer();
  void create_descriptor();

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

  // Depth buffer
  vk::Image m_depthImage;
  vk::DeviceMemory m_depthImageMemory;
  vk::ImageView m_depthImageView;
  vk::CommandPool m_commandPool;
  vk::CommandBuffer m_mainCommandBuffer;
  std::vector<vk::CommandBuffer> m_commandBuffers{};

  std::unique_ptr<Semaphore> m_imageAvailable;
  std::unique_ptr<Semaphore> m_renderFinished;
  std::unique_ptr<Fence> m_inFlight;

  // Camera
  Camera m_camera;

  // Mesh
  std::unique_ptr<Mesh> m_mesh;

  // Uniform buffer (using wrapper)
  std::unique_ptr<UniformBuffer<UniformBufferObject>> m_uniform_buffer;

  // Descriptor (using wrapper)
  std::unique_ptr<ResourceDescriptor> m_descriptor;

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

  // Input state
  bool m_keys[512] = { false };
  double m_lastMouseX = 0.0;
  double m_lastMouseY = 0.0;
  bool m_firstMouse = true;
  bool m_mousePressed = false;

  // Rendering mode toggle (R key to switch)
  bool m_use_raytracing = false;  // Start with rasterization
};
}
