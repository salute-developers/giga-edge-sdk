#include "giga_edge/modules/mcp/nodes/mcp_tool_result/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
McpToolResultParams ParseConfig(const YAML::Node& config) {
  McpToolResultParams result;
  result.result_format = mcp::ParseNamedParams(config["result_format"], "json_v1");
  result.local_debug_enabled = config["local_debug_enabled"].as<bool>(result.local_debug_enabled);
  return result;
}

} // namespace giga_edge
