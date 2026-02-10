#pragma once

#include <sps/vulkan/ibl.h>
#include <sps/vulkan/light.h>
#include <sps/vulkan/window.h>

#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.hpp>

namespace sps::vulkan
{

/// Plain struct holding all values parsed from the TOML configuration file.
/// Decouples config format from Application initialization.
struct AppConfig
{
  // [vulkan]
  std::string preferred_gpu;

  // [application.window]
  Window::Mode window_mode{ Window::Mode::WINDOWED };
  uint32_t window_width{ 1280 };
  uint32_t window_height{ 720 };
  std::string window_title{ "Vulkan Triangle" };

  // [application.rendering]
  bool backface_culling{ true };
  vk::SampleCountFlagBits msaa_samples{ vk::SampleCountFlagBits::e1 };
  bool use_raytracing{ false };

  // [application.geometry]
  std::string geometry_source{ "triangle" };
  std::string ply_file;
  std::string gltf_file;
  std::string hdr_file;

  // [glTFmodels]
  std::vector<std::string> gltf_models;
  int current_model_index{ -1 };

  // [HDRenvironments]
  std::vector<std::string> hdr_files;
  int current_hdr_index{ -1 };

  // [IBL]
  IBLSettings ibl_settings;

  // [application.lighting]
  std::unique_ptr<Light> light;
  float shininess{ 32.0f };
  float specular_strength{ 0.4f };
};

/// Parse a TOML configuration file and return an AppConfig.
AppConfig parse_toml(const std::string& file_name);

} // namespace sps::vulkan
