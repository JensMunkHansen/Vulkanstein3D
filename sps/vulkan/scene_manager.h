#pragma once

#include <sps/vulkan/gltf_loader.h>
#include <sps/vulkan/ibl.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <vulkan/vulkan.hpp>

namespace sps::vulkan
{

class Device;
class IBL;
class Mesh;
class ResourceDescriptor;
class Texture;

/// @brief Owns scene assets: mesh, materials, textures, IBL, and descriptors.
///
/// Extracted from Application to reduce god-object complexity.
/// Application delegates scene loading and descriptor creation here.
class SceneManager
{
public:
  struct LoadResult
  {
    bool success{false};
    AABB bounds;
  };

  explicit SceneManager(const Device& device);
  ~SceneManager();

  // Non-copyable, non-movable
  SceneManager(const SceneManager&) = delete;
  SceneManager& operator=(const SceneManager&) = delete;

  /// Set IBL generation settings (call before create_defaults/load_hdr).
  void set_ibl_settings(const IBLSettings& settings);

  /// Create 1x1 fallback textures and IBL environment.
  void create_defaults(const std::string& hdr_file = "");

  /// Load initial scene (gltf, ply, or triangle).
  LoadResult load_initial_scene(const std::string& geometry_source,
    const std::string& gltf_file, const std::string& ply_file);

  /// Build/rebuild descriptors (needs UBO buffer handle for binding 0).
  void create_descriptors(vk::Buffer uniform_buffer);

  /// Runtime model switch. Caller must call device.wait_idle() first.
  LoadResult load_model(const std::string& path, vk::Buffer uniform_buffer);

  /// Switch HDR environment. Caller must call device.wait_idle() first.
  void load_hdr(const std::string& hdr_file, vk::Buffer uniform_buffer);

  // Read-only accessors
  [[nodiscard]] const Mesh* mesh() const;
  [[nodiscard]] Mesh* mesh(); // non-const needed for RT vertex/index buffer
  [[nodiscard]] const GltfScene* scene() const;
  [[nodiscard]] const ResourceDescriptor* default_descriptor() const;
  [[nodiscard]] const std::vector<std::unique_ptr<ResourceDescriptor>>& material_descriptors() const;
  [[nodiscard]] int material_count() const;
  [[nodiscard]] const AABB& bounds() const;

  // IBL delegation
  [[nodiscard]] const IBL* ibl() const;
  [[nodiscard]] float ibl_intensity() const;
  void set_ibl_intensity(float v);

private:
  const Device& m_device;
  std::unique_ptr<Mesh> m_mesh;
  std::optional<GltfScene> m_scene;
  AABB m_bounds;

  // Fallback textures (1x1 defaults)
  std::unique_ptr<Texture> m_defaultTexture;
  std::unique_ptr<Texture> m_defaultNormalTexture;
  std::unique_ptr<Texture> m_defaultMetallicRoughness;
  std::unique_ptr<Texture> m_defaultEmissive;
  std::unique_ptr<Texture> m_defaultAO;
  std::unique_ptr<Texture> m_defaultIridescence;
  std::unique_ptr<Texture> m_defaultIridescenceThickness;

  // Single-material textures (non-scene path, from GltfModel)
  std::unique_ptr<Texture> m_baseColorTexture;
  std::unique_ptr<Texture> m_normalTexture;
  std::unique_ptr<Texture> m_metallicRoughnessTexture;
  std::unique_ptr<Texture> m_emissiveTexture;
  std::unique_ptr<Texture> m_aoTexture;

  // IBL
  IBLSettings m_ibl_settings;
  std::unique_ptr<IBL> m_ibl;

  // Descriptors
  std::unique_ptr<ResourceDescriptor> m_descriptor;
  std::vector<std::unique_ptr<ResourceDescriptor>> m_material_descriptors;
};

} // namespace sps::vulkan
