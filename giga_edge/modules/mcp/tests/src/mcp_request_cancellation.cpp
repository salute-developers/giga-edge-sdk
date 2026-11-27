#include "giga_edge/modules/mcp/service.h"
#include "giga_edge/modules/mcp/service/request_cancellation.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/capturing_mcp_service_observer.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_connection.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/wait_utils.h"

#include <catch.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace giga_edge::mcp;
using giga_edge::mcp::test_utils::BlockedMethod;
using giga_edge::mcp::test_utils::CapturingMcpServiceObserver;
using giga_edge::mcp::test_utils::FakeConnection;
using giga_edge::mcp::test_utils::WaitUntil;

namespace {

// A fake that keeps a shared journal of "request written / notification sent" and can hold a tool call before the
// write.
class JournalConnection final : public FakeConnection {
public:
  Result<Json::Value> sendRequestWithGate(
    const std::string& method,
    const Json::Value& params,
    std::string& out_request_id,
    uint32_t timeout_ms,
    const SendGate& gate
  ) override {
    if (method == "tools/call") {
      std::unique_lock<std::mutex> lock(mutex_);
      entered_ = true;
      cv_.notify_all();
      cv_.wait(lock, [this] {
        return !hold_;
      });
    }
    return FakeConnection::sendRequestWithGate(
      method, params, out_request_id, timeout_ms, [this, &gate, method](const std::string& id) {
        const bool allowed = !gate || gate(id);
        if (allowed) {
          Append(method);
        }
        return allowed;
      }
    );
  }
  void sendNotification(const std::string& method, const Json::Value& params) override {
    Append(method);
    FakeConnection::sendNotification(method, params);
  }

  void Hold() {
    std::lock_guard<std::mutex> lock(mutex_);
    hold_ = true;
  }
  void Release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      hold_ = false;
    }
    cv_.notify_all();
  }
  bool Entered() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entered_;
  }
  std::vector<std::string> Journal() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return journal_;
  }

private:
  void Append(const std::string& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    journal_.push_back(event);
  }

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool hold_ = false;
  bool entered_ = false;
  std::vector<std::string> journal_;
};

struct CancelFixture {
  CancelFixture()
    : service(CreateMcpService([] {
      McpServiceConfig config;
      config.thread_pool_size = 1;
      return config;
    }()))
    , observer(std::make_shared<CapturingMcpServiceObserver>())
    , connection(std::make_shared<JournalConnection>()) {
    service->SetObserver(observer);
    Json::Value initialize(Json::objectValue);
    initialize["protocolVersion"] = "2024-11-05";
    initialize["capabilities"]["tools"] = Json::Value(Json::objectValue);
    connection->EnqueueSuccess("initialize", initialize);
    Json::Value tools(Json::objectValue);
    tools["tools"][0]["name"] = "search";
    connection->EnqueueSuccess("tools/list", tools);
    service->AddServer("alpha", connection);
    REQUIRE(WaitUntil([this] {
      return service->GetServerIds().size() == 1;
    }));
  }

  McpToolCallResultEvent ToolResult() const {
    std::lock_guard<std::mutex> lock(observer->mutex);
    REQUIRE(observer->tool_results.size() == 1);
    return observer->tool_results[0].result;
  }
  size_t Count(const std::string& method) const {
    const auto journal = connection->Journal();
    return static_cast<size_t>(std::count(journal.begin(), journal.end(), method));
  }

  std::shared_ptr<IMcpService> service;
  std::shared_ptr<CapturingMcpServiceObserver> observer;
  std::shared_ptr<JournalConnection> connection;
};

McpToolCallRequest SearchCall() {
  return McpToolCallRequest{.call_id = "call", .server_id = "alpha", .tool_name = "search"};
}

} // namespace

TEST_CASE("MCP request cancelled before sending is never sent", "[mcp][cancellation]") {
  detail::RequestCancellation cancellation;
  REQUIRE_FALSE(cancellation.Cancel().has_value());
  REQUIRE_FALSE(cancellation.Cancel().has_value());
  REQUIRE_FALSE(cancellation.BeginSend("request-1"));
  REQUIRE_FALSE(cancellation.WasSent());
  REQUIRE_FALSE(cancellation.CancelledAfterSendId().has_value());
}

TEST_CASE("MCP request cancelled after sending reports its id once", "[mcp][cancellation]") {
  detail::RequestCancellation cancellation;
  REQUIRE(cancellation.BeginSend("request-1"));
  REQUIRE(cancellation.WasSent());
  REQUIRE_FALSE(cancellation.CancelledAfterSendId().has_value());
  REQUIRE(cancellation.Cancel() == std::optional<std::string>("request-1"));
  REQUIRE_FALSE(cancellation.Cancel().has_value());
  REQUIRE(cancellation.CancelledAfterSendId() == std::optional<std::string>("request-1"));
}

