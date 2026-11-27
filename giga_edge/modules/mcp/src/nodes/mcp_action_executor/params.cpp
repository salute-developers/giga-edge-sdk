#include "giga_edge/modules/mcp/nodes/mcp_action_executor/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
McpActionExecutorParams ParseConfig(const YAML::Node& config) {
  McpActionExecutorParams result;
  result.environment_name = config["environment_name"].as<std::string>(result.environment_name);
  result.consumer_name = config["consumer_name"].as<std::string>(result.consumer_name);
  result.visible_server_ids = config["visible_server_ids"].as<std::vector<std::string>>(result.visible_server_ids);
  result.completion_queue_capacity = config["completion_queue_capacity"].as<size_t>(result.completion_queue_capacity);
  result.local_debug_enabled = config["local_debug_enabled"].as<bool>(result.local_debug_enabled);

  result.call_id_prefix = config["call_id_prefix"].as<std::string>(result.call_id_prefix);
  result.tool_call_timeout_ms = config["tool_call_timeout_ms"].as<uint32_t>(result.tool_call_timeout_ms);
  result.read_resource_timeout_ms = config["read_resource_timeout_ms"].as<uint32_t>(result.read_resource_timeout_ms);
  return result;
}

} // namespace giga_edge
