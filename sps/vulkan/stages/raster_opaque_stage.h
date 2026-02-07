#pragma once

#include <sps/vulkan/render_stage.h>

namespace sps::vulkan
{

/// Draws OPAQUE + MASK primitives using the opaque pipeline.
/// Also handles the legacy single-mesh fallback path (no scene graph).
class RasterOpaqueStage : public RenderStage
{
public:
  RasterOpaqueStage(const bool* use_rt, const bool* debug_2d, vk::Pipeline pipeline)
    : RenderStage("RasterOpaqueStage"), m_use_rt(use_rt), m_debug_2d(debug_2d),
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
