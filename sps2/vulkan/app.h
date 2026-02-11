#pragma once
#include <sps/vulkan/config.h>

#include <sps/vulkan/renderer.h>

namespace sps::vulkan
{
class Application
{
private:
  void load_toml_configuration_file(const std::string& file_name);
  bool m_enable_validation_layers{ true };
  VulkanRenderer m_renderer;

public:
  Application(int argc, char* argv[]);
};
}
