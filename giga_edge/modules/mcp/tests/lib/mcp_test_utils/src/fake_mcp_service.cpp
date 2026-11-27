#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_mcp_service.h"

#include "giga_edge/modules/mcp/resource_state.h"

namespace giga_edge {
namespace mcp {
namespace test_utils {

void FakeMcpService::SetObserver(std::weak_ptr<IMcpServiceObserver> observer) {
  observer_ = std::move(observer);
}

void FakeMcpService::AddServer(const std::string&, ConnectionPtr) {}

void FakeMcpService::RemoveServer(const std::string&) {}

bool FakeMcpService::CallToolAsync(McpRequestToken token, const McpToolCallRequest& request) {
  last_tool_request = request;
  submitted_tool_tokens.push_back(token);
  return accept_requests;
}

std::vector<McpToolDescriptor> FakeMcpService::GetToolsSnapshot() const {
  return tools_snapshot;
}

std::vector<std::string> FakeMcpService::GetServerIds() const {
  return server_ids;
}

std::vector<McpResourceDescriptor> FakeMcpService::GetResourcesSnapshot() const {
  return resources_snapshot;
}

bool FakeMcpService::ReadResourceAsync(McpRequestToken token, const McpReadResourceRequest& request) {
  last_read_request = request;
  submitted_read_tokens.push_back(token);
  return accept_requests;
}

std::vector<McpResourceTemplateDescriptor> FakeMcpService::GetResourceTemplatesSnapshot() const {
  return resource_templates_snapshot;
}

bool FakeMcpService::SubscribeResourceAsync(
  McpSubscriptionOperationId operation_id, const std::string& server_id, const std::string& uri
) {
  subscription_operation_calls.push_back(
    SubscriptionOperationCall{
      .operation_id = operation_id,
      .server_id = server_id,
      .uri = uri,
      .operation = McpResourceSubscriptionEvent::Operation::SUBSCRIBE,
    }
  );
  if (accept_subscriptions && auto_complete_subscriptions) {
    EmitResourceSubscriptionResult(
      McpResourceSubscriptionEvent{
        .operation_id = operation_id,
        .server_id = server_id,
        .uri = uri,
        .operation = McpResourceSubscriptionEvent::Operation::SUBSCRIBE,
      }
    );
  }
  return accept_subscriptions;
}

bool FakeMcpService::UnsubscribeResourceAsync(
  McpSubscriptionOperationId operation_id, const std::string& server_id, const std::string& uri
) {
  subscription_operation_calls.push_back(
    SubscriptionOperationCall{
      .operation_id = operation_id,
      .server_id = server_id,
      .uri = uri,
      .operation = McpResourceSubscriptionEvent::Operation::UNSUBSCRIBE,
    }
  );
  if (accept_unsubscriptions && auto_complete_unsubscriptions) {
    EmitResourceSubscriptionResult(
      McpResourceSubscriptionEvent{
        .operation_id = operation_id,
        .server_id = server_id,
        .uri = uri,
        .operation = McpResourceSubscriptionEvent::Operation::UNSUBSCRIBE,
      }
    );
  }
  return accept_unsubscriptions;
}

bool FakeMcpService::PingAsync(McpRequestToken token, const McpPingRequest& request) {
  last_ping_request = request;
  submitted_ping_tokens.push_back(token);
  return accept_requests;
}

bool FakeMcpService::Cancel(McpRequestToken token) {
  cancelled_tokens.push_back(token);
  return cancel_result;
}

void FakeMcpService::EmitTools(std::vector<McpToolDescriptor> tools) {
  tools_snapshot = std::move(tools);
  if (auto observer = observer_.lock()) {
    observer->OnToolsSnapshotUpdated(tools_snapshot);
  }
}

void FakeMcpService::EmitResources(std::vector<McpResourceDescriptor> resources) {
  resources_snapshot = std::move(resources);
  if (auto observer = observer_.lock()) {
    observer->OnResourcesSnapshotUpdated(resources_snapshot);
  }
}

void FakeMcpService::EmitResourceTemplates(std::vector<McpResourceTemplateDescriptor> resource_templates) {
  resource_templates_snapshot = std::move(resource_templates);
  if (auto observer = observer_.lock()) {
    observer->OnResourceTemplatesSnapshotUpdated(resource_templates_snapshot);
  }
}

void FakeMcpService::EmitToolCompletion(McpRequestToken token, McpToolCallResultEvent result) {
  if (auto observer = observer_.lock()) {
    observer->OnToolCallCompleted(token, std::move(result));
  }
}

void FakeMcpService::EmitReadCompletion(McpRequestToken token, McpReadResourceResultEvent result) {
  if (auto observer = observer_.lock()) {
    observer->OnReadResourceCompleted(token, std::move(result));
  }
}

void FakeMcpService::EmitPingCompletion(McpRequestToken token, McpPingResultEvent result) {
  if (auto observer = observer_.lock()) {
    observer->OnPingCompleted(token, std::move(result));
  }
}

void FakeMcpService::EmitResourceUpdate(
  const std::string& server_id, const std::string& uri, const Json::Value& payload
) {
  if (auto observer = observer_.lock()) {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    observer->OnResourceUpdated(
      McpResourceUpdatedEvent{
        .server_id = server_id,
        .uri = uri,
        .data = BuildResourceContentState(
          ResourceContent{.uri = uri, .mime_type = "application/json", .text = Json::writeString(writer, payload)}
        ),
      }
    );
  }
}

void FakeMcpService::EmitResourceSubscriptionResult(McpResourceSubscriptionEvent event) {
  if (auto observer = observer_.lock()) {
    observer->OnResourceSubscriptionUpdated(std::move(event));
  }
}

void FakeMcpService::EmitServerDisconnected(const std::string& server_id) {
  if (auto observer = observer_.lock()) {
    observer->OnServerStatusChanged(
      McpServerStatusEvent{
        .server_id = server_id,
        .status = McpServerStatusEvent::Status::DISCONNECTED,
      }
    );
  }
}

void FakeMcpService::EmitServerError(const std::string& server_id, const std::string& message) {
  if (auto observer = observer_.lock()) {
    observer->OnServerStatusChanged(
      McpServerStatusEvent{
        .server_id = server_id,
        .status = McpServerStatusEvent::Status::ERROR,
        .message = message,
      }
    );
  }
}

} // namespace test_utils
} // namespace mcp
} // namespace giga_edge
