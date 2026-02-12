#pragma once

#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>

namespace sps::vulkan
{

class Camera;
class Device;
struct GltfScene;
class Mesh;

/// Execution phase for render stages.
/// Determines which render pass (if any) the stage runs in.
enum class Phase
{
  PrePass,       // Before any render pass (e.g. ray tracing)
  ScenePass,     // Inside the scene render pass (HDR target)
  Intermediate,  // Between render passes (e.g. compute blur)
  CompositePass  // Inside the composite render pass (swapchain target)
};

/// Per-frame context passed to every stage.
/// All pointers are non-owning — Application retains ownership.
struct FrameContext
{
  vk::CommandBuffer command_buffer;
  uint32_t image_index;
  uint32_t frame_index{ 0 }; // Index into per-frame resource rings (0..frames_in_flight-1)
  vk::Extent2D extent;

  // Scene data (read-only, not owned)
  const Mesh* mesh;
  const GltfScene* scene;
  const Camera* camera;

  // Clear color (background)
  glm::vec3 clear_color{ 0.0f, 0.0f, 0.0f };
};

/// Abstract base class for a render stage.
///
/// Stages encapsulate a single rendering concern (opaque pass, blend pass,
/// debug view, ray tracing, UI overlay). They record commands into the
/// command buffer provided via FrameContext.
///
/// Stages declare their phase via phase():
///   - PrePass: runs before any render pass (manages own synchronization)
///   - ScenePass: runs inside the scene render pass (HDR target)
///   - Intermediate: runs between render passes (e.g. compute blur)
///   - CompositePass: runs inside the composite render pass (swapchain target)
///
/// ## Multiple frames in flight (not yet supported)
///
/// Self-contained stages (those that own their pipelines, descriptors, and
/// framebuffers) are the right granularity for multiple frames in flight.
/// The render graph provides frames_in_flight() (constant, for resource
/// allocation) and FrameContext::frame_index (per-frame, 0..N-1).
///
/// Read-only resources (pipeline, sampler, render pass) stay shared.
/// Write-per-frame resources (descriptor sets, framebuffers, storage images)
/// are allocated as rings of frames_in_flight copies, indexed by frame_index.
///
/// Each stage decides what to duplicate — the app should not need to know
/// that e.g. CompositeStage needs N descriptor sets while SSSBlurStage
/// needs N ping images.
///
/// Currently frames_in_flight is always 1. When increasing it, stages that
/// are not yet self-contained will need migration first.
class RenderStage
{
public:
  explicit RenderStage(std::string name, uint32_t frames_in_flight = 1)
    : m_name(std::move(name))
    , m_frames_in_flight(frames_in_flight)
  {
  }
  virtual ~RenderStage() = default;

  /// Record commands for this stage into ctx.command_buffer.
  virtual void record(const FrameContext& ctx) = 0;

  /// Whether this stage should execute this frame.
  [[nodiscard]] virtual bool is_enabled() const { return true; }

  /// The execution phase of this stage.
  [[nodiscard]] virtual Phase phase() const { return Phase::ScenePass; }

  /// Whether this stage records inside a render pass.
  /// PrePass and Intermediate stages return false; Scene and Composite return true.
  [[nodiscard]] bool uses_render_pass() const
  {
    auto p = phase();
    return p == Phase::ScenePass || p == Phase::CompositePass;
  }

  /// Called when the swapchain is recreated.
  /// Only stages with swapchain-dependent resources need to override.
  virtual void on_swapchain_resize(const Device& /*device*/, vk::Extent2D /*extent*/) {}

  [[nodiscard]] const std::string& name() const { return m_name; }

  /// Number of frames that may be in flight simultaneously.
  /// Stages use this at construction to allocate per-frame resource rings.
  [[nodiscard]] uint32_t frames_in_flight() const { return m_frames_in_flight; }

private:
  std::string m_name;
  uint32_t m_frames_in_flight;
};

} // namespace sps::vulkan
