#include <sps/vulkan/config.h>

#include <spdlog/cfg/argv.h>
#include <spdlog/common.h>
#include <sps/vulkan/app.h>
#include <sps/vulkan/renderer.h>
#include <sps/vulkan/window.h>

#include <spdlog/spdlog.h>

using namespace sps::vulkan;

int main(int argc, char* argv[])
{
  spdlog::cfg::load_argv_levels(argc, argv);

  spdlog::set_level(spdlog::level::trace);
  spdlog::info("Loading");
  //  auto window = Window("Application", 640, 480, true, false, Window::Mode::WINDOWED);
  //  auto renderer = VulkanRenderer();
  auto app = Application(argc, argv);
  return 0;
}
