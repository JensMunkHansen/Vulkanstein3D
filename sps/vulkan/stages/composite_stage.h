#pragma once

#include <sps/vulkan/render_stage.h>

#include <vector>

namespace sps::vulkan
{

class VulkanRenderer;

/// Fullscreen composite pass: samples the HDR buffer, applies exposure + tone mapping + gamma,
/// and writes to the swapchain.
///
/// Self-contained stage: owns its pipeline, descriptors, and framebuffers.
/// Gets the render pass from the RenderGraph (shared resource).
class CompositeStage : public RenderStage
{
public:
  CompositeStage(const VulkanRenderer& renderer, vk::RenderPass render_pass,
    const float* exposure, const int* tonemap_mode);
  ~CompositeStage() override;

  CompositeStage(const CompositeStage&) = delete;
  CompositeStage& operator=(const CompositeStage&) = delete;

  void record(const FrameContext& ctx) override;
  [[nodiscard]] Phase phase() const override { return Phase::CompositePass; }
  void on_swapchain_resize(const Device& device, vk::Extent2D extent) override;

  [[nodiscard]] vk::Framebuffer framebuffer(uint32_t image_index) const;

private:
  const VulkanRenderer& m_renderer;
  vk::RenderPass m_render_pass;
  const float* m_exposure;
  const int* m_tonemap_mode;

  vk::DescriptorSetLayout m_descriptor_layout{ VK_NULL_HANDLE };
  vk::DescriptorPool m_descriptor_pool{ VK_NULL_HANDLE };
  vk::DescriptorSet m_descriptor_set{ VK_NULL_HANDLE };
  vk::PipelineLayout m_pipeline_layout{ VK_NULL_HANDLE };
  vk::Pipeline m_pipeline{ VK_NULL_HANDLE };
  std::vector<vk::Framebuffer> m_framebuffers;

  void create_descriptor();
  void update_descriptor();
  void create_pipeline();
  void create_framebuffers();
  void destroy_framebuffers();
};

} // namespace sps::vulkan
