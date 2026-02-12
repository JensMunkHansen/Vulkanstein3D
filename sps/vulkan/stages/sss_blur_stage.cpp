#include <sps/vulkan/stages/sss_blur_stage.h>

#include <spdlog/spdlog.h>
#include <sps/vulkan/config.h>
#include <sps/vulkan/render_graph.h>
#include <sps/vulkan/renderer.h>
#include <sps/vulkan/shaders.h>

#include <array>

namespace sps::vulkan
{

SSSBlurStage::SSSBlurStage(const VulkanRenderer& renderer, RenderGraph& graph,
  const bool* enabled, const bool* use_rt,
  const float* blur_width_r, const float* blur_width_g, const float* blur_width_b)
  : RenderStage("SSSBlurStage")
  , m_renderer(renderer)
  , m_graph(graph)
  , m_enabled(enabled)
  , m_use_rt(use_rt)
  , m_blur_width_r(blur_width_r)
  , m_blur_width_g(blur_width_g)
  , m_blur_width_b(blur_width_b)
{
  // Declare access intent for shared images (used by render graph for barrier insertion)
  m_graph.image_registry().declare_access("hdr", name(), phase(), AccessIntent::ReadWrite);
  m_graph.image_registry().declare_access("depth_stencil", name(), phase(), AccessIntent::Read);

  update_from_registry();
  create_pipeline();
  create_ping_image();
  create_descriptors();
  spdlog::info("Created SSS blur stage (self-contained) {}x{}", m_extent.width, m_extent.height);
}

SSSBlurStage::~SSSBlurStage()
{
  auto dev = m_renderer.device().device();

  destroy_descriptors();
  destroy_ping_image();

  if (m_pipeline)
    dev.destroyPipeline(m_pipeline);
  if (m_pipeline_layout)
    dev.destroyPipelineLayout(m_pipeline_layout);
  if (m_descriptor_layout)
    dev.destroyDescriptorSetLayout(m_descriptor_layout);
  if (m_stencil_sampler)
    dev.destroySampler(m_stencil_sampler);
}

void SSSBlurStage::update_from_registry()
{
  const auto* hdr = m_graph.image_registry().get("hdr");
  const auto* ds = m_graph.image_registry().get("depth_stencil");

  m_hdr_image = hdr ? hdr->image : vk::Image{};
  m_depth_stencil_image = ds ? ds->image : vk::Image{};
  m_extent = m_renderer.swapchain().extent();
}

void SSSBlurStage::create_pipeline()
{
  auto dev = m_renderer.device().device();

  // Descriptor set layout: 2 storage images + 1 combined image sampler for stencil
  std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};
  bindings[0].binding = 0;
  bindings[0].descriptorType = vk::DescriptorType::eStorageImage;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;
  bindings[1].binding = 1;
  bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
  bindings[1].descriptorCount = 1;
  bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;
  bindings[2].binding = 2;
  bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
  bindings[2].descriptorCount = 1;
  bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

  vk::DescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();
  m_descriptor_layout = dev.createDescriptorSetLayout(layoutInfo);

  // Stencil sampler (nearest, clamp-to-edge)
  vk::SamplerCreateInfo samplerInfo{};
  samplerInfo.magFilter = vk::Filter::eNearest;
  samplerInfo.minFilter = vk::Filter::eNearest;
  samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  m_stencil_sampler = dev.createSampler(samplerInfo);

  // Pipeline layout
  vk::PushConstantRange pcRange{};
  pcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
  pcRange.offset = 0;
  pcRange.size = 16; // 3x float blurWidth (R,G,B) + int direction

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &m_descriptor_layout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pcRange;
  m_pipeline_layout = dev.createPipelineLayout(pipelineLayoutInfo);

  // Compute pipeline
  auto shaderModule = sps::vulkan::createModule(SHADER_DIR "sss_blur.spv", dev, true);

  vk::PipelineShaderStageCreateInfo stageInfo{};
  stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
  stageInfo.module = shaderModule;
  stageInfo.pName = "main";

  vk::ComputePipelineCreateInfo pipelineInfo{};
  pipelineInfo.stage = stageInfo;
  pipelineInfo.layout = m_pipeline_layout;

  m_pipeline = dev.createComputePipeline(nullptr, pipelineInfo).value;

  dev.destroyShaderModule(shaderModule);
}

