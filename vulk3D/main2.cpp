#include <sps/vulkan/config.h>

#include <spdlog/async.h>
#include <spdlog/cfg/argv.h>
#include <spdlog/common.h>

#include <sps/vulkan/app.h>

//#include <sps/vulkan/renderer.h>
//#include <sps/vulkan/window.h>

#include <spdlog/spdlog.h>

using namespace sps::vulkan;

int main(int argc, char* argv[])
{
  spdlog::cfg::load_argv_levels(argc, argv);
  spdlog::init_thread_pool(8192, 2);

  spdlog::set_level(spdlog::level::trace);
  spdlog::info("Loading");
  auto app = Application(argc, argv);

  app.run();
  spdlog::info("Window closed");

  return 0;
}
