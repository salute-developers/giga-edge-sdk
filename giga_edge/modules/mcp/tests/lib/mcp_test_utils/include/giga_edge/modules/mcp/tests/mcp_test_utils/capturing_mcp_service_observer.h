#pragma once

#include "giga_edge/modules/mcp/service.h"

#include <mutex>
#include <vector>

namespace giga_edge {
namespace mcp {
namespace test_utils {

class CapturingMcpServiceObserver
  : public IMcpServiceObserver,
    public std::enable_shared_from_this<CapturingMcpServiceObserver> {
public:
  void OnToolsSnapshotUpdated(std::vector<McpToolDescriptor> tools) override {
    std::lock_guard<std::mutex> lock(mutex);
    tool_snapshots.push_back(std::move(tools));
  }

  void OnResourcesSnapshotUpdated(std::vector<McpResourceDescriptor> resources) override {
    std::lock_guard<std::mutex> lock(mutex);
    resource_snapshots.push_back(std::move(resources));
  }

  void OnResourceTemplatesSnapshotUpdated(std::vector<McpResourceTemplateDescriptor> resource_templates) override {
    std::lock_guard<std::mutex> lock(mutex);
    resource_template_snapshots.push_back(std::move(resource_templates));
  }

  void OnToolCallCompleted(McpRequestToken token, McpToolCallResultEvent result) override {
    std::lock_guard<std::mutex> lock(mutex);
    tool_results.push_back(
      McpCompletion<McpToolCallResultEvent>{
        .token = token,
        .result = std::move(result),
      }
    );
  }

  void OnReadResourceCompleted(McpRequestToken token, McpReadResourceResultEvent result) override {
    std::lock_guard<std::mutex> lock(mutex);
    read_results.push_back(
      McpCompletion<McpReadResourceResultEvent>{
        .token = token,
        .result = std::move(result),
      }
    );
  }

  void OnPingCompleted(McpRequestToken token, McpPingResultEvent result) override {
    std::lock_guard<std::mutex> lock(mutex);
    ping_results.push_back(
      McpCompletion<McpPingResultEvent>{
        .token = token,
        .result = std::move(result),
      }
    );
  }

  void OnResourceUpdated(McpResourceUpdatedEvent event) override {
    std::lock_guard<std::mutex> lock(mutex);
    resource_updates.push_back(std::move(event));
  }

  void OnResourceSubscriptionUpdated(McpResourceSubscriptionEvent event) override {
    std::lock_guard<std::mutex> lock(mutex);
    resource_subscription_events.push_back(std::move(event));
  }

  void OnServerStatusChanged(McpServerStatusEvent event) override {
    std::lock_guard<std::mutex> lock(mutex);
    server_status_events.push_back(std::move(event));
  }

  mutable std::mutex mutex;
  std::vector<std::vector<McpToolDescriptor>> tool_snapshots;
  std::vector<std::vector<McpResourceDescriptor>> resource_snapshots;
  std::vector<std::vector<McpResourceTemplateDescriptor>> resource_template_snapshots;
  std::vector<McpCompletion<McpToolCallResultEvent>> tool_results;
  std::vector<McpCompletion<McpReadResourceResultEvent>> read_results;
  std::vector<McpCompletion<McpPingResultEvent>> ping_results;
  std::vector<McpResourceUpdatedEvent> resource_updates;
  std::vector<McpResourceSubscriptionEvent> resource_subscription_events;
  std::vector<McpServerStatusEvent> server_status_events;
};

} // namespace test_utils
} // namespace mcp
} // namespace giga_edge
