#include <sps/vulkan/config.h>

#include <sps/vulkan/exception.h>
#include <sps/vulkan/instance.h>
#include <sps/vulkan/logging.h>
#include <sps/vulkan/make_info.h>
#include <sps/vulkan/representation.h>

#include <GLFW/glfw3.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_handles.hpp>

#include <iostream>

#ifdef SPS_VULKAN_DISPATCH_LOADER_DYNAMIC
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#endif

#if 0
#include <vulkan/vulkan.h>

static PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT_;
#define vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT_

static PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT_;
#define vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT_

#include <vulkan/vulkan.hpp>

/// load the functions when needed
vkCreateDebugUtilsMessengerEXT_ = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            instance->getProcAddr("vkCreateDebugUtilsMessengerEXT"));
vkDestroyDebugUtilsMessengerEXT_ = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            instance->getProcAddr("vkDestroyDebugUtilsMessengerEXT"));
#endif

namespace sps::vulkan
{
bool Instance::is_layer_supported(const std::string& layer_name)
{

#ifdef SPS_VULKAN_DISPATCH_LOADER_DYNAMIC
  vk::DynamicLoader dl;
  PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties =
    dl.getProcAddress<PFN_vkEnumerateInstanceLayerProperties>("vkEnumerateInstanceLayerProperties");
#endif

  std::uint32_t instance_layer_count = 0;

  if (const auto result = vkEnumerateInstanceLayerProperties(&instance_layer_count, nullptr);
      result != VK_SUCCESS)
  {
    throw VulkanException("Error: vkEnumerateInstanceLayerProperties failed!", result);
  }

  if (instance_layer_count == 0)
  {
    // This is not an error. Some platforms simply don't have any instance layers.
    spdlog::info("No Vulkan instance layers available!");
    return false;
  }
  std::vector<VkLayerProperties> instance_layers(instance_layer_count);

  // Store all available instance layers.
  if (const auto result =
        vkEnumerateInstanceLayerProperties(&instance_layer_count, instance_layers.data());
      result != VK_SUCCESS)
  {
    throw VulkanException("Error: vkEnumerateInstanceLayerProperties failed!", result);
  }

  // Search for the requested instance layer.
  return std::find_if(instance_layers.begin(), instance_layers.end(),
           [&](const VkLayerProperties instance_layer)
           { return instance_layer.layerName == layer_name; }) != instance_layers.end();
}

bool Instance::is_extension_supported(const std::string& extension_name)
{
  std::uint32_t instance_extension_count = 0;

#ifdef SPS_VULKAN_DISPATCH_LOADER_DYNAMIC
  vk::DynamicLoader dl;
  PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties =
    dl.getProcAddress<PFN_vkEnumerateInstanceExtensionProperties>(
      "vkEnumerateInstanceExtensionProperties");
#endif

  if (const auto result =
        vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, nullptr);
      result != VK_SUCCESS)
  {
    throw VulkanException("Error: vkEnumerateInstanceExtensionProperties failed!", result);
  }

  if (instance_extension_count == 0)
  {
    // This is not an error. Some platforms simply don't have any instance extensions.
    spdlog::info("No Vulkan instance extensions available!");
    return false;
  }

  std::vector<VkExtensionProperties> instance_extensions(instance_extension_count);

  // Store all available instance extensions.
  if (const auto result = vkEnumerateInstanceExtensionProperties(
        nullptr, &instance_extension_count, instance_extensions.data());
      result != VK_SUCCESS)
  {
    throw VulkanException("Error: vkEnumerateInstanceExtensionProperties failed!", result);
  }

  // Search for the requested instance extension.
  return std::find_if(instance_extensions.begin(), instance_extensions.end(),
           [&](const VkExtensionProperties instance_extension) {
             return instance_extension.extensionName == extension_name;
           }) != instance_extensions.end();
}

