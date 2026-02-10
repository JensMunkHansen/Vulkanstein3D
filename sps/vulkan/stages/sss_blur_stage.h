#pragma once

#include <sps/vulkan/render_stage.h>

namespace sps::vulkan
{

/// Screen-space subsurface scattering blur stage.
/// Runs as an Intermediate stage between the scene and composite passes.
/// Applies a separable (horizontal + vertical) blur to the HDR image.
class SSSBlurStage : public RenderStage
{
public:
  SSSBlurStage(const bool* enabled, const float* blur_width,
    vk::Pipeline pipeline, vk::PipelineLayout layout,
    vk::DescriptorSet h_descriptor, vk::DescriptorSet v_descriptor,
    vk::Image* hdr_image, vk::Extent2D* extent)
    : RenderStage("SSSBlurStage"), m_enabled(enabled), m_blur_width(blur_width),
      m_pipeline(pipeline), m_layout(layout),
      m_h_descriptor(h_descriptor), m_v_descriptor(v_descriptor),
      m_hdr_image(hdr_image), m_extent(extent)
  {
  }

  void record(const FrameContext& ctx) override;
  [[nodiscard]] bool is_enabled() const override { return *m_enabled; }
  [[nodiscard]] Phase phase() const override { return Phase::Intermediate; }

  void set_descriptors(vk::DescriptorSet h, vk::DescriptorSet v)
  {
    m_h_descriptor = h;
    m_v_descriptor = v;
  }

private:
  const bool* m_enabled;
  const float* m_blur_width;
  vk::Pipeline m_pipeline;
  vk::PipelineLayout m_layout;
  vk::DescriptorSet m_h_descriptor;  // HDR→ping (horizontal)
  vk::DescriptorSet m_v_descriptor;  // ping→HDR (vertical)
  vk::Image* m_hdr_image;
  vk::Extent2D* m_extent;
};

} // namespace sps::vulkan
