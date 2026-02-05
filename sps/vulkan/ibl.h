#pragma once

#include <vulkan/vulkan.hpp>
#include <string>

namespace sps::vulkan
{

class Device;
class Texture;

/// @brief Image-Based Lighting (IBL) resources
/// Contains pre-computed environment maps for PBR rendering:
/// - BRDF LUT: 2D lookup table for split-sum approximation
/// - Irradiance cubemap: diffuse ambient lighting
/// - Pre-filtered environment cubemap: specular reflections (mip levels = roughness)
class IBL
{
public:
  /// @brief Create IBL resources
  /// @param device Vulkan device
  /// @param hdr_path Path to HDR environment map (equirectangular format)
  /// @param resolution Cubemap face resolution (default 512)
  IBL(const Device& device, const std::string& hdr_path, uint32_t resolution = 512);

  /// @brief Create IBL with default neutral environment (for testing)
  explicit IBL(const Device& device);

  ~IBL();

  // Non-copyable
  IBL(const IBL&) = delete;
  IBL& operator=(const IBL&) = delete;

  // Accessors for descriptor binding
  [[nodiscard]] vk::ImageView brdf_lut_view() const { return m_brdf_lut_view; }
  [[nodiscard]] vk::Sampler brdf_lut_sampler() const { return m_brdf_lut_sampler; }

  [[nodiscard]] vk::ImageView irradiance_view() const { return m_irradiance_view; }
  [[nodiscard]] vk::Sampler irradiance_sampler() const { return m_irradiance_sampler; }

  [[nodiscard]] vk::ImageView prefiltered_view() const { return m_prefiltered_view; }
  [[nodiscard]] vk::Sampler prefiltered_sampler() const { return m_prefiltered_sampler; }

  [[nodiscard]] uint32_t mip_levels() const { return m_mip_levels; }
  [[nodiscard]] float intensity() const { return m_intensity; }
  void set_intensity(float intensity) { m_intensity = intensity; }

private:
  void generate_brdf_lut();
  void load_hdr_environment(const std::string& hdr_path);
  void create_cubemap_from_equirectangular();
  void generate_irradiance_map();
  void generate_prefiltered_map();
  void create_default_environment();

  const Device& m_device;
  uint32_t m_resolution;
  uint32_t m_mip_levels;
  float m_intensity{ 1.0f };

  // BRDF LUT (2D texture)
  vk::Image m_brdf_lut_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_brdf_lut_memory{ VK_NULL_HANDLE };
  vk::ImageView m_brdf_lut_view{ VK_NULL_HANDLE };
  vk::Sampler m_brdf_lut_sampler{ VK_NULL_HANDLE };

  // Irradiance cubemap (diffuse IBL)
  vk::Image m_irradiance_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_irradiance_memory{ VK_NULL_HANDLE };
  vk::ImageView m_irradiance_view{ VK_NULL_HANDLE };
  vk::Sampler m_irradiance_sampler{ VK_NULL_HANDLE };

  // Pre-filtered environment cubemap (specular IBL)
  vk::Image m_prefiltered_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_prefiltered_memory{ VK_NULL_HANDLE };
  vk::ImageView m_prefiltered_view{ VK_NULL_HANDLE };
  vk::Sampler m_prefiltered_sampler{ VK_NULL_HANDLE };

  // Source HDR environment (equirectangular)
  vk::Image m_hdr_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_hdr_memory{ VK_NULL_HANDLE };
  vk::ImageView m_hdr_view{ VK_NULL_HANDLE };

  // CPU-side HDR data for processing
  std::vector<float> m_hdr_data;
  uint32_t m_hdr_width{ 0 };
  uint32_t m_hdr_height{ 0 };

  // Helper functions for CPU-based IBL processing
  void create_cubemap_image(vk::Image& image, vk::DeviceMemory& memory,
                            uint32_t size, uint32_t mip_levels, vk::Format format);
  void upload_cubemap_data(vk::Image image, const std::vector<float>& data,
                           uint32_t size, uint32_t mip_levels);
};

/// @brief Generate BRDF integration LUT on CPU
/// @param size Resolution of the LUT (e.g., 512)
/// @return RGBA pixel data (RG channels used)
std::vector<uint8_t> generate_brdf_lut_cpu(uint32_t size);

} // namespace sps::vulkan
