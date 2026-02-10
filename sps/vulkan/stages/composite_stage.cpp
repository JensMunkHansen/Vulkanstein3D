#include <sps/vulkan/stages/composite_stage.h>

namespace sps::vulkan
{

void CompositeStage::record(const FrameContext& ctx)
{
  ctx.command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);

  ctx.command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_layout,
    0, m_descriptor_set, {});

  // Push exposure + tonemap mode
  struct CompositePushConstants
  {
    float exposure;
    int tonemapMode;
  } pc{};
  pc.exposure = *m_exposure;
  pc.tonemapMode = *m_tonemap_mode;

  ctx.command_buffer.pushConstants(m_layout,
    vk::ShaderStageFlagBits::eFragment, 0,
    static_cast<uint32_t>(sizeof(pc)), &pc);

  // Draw fullscreen triangle (3 vertices, no vertex buffer)
  ctx.command_buffer.draw(3, 1, 0, 0);
}

} // namespace sps::vulkan
