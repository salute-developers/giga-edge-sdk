#pragma once

#include <iostream>
#include <mutex>
#include <string>

namespace giga_edge {
namespace mcp {
namespace detail {

inline void WriteMcpLog(std::string message) {
  message += '\n';
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  std::cerr.write(message.data(), static_cast<std::streamsize>(message.size()));
}

} // namespace detail
} // namespace mcp
} // namespace giga_edge
