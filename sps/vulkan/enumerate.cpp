#include <sps/vulkan/enumerate.h>

#include <cassert>
namespace sps::vulkan
{
std::vector<vk::PhysicalDevice> get_physical_devices(const vk::Instance& inst)
{
  assert(inst);
  try
  {
    const auto availableDevices = inst.enumeratePhysicalDevices();
  }
  catch (...)
  {
  }
}
}
