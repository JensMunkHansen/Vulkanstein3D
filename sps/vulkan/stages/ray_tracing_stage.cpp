#include <sps/vulkan/stages/ray_tracing_stage.h>
#include <sps/vulkan/device.h>
#include <sps/vulkan/raytracing_pipeline.h>
#include <sps/vulkan/swapchain.h>

namespace sps::vulkan
{

void RayTracingStage::record(const FrameContext& ctx)
{
  vk::Extent2D extent = ctx.extent;

  // Transition RT image to general layout for writing
  vk::ImageMemoryBarrier barrier{};
  barrier.oldLayout = vk::ImageLayout::eUndefined;
  barrier.newLayout = vk::ImageLayout::eGeneral;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = *m_rt_image;
  barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = {};
  barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;

  ctx.command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
    vk::PipelineStageFlagBits::eRayTracingShaderKHR, {}, {}, {}, barrier);

  // Bind RT pipeline and descriptor set
  ctx.command_buffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, m_rt_pipeline->pipeline());
  ctx.command_buffer.bindDescriptorSets(
    vk::PipelineBindPoint::eRayTracingKHR, m_rt_pipeline->layout(), 0, *m_rt_descriptor_set, {});

  // Trace rays
  m_rt_pipeline->trace_rays(ctx.command_buffer, extent.width, extent.height);

  // Transition RT image to transfer src for blit
  barrier.oldLayout = vk::ImageLayout::eGeneral;
  barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
  barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
  barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

  ctx.command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eRayTracingShaderKHR,
    vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier);

  // Transition swapchain image to transfer dst
  vk::ImageMemoryBarrier swapBarrier{};
  swapBarrier.oldLayout = vk::ImageLayout::eUndefined;
  swapBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
  swapBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  swapBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  swapBarrier.image = ctx.swapchain->images()[ctx.image_index];
  swapBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  swapBarrier.subresourceRange.baseMipLevel = 0;
  swapBarrier.subresourceRange.levelCount = 1;
  swapBarrier.subresourceRange.baseArrayLayer = 0;
  swapBarrier.subresourceRange.layerCount = 1;
  swapBarrier.srcAccessMask = {};
  swapBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

  ctx.command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
    vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, swapBarrier);

  // Blit RT image to swapchain image
  vk::ImageBlit blitRegion{};
  blitRegion.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  blitRegion.srcSubresource.layerCount = 1;
  blitRegion.srcOffsets[0] = vk::Offset3D{ 0, 0, 0 };
  blitRegion.srcOffsets[1] =
    vk::Offset3D{ static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1 };
  blitRegion.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  blitRegion.dstSubresource.layerCount = 1;
  blitRegion.dstOffsets[0] = vk::Offset3D{ 0, 0, 0 };
  blitRegion.dstOffsets[1] =
    vk::Offset3D{ static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1 };

  ctx.command_buffer.blitImage(*m_rt_image, vk::ImageLayout::eTransferSrcOptimal,
    ctx.swapchain->images()[ctx.image_index], vk::ImageLayout::eTransferDstOptimal, blitRegion,
    vk::Filter::eNearest);

  // Transition swapchain image to present
  swapBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
  swapBarrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
  swapBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
  swapBarrier.dstAccessMask = {};

  ctx.command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
    vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {}, swapBarrier);
}

bool RayTracingStage::is_enabled() const
{
  return *m_use_rt && m_device->supports_ray_tracing() && m_rt_pipeline != nullptr;
}

} // namespace sps::vulkan
