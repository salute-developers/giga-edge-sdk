#include "mcp_stdio_connection.h"

#include "giga_edge/modules/mcp/client.h"
#include "giga_edge/modules/mcp/service.h"
#include "giga_edge/modules/mcp/service/request_cancellation.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/capturing_mcp_service_observer.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/isolated_scenario.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/wait_utils.h"

#include <catch.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <future>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace giga_edge::mcp;
using giga_edge::mcp::test_utils::cleanup_fd;
using giga_edge::mcp::test_utils::kWaitHangGuard;
using giga_edge::mcp::test_utils::RequireIsolated;
using giga_edge::mcp::test_utils::RunIsolated;
using giga_edge::mcp::test_utils::StaysFalseFor;
using giga_edge::mcp::test_utils::WaitUntil;

namespace {

ConnectionPtr CreateTestConnection(const std::string& server_id, const std::string& command) {
  return giga_edge::CreateStdioMcpConnection(server_id, "echo $$ >&" + std::to_string(cleanup_fd) + "; " + command);
}

// Records that the tool call has reached the transport.
class ToolCallProbe final : public IConnection {
public:
  explicit ToolCallProbe(ConnectionPtr connection)
    : connection_(std::move(connection)) {}

  Result<Json::Value> sendRequest(
    const std::string& method, const Json::Value& params, std::string& out_request_id, uint32_t timeout_ms
  ) override {
    return sendRequestWithGate(method, params, out_request_id, timeout_ms, {});
  }
  Result<Json::Value> sendRequestWithGate(
    const std::string& method,
    const Json::Value& params,
    std::string& out_request_id,
    uint32_t timeout_ms,
    const SendGate& gate
  ) override {
    if (method != "tools/call") {
      return connection_->sendRequestWithGate(method, params, out_request_id, timeout_ms, gate);
    }
    return connection_->sendRequestWithGate(
      method, params, out_request_id, timeout_ms, [this, &gate](const std::string& id) {
        const bool allowed = !gate || gate(id);
        tool_call_written = allowed;
        return allowed;
      }
    );
  }
  void abandonRequest(const std::string& request_id) override {
    connection_->abandonRequest(request_id);
  }
  void sendNotification(const std::string& method, const Json::Value& params) override {
    connection_->sendNotification(method, params);
  }
  void setNotificationHandler(NotificationHandler handler) override {
    connection_->setNotificationHandler(std::move(handler));
  }
  void close() override {
    connection_->close();
  }
  bool isConnected() const override {
    return connection_->isConnected();
  }

  std::atomic<bool> tool_call_written{false};

private:
  ConnectionPtr connection_;
};

struct StdioTasks {
  ConnectionPtr connection;
  std::vector<std::future<void>> tasks;
  ~StdioTasks() {
    connection->close();
    for (auto& task : tasks) {
      if (task.valid()) {
        task.wait();
      }
    }
  }
};

} // namespace

TEST_CASE("MCP stdio preserves the JSON-RPC code through the client", "[mcp][stdio]") {

  RequireIsolated([&]() {
    auto connection = CreateTestConnection("house", std::string(MCP_STDIO_TEST_SERVER) + " resources/read");
    auto client = createMcpClient(connection, 1000);
    const auto result = client->readResource("house://devices/state", nullptr, 1000);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.json_rpc_code == -31901);
    REQUIRE(result.error.message == "HOME_VIEW_UNAVAILABLE");
  });
}

TEST_CASE("MCP stdio survives malformed messages from the server", "[mcp][stdio]") {
  RequireIsolated([&]() {
    auto connection = CreateTestConnection("house", std::string(MCP_STDIO_TEST_SERVER) + " none");
    auto client = createMcpClient(connection, 1000);
    std::string request_id;
    const auto malformed = connection->sendRequest("test/malformed", Json::Value(Json::objectValue), request_id, 1000);
    REQUIRE(malformed.error.code == ErrorCode::INTERNAL_ERROR);
    REQUIRE_FALSE(malformed.error.json_rpc_code.has_value());
    REQUIRE(malformed.error.message == "MCP server returned an invalid JSON-RPC error");

    const auto refused = connection->sendRequestWithGate(
      "test/refused", Json::Value(Json::objectValue), request_id, 1000, [](const std::string&) {
        return false;
      }
    );
    REQUIRE(refused.error.code == ErrorCode::CANCELLED);

    const auto empty = connection->sendRequest("test/empty_error", Json::Value(Json::objectValue), request_id, 1000);
    REQUIRE(empty.error.code == ErrorCode::SERVER_ERROR);
    REQUIRE(empty.error.json_rpc_code == -32000);
    REQUIRE(empty.error.message == "MCP server returned an invalid JSON-RPC error");

    const auto counts = connection->sendRequest("test/counts", Json::Value(Json::objectValue), request_id, 1000);
    REQUIRE(counts.ok());
    REQUIRE(counts.value["test/malformed"].asInt() == 1);
    REQUIRE_FALSE(counts.value.isMember("test/refused"));
    REQUIRE(connection->isConnected());
  });
}

