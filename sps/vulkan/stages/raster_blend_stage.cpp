#include <sps/vulkan/stages/raster_blend_stage.h>
#include <sps/vulkan/camera.h>
#include <sps/vulkan/descriptor_builder.h>
#include <sps/vulkan/gltf_loader.h>
#include <sps/vulkan/mesh.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <vector>

namespace sps::vulkan
{

void RasterBlendStage::record(const FrameContext& ctx)
{
  if (!ctx.mesh || !ctx.scene || !ctx.material_descriptors || !ctx.camera)
    return;

  // Collect blend primitives
  std::vector<const ScenePrimitive*> blend_prims;
  for (const auto& prim : ctx.scene->primitives)
  {
    const auto& mat = ctx.scene->materials[prim.materialIndex];
    if (mat.alphaMode == AlphaMode::Blend)
      blend_prims.push_back(&prim);
  }

  if (blend_prims.empty())
    return;

  // Sort by view-space depth (back-to-front = ascending Z in view space)
  glm::mat4 viewMatrix = ctx.camera->view_matrix();
  std::sort(blend_prims.begin(), blend_prims.end(),
    [&viewMatrix](const ScenePrimitive* a, const ScenePrimitive* b)
    {
      glm::vec4 aView = viewMatrix * a->modelMatrix * glm::vec4(a->centroid, 1.0f);
      glm::vec4 bView = viewMatrix * b->modelMatrix * glm::vec4(b->centroid, 1.0f);
      return aView.z < bView.z; // more negative Z = farther = draw first
    });

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

  // Mesh is already bound by RasterOpaqueStage
  ctx.command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);

  for (const auto* prim : blend_prims)
  {
    const auto& mat = ctx.scene->materials[prim->materialIndex];

    // Per-material back-face culling: cull back faces unless material is double-sided
    ctx.command_buffer.setCullModeEXT(
      mat.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);

    pc.model = prim->modelMatrix;
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

    ctx.command_buffer.pushConstants(ctx.pipeline_layout,
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
      static_cast<uint32_t>(sizeof(pc)), &pc);
    ctx.command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.pipeline_layout,
      0, (*ctx.material_descriptors)[prim->materialIndex]->descriptor_set(), {});
    ctx.command_buffer.drawIndexed(prim->indexCount, 1, prim->firstIndex, prim->vertexOffset, 0);
  }
}

bool RasterBlendStage::is_enabled() const
{
  return !*m_use_rt && !*m_debug_2d;
}

} // namespace sps::vulkan
