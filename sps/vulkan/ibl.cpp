#include <sps/vulkan/ibl.h>
#include <sps/vulkan/buffer.h>
#include <sps/vulkan/device.h>

#include <spdlog/spdlog.h>
#include <stb_image.h>

#include <cmath>
#include <stdexcept>

namespace sps::vulkan
{

namespace
{

// Helper to transition image layout
void transition_image_layout(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout old_layout,
  vk::ImageLayout new_layout, uint32_t mip_levels, uint32_t layer_count,
  vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor)
{
  vk::ImageMemoryBarrier barrier{};
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = aspect;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = mip_levels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = layer_count;

  vk::PipelineStageFlags src_stage;
  vk::PipelineStageFlags dst_stage;

  if (old_layout == vk::ImageLayout::eUndefined &&
      new_layout == vk::ImageLayout::eTransferDstOptimal)
  {
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
    src_stage = vk::PipelineStageFlagBits::eTopOfPipe;
    dst_stage = vk::PipelineStageFlagBits::eTransfer;
  }
  else if (old_layout == vk::ImageLayout::eTransferDstOptimal &&
           new_layout == vk::ImageLayout::eShaderReadOnlyOptimal)
  {
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    src_stage = vk::PipelineStageFlagBits::eTransfer;
    dst_stage = vk::PipelineStageFlagBits::eFragmentShader;
  }
  else
  {
    // Generic transition
    barrier.srcAccessMask = vk::AccessFlagBits::eMemoryWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eShaderRead;
    src_stage = vk::PipelineStageFlagBits::eAllCommands;
    dst_stage = vk::PipelineStageFlagBits::eAllCommands;
  }

  cmd.pipelineBarrier(src_stage, dst_stage, {}, {}, {}, barrier);
}

// Create command buffer, begin recording
vk::CommandBuffer begin_single_time_commands(const Device& device, vk::CommandPool pool)
{
  vk::CommandBufferAllocateInfo alloc_info{};
  alloc_info.commandPool = pool;
  alloc_info.level = vk::CommandBufferLevel::ePrimary;
  alloc_info.commandBufferCount = 1;

  auto cmd_buffers = device.device().allocateCommandBuffers(alloc_info);
  vk::CommandBuffer cmd = cmd_buffers[0];

  vk::CommandBufferBeginInfo begin_info{};
  begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
  cmd.begin(begin_info);

  return cmd;
}

// End recording, submit and wait
void end_single_time_commands(
  const Device& device, vk::CommandPool pool, vk::CommandBuffer cmd)
{
  cmd.end();

  vk::SubmitInfo submit_info{};
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &cmd;

  device.graphics_queue().submit(submit_info, nullptr);
  device.graphics_queue().waitIdle();

  device.device().freeCommandBuffers(pool, cmd);
}

} // namespace

// CPU-based BRDF LUT generation (split-sum approximation)
// Reference: https://learnopengl.com/PBR/IBL/Specular-IBL
std::vector<uint8_t> generate_brdf_lut_cpu(uint32_t size)
{
  std::vector<uint8_t> data(size * size * 4); // RGBA

  auto radical_inverse_vdc = [](uint32_t bits) -> float {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
  };

  auto hammersley = [&](uint32_t i, uint32_t n) -> std::pair<float, float> {
    return { static_cast<float>(i) / static_cast<float>(n), radical_inverse_vdc(i) };
  };

  auto importance_sample_ggx =
    [](std::pair<float, float> xi, float roughness) -> std::array<float, 3> {
    float a = roughness * roughness;
    float phi = 2.0f * 3.14159265359f * xi.first;
    float cos_theta = std::sqrt((1.0f - xi.second) / (1.0f + (a * a - 1.0f) * xi.second));
    float sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);

    return { std::cos(phi) * sin_theta, std::sin(phi) * sin_theta, cos_theta };
  };

  constexpr uint32_t SAMPLE_COUNT = 256;  // Reduced from 1024 for faster CPU generation

  for (uint32_t y = 0; y < size; ++y)
  {
    float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
    roughness = std::max(roughness, 0.001f); // Avoid zero roughness

    for (uint32_t x = 0; x < size; ++x)
    {
      float NdotV = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
      NdotV = std::max(NdotV, 0.001f);

      std::array<float, 3> V = { std::sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV };

      float A = 0.0f;
      float B = 0.0f;

      for (uint32_t i = 0; i < SAMPLE_COUNT; ++i)
      {
        auto xi = hammersley(i, SAMPLE_COUNT);
        auto H = importance_sample_ggx(xi, roughness);

        // L = reflect(-V, H) = 2 * dot(V, H) * H - V
        float VdotH = V[0] * H[0] + V[1] * H[1] + V[2] * H[2];
        VdotH = std::max(VdotH, 0.0f);

        std::array<float, 3> L = { 2.0f * VdotH * H[0] - V[0], 2.0f * VdotH * H[1] - V[1],
          2.0f * VdotH * H[2] - V[2] };

        float NdotL = std::max(L[2], 0.0f);
        float NdotH = std::max(H[2], 0.0f);

        if (NdotL > 0.0f)
        {
          // Geometry function (Smith GGX)
          float a2 = roughness * roughness * roughness * roughness;
          float G_V = NdotV / (NdotV * (1.0f - std::sqrt(a2 / 2.0f)) + std::sqrt(a2 / 2.0f));
          float G_L = NdotL / (NdotL * (1.0f - std::sqrt(a2 / 2.0f)) + std::sqrt(a2 / 2.0f));
          float G = G_V * G_L;

          float G_Vis = (G * VdotH) / (NdotH * NdotV);
          float Fc = std::pow(1.0f - VdotH, 5.0f);

          A += (1.0f - Fc) * G_Vis;
          B += Fc * G_Vis;
        }
      }

      A /= static_cast<float>(SAMPLE_COUNT);
      B /= static_cast<float>(SAMPLE_COUNT);

      uint32_t idx = (y * size + x) * 4;
      data[idx + 0] = static_cast<uint8_t>(std::clamp(A * 255.0f, 0.0f, 255.0f));
      data[idx + 1] = static_cast<uint8_t>(std::clamp(B * 255.0f, 0.0f, 255.0f));
      data[idx + 2] = 0;
      data[idx + 3] = 255;
    }
  }

