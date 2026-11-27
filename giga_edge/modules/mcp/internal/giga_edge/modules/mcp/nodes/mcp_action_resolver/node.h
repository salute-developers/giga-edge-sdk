#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/mcp/contracts/types.h"
#include "giga_edge/modules/mcp/nodes/mcp_action_resolver/params.h"
#include "giga_edge/modules/mcp/profiles.h"

#include <memory>
#include <string>

namespace giga_edge {

class McpActionResolverNode : public NodeBase {
public:
  McpActionResolverNode(const NodeBaseConfig& base_config, const McpActionResolverParams& params);

  void ProcessOnce() override;
  void Reset() override {}

  REGISTER_INPUT(
    mcp_action_resolver, assistant_result, AssistantResult, AssistantResult, "Final assistant result with tool calls."
  );
  REGISTER_INPUT(
    mcp_action_resolver,
    mcp_catalog_snapshot,
    McpCatalogSnapshot,
    McpCatalogSnapshot,
    "Catalog used to resolve and validate the call."
  );
  REGISTER_OUTPUT(
    mcp_action_resolver, mcp_resolved_action, McpResolvedAction, McpResolvedAction, "Validated MCP action."
  );

private:
  McpResolvedAction ResolveAction(const AssistantResult& assistant_result, const McpCatalogSnapshot& snapshot) const;
  McpResolvedAction Validate(McpResolvedAction action, const McpCatalogSnapshot& snapshot) const;

  McpActionResolverParams params_;
  std::shared_ptr<mcp::IActionProfile> profile_;
};

REGISTER_NODE(
  mcp_action_resolver,
  McpActionResolverNode,
  McpActionResolverParams,
  "Turns the assistant's tool call into an MCP action with a profile chosen by name and validates it against the "
  "catalog."
);

} // namespace giga_edge
