#pragma once

#include <sps/vulkan/material_texture_set.h>
#include <sps/vulkan/render_stage.h>
#include <sps/vulkan/shared_image_registry.h>

#include <array>
#include <memory>
#include <vector>

namespace sps::vulkan
{

class CompositeStage;
class Device;
class VulkanRenderer;

/// Fixed-order render graph.
///
/// Owns render stages and drives per-frame command recording.
/// Stages execute in registration order, grouped by phase:
///   1. PrePass — outside any render pass (e.g. ray tracing)
///   2. ScenePass — inside scene render pass (HDR target)
///   3. Intermediate — between render passes (e.g. compute blur)
///   4. CompositePass — inside composite render pass (swapchain target)
///
/// ## Material descriptor sets
///
/// The graph owns a material descriptor pool and allocates descriptor sets
/// for each material. SceneManager provides texture handles (view + sampler
/// pairs via MaterialTextureSet), and the graph creates and writes the
/// descriptor sets. The API is frame-indexed from day one for future N>1
/// frames in flight support.
///
/// ## Scene framebuffers
///
/// The graph owns the scene framebuffers (one per swapchain image), which
/// attach the shared HDR image, depth-stencil, and optional MSAA resolve
/// target to the scene render pass. These are created from the image registry
/// entries and rebuilt on swapchain resize.
///
/// ## Shared image registry
///
/// The graph owns a SharedImageRegistry where the application registers
/// shared images ("hdr", "depth_stencil") and stages declare their access
/// intent (Read, Write, ReadWrite) at construction time.
///
/// ## Barrier strategy
///
/// No gratuitous barriers are injected between stages within a phase.
/// Between phases, some barriers are unavoidable (e.g. ScenePass writes
/// HDR → Intermediate reads it). Currently stages manage these manually.
///
/// The access declarations in the registry provide the information needed
/// for the render graph to insert these between-phase barriers automatically:
///   - For each shared image, inspect access_records() to see which stages
///     read/write it and in which phase.
///   - Insert a barrier only where a prior phase wrote and a later phase reads.
///
/// ## Multiple frames in flight
///
/// Inter-frame synchronization uses per-frame resource indexing (rings),
/// not barriers. Each stage duplicates its mutable resources N times and
/// indexes by FrameContext::frame_index. Read-only resources (pipelines,
/// samplers, render passes) stay shared. This avoids inter-frame barriers
/// entirely — the only barriers are the intra-frame phase transitions above.
class RenderGraph
{
public:
  RenderGraph() = default;
  ~RenderGraph();

  /// Set the renderer reference (needed for framebuffer creation).
  /// Must be called before create_scene_framebuffers().
  void set_renderer(const VulkanRenderer& renderer) { m_renderer = &renderer; }

  /// Register a stage. Returns a non-owning pointer for the caller to store.
  template <typename T, typename... Args>
  T* add(Args&&... args)
  {
    auto stage = std::make_unique<T>(std::forward<Args>(args)...);
    T* ptr = stage.get();
    m_stages.push_back(std::move(stage));
    return ptr;
  }

  /// Record all enabled stages into the command buffer.
  void record(const FrameContext& ctx);

  /// Propagate swapchain resize to all stages.
  void on_swapchain_resize(const Device& device, vk::Extent2D extent);

  /// Register a shared render pass for a given phase.
  void set_render_pass(Phase phase, vk::RenderPass rp);

  /// Retrieve the shared render pass for a given phase.
  [[nodiscard]] vk::RenderPass render_pass(Phase phase) const;

  /// Create scene framebuffers from the registry images and scene render pass.
  /// Call after populating the image registry and setting the scene render pass.
  void create_scene_framebuffers();

  /// Destroy and recreate scene framebuffers (called during swapchain resize).
  void recreate_scene_framebuffers();

  /// Create the canonical material descriptor set layout (12 bindings).
  /// Call after set_renderer() but before adding stages that need it.
  void create_material_descriptor_layout();

  /// The graph-owned material descriptor set layout (stable, never recreated).
  [[nodiscard]] vk::DescriptorSetLayout material_descriptor_layout() const;

