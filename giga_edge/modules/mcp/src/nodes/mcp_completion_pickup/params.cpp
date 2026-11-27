#include "giga_edge/modules/mcp/nodes/mcp_completion_pickup/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

#include <stdexcept>

namespace giga_edge {

template<>
McpCompletionPickupParams ParseConfig(const YAML::Node& config) {
  McpCompletionPickupParams result;
  result.environment_name = config["environment_name"].as<std::string>(result.environment_name);
  result.consumer_name = config["consumer_name"].as<std::string>(result.consumer_name);
  if (config["visible_server_ids"]) {
    for (const auto& server_id : config["visible_server_ids"]) {
      result.visible_server_ids.push_back(server_id.as<std::string>());
    }
  }
  result.completion_queue_capacity = config["completion_queue_capacity"].as<size_t>(result.completion_queue_capacity);
  result.local_debug_enabled = config["local_debug_enabled"].as<bool>(result.local_debug_enabled);
  result.owns_consumer_registration = config["owns_consumer_registration"].as<bool>(result.owns_consumer_registration);
  result.drain_limit = config["drain_limit"].as<size_t>(result.drain_limit);
  if (result.drain_limit == 0 || result.completion_queue_capacity == 0) {
    throw std::invalid_argument("MCP completion queue capacity and drain limit must be positive");
  }
  return result;
}

} // namespace giga_edge
