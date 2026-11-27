#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_connection.h"

#include <algorithm>

namespace giga_edge {
namespace mcp {
namespace test_utils {

void FakeConnection::EnqueueSuccess(const std::string& method, Json::Value payload, const std::string& request_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  Result<Json::Value> result;
  result.value = std::move(payload);
  queued_responses_[method].push_back(
    QueuedResponse{
      .response = std::move(result),
      .request_id = request_id,
    }
  );
}

void FakeConnection::EnqueueError(
  const std::string& method,
  ErrorCode code,
  const std::string& message,
  Json::Value data,
  const std::string& request_id,
  std::optional<int32_t> json_rpc_code
) {
  std::lock_guard<std::mutex> lock(mutex_);
  Result<Json::Value> result;
  result.error.code = code;
  result.error.message = message;
  result.error.data = std::move(data);
  result.error.json_rpc_code = json_rpc_code;
  queued_responses_[method].push_back(
    QueuedResponse{
      .response = std::move(result),
      .request_id = request_id,
    }
  );
}

void FakeConnection::EmitNotification(const std::string& method, const Json::Value& params) {
  NotificationHandler handler;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    handler = notification_handler_;
  }
  if (handler) {
    handler(method, params);
  }
}

void FakeConnection::BlockRequest(const std::string& request_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  blocked_requests_.insert(request_id);
}

void FakeConnection::UnblockRequest(const std::string& request_id) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    blocked_requests_.erase(request_id);
  }
  cv_.notify_all();
}

void FakeConnection::BlockMethod(const std::string& method) {
  std::lock_guard<std::mutex> lock(mutex_);
  blocked_methods_.insert(method);
}

void FakeConnection::UnblockMethod(const std::string& method) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    blocked_methods_.erase(method);
  }
  cv_.notify_all();
}

Result<Json::Value> FakeConnection::sendRequest(
  const std::string& method, const Json::Value& params, std::string& out_request_id, uint32_t timeout_ms
) {
  return sendRequestWithGate(method, params, out_request_id, timeout_ms, {});
}

Result<Json::Value> FakeConnection::sendRequestWithGate(
  const std::string& method,
  const Json::Value& params,
  std::string& out_request_id,
  uint32_t timeout_ms,
  const SendGate& gate
) {
  std::unique_lock<std::mutex> lock(mutex_);
  RequestCall call{
    .method = method,
    .params = params,
    .timeout_ms = timeout_ms,
    .started_at = std::chrono::steady_clock::now(),
  };

  // A resources/read response with a uri goes only to the read of that uri, as with a real server; otherwise, on
  // concurrent reads, the shared queue of the method hands out responses in the order the mutex is taken. The other
  // responses go in FIFO order.
  auto it = queued_responses_.find(method);
  std::deque<QueuedResponse>::iterator queued_it;
  if (it != queued_responses_.end() && !it->second.empty()) {
    auto& queue = it->second;
    queued_it = std::find_if(queue.begin(), queue.end(), [&params](const QueuedResponse& queued) {
      const Json::Value& value = queued.response.value;
      if (
        !value.isObject() || !value["contents"].isArray() || value["contents"].empty()
        || !value["contents"][0].isObject() || !params.isObject()
      ) {
        return true;
      }
      const Json::Value& uri = value["contents"][0]["uri"];
      return !uri.isString() || uri == params["uri"];
    });
    if (queued_it == queue.end()) {
      queued_it = queue.begin();
    }
  }
  const bool has_response = it != queued_responses_.end() && !it->second.empty();
  out_request_id = has_response && !queued_it->request_id.empty()
    ? queued_it->request_id
    : "fake-request-" + std::to_string(next_request_id_++);

  // "Writing" a request means putting it into request_calls_: the fake does not see a request cancelled before that.
  if (gate && !gate(out_request_id)) {
    Result<Json::Value> result;
    result.error.code = ErrorCode::CANCELLED;
    result.error.message = "MCP request cancelled before sending";
    return result;
  }

  const auto call_index = request_calls_.size();
  call.request_id = out_request_id;
  if (!has_response) {
    call.completed_at = std::chrono::steady_clock::now();
    request_calls_.push_back(call);
    Result<Json::Value> result;
    result.error.code = ErrorCode::INTERNAL_ERROR;
    result.error.message = "No queued fake response for method: " + method;
    result.error.data = Json::Value(Json::nullValue);
    return result;
  }
  auto queued = std::move(*queued_it);
  it->second.erase(queued_it);
  request_calls_.push_back(call);

  const auto request_id = out_request_id;
  cv_.wait(lock, [&]() {
    return abandoned_requests_.count(request_id) != 0
      || (blocked_methods_.count(method) == 0 && blocked_requests_.count(request_id) == 0);
  });
  request_calls_[call_index].completed_at = std::chrono::steady_clock::now();
  if (abandoned_requests_.erase(request_id) != 0) {
    Result<Json::Value> result;
    result.error.code = ErrorCode::CANCELLED;
    result.error.message = "MCP request abandoned";
    return result;
  }
  return queued.response;
}

void FakeConnection::abandonRequest(const std::string& request_id) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool waiting = std::any_of(request_calls_.begin(), request_calls_.end(), [&](const RequestCall& call) {
      return call.request_id == request_id && call.completed_at == std::chrono::steady_clock::time_point{};
    });
    if (!waiting) {
      return;
    }
    abandoned_requests_.insert(request_id);
  }
  cv_.notify_all();
}

void FakeConnection::sendNotification(const std::string& method, const Json::Value& params) {
  std::lock_guard<std::mutex> lock(mutex_);
  notification_calls_.push_back(
    NotificationCall{
      .method = method,
      .params = params,
    }
  );
}

void FakeConnection::setNotificationHandler(NotificationHandler handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  notification_handler_ = std::move(handler);
}

void FakeConnection::setCloseHandler(CloseHandler handler) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!handler || !closed_by_server_) {
      close_handler_ = std::move(handler);
      return;
    }
    closed_by_server_ = false;
  }
  handler();
}

void FakeConnection::SimulateClose() {
  CloseHandler handler;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
    handler = std::move(close_handler_);
    closed_by_server_ = !handler;
  }
  cv_.notify_all();
  if (handler) {
    handler();
  }
}

void FakeConnection::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  connected_ = false;
  ++close_count_;
  cv_.notify_all();
}

bool FakeConnection::isConnected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return connected_;
}

void FakeConnection::SetConnected(bool connected) {
  std::lock_guard<std::mutex> lock(mutex_);
  connected_ = connected;
}

std::vector<FakeConnection::RequestCall> FakeConnection::RequestCalls() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return request_calls_;
}

std::vector<FakeConnection::NotificationCall> FakeConnection::NotificationCalls() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return notification_calls_;
}

size_t FakeConnection::CloseCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return close_count_;
}

} // namespace test_utils
} // namespace mcp
} // namespace giga_edge