  spdlog::info("Generated BRDF LUT ({}x{})", size, size);
  return data;
}

IBL::IBL(const Device& device)
  : m_device(device)
  , m_resolution(64)
  , m_mip_levels(1)
{
  spdlog::info("Creating default neutral IBL environment");
  create_default_environment();
  generate_brdf_lut();
}

IBL::IBL(const Device& device, const std::string& hdr_path, uint32_t resolution)
  : m_device(device)
  , m_resolution(resolution)
  , m_mip_levels(static_cast<uint32_t>(std::floor(std::log2(resolution))) + 1)
{
  spdlog::info("Creating IBL from HDR: {} (resolution: {}, mips: {})", hdr_path, resolution,
    m_mip_levels);

  load_hdr_environment(hdr_path);
  create_cubemap_from_equirectangular();
  generate_irradiance_map();
  generate_prefiltered_map();
  generate_brdf_lut();
}

IBL::~IBL()
{
  auto dev = m_device.device();

  // BRDF LUT cleanup
  if (m_brdf_lut_sampler)
    dev.destroySampler(m_brdf_lut_sampler);
  if (m_brdf_lut_view)
    dev.destroyImageView(m_brdf_lut_view);
  if (m_brdf_lut_image)
    dev.destroyImage(m_brdf_lut_image);
  if (m_brdf_lut_memory)
    dev.freeMemory(m_brdf_lut_memory);

  // Irradiance cleanup
  if (m_irradiance_sampler)
    dev.destroySampler(m_irradiance_sampler);
  if (m_irradiance_view)
    dev.destroyImageView(m_irradiance_view);
  if (m_irradiance_image)
    dev.destroyImage(m_irradiance_image);
  if (m_irradiance_memory)
    dev.freeMemory(m_irradiance_memory);

  // Pre-filtered cleanup
  if (m_prefiltered_sampler)
    dev.destroySampler(m_prefiltered_sampler);
  if (m_prefiltered_view)
    dev.destroyImageView(m_prefiltered_view);
  if (m_prefiltered_image)
    dev.destroyImage(m_prefiltered_image);
  if (m_prefiltered_memory)
    dev.freeMemory(m_prefiltered_memory);

  // HDR source cleanup
  if (m_hdr_view)
    dev.destroyImageView(m_hdr_view);
  if (m_hdr_image)
    dev.destroyImage(m_hdr_image);
  if (m_hdr_memory)
    dev.freeMemory(m_hdr_memory);

  spdlog::trace("IBL resources destroyed");
}

