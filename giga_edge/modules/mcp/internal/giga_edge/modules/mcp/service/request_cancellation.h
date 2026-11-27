#pragma once

#include <mutex>
#include <optional>
#include <string>

namespace giga_edge {
namespace mcp {
namespace detail {

// Cancelling a request and sending it to the transport are decided under one lock: if BeginSend did not let the
// request through, it was not written, and request_sent=false is accurate.
class RequestCancellation {
public:
  // The transport calls it right before writing (IConnection::SendGate). false means the request is already cancelled.
  bool BeginSend(const std::string& request_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancelled_) {
      return false;
    }
    sent_ = true;
    request_id_ = request_id;
    return true;
  }

  // Cancels the request; returns the id if the request was already sent and the transport has something to abort.
  std::optional<std::string> Cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (finished_ || cancelled_) {
      return std::nullopt;
    }
    cancelled_ = true;
    if (sent_ && !request_id_.empty()) {
      return request_id_;
    }
    return std::nullopt;
  }

  bool WasSent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sent_;
  }

  // The id of a request cancelled after it was sent: the server may have started working and needs
  // notifications/cancelled.
  std::optional<std::string> CancelledAfterSendId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancelled_ && sent_ && !request_id_.empty()) {
      return request_id_;
    }
    return std::nullopt;
  }

  void Finish() {
    std::lock_guard<std::mutex> lock(mutex_);
    finished_ = true;
  }

private:
  mutable std::mutex mutex_;
  bool sent_ = false;
  bool cancelled_ = false;
  bool finished_ = false;
  std::string request_id_;
};

} // namespace detail
} // namespace mcp
} // namespace giga_edge
