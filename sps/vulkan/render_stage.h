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
class ResourceDescriptor;
class RayTracingPipeline;
class Swapchain;

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
  vk::Extent2D extent;

  // Scene render pass infrastructure (HDR target)
  vk::RenderPass scene_render_pass;
  vk::Framebuffer scene_framebuffer;
  vk::PipelineLayout pipeline_layout;

  // Composite render pass infrastructure (swapchain target)
  vk::RenderPass composite_render_pass;
  vk::Framebuffer composite_framebuffer;

  // Scene data (read-only, not owned)
  const Mesh* mesh;
  const GltfScene* scene;
  const Camera* camera;

  // Descriptors (shared, not owned)
  const ResourceDescriptor* default_descriptor;
  const std::vector<std::unique_ptr<ResourceDescriptor>>* material_descriptors;

  // For RT blit target
  const Swapchain* swapchain;

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
class RenderStage
{
public:
  explicit RenderStage(std::string name) : m_name(std::move(name)) {}
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

private:
  std::string m_name;
};

} // namespace sps::vulkan
