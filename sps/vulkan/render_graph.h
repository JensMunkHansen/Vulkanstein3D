#pragma once

#include <sps/vulkan/render_stage.h>

#include <array>
#include <memory>
#include <vector>

namespace sps::vulkan
{

class Device;

/// Fixed-order render graph.
///
/// Owns render stages and drives per-frame command recording.
/// Stages execute in registration order:
///   1. Pre-pass stages (uses_render_pass() == false) — e.g. ray tracing
///   2. beginRenderPass + viewport/scissor
///   3. Render-pass stages in order — opaque, blend, debug 2D, UI
///   4. endRenderPass
///
/// No barriers are injected between stages (avoids Inexor limitation #2).
class RenderGraph
{
public:
  RenderGraph() = default;

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

private:
  std::vector<std::unique_ptr<RenderStage>> m_stages;
  std::array<vk::RenderPass, 4> m_render_passes{};
};

} // namespace sps::vulkan
