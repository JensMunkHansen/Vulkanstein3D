#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <thread>
#include <atomic>

namespace sps::vulkan
{

/// @brief Simple remote control via TCP socket.
/// Listens for text commands and executes callbacks.
/// Commands are line-based: "command arg1 arg2\n"
class RemoteControl
{
public:
  using CommandHandler = std::function<void(const std::vector<std::string>& args)>;

  /// @brief Start listening on specified port.
  /// @param port TCP port to listen on (default 9999).
  RemoteControl(uint16_t port = 9999);
  ~RemoteControl();

  /// @brief Register a command handler.
  /// @param command Command name (e.g., "set", "screenshot").
  /// @param handler Function to call when command is received.
  void register_command(const std::string& command, CommandHandler handler);

  /// @brief Start the listener thread.
  void start();

  /// @brief Stop the listener thread.
  void stop();

  /// @brief Process any pending commands (call from main thread).
  void poll();

private:
  void listener_thread();
  void process_line(const std::string& line);

  uint16_t m_port;
  int m_server_fd{ -1 };
  std::atomic<bool> m_running{ false };
  std::thread m_thread;

  std::unordered_map<std::string, CommandHandler> m_handlers;

  // Thread-safe command queue
  std::mutex m_queue_mutex;
  std::vector<std::string> m_command_queue;
};

} // namespace sps::vulkan
