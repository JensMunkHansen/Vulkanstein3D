#pragma once

#include <vulkan/vulkan.hpp>

#include <string>
#include <vector>

namespace sps::vulkan
{

class Device;

/// @brief RAII wrapper for descriptor set infrastructure.
///
/// Owns the descriptor pool, layout, and allocated descriptor sets.
class ResourceDescriptor
{
public:
  ResourceDescriptor(const Device& device, std::vector<vk::DescriptorSetLayoutBinding>&& bindings,
    std::vector<vk::WriteDescriptorSet>&& writes,
    std::vector<vk::DescriptorBufferInfo>&& buffer_infos,
    std::vector<vk::DescriptorImageInfo>&& image_infos, std::string&& name);

  ~ResourceDescriptor();

  // Non-copyable
  ResourceDescriptor(const ResourceDescriptor&) = delete;
  ResourceDescriptor& operator=(const ResourceDescriptor&) = delete;

  // Movable
  ResourceDescriptor(ResourceDescriptor&& other) noexcept;
  ResourceDescriptor& operator=(ResourceDescriptor&& other) noexcept;

  [[nodiscard]] vk::DescriptorSetLayout layout() const { return m_layout; }
  [[nodiscard]] vk::DescriptorSet descriptor_set() const { return m_descriptor_set; }
  [[nodiscard]] const std::string& name() const { return m_name; }

private:
  const Device* m_device{ nullptr };
  std::string m_name;

  vk::DescriptorPool m_pool{ VK_NULL_HANDLE };
  vk::DescriptorSetLayout m_layout{ VK_NULL_HANDLE };
  vk::DescriptorSet m_descriptor_set{ VK_NULL_HANDLE };

  // Keep these alive for the lifetime of the descriptor
  std::vector<vk::DescriptorBufferInfo> m_buffer_infos;
  std::vector<vk::DescriptorImageInfo> m_image_infos;
};

/// @brief Builder pattern for creating descriptor sets.
///
/// Provides a fluent API for adding uniform buffers, samplers, etc.
/// and then building a ResourceDescriptor.
///
/// Example:
/// @code
/// auto descriptor = DescriptorBuilder(device)
///     .add_uniform_buffer<UBO>(ubo_buffer.buffer(), 0)
///     .add_combined_image_sampler(texture_view, sampler, 1)
///     .build("My Descriptor");
/// @endcode
class DescriptorBuilder
{
public:
  explicit DescriptorBuilder(const Device& device);

  /// @brief Add a uniform buffer binding.
  /// @tparam T The uniform buffer data type (for sizeof).
  /// @param buffer The VkBuffer handle.
  /// @param binding The binding index in the shader.
  /// @param stage Shader stage(s) that access this buffer.
  /// @return Reference to this builder for chaining.
  template <typename T>
  DescriptorBuilder& add_uniform_buffer(vk::Buffer buffer, uint32_t binding,
    vk::ShaderStageFlags stage = vk::ShaderStageFlagBits::eVertex);

  /// @brief Add a uniform buffer binding with explicit size.
  /// @param buffer The VkBuffer handle.
  /// @param binding The binding index in the shader.
  /// @param size Size of the buffer in bytes.
  /// @param stage Shader stage(s) that access this buffer.
  /// @return Reference to this builder for chaining.
  DescriptorBuilder& add_uniform_buffer(vk::Buffer buffer, uint32_t binding, vk::DeviceSize size,
    vk::ShaderStageFlags stage = vk::ShaderStageFlagBits::eVertex);

  /// @brief Add a combined image sampler binding.
  /// @param image_view The image view.
  /// @param sampler The sampler.
  /// @param binding The binding index in the shader.
  /// @param stage Shader stage(s) that access this sampler.
  /// @return Reference to this builder for chaining.
  DescriptorBuilder& add_combined_image_sampler(vk::ImageView image_view, vk::Sampler sampler,
    uint32_t binding, vk::ShaderStageFlags stage = vk::ShaderStageFlagBits::eFragment);

  /// @brief Build the descriptor set.
  /// @param name Debug name for the descriptor.
  /// @return The created ResourceDescriptor.
  [[nodiscard]] ResourceDescriptor build(std::string name);

private:
  const Device* m_device;

  std::vector<vk::DescriptorSetLayoutBinding> m_bindings;
  std::vector<vk::WriteDescriptorSet> m_writes;
  std::vector<vk::DescriptorBufferInfo> m_buffer_infos;
  std::vector<vk::DescriptorImageInfo> m_image_infos;
  std::vector<vk::DescriptorPoolSize> m_pool_sizes;
};

// Template implementation
template <typename T>
DescriptorBuilder& DescriptorBuilder::add_uniform_buffer(
  vk::Buffer buffer, uint32_t binding, vk::ShaderStageFlags stage)
{
  return add_uniform_buffer(buffer, binding, sizeof(T), stage);
}

} // namespace sps::vulkan