void IBL::generate_brdf_lut()
{
  constexpr uint32_t LUT_SIZE = 128;  // Reduced from 512 for faster CPU generation

  auto dev = m_device.device();

  // Generate LUT data on CPU
  auto lut_data = generate_brdf_lut_cpu(LUT_SIZE);

  // Create image
  vk::ImageCreateInfo image_info{};
  image_info.imageType = vk::ImageType::e2D;
  image_info.format = vk::Format::eR8G8B8A8Unorm;
  image_info.extent = vk::Extent3D{ LUT_SIZE, LUT_SIZE, 1 };
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = vk::SampleCountFlagBits::e1;
  image_info.tiling = vk::ImageTiling::eOptimal;
  image_info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
  image_info.sharingMode = vk::SharingMode::eExclusive;
  image_info.initialLayout = vk::ImageLayout::eUndefined;

  m_brdf_lut_image = dev.createImage(image_info);

  // Allocate memory
  auto mem_reqs = dev.getImageMemoryRequirements(m_brdf_lut_image);
  vk::MemoryAllocateInfo alloc_info{};
  alloc_info.allocationSize = mem_reqs.size;
  alloc_info.memoryTypeIndex =
    m_device.find_memory_type(mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_brdf_lut_memory = dev.allocateMemory(alloc_info);
  dev.bindImageMemory(m_brdf_lut_image, m_brdf_lut_memory, 0);

  // Create staging buffer and upload
  Buffer staging(m_device, "BRDF LUT staging", lut_data.size(),
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  staging.update(lut_data.data(), lut_data.size());

  // Create command pool and buffer
  vk::CommandPoolCreateInfo pool_info{};
  pool_info.queueFamilyIndex = m_device.m_graphics_queue_family_index;
  pool_info.flags = vk::CommandPoolCreateFlagBits::eTransient;
  vk::CommandPool cmd_pool = dev.createCommandPool(pool_info);

  auto cmd = begin_single_time_commands(m_device, cmd_pool);

  // Transition and copy
  transition_image_layout(
    cmd, m_brdf_lut_image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1, 1);

  vk::BufferImageCopy region{};
  region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageExtent = vk::Extent3D{ LUT_SIZE, LUT_SIZE, 1 };

  cmd.copyBufferToImage(
    staging.buffer(), m_brdf_lut_image, vk::ImageLayout::eTransferDstOptimal, region);

  transition_image_layout(cmd, m_brdf_lut_image, vk::ImageLayout::eTransferDstOptimal,
    vk::ImageLayout::eShaderReadOnlyOptimal, 1, 1);

  end_single_time_commands(m_device, cmd_pool, cmd);
  dev.destroyCommandPool(cmd_pool);

  // Create image view
  vk::ImageViewCreateInfo view_info{};
  view_info.image = m_brdf_lut_image;
  view_info.viewType = vk::ImageViewType::e2D;
  view_info.format = vk::Format::eR8G8B8A8Unorm;
  view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 1;

  m_device.create_image_view(view_info, &m_brdf_lut_view, "BRDF LUT view");

  // Create sampler
  vk::SamplerCreateInfo sampler_info{};
  sampler_info.magFilter = vk::Filter::eLinear;
  sampler_info.minFilter = vk::Filter::eLinear;
  sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
  sampler_info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.maxLod = 1.0f;

  m_brdf_lut_sampler = dev.createSampler(sampler_info);

  spdlog::trace("BRDF LUT created ({}x{})", LUT_SIZE, LUT_SIZE);
}

void IBL::load_hdr_environment(const std::string& hdr_path)
{
  // Load HDR image using stb_image
  int width, height, channels;
  float* hdr_data = stbi_loadf(hdr_path.c_str(), &width, &height, &channels, 4);

  if (!hdr_data)
  {
    throw std::runtime_error("Failed to load HDR environment: " + hdr_path);
  }

  spdlog::info("Loaded HDR: {}x{} (channels: {})", width, height, channels);

  // Store in CPU memory for cubemap conversion
  m_hdr_width = static_cast<uint32_t>(width);
  m_hdr_height = static_cast<uint32_t>(height);
  m_hdr_data.resize(width * height * 4);
  std::memcpy(m_hdr_data.data(), hdr_data, width * height * 4 * sizeof(float));

  stbi_image_free(hdr_data);
}

void IBL::create_default_environment()
{
  // Create minimal irradiance and prefiltered maps with neutral gray
  auto dev = m_device.device();

  // Small gray cubemap (32x32 per face)
  constexpr uint32_t CUBE_SIZE = 32;

  vk::ImageCreateInfo image_info{};
  image_info.imageType = vk::ImageType::e2D;
  image_info.format = vk::Format::eR8G8B8A8Unorm;
  image_info.extent = vk::Extent3D{ CUBE_SIZE, CUBE_SIZE, 1 };
  image_info.mipLevels = 1;
  image_info.arrayLayers = 6; // Cubemap
  image_info.samples = vk::SampleCountFlagBits::e1;
  image_info.tiling = vk::ImageTiling::eOptimal;
  image_info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
  image_info.sharingMode = vk::SharingMode::eExclusive;
  image_info.initialLayout = vk::ImageLayout::eUndefined;
  image_info.flags = vk::ImageCreateFlagBits::eCubeCompatible;

  // Create both cubemap images
  m_irradiance_image = dev.createImage(image_info);
  m_prefiltered_image = dev.createImage(image_info);

  // Allocate memory for irradiance
  auto irr_mem_reqs = dev.getImageMemoryRequirements(m_irradiance_image);
  vk::MemoryAllocateInfo irr_alloc{};
  irr_alloc.allocationSize = irr_mem_reqs.size;
  irr_alloc.memoryTypeIndex =
    m_device.find_memory_type(irr_mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
  m_irradiance_memory = dev.allocateMemory(irr_alloc);
  dev.bindImageMemory(m_irradiance_image, m_irradiance_memory, 0);

  // Allocate memory for prefiltered
  auto pf_mem_reqs = dev.getImageMemoryRequirements(m_prefiltered_image);
  vk::MemoryAllocateInfo pf_alloc{};
  pf_alloc.allocationSize = pf_mem_reqs.size;
  pf_alloc.memoryTypeIndex =
    m_device.find_memory_type(pf_mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
  m_prefiltered_memory = dev.allocateMemory(pf_alloc);
  dev.bindImageMemory(m_prefiltered_image, m_prefiltered_memory, 0);

  // Create neutral gray pixel data for all 6 faces
  std::vector<uint8_t> gray_data(CUBE_SIZE * CUBE_SIZE * 4 * 6);
  for (size_t i = 0; i < gray_data.size(); i += 4)
  {
    gray_data[i + 0] = 128; // R
    gray_data[i + 1] = 128; // G
    gray_data[i + 2] = 128; // B
    gray_data[i + 3] = 255; // A
  }

  Buffer staging(m_device, "Cubemap staging", gray_data.size(),
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  staging.update(gray_data.data(), gray_data.size());

  // Setup copy regions for 6 faces
  std::vector<vk::BufferImageCopy> regions(6);
  for (uint32_t face = 0; face < 6; ++face)
  {
    regions[face].bufferOffset = face * CUBE_SIZE * CUBE_SIZE * 4;
    regions[face].imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    regions[face].imageSubresource.mipLevel = 0;
    regions[face].imageSubresource.baseArrayLayer = face;
    regions[face].imageSubresource.layerCount = 1;
    regions[face].imageExtent = vk::Extent3D{ CUBE_SIZE, CUBE_SIZE, 1 };
  }

  // Single command buffer for all uploads
  vk::CommandPoolCreateInfo pool_info{};
  pool_info.queueFamilyIndex = m_device.m_graphics_queue_family_index;
  pool_info.flags = vk::CommandPoolCreateFlagBits::eTransient;
  vk::CommandPool cmd_pool = dev.createCommandPool(pool_info);
  auto cmd = begin_single_time_commands(m_device, cmd_pool);

  // Upload irradiance cubemap
  transition_image_layout(cmd, m_irradiance_image, vk::ImageLayout::eUndefined,
    vk::ImageLayout::eTransferDstOptimal, 1, 6);
  cmd.copyBufferToImage(staging.buffer(), m_irradiance_image,
    vk::ImageLayout::eTransferDstOptimal, regions);
  transition_image_layout(cmd, m_irradiance_image, vk::ImageLayout::eTransferDstOptimal,
    vk::ImageLayout::eShaderReadOnlyOptimal, 1, 6);

  // Upload prefiltered cubemap
  transition_image_layout(cmd, m_prefiltered_image, vk::ImageLayout::eUndefined,
    vk::ImageLayout::eTransferDstOptimal, 1, 6);
  cmd.copyBufferToImage(staging.buffer(), m_prefiltered_image,
    vk::ImageLayout::eTransferDstOptimal, regions);
  transition_image_layout(cmd, m_prefiltered_image, vk::ImageLayout::eTransferDstOptimal,
    vk::ImageLayout::eShaderReadOnlyOptimal, 1, 6);

  end_single_time_commands(m_device, cmd_pool, cmd);
  dev.destroyCommandPool(cmd_pool);

  // Create views and samplers
  vk::ImageViewCreateInfo view_info{};
  view_info.viewType = vk::ImageViewType::eCube;
  view_info.format = vk::Format::eR8G8B8A8Unorm;
  view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 6;

  view_info.image = m_irradiance_image;
  m_device.create_image_view(view_info, &m_irradiance_view, "Irradiance cubemap view");

  view_info.image = m_prefiltered_image;
  m_device.create_image_view(view_info, &m_prefiltered_view, "Prefiltered cubemap view");

  vk::SamplerCreateInfo sampler_info{};
  sampler_info.magFilter = vk::Filter::eLinear;
  sampler_info.minFilter = vk::Filter::eLinear;
  sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
  sampler_info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.maxLod = 1.0f;

  m_irradiance_sampler = dev.createSampler(sampler_info);
  m_prefiltered_sampler = dev.createSampler(sampler_info);

  spdlog::trace("Default IBL environment created");
}

namespace
{
// Sample equirectangular map at direction
inline void sample_equirect(const std::vector<float>& hdr, uint32_t w, uint32_t h,
                            float dx, float dy, float dz, float& r, float& g, float& b)
{
  // Normalize direction
  float len = std::sqrt(dx * dx + dy * dy + dz * dz);
  dx /= len; dy /= len; dz /= len;

  // Convert to spherical coordinates
  float theta = std::atan2(dz, dx);  // [-PI, PI]
  float phi = std::asin(dy);          // [-PI/2, PI/2]

  // Convert to UV
  float u = (theta + 3.14159265359f) / (2.0f * 3.14159265359f);
  float v = (phi + 3.14159265359f / 2.0f) / 3.14159265359f;

  // Sample with bilinear filtering
  float fx = u * (w - 1);
  float fy = (1.0f - v) * (h - 1);  // Flip V for top-down
  int x0 = static_cast<int>(fx);
  int y0 = static_cast<int>(fy);
  int x1 = std::min(x0 + 1, static_cast<int>(w - 1));
  int y1 = std::min(y0 + 1, static_cast<int>(h - 1));
  float tx = fx - x0;
  float ty = fy - y0;

  auto sample = [&](int x, int y) -> std::array<float, 3> {
    size_t idx = (y * w + x) * 4;
    return { hdr[idx], hdr[idx + 1], hdr[idx + 2] };
  };

  auto c00 = sample(x0, y0);
  auto c10 = sample(x1, y0);
  auto c01 = sample(x0, y1);
  auto c11 = sample(x1, y1);

  r = (c00[0] * (1 - tx) + c10[0] * tx) * (1 - ty) + (c01[0] * (1 - tx) + c11[0] * tx) * ty;
  g = (c00[1] * (1 - tx) + c10[1] * tx) * (1 - ty) + (c01[1] * (1 - tx) + c11[1] * tx) * ty;
  b = (c00[2] * (1 - tx) + c10[2] * tx) * (1 - ty) + (c01[2] * (1 - tx) + c11[2] * tx) * ty;
}

// Get direction for cubemap face and UV
inline void get_cube_direction(int face, float u, float v, float& dx, float& dy, float& dz)
{
  // Map UV from [0,1] to [-1,1]
  float uc = 2.0f * u - 1.0f;
  float vc = 2.0f * v - 1.0f;

  switch (face)
  {
    case 0: dx =  1; dy = -vc; dz = -uc; break;  // +X
    case 1: dx = -1; dy = -vc; dz =  uc; break;  // -X
    case 2: dx =  uc; dy =  1; dz =  vc; break;  // +Y
    case 3: dx =  uc; dy = -1; dz = -vc; break;  // -Y
    case 4: dx =  uc; dy = -vc; dz =  1; break;  // +Z
    case 5: dx = -uc; dy = -vc; dz = -1; break;  // -Z
  }
}
} // namespace

void IBL::create_cubemap_from_equirectangular()
{
  if (m_hdr_data.empty())
  {
    spdlog::warn("No HDR data loaded - using default environment");
    create_default_environment();
    return;
  }

  spdlog::info("Converting equirectangular HDR to cubemap ({}x{})", m_resolution, m_resolution);

  auto dev = m_device.device();
  const uint32_t CUBE_SIZE = m_resolution;

  // Generate cubemap data on CPU
  std::vector<float> cube_data(CUBE_SIZE * CUBE_SIZE * 4 * 6);

  for (int face = 0; face < 6; ++face)
  {
    for (uint32_t y = 0; y < CUBE_SIZE; ++y)
    {
      for (uint32_t x = 0; x < CUBE_SIZE; ++x)
      {
        float u = (x + 0.5f) / CUBE_SIZE;
        float v = (y + 0.5f) / CUBE_SIZE;

        float dx, dy, dz;
        get_cube_direction(face, u, v, dx, dy, dz);

        float r, g, b;
        sample_equirect(m_hdr_data, m_hdr_width, m_hdr_height, dx, dy, dz, r, g, b);

        size_t idx = (face * CUBE_SIZE * CUBE_SIZE + y * CUBE_SIZE + x) * 4;
        cube_data[idx + 0] = r;
        cube_data[idx + 1] = g;
        cube_data[idx + 2] = b;
        cube_data[idx + 3] = 1.0f;
      }
    }
  }

  // Create prefiltered cubemap image (we'll use this as our environment)
  vk::ImageCreateInfo image_info{};
  image_info.imageType = vk::ImageType::e2D;
  image_info.format = vk::Format::eR32G32B32A32Sfloat;
  image_info.extent = vk::Extent3D{ CUBE_SIZE, CUBE_SIZE, 1 };
  image_info.mipLevels = m_mip_levels;
  image_info.arrayLayers = 6;
  image_info.samples = vk::SampleCountFlagBits::e1;
  image_info.tiling = vk::ImageTiling::eOptimal;
  image_info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
  image_info.sharingMode = vk::SharingMode::eExclusive;
  image_info.initialLayout = vk::ImageLayout::eUndefined;
  image_info.flags = vk::ImageCreateFlagBits::eCubeCompatible;

  m_prefiltered_image = dev.createImage(image_info);

  auto mem_reqs = dev.getImageMemoryRequirements(m_prefiltered_image);
  vk::MemoryAllocateInfo alloc_info{};
  alloc_info.allocationSize = mem_reqs.size;
  alloc_info.memoryTypeIndex =
    m_device.find_memory_type(mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_prefiltered_memory = dev.allocateMemory(alloc_info);
  dev.bindImageMemory(m_prefiltered_image, m_prefiltered_memory, 0);

  // Upload mip level 0
  vk::DeviceSize data_size = CUBE_SIZE * CUBE_SIZE * 4 * 6 * sizeof(float);
  Buffer staging(m_device, "Cubemap staging", data_size, vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  staging.update(cube_data.data(), data_size);

  vk::CommandPoolCreateInfo pool_info{};
  pool_info.queueFamilyIndex = m_device.m_graphics_queue_family_index;
  pool_info.flags = vk::CommandPoolCreateFlagBits::eTransient;
  vk::CommandPool cmd_pool = dev.createCommandPool(pool_info);

  auto cmd = begin_single_time_commands(m_device, cmd_pool);

  transition_image_layout(cmd, m_prefiltered_image, vk::ImageLayout::eUndefined,
    vk::ImageLayout::eTransferDstOptimal, m_mip_levels, 6);

  std::vector<vk::BufferImageCopy> regions(6);
  for (uint32_t face = 0; face < 6; ++face)
  {
    regions[face].bufferOffset = face * CUBE_SIZE * CUBE_SIZE * 4 * sizeof(float);
    regions[face].imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    regions[face].imageSubresource.mipLevel = 0;
    regions[face].imageSubresource.baseArrayLayer = face;
    regions[face].imageSubresource.layerCount = 1;
    regions[face].imageExtent = vk::Extent3D{ CUBE_SIZE, CUBE_SIZE, 1 };
  }
  cmd.copyBufferToImage(staging.buffer(), m_prefiltered_image,
    vk::ImageLayout::eTransferDstOptimal, regions);

  // Leave in TransferDstOptimal — generate_prefiltered_map() will upload remaining
  // mip levels and do the final transition to ShaderReadOnlyOptimal

  end_single_time_commands(m_device, cmd_pool, cmd);
  dev.destroyCommandPool(cmd_pool);

  // Create cubemap view
  vk::ImageViewCreateInfo view_info{};
  view_info.image = m_prefiltered_image;
  view_info.viewType = vk::ImageViewType::eCube;
  view_info.format = vk::Format::eR32G32B32A32Sfloat;
  view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = m_mip_levels;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 6;

  m_device.create_image_view(view_info, &m_prefiltered_view, "Prefiltered cubemap view");

  // Create sampler with trilinear filtering for mip levels
  vk::SamplerCreateInfo sampler_info{};
  sampler_info.magFilter = vk::Filter::eLinear;
  sampler_info.minFilter = vk::Filter::eLinear;
  sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
  sampler_info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.maxLod = static_cast<float>(m_mip_levels);

  m_prefiltered_sampler = dev.createSampler(sampler_info);

  spdlog::info("Created prefiltered cubemap ({}x{}, {} mips)", CUBE_SIZE, CUBE_SIZE, m_mip_levels);
}

void IBL::generate_irradiance_map()
{
  if (m_hdr_data.empty())
  {
    spdlog::warn("No HDR data - skipping irradiance map generation");
    return;
  }

  spdlog::info("Generating irradiance map...");

  auto dev = m_device.device();
  constexpr uint32_t IRR_SIZE = 32;  // Irradiance can be low resolution
  constexpr float PI = 3.14159265359f;

  // Generate irradiance cubemap on CPU by convolving the environment
  std::vector<float> irr_data(IRR_SIZE * IRR_SIZE * 4 * 6, 0.0f);

  constexpr int SAMPLE_COUNT = 64;  // Samples per hemisphere

  for (int face = 0; face < 6; ++face)
  {
    for (uint32_t y = 0; y < IRR_SIZE; ++y)
    {
      for (uint32_t x = 0; x < IRR_SIZE; ++x)
      {
        float u = (x + 0.5f) / IRR_SIZE;
        float v = (y + 0.5f) / IRR_SIZE;

        float nx, ny, nz;
        get_cube_direction(face, u, v, nx, ny, nz);
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        nx /= len; ny /= len; nz /= len;

        // Build tangent space
        float upx = std::abs(ny) < 0.999f ? 0.0f : 1.0f;
        float upy = std::abs(ny) < 0.999f ? 1.0f : 0.0f;
        float upz = 0.0f;
        float tx = upy * nz - upz * ny;
        float ty = upz * nx - upx * nz;
        float tz = upx * ny - upy * nx;
        len = std::sqrt(tx * tx + ty * ty + tz * tz);
        tx /= len; ty /= len; tz /= len;
        float bx = ny * tz - nz * ty;
        float by = nz * tx - nx * tz;
        float bz = nx * ty - ny * tx;

        float ir = 0, ig = 0, ib = 0;

        // Uniform hemisphere sampling
        for (int i = 0; i < SAMPLE_COUNT; ++i)
        {
          float xi1 = static_cast<float>(i) / SAMPLE_COUNT;
          float xi2 = static_cast<float>((i * 7) % SAMPLE_COUNT) / SAMPLE_COUNT;

          float phi = 2.0f * PI * xi1;
          float cos_theta = std::sqrt(1.0f - xi2);  // Cosine-weighted
          float sin_theta = std::sqrt(xi2);

          // Tangent space direction
          float hx = std::cos(phi) * sin_theta;
          float hy = cos_theta;
          float hz = std::sin(phi) * sin_theta;

          // Transform to world space
          float dx = hx * tx + hy * nx + hz * bx;
          float dy = hx * ty + hy * ny + hz * by;
          float dz = hx * tz + hy * nz + hz * bz;

          float sr, sg, sb;
          sample_equirect(m_hdr_data, m_hdr_width, m_hdr_height, dx, dy, dz, sr, sg, sb);

          ir += sr;
          ig += sg;
          ib += sb;
        }

        ir = ir / SAMPLE_COUNT * PI;
        ig = ig / SAMPLE_COUNT * PI;
        ib = ib / SAMPLE_COUNT * PI;

        size_t idx = (face * IRR_SIZE * IRR_SIZE + y * IRR_SIZE + x) * 4;
        irr_data[idx + 0] = ir;
        irr_data[idx + 1] = ig;
        irr_data[idx + 2] = ib;
        irr_data[idx + 3] = 1.0f;
      }
    }
  }

  // Create irradiance cubemap
  vk::ImageCreateInfo image_info{};
  image_info.imageType = vk::ImageType::e2D;
  image_info.format = vk::Format::eR32G32B32A32Sfloat;
  image_info.extent = vk::Extent3D{ IRR_SIZE, IRR_SIZE, 1 };
  image_info.mipLevels = 1;
  image_info.arrayLayers = 6;
  image_info.samples = vk::SampleCountFlagBits::e1;
  image_info.tiling = vk::ImageTiling::eOptimal;
  image_info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
  image_info.sharingMode = vk::SharingMode::eExclusive;
  image_info.initialLayout = vk::ImageLayout::eUndefined;
  image_info.flags = vk::ImageCreateFlagBits::eCubeCompatible;

  m_irradiance_image = dev.createImage(image_info);

  auto mem_reqs = dev.getImageMemoryRequirements(m_irradiance_image);
  vk::MemoryAllocateInfo alloc_info{};
  alloc_info.allocationSize = mem_reqs.size;
  alloc_info.memoryTypeIndex =
    m_device.find_memory_type(mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

  m_irradiance_memory = dev.allocateMemory(alloc_info);
  dev.bindImageMemory(m_irradiance_image, m_irradiance_memory, 0);

  // Upload
  vk::DeviceSize data_size = IRR_SIZE * IRR_SIZE * 4 * 6 * sizeof(float);
  Buffer staging(m_device, "Irradiance staging", data_size, vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  staging.update(irr_data.data(), data_size);

  vk::CommandPoolCreateInfo pool_info{};
  pool_info.queueFamilyIndex = m_device.m_graphics_queue_family_index;
  pool_info.flags = vk::CommandPoolCreateFlagBits::eTransient;
  vk::CommandPool cmd_pool = dev.createCommandPool(pool_info);

  auto cmd = begin_single_time_commands(m_device, cmd_pool);

  transition_image_layout(cmd, m_irradiance_image, vk::ImageLayout::eUndefined,
    vk::ImageLayout::eTransferDstOptimal, 1, 6);

  std::vector<vk::BufferImageCopy> regions(6);
  for (uint32_t face = 0; face < 6; ++face)
  {
    regions[face].bufferOffset = face * IRR_SIZE * IRR_SIZE * 4 * sizeof(float);
    regions[face].imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    regions[face].imageSubresource.mipLevel = 0;
    regions[face].imageSubresource.baseArrayLayer = face;
    regions[face].imageSubresource.layerCount = 1;
    regions[face].imageExtent = vk::Extent3D{ IRR_SIZE, IRR_SIZE, 1 };
  }
  cmd.copyBufferToImage(staging.buffer(), m_irradiance_image,
    vk::ImageLayout::eTransferDstOptimal, regions);

  transition_image_layout(cmd, m_irradiance_image, vk::ImageLayout::eTransferDstOptimal,
    vk::ImageLayout::eShaderReadOnlyOptimal, 1, 6);

  end_single_time_commands(m_device, cmd_pool, cmd);
  dev.destroyCommandPool(cmd_pool);

  // Create view and sampler
  vk::ImageViewCreateInfo view_info{};
  view_info.image = m_irradiance_image;
  view_info.viewType = vk::ImageViewType::eCube;
  view_info.format = vk::Format::eR32G32B32A32Sfloat;
  view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 6;

  m_device.create_image_view(view_info, &m_irradiance_view, "Irradiance cubemap view");

  vk::SamplerCreateInfo sampler_info{};
  sampler_info.magFilter = vk::Filter::eLinear;
  sampler_info.minFilter = vk::Filter::eLinear;
  sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
  sampler_info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  sampler_info.maxLod = 1.0f;

  m_irradiance_sampler = dev.createSampler(sampler_info);

  spdlog::info("Created irradiance cubemap ({}x{})", IRR_SIZE, IRR_SIZE);
}

void IBL::generate_prefiltered_map()
{
  if (m_hdr_data.empty())
    return; // Default environment already fully initialized

  auto dev = m_device.device();
  constexpr float PI = 3.14159265359f;
  constexpr uint32_t SAMPLE_COUNT = 256;
  constexpr float MAX_REFLECTION_LOD = 4.0f; // Must match shader

  // Hammersley sequence for quasi-random sampling
  auto radical_inverse_vdc = [](uint32_t bits) -> float {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
  };

  auto hammersley = [&](uint32_t i, uint32_t n) -> std::pair<float, float> {
    return { static_cast<float>(i) / static_cast<float>(n), radical_inverse_vdc(i) };
  };

  spdlog::info("Generating prefiltered environment mip levels...");

  vk::CommandPoolCreateInfo pool_info{};
  pool_info.queueFamilyIndex = m_device.m_graphics_queue_family_index;
  pool_info.flags = vk::CommandPoolCreateFlagBits::eTransient;
  vk::CommandPool cmd_pool = dev.createCommandPool(pool_info);

  // Generate and upload each mip level (mip 0 already has the raw environment)
  for (uint32_t mip = 1; mip < m_mip_levels; ++mip)
  {
    // Roughness for this mip level (matches shader: LOD = roughness * MAX_REFLECTION_LOD)
    float roughness = std::min(1.0f, static_cast<float>(mip) / MAX_REFLECTION_LOD);
    uint32_t mip_size = std::max(1u, m_resolution >> mip);
    float alpha = roughness * roughness; // GGX alpha = perceptualRoughness^2

    spdlog::trace("  Mip {}: {}x{}, roughness={:.3f}", mip, mip_size, mip_size, roughness);

    std::vector<float> mip_data(mip_size * mip_size * 4 * 6);

    for (uint32_t face = 0; face < 6; ++face)
    {
      for (uint32_t y = 0; y < mip_size; ++y)
      {
        for (uint32_t x = 0; x < mip_size; ++x)
        {
          float u = (x + 0.5f) / mip_size;
          float v = (y + 0.5f) / mip_size;

          // Normal = View = Reflection direction for prefiltering
          float nx, ny, nz;
          get_cube_direction(face, u, v, nx, ny, nz);
          float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
          nx /= nlen; ny /= nlen; nz /= nlen;

          // Build tangent frame from N
          float upx, upy, upz;
          if (std::abs(ny) < 0.999f) { upx = 0; upy = 1; upz = 0; }
          else { upx = 1; upy = 0; upz = 0; }

          // T = normalize(cross(up, N))
          float tx = upy * nz - upz * ny;
          float ty = upz * nx - upx * nz;
          float tz = upx * ny - upy * nx;
          float tlen = std::sqrt(tx * tx + ty * ty + tz * tz);
          tx /= tlen; ty /= tlen; tz /= tlen;

          // B = cross(N, T)
          float bx = ny * tz - nz * ty;
          float by = nz * tx - nx * tz;
          float bz = nx * ty - ny * tx;

          float total_r = 0, total_g = 0, total_b = 0;
          float total_weight = 0;

          for (uint32_t s = 0; s < SAMPLE_COUNT; ++s)
          {
            auto xi = hammersley(s, SAMPLE_COUNT);

            // GGX importance sample half-vector in tangent space
            float phi = 2.0f * PI * xi.first;
            float cos_theta = std::sqrt(
              (1.0f - xi.second) / (1.0f + (alpha * alpha - 1.0f) * xi.second));
            float sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);

            float hx_t = std::cos(phi) * sin_theta;
            float hy_t = std::sin(phi) * sin_theta;
            float hz_t = cos_theta;

            // Transform half-vector to world space: H = hx*T + hy*B + hz*N
            float hwx = hx_t * tx + hy_t * bx + hz_t * nx;
            float hwy = hx_t * ty + hy_t * by + hz_t * ny;
            float hwz = hx_t * tz + hy_t * bz + hz_t * nz;

            // L = reflect(-N, H) = 2*(N·H)*H - N
            float NdotH = nx * hwx + ny * hwy + nz * hwz;
            float lx = 2.0f * NdotH * hwx - nx;
            float ly = 2.0f * NdotH * hwy - ny;
            float lz = 2.0f * NdotH * hwz - nz;

            float NdotL = nx * lx + ny * ly + nz * lz;

            if (NdotL > 0.0f)
            {
              float sr, sg, sb;
              sample_equirect(m_hdr_data, m_hdr_width, m_hdr_height, lx, ly, lz, sr, sg, sb);
              total_r += sr * NdotL;
              total_g += sg * NdotL;
              total_b += sb * NdotL;
              total_weight += NdotL;
            }
          }

          if (total_weight > 0.0f)
          {
            total_r /= total_weight;
            total_g /= total_weight;
            total_b /= total_weight;
          }

          size_t idx = (face * mip_size * mip_size + y * mip_size + x) * 4;
          mip_data[idx + 0] = total_r;
          mip_data[idx + 1] = total_g;
          mip_data[idx + 2] = total_b;
          mip_data[idx + 3] = 1.0f;
        }
      }
    }

    // Upload this mip level via staging buffer
    vk::DeviceSize data_size = mip_data.size() * sizeof(float);
    Buffer staging(m_device, "prefiltered mip " + std::to_string(mip), data_size,
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    staging.update(mip_data.data(), data_size);

    auto cmd = begin_single_time_commands(m_device, cmd_pool);

    std::vector<vk::BufferImageCopy> regions(6);
    for (uint32_t f = 0; f < 6; ++f)
    {
      regions[f].bufferOffset = f * mip_size * mip_size * 4 * sizeof(float);
      regions[f].imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
      regions[f].imageSubresource.mipLevel = mip;
      regions[f].imageSubresource.baseArrayLayer = f;
      regions[f].imageSubresource.layerCount = 1;
      regions[f].imageExtent = vk::Extent3D{ mip_size, mip_size, 1 };
    }

    cmd.copyBufferToImage(staging.buffer(), m_prefiltered_image,
      vk::ImageLayout::eTransferDstOptimal, regions);

    end_single_time_commands(m_device, cmd_pool, cmd);
  }

  // Final transition: all mip levels to ShaderReadOnlyOptimal
  {
    auto cmd = begin_single_time_commands(m_device, cmd_pool);
    transition_image_layout(cmd, m_prefiltered_image, vk::ImageLayout::eTransferDstOptimal,
      vk::ImageLayout::eShaderReadOnlyOptimal, m_mip_levels, 6);
    end_single_time_commands(m_device, cmd_pool, cmd);
  }

  dev.destroyCommandPool(cmd_pool);

  spdlog::info("Generated prefiltered environment ({} mip levels)", m_mip_levels);
}

} // namespace sps::vulkan
