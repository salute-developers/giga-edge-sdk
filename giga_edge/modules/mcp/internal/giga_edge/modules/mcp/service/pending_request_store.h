#pragma once

#include "giga_edge/modules/mcp/service/request_cancellation.h"
#include "giga_edge/modules/mcp/types.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace giga_edge {
namespace mcp {
namespace detail {

struct ServicePendingRequest {
  enum class Kind {
    TOOL_CALL,
    READ_RESOURCE,
    PING,
  } kind;

  std::string call_id;
  std::string server_id;
  std::string tool_name;
  std::string uri;
  std::shared_ptr<RequestCancellation> cancellation = std::make_shared<RequestCancellation>();
};

inline ServicePendingRequest MakePendingToolCallRequest(const McpToolCallRequest& request) {
  return ServicePendingRequest{
    .kind = ServicePendingRequest::Kind::TOOL_CALL,
    .call_id = request.call_id,
    .server_id = request.server_id,
    .tool_name = request.tool_name,
  };
}

inline ServicePendingRequest MakePendingReadResourceRequest(const McpReadResourceRequest& request) {
  return ServicePendingRequest{
    .kind = ServicePendingRequest::Kind::READ_RESOURCE,
    .call_id = request.call_id,
    .server_id = request.server_id,
    .uri = request.uri,
  };
}

inline ServicePendingRequest MakePendingPingRequest(const McpPingRequest& request) {
  return ServicePendingRequest{
    .kind = ServicePendingRequest::Kind::PING,
    .call_id = request.call_id,
    .server_id = request.server_id,
  };
}

template<class PendingRequestsMap>
bool StorePendingRequest(
  McpRequestToken token, ServicePendingRequest pending_request, PendingRequestsMap& pending_requests
) {
  return pending_requests.emplace(token, std::move(pending_request)).second;
}

template<class PendingRequestsMap>
std::optional<ServicePendingRequest> TakePendingRequest(McpRequestToken token, PendingRequestsMap& pending_requests) {
  auto it = pending_requests.find(token);
  if (it == pending_requests.end()) {
    return std::nullopt;
  }

  ServicePendingRequest pending = std::move(it->second);
  pending_requests.erase(it);
  return pending;
}

} // namespace detail
} // namespace mcp
} // namespace giga_edge
