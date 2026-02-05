#pragma once

// Shared constants for debug modes, textures, channels
// Used by: C++ (app, imgui), GLSL shaders, commands
// Keep in sync with debug_constants.glsl

namespace sps::vulkan::debug
{

// Texture indices (for 2D debug view)
enum TextureIndex : int
{
  TEX_BASE_COLOR = 0,
  TEX_NORMAL = 1,
  TEX_METALLIC_ROUGHNESS = 2,
  TEX_EMISSIVE = 3,
  TEX_AO = 4,
  TEX_COUNT = 5
};

// Channel display modes
enum ChannelMode : int
{
  CHANNEL_RGB = 0,
  CHANNEL_R = 1,
  CHANNEL_G = 2,
  CHANNEL_B = 3,
  CHANNEL_A = 4,
  CHANNEL_COUNT = 5
};

// Shader modes
enum ShaderMode : int
{
  SHADER_PBR = 0,
  SHADER_BLINN_PHONG = 1,
  SHADER_DEBUG_UV = 2,
  SHADER_DEBUG_NORMALS = 3,
  SHADER_DEBUG_BASE_COLOR = 4,
  SHADER_DEBUG_METALLIC_ROUGHNESS = 5,
  SHADER_DEBUG_AO = 6,
  SHADER_DEBUG_EMISSIVE = 7,
  SHADER_2D_TEXTURE = 8,  // Fullscreen 2D texture view
  SHADER_COUNT = 9
};

// String names for UI
inline constexpr const char* texture_names[] = {
  "Base Color",
  "Normal",
  "Metallic/Roughness",
  "Emissive",
  "AO"
};

inline constexpr const char* channel_names[] = {
  "RGB",
  "Red",
  "Green",
  "Blue",
  "Alpha"
};

inline constexpr const char* shader_names[] = {
  "PBR (Cook-Torrance)",
  "Blinn-Phong",
  "Debug: UV",
  "Debug: Normals",
  "Debug: Base Color",
  "Debug: Metallic/Roughness",
  "Debug: AO",
  "Debug: Emissive",
  "2D Texture View"
};

// Shader file paths (relative to build dir)
inline constexpr const char* vertex_shaders[] = {
  "../sps/vulkan/shaders/vertex.spv",           // PBR
  "../sps/vulkan/shaders/vertex.spv",           // Blinn-Phong
  "../sps/vulkan/shaders/vertex.spv",           // Debug UV
  "../sps/vulkan/shaders/vertex.spv",           // Debug Normals
  "../sps/vulkan/shaders/vertex.spv",           // Debug Base Color
  "../sps/vulkan/shaders/vertex.spv",           // Debug Metallic/Roughness
  "../sps/vulkan/shaders/vertex.spv",           // Debug AO
  "../sps/vulkan/shaders/vertex.spv",           // Debug Emissive
  "../sps/vulkan/shaders/fullscreen_quad.spv"   // 2D Texture
};

inline constexpr const char* fragment_shaders[] = {
  "../sps/vulkan/shaders/fragment.spv",
  "../sps/vulkan/shaders/blinn_phong.spv",
  "../sps/vulkan/shaders/debug_uv.spv",
  "../sps/vulkan/shaders/debug_normals.spv",
  "../sps/vulkan/shaders/debug_basecolor.spv",
  "../sps/vulkan/shaders/debug_metallic_roughness.spv",
  "../sps/vulkan/shaders/debug_ao.spv",
  "../sps/vulkan/shaders/debug_emissive.spv",
  "../sps/vulkan/shaders/debug_texture2d.spv"
};

} // namespace sps::vulkan::debug
