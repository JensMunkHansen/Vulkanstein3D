#include <sps/vulkan/app_config.h>

#include <spdlog/spdlog.h>
#include <toml.hpp>

#include <fstream>
#include <stdexcept>

namespace
{
inline constexpr auto hash_djb2a(const std::string_view sv)
{
  unsigned long hash{ 5381 };
  for (unsigned char c : sv)
  {
    hash = ((hash << 5) + hash) ^ c;
  }
  return hash;
}

inline constexpr auto operator"" _sh(const char* str, size_t len)
{
  return hash_djb2a(std::string_view{ str, len });
}
} // namespace

namespace sps::vulkan
{

AppConfig parse_toml(const std::string& file_name)
{
  spdlog::trace("Loading TOML configuration file: {}", file_name);

  std::ifstream toml_file(file_name, std::ios::in);
  if (!toml_file)
  {
    throw std::runtime_error("Could not find configuration file: " + file_name +
      "! You must set the working directory properly in your IDE");
  }
  toml_file.close();

  auto cfg = toml::parse(file_name);

  const auto& title = toml::find<std::string>(cfg, "title");
  spdlog::trace("Title: {}", title);

  AppConfig c;

  // [vulkan]
  c.preferred_gpu = toml::find_or<std::string>(cfg, "vulkan", "preferred_gpu", "");
  if (!c.preferred_gpu.empty())
  {
    spdlog::info("Preferred GPU from config: {}", c.preferred_gpu);
  }

  // [application.window]
  const auto& wmodestr =
    toml::find<std::string>(cfg, "application", "window", "mode");

  using WindowMode = sps::vulkan::Window::Mode;
  switch (hash_djb2a(wmodestr))
  {
    case "windowed"_sh:
      c.window_mode = WindowMode::WINDOWED;
      break;
    case "windowed_fullscreen"_sh:
      c.window_mode = WindowMode::WINDOWED_FULLSCREEN;
      break;
    case "fullscreen"_sh:
      c.window_mode = WindowMode::FULLSCREEN;
      break;
    default:
      spdlog::warn("Invalid application window mode: {}", wmodestr);
      c.window_mode = WindowMode::WINDOWED;
  }

  c.window_width = toml::find<int>(cfg, "application", "window", "width");
  c.window_height = toml::find<int>(cfg, "application", "window", "height");
  c.window_title = toml::find<std::string>(cfg, "application", "window", "name");
  spdlog::trace("Window: {}, {} x {}", c.window_title, c.window_width, c.window_height);

  // [application.rendering]
  c.backface_culling = toml::find_or<bool>(
    cfg, "application", "rendering", "backface_culling", true);
  spdlog::trace("Backface culling: {}", c.backface_culling);

  {
    int msaa_config = toml::find_or<int>(
      cfg, "application", "rendering", "msaa_samples", 4);
    switch (msaa_config)
    {
      case 2: c.msaa_samples = vk::SampleCountFlagBits::e2; break;
      case 4: c.msaa_samples = vk::SampleCountFlagBits::e4; break;
      case 8: c.msaa_samples = vk::SampleCountFlagBits::e8; break;
      case 16: c.msaa_samples = vk::SampleCountFlagBits::e16; break;
      default: c.msaa_samples = vk::SampleCountFlagBits::e1; break;
    }
    spdlog::trace("MSAA samples (config): {}", msaa_config);
  }

  auto render_mode = toml::find_or<std::string>(
    cfg, "application", "rendering", "mode", "rasterization");
  c.use_raytracing = (render_mode == "raytracing");
  spdlog::trace("Rendering mode: {}", render_mode);

  // [application.geometry]
  c.geometry_source = toml::find_or<std::string>(
    cfg, "application", "geometry", "source", "triangle");
  c.ply_file = toml::find_or<std::string>(
    cfg, "application", "geometry", "ply_file", "");
  c.gltf_file = toml::find_or<std::string>(
    cfg, "application", "geometry", "gltf_file", "");
  c.hdr_file = toml::find_or<std::string>(
    cfg, "application", "geometry", "hdr_file", "");
  spdlog::trace("Geometry source: {}, PLY file: {}, glTF file: {}",
    c.geometry_source, c.ply_file, c.gltf_file);

  // [glTFmodels]
  if (cfg.contains("glTFmodels"))
  {
    const auto& gltf_section = toml::find(cfg, "glTFmodels");
    c.gltf_models = toml::find_or<std::vector<std::string>>(gltf_section, "files", {});
  }
  for (int i = 0; i < static_cast<int>(c.gltf_models.size()); ++i)
  {
    if (c.gltf_models[i] == c.gltf_file)
    {
      c.current_model_index = i;
      break;
    }
  }
  spdlog::trace("glTF model list: {} entries, current index: {}",
    c.gltf_models.size(), c.current_model_index);

  // [HDRenvironments]
  if (cfg.contains("HDRenvironments"))
  {
    const auto& hdr_section = toml::find(cfg, "HDRenvironments");
    c.hdr_files = toml::find_or<std::vector<std::string>>(hdr_section, "files", {});
  }
  for (int i = 0; i < static_cast<int>(c.hdr_files.size()); ++i)
  {
    if (c.hdr_files[i] == c.hdr_file)
    {
      c.current_hdr_index = i;
      break;
    }
  }
  spdlog::trace("HDR environment list: {} entries, current index: {}",
    c.hdr_files.size(), c.current_hdr_index);

  // [IBL]
  if (cfg.contains("IBL"))
  {
    const auto& ibl_section = toml::find(cfg, "IBL");
    c.ibl_settings.resolution = static_cast<uint32_t>(
      toml::find_or<int>(ibl_section, "resolution", 256));
    c.ibl_settings.irradiance_samples = static_cast<uint32_t>(
      toml::find_or<int>(ibl_section, "irradiance_samples", 2048));
    c.ibl_settings.prefilter_samples = static_cast<uint32_t>(
      toml::find_or<int>(ibl_section, "prefilter_samples", 2048));
    c.ibl_settings.brdf_samples = static_cast<uint32_t>(
      toml::find_or<int>(ibl_section, "brdf_samples", 1024));
  }
  spdlog::info("IBL settings: resolution={}, irradiance_samples={}, prefilter_samples={}, brdf_samples={}",
    c.ibl_settings.resolution, c.ibl_settings.irradiance_samples,
    c.ibl_settings.prefilter_samples, c.ibl_settings.brdf_samples);

  // [application.lighting]
  try
  {
    const auto& lighting = toml::find(cfg, "application", "lighting");

    auto light_type = toml::find<std::string>(lighting, "light_type");
    auto light_color = toml::find<std::vector<double>>(lighting, "light_color");
    auto light_intensity = static_cast<float>(toml::find<double>(lighting, "light_intensity"));
    auto ambient_color = toml::find<std::vector<double>>(lighting, "ambient_color");

    c.shininess = static_cast<float>(toml::find<double>(lighting, "shininess"));
    c.specular_strength = static_cast<float>(toml::find<double>(lighting, "specular_strength"));

    if (light_type == "directional")
    {
      auto light_dir = toml::find<std::vector<double>>(lighting, "light_direction");
      auto light = std::make_unique<DirectionalLight>();
      if (light_dir.size() >= 3)
      {
        light->set_direction(static_cast<float>(light_dir[0]),
          static_cast<float>(light_dir[1]), static_cast<float>(light_dir[2]));
      }
      c.light = std::move(light);
    }
    else if (light_type == "point")
    {
      auto light_dir = toml::find<std::vector<double>>(lighting, "light_direction");
      auto light = std::make_unique<PointLight>();
      if (light_dir.size() >= 3)
      {
        light->set_position(static_cast<float>(light_dir[0]),
          static_cast<float>(light_dir[1]), static_cast<float>(light_dir[2]));
      }
      c.light = std::move(light);
    }
    else
    {
      auto light_dir =
        toml::find_or<std::vector<double>>(lighting, "light_direction", { 0.0, 0.0, 0.0 });
      auto light = std::make_unique<PointLight>();
      if (light_dir.size() >= 3)
      {
        light->set_position(static_cast<float>(light_dir[0]),
          static_cast<float>(light_dir[1]), static_cast<float>(light_dir[2]));
      }
      c.light = std::move(light);
    }

    if (light_color.size() >= 3)
    {
      c.light->set_color(static_cast<float>(light_color[0]),
        static_cast<float>(light_color[1]), static_cast<float>(light_color[2]));
    }
    c.light->set_intensity(light_intensity);
    if (ambient_color.size() >= 3)
    {
      c.light->set_ambient(static_cast<float>(ambient_color[0]),
        static_cast<float>(ambient_color[1]), static_cast<float>(ambient_color[2]));
    }

    spdlog::trace("Light type: {}", light_type);
    spdlog::trace("Shininess: {}, Specular strength: {}", c.shininess, c.specular_strength);
  }
  catch (const std::out_of_range&)
  {
    spdlog::trace("No lighting configuration found, using defaults");
    c.light = std::make_unique<DirectionalLight>(glm::vec3(0.3f, 0.5f, 1.0f));
  }

  return c;
}

} // namespace sps::vulkan
