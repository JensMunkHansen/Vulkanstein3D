#include <sps/vulkan/stages/ray_tracing_stage.h>

#include <spdlog/spdlog.h>
#include <sps/vulkan/acceleration_structure.h>
#include <sps/vulkan/buffer.h>
#include <sps/vulkan/config.h>
#include <sps/vulkan/device.h>
#include <sps/vulkan/gltf_loader.h>
#include <sps/vulkan/ibl.h>
#include <sps/vulkan/mesh.h>
#include <sps/vulkan/raytracing_pipeline.h>
#include <sps/vulkan/render_graph.h>
#include <sps/vulkan/renderer.h>
#include <sps/vulkan/texture.h>
#include <sps/vulkan/vertex.h>

namespace sps::vulkan
{

RayTracingStage::RayTracingStage(const VulkanRenderer& renderer, RenderGraph& graph,
  const bool* use_rt, vk::Buffer uniform_buffer)
  : RenderStage("RayTracingStage")
  , m_renderer(renderer)
  , m_graph(graph)
  , m_use_rt(use_rt)
  , m_uniform_buffer(uniform_buffer)
{
  m_graph.image_registry().declare_access("hdr", name(), phase(), AccessIntent::Write);

  update_from_registry();

  if (m_renderer.device().supports_ray_tracing())
  {
    create_storage_image();
  }
}

RayTracingStage::~RayTracingStage()
{
  auto dev = m_renderer.device().device();

  m_rt_pipeline.reset();
  m_tlas.reset();
  m_blas.reset();
  m_material_index_buffer.reset();
  m_fallback_texture.reset();

  destroy_storage_image();

  if (m_descriptor_pool)
    dev.destroyDescriptorPool(m_descriptor_pool);
  if (m_descriptor_layout)
    dev.destroyDescriptorSetLayout(m_descriptor_layout);
}

void RayTracingStage::update_from_registry()
{
  if (auto* hdr = m_graph.image_registry().get("hdr"))
    m_hdr_image = hdr->image;
}

void RayTracingStage::create_storage_image()
{
  auto dev = m_renderer.device().device();
  vk::Extent2D extent = m_renderer.swapchain().extent();

  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.format = vk::Format::eR8G8B8A8Unorm;
  imageInfo.extent.width = extent.width;
  imageInfo.extent.height = extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = vk::SampleCountFlagBits::e1;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc;
  imageInfo.initialLayout = vk::ImageLayout::eUndefined;

  m_rt_image = dev.createImage(imageInfo);

  vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(m_rt_image);

  vk::MemoryAllocateInfo allocInfo{};
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex =
    m_renderer.device().find_memory_type(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_rt_image_memory = dev.allocateMemory(allocInfo);
  dev.bindImageMemory(m_rt_image, m_rt_image_memory, 0);

  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = m_rt_image;
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.format = vk::Format::eR8G8B8A8Unorm;
  viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  m_rt_image_view = dev.createImageView(viewInfo);

  spdlog::trace("Created RT storage image {}x{}", extent.width, extent.height);
}

void RayTracingStage::destroy_storage_image()
{
  auto dev = m_renderer.device().device();

  if (m_rt_image_view)
    dev.destroyImageView(m_rt_image_view);
  if (m_rt_image)
    dev.destroyImage(m_rt_image);
  if (m_rt_image_memory)
    dev.freeMemory(m_rt_image_memory);

  m_rt_image_view = VK_NULL_HANDLE;
  m_rt_image = VK_NULL_HANDLE;
  m_rt_image_memory = VK_NULL_HANDLE;
}

void RayTracingStage::build_material_index_buffer(const Mesh& mesh, const GltfScene* scene)
{
  uint32_t triangleCount = mesh.index_count() / 3;
  std::vector<uint32_t> materialIndices(triangleCount, 0);

  if (scene)
  {
    for (const auto& prim : scene->primitives)
    {
      uint32_t startTriangle = prim.firstIndex / 3;
      uint32_t numTriangles = prim.indexCount / 3;
      for (uint32_t t = 0; t < numTriangles; t++)
      {
        materialIndices[startTriangle + t] = prim.materialIndex;
      }
    }
  }

  vk::DeviceSize bufferSize = triangleCount * sizeof(uint32_t);
  m_material_index_buffer = std::make_unique<Buffer>(m_renderer.device(),
    "RT material indices", bufferSize,
    vk::BufferUsageFlagBits::eStorageBuffer,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  m_material_index_buffer->update(materialIndices.data(), bufferSize);

  spdlog::trace("Built RT material index buffer: {} triangles", triangleCount);
}

void RayTracingStage::create_descriptor(const Mesh& mesh, const GltfScene* scene, const IBL* ibl)
{
  auto dev = m_renderer.device().device();

  // Ensure fallback texture exists
  if (!m_fallback_texture)
  {
    const uint8_t white[] = { 255, 255, 255, 255 };
    m_fallback_texture = std::make_unique<Texture>(m_renderer.device(), "RT fallback white", white, 1, 1);
  }

  // Determine texture count (at least 1 for fallback)
  m_texture_count = scene ? std::max(static_cast<uint32_t>(scene->materials.size()), 1u) : 1;

  // Create descriptor pool (+3 IBL: prefiltered env, irradiance, BRDF LUT)
  std::vector<vk::DescriptorPoolSize> poolSizes = {
    { vk::DescriptorType::eAccelerationStructureKHR, 1 },
    { vk::DescriptorType::eStorageImage, 1 },
    { vk::DescriptorType::eUniformBuffer, 1 },
    { vk::DescriptorType::eStorageBuffer, 3 },  // vertex + index + material index
    { vk::DescriptorType::eCombinedImageSampler, m_texture_count + 3 }  // +3 for IBL (prefiltered, irradiance, BRDF LUT)
  };

  vk::DescriptorPoolCreateInfo poolInfo{};
  poolInfo.maxSets = 1;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();

  m_descriptor_pool = dev.createDescriptorPool(poolInfo);

  // Create descriptor set layout
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
    // Binding 0: TLAS
    { 0, vk::DescriptorType::eAccelerationStructureKHR, 1,
      vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR },
    // Binding 1: Storage image
    { 1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR },
    // Binding 2: Uniform buffer (raygen + closesthit + miss for clear_color/ibl_params)
    { 2, vk::DescriptorType::eUniformBuffer, 1,
      vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR |
      vk::ShaderStageFlagBits::eMissKHR },
    // Binding 3: Vertex buffer
    { 3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR },
    // Binding 4: Index buffer
    { 4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR },
    // Binding 5: Material index buffer
    { 5, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR },
    // Binding 6: Base color textures (one per material)
    { 6, vk::DescriptorType::eCombinedImageSampler, m_texture_count,
      vk::ShaderStageFlagBits::eClosestHitKHR },
    // Binding 7: Prefiltered environment cubemap (miss background + closesthit specular IBL)
    { 7, vk::DescriptorType::eCombinedImageSampler, 1,
      vk::ShaderStageFlagBits::eMissKHR | vk::ShaderStageFlagBits::eClosestHitKHR },
    // Binding 8: Irradiance cubemap (closesthit diffuse IBL)
    { 8, vk::DescriptorType::eCombinedImageSampler, 1,
      vk::ShaderStageFlagBits::eClosestHitKHR },
    // Binding 9: BRDF LUT (closesthit specular IBL split-sum)
    { 9, vk::DescriptorType::eCombinedImageSampler, 1,
      vk::ShaderStageFlagBits::eClosestHitKHR }
  };

  vk::DescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  m_descriptor_layout = dev.createDescriptorSetLayout(layoutInfo);

  // Allocate descriptor set
  vk::DescriptorSetAllocateInfo allocInfo{};
  allocInfo.descriptorPool = m_descriptor_pool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &m_descriptor_layout;

  auto sets = dev.allocateDescriptorSets(allocInfo);
  m_descriptor_set = sets[0];

  // Build texture image infos (one per material, fallback for missing)
  std::vector<vk::DescriptorImageInfo> textureInfos(m_texture_count);
  for (uint32_t i = 0; i < m_texture_count; i++)
  {
    const Texture* tex = m_fallback_texture.get();
    if (scene && i < scene->materials.size() && scene->materials[i].baseColorTexture)
      tex = scene->materials[i].baseColorTexture.get();

    textureInfos[i].imageView = tex->image_view();
    textureInfos[i].sampler = tex->sampler();
    textureInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  }

  // IBL textures (prefiltered environment, irradiance, BRDF LUT)
  vk::DescriptorImageInfo envInfo{};
  vk::DescriptorImageInfo irradianceInfo{};
  vk::DescriptorImageInfo brdfLutInfo{};
  if (ibl)
  {
    envInfo.imageView = ibl->prefiltered_view();
    envInfo.sampler = ibl->prefiltered_sampler();
    irradianceInfo.imageView = ibl->irradiance_view();
    irradianceInfo.sampler = ibl->irradiance_sampler();
    brdfLutInfo.imageView = ibl->brdf_lut_view();
    brdfLutInfo.sampler = ibl->brdf_lut_sampler();
  }
  else
  {
    envInfo.imageView = m_fallback_texture->image_view();
    envInfo.sampler = m_fallback_texture->sampler();
    irradianceInfo.imageView = m_fallback_texture->image_view();
    irradianceInfo.sampler = m_fallback_texture->sampler();
    brdfLutInfo.imageView = m_fallback_texture->image_view();
    brdfLutInfo.sampler = m_fallback_texture->sampler();
  }
  envInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  irradianceInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  brdfLutInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

  // Update descriptor set
  vk::WriteDescriptorSetAccelerationStructureKHR asWrite{};
  asWrite.accelerationStructureCount = 1;
  vk::AccelerationStructureKHR tlas = m_tlas->handle();
  asWrite.pAccelerationStructures = &tlas;

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.imageView = m_rt_image_view;
  imageInfo.imageLayout = vk::ImageLayout::eGeneral;

  vk::DescriptorBufferInfo bufferInfo{};
  bufferInfo.buffer = m_uniform_buffer;
  bufferInfo.offset = 0;
  bufferInfo.range = VK_WHOLE_SIZE;

  vk::DescriptorBufferInfo vertexBufferInfo{};
  vertexBufferInfo.buffer = mesh.vertex_buffer();
  vertexBufferInfo.offset = 0;
  vertexBufferInfo.range = VK_WHOLE_SIZE;

  vk::DescriptorBufferInfo indexBufferInfo{};
  indexBufferInfo.buffer = mesh.index_buffer();
  indexBufferInfo.offset = 0;
  indexBufferInfo.range = VK_WHOLE_SIZE;

  vk::DescriptorBufferInfo materialIndexInfo{};
  materialIndexInfo.buffer = m_material_index_buffer->buffer();
  materialIndexInfo.offset = 0;
  materialIndexInfo.range = VK_WHOLE_SIZE;

  std::vector<vk::WriteDescriptorSet> writes(10);

  // TLAS
  writes[0].dstSet = m_descriptor_set;
  writes[0].dstBinding = 0;
  writes[0].descriptorCount = 1;
  writes[0].descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
  writes[0].pNext = &asWrite;

  // Storage image
  writes[1].dstSet = m_descriptor_set;
  writes[1].dstBinding = 1;
  writes[1].descriptorCount = 1;
  writes[1].descriptorType = vk::DescriptorType::eStorageImage;
  writes[1].pImageInfo = &imageInfo;

  // Uniform buffer
  writes[2].dstSet = m_descriptor_set;
  writes[2].dstBinding = 2;
  writes[2].descriptorCount = 1;
  writes[2].descriptorType = vk::DescriptorType::eUniformBuffer;
  writes[2].pBufferInfo = &bufferInfo;

  // Vertex buffer
  writes[3].dstSet = m_descriptor_set;
  writes[3].dstBinding = 3;
  writes[3].descriptorCount = 1;
  writes[3].descriptorType = vk::DescriptorType::eStorageBuffer;
  writes[3].pBufferInfo = &vertexBufferInfo;

  // Index buffer
  writes[4].dstSet = m_descriptor_set;
  writes[4].dstBinding = 4;
  writes[4].descriptorCount = 1;
  writes[4].descriptorType = vk::DescriptorType::eStorageBuffer;
  writes[4].pBufferInfo = &indexBufferInfo;

  // Material index buffer
  writes[5].dstSet = m_descriptor_set;
  writes[5].dstBinding = 5;
  writes[5].descriptorCount = 1;
  writes[5].descriptorType = vk::DescriptorType::eStorageBuffer;
  writes[5].pBufferInfo = &materialIndexInfo;

  // Base color textures
  writes[6].dstSet = m_descriptor_set;
  writes[6].dstBinding = 6;
  writes[6].descriptorCount = m_texture_count;
  writes[6].descriptorType = vk::DescriptorType::eCombinedImageSampler;
  writes[6].pImageInfo = textureInfos.data();

  // Prefiltered environment cubemap
  writes[7].dstSet = m_descriptor_set;
  writes[7].dstBinding = 7;
  writes[7].descriptorCount = 1;
  writes[7].descriptorType = vk::DescriptorType::eCombinedImageSampler;
  writes[7].pImageInfo = &envInfo;

  // Irradiance cubemap
  writes[8].dstSet = m_descriptor_set;
  writes[8].dstBinding = 8;
  writes[8].descriptorCount = 1;
  writes[8].descriptorType = vk::DescriptorType::eCombinedImageSampler;
  writes[8].pImageInfo = &irradianceInfo;

  // BRDF LUT
  writes[9].dstSet = m_descriptor_set;
  writes[9].dstBinding = 9;
  writes[9].descriptorCount = 1;
  writes[9].descriptorType = vk::DescriptorType::eCombinedImageSampler;
  writes[9].pImageInfo = &brdfLutInfo;

  dev.updateDescriptorSets(writes, {});

  spdlog::trace("Created RT descriptor set with {} textures + IBL", m_texture_count);
}

void RayTracingStage::create_pipeline()
{
  m_rt_pipeline = std::make_unique<RayTracingPipeline>(m_renderer.device());
  m_rt_pipeline->create(
    SHADER_DIR "raygen.spv",
    SHADER_DIR "miss.spv",
    SHADER_DIR "closesthit.spv",
    m_descriptor_layout,
    static_cast<uint32_t>(sizeof(Vertex) / sizeof(float)));
}

void RayTracingStage::build_acceleration_structures(const Mesh& mesh)
{
  if (!m_renderer.device().supports_ray_tracing())
  {
    spdlog::warn("Cannot build acceleration structures: RT not supported");
    return;
  }

  vk::CommandBufferAllocateInfo allocInfo{};
  allocInfo.commandPool = m_renderer.command_pool();
  allocInfo.level = vk::CommandBufferLevel::ePrimary;
  allocInfo.commandBufferCount = 1;

  auto cmdBuffers = m_renderer.device().device().allocateCommandBuffers(allocInfo);
  vk::CommandBuffer cmd = cmdBuffers[0];

  vk::CommandBufferBeginInfo beginInfo{};
  beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
  cmd.begin(beginInfo);

  // Build BLAS
  m_blas = std::make_unique<AccelerationStructure>(m_renderer.device(), "mesh BLAS");
  m_blas->build_blas(cmd, mesh);

  // Build TLAS
  m_tlas = std::make_unique<AccelerationStructure>(m_renderer.device(), "scene TLAS");
  std::vector<std::pair<const AccelerationStructure*, glm::mat4>> instances;
  instances.push_back({ m_blas.get(), glm::mat4(1.0f) });
  m_tlas->build_tlas(cmd, instances);

  cmd.end();

  vk::SubmitInfo submitInfo{};
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;

  m_renderer.device().graphics_queue().submit(submitInfo, nullptr);
  m_renderer.device().wait_idle();

  m_renderer.device().device().freeCommandBuffers(m_renderer.command_pool(), cmd);

  spdlog::trace("Built acceleration structures");
}

void RayTracingStage::on_mesh_changed(const Mesh& mesh, const GltfScene* scene, const IBL* ibl)
{
  auto dev = m_renderer.device().device();

  // Destroy old acceleration structures
  m_tlas.reset();
  m_blas.reset();

  // Rebuild
  build_acceleration_structures(mesh);
  build_material_index_buffer(mesh, scene);

  // Rebuild descriptor (pool is not reusable after free — destroy and recreate)
  if (m_descriptor_pool)
  {
    dev.destroyDescriptorPool(m_descriptor_pool);
    m_descriptor_pool = VK_NULL_HANDLE;
    m_descriptor_set = VK_NULL_HANDLE;
  }
  if (m_descriptor_layout)
  {
    dev.destroyDescriptorSetLayout(m_descriptor_layout);
    m_descriptor_layout = VK_NULL_HANDLE;
  }

  // Recreate pipeline needs new layout, so destroy it too
  m_rt_pipeline.reset();

  create_descriptor(mesh, scene, ibl);
  create_pipeline();
}

void RayTracingStage::update_environment(const IBL& ibl)
{
  if (!m_descriptor_set)
    return;

  vk::DescriptorImageInfo envInfo{};
  envInfo.imageView = ibl.prefiltered_view();
  envInfo.sampler = ibl.prefiltered_sampler();
  envInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

  vk::DescriptorImageInfo irradianceInfo{};
  irradianceInfo.imageView = ibl.irradiance_view();
  irradianceInfo.sampler = ibl.irradiance_sampler();
  irradianceInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

  vk::DescriptorImageInfo brdfLutInfo{};
  brdfLutInfo.imageView = ibl.brdf_lut_view();
  brdfLutInfo.sampler = ibl.brdf_lut_sampler();
  brdfLutInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

  std::array<vk::WriteDescriptorSet, 3> writes{};

  writes[0].dstSet = m_descriptor_set;
  writes[0].dstBinding = 7;
  writes[0].descriptorCount = 1;
  writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
  writes[0].pImageInfo = &envInfo;

  writes[1].dstSet = m_descriptor_set;
  writes[1].dstBinding = 8;
  writes[1].descriptorCount = 1;
  writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
  writes[1].pImageInfo = &irradianceInfo;

  writes[2].dstSet = m_descriptor_set;
  writes[2].dstBinding = 9;
  writes[2].descriptorCount = 1;
  writes[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
  writes[2].pImageInfo = &brdfLutInfo;

  m_renderer.device().device().updateDescriptorSets(writes, {});
}

void RayTracingStage::on_swapchain_resize(const Device& /*device*/, vk::Extent2D /*extent*/)
{
  if (!m_rt_image)
    return;

  update_from_registry();

  destroy_storage_image();
  create_storage_image();

  // Update descriptor binding 1 (storage image)
  if (m_descriptor_set)
  {
    vk::DescriptorImageInfo imageInfo{};
    imageInfo.imageView = m_rt_image_view;
    imageInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::WriteDescriptorSet write{};
    write.dstSet = m_descriptor_set;
    write.dstBinding = 1;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eStorageImage;
    write.pImageInfo = &imageInfo;

    m_renderer.device().device().updateDescriptorSets(write, {});
  }
}

bool RayTracingStage::is_enabled() const
{
  return *m_use_rt && m_renderer.device().supports_ray_tracing() && m_rt_pipeline != nullptr;
}

void RayTracingStage::record(const FrameContext& ctx)
{
  vk::Extent2D extent = ctx.extent;

  // 1. Transition HDR image to TransferDstOptimal for receiving blit
  vk::ImageMemoryBarrier hdrBarrier{};
  hdrBarrier.oldLayout = vk::ImageLayout::eUndefined;
  hdrBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
  hdrBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  hdrBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  hdrBarrier.image = m_hdr_image;
  hdrBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  hdrBarrier.subresourceRange.baseMipLevel = 0;
  hdrBarrier.subresourceRange.levelCount = 1;
  hdrBarrier.subresourceRange.baseArrayLayer = 0;
  hdrBarrier.subresourceRange.layerCount = 1;
  hdrBarrier.srcAccessMask = {};
  hdrBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

  ctx.command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
    vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, hdrBarrier);

  // 2. Transition RT storage image to General for writing
  vk::ImageMemoryBarrier rtBarrier{};
  rtBarrier.oldLayout = vk::ImageLayout::eUndefined;
  rtBarrier.newLayout = vk::ImageLayout::eGeneral;
  rtBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  rtBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  rtBarrier.image = m_rt_image;
  rtBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  rtBarrier.subresourceRange.baseMipLevel = 0;
  rtBarrier.subresourceRange.levelCount = 1;
  rtBarrier.subresourceRange.baseArrayLayer = 0;
  rtBarrier.subresourceRange.layerCount = 1;
  rtBarrier.srcAccessMask = {};
  rtBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;

  ctx.command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
    vk::PipelineStageFlagBits::eRayTracingShaderKHR, {}, {}, {}, rtBarrier);

  // 3. Bind RT pipeline and trace rays
  ctx.command_buffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, m_rt_pipeline->pipeline());
  ctx.command_buffer.bindDescriptorSets(
    vk::PipelineBindPoint::eRayTracingKHR, m_rt_pipeline->layout(), 0, m_descriptor_set, {});

