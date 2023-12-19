#pragma once
#include <sps/vulkan/config.h>
#include <string>

#include <sps/vulkan/renderer.h>
#include <vulkan/vulkan_handles.hpp>

namespace sps::vulkan
{
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
  void render();
  ~Application();

private:
  bool m_debugMode = true;
  vk::PipelineLayout m_pipelineLayout;
  vk::RenderPass m_renderpass;
  vk::Pipeline m_pipeline;
};
}