TEST_CASE("MCP stdio resource retries use the server's numeric error code", "[mcp][stdio][hydration]") {
  const std::string method = GENERATE("resources/read", "resources/subscribe");
  const bool permanent = GENERATE(false, true);
  CAPTURE(method, permanent);
  RequireIsolated([&]() {
    auto connection = CreateTestConnection("house", std::string(MCP_STDIO_TEST_SERVER) + " " + method);
    McpServiceConfig config;
    config.resource_hydration.read_resources_on_list = method == "resources/read";
    config.resource_hydration.subscribe_to_subscribable_resources = method == "resources/subscribe";
    config.resource_hydration.retry_initial_ms = 20;
    config.resource_hydration.retry_max_ms = 20;
    config.resource_hydration.no_retry_error_codes = permanent ? std::vector<int32_t>{-31901} : std::vector<int32_t>{};
    auto service = CreateMcpService(config);
    service->AddServer("house", connection);
    const auto count = [&]() {
      std::string id;
      const auto response = connection->sendRequest("test/counts", Json::Value(Json::objectValue), id, 1000);
      REQUIRE(response.ok());
      return response.value.get(method, 0).asInt();
    };
    REQUIRE(WaitUntil([&]() {
      return count() >= (permanent ? 1 : 2);
    }));
    if (permanent) {
      REQUIRE(StaysFalseFor(
        [&]() {
          return count() != 1;
        },
        std::chrono::milliseconds(100)
      ));
    }
    service->Shutdown();
  });
}

TEST_CASE("MCP stdio reads and subscribes through a real process", "[mcp][stdio][stdio_lifecycle]") {

  RequireIsolated([&]() {
    std::promise<ResourceNotification> notification;
    auto received = notification.get_future();
    auto connection = CreateTestConnection("house", std::string(MCP_STDIO_TEST_SERVER) + " lifecycle");
    auto client = createMcpClient(connection, 5000);
    const auto initialized = client->initialize("2024-11-05", "stdio-test", "1");
    REQUIRE(initialized.ok());
    REQUIRE(initialized.value.capabilities.supports_resource_subscriptions);
    client->notifyInitialized();
    const auto resources = client->listResources();
    REQUIRE(resources.ok());
    REQUIRE(resources.value.size() == 1);
    const auto& uri = resources.value.front().uri;
    const auto initial = client->readResource(uri);
    REQUIRE(initial.ok());
    REQUIRE(initial.value.uri == uri);
    REQUIRE(initial.value.text == R"({"version":1})");
    REQUIRE(client
              ->subscribeResource(
                uri,
                [&](const ResourceNotification& update) {
                  notification.set_value(update);
                }
              )
              .ok());
    connection->sendNotification("test/notify", Json::Value(Json::objectValue));
    REQUIRE(received.wait_for(kWaitHangGuard) == std::future_status::ready);
    const auto update = received.get();
    REQUIRE(update.uri == uri);
    const auto refreshed = client->readResource(uri);
    REQUIRE(refreshed.ok());
    REQUIRE(refreshed.value.text == R"({"version":2})");
    REQUIRE(client->unsubscribeResource(uri).ok());
    client->shutdown();
    REQUIRE_FALSE(connection->isConnected());
  });
}