Instance::Instance(const std::string& application_name, const std::string& engine_name,
  const std::uint32_t application_version, const std::uint32_t engine_version,
  bool enable_validation_layers, bool enable_renderdoc_layer,
  const std::vector<std::string>& requested_instance_extensions,
  const std::vector<std::string>& requested_instance_layers)
{
  assert(!application_name.empty());
  assert(!engine_name.empty());

  spdlog::trace("Initializing Vulkan metaloader");

  // Vulkan defined macro storage for dispatch loader

  spdlog::trace("Initialising Vulkan instance");
  spdlog::trace("Application name: {}", application_name);
  spdlog::trace("Application version: {}.{}.{}", VK_API_VERSION_MAJOR(application_version),
    VK_API_VERSION_MINOR(application_version), VK_API_VERSION_PATCH(application_version));
  spdlog::trace("Engine name: {}", engine_name);
  spdlog::trace("Engine version: {}.{}.{}", VK_API_VERSION_MAJOR(engine_version),
    VK_API_VERSION_MINOR(engine_version), VK_API_VERSION_PATCH(engine_version));
  spdlog::trace("Requested Vulkan API version: {}.{}.{}",
    VK_API_VERSION_MAJOR(REQUIRED_VK_API_VERSION), VK_API_VERSION_MINOR(REQUIRED_VK_API_VERSION),
    VK_API_VERSION_PATCH(REQUIRED_VK_API_VERSION));

#ifdef SPS_VULKAN_DISPATCH_LOADER_DYNAMIC
  vk::DynamicLoader dl;
  PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion =
    dl.getProcAddress<PFN_vkEnumerateInstanceVersion>("vkEnumerateInstanceVersion");
  PFN_vkCreateInstance vkCreateInstance =
    dl.getProcAddress<PFN_vkCreateInstance>("vkCreateInstance");
#endif

  std::uint32_t available_api_version = 0;
  if (const auto result = vkEnumerateInstanceVersion(&available_api_version); result != VK_SUCCESS)
  {
    spdlog::error("Error: vkEnumerateInstanceVersion returned {}!", utils::as_string(result));
    return;
  }

  // This code will throw an exception if the required version of Vulkan API is not available on the
  // system
  if (VK_API_VERSION_MAJOR(REQUIRED_VK_API_VERSION) > VK_API_VERSION_MAJOR(available_api_version) ||
    (VK_API_VERSION_MAJOR(REQUIRED_VK_API_VERSION) == VK_API_VERSION_MAJOR(available_api_version) &&
      VK_API_VERSION_MINOR(REQUIRED_VK_API_VERSION) > VK_API_VERSION_MINOR(available_api_version)))
  {
    std::string exception_message = fmt::format(
      "Your system does not support the required version of Vulkan API. Required version: "
      "{}.{}.{}. Available "
      "Vulkan API version on this machine: {}.{}.{}. Please update your graphics drivers!",
      std::to_string(VK_API_VERSION_MAJOR(REQUIRED_VK_API_VERSION)),
      std::to_string(VK_API_VERSION_MINOR(REQUIRED_VK_API_VERSION)),
      std::to_string(VK_API_VERSION_PATCH(REQUIRED_VK_API_VERSION)),
      std::to_string(VK_API_VERSION_MAJOR(available_api_version)),
      std::to_string(VK_API_VERSION_MINOR(available_api_version)),
      std::to_string(VK_API_VERSION_PATCH(available_api_version)));
    throw std::runtime_error(exception_message);
  }

  const auto app_info = make_info<VkApplicationInfo>({
    .pApplicationName = application_name.c_str(),
    .applicationVersion = application_version,
    .pEngineName = engine_name.c_str(),
    .engineVersion = engine_version,
    .apiVersion = REQUIRED_VK_API_VERSION,
  });

  std::vector<const char*> instance_extension_wishlist = {
#ifdef SPS_DEBUG
    // In debug mode, we use the following instance extensions:
    // This one is for assigning internal names to Vulkan resources.
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    // This one is for setting up a Vulkan debug report callback function.
    VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
#endif
  };

  std::uint32_t glfw_extension_count = 0;

  // Because this requires some dynamic libraries to be loaded, this may take even up to some
  // seconds!
  auto* glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);

  if (glfw_extension_count == 0)
  {
    throw std::runtime_error("Error: glfwGetRequiredInstanceExtensions results 0 as number of "
                             "required instance extensions!");
  }

  spdlog::trace("Required GLFW instance extensions:");

  // Add all instance extensions which are required by GLFW to our wishlist.
  for (std::size_t i = 0; i < glfw_extension_count; i++)
  {
    spdlog::trace("   - {}", glfw_extensions[i]);              // NOLINT
    instance_extension_wishlist.push_back(glfw_extensions[i]); // NOLINT
  }

  // We have to check which instance extensions of our wishlist are available on the current system!
  // Add requested instance extensions to wishlist.
  for (const auto& requested_instance_extension : requested_instance_extensions)
  {
    instance_extension_wishlist.push_back(requested_instance_extension.c_str());
  }

  std::vector<const char*> enabled_instance_extensions{};

  spdlog::trace("List of enabled instance extensions:");

  // We are not checking for duplicated entries but this is no problem.
  for (const auto& instance_extension : instance_extension_wishlist)
  {
    if (is_extension_supported(instance_extension))
    {
      spdlog::trace("   - {} ", instance_extension);
      enabled_instance_extensions.push_back(instance_extension);
    }
    else
    {
      spdlog::error(
        "Requested instance extension {} is not available on this system!", instance_extension);
    }
  }

  std::vector<const char*> instance_layers_wishlist{};

  spdlog::trace("Instance layer wishlist:");

