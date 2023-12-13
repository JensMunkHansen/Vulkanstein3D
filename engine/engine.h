#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>
/*
* including the prebuilt header from the lunarg sdk will load
* most functions, but not all.
*
* Functions can also be dynamically loaded, using the call
*
* PFN_vkVoidFunction vkGetInstanceProcAddr(
    VkInstance                                  instance,
    const char*                                 pName);

 or

 PFN_vkVoidFunction vkGetDeviceProcAddr(
        VkDevice                                    device,
        const char*                                 pName);

        We will look at this later, once we've created an instance and device.
*/
namespace sps::vulkan
{
class Engine
{
public:
  Engine(int argc, char* argv[]);

  ~Engine();

private:
  // glfw window parameters
  int width{ 640 };
  int height{ 480 };
  GLFWwindow* window{ nullptr };

  // glfw setup
  void build_glfw_window();
};
} // namespace sps::vulkan