TEST_CASE("MCP stdio times out a pending request and ignores its late response", "[mcp][stdio][stdio_lifecycle]") {

  RequireIsolated([&]() {
    auto connection = CreateTestConnection("house", std::string(MCP_STDIO_TEST_SERVER) + " lifecycle");
    auto client = createMcpClient(connection, 5000);
    REQUIRE(client->ping().ok());
    std::string request_id;
    const auto started = std::chrono::steady_clock::now();
    const auto result = connection->sendRequest("test/pending", Json::Value(Json::objectValue), request_id, 1000);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.code == ErrorCode::TIMEOUT);
    REQUIRE(elapsed >= std::chrono::milliseconds(1000));
    REQUIRE(elapsed < std::chrono::milliseconds(kDefaultRequestTimeoutMs));
    REQUIRE_FALSE(request_id.empty());
    REQUIRE(connection->isConnected());
    connection->sendNotification("test/reply_pending", Json::Value(Json::objectValue));
    const auto next = client->readResource("house://devices/state");
    REQUIRE(next.ok());
    REQUIRE(next.value.text == R"({"version":1})");
    client->shutdown();
    REQUIRE_FALSE(connection->isConnected());
  });
}

TEST_CASE("MCP stdio fails a pending request when the child exits", "[mcp][stdio][stdio_lifecycle]") {

  RequireIsolated([&]() {
    auto connection = CreateTestConnection("house", std::string(MCP_STDIO_TEST_SERVER) + " lifecycle");
    auto client = createMcpClient(connection, 5000);
    REQUIRE(client->ping().ok());
    auto pending = std::async(std::launch::async, [connection]() {
      std::string id;
      return connection->sendRequest("test/pending", Json::Value(Json::objectValue), id, 10000);
    });
    REQUIRE(WaitUntil([&]() {
      std::string id;
      const auto counts = connection->sendRequest("test/counts", Json::Value(Json::objectValue), id, 5000);
      return counts.ok() && counts.value.get("test/pending", 0).asInt() == 1;
    }));
    REQUIRE(pending.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
    connection->sendNotification("test/exit", Json::Value(Json::objectValue));
    REQUIRE(pending.wait_for(kWaitHangGuard) == std::future_status::ready);
    const auto result = pending.get();
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.code == ErrorCode::TRANSPORT_ERROR);
    REQUIRE_FALSE(connection->isConnected());
    REQUIRE(client->ping().error.code == ErrorCode::TRANSPORT_ERROR);
    connection->close();
    connection->close();
  });
}

TEST_CASE("MCP stdio kills a process that ignores SIGTERM including shell descendants", "[mcp][stdio][stdio_close]") {
  const bool descendant = GENERATE(false, true);
  RequireIsolated([&]() {
    std::promise<void> blocked;
    auto child_blocked = blocked.get_future();
    // dash (/bin/sh of Debian and Ubuntu) gives a background command /dev/null even with an explicit <&0, so the
    // stdin of the shell reaches the descendant through fd 3.
    const auto server = std::string(MCP_STDIO_TEST_SERVER) + " stubborn";
    const auto command = descendant ? "exec 3<&0; " + server + " <&3 3<&- & wait" : server;
    auto connection = CreateTestConnection("stubborn", command);
    std::string id;
    REQUIRE(connection->sendRequest("ping", Json::Value(Json::objectValue), id, 5000).ok());
    StdioTasks work{connection};
    connection->setNotificationHandler([&](const std::string& method, const Json::Value&) {
      if (method == "test/blocked") {
        blocked.set_value();
      }
    });
    connection->sendNotification("test/block", Json::Value(Json::objectValue));
    REQUIRE(child_blocked.wait_for(kWaitHangGuard) == std::future_status::ready);
    work.tasks.push_back(std::async(std::launch::async, [connection]() {
      connection->close();
    }));
    work.tasks.push_back(std::async(std::launch::async, [connection]() {
      connection->close();
    }));
    for (auto& task : work.tasks) {
      REQUIRE(task.wait_for(kWaitHangGuard) == std::future_status::ready);
      task.get();
    }
    connection->close();
    REQUIRE_FALSE(connection->isConnected());
  });
}

