#pragma once
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/smart_home_ha/actions.h"
namespace giga_edge {
class HaActionResolverNode : public NodeBase {
public:
  explicit HaActionResolverNode(const NodeBaseConfig& base)
    : NodeBase(base) {}
  void ProcessOnce() override;
  void Reset() override {
    expected_generation_.clear();
    continuation_call_.clear();
  }
private:
  REGISTER_INPUT(
    ha_action_resolver, user_query, Query, UserQuery, "New logical request; invalidates previous continuations."
  );
  REGISTER_INPUT(ha_action_resolver, dialogue_reset, ResetFlag, bool, "Cancellation or dialogue reset.");
  REGISTER_INPUT(ha_action_resolver, assistant_result, Result, AssistantResult, "Guarded final model result.");
  REGISTER_INPUT(ha_action_resolver, ha_catalog, Catalog, ha::CatalogPtr, "Normalized Home Assistant targets.");
  REGISTER_INPUT(
    ha_action_resolver,
    continuations,
    Continuations,
    std::vector<McpResolvedAction>,
    "Correlated continuations of pending reads and writes."
  );
  REGISTER_OUTPUT(
    ha_action_resolver, mcp_resolved_action, Action, McpResolvedAction, "Native HA tool call for the runtime executor."
  );
  std::string expected_generation_, continuation_call_;
};
REGISTER_NODE(
  ha_action_resolver, HaActionResolverNode, "Resolves compact model actions to native Home Assistant MCP tools."
);
} // namespace giga_edge
