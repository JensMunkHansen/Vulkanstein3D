#include <cstdlib>
#include <sps/vulkan/engine.h>

using sps::vulkan::Engine;

int main(int argc, char* argv[])
{
  Engine* graphicsEngine = new Engine(argc, argv);
  
  delete graphicsEngine;
  
  return EXIT_SUCCESS;
}
