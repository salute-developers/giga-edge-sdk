#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace giga_edge {

struct McpCompletionPickupParams {
  std::string environment_name = "mcp";
  std::string consumer_name = "mcp_executor";
  std::vector<std::string> visible_server_ids;
  size_t completion_queue_capacity = 64;
  bool local_debug_enabled = false;
  bool owns_consumer_registration = false;

  size_t drain_limit = 16;
};

} // namespace giga_edge
