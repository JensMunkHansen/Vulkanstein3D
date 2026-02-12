#include <sps/vulkan/stages/composite_stage.h>

#include <spdlog/spdlog.h>
#include <sps/vulkan/config.h>
#include <sps/vulkan/pipeline.h>
#include <sps/vulkan/render_graph.h>
#include <sps/vulkan/renderer.h>

namespace sps::vulkan
{

CompositeStage::CompositeStage(const VulkanRenderer& renderer, const RenderGraph& graph,
  vk::RenderPass render_pass, const float* exposure, const int* tonemap_mode)
  : RenderStage("CompositeStage")
  , m_renderer(renderer)
  , m_graph(graph)
  , m_render_pass(render_pass)
  , m_exposure(exposure)
  , m_tonemap_mode(tonemap_mode)
{
  create_descriptor();
  create_pipeline();
  create_framebuffers();
  spdlog::info("Created composite stage (self-contained)");
}

CompositeStage::~CompositeStage()
{
  auto dev = m_renderer.device().device();

  destroy_framebuffers();

  if (m_pipeline)
    dev.destroyPipeline(m_pipeline);
  if (m_pipeline_layout)
    dev.destroyPipelineLayout(m_pipeline_layout);
  if (m_descriptor_pool)
    dev.destroyDescriptorPool(m_descriptor_pool);
  if (m_descriptor_layout)
    dev.destroyDescriptorSetLayout(m_descriptor_layout);
}

void CompositeStage::create_descriptor()
{
  auto dev = m_renderer.device().device();

  // Descriptor set layout: single sampler for the HDR buffer
  vk::DescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
  binding.descriptorCount = 1;
  binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

  vk::DescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &binding;
  m_descriptor_layout = dev.createDescriptorSetLayout(layoutInfo);

  // Descriptor pool
  vk::DescriptorPoolSize poolSize{};
  poolSize.type = vk::DescriptorType::eCombinedImageSampler;
  poolSize.descriptorCount = 1;

  vk::DescriptorPoolCreateInfo poolInfo{};
  poolInfo.maxSets = 1;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  m_descriptor_pool = dev.createDescriptorPool(poolInfo);

  // Allocate descriptor set
  vk::DescriptorSetAllocateInfo allocInfo{};
  allocInfo.descriptorPool = m_descriptor_pool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &m_descriptor_layout;
  m_descriptor_set = dev.allocateDescriptorSets(allocInfo)[0];

  // Write descriptor with HDR image
  update_descriptor();
}

void CompositeStage::update_descriptor()
{
  auto dev = m_renderer.device().device();

  const auto* hdr = m_graph.image_registry().get("hdr");
  vk::DescriptorImageInfo imageInfo{};
  imageInfo.sampler = hdr->sampler;
  imageInfo.imageView = hdr->image_view;
  imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

  vk::WriteDescriptorSet write{};
  write.dstSet = m_descriptor_set;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
  write.pImageInfo = &imageInfo;
  dev.updateDescriptorSets(write, {});
}

void CompositeStage::create_pipeline()
{
  auto dev = m_renderer.device().device();

  // Pipeline layout: descriptor set + push constants (exposure + tonemapMode)
  vk::PushConstantRange pcRange{};
  pcRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
  pcRange.offset = 0;
  pcRange.size = 8; // float exposure + int tonemapMode

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &m_descriptor_layout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pcRange;
  m_pipeline_layout = dev.createPipelineLayout(pipelineLayoutInfo);

  // Create pipeline
  sps::vulkan::GraphicsPipelineInBundle specification{};
  specification.device = dev;
  specification.vertexFilepath = SHADER_DIR "fullscreen_quad.spv";
  specification.fragmentFilepath = SHADER_DIR "composite.spv";
  specification.swapchainExtent = m_renderer.swapchain().extent();
  specification.swapchainImageFormat = m_renderer.swapchain().image_format();
  specification.backfaceCulling = false;
  specification.existingRenderPass = m_render_pass;
  specification.existingPipelineLayout = m_pipeline_layout;
  specification.depthTestEnabled = false;
  specification.msaaSamples = vk::SampleCountFlagBits::e1;

  auto output = sps::vulkan::create_graphics_pipeline(specification, true);
  m_pipeline = output.pipeline;
}

void CompositeStage::create_framebuffers()
{
  auto dev = m_renderer.device().device();
  vk::Extent2D extent = m_renderer.swapchain().extent();
  const auto& imageViews = m_renderer.swapchain().image_views();

  m_framebuffers.resize(imageViews.size());
  for (size_t i = 0; i < imageViews.size(); i++)
  {
    vk::ImageView attachments[] = { imageViews[i] };

    vk::FramebufferCreateInfo fbInfo{};
    fbInfo.renderPass = m_render_pass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = attachments;
    fbInfo.width = extent.width;
    fbInfo.height = extent.height;
    fbInfo.layers = 1;

    m_framebuffers[i] = dev.createFramebuffer(fbInfo);
  }
}

void CompositeStage::destroy_framebuffers()
{
  auto dev = m_renderer.device().device();
  for (auto fb : m_framebuffers)
    dev.destroyFramebuffer(fb);
  m_framebuffers.clear();
}

vk::Framebuffer CompositeStage::framebuffer(uint32_t image_index) const
{
  return m_framebuffers[image_index];
}

void CompositeStage::record(const FrameContext& ctx)
{
  ctx.command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);

  ctx.command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipeline_layout,
    0, m_descriptor_set, {});

  // Push exposure + tonemap mode
  struct CompositePushConstants
  {
    float exposure;
    int tonemapMode;
  } pc{};
  pc.exposure = *m_exposure;
  pc.tonemapMode = *m_tonemap_mode;

  ctx.command_buffer.pushConstants(m_pipeline_layout,
    vk::ShaderStageFlagBits::eFragment, 0,
    static_cast<uint32_t>(sizeof(pc)), &pc);

  // Draw fullscreen triangle (3 vertices, no vertex buffer)
  ctx.command_buffer.draw(3, 1, 0, 0);
}

void CompositeStage::on_swapchain_resize(const Device& /*device*/, vk::Extent2D /*extent*/)
{
  destroy_framebuffers();
  create_framebuffers();
  update_descriptor();
}

} // namespace sps::vulkan
