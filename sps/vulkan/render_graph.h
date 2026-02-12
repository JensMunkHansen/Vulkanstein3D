#pragma once

#include <sps/vulkan/render_stage.h>
#include <sps/vulkan/shared_image_registry.h>

#include <array>
#include <memory>
#include <vector>

namespace sps::vulkan
{

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

  /// Shared image registry for cross-stage resource access.
  [[nodiscard]] SharedImageRegistry& image_registry() { return m_image_registry; }
  [[nodiscard]] const SharedImageRegistry& image_registry() const { return m_image_registry; }

private:
  const VulkanRenderer* m_renderer{ nullptr };
  std::vector<std::unique_ptr<RenderStage>> m_stages;
  std::array<vk::RenderPass, 4> m_render_passes{};
  std::vector<vk::Framebuffer> m_scene_framebuffers;
  SharedImageRegistry m_image_registry;

  void destroy_scene_framebuffers();
};

} // namespace sps::vulkan
