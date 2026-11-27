#pragma once

#include "giga_edge/modules/mcp/connection.h"
#include "giga_edge/modules/mcp/types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace giga_edge {

class IThreadPool;

namespace mcp {

class IMcpServiceObserver {
public:
  virtual ~IMcpServiceObserver() = default;

  virtual void OnToolsSnapshotUpdated(std::vector<McpToolDescriptor> tools) = 0;
  virtual void OnResourcesSnapshotUpdated(std::vector<McpResourceDescriptor> resources) = 0;
  virtual void OnResourceTemplatesSnapshotUpdated(std::vector<McpResourceTemplateDescriptor> resource_templates) = 0;
  virtual void OnToolCallCompleted(McpRequestToken token, McpToolCallResultEvent result) = 0;
  virtual void OnReadResourceCompleted(McpRequestToken token, McpReadResourceResultEvent result) = 0;
  virtual void OnPingCompleted(McpRequestToken token, McpPingResultEvent result) = 0;
  virtual void OnResourceUpdated(McpResourceUpdatedEvent event) = 0;
  virtual void OnResourceSubscriptionUpdated(McpResourceSubscriptionEvent event) = 0;
  virtual void OnServerStatusChanged(McpServerStatusEvent event) = 0;
};

class IMcpService {
public:
  virtual ~IMcpService() = default;

  // Completes the accepted requests by calling the observer on this thread: do not call it under a lock that the
  // observer takes.
  virtual void Shutdown() = 0;

  virtual void SetObserver(std::weak_ptr<IMcpServiceObserver> observer) = 0;

  virtual void AddServer(const std::string& server_id, ConnectionPtr connection) = 0;
  virtual void RemoveServer(const std::string& server_id) = 0;

  virtual bool CallToolAsync(McpRequestToken token, const McpToolCallRequest& request) = 0;

  virtual std::vector<McpToolDescriptor> GetToolsSnapshot() const = 0;
  virtual std::vector<std::string> GetServerIds() const = 0;

  virtual std::vector<McpResourceDescriptor> GetResourcesSnapshot() const = 0;
  virtual bool ReadResourceAsync(McpRequestToken token, const McpReadResourceRequest& request) = 0;
  virtual std::vector<McpResourceTemplateDescriptor> GetResourceTemplatesSnapshot() const = 0;
  virtual bool SubscribeResourceAsync(
    McpSubscriptionOperationId operation_id, const std::string& server_id, const std::string& uri
  ) = 0;
  virtual bool UnsubscribeResourceAsync(
    McpSubscriptionOperationId operation_id, const std::string& server_id, const std::string& uri
  ) = 0;

  virtual bool PingAsync(McpRequestToken token, const McpPingRequest& request) = 0;
  virtual bool Cancel(McpRequestToken token) = 0;
};

size_t GetMcpResourceRetryCount(const IMcpService& service);

std::shared_ptr<IMcpService> CreateMcpService(McpServiceConfig config);
std::shared_ptr<IMcpService> CreateMcpService(McpServiceConfig config, std::unique_ptr<IThreadPool> thread_pool);

} // namespace mcp
} // namespace giga_edge
