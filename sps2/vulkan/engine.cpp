#include <sps/vulkan/config.h>

#include <sps/vulkan/engine.h>

#include <iostream>
#include <fmt/format.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <spdlog/cfg/argv.h>

namespace sps::vulkan {
  
Engine::Engine(int argc, char* argv[])
{
  spdlog::cfg::load_argv_levels(argc, argv);

  spdlog::info("Making a graphics engine");
  build_glfw_window();
}

void Engine::build_glfw_window()
{

  // initialize glfw
  glfwInit();

  // no default rendering client, we'll hook vulkan up
  // to the window later
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  // resizing breaks the swapchain, we'll disable it for now
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  // GLFWwindow* glfwCreateWindow (int width, int height, const char *title, GLFWmonitor *monitor,
  // GLFWwindow *share)
  if ((window = glfwCreateWindow(width, height, "ID Tech 12", nullptr, nullptr)))
  {
    spdlog::info(fmt::format(fmt::runtime("Successfully made a glfw window called \"ID Tech 12\", width: {}, height: {}"), width, height));
  }
  else
  {
    spdlog::debug("GLFW window creation failed\n");
  }
}

Engine::~Engine()
{
  spdlog::info("Goodbye see you!");
  // terminate glfw
  glfwTerminate();
}
}  // namespace sps::vulkan
