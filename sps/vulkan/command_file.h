#pragma once

#include <ctime>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sps::vulkan
{

/// @brief Simple command file interface for remote control.
/// Watches a file for commands and executes them.
/// Commands are line-based: "command arg1 arg2"
class CommandFile
{
public:
  using CommandHandler = std::function<void(const std::vector<std::string>& args)>;

  /// @brief Create command file watcher.
  /// @param filepath Path to command file (default: ./commands.txt)
  explicit CommandFile(const std::string& filepath = "./commands.txt");

  /// @brief Register a command handler.
  /// @param command Command name (e.g., "set", "screenshot").
  /// @param handler Function to call when command is received.
  void register_command(const std::string& command, CommandHandler handler);

  /// @brief Check for new commands and execute them.
  /// Call this once per frame from the main loop.
  void poll();

  /// @brief Get the command file path.
  [[nodiscard]] const std::string& filepath() const { return m_filepath; }

private:
  void process_line(const std::string& line);
  std::vector<std::string> split_args(const std::string& line);

  std::string m_filepath;
  std::unordered_map<std::string, CommandHandler> m_handlers;
  std::size_t m_last_file_size{ 0 };
  time_t m_last_modified{ 0 };
};

} // namespace sps::vulkan
