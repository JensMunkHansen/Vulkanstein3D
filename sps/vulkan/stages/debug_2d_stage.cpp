#include <sps/vulkan/stages/debug_2d_stage.h>

#include <spdlog/spdlog.h>
#include <sps/vulkan/config.h>
#include <sps/vulkan/descriptor_builder.h>
#include <sps/vulkan/pipeline.h>
#include <sps/vulkan/render_graph.h>
#include <sps/vulkan/renderer.h>

#include <algorithm>

namespace sps::vulkan
{

Debug2DStage::Debug2DStage(const VulkanRenderer& renderer,
  vk::RenderPass composite_render_pass, const RenderGraph& graph,
  const bool* enabled, const int* material_index)
  : RenderStage("Debug2DStage")
  , m_renderer(renderer)
  , m_graph(graph)
  , m_enabled(enabled)
  , m_material_index(material_index)
{
  create_pipeline(composite_render_pass, graph.material_descriptor_layout());
  spdlog::info("Created 2D debug stage (self-contained)");
}

Debug2DStage::~Debug2DStage()
{
  auto dev = m_renderer.device().device();
  if (m_pipeline)
    dev.destroyPipeline(m_pipeline);
  if (m_pipeline_layout)
    dev.destroyPipelineLayout(m_pipeline_layout);
}

void Debug2DStage::create_pipeline(vk::RenderPass composite_render_pass,
  vk::DescriptorSetLayout material_layout)
{
  GraphicsPipelineInBundle specification{};
  specification.device = m_renderer.device().device();
  specification.vertexFilepath = SHADER_DIR "fullscreen_quad.spv";
  specification.fragmentFilepath = SHADER_DIR "debug_texture2d.spv";
  specification.swapchainExtent = m_renderer.swapchain().extent();
  specification.swapchainImageFormat = m_renderer.swapchain().image_format();
  specification.descriptorSetLayout = material_layout;

  // No vertex input — fullscreen quad generates vertices in shader
  specification.backfaceCulling = false;

  // Composite pass: swapchain target, no depth, no MSAA
  specification.existingRenderPass = composite_render_pass;
  specification.depthTestEnabled = false;
  specification.msaaSamples = vk::SampleCountFlagBits::e1;

  auto output = create_graphics_pipeline(specification, true);
  m_pipeline_layout = output.layout;
  m_pipeline = output.pipeline;
}

void Debug2DStage::record(const FrameContext& ctx)
{
  ctx.command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);

  // Use selected material descriptor if available, otherwise default
  const auto& mat_descs = m_graph.material_descriptors();
  const auto* desc = m_graph.default_descriptor();
  if (!mat_descs.empty())
  {
    int idx = std::clamp(*m_material_index, 0,
      static_cast<int>(mat_descs.size()) - 1);
    desc = mat_descs[idx].get();
  }

  ctx.command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipeline_layout,
    0, desc->descriptor_set(), {});

  // Draw fullscreen triangle (3 vertices, no vertex buffer)
  ctx.command_buffer.draw(3, 1, 0, 0);
}

bool Debug2DStage::is_enabled() const
{
  return *m_enabled;
}

} // namespace sps::vulkan
