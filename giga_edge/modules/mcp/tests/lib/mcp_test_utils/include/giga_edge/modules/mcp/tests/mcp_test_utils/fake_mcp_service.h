#pragma once

#include "giga_edge/modules/mcp/service.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace giga_edge {
namespace mcp {
namespace test_utils {

class FakeMcpService : public IMcpService {
public:
  void Shutdown() override {}

  struct SubscriptionOperationCall {
    McpSubscriptionOperationId operation_id = 0;
    std::string server_id;
    std::string uri;
    McpResourceSubscriptionEvent::Operation operation = McpResourceSubscriptionEvent::Operation::SUBSCRIBE;
  };

  void SetObserver(std::weak_ptr<IMcpServiceObserver> observer) override;

  void AddServer(const std::string& server_id, ConnectionPtr connection) override;
  void RemoveServer(const std::string& server_id) override;

  bool CallToolAsync(McpRequestToken token, const McpToolCallRequest& request) override;
  std::vector<McpToolDescriptor> GetToolsSnapshot() const override;
  std::vector<std::string> GetServerIds() const override;

  std::vector<McpResourceDescriptor> GetResourcesSnapshot() const override;
  bool ReadResourceAsync(McpRequestToken token, const McpReadResourceRequest& request) override;
  std::vector<McpResourceTemplateDescriptor> GetResourceTemplatesSnapshot() const override;
  bool SubscribeResourceAsync(
    McpSubscriptionOperationId operation_id, const std::string& server_id, const std::string& uri
  ) override;
  bool UnsubscribeResourceAsync(
    McpSubscriptionOperationId operation_id, const std::string& server_id, const std::string& uri
  ) override;

  bool PingAsync(McpRequestToken token, const McpPingRequest& request) override;
  bool Cancel(McpRequestToken token) override;

  void EmitTools(std::vector<McpToolDescriptor> tools);
  void EmitResources(std::vector<McpResourceDescriptor> resources);
  void EmitResourceTemplates(std::vector<McpResourceTemplateDescriptor> resource_templates);
  void EmitToolCompletion(McpRequestToken token, McpToolCallResultEvent result);
  void EmitReadCompletion(McpRequestToken token, McpReadResourceResultEvent result);
  void EmitPingCompletion(McpRequestToken token, McpPingResultEvent result);
  // payload is the JSON of the resource content; the state is built the same way the service publishes it after
  // resources/read.
  void EmitResourceUpdate(const std::string& server_id, const std::string& uri, const Json::Value& payload);
  void EmitResourceSubscriptionResult(McpResourceSubscriptionEvent event);
  void EmitServerDisconnected(const std::string& server_id);
  void EmitServerError(const std::string& server_id, const std::string& message = "");

  bool accept_requests = true;
  bool accept_subscriptions = true;
  bool accept_unsubscriptions = true;
  bool cancel_result = true;
  bool auto_complete_subscriptions = true;
  bool auto_complete_unsubscriptions = true;

  std::vector<McpToolDescriptor> tools_snapshot;
  std::vector<McpResourceDescriptor> resources_snapshot;
  std::vector<McpResourceTemplateDescriptor> resource_templates_snapshot;
  std::vector<std::string> server_ids;

  McpToolCallRequest last_tool_request;
  McpReadResourceRequest last_read_request;
  McpPingRequest last_ping_request;

  std::vector<McpRequestToken> submitted_tool_tokens;
  std::vector<McpRequestToken> submitted_read_tokens;
  std::vector<McpRequestToken> submitted_ping_tokens;
  std::vector<McpRequestToken> cancelled_tokens;
  std::vector<SubscriptionOperationCall> subscription_operation_calls;

private:
  std::weak_ptr<IMcpServiceObserver> observer_;
};

} // namespace test_utils
} // namespace mcp
} // namespace giga_edge
