#pragma once

#include "giga_edge/modules/mcp/profiles.h"

namespace giga_edge {

struct McpToolResultParams {
  mcp::NamedParams result_format;
  bool local_debug_enabled = false;
};

} // namespace giga_edge
