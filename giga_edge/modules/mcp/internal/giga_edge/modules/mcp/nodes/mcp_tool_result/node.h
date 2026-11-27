#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/mcp/contracts/types.h"
#include "giga_edge/modules/mcp/nodes/mcp_tool_result/params.h"
#include "giga_edge/modules/mcp/profiles.h"

#include <deque>
#include <memory>

namespace giga_edge {

class McpToolResultNode : public NodeBase {
public:
  McpToolResultNode(const NodeBaseConfig& base_config, const McpToolResultParams& params);

  void ProcessOnce() override;
  void Reset() override {
    pending_.clear();
  }

private:
  ToolResult Format(const McpActionCompletion& completion) const;

  REGISTER_INPUT(mcp_tool_result, mcp_completions, McpCompletions, McpActionCompletions, "Correlated MCP completions.");
  REGISTER_OUTPUT(
    mcp_tool_result, tool_result, ToolResult, ToolResult, "Next formatted completion as a ToolResult, or an empty one."
  );

  McpToolResultParams params_;
  std::shared_ptr<mcp::IResultFormat> format_;
  std::deque<ToolResult> pending_;
};

REGISTER_NODE(
  mcp_tool_result,
  McpToolResultNode,
  McpToolResultParams,
  "Formats correlated MCP completions for the model with a result format chosen by name, one per tick."
);

} // namespace giga_edge
