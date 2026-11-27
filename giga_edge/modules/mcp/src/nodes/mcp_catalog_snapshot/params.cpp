#include "giga_edge/modules/mcp/nodes/mcp_catalog_snapshot/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
McpCatalogSnapshotParams ParseConfig(const YAML::Node& config) {
  McpCatalogSnapshotParams result;
  result.environment_name = config["environment_name"].as<std::string>(result.environment_name);
  result.consumer_name = config["consumer_name"].as<std::string>(result.consumer_name);
  result.visible_server_ids = config["visible_server_ids"].as<std::vector<std::string>>(result.visible_server_ids);
  result.include_shared_latest_resource_states = config["include_shared_latest_resource_states"].as<bool>(
    result.include_shared_latest_resource_states
  );
  result.local_debug_enabled = config["local_debug_enabled"].as<bool>(result.local_debug_enabled);

  result.include_tools = config["include_tools"].as<bool>(result.include_tools);
  result.include_resources = config["include_resources"].as<bool>(result.include_resources);
  result.include_resource_templates = config["include_resource_templates"].as<bool>(result.include_resource_templates);
  result.include_latest_resource_states = config["include_latest_resource_states"].as<bool>(
    result.include_latest_resource_states
  );
  return result;
}

} // namespace giga_edge
