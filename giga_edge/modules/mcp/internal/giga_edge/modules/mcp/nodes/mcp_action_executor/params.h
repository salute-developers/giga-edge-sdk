#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace giga_edge {

struct McpActionExecutorParams {
  std::string environment_name = "mcp";
  std::string consumer_name = "mcp_executor";
  std::vector<std::string> visible_server_ids;
  size_t completion_queue_capacity = 64;
  bool local_debug_enabled = false;

  std::string call_id_prefix = "mcp-action-";
  uint32_t tool_call_timeout_ms = 0;
  uint32_t read_resource_timeout_ms = 0;
};

} // namespace giga_edge
