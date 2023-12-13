#include <sps/vulkan/device.h>

namespace sps::vulkan
{
Device::~Device()
{
  vkDestroyDevice(m_device, nullptr);
}
Device::Device(Device&& other) noexcept
{
  m_device = std::exchange(other.m_device, nullptr);
  m_physical_device = std::exchange(other.m_physical_device, nullptr);
}

}
