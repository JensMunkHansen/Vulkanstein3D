#pragma once

#include <sps/vulkan/render_stage.h>

namespace sps::vulkan
{

class RasterOpaqueStage;
class RenderGraph;

/// Draws BLEND primitives sorted back-to-front using the blend pipeline.
/// Depth write is disabled; alpha blending is enabled.
///
/// Queries pipeline and layout from RasterOpaqueStage each frame — no stale handles
/// even after shader hot-reload.
class RasterBlendStage : public RenderStage
{
public:
  RasterBlendStage(const RasterOpaqueStage& opaque_stage, const RenderGraph& graph,
    const bool* use_rt, const bool* debug_2d)
    : RenderStage("RasterBlendStage")
    , m_opaque(opaque_stage)
    , m_graph(graph)
    , m_use_rt(use_rt)
    , m_debug_2d(debug_2d)
  {
  }

  void record(const FrameContext& ctx) override;
  [[nodiscard]] bool is_enabled() const override;

private:
  const RasterOpaqueStage& m_opaque;
  const RenderGraph& m_graph;
  const bool* m_use_rt;
  const bool* m_debug_2d;
};

} // namespace sps::vulkan
