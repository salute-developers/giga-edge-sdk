#pragma once

#include "giga_edge/modules/mcp/connection.h"

#include <string>

namespace giga_edge {

// command is a string for /bin/sh -c, like the server command in MCP client configs: pass only a trusted command
// (from your own config), never external data.
mcp::ConnectionPtr CreateStdioMcpConnection(
  const std::string& server_id, const std::string& command, bool debug_enabled = false
);

} // namespace giga_edge
