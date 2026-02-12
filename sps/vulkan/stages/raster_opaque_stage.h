#pragma once

#include <sps/vulkan/render_stage.h>

#include <string>

namespace sps::vulkan
{

class RenderGraph;
class VulkanRenderer;

/// Draws OPAQUE + MASK primitives using the opaque pipeline.
/// Also handles the legacy single-mesh fallback path (no scene graph).
///
/// Self-contained stage: owns the shared raster pipeline layout, the opaque pipeline,
/// and the blend pipeline. RasterBlendStage queries blend_pipeline() and pipeline_layout().
class RasterOpaqueStage : public RenderStage
{
public:
  RasterOpaqueStage(const VulkanRenderer& renderer,
    vk::RenderPass scene_render_pass, const RenderGraph& graph,
    const std::string& vertex_shader, const std::string& fragment_shader,
    const bool* use_rt, const bool* debug_2d);
  ~RasterOpaqueStage() override;

  RasterOpaqueStage(const RasterOpaqueStage&) = delete;
  RasterOpaqueStage& operator=(const RasterOpaqueStage&) = delete;

  void record(const FrameContext& ctx) override;
  [[nodiscard]] bool is_enabled() const override;

  /// Hot-reload shaders: destroys and recreates both pipelines + layout.
  void reload_shaders(const std::string& vertex_shader, const std::string& fragment_shader);

  /// Switch to a predefined shader mode (index into debug_constants.h tables).
  void apply_shader_mode(int mode);

  [[nodiscard]] int current_shader_mode() const { return m_current_mode; }
  [[nodiscard]] const std::string& current_vertex_shader() const { return m_vertex_shader; }
  [[nodiscard]] const std::string& current_fragment_shader() const { return m_fragment_shader; }

  /// Shared resources for RasterBlendStage.
  [[nodiscard]] vk::Pipeline blend_pipeline() const { return m_blend_pipeline; }
  [[nodiscard]] vk::PipelineLayout pipeline_layout() const { return m_pipeline_layout; }

private:
  const VulkanRenderer& m_renderer;
  vk::RenderPass m_scene_render_pass;  // non-owning
  const RenderGraph& m_graph;
  const bool* m_use_rt;
  const bool* m_debug_2d;

  vk::PipelineLayout m_pipeline_layout{ VK_NULL_HANDLE };
  vk::Pipeline m_pipeline{ VK_NULL_HANDLE };
  vk::Pipeline m_blend_pipeline{ VK_NULL_HANDLE };

  std::string m_vertex_shader;
  std::string m_fragment_shader;
  int m_current_mode{ 0 };

  void create_pipelines();
  void destroy_pipelines();
};

} // namespace sps::vulkan
