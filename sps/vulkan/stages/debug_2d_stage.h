#pragma once

#include <sps/vulkan/render_stage.h>

namespace sps::vulkan
{

class ResourceDescriptor;
class VulkanRenderer;

/// Fullscreen texture viewer for 2D debug mode.
///
/// Self-contained stage: owns its graphics pipeline and pipeline layout.
/// Uses the composite render pass (swapchain target, no depth, no MSAA).
/// Draws a fullscreen triangle using fullscreen_quad.vert + debug_texture2d.frag.
class Debug2DStage : public RenderStage
{
public:
  Debug2DStage(const VulkanRenderer& renderer, vk::RenderPass composite_render_pass,
    vk::DescriptorSetLayout material_layout,
    const bool* enabled, const int* material_index);
  ~Debug2DStage() override;

  Debug2DStage(const Debug2DStage&) = delete;
  Debug2DStage& operator=(const Debug2DStage&) = delete;

  void record(const FrameContext& ctx) override;
  [[nodiscard]] bool is_enabled() const override;
  [[nodiscard]] Phase phase() const override { return Phase::CompositePass; }

private:
  const VulkanRenderer& m_renderer;
  const bool* m_enabled;
  const int* m_material_index;

  vk::PipelineLayout m_pipeline_layout{ VK_NULL_HANDLE };
  vk::Pipeline m_pipeline{ VK_NULL_HANDLE };

  void create_pipeline(vk::RenderPass composite_render_pass,
    vk::DescriptorSetLayout material_layout);
};

} // namespace sps::vulkan
