#pragma once

#include "giga_edge/modules/mcp/profiles.h"

#include <string>
#include <vector>

namespace giga_edge {

struct McpActionResolverParams {
  mcp::NamedParams profile;
  std::vector<std::string> supported_server_ids;
  bool require_single_action = true;
  bool local_debug_enabled = false;
};

struct McpGenericProfileParams {
  std::string call_tool_action_name = "call_tool";
  std::string read_resource_action_name = "read_resource";
  std::string subscribe_resource_action_name = "subscribe_resource";
  std::string unsubscribe_resource_action_name = "unsubscribe_resource";
  std::string server_id_field = "server_id";
  std::string tool_name_field = "tool_name";
  std::string uri_field = "uri";
  std::string arguments_json_field = "arguments_json";
  std::string arguments_field = "arguments";
  bool allow_direct_tool_actions = true;
};

McpGenericProfileParams ParseMcpGenericProfileParams(const YAML::Node& config);

} // namespace giga_edge
