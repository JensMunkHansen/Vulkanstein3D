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
class GltfScene;
class Mesh;
class ResourceDescriptor;
class RayTracingPipeline;
class Swapchain;

/// Per-frame context passed to every stage.
/// All pointers are non-owning — Application retains ownership.
struct FrameContext
{
  vk::CommandBuffer command_buffer;
  uint32_t image_index;
  vk::Extent2D extent;

  // Shared render pass infrastructure (owned by Application)
  vk::RenderPass render_pass;
  vk::Framebuffer framebuffer;
  vk::PipelineLayout pipeline_layout;

  // Scene data (read-only, not owned)
  const Mesh* mesh;
  const GltfScene* scene;
  const Camera* camera;

  // Descriptors (shared, not owned)
  const ResourceDescriptor* default_descriptor;
  const std::vector<std::unique_ptr<ResourceDescriptor>>* material_descriptors;

  // For RT blit target
  const Swapchain* swapchain;
};

/// Abstract base class for a render stage.
///
/// Stages encapsulate a single rendering concern (opaque pass, blend pass,
/// debug view, ray tracing, UI overlay). They record commands into the
/// command buffer provided via FrameContext.
///
/// Render-pass stages (`uses_render_pass() == true`) execute inside a
/// render pass that the RenderGraph begins/ends. Non-render-pass stages
/// (like ray tracing) run before the render pass and manage their own
/// synchronization.
class RenderStage
{
public:
  explicit RenderStage(std::string name) : m_name(std::move(name)) {}
  virtual ~RenderStage() = default;

  /// Record commands for this stage into ctx.command_buffer.
  virtual void record(const FrameContext& ctx) = 0;

  /// Whether this stage should execute this frame.
  [[nodiscard]] virtual bool is_enabled() const { return true; }

  /// Whether this stage records inside the shared render pass.
  /// Stages returning false (e.g. ray tracing) run before beginRenderPass.
  [[nodiscard]] virtual bool uses_render_pass() const { return true; }

  /// Called when the swapchain is recreated.
  /// Only stages with swapchain-dependent resources need to override.
  virtual void on_swapchain_resize(const Device& /*device*/, vk::Extent2D /*extent*/) {}

  [[nodiscard]] const std::string& name() const { return m_name; }

private:
  std::string m_name;
};

} // namespace sps::vulkan
