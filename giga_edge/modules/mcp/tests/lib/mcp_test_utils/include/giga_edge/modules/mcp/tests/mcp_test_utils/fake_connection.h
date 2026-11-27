#pragma once

#include "giga_edge/modules/mcp/connection.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace giga_edge {
namespace mcp {
namespace test_utils {

class FakeConnection : public IConnection {
public:
  struct QueuedResponse {
    Result<Json::Value> response;
    std::string request_id;
  };

  struct RequestCall {
    std::string method;
    Json::Value params;
    std::string request_id;
    uint32_t timeout_ms = 0;
    std::chrono::steady_clock::time_point started_at;
    std::chrono::steady_clock::time_point completed_at;
  };

  struct NotificationCall {
    std::string method;
    Json::Value params;
  };

  void EnqueueSuccess(const std::string& method, Json::Value payload, const std::string& request_id = "");
  void EnqueueError(
    const std::string& method,
    ErrorCode code,
    const std::string& message,
    Json::Value data = Json::Value(Json::nullValue),
    const std::string& request_id = "",
    std::optional<int32_t> json_rpc_code = std::nullopt
  );
  void EmitNotification(const std::string& method, const Json::Value& params);
  void BlockRequest(const std::string& request_id);
  void UnblockRequest(const std::string& request_id);
  void BlockMethod(const std::string& method);
  void UnblockMethod(const std::string& method);

  Result<Json::Value> sendRequest(
    const std::string& method, const Json::Value& params, std::string& out_request_id, uint32_t timeout_ms = 0
  ) override;
  Result<Json::Value> sendRequestWithGate(
    const std::string& method,
    const Json::Value& params,
    std::string& out_request_id,
    uint32_t timeout_ms,
    const SendGate& gate
  ) override;
  void abandonRequest(const std::string& request_id) override;
  void sendNotification(const std::string& method, const Json::Value& params) override;
  void setNotificationHandler(NotificationHandler handler) override;
  void close() override;
  void setCloseHandler(CloseHandler handler) override;
  // A close from the server side: the close handler is called now or right when it is set.
  void SimulateClose();
  bool isConnected() const override;

  void SetConnected(bool connected);

  std::vector<RequestCall> RequestCalls() const;
  std::vector<NotificationCall> NotificationCalls() const;
  size_t CloseCount() const;

private:
  mutable std::mutex mutex_;
  NotificationHandler notification_handler_;
  bool connected_ = true;
  size_t close_count_ = 0;
  uint64_t next_request_id_ = 1;
  std::condition_variable cv_;
  std::unordered_map<std::string, std::deque<QueuedResponse>> queued_responses_;
  std::unordered_set<std::string> blocked_methods_;
  std::unordered_set<std::string> blocked_requests_;
  std::unordered_set<std::string> abandoned_requests_;
  CloseHandler close_handler_;
  bool closed_by_server_ = false;
  std::vector<RequestCall> request_calls_;
  std::vector<NotificationCall> notification_calls_;
};

struct BlockedMethod {
  std::shared_ptr<FakeConnection> connection;
  std::string method;

  BlockedMethod(std::shared_ptr<FakeConnection> connection, std::string method)
    : connection(std::move(connection))
    , method(std::move(method)) {
    this->connection->BlockMethod(this->method);
  }

  ~BlockedMethod() {
    connection->UnblockMethod(method);
  }
};

} // namespace test_utils
} // namespace mcp
} // namespace giga_edge
