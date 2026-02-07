#pragma once

#include <sps/vulkan/render_stage.h>

namespace sps::vulkan
{

/// Draws BLEND primitives sorted back-to-front using the blend pipeline.
/// Depth write is disabled; alpha blending is enabled.
class RasterBlendStage : public RenderStage
{
public:
  RasterBlendStage(const bool* use_rt, const bool* debug_2d, vk::Pipeline pipeline)
    : RenderStage("RasterBlendStage"), m_use_rt(use_rt), m_debug_2d(debug_2d),
      m_pipeline(pipeline)
  {
  }

  void record(const FrameContext& ctx) override;
  [[nodiscard]] bool is_enabled() const override;

  void set_pipeline(vk::Pipeline pipeline) { m_pipeline = pipeline; }

private:
  const bool* m_use_rt;
  const bool* m_debug_2d;
  vk::Pipeline m_pipeline;
};

} // namespace sps::vulkan
