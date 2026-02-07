#pragma once

#include <sps/vulkan/render_stage.h>

namespace sps::vulkan
{

class ResourceDescriptor;

/// Fullscreen texture viewer for 2D debug mode.
/// Draws a fullscreen triangle using the debug 2D pipeline.
class Debug2DStage : public RenderStage
{
public:
  Debug2DStage(const bool* enabled, const int* material_index,
    vk::Pipeline pipeline, vk::PipelineLayout layout)
    : RenderStage("Debug2DStage"), m_enabled(enabled), m_material_index(material_index),
      m_pipeline(pipeline), m_layout(layout)
  {
  }

  void record(const FrameContext& ctx) override;
  [[nodiscard]] bool is_enabled() const override;

  void set_pipeline(vk::Pipeline pipeline, vk::PipelineLayout layout)
  {
    m_pipeline = pipeline;
    m_layout = layout;
  }

private:
  const bool* m_enabled;
  const int* m_material_index;
  vk::Pipeline m_pipeline;
  vk::PipelineLayout m_layout;
};

} // namespace sps::vulkan
