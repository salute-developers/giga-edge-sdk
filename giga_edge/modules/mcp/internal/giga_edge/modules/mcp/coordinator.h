#pragma once

#include "giga_edge/modules/mcp/service.h"
#include "giga_edge/modules/mcp/types.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace giga_edge {
namespace mcp {

class IMcpCoordinator {
public:
  virtual ~IMcpCoordinator() = default;

  virtual McpConsumerId RegisterConsumer(McpConsumerConfig config) = 0;
  // Returns the existing consumer id when consumer_name is already registered; otherwise registers a new one.
  virtual McpConsumerId EnsureConsumer(McpConsumerConfig config) = 0;
  virtual void UnregisterConsumer(McpConsumerId consumer_id) = 0;
  virtual void UpdateConsumerConfig(McpConsumerId consumer_id, McpConsumerConfig config) = 0;

  virtual SubmitResult SubmitToolCall(McpConsumerId consumer_id, const McpToolCallRequest& request) = 0;

  virtual SubmitResult SubmitReadResource(McpConsumerId consumer_id, const McpReadResourceRequest& request) = 0;

  virtual SubmitResult SubmitPing(McpConsumerId consumer_id, const McpPingRequest& request) = 0;

  virtual bool CancelRequest(McpConsumerId consumer_id, McpRequestToken token) = 0;

  virtual TakeBatchResult<McpToolCallResultEvent> TakeToolCallResults(McpConsumerId consumer_id, size_t limit) = 0;

  virtual TakeBatchResult<McpReadResourceResultEvent> TakeReadResourceResults(McpConsumerId consumer_id, size_t limit)
    = 0;

  virtual TakeBatchResult<McpPingResultEvent> TakePingResults(McpConsumerId consumer_id, size_t limit) = 0;

  virtual VersionedSnapshot<std::vector<McpToolDescriptor>> GetToolsSnapshot(McpConsumerId consumer_id) const = 0;

  virtual VersionedSnapshot<std::vector<McpResourceDescriptor>> GetResourcesSnapshot(McpConsumerId consumer_id) const
    = 0;

  virtual VersionedSnapshot<std::vector<McpResourceTemplateDescriptor>> GetResourceTemplatesSnapshot(
    McpConsumerId consumer_id
  ) const = 0;

  virtual bool SubscribeResource(McpConsumerId consumer_id, const std::string& server_id, const std::string& uri) = 0;

  virtual void UnsubscribeResource(McpConsumerId consumer_id, const std::string& server_id, const std::string& uri) = 0;

  virtual VersionedSnapshot<std::vector<McpResourceSubscriptionState>> GetResourceSubscriptionStatesSnapshot(
    McpConsumerId consumer_id
  ) const = 0;

  virtual VersionedSnapshot<std::vector<McpLatestResourceState>> GetLatestResourceStatesSnapshot(
    McpConsumerId consumer_id
  ) const = 0;
};

std::shared_ptr<IMcpCoordinator> CreateMcpCoordinator(std::shared_ptr<IMcpService> service);

} // namespace mcp
} // namespace giga_edge
