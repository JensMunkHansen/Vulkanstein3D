#include <vulkan/vulkan.hpp>

namespace engine::core
{
constexpr auto validationLayers = { "VK_LAYER_KHRONOS_validation" };

class Instance
{
public:
  Instance();
  ~Instance();
  [[nodiscard]] const vk::Instance& getInstance() const { return m_instance; }

private:
  vk::detail::DispatchLoaderDynamic m_dldi;
  vk::Instance m_instance{ VK_NULL_HANDLE };
  vk::DebugUtilsMessengerEXT m_debugUtilsMessenger;
};
}
