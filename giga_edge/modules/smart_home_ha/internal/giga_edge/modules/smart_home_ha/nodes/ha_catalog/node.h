#pragma once
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/mcp/coordinator.h"
#include "giga_edge/modules/smart_home_ha/catalog.h"

#include <chrono>
namespace giga_edge {
template<>
ha::CatalogParams ParseConfig(const YAML::Node& config);
class HaCatalogNode : public NodeBase {
public:
  HaCatalogNode(const NodeBaseConfig& base, const ha::CatalogParams& params);
  ~HaCatalogNode() override;
  void ProcessOnce() override;
  void Reset() override;
private:
  REGISTER_INPUT(
    ha_catalog, mcp_catalog_snapshot, Snapshot, McpCatalogSnapshot, "Runtime MCP catalog and hydrated context."
  );
  REGISTER_OUTPUT(ha_catalog, ha_catalog, Catalog, ha::CatalogPtr, "Normalized Home Assistant targets.");
  REGISTER_OPTIONAL_OUTPUT(
    ha_catalog, current_room, CurrentRoom, std::string, "Room of the assistant from params; empty — unknown."
  );
  bool AddUnreadContext();
  void WaitForContext();
  McpCatalogSnapshot ReadContext() const;
  // all: re-read the whole context (periodic refresh), otherwise only the unread part.
  void StartNativeRefresh(bool all);
  void PollNativeRefresh();
  void CancelReads();
  bool RefreshDue() const;
  bool UnreadDue() const;
  ha::CatalogParams params_;
  std::shared_ptr<mcp::IMcpCoordinator> coordinator_;
  mcp::McpConsumerId consumer_id_ = 0;
  uint64_t request_index_ = 0;
  ha::CatalogBuilder builder_;
  ha::CatalogPtr cached_;
  // A refresh keeps serving the previous catalog until the new one is complete.
  std::chrono::steady_clock::time_point refreshed_at_ = std::chrono::steady_clock::now();
  // The last read of the unread context: its retries have their own interval and do not postpone the periodic
  // refresh.
  std::chrono::steady_clock::time_point read_at_ = std::chrono::steady_clock::now();
  McpCatalogSnapshot native_;
  std::map<mcp::McpRequestToken, size_t> reads_;
  bool refresh_changed_ = false;
  bool room_checked_ = false;
  AssistantStatus input_status_ = AssistantStatus::OK;
  std::string input_error_;
};
REGISTER_NODE(
  ha_catalog,
  HaCatalogNode,
  ha::CatalogParams,
  "Normalizes the Home Assistant Assist context and re-reads it periodically."
);
} // namespace giga_edge
