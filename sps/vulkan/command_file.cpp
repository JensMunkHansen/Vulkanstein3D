#include <sps/vulkan/command_file.h>
#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>
#include <filesystem>
#include <sys/stat.h>

namespace sps::vulkan
{

CommandFile::CommandFile(const std::string& filepath)
  : m_filepath(filepath)
{
  // Create empty file if it doesn't exist
  if (!std::filesystem::exists(m_filepath))
  {
    std::ofstream file(m_filepath);
    file << "# Vulkanstein3D Command File\n";
    file << "# Commands: set <var> <value>, shader <name>, screenshot [filename]\n";
    file << "# Example: set metallic_ambient 0.5\n";
    file.close();
  }
  spdlog::info("Command file: {}", std::filesystem::absolute(m_filepath).string());
}

void CommandFile::register_command(const std::string& command, CommandHandler handler)
{
  m_handlers[command] = std::move(handler);
}

void CommandFile::poll()
{
  if (!std::filesystem::exists(m_filepath))
  {
    return;
  }

  // Check if file was modified
  struct stat file_stat;
  if (stat(m_filepath.c_str(), &file_stat) != 0)
  {
    return;
  }

  if (file_stat.st_mtime <= m_last_modified)
  {
    return;  // No changes
  }

  m_last_modified = file_stat.st_mtime;

  // Read and process all lines
  std::ifstream file(m_filepath);
  if (!file.is_open())
  {
    return;
  }

  std::vector<std::string> commands;
  std::string line;
  while (std::getline(file, line))
  {
    // Skip empty lines and comments
    if (line.empty() || line[0] == '#')
    {
      continue;
    }
    commands.push_back(line);
  }
  file.close();

  // Process commands
  for (const auto& cmd : commands)
  {
    process_line(cmd);
  }

  // Clear the file after processing (keep header comments)
  std::ofstream clear_file(m_filepath);
  clear_file << "# Vulkanstein3D Command File\n";
  clear_file << "# Commands: set <var> <value>, shader <name>, screenshot [filename], mode <3d|2d>\n";
  clear_file << "# Variables: metallic_ambient, ao_strength, shininess, specular\n";
  clear_file << "# Toggles: normal_mapping, emissive, ao (use: set <name> 0 or 1)\n";
  clear_file << "# 2D mode: texture <0-4>, channel <0-4> (0=RGB, 1=R, 2=G, 3=B, 4=A)\n";
  clear_file.close();
}

void CommandFile::process_line(const std::string& line)
{
  auto args = split_args(line);
  if (args.empty())
  {
    return;
  }

  const std::string& command = args[0];
  args.erase(args.begin());  // Remove command from args

  auto it = m_handlers.find(command);
  if (it != m_handlers.end())
  {
    spdlog::info("Command: {} {}", command,
      args.empty() ? "" : args[0] + (args.size() > 1 ? " " + args[1] : ""));
    it->second(args);
  }
  else
  {
    spdlog::warn("Unknown command: {}", command);
  }
}

std::vector<std::string> CommandFile::split_args(const std::string& line)
{
  std::vector<std::string> args;
  std::istringstream iss(line);
  std::string arg;
  while (iss >> arg)
  {
    args.push_back(arg);
  }
  return args;
}

} // namespace sps::vulkan
