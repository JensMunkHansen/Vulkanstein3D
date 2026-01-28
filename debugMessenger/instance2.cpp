#include "instance2.h"

#include <GLFW/glfw3.h>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
std::vector<const char*> getRequiredExtensions()
{
  uint32_t glfwExtensionCount = 0;
  const auto** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  return { glfwExtensions, glfwExtensions + glfwExtensionCount };
}
} // namespace

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  VkDebugUtilsMessageTypeFlagsEXT messageType,
  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
  std::cerr << "Validation Layer: " << pCallbackData->pMessage << std::endl;

  return VK_FALSE;
}

namespace
{

bool supported(std::vector<const char*>& extensions, std::vector<const char*>& layers, bool debug)
{

  // check extension support
  std::vector<vk::ExtensionProperties> supportedExtensions =
    vk::enumerateInstanceExtensionProperties();

  if (debug)
  {
    std::cout << "Device can support the following extensions:\n";
    for (vk::ExtensionProperties supportedExtension : supportedExtensions)
    {
      std::cout << '\t' << supportedExtension.extensionName << '\n';
    }
  }

  bool found;
  for (const char* extension : extensions)
  {
    found = false;
    for (vk::ExtensionProperties supportedExtension : supportedExtensions)
    {
      if (strcmp(extension, supportedExtension.extensionName) == 0)
      {
        found = true;
        if (debug)
        {
          std::cout << "Extension \"" << extension << "\" is supported!\n";
        }
      }
    }
    if (!found)
    {
      if (debug)
      {
        std::cout << "Extension \"" << extension << "\" is not supported!\n";
      }
      return false;
    }
  }

  // check layer support
  std::vector<vk::LayerProperties> supportedLayers = vk::enumerateInstanceLayerProperties();

  if (debug)
  {
    std::cout << "Device can support the following layers:\n";
    for (vk::LayerProperties supportedLayer : supportedLayers)
    {
      std::cout << '\t' << supportedLayer.layerName << '\n';
    }
  }

  for (const char* layer : layers)
  {
    found = false;
    for (vk::LayerProperties supportedLayer : supportedLayers)
    {
      if (strcmp(layer, supportedLayer.layerName) == 0)
      {
        found = true;
        if (debug)
        {
          std::cout << "Layer \"" << layer << "\" is supported!\n";
        }
      }
    }
    if (!found)
    {
      if (debug)
      {
        std::cout << "Layer \"" << layer << "\" is not supported!\n";
      }
      return false;
    }
  }

  return true;
}

}

namespace engine::core
{

vk::detail::DynamicLoader Instance::dl;

Instance::Instance()
{
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
    dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

  bool debug = true;

  uint32_t version{ 0 };
  vkEnumerateInstanceVersion(&version);

  if (debug)
  {
    std::cout << "System can support vulkan Variant: " << VK_API_VERSION_VARIANT(version)
              << ", Major: " << VK_API_VERSION_MAJOR(version)
              << ", Minor: " << VK_API_VERSION_MINOR(version)
              << ", Patch: " << VK_API_VERSION_PATCH(version) << '\n';
  }

  /*
   * We can then either use this version
   * (We shoud just be sure to set the patch to 0 for best compatibility/stability)
   */
  version &= ~(0xFFFU);

  /*
   * Or drop down to an earlier version to ensure compatibility with more devices
   * VK_MAKE_API_VERSION(variant, major, minor, patch)
   */
  version = VK_MAKE_API_VERSION(0, 1, 0, 0);

  /*
  * from vulkan_structs.hpp:
  *
  * VULKAN_HPP_CONSTEXPR ApplicationInfo( const char * pApplicationName_   = {},
                                                                    uint32_t     applicationVersion_
  = {}, const char * pEngineName_        = {}, uint32_t     engineVersion_      = {}, uint32_t
  apiVersion_         = {} )
  */

  vk::ApplicationInfo appInfo =
    vk::ApplicationInfo("BUM", version, "Doing it the hard way", version, version);

  /*
   * Everything with Vulkan is "opt-in", so we need to query which extensions glfw needs
   * in order to interface with vulkan.
   */
  uint32_t glfwExtensionCount = 0;
  const char** glfwExtensions;
  glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

  // In order to hook in a custom validation callback
  if (debug)
  {
    extensions.push_back("VK_EXT_debug_utils");
  }

  if (debug)
  {
    std::cout << "extensions to be requested:\n";

    for (const char* extensionName : extensions)
    {
      std::cout << "\t\"" << extensionName << "\"\n";
    }
  }

  std::vector<const char*> layers;
  if (debug)
  {
    layers.push_back("VK_LAYER_KHRONOS_validation");
  }

  if (!supported(extensions, layers, debug))
  {
    std::cerr << "UPS" << std::endl;
  }
  /*
  *
  * from vulkan_structs.hpp:
  *
  * InstanceCreateInfo( VULKAN_HPP_NAMESPACE::InstanceCreateFlags     flags_                 = {},
                                                                           const
  VULKAN_HPP_NAMESPACE::ApplicationInfo * pApplicationInfo_      = {}, uint32_t enabledLayerCount_
  = {}, const char * const *                          ppEnabledLayerNames_   = {}, uint32_t
  enabledExtensionCount_ = {}, const char * const * ppEnabledExtensionNames_ = {} )
  */
  vk::InstanceCreateInfo createInfo = vk::InstanceCreateInfo(vk::InstanceCreateFlags(), &appInfo,
    static_cast<uint32_t>(layers.size()), layers.data(),        // enabled layers
    static_cast<uint32_t>(extensions.size()), extensions.data() // enabled extensions
  );

  vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo;

  debugUtilsMessengerCreateInfo.setMessageSeverity(
    vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
    vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
    vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
  debugUtilsMessengerCreateInfo.setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
    vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
    vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance);
  debugUtilsMessengerCreateInfo.setPfnUserCallback(debugCallback);

  createInfo.setPNext(&debugUtilsMessengerCreateInfo);

  m_instance = vk::createInstance(createInfo, nullptr);
  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);
  m_debugUtilsMessenger = m_instance.createDebugUtilsMessengerEXT(
    debugUtilsMessengerCreateInfo, nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);
}

Instance::~Instance()
{
  m_instance.destroy(m_debugUtilsMessenger, nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);
  m_instance.destroy();
}

}