TEST_CASE("MCP stdio interrupts a blocked writer before closing its descriptors", "[mcp][stdio][stdio_close]") {

  RequireIsolated([&]() {
    std::promise<void> blocked;
    auto child_blocked = blocked.get_future();
    std::promise<void> started;
    auto writer_started = started.get_future();
    std::promise<Result<Json::Value>> response;
    auto received = response.get_future();
    auto connection = CreateTestConnection("blocked", std::string(MCP_STDIO_TEST_SERVER) + " stubborn");
    StdioTasks work{connection};
    std::string id;
    REQUIRE(connection->sendRequest("ping", Json::Value(Json::objectValue), id, 5000).ok());
    connection->setNotificationHandler([&](const std::string& method, const Json::Value&) {
      if (method == "test/blocked") {
        blocked.set_value();
      }
    });
    connection->sendNotification("test/block", Json::Value(Json::objectValue));
    REQUIRE(child_blocked.wait_for(kWaitHangGuard) == std::future_status::ready);
    Json::Value payload;
    payload["text"] = std::string(4 * 1024 * 1024, 'x');
    work.tasks.push_back(std::async(std::launch::async, [&, payload]() {
      started.set_value();
      std::string request_id;
      response.set_value(connection->sendRequest("test/large", payload, request_id, 10000));
    }));
    REQUIRE(writer_started.wait_for(kWaitHangGuard) == std::future_status::ready);
    REQUIRE(received.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
    work.tasks.push_back(std::async(std::launch::async, [connection]() {
      connection->close();
    }));
    work.tasks.push_back(std::async(std::launch::async, [connection]() {
      connection->close();
    }));
    for (auto& task : work.tasks) {
      REQUIRE(task.wait_for(kWaitHangGuard) == std::future_status::ready);
      task.get();
    }
    const auto result = received.get();
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.code == ErrorCode::TRANSPORT_ERROR);
    REQUIRE_FALSE(connection->isConnected());
  });
}

TEST_CASE("MCP stdio watchdog turns a hung child into a completed failure", "[mcp][stdio][stdio_watchdog]") {
  const auto result = RunIsolated(
    []() {
      for (;;) {
        ::pause();
      }
    },
    std::chrono::milliseconds(100)
  );
  REQUIRE_FALSE(result.finished);
  REQUIRE(WIFSIGNALED(result.status));
  REQUIRE(WTERMSIG(result.status) == SIGKILL);
}

TEST_CASE(
  "MCP stdio does not write a request cancelled while it waits for the write lock", "[mcp][stdio][stdio_write_deadline]"
) {
  RequireIsolated([]() {
    auto connection = CreateTestConnection("queued", std::string(MCP_STDIO_TEST_SERVER) + " stubborn");
    // The server does not read for half a second: the large request holds the write lock until the server continues.
    Json::Value pause(Json::objectValue);
    pause["ms"] = 500;
    connection->sendNotification("test/pause_reading", pause);
    std::promise<void> writing;
    auto writer = std::async(std::launch::async, [&] {
      Json::Value payload;
      payload["text"] = std::string(4 * 1024 * 1024, 'x');
      std::string id;
      return connection->sendRequestWithGate("test/large", payload, id, 5000, [&](const std::string&) {
        writing.set_value();
        return true;
      });
    });
    writing.get_future().wait();

    giga_edge::mcp::detail::RequestCancellation cancellation;
    auto queued = std::async(std::launch::async, [&] {
      std::string id;
      return connection->sendRequestWithGate(
        "test/queued", Json::Value(Json::objectValue), id, 5000, [&](const std::string& sent) {
          return cancellation.BeginSend(sent);
        }
      );
    });
    // The request waits behind the writer; the cancellation arrives before the write lock is released.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE_FALSE(cancellation.Cancel().has_value());
    REQUIRE(writer.get().ok());
    const auto result = queued.get();
    REQUIRE(result.error.code == ErrorCode::CANCELLED);
    REQUIRE_FALSE(cancellation.WasSent());
    std::string id;
    const auto counts = connection->sendRequest("test/counts", Json::Value(Json::objectValue), id, 5000);
    REQUIRE(counts.ok());
    REQUIRE(counts.value["test/large"].asInt() == 1);
    REQUIRE_FALSE(counts.value.isMember("test/queued"));
    connection->close();
  });
}

