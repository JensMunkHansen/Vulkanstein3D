#include <sps/vulkan/config.h>

#include <sps/tools/cla_parser.hpp>
#include <sps/vulkan/app.h>
#include <sps/vulkan/meta.hpp>

#include <spdlog/spdlog.h>
#include <toml.hpp>

namespace sps::vulkan
{

Application::Application(int argc, char** argv)
{
  spdlog::trace("Initialising vulkan-renderer");
  bool enable_renderdoc_instance_layer = false;

  m_window_title = "TESTME";

  sps::tools::CommandLineArgumentParser cla_parser;
  cla_parser.parse_args(argc, argv);

  spdlog::trace("Application version: {}.{}.{}", APP_VERSION[0], APP_VERSION[1], APP_VERSION[2]);
  spdlog::trace(
    "Engine version: {}.{}.{}", ENGINE_VERSION[0], ENGINE_VERSION[1], ENGINE_VERSION[2]);

  // Load the configuration from the TOML file.
  load_toml_configuration_file("./vulk3D.toml");

  auto enable_renderdoc = cla_parser.arg<bool>("--renderdoc");
  if (enable_renderdoc)
  {
#ifndef SPS_DEBUG
    spdlog::warn("You can't use --renderdoc command line argument in release mode. You have to "
                 "download the code "
                 "and compile it yourself in debug mode");
#else
    if (*enable_renderdoc)
    {
      spdlog::trace("--renderdoc specified, enabling renderdoc instance layer");
      enable_renderdoc_instance_layer = true;
    }
#endif
  }

  // If the user specified command line argument "--no-validation", the Khronos validation instance
  // layer will be disabled. For debug builds, this is not advisable! Always use validation layers
  // during development!
  const auto disable_validation = cla_parser.arg<bool>("--no-validation");
  if (disable_validation.value_or(false))
  {
    spdlog::warn("--no-validation specified, disabling validation layers");
    m_enable_validation_layers = false;
  }

  spdlog::trace("Creating Vulkan instance");

  m_window_width = 640;
  m_window_height = 480;

  m_window = std::make_unique<sps::vulkan::Window>(
    m_window_title, m_window_width, m_window_height, true, true, m_window_mode);

  m_instance = std::make_unique<sps::vulkan::Instance>(APP_NAME, ENGINE_NAME,
    VK_MAKE_API_VERSION(0, APP_VERSION[0], APP_VERSION[1], APP_VERSION[2]),
    VK_MAKE_API_VERSION(0, ENGINE_VERSION[0], ENGINE_VERSION[1], ENGINE_VERSION[2]),
    m_enable_validation_layers, enable_renderdoc_instance_layer);
}

void Application::load_toml_configuration_file(const std::string& file_name)
{
  spdlog::trace("Loading TOML configuration file: {}", file_name);

  std::ifstream toml_file(file_name, std::ios::in);
  if (!toml_file)
  {
    // If you are using CLion, go to "Edit Configurations" and select "Working Directory".
    throw std::runtime_error("Could not find configuration file: " + file_name +
      "! You must set the working directory properly in your IDE");
  }

  toml_file.close();

  // Load the TOML file using toml11.
  auto renderer_configuration = toml::parse(file_name);

  // Search for the title of the configuration file and print it to debug output.
  const auto& configuration_title = toml::find<std::string>(renderer_configuration, "title");
  spdlog::trace("Title: {}", configuration_title);

  using WindowMode = sps::vulkan::Window::Mode;
  const auto& wmodestr =
    toml::find<std::string>(renderer_configuration, "application", "window", "mode");
  if (wmodestr == "windowed")
  {
    m_window_mode = WindowMode::WINDOWED;
  }
  else if (wmodestr == "windowed_fullscreen")
  {
    m_window_mode = WindowMode::WINDOWED_FULLSCREEN;
  }
  else if (wmodestr == "fullscreen")
  {
    m_window_mode = WindowMode::FULLSCREEN;
  }
  else
  {
    spdlog::warn("Invalid application window mode: {}", wmodestr);
    m_window_mode = WindowMode::WINDOWED;
  }

  m_window_width = toml::find<int>(renderer_configuration, "application", "window", "width");
  m_window_height = toml::find<int>(renderer_configuration, "application", "window", "height");
  m_window_title = toml::find<std::string>(renderer_configuration, "application", "window", "name");
  spdlog::trace("Window: {}, {} x {}", m_window_title, m_window_width, m_window_height);

#if 0
  m_texture_files =
    toml::find<std::vector<std::string>>(renderer_configuration, "textures", "files");

  spdlog::trace("Textures:");

  for (const auto& texture_file : m_texture_files)
  {
    spdlog::trace("   - {}", texture_file);
  }

  m_gltf_model_files =
    toml::find<std::vector<std::string>>(renderer_configuration, "glTFmodels", "files");

  spdlog::trace("glTF 2.0 models:");

  for (const auto& gltf_model_file : m_gltf_model_files)
  {
    spdlog::trace("   - {}", gltf_model_file);
  }

  m_vertex_shader_files =
    toml::find<std::vector<std::string>>(renderer_configuration, "shaders", "vertex", "files");

  spdlog::trace("Vertex shaders:");

  for (const auto& vertex_shader_file : m_vertex_shader_files)
  {
    spdlog::trace("   - {}", vertex_shader_file);
  }

  m_fragment_shader_files =
    toml::find<std::vector<std::string>>(renderer_configuration, "shaders", "fragment", "files");

  spdlog::trace("Fragment shaders:");

  for (const auto& fragment_shader_file : m_fragment_shader_files)
  {
    spdlog::trace("   - {}", fragment_shader_file);
  }
#endif
}

void Application::run()
{
  spdlog::trace("Running Application");

  while (!m_window->should_close())
  {
    m_window->poll();
  }
}
}