TEST_CASE("MCP request sent without an id cannot be interrupted", "[mcp][cancellation]") {
  // This is how a transport without cancellation support sends (the default implementation of sendRequestWithGate).
  detail::RequestCancellation cancellation;
  REQUIRE(cancellation.BeginSend(""));
  REQUIRE_FALSE(cancellation.Cancel().has_value());
  REQUIRE(cancellation.WasSent());
  REQUIRE_FALSE(cancellation.CancelledAfterSendId().has_value());
}

TEST_CASE("MCP completed requests cannot be cancelled", "[mcp][cancellation]") {
  detail::RequestCancellation cancellation;
  const bool sent = GENERATE(false, true);
  if (sent) {
    REQUIRE(cancellation.BeginSend("request-1"));
  }
  cancellation.Finish();
  REQUIRE_FALSE(cancellation.Cancel().has_value());
  REQUIRE_FALSE(cancellation.CancelledAfterSendId().has_value());
}

TEST_CASE(
  "MCP simultaneous cancellation and sending agree on whether the request left", "[mcp][cancellation][concurrency]"
) {
  for (int attempt = 0; attempt < 1000; ++attempt) {
    detail::RequestCancellation cancellation;
    std::atomic<bool> start{false};
    bool sent = false;
    std::thread sender([&] {
      while (!start.load()) {
        std::this_thread::yield();
      }
      sent = cancellation.BeginSend("request-1");
    });
    start.store(true);
    const auto interrupted = cancellation.Cancel();
    sender.join();
    // Sent: the cancellation knows its id; not sent: it will never be sent.
    REQUIRE(sent == interrupted.has_value());
    REQUIRE(cancellation.WasSent() == sent);
  }
}

TEST_CASE("MCP transports with only the original API honour the send gate", "[mcp][cancellation]") {
  class GateUnawareConnection final : public IConnection {
  public:
    Result<Json::Value> sendRequest(
      const std::string& method, const Json::Value& params, std::string& out_request_id, uint32_t timeout_ms
    ) override {
      return connection.sendRequest(method, params, out_request_id, timeout_ms);
    }
    void sendNotification(const std::string& method, const Json::Value& params) override {
      connection.sendNotification(method, params);
    }
    void setNotificationHandler(NotificationHandler handler) override {
      connection.setNotificationHandler(std::move(handler));
    }
    void close() override {
      connection.close();
    }
    bool isConnected() const override {
      return connection.isConnected();
    }
    test_utils::FakeConnection connection;
  } transport;

  transport.connection.EnqueueSuccess("ping", Json::Value(Json::objectValue), "gate-unaware");
  std::string request_id;
  std::optional<std::string> gate_id;
  const bool allow = GENERATE(false, true);
  const auto result = transport.sendRequestWithGate("ping", {}, request_id, 10000, [&](const std::string& id) {
    gate_id = id;
    return allow;
  });
  REQUIRE(gate_id == std::optional<std::string>(""));
  if (allow) {
    REQUIRE(result.ok());
    REQUIRE(request_id == "gate-unaware");
    REQUIRE(transport.connection.RequestCalls().front().timeout_ms == 10000);
  } else {
    REQUIRE(result.error.code == ErrorCode::CANCELLED);
    REQUIRE(transport.connection.RequestCalls().empty());
  }
  transport.abandonRequest("gate-unaware");
}

TEST_CASE("MCP cancel frees the service thread held by an unanswered call", "[mcp][cancellation][service]") {
  CancelFixture f;
  f.connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue), "call-1");
  f.connection->EnqueueSuccess("ping", Json::Value(Json::objectValue));
  const BlockedMethod silent(f.connection, "tools/call");
  REQUIRE(f.service->CallToolAsync(1, SearchCall()));
  REQUIRE(WaitUntil([&] {
    return f.Count("tools/call") == 1;
  }));
  // The only pool thread waits for the response to the call being cancelled: ping queues up behind it.
  REQUIRE(f.service->PingAsync(2, McpPingRequest{.call_id = "ping", .server_id = "alpha"}));
  REQUIRE(f.service->Cancel(1));
  REQUIRE(WaitUntil([&] {
    std::lock_guard<std::mutex> lock(f.observer->mutex);
    return f.observer->ping_results.size() == 1;
  }));
  const auto result = f.ToolResult();
  REQUIRE(result.error_code == ErrorCode::CANCELLED);
  REQUIRE(result.request_sent);
  REQUIRE(WaitUntil([&] {
    return f.Count("notifications/cancelled") == 1;
  }));
  const auto notifications = f.connection->NotificationCalls();
  REQUIRE(notifications.back().params["requestId"].asString() == "call-1");
  REQUIRE(notifications.back().params["reason"].asString() == "cancelled");
}

