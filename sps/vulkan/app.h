#pragma once
#include <sps/vulkan/config.h>
#include <string>

#include <sps/vulkan/renderer.h>

namespace sps::vulkan
{
class Application : public VulkanRenderer
{
private:
  void load_toml_configuration_file(const std::string& file_name);
  bool m_enable_validation_layers{ true };

public:
  Application(int argc, char* argv[]);
  void run();
};
}
