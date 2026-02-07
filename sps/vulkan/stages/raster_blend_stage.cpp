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

  // Push constant struct matching shader layout (88 bytes)
  struct PushConstants
  {
    glm::mat4 model;
    glm::vec4 baseColorFactor;
    float alphaCutoff;
    uint32_t alphaMode;
  } pc{};

  // Mesh is already bound by RasterOpaqueStage
  ctx.command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);

  for (const auto* prim : blend_prims)
  {
    const auto& mat = ctx.scene->materials[prim->materialIndex];

    pc.model = prim->modelMatrix;
    pc.baseColorFactor = mat.baseColorFactor;
    pc.alphaCutoff = mat.alphaCutoff;
    pc.alphaMode = static_cast<uint32_t>(mat.alphaMode);

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
