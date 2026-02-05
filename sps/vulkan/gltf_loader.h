#pragma once

#include <sps/vulkan/mesh.h>
#include <sps/vulkan/texture.h>

#include <memory>
#include <string>

namespace sps::vulkan
{

class Device;

/// @brief Complete glTF model with mesh and textures.
/// @see glTF 2.0 PBR: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials
struct GltfModel
{
  std::unique_ptr<Mesh> mesh;
  std::unique_ptr<Texture> baseColorTexture;         // nullptr if no texture
  std::unique_ptr<Texture> normalTexture;            // nullptr if no normal map
  std::unique_ptr<Texture> metallicRoughnessTexture; // nullptr if no PBR texture (G=roughness, B=metallic)
  std::unique_ptr<Texture> emissiveTexture;          // nullptr if no emissive (RGB glow)
  std::unique_ptr<Texture> aoTexture;                // nullptr if no ambient occlusion (R channel)
};

/// @brief Load a glTF 2.0 mesh file.
///
/// Supports .gltf (JSON) and .glb (binary) files.
/// Extracts vertex positions, normals, UVs, colors, and indices.
/// Uses cgltf for parsing.
///
/// @param device The Vulkan device wrapper.
/// @param filepath Path to the glTF file.
/// @return Loaded mesh, or nullptr on failure.
std::unique_ptr<Mesh> load_gltf(const Device& device, const std::string& filepath);

/// @brief Load a glTF 2.0 model with textures.
///
/// Supports .gltf (JSON) and .glb (binary) files.
/// Extracts mesh geometry and base color texture from materials.
///
/// @param device The Vulkan device wrapper.
/// @param filepath Path to the glTF file.
/// @return GltfModel with mesh and optional textures.
GltfModel load_gltf_model(const Device& device, const std::string& filepath);

} // namespace sps::vulkan
