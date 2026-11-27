#pragma once

#include <json/json.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace giga_edge {
namespace mcp {

inline constexpr uint32_t kDefaultRequestTimeoutMs = 10000;

enum class ErrorCode : int32_t {
  NONE = 0,
  TRANSPORT_ERROR = -1,
  TIMEOUT = -2,
  CANCELLED = -3,
  INVALID_REQUEST = -32600,
  METHOD_NOT_FOUND = -32601,
  INVALID_PARAMS = -32602,
  INTERNAL_ERROR = -32603,
  SERVER_ERROR = -32000,
};

struct Error {
  ErrorCode code;
  std::string message;
  Json::Value data;
  std::optional<int32_t> json_rpc_code;

  Error()
    : code(ErrorCode::NONE) {}
  explicit operator bool() const {
    return code != ErrorCode::NONE;
  }
};

template<typename T>
struct Result {
  T value;
  Error error;
  bool ok() const {
    return !error;
  }
};

template<>
struct Result<void> {
  Error error;
  bool ok() const {
    return !error;
  }
};

// MCP transport. Methods do not throw: the client turns an exception into TRANSPORT_ERROR anyway.
class IConnection {
public:
  // Decides whether to write the request: the transport calls it after registering the wait for the response, under
  // the write lock, right before writing. false means the request is cancelled: the transport does not write it and
  // returns CANCELLED.
  using SendGate = std::function<bool(const std::string& request_id)>;
  using CloseHandler = std::function<void()>;

  virtual ~IConnection() = default;

  virtual Result<Json::Value> sendRequest(
    const std::string& method, const Json::Value& params, std::string& out_request_id, uint32_t timeout_ms
  ) = 0;
  virtual void sendNotification(const std::string& method, const Json::Value& params) = 0;

  using NotificationHandler = std::function<void(const std::string& method, const Json::Value& params)>;
  // The replacement applies to notifications the transport takes after it: the previous handler may still start and
  // run after the return.
  virtual void setNotificationHandler(NotificationHandler handler) = 0;

  // After a return off the transport thread, handlers are not running and are never called again: the client removes
  // them, closes the connection and destroys what they refer to.
  virtual void close() = 0;
  virtual bool isConnected() const = 0;

  // For a transport without cancellation support: the gate is checked before sendRequest, the request id is unknown.
  virtual Result<Json::Value> sendRequestWithGate(
    const std::string& method,
    const Json::Value& params,
    std::string& out_request_id,
    uint32_t timeout_ms,
    const SendGate& gate
  ) {
    if (gate && !gate("")) {
      Result<Json::Value> result;
      result.error.code = ErrorCode::CANCELLED;
      result.error.message = "MCP request cancelled before sending";
      return result;
    }
    return sendRequest(method, params, out_request_id, timeout_ms);
  }

  // Ends the wait for the response to request_id with CANCELLED. Does not block, does not write to the transport and
  // does not take the write lock; safe concurrently with close() and after it. An unknown or finished id does nothing.
  virtual void abandonRequest(const std::string& /*request_id*/) {}

  // Final close by the server or the transport; a close by close() is not reported. The handler is called at most once
  // and outside the transport locks; if the transport has already closed by itself, right when it is set. An empty
  // handler removes the previous one. By default the transport does not report a close: the owner of the connection
  // removes a broken one through RemoveServer.
  virtual void setCloseHandler(CloseHandler /*handler*/) {}
};

using ConnectionPtr = std::shared_ptr<IConnection>;

} // namespace mcp
} // namespace giga_edge