  /// Allocate and write material descriptor sets from the graph-owned pool.
  ///
  /// Destroys any previous pool and sets, then creates a new pool with
  /// enough capacity for (1 + material_count) * frames_in_flight sets.
  /// Each set is written with the UBO buffer info for its frame and the
  /// texture bindings from the corresponding MaterialTextureSet.
  ///
  /// @param default_textures  Texture bindings for the default (non-scene) path.
  /// @param material_textures Per-material texture bindings.
  /// @param ubo_infos         One UBO descriptor buffer info per frame in flight.
  void allocate_material_descriptors(
    const MaterialTextureSet& default_textures,
    const std::vector<MaterialTextureSet>& material_textures,
    const std::vector<vk::DescriptorBufferInfo>& ubo_infos);

  /// Get the default descriptor set for a given frame index.
  [[nodiscard]] vk::DescriptorSet default_descriptor_set(uint32_t frame_index) const;

  /// Get a material descriptor set for a given frame index and material index.
  [[nodiscard]] vk::DescriptorSet material_descriptor_set(
    uint32_t frame_index, uint32_t material_index) const;

  /// Number of material descriptor sets (per frame). 0 when no scene is loaded.
  [[nodiscard]] uint32_t material_set_count() const;

  /// Register the composite stage so the graph can query its framebuffer.
  void set_composite_stage(const CompositeStage* stage);

  /// Create the HDR image, sampler, optional MSAA color target, and register in the image registry.
  /// Call after set_renderer() but before adding stages that need the HDR image.
  void create_hdr_resources();

  /// Destroy and recreate HDR + MSAA images, then update the registry.
  /// Call during swapchain resize, before recreate_scene_framebuffers().
  void recreate_hdr_resources();

  /// The HDR image format (static, never changes).
  [[nodiscard]] static constexpr vk::Format hdr_format() { return vk::Format::eR16G16B16A16Sfloat; }

  /// The HDR sampler (immutable, created once).
  [[nodiscard]] vk::Sampler hdr_sampler() const;

  /// Shared image registry for cross-stage resource access.
  [[nodiscard]] SharedImageRegistry& image_registry() { return m_image_registry; }
  [[nodiscard]] const SharedImageRegistry& image_registry() const { return m_image_registry; }

private:
  const VulkanRenderer* m_renderer{ nullptr };
  const CompositeStage* m_composite_stage{ nullptr };
  vk::DescriptorSetLayout m_material_layout{ VK_NULL_HANDLE };
  uint32_t m_frames_in_flight{ 1 };
  vk::DescriptorPool m_material_pool{ VK_NULL_HANDLE };
  std::vector<vk::DescriptorSet> m_default_sets;                // [frame_index]
  std::vector<std::vector<vk::DescriptorSet>> m_material_sets;  // [frame_index][material_index]
  std::vector<std::unique_ptr<RenderStage>> m_stages;
  std::array<vk::RenderPass, 4> m_render_passes{};
  std::vector<vk::Framebuffer> m_scene_framebuffers;
  SharedImageRegistry m_image_registry;

  void destroy_scene_framebuffers();
  void destroy_material_pool();

  // HDR image (single-sample resolve target + composite source)
  static constexpr vk::Format m_hdr_format = vk::Format::eR16G16B16A16Sfloat;
  vk::Image m_hdr_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_hdr_image_memory{ VK_NULL_HANDLE };
  vk::ImageView m_hdr_image_view{ VK_NULL_HANDLE };
  vk::Sampler m_hdr_sampler{ VK_NULL_HANDLE };

  // MSAA color target (resolves to m_hdr_image in scene framebuffer)
  vk::Image m_hdr_msaa_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_hdr_msaa_image_memory{ VK_NULL_HANDLE };
  vk::ImageView m_hdr_msaa_image_view{ VK_NULL_HANDLE };

  void destroy_hdr_resources();
  void create_msaa_color_resources();

  /// Write one descriptor set with a UBO and 11 texture bindings.
  void write_material_set(vk::DescriptorSet set,
    const vk::DescriptorBufferInfo& ubo_info,
    const MaterialTextureSet& textures);
};

} // namespace sps::vulkan
