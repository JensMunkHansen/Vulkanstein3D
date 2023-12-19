#pragma once
#include <sps/vulkan/config.h>
#include <string>

#include <sps/vulkan/renderer.h>
#include <vulkan/vulkan_handles.hpp>

namespace sps::vulkan
{
class Fence;
class Semaphore;

class Application : public VulkanRenderer
{
private:
  void load_toml_configuration_file(const std::string& file_name);
  bool m_stop_on_validation_message{ false };

public:
  Application(int argc, char* argv[]);
  void run();
  void make_pipeline();
  void finalize_setup();
  void record_draw_commands(vk::CommandBuffer, uint32_t imageIndex);

  void render();
  ~Application();

private:
  // HACK
  bool m_debugMode = true;
  std::vector<vk::Framebuffer> m_frameBuffers;
  vk::PipelineLayout m_pipelineLayout;
  vk::RenderPass m_renderpass;
  vk::Pipeline m_pipeline;
  vk::CommandPool m_commandPool;
  vk::CommandBuffer m_mainCommandBuffer;
  std::vector<vk::CommandBuffer> m_commandBuffers{};

  std::unique_ptr<Semaphore> m_imageAvailable;
  std::unique_ptr<Semaphore> m_renderFinished;
  std::unique_ptr<Fence> m_inFlight;
};
}
