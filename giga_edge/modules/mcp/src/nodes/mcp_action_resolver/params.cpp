#include "giga_edge/modules/mcp/nodes/mcp_action_resolver/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

McpGenericProfileParams ParseMcpGenericProfileParams(const YAML::Node& config) {
  McpGenericProfileParams result;
  if (!config) {
    return result;
  }
  result.call_tool_action_name = config["call_tool_action_name"].as<std::string>(result.call_tool_action_name);
  result.read_resource_action_name = config["read_resource_action_name"].as<std::string>(
    result.read_resource_action_name
  );
  result.subscribe_resource_action_name = config["subscribe_resource_action_name"].as<std::string>(
    result.subscribe_resource_action_name
  );
  result.unsubscribe_resource_action_name = config["unsubscribe_resource_action_name"].as<std::string>(
    result.unsubscribe_resource_action_name
  );
  result.server_id_field = config["server_id_field"].as<std::string>(result.server_id_field);
  result.tool_name_field = config["tool_name_field"].as<std::string>(result.tool_name_field);
  result.uri_field = config["uri_field"].as<std::string>(result.uri_field);
  result.arguments_json_field = config["arguments_json_field"].as<std::string>(result.arguments_json_field);
  result.arguments_field = config["arguments_field"].as<std::string>(result.arguments_field);
  result.allow_direct_tool_actions = config["allow_direct_tool_actions"].as<bool>(result.allow_direct_tool_actions);
  return result;
}

template<>
McpActionResolverParams ParseConfig(const YAML::Node& config) {
  McpActionResolverParams result;
  result.profile = mcp::ParseNamedParams(config["profile"], "generic_mcp_v1");
  for (const auto& server_id : config["supported_server_ids"]) {
    result.supported_server_ids.push_back(server_id.as<std::string>());
  }
  result.require_single_action = config["require_single_action"].as<bool>(result.require_single_action);
  result.local_debug_enabled = config["local_debug_enabled"].as<bool>(result.local_debug_enabled);
  return result;
}

} // namespace giga_edge
