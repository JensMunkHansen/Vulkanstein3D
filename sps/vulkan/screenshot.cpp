#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <sps/vulkan/screenshot.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>

namespace sps::vulkan
{

bool save_screenshot(
  const Device& device,
  vk::CommandPool command_pool,
  vk::Image source_image,
  vk::Format format,
  vk::Extent2D extent,
  const std::string& filepath)
{
  auto dev = device.device();
  auto physical_device = device.physicalDevice();

  // Check if source format supports transfer
  vk::FormatProperties format_props = physical_device.getFormatProperties(format);
  bool supports_blit = (format_props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitSrc)
    == vk::FormatFeatureFlagBits::eBlitSrc;

  // Create destination image with linear tiling for CPU access
  vk::ImageCreateInfo image_info{};
  image_info.imageType = vk::ImageType::e2D;
  image_info.format = vk::Format::eR8G8B8A8Unorm;
  image_info.extent.width = extent.width;
  image_info.extent.height = extent.height;
  image_info.extent.depth = 1;
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = vk::SampleCountFlagBits::e1;
  image_info.tiling = vk::ImageTiling::eLinear;
  image_info.usage = vk::ImageUsageFlagBits::eTransferDst;
  image_info.initialLayout = vk::ImageLayout::eUndefined;

  vk::Image dst_image = dev.createImage(image_info);

  // Allocate memory for destination image
  vk::MemoryRequirements mem_reqs = dev.getImageMemoryRequirements(dst_image);

  vk::MemoryAllocateInfo alloc_info{};
  alloc_info.allocationSize = mem_reqs.size;
  alloc_info.memoryTypeIndex = device.find_memory_type(
    mem_reqs.memoryTypeBits,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

  vk::DeviceMemory dst_memory = dev.allocateMemory(alloc_info);
  dev.bindImageMemory(dst_image, dst_memory, 0);

  // Create command buffer for copy operation
  vk::CommandBufferAllocateInfo cmd_alloc_info{};
  cmd_alloc_info.level = vk::CommandBufferLevel::ePrimary;
  cmd_alloc_info.commandPool = command_pool;
  cmd_alloc_info.commandBufferCount = 1;

  vk::CommandBuffer cmd_buffer = dev.allocateCommandBuffers(cmd_alloc_info)[0];

  vk::CommandBufferBeginInfo begin_info{};
  begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
  cmd_buffer.begin(begin_info);

  // Transition destination image to transfer dst
  vk::ImageMemoryBarrier barrier{};
  barrier.oldLayout = vk::ImageLayout::eUndefined;
  barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = dst_image;
  barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = {};
  barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

  cmd_buffer.pipelineBarrier(
    vk::PipelineStageFlagBits::eTransfer,
    vk::PipelineStageFlagBits::eTransfer,
    {}, {}, {}, barrier);

  // Transition source image to transfer src
  barrier.image = source_image;
  barrier.oldLayout = vk::ImageLayout::ePresentSrcKHR;
  barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
  barrier.srcAccessMask = vk::AccessFlagBits::eMemoryRead;
  barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

  cmd_buffer.pipelineBarrier(
    vk::PipelineStageFlagBits::eTransfer,
    vk::PipelineStageFlagBits::eTransfer,
    {}, {}, {}, barrier);

  // Copy or blit the image
  if (supports_blit && format != vk::Format::eR8G8B8A8Unorm)
  {
    // Use blit for format conversion
    vk::ImageBlit blit_region{};
    blit_region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit_region.srcSubresource.layerCount = 1;
    blit_region.srcOffsets[0] = vk::Offset3D{ 0, 0, 0 };
    blit_region.srcOffsets[1] = vk::Offset3D{
      static_cast<int32_t>(extent.width),
      static_cast<int32_t>(extent.height), 1 };
    blit_region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit_region.dstSubresource.layerCount = 1;
    blit_region.dstOffsets[0] = vk::Offset3D{ 0, 0, 0 };
    blit_region.dstOffsets[1] = vk::Offset3D{
      static_cast<int32_t>(extent.width),
      static_cast<int32_t>(extent.height), 1 };

    cmd_buffer.blitImage(
      source_image, vk::ImageLayout::eTransferSrcOptimal,
      dst_image, vk::ImageLayout::eTransferDstOptimal,
      blit_region, vk::Filter::eNearest);
  }
  else
  {
    // Use copy (same format or blit not supported)
    vk::ImageCopy copy_region{};
    copy_region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    copy_region.srcSubresource.layerCount = 1;
    copy_region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    copy_region.dstSubresource.layerCount = 1;
    copy_region.extent.width = extent.width;
    copy_region.extent.height = extent.height;
    copy_region.extent.depth = 1;

    cmd_buffer.copyImage(
      source_image, vk::ImageLayout::eTransferSrcOptimal,
      dst_image, vk::ImageLayout::eTransferDstOptimal,
      copy_region);
  }

  // Transition destination to general for reading
  barrier.image = dst_image;
  barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
  barrier.newLayout = vk::ImageLayout::eGeneral;
  barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
  barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead;

  cmd_buffer.pipelineBarrier(
    vk::PipelineStageFlagBits::eTransfer,
    vk::PipelineStageFlagBits::eTransfer,
    {}, {}, {}, barrier);

  // Transition source back to present
  barrier.image = source_image;
  barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
  barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
  barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
  barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead;

  cmd_buffer.pipelineBarrier(
    vk::PipelineStageFlagBits::eTransfer,
    vk::PipelineStageFlagBits::eTransfer,
    {}, {}, {}, barrier);

  cmd_buffer.end();

  // Submit and wait
  vk::SubmitInfo submit_info{};
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &cmd_buffer;

  vk::Fence fence = dev.createFence({});
  device.graphics_queue().submit(submit_info, fence);
  (void)dev.waitForFences(fence, VK_TRUE, UINT64_MAX);

  dev.destroyFence(fence);
  dev.freeCommandBuffers(command_pool, cmd_buffer);

  // Map memory and read pixels
  vk::ImageSubresource subresource{};
  subresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  vk::SubresourceLayout layout = dev.getImageSubresourceLayout(dst_image, subresource);

  const char* data = static_cast<const char*>(dev.mapMemory(dst_memory, 0, VK_WHOLE_SIZE));
  data += layout.offset;

  // Copy to contiguous buffer, handling row pitch
  std::vector<uint8_t> pixels(extent.width * extent.height * 4);
  for (uint32_t y = 0; y < extent.height; ++y)
  {
    const uint8_t* row = reinterpret_cast<const uint8_t*>(data + y * layout.rowPitch);
    for (uint32_t x = 0; x < extent.width; ++x)
    {
      // Handle BGRA -> RGBA swizzle if needed
      size_t dst_idx = (y * extent.width + x) * 4;
      if (format == vk::Format::eB8G8R8A8Unorm || format == vk::Format::eB8G8R8A8Srgb)
      {
        pixels[dst_idx + 0] = row[x * 4 + 2];  // R <- B
        pixels[dst_idx + 1] = row[x * 4 + 1];  // G <- G
        pixels[dst_idx + 2] = row[x * 4 + 0];  // B <- R
        pixels[dst_idx + 3] = row[x * 4 + 3];  // A <- A
      }
      else
      {
        pixels[dst_idx + 0] = row[x * 4 + 0];
        pixels[dst_idx + 1] = row[x * 4 + 1];
        pixels[dst_idx + 2] = row[x * 4 + 2];
        pixels[dst_idx + 3] = row[x * 4 + 3];
      }
    }
  }

  dev.unmapMemory(dst_memory);

  // Cleanup
  dev.destroyImage(dst_image);
  dev.freeMemory(dst_memory);

  // Save to file
  bool success = false;
  std::string ext = filepath.substr(filepath.find_last_of('.'));

  if (ext == ".png")
  {
    success = stbi_write_png(filepath.c_str(), extent.width, extent.height, 4,
      pixels.data(), extent.width * 4) != 0;
  }
  else if (ext == ".jpg" || ext == ".jpeg")
  {
    success = stbi_write_jpg(filepath.c_str(), extent.width, extent.height, 4,
      pixels.data(), 90) != 0;
  }
  else if (ext == ".bmp")
  {
    success = stbi_write_bmp(filepath.c_str(), extent.width, extent.height, 4,
      pixels.data()) != 0;
  }
  else
  {
    spdlog::error("Unsupported screenshot format: {}", ext);
    return false;
  }

  if (success)
  {
    spdlog::info("Screenshot saved: {} ({}x{})", filepath, extent.width, extent.height);
  }
  else
  {
    spdlog::error("Failed to save screenshot: {}", filepath);
  }

  return success;
}

std::string generate_screenshot_filename(const std::string& prefix, const std::string& extension)
{
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  auto tm = *std::localtime(&time);

  std::ostringstream oss;
  oss << prefix << "_" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << extension;
  return oss.str();
}

} // namespace sps::vulkan
