#include <sps/vulkan/stages/debug_2d_stage.h>
#include <sps/vulkan/descriptor_builder.h>

#include <algorithm>

namespace sps::vulkan
{

void Debug2DStage::record(const FrameContext& ctx)
{
  ctx.command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);

  // Use selected material descriptor if available, otherwise default
  const auto* desc = ctx.default_descriptor;
  if (ctx.material_descriptors && !ctx.material_descriptors->empty())
  {
    int idx = std::clamp(*m_material_index, 0,
      static_cast<int>(ctx.material_descriptors->size()) - 1);
    desc = (*ctx.material_descriptors)[idx].get();
  }

  ctx.command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_layout,
    0, desc->descriptor_set(), {});

  // Draw fullscreen triangle (3 vertices, no vertex buffer)
  ctx.command_buffer.draw(3, 1, 0, 0);
}

bool Debug2DStage::is_enabled() const
{
  return *m_enabled;
}

} // namespace sps::vulkan
