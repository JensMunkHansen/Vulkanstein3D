#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <vulkan/vulkan.hpp>

namespace engine::core
{
constexpr auto validationLayers = { "VK_LAYER_KHRONOS_validation" };

class Instance
{
public:
  static vk::DynamicLoader dl;

  Instance();
  ~Instance();
  [[nodiscard]] const vk::Instance& getInstance() const { return m_instance; }

private:
  vk::Instance m_instance{ VK_NULL_HANDLE };
  vk::DebugUtilsMessengerEXT m_debugUtilsMessenger;
};
}
