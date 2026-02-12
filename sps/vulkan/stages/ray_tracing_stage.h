#pragma once

#include <sps/vulkan/render_stage.h>

#include <memory>
#include <vector>

namespace sps::vulkan
{

class AccelerationStructure;
class Buffer;
class Device;
class GltfScene;
class IBL;
class Mesh;
class RayTracingPipeline;
class RenderGraph;
class Texture;
class VulkanRenderer;

/// Self-contained ray tracing stage: owns storage image, descriptor set,
/// pipeline, and acceleration structures.
///
/// Traces rays into a private storage image (R8G8B8A8Unorm), then blits
/// the result to the shared HDR image from the registry. The composite
/// stage handles tone mapping + gamma + present, same as the raster path.
///
/// Acceleration structures are rebuilt on mesh change via on_mesh_changed().
/// Storage image is resized on swapchain resize via on_swapchain_resize().
class RayTracingStage : public RenderStage
{
public:
  RayTracingStage(const VulkanRenderer& renderer, RenderGraph& graph,
    const bool* use_rt, vk::Buffer uniform_buffer);
  ~RayTracingStage() override;

  RayTracingStage(const RayTracingStage&) = delete;
  RayTracingStage& operator=(const RayTracingStage&) = delete;

  void record(const FrameContext& ctx) override;
  [[nodiscard]] bool is_enabled() const override;
  [[nodiscard]] Phase phase() const override { return Phase::PrePass; }
  void on_swapchain_resize(const Device& device, vk::Extent2D extent) override;

  /// Rebuild BLAS/TLAS and update descriptor bindings for new mesh.
  /// @param mesh The mesh with vertex/index buffers.
  /// @param scene Optional scene with materials/primitives for texture binding.
  ///              If null, vertex colors are used (fallback white texture).
  /// @param ibl Optional IBL for environment cubemap background in miss shader.
  void on_mesh_changed(const Mesh& mesh, const GltfScene* scene = nullptr, const IBL* ibl = nullptr);

  /// Update environment cubemap binding (e.g. after HDR switch).
  void update_environment(const IBL& ibl);

private:
  const VulkanRenderer& m_renderer;
  RenderGraph& m_graph;
  const bool* m_use_rt;
  vk::Buffer m_uniform_buffer;

  // Acceleration structures
  std::unique_ptr<AccelerationStructure> m_blas;
  std::unique_ptr<AccelerationStructure> m_tlas;

  // RT pipeline (pipeline + layout + SBT)
  std::unique_ptr<RayTracingPipeline> m_rt_pipeline;

  // RT storage image (render target)
  vk::Image m_rt_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_rt_image_memory{ VK_NULL_HANDLE };
  vk::ImageView m_rt_image_view{ VK_NULL_HANDLE };

  // RT descriptor set
  vk::DescriptorPool m_descriptor_pool{ VK_NULL_HANDLE };
  vk::DescriptorSetLayout m_descriptor_layout{ VK_NULL_HANDLE };
  vk::DescriptorSet m_descriptor_set{ VK_NULL_HANDLE };

  // Material index buffer (triangleID -> materialIndex)
  std::unique_ptr<Buffer> m_material_index_buffer;

  // Fallback 1x1 white texture for materials without base color
  std::unique_ptr<Texture> m_fallback_texture;

  // Number of textures bound in descriptor (for pool sizing)
  uint32_t m_texture_count{ 0 };

  // Cached HDR image handle from registry
  vk::Image m_hdr_image;

  void create_storage_image();
  void destroy_storage_image();
  void create_descriptor(const Mesh& mesh, const GltfScene* scene, const IBL* ibl);
  void create_pipeline();
  void build_acceleration_structures(const Mesh& mesh, const GltfScene* scene);
  void build_material_index_buffer(const Mesh& mesh, const GltfScene* scene);
  void update_from_registry();
};

} // namespace sps::vulkan
