#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/mcp/contracts/types.h"
#include "giga_edge/modules/mcp/coordinator.h"
#include "giga_edge/modules/mcp/nodes/mcp_catalog_snapshot/params.h"

#include <memory>
#include <string>

namespace giga_edge {

class McpCatalogSnapshotNode : public NodeBase {
public:
  McpCatalogSnapshotNode(const NodeBaseConfig& base_config, const McpCatalogSnapshotParams& params);
  ~McpCatalogSnapshotNode() override;

  void ProcessOnce() override;
  void Reset() override {}

  REGISTER_INPUT(
    mcp_catalog_snapshot,
    user_query,
    UserQuery,
    UserQuery,
    "UserQuery tick that triggers reading current MCP coordinator snapshots."
  );
  REGISTER_OUTPUT(
    mcp_catalog_snapshot,
    mcp_catalog_snapshot,
    McpCatalogSnapshot,
    McpCatalogSnapshot,
    "Prompt-format-agnostic MCP tools/resources/latest-state snapshot."
  );

private:
  McpCatalogSnapshotParams params_;
  std::string init_error_;
  std::shared_ptr<mcp::IMcpCoordinator> coordinator_;
  mcp::McpConsumerId consumer_id_ = 0;
  std::string last_debug_logged_revision_;
};

REGISTER_NODE(
  mcp_catalog_snapshot,
  McpCatalogSnapshotNode,
  McpCatalogSnapshotParams,
  "Reads MCP coordinator snapshots into a prompt-format-agnostic catalog snapshot."
);

} // namespace giga_edge
