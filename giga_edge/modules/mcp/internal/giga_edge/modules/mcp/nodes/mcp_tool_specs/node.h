#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/mcp/contracts/types.h"

namespace giga_edge {

class McpToolSpecsNode : public NodeBase {
public:
  explicit McpToolSpecsNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;
  void Reset() override {}

private:
  REGISTER_INPUT(
    mcp_tool_specs, mcp_catalog_snapshot, McpCatalogSnapshot, McpCatalogSnapshot, "Current MCP catalog snapshot."
  );
  REGISTER_OUTPUT(
    mcp_tool_specs, tool_specs, ToolSpecs, ToolSpecs, "MCP tools as ToolSpecs for native function calling."
  );
};

REGISTER_NODE(mcp_tool_specs, McpToolSpecsNode, "Maps the MCP tool catalog into ToolSpecs.");

} // namespace giga_edge
