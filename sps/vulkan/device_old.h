#pragma once

#include <sps/vulkan/config.h>

#include <vulkan/vulkan.hpp>

namespace sps::vulkan
{
void log_device_properties(const vk::PhysicalDevice& device);

bool is_device_suitable(const vk::PhysicalDevice& device);

vk::PhysicalDevice choose_physical_device(const vk::Instance& instance);
}
