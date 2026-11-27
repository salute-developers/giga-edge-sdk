#pragma once

#include "giga_edge/modules/mcp/connection.h"

#include <stdexcept>
#include <string>

namespace giga_edge {
namespace mcp {
namespace test_utils {

// A host transport that throws from every call (like a host adapter on broken JSON).
class ThrowingConnection final : public IConnection {
public:
  explicit ThrowingConnection(bool is_connected_throws = false)
    : is_connected_throws_(is_connected_throws) {}

  Result<Json::Value> sendRequest(const std::string&, const Json::Value&, std::string&, uint32_t) override {
    throw std::runtime_error("ipc parse failed");
  }
  void sendNotification(const std::string&, const Json::Value&) override {
    throw std::runtime_error("ipc send failed");
  }
  void setNotificationHandler(NotificationHandler) override {
    throw std::runtime_error("ipc handler failed");
  }
  void close() override {
    throw std::runtime_error("ipc close failed");
  }
  bool isConnected() const override {
    if (is_connected_throws_) {
      throw std::runtime_error("ipc status failed");
    }
    return true;
  }

private:
  const bool is_connected_throws_;
};

} // namespace test_utils
} // namespace mcp
} // namespace giga_edge
