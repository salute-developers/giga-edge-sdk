#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/mcp/contracts/types.h"
#include "giga_edge/modules/mcp/coordinator.h"
#include "giga_edge/modules/mcp/nodes/mcp_action_executor/params.h"

#include <cstdint>
#include <memory>
#include <string>

namespace giga_edge {

class McpActionExecutorNode : public NodeBase {
public:
  McpActionExecutorNode(const NodeBaseConfig& base_config, const McpActionExecutorParams& params);
  ~McpActionExecutorNode() override;

  void ProcessOnce() override;
  void Reset() override {}

private:
  void DoInitRecursiveOutputs() override;
  McpActionSubmitReport ExecuteAction(const McpResolvedAction& action);
  std::string NextCallId();

  REGISTER_INPUT(
    mcp_action_executor,
    mcp_resolved_action,
    McpResolvedAction,
    McpResolvedAction,
    "Validated MCP action to submit asynchronously."
  );

  REGISTER_RECURSIVE_OUTPUT(
    mcp_action_executor, mcp_action_result, McpActionResult, McpActionSubmitReport, "Immediate MCP submission report"
  );

private:
  McpActionExecutorParams params_;
  std::shared_ptr<mcp::IMcpCoordinator> coordinator_;
  mcp::McpConsumerId consumer_id_ = 0;
  std::string init_error_;
  std::uint64_t next_call_id_ = 1;
};

REGISTER_NODE(
  mcp_action_executor, McpActionExecutorNode, McpActionExecutorParams, "Submits resolved MCP actions asynchronously"
);

} // namespace giga_edge
