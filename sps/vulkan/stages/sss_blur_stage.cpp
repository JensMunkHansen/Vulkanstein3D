#include <sps/vulkan/stages/sss_blur_stage.h>

namespace sps::vulkan
{

void SSSBlurStage::record(const FrameContext& ctx)
{
  auto cmd = ctx.command_buffer;
  uint32_t w = m_extent->width;
  uint32_t h = m_extent->height;

  // Transition HDR from ShaderReadOnlyOptimal to General for compute read/write
  {
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = *m_hdr_image;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

    cmd.pipelineBarrier(
      vk::PipelineStageFlagBits::eColorAttachmentOutput,
      vk::PipelineStageFlagBits::eComputeShader,
      {}, {}, {}, barrier);
  }

  uint32_t groupsX = (w + 15) / 16;
  uint32_t groupsY = (h + 15) / 16;

  struct BlurPushConstants
  {
    float blurWidth;
    int direction;
  } pc{};
  pc.blurWidth = *m_blur_width;

  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_pipeline);

  // Pass 1: Horizontal (HDR → ping)
  pc.direction = 0;
  cmd.pushConstants(m_layout, vk::ShaderStageFlagBits::eCompute, 0,
    static_cast<uint32_t>(sizeof(pc)), &pc);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_layout, 0, m_h_descriptor, {});
  cmd.dispatch(groupsX, groupsY, 1);

  // Memory barrier between H and V passes
  {
    vk::MemoryBarrier memBarrier{};
    memBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    memBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    cmd.pipelineBarrier(
      vk::PipelineStageFlagBits::eComputeShader,
      vk::PipelineStageFlagBits::eComputeShader,
      {}, memBarrier, {}, {});
  }

  // Pass 2: Vertical (ping → HDR)
  pc.direction = 1;
  cmd.pushConstants(m_layout, vk::ShaderStageFlagBits::eCompute, 0,
    static_cast<uint32_t>(sizeof(pc)), &pc);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_layout, 0, m_v_descriptor, {});
  cmd.dispatch(groupsX, groupsY, 1);

  // Transition HDR back to ShaderReadOnlyOptimal for composite sampling
  {
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eGeneral;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = *m_hdr_image;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cmd.pipelineBarrier(
      vk::PipelineStageFlagBits::eComputeShader,
      vk::PipelineStageFlagBits::eFragmentShader,
      {}, {}, {}, barrier);
  }
}

} // namespace sps::vulkan