TEST_CASE("MCP stdio abandons a request without waiting for a blocked writer", "[mcp][stdio][stdio_write_deadline]") {
  RequireIsolated([]() {
    auto connection = CreateTestConnection("abandon", std::string(MCP_STDIO_TEST_SERVER) + " stubborn");
    // A request without a response: the server remembers test/pending and does not answer.
    std::promise<std::string> pending_id;
    auto pending = std::async(std::launch::async, [&] {
      std::string id;
      return connection->sendRequestWithGate(
        "test/pending", Json::Value(Json::objectValue), id, 30000, [&](const std::string& sent) {
          pending_id.set_value(sent);
          return true;
        }
      );
    });
    const auto id = pending_id.get_future().get();

    // The server stops reading, and the large request holds the write lock until its deadline.
    std::promise<void> blocked;
    connection->setNotificationHandler([&](const std::string& method, const Json::Value&) {
      if (method == "test/blocked") {
        blocked.set_value();
      }
    });
    connection->sendNotification("test/block", Json::Value(Json::objectValue));
    REQUIRE(blocked.get_future().wait_for(kWaitHangGuard) == std::future_status::ready);
    std::promise<void> writing;
    auto writer = std::async(std::launch::async, [&] {
      Json::Value payload;
      payload["text"] = std::string(4 * 1024 * 1024, 'x');
      std::string large_id;
      return connection->sendRequestWithGate("test/large", payload, large_id, 5000, [&](const std::string&) {
        writing.set_value();
        return true;
      });
    });
    writing.get_future().wait();

    connection->abandonRequest(id);
    REQUIRE(pending.wait_for(kWaitHangGuard) == std::future_status::ready);
    REQUIRE(writer.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
    REQUIRE(pending.get().error.code == ErrorCode::CANCELLED);
    connection->abandonRequest(id);
    connection->abandonRequest("unknown");
    connection->close();
    writer.wait();
  });
}

TEST_CASE("MCP service drops a stdio server whose process exited", "[mcp][stdio][stdio_lifecycle]") {
  RequireIsolated([]() {
    auto connection = CreateTestConnection("house", std::string(MCP_STDIO_TEST_SERVER) + " lifecycle");
    auto service = CreateMcpService(McpServiceConfig{});
    auto observer = std::make_shared<giga_edge::mcp::test_utils::CapturingMcpServiceObserver>();
    service->SetObserver(observer);
    service->AddServer("house", connection);
    REQUIRE(WaitUntil([&]() {
      return service->GetServerIds().size() == 1;
    }));
    // The process exits by itself: there are no calls after that, and the server is removed anyway.
    connection->sendNotification("test/exit", Json::Value(Json::objectValue));
    REQUIRE(WaitUntil([&]() {
      std::lock_guard<std::mutex> lock(observer->mutex);
      return std::any_of(
        observer->server_status_events.begin(), observer->server_status_events.end(), [](const auto& event) {
          return event.status == McpServerStatusEvent::Status::DISCONNECTED;
        }
      );
    }));
    REQUIRE(service->GetServerIds().empty());
    service->Shutdown();
  });
}

TEST_CASE("MCP stdio reports a close that happened before the handler was set", "[mcp][stdio][stdio_lifecycle]") {
  RequireIsolated([]() {
    auto connection = CreateTestConnection("house", std::string(MCP_STDIO_TEST_SERVER) + " lifecycle");
    connection->sendNotification("test/exit", Json::Value(Json::objectValue));
    REQUIRE(WaitUntil([&]() {
      return !connection->isConnected();
    }));
    int closed = 0;
    connection->setCloseHandler([&closed] {
      ++closed;
    });
    REQUIRE(closed == 1);
    connection->setCloseHandler([&closed] {
      ++closed;
    });
    connection->close();
    REQUIRE(closed == 1);
  });
}

TEST_CASE(
  "MCP stdio connection released by its own reader thread shuts down cleanly", "[mcp][stdio][stdio_lifecycle]"
) {
  RequireIsolated([]() {
    ConnectionPtr owner = CreateTestConnection("house", std::string(MCP_STDIO_TEST_SERVER) + " lifecycle");
    std::promise<void> main_released;
    auto released = main_released.get_future().share();
    // The signal that the reader thread has exited: thread_local destructors run after the return from ReaderLoop and
    // from the thread lambda.
    struct ThreadExit {
      std::promise<void>* exited;
      ~ThreadExit() {
        exited->set_value();
      }
    };
    std::promise<void> reader_exited;
    // The handler on the reader thread releases the last reference: the destructor runs on this same thread.
    owner->setNotificationHandler([&owner, released, &reader_exited](const std::string& method, const Json::Value&) {
      if (method == "test/blocked") {
        released.wait();
        thread_local ThreadExit signal{&reader_exited};
        (void)signal;
        owner.reset();
      }
    });
    {
      // The copy holds the connection while the reader thread resets owner.
      // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
      const ConnectionPtr copy = owner;
      copy->sendNotification("test/block", Json::Value(Json::objectValue));
    }
    main_released.set_value();
    // The reader thread runs to the end without touching the destroyed connection.
    REQUIRE(reader_exited.get_future().wait_for(kWaitHangGuard) == std::future_status::ready);
  });
}

TEST_CASE("MCP stdio does not report its own close to the close handler", "[mcp][stdio][stdio_lifecycle]") {
  RequireIsolated([]() {
    auto connection = CreateTestConnection("house", std::string(MCP_STDIO_TEST_SERVER) + " lifecycle");
    std::atomic<int> closed{0};
    connection->setCloseHandler([&closed] {
      ++closed;
    });
    connection->close();
    REQUIRE(closed.load() == 0);
    connection->setCloseHandler([&closed] {
      ++closed;
    });
    REQUIRE(closed.load() == 0);
  });
}

TEST_CASE("MCP stdio bounds a blocked write by the request deadline", "[mcp][stdio][stdio_write_deadline]") {
  RequireIsolated([]() {
    std::promise<void> blocked;
    auto ready = blocked.get_future();
    auto connection = CreateTestConnection("timeout", std::string(MCP_STDIO_TEST_SERVER) + " stubborn");
    std::string id;
    REQUIRE(connection->sendRequest("ping", Json::Value(Json::objectValue), id, 5000).ok());
    connection->setNotificationHandler([&](const std::string& method, const Json::Value&) {
      if (method == "test/blocked") {
        blocked.set_value();
      }
    });
    connection->sendNotification("test/block", Json::Value(Json::objectValue));
    REQUIRE(ready.wait_for(kWaitHangGuard) == std::future_status::ready);
    Json::Value payload;
    payload["text"] = std::string(4 * 1024 * 1024, 'x');
    const auto result = connection->sendRequest("test/large", payload, id, 200);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error.code == ErrorCode::TIMEOUT);
    connection->close();
  });
}

