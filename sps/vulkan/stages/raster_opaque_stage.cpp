#include <sps/vulkan/stages/raster_opaque_stage.h>
#include <sps/vulkan/descriptor_builder.h>
#include <sps/vulkan/gltf_loader.h>
#include <sps/vulkan/mesh.h>

#include <glm/glm.hpp>

namespace sps::vulkan
{

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

  if (ctx.scene && !ctx.scene->primitives.empty() && ctx.material_descriptors &&
    !ctx.material_descriptors->empty())
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

      pc.model = prim.modelMatrix;
      pc.baseColorFactor = mat.baseColorFactor;
      pc.metallicFactor = mat.metallicFactor;
      pc.roughnessFactor = mat.roughnessFactor;
      pc.alphaCutoff = mat.alphaCutoff;
      pc.alphaMode = static_cast<uint32_t>(mat.alphaMode) | (mat.doubleSided ? 4u : 0u);
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

      ctx.command_buffer.pushConstants(ctx.pipeline_layout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
        static_cast<uint32_t>(sizeof(pc)), &pc);
      ctx.command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.pipeline_layout,
        0, (*ctx.material_descriptors)[prim.materialIndex]->descriptor_set(), {});
      ctx.command_buffer.drawIndexed(prim.indexCount, 1, prim.firstIndex, prim.vertexOffset, 0);
    }
  }
  else
  {
    // Legacy single-draw path: opaque defaults
    ctx.command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);
    ctx.command_buffer.setCullModeEXT(vk::CullModeFlagBits::eBack);

    pc.model = glm::mat4(1.0f);
    pc.baseColorFactor = glm::vec4(1.0f);
    pc.metallicFactor = 1.0f;
    pc.roughnessFactor = 1.0f;
    pc.alphaCutoff = 0.5f;
    pc.alphaMode = 0; // OPAQUE

    ctx.command_buffer.pushConstants(ctx.pipeline_layout,
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
      static_cast<uint32_t>(sizeof(pc)), &pc);
    ctx.command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.pipeline_layout, 0,
      ctx.default_descriptor->descriptor_set(), {});
    ctx.mesh->draw(ctx.command_buffer);
  }
}

bool RasterOpaqueStage::is_enabled() const
{
  return !*m_use_rt && !*m_debug_2d;
}

} // namespace sps::vulkan
