#pragma once

#include <sps/vulkan/config.h>

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
  TEX_IRIDESCENCE = 5,
  TEX_IRIDESCENCE_THICKNESS = 6,
  TEX_THICKNESS = 7,
  TEX_COUNT = 8
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
  SHADER_DEBUG_THICKNESS = 8,
  SHADER_DEBUG_SSS = 9,
  SHADER_DEBUG_STENCIL = 10,
  SHADER_2D_TEXTURE = 11,  // Fullscreen 2D texture view (not in 3D dropdown)
  SHADER_COUNT = 12
};

// Number of 3D shaders shown in the UI dropdown (excludes 2D texture view)
inline constexpr int SHADER_3D_COUNT = SHADER_2D_TEXTURE;

// String names for UI
inline constexpr const char* texture_names[] = {
  "Base Color",
  "Normal",
  "Metallic/Roughness",
  "Emissive",
  "AO",
  "Iridescence",
  "Iridescence Thickness",
  "Thickness"
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
  "Debug: Thickness",
  "Debug: SSS",
  "Debug: Stencil",
  "2D Texture View"
};

// Shader file paths (absolute, from config.h SHADER_DIR)
inline const char* vertex_shaders[] = {
  SHADER_DIR "vertex.spv",           // PBR
  SHADER_DIR "vertex.spv",           // Blinn-Phong
  SHADER_DIR "vertex.spv",           // Debug UV
  SHADER_DIR "vertex.spv",           // Debug Normals
  SHADER_DIR "vertex.spv",           // Debug Base Color
  SHADER_DIR "vertex.spv",           // Debug Metallic/Roughness
  SHADER_DIR "vertex.spv",           // Debug AO
  SHADER_DIR "vertex.spv",           // Debug Emissive
  SHADER_DIR "vertex.spv",           // Debug Thickness
  SHADER_DIR "vertex.spv",           // Debug SSS
  SHADER_DIR "vertex.spv",           // Debug Stencil
  SHADER_DIR "fullscreen_quad.spv"   // 2D Texture
};

inline const char* fragment_shaders[] = {
  SHADER_DIR "fragment.spv",
  SHADER_DIR "blinn_phong.spv",
  SHADER_DIR "debug_uv.spv",
  SHADER_DIR "debug_normals.spv",
  SHADER_DIR "debug_basecolor.spv",
  SHADER_DIR "debug_metallic_roughness.spv",
  SHADER_DIR "debug_ao.spv",
  SHADER_DIR "debug_emissive.spv",
  SHADER_DIR "debug_thickness.spv",
  SHADER_DIR "debug_sss.spv",
  SHADER_DIR "debug_stencil.spv",
  SHADER_DIR "debug_texture2d.spv"
};

} // namespace sps::vulkan::debug