#ifdef SPS_DEBUG
  // RenderDoc is a very useful open source graphics debugger for Vulkan and other APIs.
  // Not using it all the time during development is fine, but as soon as something crashes
  // you should enable it, take a snapshot and look up what's wrong.
  if (enable_renderdoc_layer)
  {
    spdlog::trace("   - VK_LAYER_RENDERDOC_Capture");
    instance_layers_wishlist.push_back("VK_LAYER_RENDERDOC_Capture");
  }

  // We can't stress enough how important it is to use validation layers during development!
  // Validation layers in Vulkan are in-depth error checks for the application's use of the API.
  // They check for a multitude of possible errors. They can be disabled easily for releases.
  // Understand that in contrary to other APIs, in Vulkan API the driver provides no error checks
  // for you! If you use Vulkan API incorrectly, your application will likely just crash.
  // To avoid this, you must use validation layers during development!
  if (enable_validation_layers)
  {
    spdlog::trace("   - VK_LAYER_KHRONOS_validation");
    instance_layers_wishlist.push_back("VK_LAYER_KHRONOS_validation");
  }

#endif

  // Add requested instance layers to wishlist.
  for (const auto& instance_layer : requested_instance_layers)
  {
    instance_layers_wishlist.push_back(instance_layer.c_str());
  }

  std::vector<const char*> enabled_instance_layers{};

  spdlog::trace("List of enabled instance layers:");

  // We have to check which instance layers of our wishlist are available on the current system!
  // We are not checking for duplicated entries but this is no problem.
  for (const auto& current_layer : instance_layers_wishlist)
  {
    if (is_layer_supported(current_layer))
    {
      spdlog::trace("   - {}", current_layer);
      enabled_instance_layers.push_back(current_layer);
    }
    else
    {
#ifdef SPS_DEBUG
      if (std::string(current_layer) == VK_EXT_DEBUG_MARKER_EXTENSION_NAME)
      {
        spdlog::error("You can't use command line argument -renderdoc in release mode");
      }
#else
      spdlog::trace("Requested instance layer {} is not available on this system!", current_layer);
#endif
    }
  }

  const auto instance_ci = make_info<VkInstanceCreateInfo>({
    .pApplicationInfo = &app_info,
    // Enabled layers
    .enabledLayerCount = static_cast<std::uint32_t>(enabled_instance_layers.size()),
    .ppEnabledLayerNames = enabled_instance_layers.data(),
    // Enabled extenstions
    .enabledExtensionCount = static_cast<std::uint32_t>(enabled_instance_extensions.size()),
    .ppEnabledExtensionNames = enabled_instance_extensions.data(),
  });

  if (const auto result = vkCreateInstance(&instance_ci, nullptr, &m_instance);
      result != VK_SUCCESS)
  {
    throw VulkanException("Error: vkCreateInstance failed!", result);
  }

#ifdef ENABLE_DEBUG_MESSENGER

#ifdef SPS_VULKAN_DISPATCH_LOADER_DYNAMIC
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
    dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
#endif
  // Only works if vk::Instance use
  // vkCreateDebugUtilsMessengerEXT(instance, const
  // VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const
  // VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT*
  // pMessenger) instead

  m_dldi = vk::DispatchLoaderDynamic(m_instance, vkGetInstanceProcAddr);
  m_debugMessenger = sps::vulkan::make_debug_messenger(m_instance, m_dldi);

#endif
}

Instance::Instance(const std::string& application_name, const std::string& engine_name,
  const std::uint32_t application_version, const std::uint32_t engine_version,
  bool enable_validation_layers, bool enable_renderdoc_layer)
  : Instance(application_name, engine_name, application_version, engine_version,
      enable_validation_layers, enable_renderdoc_layer, {}, {})
{
}

Instance::Instance(Instance&& other) noexcept
{
  m_instance = std::exchange(other.m_instance, nullptr);
#ifdef ENABLE_DEBUG_MESSENGER
  m_debugMessenger = std::exchange(other.m_debugMessenger, nullptr);
#endif
}

Instance::~Instance()
{

#ifdef ENABLE_DEBUG_MESSENGER
  vk::Instance inst = static_cast<vk::Instance>(m_instance);
  inst.destroyDebugUtilsMessengerEXT(m_debugMessenger, nullptr, m_dldi);
#endif

#ifdef SPS_VULKAN_DISPATCH_LOADER_DYNAMIC
  vk::DynamicLoader dl;
  PFN_vkDestroyInstance vkDestroyInstance =
    dl.getProcAddress<PFN_vkDestroyInstance>("vkDestroyInstance");
#endif

  vkDestroyInstance(m_instance, nullptr);
}
}
