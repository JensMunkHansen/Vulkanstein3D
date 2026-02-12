#include <sps/vulkan/stages/raster_opaque_stage.h>

#include <spdlog/spdlog.h>
#include <sps/vulkan/debug_constants.h>
#include <sps/vulkan/gltf_loader.h>
#include <sps/vulkan/mesh.h>
#include <sps/vulkan/pipeline.h>
#include <sps/vulkan/render_graph.h>
#include <sps/vulkan/renderer.h>
#include <sps/vulkan/vertex.h>

#include <glm/glm.hpp>

namespace sps::vulkan
{

RasterOpaqueStage::RasterOpaqueStage(const VulkanRenderer& renderer,
  vk::RenderPass scene_render_pass, const RenderGraph& graph,
  const std::string& vertex_shader, const std::string& fragment_shader,
  const bool* use_rt, const bool* debug_2d)
  : RenderStage("RasterOpaqueStage")
  , m_renderer(renderer)
  , m_scene_render_pass(scene_render_pass)
  , m_graph(graph)
  , m_use_rt(use_rt)
  , m_debug_2d(debug_2d)
  , m_vertex_shader(vertex_shader)
  , m_fragment_shader(fragment_shader)
{
  create_pipelines();
  spdlog::info("Created raster opaque stage (self-contained)");
}

RasterOpaqueStage::~RasterOpaqueStage()
{
  destroy_pipelines();
}

void RasterOpaqueStage::create_pipelines()
{
  sps::vulkan::GraphicsPipelineInBundle specification{};
  specification.device = m_renderer.device().device();
  specification.vertexFilepath = m_vertex_shader;
  specification.fragmentFilepath = m_fragment_shader;
  specification.swapchainExtent = m_renderer.swapchain().extent();
  specification.swapchainImageFormat = m_renderer.hdr_format();
  specification.descriptorSetLayout = m_graph.material_descriptor_layout();

  auto binding = Vertex::binding_description();
  auto attributes = Vertex::attribute_descriptions();
  specification.vertexBindings = { binding };
  specification.vertexAttributes = { attributes.begin(), attributes.end() };

  specification.backfaceCulling = true;
  specification.dynamicCullMode = true;
  specification.depthTestEnabled = true;
  specification.depthFormat = m_renderer.depth_format();
  specification.msaaSamples = m_renderer.msaa_samples();
  specification.existingRenderPass = m_scene_render_pass;

  vk::PushConstantRange pcRange{
    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, 128
  };
  specification.pushConstantRanges = { pcRange };

  // Pipeline 1: opaque (no blend, depth write on, stencil write for SSS masking)
  specification.blendEnabled = false;
  specification.depthWriteEnabled = true;
  specification.stencilWriteEnabled = true;

  auto output = sps::vulkan::create_graphics_pipeline(specification, true);
  m_pipeline_layout = output.layout;
  m_pipeline = output.pipeline;

  // Pipeline 2: blend (alpha blend on, depth write off, stencil disabled)
  specification.blendEnabled = true;
  specification.depthWriteEnabled = false;
  specification.stencilWriteEnabled = false;
  specification.existingPipelineLayout = m_pipeline_layout;

  auto blendOutput = sps::vulkan::create_graphics_pipeline(specification, true);
  m_blend_pipeline = blendOutput.pipeline;
}

void RasterOpaqueStage::destroy_pipelines()
{
  auto dev = m_renderer.device().device();

  if (m_blend_pipeline)
  {
    dev.destroyPipeline(m_blend_pipeline);
    m_blend_pipeline = VK_NULL_HANDLE;
  }
  if (m_pipeline)
  {
    dev.destroyPipeline(m_pipeline);
    m_pipeline = VK_NULL_HANDLE;
  }
  if (m_pipeline_layout)
  {
    dev.destroyPipelineLayout(m_pipeline_layout);
    m_pipeline_layout = VK_NULL_HANDLE;
  }
}

void RasterOpaqueStage::reload_shaders(
  const std::string& vertex_shader, const std::string& fragment_shader)
{
  destroy_pipelines();
  m_vertex_shader = vertex_shader;
  m_fragment_shader = fragment_shader;
  create_pipelines();
  spdlog::info("Reloaded raster shaders: {} + {}", vertex_shader, fragment_shader);
}

void RasterOpaqueStage::apply_shader_mode(int mode)
{
  if (mode >= 0 && mode < debug::SHADER_3D_COUNT)
  {
    m_current_mode = mode;
    reload_shaders(debug::vertex_shaders[mode], debug::fragment_shaders[mode]);
  }
}

void RasterOpaqueStage::record(const FrameContext& ctx)
{
  // Push constant struct matching shader layout (128 bytes)
  struct PushConstants
  {
    glm::mat4 model;
    glm::vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float alphaCutoff;
    uint32_t alphaMode;
    float iridescenceFactor;
    float iridescenceIor;
    float iridescenceThicknessMin;
    float iridescenceThicknessMax;
    float transmissionFactor;
    float thicknessFactor;
    uint32_t attenuationColorPacked;
    float attenuationDistance;
  } pc{};

  if (!ctx.mesh)
    return;

  ctx.mesh->bind(ctx.command_buffer);

  if (ctx.scene && !ctx.scene->primitives.empty() && m_graph.material_set_count() > 0)
  {
    // Multi-material scene: draw OPAQUE + MASK primitives
    ctx.command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);

    for (const auto& prim : ctx.scene->primitives)
    {
      const auto& mat = ctx.scene->materials[prim.materialIndex];

      if (mat.alphaMode == AlphaMode::Blend)
        continue; // Skip blend primitives — handled by RasterBlendStage

      // Per-material back-face culling: cull back faces unless material is double-sided
      ctx.command_buffer.setCullModeEXT(
        mat.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);

      // Write stencil=1 for SSS materials, stencil=0 for others
      ctx.command_buffer.setStencilReference(
        vk::StencilFaceFlagBits::eFrontAndBack,
        mat.transmissionFactor > 0.0f ? 1u : 0u);

      pc.model = prim.modelMatrix;
      pc.baseColorFactor = mat.baseColorFactor;
      pc.metallicFactor = mat.metallicFactor;
      pc.roughnessFactor = mat.roughnessFactor;
      pc.alphaCutoff = mat.alphaCutoff;
      pc.alphaMode = static_cast<uint32_t>(mat.alphaMode) | (mat.doubleSided ? 4u : 0u)
                   | (mat.deriveTransmissionFromThickness ? 8u : 0u);
      pc.iridescenceFactor = mat.iridescenceFactor;
      pc.iridescenceIor = mat.iridescenceIor;
      pc.iridescenceThicknessMin = mat.iridescenceThicknessMin;
      pc.iridescenceThicknessMax = mat.iridescenceThicknessMax;
      pc.transmissionFactor = mat.transmissionFactor;
      pc.thicknessFactor = mat.thicknessFactor;
      pc.attenuationColorPacked =
        (uint32_t(glm::clamp(mat.attenuationColor.r, 0.0f, 1.0f) * 255.0f) << 0) |
        (uint32_t(glm::clamp(mat.attenuationColor.g, 0.0f, 1.0f) * 255.0f) << 8) |
        (uint32_t(glm::clamp(mat.attenuationColor.b, 0.0f, 1.0f) * 255.0f) << 16);
      pc.attenuationDistance = mat.attenuationDistance;

      ctx.command_buffer.pushConstants(m_pipeline_layout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
        static_cast<uint32_t>(sizeof(pc)), &pc);
      ctx.command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipeline_layout,
        0, m_graph.material_descriptor_set(ctx.frame_index, prim.materialIndex), {});
      ctx.command_buffer.drawIndexed(prim.indexCount, 1, prim.firstIndex, prim.vertexOffset, 0);
    }
  }
  else
  {
    // Legacy single-draw path: opaque defaults
    ctx.command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);
    ctx.command_buffer.setCullModeEXT(vk::CullModeFlagBits::eBack);
    ctx.command_buffer.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, 0u);

    pc.model = glm::mat4(1.0f);
    pc.baseColorFactor = glm::vec4(1.0f);
    pc.metallicFactor = 1.0f;
    pc.roughnessFactor = 1.0f;
    pc.alphaCutoff = 0.5f;
    pc.alphaMode = 0; // OPAQUE

    ctx.command_buffer.pushConstants(m_pipeline_layout,
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
      static_cast<uint32_t>(sizeof(pc)), &pc);
    ctx.command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipeline_layout, 0,
      m_graph.default_descriptor_set(ctx.frame_index), {});
    ctx.mesh->draw(ctx.command_buffer);
  }
}

bool RasterOpaqueStage::is_enabled() const
{
  return !*m_use_rt && !*m_debug_2d;
}

} // namespace sps::vulkan
