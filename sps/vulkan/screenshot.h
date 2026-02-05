#pragma once

#include <sps/vulkan/device.h>
#include <vulkan/vulkan.hpp>
#include <string>

namespace sps::vulkan
{

/// @brief Save a screenshot from a Vulkan image to a file.
/// @param device The Vulkan device wrapper.
/// @param command_pool Command pool for creating temporary command buffer.
/// @param source_image The image to capture (typically swapchain image).
/// @param format The format of the source image.
/// @param extent The dimensions of the image.
/// @param filepath Output file path (supports .png, .jpg, .bmp).
/// @return true on success, false on failure.
/// @see Uses stb_image_write for encoding.
bool save_screenshot(
  const Device& device,
  vk::CommandPool command_pool,
  vk::Image source_image,
  vk::Format format,
  vk::Extent2D extent,
  const std::string& filepath);

/// @brief Generate a timestamped screenshot filename.
/// @param prefix Optional prefix (e.g., "debug_normals").
/// @param extension File extension (default ".png").
/// @return Filename like "screenshot_2024-01-15_14-30-45.png"
std::string generate_screenshot_filename(
  const std::string& prefix = "screenshot",
  const std::string& extension = ".png");

} // namespace sps::vulkan
