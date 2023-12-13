#pragma once
#include <config.h>
#include <cstdint>

#include <vulkan/vulkan.hpp>

namespace sps::vulkan
{
class Instance
{
private:
  VkInstance m_instance{ VK_NULL_HANDLE };
  static constexpr std::uint32_t REQUIRED_VK_API_VERSION{ VK_MAKE_API_VERSION(0, 1, 2, 0) };

public:
  [[nodiscard]] static bool is_layer_supported(const std::string& layer_name);
  [[nodiscard]] static bool is_extension_supported(const std::string& extension_name);

  Instance(const std::string& application_name, const std::string& engine_name,
    std::uint32_t application_version, std::uint32_t engine_version, bool enable_validation_layers,
    bool enable_renderdoc_layer, const std::vector<std::string>& requested_instance_extensions,
    const std::vector<std::string>& requested_instance_layers);

  Instance(const std::string& application_name, const std::string& engine_name,
    std::uint32_t application_version, std::uint32_t engine_version, bool enable_validation_layers,
    bool enable_renderdoc_layer);

  Instance& operator=(const Instance&) = delete;
  Instance& operator=(Instance&&) = default;

  [[nodiscard]] VkInstance instance() const { return m_instance; }
};
}
