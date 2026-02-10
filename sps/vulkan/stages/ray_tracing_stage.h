#pragma once

#include <sps/vulkan/render_stage.h>

namespace sps::vulkan
{

class Device;
class RayTracingPipeline;
class Swapchain;

/// Ray tracing stage: traces rays into a storage image and blits to the swapchain.
///
/// This stage does NOT use the shared render pass — it manages its own
/// image barriers and executes before the rasterization render pass.
///
/// Owns no Vulkan resources directly. All RT resources (image, descriptor set,
/// pipeline) are owned by Application and passed via constructor/FrameContext.
class RayTracingStage : public RenderStage
{
public:
  RayTracingStage(const bool* use_rt, const Device* device,
    RayTracingPipeline* rt_pipeline,
    vk::Image* rt_image,
    vk::DescriptorSet* rt_descriptor_set)
    : RenderStage("RayTracingStage"), m_use_rt(use_rt), m_device(device),
      m_rt_pipeline(rt_pipeline), m_rt_image(rt_image),
      m_rt_descriptor_set(rt_descriptor_set)
  {
  }

  void record(const FrameContext& ctx) override;
  [[nodiscard]] bool is_enabled() const override;
  [[nodiscard]] Phase phase() const override { return Phase::PrePass; }

private:
  const bool* m_use_rt;
  const Device* m_device;
  RayTracingPipeline* m_rt_pipeline;
  vk::Image* m_rt_image;
  vk::DescriptorSet* m_rt_descriptor_set;
};

} // namespace sps::vulkan
