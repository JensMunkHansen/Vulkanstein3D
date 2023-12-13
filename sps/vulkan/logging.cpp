#include <sps/vulkan/config.h>

#include <spdlog/spdlog.h>
#include <sps/vulkan/logging.h>

namespace sps::vulkan
{

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  VkDebugUtilsMessageTypeFlagsEXT messageType,
  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
  spdlog::error("validation layer: {}", pCallbackData->pMessage);

  return VK_FALSE;
}

vk::DebugUtilsMessengerEXT make_debug_messenger(
  vk::Instance instance, vk::DispatchLoaderDynamic& dldi)
{

  /*
  * DebugUtilsMessengerCreateInfoEXT( VULKAN_HPP_NAMESPACE::DebugUtilsMessengerCreateFlagsEXT flags_
  = {}, VULKAN_HPP_NAMESPACE::DebugUtilsMessageSeverityFlagsEXT messageSeverity_ = {},
                                                                  VULKAN_HPP_NAMESPACE::DebugUtilsMessageTypeFlagsEXT
  messageType_     = {}, PFN_vkDebugUtilsMessengerCallbackEXT                    pfnUserCallback_ =
  {}, void * pUserData_ = {} )
  */

  vk::DebugUtilsMessengerCreateInfoEXT createInfo =
    vk::DebugUtilsMessengerCreateInfoEXT(vk::DebugUtilsMessengerCreateFlagsEXT(),
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
      vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
      debugCallback, nullptr);

  return instance.createDebugUtilsMessengerEXT(createInfo, nullptr, dldi);
}
}
