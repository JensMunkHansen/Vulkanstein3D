#pragma once

#include <sps/vulkan/render_stage.h>

namespace sps::vulkan
{

/// Fullscreen composite pass: samples the HDR buffer, applies exposure + tone mapping + gamma,
/// and writes to the swapchain.
class CompositeStage : public RenderStage
{
public:
  CompositeStage(vk::Pipeline pipeline, vk::PipelineLayout layout,
    vk::DescriptorSet descriptor_set,
    const float* exposure, const int* tonemap_mode)
    : RenderStage("CompositeStage"), m_pipeline(pipeline), m_layout(layout),
      m_descriptor_set(descriptor_set),
      m_exposure(exposure), m_tonemap_mode(tonemap_mode)
  {
  }

  void record(const FrameContext& ctx) override;
  [[nodiscard]] Phase phase() const override { return Phase::CompositePass; }

  void set_pipeline(vk::Pipeline pipeline) { m_pipeline = pipeline; }
  void set_descriptor_set(vk::DescriptorSet ds) { m_descriptor_set = ds; }

private:
  vk::Pipeline m_pipeline;
  vk::PipelineLayout m_layout;
  vk::DescriptorSet m_descriptor_set;
  const float* m_exposure;
  const int* m_tonemap_mode;
};

} // namespace sps::vulkan