TEST_CASE(
  "MCP service shutdown finishes while stdio is not reading a tool request", "[mcp][stdio][stdio_write_deadline]"
) {
  RequireIsolated([]() {
    auto connection = std::make_shared<ToolCallProbe>(
      CreateTestConnection("shutdown", std::string(MCP_STDIO_TEST_SERVER) + " stubborn")
    );
    McpServiceConfig config;
    config.default_tool_call_timeout_ms = 1500;
    auto service = CreateMcpService(config);
    auto observer = std::make_shared<giga_edge::mcp::test_utils::CapturingMcpServiceObserver>();
    service->SetObserver(observer);
    service->AddServer("shutdown", connection);
    REQUIRE(WaitUntil([&]() {
      return service->GetServerIds().size() == 1;
    }));
    // The server no longer reads: the 4 MB request blocks on the pipe and ends by the write deadline.
    connection->sendNotification("test/block", Json::Value(Json::objectValue));
    Json::Value payload;
    payload["text"] = std::string(4 * 1024 * 1024, 'x');
    REQUIRE(service->CallToolAsync(
      1, McpToolCallRequest{.call_id = "blocked", .server_id = "shutdown", .tool_name = "large", .arguments = payload}
    ));
    REQUIRE(WaitUntil([&]() {
      return connection->tool_call_written.load();
    }));
    service->Shutdown();
    std::lock_guard<std::mutex> lock(observer->mutex);
    REQUIRE(observer->tool_results.size() == 1);
    REQUIRE(observer->tool_results.front().result.is_error);
    // The write hits its deadline, and the result of the accepted call is delivered by Shutdown itself.
    REQUIRE(observer->tool_results.front().result.error_message == "Service shut down");
    REQUIRE(observer->tool_results.front().result.request_sent);
    REQUIRE_FALSE(connection->isConnected());
  });
}
