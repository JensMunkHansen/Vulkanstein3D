#include <sps/vulkan/descriptor_builder.h>
#include <sps/vulkan/device.h>

#include <spdlog/spdlog.h>

#include <stdexcept>

namespace sps::vulkan
{

//-----------------------------------------------------------------------------
// ResourceDescriptor
//-----------------------------------------------------------------------------

ResourceDescriptor::ResourceDescriptor(const Device& device,
  std::vector<vk::DescriptorSetLayoutBinding>&& bindings,
  std::vector<vk::WriteDescriptorSet>&& writes,
  std::vector<vk::DescriptorBufferInfo>&& buffer_infos,
  std::vector<vk::DescriptorImageInfo>&& image_infos, std::string&& name)
  : m_device(&device)
  , m_name(std::move(name))
  , m_buffer_infos(std::move(buffer_infos))
  , m_image_infos(std::move(image_infos))
{
  // Count descriptor types for pool
  std::vector<vk::DescriptorPoolSize> pool_sizes;
  uint32_t uniform_count = 0;
  uint32_t sampler_count = 0;

  for (const auto& binding : bindings)
  {
    if (binding.descriptorType == vk::DescriptorType::eUniformBuffer)
    {
      uniform_count += binding.descriptorCount;
    }
    else if (binding.descriptorType == vk::DescriptorType::eCombinedImageSampler)
    {
      sampler_count += binding.descriptorCount;
    }
  }

  if (uniform_count > 0)
  {
    pool_sizes.push_back({ vk::DescriptorType::eUniformBuffer, uniform_count });
  }
  if (sampler_count > 0)
  {
    pool_sizes.push_back({ vk::DescriptorType::eCombinedImageSampler, sampler_count });
  }

  // Create descriptor pool
  vk::DescriptorPoolCreateInfo pool_info{};
  pool_info.maxSets = 1;
  pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
  pool_info.pPoolSizes = pool_sizes.data();

  m_pool = m_device->device().createDescriptorPool(pool_info);

  // Create descriptor set layout
  vk::DescriptorSetLayoutCreateInfo layout_info{};
  layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
  layout_info.pBindings = bindings.data();

  m_layout = m_device->device().createDescriptorSetLayout(layout_info);

  // Allocate descriptor set
  vk::DescriptorSetAllocateInfo alloc_info{};
  alloc_info.descriptorPool = m_pool;
  alloc_info.descriptorSetCount = 1;
  alloc_info.pSetLayouts = &m_layout;

  auto sets = m_device->device().allocateDescriptorSets(alloc_info);
  m_descriptor_set = sets[0];

  // Update write descriptor sets with correct pointers
  // (they were created with placeholder pointers in the builder)
  size_t buffer_idx = 0;
  size_t image_idx = 0;

  for (auto& write : writes)
  {
    write.dstSet = m_descriptor_set;

    if (write.descriptorType == vk::DescriptorType::eUniformBuffer)
    {
      write.pBufferInfo = &m_buffer_infos[buffer_idx++];
    }
    else if (write.descriptorType == vk::DescriptorType::eCombinedImageSampler)
    {
      write.pImageInfo = &m_image_infos[image_idx++];
    }
  }

  // Update descriptor sets
  m_device->device().updateDescriptorSets(writes, {});

  // Set debug names
  m_device->set_debug_name(reinterpret_cast<uint64_t>(static_cast<VkDescriptorPool>(m_pool)),
    vk::ObjectType::eDescriptorPool, m_name + " pool");
  m_device->set_debug_name(
    reinterpret_cast<uint64_t>(static_cast<VkDescriptorSetLayout>(m_layout)),
    vk::ObjectType::eDescriptorSetLayout, m_name + " layout");

  spdlog::trace("Created descriptor '{}'", m_name);
}

ResourceDescriptor::~ResourceDescriptor()
{
  if (m_device == nullptr)
  {
    return;
  }

  // Descriptor sets are freed when pool is destroyed
  if (m_layout)
  {
    m_device->device().destroyDescriptorSetLayout(m_layout);
    m_layout = VK_NULL_HANDLE;
  }

  if (m_pool)
  {
    m_device->device().destroyDescriptorPool(m_pool);
    m_pool = VK_NULL_HANDLE;
  }

  spdlog::trace("Destroyed descriptor '{}'", m_name);
}

ResourceDescriptor::ResourceDescriptor(ResourceDescriptor&& other) noexcept
  : m_device(other.m_device)
  , m_name(std::move(other.m_name))
  , m_pool(other.m_pool)
  , m_layout(other.m_layout)
  , m_descriptor_set(other.m_descriptor_set)
  , m_buffer_infos(std::move(other.m_buffer_infos))
  , m_image_infos(std::move(other.m_image_infos))
{
  other.m_device = nullptr;
  other.m_pool = VK_NULL_HANDLE;
  other.m_layout = VK_NULL_HANDLE;
  other.m_descriptor_set = VK_NULL_HANDLE;
}

ResourceDescriptor& ResourceDescriptor::operator=(ResourceDescriptor&& other) noexcept
{
  if (this != &other)
  {
    // Clean up existing
    if (m_device != nullptr)
    {
      if (m_layout)
      {
        m_device->device().destroyDescriptorSetLayout(m_layout);
      }
      if (m_pool)
      {
        m_device->device().destroyDescriptorPool(m_pool);
      }
    }

    // Move
    m_device = other.m_device;
    m_name = std::move(other.m_name);
    m_pool = other.m_pool;
    m_layout = other.m_layout;
    m_descriptor_set = other.m_descriptor_set;
    m_buffer_infos = std::move(other.m_buffer_infos);
    m_image_infos = std::move(other.m_image_infos);

    // Invalidate other
    other.m_device = nullptr;
    other.m_pool = VK_NULL_HANDLE;
    other.m_layout = VK_NULL_HANDLE;
    other.m_descriptor_set = VK_NULL_HANDLE;
  }
  return *this;
}

//-----------------------------------------------------------------------------
// DescriptorBuilder
//-----------------------------------------------------------------------------

DescriptorBuilder::DescriptorBuilder(const Device& device)
  : m_device(&device)
{
}

DescriptorBuilder& DescriptorBuilder::add_uniform_buffer(
  vk::Buffer buffer, uint32_t binding, vk::DeviceSize size, vk::ShaderStageFlags stage)
{
  // Add layout binding
  vk::DescriptorSetLayoutBinding layout_binding{};
  layout_binding.binding = binding;
  layout_binding.descriptorType = vk::DescriptorType::eUniformBuffer;
  layout_binding.descriptorCount = 1;
  layout_binding.stageFlags = stage;
  layout_binding.pImmutableSamplers = nullptr;
  m_bindings.push_back(layout_binding);

  // Add buffer info
  vk::DescriptorBufferInfo buffer_info{};
  buffer_info.buffer = buffer;
  buffer_info.offset = 0;
  buffer_info.range = size;
  m_buffer_infos.push_back(buffer_info);

  // Add write descriptor (dstSet and pBufferInfo set later in build)
  vk::WriteDescriptorSet write{};
  write.dstBinding = binding;
  write.dstArrayElement = 0;
  write.descriptorType = vk::DescriptorType::eUniformBuffer;
  write.descriptorCount = 1;
  m_writes.push_back(write);

  return *this;
}

DescriptorBuilder& DescriptorBuilder::add_combined_image_sampler(
  vk::ImageView image_view, vk::Sampler sampler, uint32_t binding, vk::ShaderStageFlags stage)
{
  // Add layout binding
  vk::DescriptorSetLayoutBinding layout_binding{};
  layout_binding.binding = binding;
  layout_binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
  layout_binding.descriptorCount = 1;
  layout_binding.stageFlags = stage;
  layout_binding.pImmutableSamplers = nullptr;
  m_bindings.push_back(layout_binding);

  // Add image info
  vk::DescriptorImageInfo image_info{};
  image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  image_info.imageView = image_view;
  image_info.sampler = sampler;
  m_image_infos.push_back(image_info);

  // Add write descriptor
  vk::WriteDescriptorSet write{};
  write.dstBinding = binding;
  write.dstArrayElement = 0;
  write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
  write.descriptorCount = 1;
  m_writes.push_back(write);

  return *this;
}

ResourceDescriptor DescriptorBuilder::build(std::string name)
{
  if (m_bindings.empty())
  {
    throw std::runtime_error("DescriptorBuilder: No bindings added");
  }

  return ResourceDescriptor(*m_device, std::move(m_bindings), std::move(m_writes),
    std::move(m_buffer_infos), std::move(m_image_infos), std::move(name));
}

} // namespace sps::vulkan
