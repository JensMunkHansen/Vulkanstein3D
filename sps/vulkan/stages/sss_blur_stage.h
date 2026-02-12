#pragma once

#include <sps/vulkan/render_stage.h>

namespace sps::vulkan
{

class RenderGraph;
class VulkanRenderer;

/// Screen-space subsurface scattering blur stage.
///
/// Self-contained stage: owns its compute pipeline, descriptors, and ping image.
/// Runs as an Intermediate stage between the scene and composite passes.
/// Applies a separable (horizontal + vertical) blur to SSS pixels only
/// (identified by alpha == 1 in the HDR buffer), with per-channel blur widths.
///
/// Queries the SharedImageRegistry (via RenderGraph) for "hdr" and "depth_stencil"
/// entries. Refreshes cached handles on swapchain resize.
class SSSBlurStage : public RenderStage
{
public:
  SSSBlurStage(const VulkanRenderer& renderer, RenderGraph& graph,
    const bool* enabled,
    const float* blur_width_r, const float* blur_width_g, const float* blur_width_b);
  ~SSSBlurStage() override;

  SSSBlurStage(const SSSBlurStage&) = delete;
  SSSBlurStage& operator=(const SSSBlurStage&) = delete;

  void record(const FrameContext& ctx) override;
  [[nodiscard]] bool is_enabled() const override { return *m_enabled; }
  [[nodiscard]] Phase phase() const override { return Phase::Intermediate; }
  void on_swapchain_resize(const Device& device, vk::Extent2D extent) override;

private:
  const VulkanRenderer& m_renderer;
  RenderGraph& m_graph;
  const bool* m_enabled;
  const float* m_blur_width_r;
  const float* m_blur_width_g;
  const float* m_blur_width_b;

  // Owned resources
  vk::DescriptorSetLayout m_descriptor_layout{ VK_NULL_HANDLE };
  vk::DescriptorPool m_descriptor_pool{ VK_NULL_HANDLE };
  vk::DescriptorSet m_h_descriptor{ VK_NULL_HANDLE };  // HDR->ping (horizontal)
  vk::DescriptorSet m_v_descriptor{ VK_NULL_HANDLE };  // ping->HDR (vertical)
  vk::PipelineLayout m_pipeline_layout{ VK_NULL_HANDLE };
  vk::Pipeline m_pipeline{ VK_NULL_HANDLE };
  vk::Sampler m_stencil_sampler{ VK_NULL_HANDLE };

  // Ping image (intermediate for separable blur)
  vk::Image m_ping_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_ping_image_memory{ VK_NULL_HANDLE };
  vk::ImageView m_ping_image_view{ VK_NULL_HANDLE };

  // Cached from registry (refreshed on resize)
  vk::Image m_hdr_image;
  vk::Image m_depth_stencil_image;
  vk::Extent2D m_extent{};

  void create_pipeline();
  void create_ping_image();
  void destroy_ping_image();
  void create_descriptors();
  void destroy_descriptors();
  void update_from_registry();
};

} // namespace sps::vulkan
