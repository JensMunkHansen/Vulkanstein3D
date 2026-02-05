#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>

#include <spdlog/spdlog.h>

namespace sps::vulkan
{

/// @brief Command definition with metadata for auto-generated help.
struct CommandDef
{
  std::string name;
  std::string description;
  std::string usage;  // e.g., "<value>" or "<name> <value>"
  std::function<void(const std::vector<std::string>&)> handler;
};

/// @brief Central command registry with auto-help generation.
///
/// Usage:
/// @code
///   CommandRegistry registry;
///   registry.add("screenshot", "Save screenshot", "[filename]",
///     [&](auto& args) { app.save_screenshot(args.empty() ? "" : args[0]); });
///   registry.add("set", "Set variable", "<name> <value>",
///     [&](auto& args) { handle_set(args); });
///
///   // In poll loop:
///   registry.execute("screenshot test.png");
/// @endcode
class CommandRegistry
{
public:
  /// @brief Add a command to the registry.
  void add(const std::string& name,
           const std::string& description,
           const std::string& usage,
           std::function<void(const std::vector<std::string>&)> handler)
  {
    m_commands[name] = { name, description, usage, std::move(handler) };
  }

  /// @brief Execute a command line.
  /// @param line Command with arguments (e.g., "set metallic_ambient 0.5")
  /// @return true if command was found and executed
  bool execute(const std::string& line)
  {
    auto args = split(line);
    if (args.empty()) return false;

    const std::string& cmd = args[0];
    args.erase(args.begin());

    // Built-in help
    if (cmd == "help")
    {
      print_help();
      return true;
    }

    auto it = m_commands.find(cmd);
    if (it == m_commands.end())
    {
      return false;
    }

    it->second.handler(args);
    return true;
  }

  /// @brief Get help text for all commands.
  [[nodiscard]] std::string help_text() const
  {
    std::ostringstream oss;
    oss << "Available commands:\n";
    for (const auto& [name, def] : m_commands)
    {
      oss << "  " << name;
      if (!def.usage.empty()) oss << " " << def.usage;
      oss << "\n    " << def.description << "\n";
    }
    oss << "  help\n    Show this help\n";
    return oss.str();
  }

  /// @brief Check if command exists.
  [[nodiscard]] bool has_command(const std::string& name) const
  {
    return m_commands.count(name) > 0;
  }

private:
  void print_help() const
  {
    spdlog::info("{}", help_text());
  }

  static std::vector<std::string> split(const std::string& line)
  {
    std::vector<std::string> args;
    std::istringstream iss(line);
    std::string arg;
    while (iss >> arg) args.push_back(arg);
    return args;
  }

  std::unordered_map<std::string, CommandDef> m_commands;
};

/// @brief Convenience macro for adding commands with lambdas.
/// Usage: CMD_ADD(registry, "name", "description", "usage", { /* handler code */ });
#define CMD_ADD(registry, name, desc, usage, ...) \
  registry.add(name, desc, usage, [&](const std::vector<std::string>& args) __VA_ARGS__)

} // namespace sps::vulkan