void SSSBlurStage::create_ping_image()
{
  auto dev = m_renderer.device().device();

  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.extent.width = m_extent.width;
  imageInfo.extent.height = m_extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = RenderGraph::hdr_format();
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.initialLayout = vk::ImageLayout::eUndefined;
  imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
  imageInfo.samples = vk::SampleCountFlagBits::e1;
  imageInfo.sharingMode = vk::SharingMode::eExclusive;

  m_ping_image = dev.createImage(imageInfo);

  vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(m_ping_image);
  vk::MemoryAllocateInfo allocInfo{};
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = m_renderer.device().find_memory_type(
    memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_ping_image_memory = dev.allocateMemory(allocInfo);
  dev.bindImageMemory(m_ping_image, m_ping_image_memory, 0);

  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = m_ping_image;
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.format = RenderGraph::hdr_format();
  viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  m_ping_image_view = dev.createImageView(viewInfo);

  // Transition ping image to General layout
  vk::CommandBufferAllocateInfo allocCmdInfo{};
  allocCmdInfo.commandPool = m_renderer.command_pool();
  allocCmdInfo.level = vk::CommandBufferLevel::ePrimary;
  allocCmdInfo.commandBufferCount = 1;
  auto cmd = dev.allocateCommandBuffers(allocCmdInfo)[0];
  cmd.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

  vk::ImageMemoryBarrier barrier{};
  barrier.oldLayout = vk::ImageLayout::eUndefined;
  barrier.newLayout = vk::ImageLayout::eGeneral;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = m_ping_image;
  barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = {};
  barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
  cmd.pipelineBarrier(
    vk::PipelineStageFlagBits::eTopOfPipe,
    vk::PipelineStageFlagBits::eComputeShader,
    {}, {}, {}, barrier);

  cmd.end();
  vk::SubmitInfo submitInfo{};
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;
  m_renderer.device().graphics_queue().submit(submitInfo);
  m_renderer.device().graphics_queue().waitIdle();
  dev.freeCommandBuffers(m_renderer.command_pool(), cmd);
}

void SSSBlurStage::destroy_ping_image()
{
  auto dev = m_renderer.device().device();

  if (m_ping_image_view)
  {
    dev.destroyImageView(m_ping_image_view);
    m_ping_image_view = VK_NULL_HANDLE;
  }
  if (m_ping_image)
  {
    dev.destroyImage(m_ping_image);
    m_ping_image = VK_NULL_HANDLE;
  }
  if (m_ping_image_memory)
  {
    dev.freeMemory(m_ping_image_memory);
    m_ping_image_memory = VK_NULL_HANDLE;
  }
}

void SSSBlurStage::create_descriptors()
{
  auto dev = m_renderer.device().device();

  // Descriptor pool (2 sets, 4 storage images + 2 combined image samplers)
  std::array<vk::DescriptorPoolSize, 2> poolSizes{};
  poolSizes[0].type = vk::DescriptorType::eStorageImage;
  poolSizes[0].descriptorCount = 4;
  poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
  poolSizes[1].descriptorCount = 2;

  vk::DescriptorPoolCreateInfo poolInfo{};
  poolInfo.maxSets = 2;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  m_descriptor_pool = dev.createDescriptorPool(poolInfo);

  // Allocate 2 descriptor sets
  std::array<vk::DescriptorSetLayout, 2> layouts = { m_descriptor_layout, m_descriptor_layout };
  vk::DescriptorSetAllocateInfo dsAllocInfo{};
  dsAllocInfo.descriptorPool = m_descriptor_pool;
  dsAllocInfo.descriptorSetCount = 2;
  dsAllocInfo.pSetLayouts = layouts.data();
  auto sets = dev.allocateDescriptorSets(dsAllocInfo);
  m_h_descriptor = sets[0]; // H pass: read HDR, write ping
  m_v_descriptor = sets[1]; // V pass: read ping, write HDR

  // Get image views from registry
  const auto* hdr = m_graph.image_registry().get("hdr");
  const auto* ds = m_graph.image_registry().get("depth_stencil");

  vk::DescriptorImageInfo hdrInfo{};
  hdrInfo.imageView = hdr ? hdr->image_view : vk::ImageView{};
  hdrInfo.imageLayout = vk::ImageLayout::eGeneral;

  vk::DescriptorImageInfo pingInfo{};
  pingInfo.imageView = m_ping_image_view;
  pingInfo.imageLayout = vk::ImageLayout::eGeneral;

  vk::DescriptorImageInfo stencilInfo{};
  stencilInfo.sampler = m_stencil_sampler;
  stencilInfo.imageView = ds ? ds->image_view : vk::ImageView{};
  stencilInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

  // H descriptor: binding 0 = HDR (read), binding 1 = ping (write), binding 2 = stencil
  // V descriptor: binding 0 = ping (read), binding 1 = HDR (write), binding 2 = stencil
  std::array<vk::WriteDescriptorSet, 6> writes{};
  writes[0].dstSet = m_h_descriptor;
  writes[0].dstBinding = 0;
  writes[0].descriptorCount = 1;
  writes[0].descriptorType = vk::DescriptorType::eStorageImage;
  writes[0].pImageInfo = &hdrInfo;

  writes[1].dstSet = m_h_descriptor;
  writes[1].dstBinding = 1;
  writes[1].descriptorCount = 1;
  writes[1].descriptorType = vk::DescriptorType::eStorageImage;
  writes[1].pImageInfo = &pingInfo;

  writes[2].dstSet = m_h_descriptor;
  writes[2].dstBinding = 2;
  writes[2].descriptorCount = 1;
  writes[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
  writes[2].pImageInfo = &stencilInfo;

  writes[3].dstSet = m_v_descriptor;
  writes[3].dstBinding = 0;
  writes[3].descriptorCount = 1;
  writes[3].descriptorType = vk::DescriptorType::eStorageImage;
  writes[3].pImageInfo = &pingInfo;

  writes[4].dstSet = m_v_descriptor;
  writes[4].dstBinding = 1;
  writes[4].descriptorCount = 1;
  writes[4].descriptorType = vk::DescriptorType::eStorageImage;
  writes[4].pImageInfo = &hdrInfo;

  writes[5].dstSet = m_v_descriptor;
  writes[5].dstBinding = 2;
  writes[5].descriptorCount = 1;
  writes[5].descriptorType = vk::DescriptorType::eCombinedImageSampler;
  writes[5].pImageInfo = &stencilInfo;

  dev.updateDescriptorSets(writes, {});
}

void SSSBlurStage::destroy_descriptors()
{
  auto dev = m_renderer.device().device();

  if (m_descriptor_pool)
  {
    dev.destroyDescriptorPool(m_descriptor_pool);
    m_descriptor_pool = VK_NULL_HANDLE;
    m_h_descriptor = VK_NULL_HANDLE;
    m_v_descriptor = VK_NULL_HANDLE;
  }
}

void SSSBlurStage::on_swapchain_resize(const Device& /*device*/, vk::Extent2D /*extent*/)
{
  update_from_registry();
  destroy_descriptors();
  destroy_ping_image();
  create_ping_image();
  create_descriptors();
}

void SSSBlurStage::record(const FrameContext& ctx)
{
  auto cmd = ctx.command_buffer;
  uint32_t w = m_extent.width;
  uint32_t h = m_extent.height;

  // Transition HDR from ShaderReadOnlyOptimal to General for compute read/write
  {
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_hdr_image;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

    cmd.pipelineBarrier(
      vk::PipelineStageFlagBits::eColorAttachmentOutput,
      vk::PipelineStageFlagBits::eComputeShader,
      {}, {}, {}, barrier);
  }

  // Transition depth-stencil to read-only for stencil sampling in compute
  {
    vk::ImageMemoryBarrier dsBarrier{};
    dsBarrier.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    dsBarrier.newLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
    dsBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dsBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dsBarrier.image = m_depth_stencil_image;
    dsBarrier.subresourceRange.aspectMask =
      vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    dsBarrier.subresourceRange.baseMipLevel = 0;
    dsBarrier.subresourceRange.levelCount = 1;
    dsBarrier.subresourceRange.baseArrayLayer = 0;
    dsBarrier.subresourceRange.layerCount = 1;
    dsBarrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    dsBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cmd.pipelineBarrier(
      vk::PipelineStageFlagBits::eLateFragmentTests,
      vk::PipelineStageFlagBits::eComputeShader,
      {}, {}, {}, dsBarrier);
  }

  uint32_t groupsX = (w + 15) / 16;
  uint32_t groupsY = (h + 15) / 16;

  struct BlurPushConstants
  {
    float blurWidthR;
    float blurWidthG;
    float blurWidthB;
    int direction;
  } pc{};
  pc.blurWidthR = *m_blur_width_r;
  pc.blurWidthG = *m_blur_width_g;
  pc.blurWidthB = *m_blur_width_b;

  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_pipeline);

  // Pass 1: Horizontal (HDR -> ping)
  pc.direction = 0;
  cmd.pushConstants(m_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
    static_cast<uint32_t>(sizeof(pc)), &pc);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_pipeline_layout, 0, m_h_descriptor, {});
  cmd.dispatch(groupsX, groupsY, 1);

  // Memory barrier between H and V passes
  {
    vk::MemoryBarrier memBarrier{};
    memBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    memBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    cmd.pipelineBarrier(
      vk::PipelineStageFlagBits::eComputeShader,
      vk::PipelineStageFlagBits::eComputeShader,
      {}, memBarrier, {}, {});
  }

  // Pass 2: Vertical (ping -> HDR)
  pc.direction = 1;
  cmd.pushConstants(m_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
    static_cast<uint32_t>(sizeof(pc)), &pc);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_pipeline_layout, 0, m_v_descriptor, {});
  cmd.dispatch(groupsX, groupsY, 1);

  // Transition HDR back to ShaderReadOnlyOptimal for composite sampling
  {
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eGeneral;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_hdr_image;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cmd.pipelineBarrier(
      vk::PipelineStageFlagBits::eComputeShader,
      vk::PipelineStageFlagBits::eFragmentShader,
      {}, {}, {}, barrier);
  }

  // No need to transition depth-stencil back — scene render pass has
  // initialLayout = eUndefined which discards old content
}

} // namespace sps::vulkan
