#pragma once
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/mcp/contracts/types.h"
#include "giga_edge/modules/mcp/coordinator.h"
#include "giga_edge/modules/mcp/nodes/mcp_completion_pickup/params.h"

#include <map>

namespace giga_edge {
class McpCompletionPickupNode : public NodeBase {
public:
  McpCompletionPickupNode(const NodeBaseConfig&, const McpCompletionPickupParams&);
  ~McpCompletionPickupNode() override;
  void ProcessOnce() override;
  void Reset() override;
private:
  REGISTER_INPUT(mcp_completion_pickup, user_query, UserQuery, UserQuery, "Polling tick.");
  REGISTER_OPTIONAL_INPUT(
    mcp_completion_pickup, dialogue_reset, DialogueReset, bool, "Cancels pending requests on reset."
  );
  REGISTER_INPUT(
    mcp_completion_pickup,
    submission,
    Submission,
    McpActionSubmitReport,
    "Previous tick's recursive executor report, including caller context."
  );
  REGISTER_OUTPUT(
    mcp_completion_pickup,
    mcp_completions,
    Completions,
    McpActionCompletions,
    "All ready MCP results with original request context; no prompt formatting."
  );
  struct Orphan {
    McpActionCompletion completion;
    size_t age = 0;
  };

  void Complete(mcp::McpRequestToken token, McpActionCompletion completion, McpActionCompletions& out);

  McpCompletionPickupParams params_;
  std::shared_ptr<mcp::IMcpCoordinator> coordinator_;
  mcp::McpConsumerId consumer_id_ = 0;
  std::string init_error_;
  std::map<mcp::McpRequestToken, McpResolvedAction> pending_;
  std::map<mcp::McpRequestToken, Orphan> orphans_;
};
REGISTER_NODE(
  mcp_completion_pickup,
  McpCompletionPickupNode,
  McpCompletionPickupParams,
  "Collects MCP completions and correlates them with submitted requests."
);
} // namespace giga_edge
