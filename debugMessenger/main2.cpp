#include <iostream>
#include <string>

#include <GLFW/glfw3.h>

#include "instance.h"

int main(int argc, char** argv)
{

  glfwInit();
  auto* window = glfwCreateWindow(1280, 720, "Vulkan", nullptr, nullptr);

  engine::core::Instance instance;

  while (!glfwWindowShouldClose(window))
  {
    glfwPollEvents();
  }
  glfwDestroyWindow(window);
  glfwTerminate();

  return EXIT_SUCCESS;
}
