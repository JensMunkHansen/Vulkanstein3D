#include <sps/vulkan/render_graph.h>
#include <sps/vulkan/device.h>
#include <sps/vulkan/renderer.h>
#include <sps/vulkan/stages/composite_stage.h>

#include <spdlog/spdlog.h>

#include <array>

namespace sps::vulkan
{

namespace
{

void begin_render_pass(const FrameContext& ctx, vk::RenderPass renderPass,
  vk::Framebuffer framebuffer, uint32_t clearCount,
  const vk::ClearValue* clearValues)
{
  vk::RenderPassBeginInfo rpInfo{};
  rpInfo.renderPass = renderPass;
  rpInfo.framebuffer = framebuffer;
  rpInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
  rpInfo.renderArea.extent = ctx.extent;
  rpInfo.clearValueCount = clearCount;
  rpInfo.pClearValues = clearValues;

  ctx.command_buffer.beginRenderPass(&rpInfo, vk::SubpassContents::eInline);

  // Set dynamic viewport
  vk::Viewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(ctx.extent.width);
  viewport.height = static_cast<float>(ctx.extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  ctx.command_buffer.setViewport(0, 1, &viewport);

  // Set dynamic scissor
  vk::Rect2D scissor{};
  scissor.offset = vk::Offset2D{ 0, 0 };
  scissor.extent = ctx.extent;
  ctx.command_buffer.setScissor(0, 1, &scissor);
}

} // anonymous namespace

RenderGraph::~RenderGraph()
{
  // Stages destroyed first — they may reference graph-owned images via descriptors.
  m_stages.clear();
  destroy_scene_framebuffers();
  destroy_material_pool();
  destroy_hdr_resources();

  if (m_hdr_sampler && m_renderer)
  {
    m_renderer->device().device().destroySampler(m_hdr_sampler);
    m_hdr_sampler = VK_NULL_HANDLE;
  }

  if (m_material_layout && m_renderer)
  {
    m_renderer->device().device().destroyDescriptorSetLayout(m_material_layout);
    m_material_layout = VK_NULL_HANDLE;
  }
}

void RenderGraph::create_material_descriptor_layout()
{
  // 12 bindings matching the material descriptor used by SceneManager:
  //   0: UBO (vertex + fragment)
  //   1-11: combined image samplers (fragment only)
  std::array<vk::DescriptorSetLayoutBinding, 12> bindings{};

  bindings[0].binding = 0;
  bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

  for (uint32_t i = 1; i <= 11; ++i)
  {
    bindings[i].binding = i;
    bindings[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    bindings[i].descriptorCount = 1;
    bindings[i].stageFlags = vk::ShaderStageFlagBits::eFragment;
  }

  vk::DescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  m_material_layout = m_renderer->device().device().createDescriptorSetLayout(layoutInfo);
  spdlog::info("Created graph-owned material descriptor layout (12 bindings)");
}

vk::DescriptorSetLayout RenderGraph::material_descriptor_layout() const
{
  return m_material_layout;
}

void RenderGraph::destroy_material_pool()
{
  if (m_material_pool && m_renderer)
  {
    m_renderer->device().device().destroyDescriptorPool(m_material_pool);
    m_material_pool = VK_NULL_HANDLE;
  }
  m_default_sets.clear();
  m_material_sets.clear();
}

void RenderGraph::allocate_material_descriptors(
  const MaterialTextureSet& default_textures,
  const std::vector<MaterialTextureSet>& material_textures,
  const std::vector<vk::DescriptorBufferInfo>& ubo_infos)
{
  auto dev = m_renderer->device().device();

  // Destroy old pool (implicitly frees all sets)
  destroy_material_pool();

  m_frames_in_flight = static_cast<uint32_t>(ubo_infos.size());
  const uint32_t mat_count = static_cast<uint32_t>(material_textures.size());
  const uint32_t sets_per_frame = 1 + mat_count; // 1 default + N materials
  const uint32_t total_sets = sets_per_frame * m_frames_in_flight;

  // Create pool
  std::array<vk::DescriptorPoolSize, 2> pool_sizes{};
  pool_sizes[0].type = vk::DescriptorType::eUniformBuffer;
  pool_sizes[0].descriptorCount = total_sets; // 1 UBO per set
  pool_sizes[1].type = vk::DescriptorType::eCombinedImageSampler;
  pool_sizes[1].descriptorCount = total_sets * MaterialTextureSet::TEXTURE_COUNT;

  vk::DescriptorPoolCreateInfo poolInfo{};
  poolInfo.maxSets = total_sets;
  poolInfo.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
  poolInfo.pPoolSizes = pool_sizes.data();

  m_material_pool = dev.createDescriptorPool(poolInfo);

  // Batch-allocate all sets
  std::vector<vk::DescriptorSetLayout> layouts(total_sets, m_material_layout);

  vk::DescriptorSetAllocateInfo allocInfo{};
  allocInfo.descriptorPool = m_material_pool;
  allocInfo.descriptorSetCount = total_sets;
  allocInfo.pSetLayouts = layouts.data();

  auto all_sets = dev.allocateDescriptorSets(allocInfo);

  // Distribute sets into the frame-indexed arrays
  m_default_sets.resize(m_frames_in_flight);
  m_material_sets.resize(m_frames_in_flight);

  uint32_t idx = 0;
  for (uint32_t f = 0; f < m_frames_in_flight; ++f)
  {
    m_default_sets[f] = all_sets[idx++];
    m_material_sets[f].resize(mat_count);
    for (uint32_t m = 0; m < mat_count; ++m)
    {
      m_material_sets[f][m] = all_sets[idx++];
    }
  }

  // Write all sets
  for (uint32_t f = 0; f < m_frames_in_flight; ++f)
  {
    write_material_set(m_default_sets[f], ubo_infos[f], default_textures);
    for (uint32_t m = 0; m < mat_count; ++m)
    {
      write_material_set(m_material_sets[f][m], ubo_infos[f], material_textures[m]);
    }
  }

  spdlog::info("Allocated {} material descriptor sets ({} frames x ({} materials + 1 default))",
    total_sets, m_frames_in_flight, mat_count);
}

void RenderGraph::write_material_set(vk::DescriptorSet set,
  const vk::DescriptorBufferInfo& ubo_info,
  const MaterialTextureSet& textures)
{
  // Build image infos for the 11 texture bindings
  std::array<vk::DescriptorImageInfo, MaterialTextureSet::TEXTURE_COUNT> image_infos{};
  for (uint32_t i = 0; i < MaterialTextureSet::TEXTURE_COUNT; ++i)
  {
    image_infos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    image_infos[i].imageView = textures.textures[i].view;
    image_infos[i].sampler = textures.textures[i].sampler;
  }

  // 12 writes: 1 UBO + 11 image samplers
  std::array<vk::WriteDescriptorSet, 12> writes{};

  // Binding 0: UBO
  writes[0].dstSet = set;
  writes[0].dstBinding = 0;
  writes[0].dstArrayElement = 0;
  writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
  writes[0].descriptorCount = 1;
  writes[0].pBufferInfo = &ubo_info;

  // Bindings 1-11: combined image samplers
  for (uint32_t i = 0; i < MaterialTextureSet::TEXTURE_COUNT; ++i)
  {
    writes[i + 1].dstSet = set;
    writes[i + 1].dstBinding = i + 1;
    writes[i + 1].dstArrayElement = 0;
    writes[i + 1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    writes[i + 1].descriptorCount = 1;
    writes[i + 1].pImageInfo = &image_infos[i];
  }

  m_renderer->device().device().updateDescriptorSets(writes, {});
}

vk::DescriptorSet RenderGraph::default_descriptor_set(uint32_t frame_index) const
{
  return m_default_sets[frame_index];
}

vk::DescriptorSet RenderGraph::material_descriptor_set(
  uint32_t frame_index, uint32_t material_index) const
{
  return m_material_sets[frame_index][material_index];
}

uint32_t RenderGraph::material_set_count() const
{
  return m_material_sets.empty() ? 0 : static_cast<uint32_t>(m_material_sets[0].size());
}

void RenderGraph::set_composite_stage(const CompositeStage* stage)
{
  m_composite_stage = stage;
}

void RenderGraph::record(const FrameContext& ctx)
{
  // Phase 1: PrePass stages (outside render pass)
  for (auto& stage : m_stages)
  {
    if (stage->phase() == Phase::PrePass && stage->is_enabled())
    {
      stage->record(ctx);
    }
  }

  // Phase 2: Scene render pass (HDR target)
  bool any_scene_stage = false;
  for (auto& stage : m_stages)
  {
    if (stage->phase() == Phase::ScenePass && stage->is_enabled())
    {
      any_scene_stage = true;
      break;
    }
  }

  if (any_scene_stage)
  {
    auto scene_rp = m_render_passes[static_cast<int>(Phase::ScenePass)];
    auto scene_fb = m_scene_framebuffers[ctx.image_index];

    // 3 clear values: color, depth, resolve (extra values ignored when not using MSAA)
    std::array<vk::ClearValue, 3> clearValues{};
    // Alpha=0: background pixels have no SSS blur (blur shader reads alpha as blur scale)
    clearValues[0].color = vk::ClearColorValue{
      std::array<float, 4>{ ctx.clear_color.r, ctx.clear_color.g, ctx.clear_color.b, 0.0f } };
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{ 1.0f, 0 };
    clearValues[2].color = vk::ClearColorValue{
      std::array<float, 4>{ ctx.clear_color.r, ctx.clear_color.g, ctx.clear_color.b, 0.0f } };

    begin_render_pass(ctx, scene_rp, scene_fb,
      static_cast<uint32_t>(clearValues.size()), clearValues.data());

    for (auto& stage : m_stages)
    {
      if (stage->phase() == Phase::ScenePass && stage->is_enabled())
      {
        stage->record(ctx);
      }
    }

    ctx.command_buffer.endRenderPass();
  }

  // Phase 3: Intermediate stages (outside render pass, e.g. compute blur)
  for (auto& stage : m_stages)
  {
    if (stage->phase() == Phase::Intermediate && stage->is_enabled())
    {
      stage->record(ctx);
    }
  }

  // Phase 4: Composite render pass (swapchain target)
  bool any_composite_stage = false;
  for (auto& stage : m_stages)
  {
    if (stage->phase() == Phase::CompositePass && stage->is_enabled())
    {
      any_composite_stage = true;
      break;
    }
  }

  if (any_composite_stage)
  {
    // Single clear value: swapchain color
    std::array<vk::ClearValue, 1> clearValues{};
    clearValues[0].color = vk::ClearColorValue{
      std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f } };

    begin_render_pass(ctx, m_render_passes[static_cast<int>(Phase::CompositePass)],
      m_composite_stage->framebuffer(ctx.image_index),
      static_cast<uint32_t>(clearValues.size()), clearValues.data());

    for (auto& stage : m_stages)
    {
      if (stage->phase() == Phase::CompositePass && stage->is_enabled())
      {
        stage->record(ctx);
      }
    }

    ctx.command_buffer.endRenderPass();
  }
}

void RenderGraph::on_swapchain_resize(const Device& device, vk::Extent2D extent)
{
  for (auto& stage : m_stages)
  {
    stage->on_swapchain_resize(device, extent);
  }
}

void RenderGraph::set_render_pass(Phase phase, vk::RenderPass rp)
{
  m_render_passes[static_cast<int>(phase)] = rp;
}

vk::RenderPass RenderGraph::render_pass(Phase phase) const
{
  return m_render_passes[static_cast<int>(phase)];
}

void RenderGraph::create_scene_framebuffers()
{
  vk::Extent2D extent = m_renderer->swapchain().extent();
  uint32_t count = m_renderer->swapchain().image_count();
  auto scene_rp = m_render_passes[static_cast<int>(Phase::ScenePass)];

  m_scene_framebuffers.resize(count);
  for (uint32_t i = 0; i < count; i++)
  {
    std::vector<vk::ImageView> attachments;
    if (m_renderer->msaa_samples() != vk::SampleCountFlagBits::e1)
    {
      // MSAA: [hdrMsaa, depth, hdrResolve]
      attachments = {
        m_hdr_msaa_image_view,
        m_renderer->depth_stencil().combined_view(),
        m_hdr_image_view
      };
    }
    else
    {
      // No MSAA: [hdr, depth]
      attachments = {
        m_hdr_image_view,
        m_renderer->depth_stencil().combined_view()
      };
    }

    vk::FramebufferCreateInfo fbInfo{};
    fbInfo.renderPass = scene_rp;
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = extent.width;
    fbInfo.height = extent.height;
    fbInfo.layers = 1;
    m_scene_framebuffers[i] = m_renderer->device().device().createFramebuffer(fbInfo);
  }
}

void RenderGraph::destroy_scene_framebuffers()
{
  if (!m_renderer || m_scene_framebuffers.empty())
    return;

  for (auto fb : m_scene_framebuffers)
    m_renderer->device().device().destroyFramebuffer(fb);
  m_scene_framebuffers.clear();
}

void RenderGraph::recreate_scene_framebuffers()
{
  destroy_scene_framebuffers();
  create_scene_framebuffers();
}

// ---------------------------------------------------------------------------
// HDR image resources
// ---------------------------------------------------------------------------

vk::Sampler RenderGraph::hdr_sampler() const
{
  return m_hdr_sampler;
}

void RenderGraph::create_hdr_resources()
{
  auto dev = m_renderer->device().device();
  vk::Extent2D extent = m_renderer->swapchain().extent();

  // HDR image (single-sample)
  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.extent.width = extent.width;
  imageInfo.extent.height = extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = m_hdr_format;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.initialLayout = vk::ImageLayout::eUndefined;
  imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled
    | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst;
  imageInfo.samples = vk::SampleCountFlagBits::e1;
  imageInfo.sharingMode = vk::SharingMode::eExclusive;

  m_hdr_image = dev.createImage(imageInfo);

  vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(m_hdr_image);
  vk::MemoryAllocateInfo allocInfo{};
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = m_renderer->device().find_memory_type(
    memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_hdr_image_memory = dev.allocateMemory(allocInfo);
  dev.bindImageMemory(m_hdr_image, m_hdr_image_memory, 0);

  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = m_hdr_image;
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.format = m_hdr_format;
  viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  m_hdr_image_view = dev.createImageView(viewInfo);

  // Sampler is immutable — create once, never destroy on resize
  if (!m_hdr_sampler)
  {
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    m_hdr_sampler = dev.createSampler(samplerInfo);
  }

  // Optional MSAA color target
  if (m_renderer->msaa_samples() != vk::SampleCountFlagBits::e1)
  {
    create_msaa_color_resources();
  }

  // Update shared image registry
  m_image_registry.set("hdr",
    { m_hdr_image, m_hdr_image_view, m_hdr_sampler, m_hdr_format });

  spdlog::trace("Created HDR image {}x{}", extent.width, extent.height);
}

void RenderGraph::create_msaa_color_resources()
{
  auto dev = m_renderer->device().device();
  vk::Extent2D extent = m_renderer->swapchain().extent();

  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.extent.width = extent.width;
  imageInfo.extent.height = extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = m_hdr_format;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.initialLayout = vk::ImageLayout::eUndefined;
  imageInfo.usage =
    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment;
  imageInfo.samples = m_renderer->msaa_samples();
  imageInfo.sharingMode = vk::SharingMode::eExclusive;

  m_hdr_msaa_image = dev.createImage(imageInfo);

  vk::MemoryRequirements memRequirements =
    dev.getImageMemoryRequirements(m_hdr_msaa_image);

  vk::MemoryAllocateInfo allocInfo{};
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = m_renderer->device().find_memory_type(
    memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_hdr_msaa_image_memory = dev.allocateMemory(allocInfo);
  dev.bindImageMemory(m_hdr_msaa_image, m_hdr_msaa_image_memory, 0);

  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = m_hdr_msaa_image;
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.format = m_hdr_format;
  viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  m_hdr_msaa_image_view = dev.createImageView(viewInfo);

  spdlog::trace("Created HDR MSAA color image {}x{} ({}x samples)", extent.width, extent.height,
    static_cast<int>(m_renderer->msaa_samples()));
}

void RenderGraph::destroy_hdr_resources()
{
  if (!m_renderer)
    return;

  auto dev = m_renderer->device().device();

  if (m_hdr_image_view)
  {
    dev.destroyImageView(m_hdr_image_view);
    m_hdr_image_view = VK_NULL_HANDLE;
  }
  if (m_hdr_image)
  {
    dev.destroyImage(m_hdr_image);
    m_hdr_image = VK_NULL_HANDLE;
  }
  if (m_hdr_image_memory)
  {
    dev.freeMemory(m_hdr_image_memory);
    m_hdr_image_memory = VK_NULL_HANDLE;
  }

  if (m_hdr_msaa_image_view)
  {
    dev.destroyImageView(m_hdr_msaa_image_view);
    m_hdr_msaa_image_view = VK_NULL_HANDLE;
  }
  if (m_hdr_msaa_image)
  {
    dev.destroyImage(m_hdr_msaa_image);
    m_hdr_msaa_image = VK_NULL_HANDLE;
  }
  if (m_hdr_msaa_image_memory)
  {
    dev.freeMemory(m_hdr_msaa_image_memory);
    m_hdr_msaa_image_memory = VK_NULL_HANDLE;
  }
}

void RenderGraph::recreate_hdr_resources()
{
  destroy_hdr_resources();
  create_hdr_resources();
}

} // namespace sps::vulkan
