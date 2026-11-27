#pragma once

#include "giga_edge/modules/mcp/client.h"
#include "giga_edge/modules/mcp/service/request_cancellation.h"
#include "giga_edge/modules/mcp/types.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace giga_edge {
namespace mcp {
namespace detail {

struct PendingClientResolution {
  std::shared_ptr<IMcpClient> client;
  std::shared_ptr<RequestCancellation> cancellation;
  bool request_missing = false;
  bool server_unavailable = false;
};

template<class ServerState, class PendingRequest, class ServerStatus>
PendingClientResolution ResolvePendingReadyClient(
  McpRequestToken token,
  const std::string& server_id,
  ServerStatus ready_status,
  std::unordered_map<std::string, ServerState>& servers,
  std::unordered_map<McpRequestToken, PendingRequest>& pending_requests
) {
  PendingClientResolution resolution;

  auto pending_it = pending_requests.find(token);
  if (pending_it == pending_requests.end()) {
    resolution.request_missing = true;
    return resolution;
  }

  auto server_it = servers.find(server_id);
  if (server_it == servers.end() || !server_it->second.client || server_it->second.status != ready_status) {
    pending_requests.erase(pending_it);
    resolution.server_unavailable = true;
    return resolution;
  }

  resolution.client = server_it->second.client;
  resolution.cancellation = pending_it->second.cancellation;
  return resolution;
}

inline IConnection::SendGate MakeSendGate(const PendingClientResolution& resolution) {
  return [cancellation = resolution.cancellation](const std::string& request_id) {
    return cancellation->BeginSend(request_id);
  };
}

template<class PendingRequest>
bool ClearPendingRequest(McpRequestToken token, std::unordered_map<McpRequestToken, PendingRequest>& pending_requests) {
  auto it = pending_requests.find(token);
  if (it == pending_requests.end()) {
    return false;
  }
  pending_requests.erase(it);
  return true;
}

inline McpToolCallResultEvent MakeServerUnavailableToolResult(const McpToolCallRequest& request) {
  return McpToolCallResultEvent{
    .call_id = request.call_id,
    .server_id = request.server_id,
    .tool_name = request.tool_name,
    .is_error = true,
    .error_message = "Server not available: " + request.server_id,
    .error_code = ErrorCode::TRANSPORT_ERROR,
  };
}

inline McpReadResourceResultEvent MakeServerUnavailableReadResult(const McpReadResourceRequest& request) {
  return McpReadResourceResultEvent{
    .call_id = request.call_id,
    .server_id = request.server_id,
    .uri = request.uri,
    .is_error = true,
    .error_message = "Server not available: " + request.server_id,
  };
}

inline McpPingResultEvent MakeServerUnavailablePingResult(const McpPingRequest& request) {
  return McpPingResultEvent{
    .call_id = request.call_id,
    .server_id = request.server_id,
    .is_error = true,
    .error_message = "Server not available: " + request.server_id,
  };
}

inline McpResourceSubscriptionEvent MakeSubscriptionEvent(
  McpSubscriptionOperationId operation_id,
  const std::string& server_id,
  const std::string& uri,
  McpResourceSubscriptionEvent::Operation operation,
  bool is_error = false,
  std::string error_message = {}
) {
  return McpResourceSubscriptionEvent{
    .operation_id = operation_id,
    .server_id = server_id,
    .uri = uri,
    .operation = operation,
    .is_error = is_error,
    .error_message = std::move(error_message),
  };
}

} // namespace detail
} // namespace mcp
} // namespace giga_edge
