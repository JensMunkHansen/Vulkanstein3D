#include <iostream>
#include <string>

#include <GLFW/glfw3.h>

#include <vulkan/vulkan.hpp>

int main(int argc, char** argv)
{

  glfwInit();
  auto* window = glfwCreateWindow(1280, 720, "Vulkan", nullptr, nullptr);

  const vk::ApplicationInfo appInfo = vk::ApplicationInfo()
                                        .setPApplicationName("vulkan-engine")
                                        .setApplicationVersion(0)
                                        .setEngineVersion(0)
                                        .setPEngineName("vulkan-engine");

  const vk::InstanceCreateInfo instanceInfo =
    vk::InstanceCreateInfo().setPApplicationInfo(&appInfo);

  vk::Instance instance;
  vk::Result result = vk::createInstance(&instanceInfo, nullptr, &instance);
  if (result != vk::Result::eSuccess)
  {
    std::cerr << "Failed to create Vulkan instance." << std::endl;
    exit(-1);
  }

  auto extensions = vk::enumerateInstanceExtensionProperties();
  for (const auto& ext : extensions)
  {
    std::cout << ext.extensionName << std::endl;
  }

  while (!glfwWindowShouldClose(window))
  {
    glfwPollEvents();
  }
  glfwDestroyWindow(window);
  glfwTerminate();

  return EXIT_SUCCESS;
}
