#pragma once

#include <string>
#include <vector>

namespace giga_edge {

struct McpCatalogSnapshotParams {
  std::string environment_name = "mcp";
  std::string consumer_name = "mcp_catalog_snapshot";
  std::vector<std::string> visible_server_ids;
  bool include_shared_latest_resource_states = true;
  bool local_debug_enabled = false;

  bool include_tools = true;
  bool include_resources = true;
  bool include_resource_templates = true;
  bool include_latest_resource_states = true;
};

} // namespace giga_edge