TEST_CASE("MCP cancel before the request is written keeps it off the transport", "[mcp][cancellation][service]") {
  CancelFixture f;
  f.connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue), "call-1");
  f.connection->Hold();
  REQUIRE(f.service->CallToolAsync(1, SearchCall()));
  REQUIRE(WaitUntil([&] {
    return f.connection->Entered();
  }));
  REQUIRE(f.service->Cancel(1));
  f.connection->Release();
  // The pool task has finished: the next ping goes through the same single thread.
  f.connection->EnqueueSuccess("ping", Json::Value(Json::objectValue));
  REQUIRE(f.service->PingAsync(2, McpPingRequest{.call_id = "ping", .server_id = "alpha"}));
  REQUIRE(WaitUntil([&] {
    std::lock_guard<std::mutex> lock(f.observer->mutex);
    return f.observer->ping_results.size() == 1;
  }));
  const auto result = f.ToolResult();
  REQUIRE(result.error_code == ErrorCode::CANCELLED);
  REQUIRE_FALSE(result.request_sent);
  REQUIRE(f.Count("tools/call") == 0);
  REQUIRE(f.Count("notifications/cancelled") == 0);
}

TEST_CASE(
  "MCP cancel after the request is written tells the server after the request", "[mcp][cancellation][service]"
) {
  CancelFixture f;
  f.connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue), "call-1");
  const BlockedMethod silent(f.connection, "tools/call");
  REQUIRE(f.service->CallToolAsync(1, SearchCall()));
  REQUIRE(WaitUntil([&] {
    return f.Count("tools/call") == 1;
  }));
  REQUIRE(f.service->Cancel(1));
  REQUIRE(WaitUntil([&] {
    return f.Count("notifications/cancelled") == 1;
  }));
  const auto journal = f.connection->Journal();
  REQUIRE(journal.size() >= 2);
  REQUIRE(
    std::vector<std::string>(journal.end() - 2, journal.end())
    == std::vector<std::string>{"tools/call", "notifications/cancelled"}
  );
  REQUIRE(f.ToolResult().request_sent);
}

TEST_CASE("MCP request of a replaced server is not written once the server is gone", "[mcp][cancellation][service]") {
  CancelFixture f;
  f.connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue), "call-1");
  f.connection->Hold();
  REQUIRE(f.service->CallToolAsync(1, SearchCall()));
  // The pool thread has already picked the client and stands right before the write when the server is replaced.
  REQUIRE(WaitUntil([&] {
    return f.connection->Entered();
  }));
  f.service->AddServer("alpha", std::make_shared<FakeConnection>());
  f.connection->Release();
  REQUIRE(WaitUntil([&] {
    std::lock_guard<std::mutex> lock(f.observer->mutex);
    return f.observer->tool_results.size() == 1;
  }));
  const auto result = f.ToolResult();
  REQUIRE(result.error_message == "Server disconnected");
  REQUIRE_FALSE(result.request_sent);
  REQUIRE(WaitUntil([&] {
    return f.connection->CloseCount() == 1;
  }));
  REQUIRE(f.Count("tools/call") == 0);
}

TEST_CASE("MCP Shutdown does not wait for the answer to an accepted call", "[mcp][cancellation][service]") {
  CancelFixture f;
  f.connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue), "call-1");
  const BlockedMethod silent(f.connection, "tools/call");
  REQUIRE(f.service->CallToolAsync(1, SearchCall()));
  REQUIRE(WaitUntil([&] {
    return f.Count("tools/call") == 1;
  }));
  // The server is silent: without aborting the wait Shutdown would wait for a response or a timeout.
  auto shutdown = std::async(std::launch::async, [&] {
    f.service->Shutdown();
  });
  REQUIRE(shutdown.wait_for(giga_edge::mcp::test_utils::kWaitHangGuard) == std::future_status::ready);
  const auto result = f.ToolResult();
  REQUIRE(result.error_message == "Service shut down");
  REQUIRE(result.error_code == ErrorCode::CANCELLED);
  REQUIRE(result.request_sent);
}