  m_rt_pipeline->trace_rays(ctx.command_buffer, extent.width, extent.height);

  // 4. Transition RT storage image to TransferSrcOptimal for blit
  rtBarrier.oldLayout = vk::ImageLayout::eGeneral;
  rtBarrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
  rtBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
  rtBarrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

  ctx.command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eRayTracingShaderKHR,
    vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, rtBarrier);

  // 5. Blit RT image to HDR image
  vk::ImageBlit blitRegion{};
  blitRegion.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  blitRegion.srcSubresource.layerCount = 1;
  blitRegion.srcOffsets[0] = vk::Offset3D{ 0, 0, 0 };
  blitRegion.srcOffsets[1] =
    vk::Offset3D{ static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1 };
  blitRegion.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  blitRegion.dstSubresource.layerCount = 1;
  blitRegion.dstOffsets[0] = vk::Offset3D{ 0, 0, 0 };
  blitRegion.dstOffsets[1] =
    vk::Offset3D{ static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1 };

  ctx.command_buffer.blitImage(m_rt_image, vk::ImageLayout::eTransferSrcOptimal,
    m_hdr_image, vk::ImageLayout::eTransferDstOptimal, blitRegion,
    vk::Filter::eNearest);

  // 6. Transition HDR image to ShaderReadOnlyOptimal for composite pass sampling
  hdrBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
  hdrBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  hdrBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
  hdrBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

  ctx.command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
    vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, hdrBarrier);
}

} // namespace sps::vulkan
