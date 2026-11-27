#include "giga_edge/modules/mcp/nodes/mcp_tool_specs/node.h"

namespace giga_edge {

McpToolSpecsNode::McpToolSpecsNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void McpToolSpecsNode::ProcessOnce() {
  const auto& snapshot = GetMcpCatalogSnapshot();
  ToolSpecs out;
  out.status = snapshot.status;
  out.error = snapshot.error;
  out.revision = snapshot.revision;
  out.tools.reserve(snapshot.tools.size());
  for (const auto& tool : snapshot.tools) {
    out.tools.push_back(ToolSpec{tool.tool_name, tool.description, tool.input_schema_json});
  }
  PushToolSpecs(std::move(out));
}

} // namespace giga_edge
