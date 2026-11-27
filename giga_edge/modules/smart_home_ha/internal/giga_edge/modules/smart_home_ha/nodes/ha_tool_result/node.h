#pragma once
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/smart_home_ha/actions.h"
namespace giga_edge {
class HaToolResultNode : public NodeBase {
public:
  explicit HaToolResultNode(const NodeBaseConfig& base)
    : NodeBase(base) {}
  void Reset() override {}
  void ProcessOnce() override;
private:
  REGISTER_INPUT(
    ha_tool_result, mcp_completions, Completions, McpActionCompletions, "Correlated runtime MCP completions."
  );
  REGISTER_OUTPUT(
    ha_tool_result, tool_results, Results, CompactToolResults, "Learned function result values and error codes."
  );
  REGISTER_OUTPUT(
    ha_tool_result,
    continuations,
    Continuations,
    std::vector<McpResolvedAction>,
    "Validated continuation after a fresh read."
  );
};
REGISTER_NODE(ha_tool_result, HaToolResultNode, "Normalizes HA intent/read replies for fc_tool_result.");
} // namespace giga_edge
