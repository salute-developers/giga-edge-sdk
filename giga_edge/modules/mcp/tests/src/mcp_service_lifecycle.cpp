#include "giga_edge/lib/threadpool/threadpool.h"
#include "giga_edge/modules/mcp/service.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/capturing_mcp_service_observer.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_connection.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/isolated_scenario.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/throwing_connection.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/wait_utils.h"

#include <catch.hpp>

#include <algorithm>
#include <atomic>
#include <iostream>
#include <memory>
#include <new>
#include <sstream>

using namespace giga_edge::mcp;
using giga_edge::mcp::test_utils::BlockedMethod;
using giga_edge::mcp::test_utils::CapturingMcpServiceObserver;
using giga_edge::mcp::test_utils::FakeConnection;
using giga_edge::mcp::test_utils::RequireIsolated;
using giga_edge::mcp::test_utils::StaysFalseFor;
using giga_edge::mcp::test_utils::ThrowingConnection;
using giga_edge::mcp::test_utils::WaitUntil;

TEST_CASE("MCP shutdown is terminal and repeatable", "[mcp][service][lifecycle]") {
  RequireIsolated([&]() {
    auto service = CreateMcpService(McpServiceConfig{});
    service->Shutdown();
    service->Shutdown();
    auto connection = std::make_shared<FakeConnection>();
    service->AddServer("stopped", connection);
    REQUIRE(service->GetServerIds().empty());
    REQUIRE(connection->RequestCalls().empty());
    REQUIRE_FALSE(
      service->CallToolAsync(1, McpToolCallRequest{.call_id = "call", .server_id = "stopped", .tool_name = "test"})
    );
    REQUIRE_FALSE(service->ReadResourceAsync(
      2, McpReadResourceRequest{.call_id = "read", .server_id = "stopped", .uri = "test://state"}
    ));
    REQUIRE_FALSE(service->SubscribeResourceAsync(3, "stopped", "test://state"));
    REQUIRE_FALSE(service->UnsubscribeResourceAsync(4, "stopped", "test://state"));
  });
}

namespace {

Json::Value MakeInitializePayload(bool supports_resources, bool supports_resource_subscriptions) {
  Json::Value payload(Json::objectValue);
  payload["protocolVersion"] = "2024-11-05";
  payload["serverInfo"]["name"] = "fake-server";
  payload["serverInfo"]["version"] = "1.0";
  payload["capabilities"]["tools"] = Json::Value(Json::objectValue);
  if (supports_resources) {
    payload["capabilities"]["resources"] = Json::Value(Json::objectValue);
    payload["capabilities"]["resources"]["subscribe"] = supports_resource_subscriptions;
  }
  return payload;
}

Json::Value MakeToolsList(std::initializer_list<const char*> tool_names) {
  Json::Value payload(Json::objectValue);
  payload["tools"] = Json::Value(Json::arrayValue);
  for (const auto* name : tool_names) {
    Json::Value item(Json::objectValue);
    item["name"] = name;
    item["description"] = std::string("desc-") + name;
    item["inputSchema"] = Json::Value(Json::objectValue);
    payload["tools"].append(item);
  }
  return payload;
}

Json::Value MakeResourcesList(std::initializer_list<const char*> uris) {
  Json::Value payload(Json::objectValue);
  payload["resources"] = Json::Value(Json::arrayValue);
  for (const auto* uri : uris) {
    Json::Value item(Json::objectValue);
    item["uri"] = uri;
    item["name"] = uri;
    item["mimeType"] = "text/plain";
    item["description"] = "resource";
    item["subscribable"] = true;
    payload["resources"].append(item);
  }
  return payload;
}

Json::Value MakeResourceTemplatesList(std::initializer_list<const char*> templates) {
  Json::Value payload(Json::objectValue);
  payload["resourceTemplates"] = Json::Value(Json::arrayValue);
  for (const auto* uri_template : templates) {
    Json::Value item(Json::objectValue);
    item["uriTemplate"] = uri_template;
    item["name"] = uri_template;
    item["mimeType"] = "text/plain";
    item["description"] = "template";
    payload["resourceTemplates"].append(item);
  }
  return payload;
}

bool HasServerStatus(
  const std::shared_ptr<CapturingMcpServiceObserver>& observer, McpServerStatusEvent::Status status
) {
  std::lock_guard<std::mutex> lock(observer->mutex);
  for (const auto& event : observer->server_status_events) {
    if (event.status == status) {
      return true;
    }
  }
  return false;
}

bool HasServerStatusWithMessage(
  const std::shared_ptr<CapturingMcpServiceObserver>& observer,
  McpServerStatusEvent::Status status,
  const std::string& message_part
) {
  std::lock_guard<std::mutex> lock(observer->mutex);
  for (const auto& event : observer->server_status_events) {
    if (event.status == status && event.message.find(message_part) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool HasNotification(const std::shared_ptr<FakeConnection>& connection, const std::string& method) {
  for (const auto& call : connection->NotificationCalls()) {
    if (call.method == method) {
      return true;
    }
  }
  return false;
}

// The MCP log is written to std::cerr.
class CapturedCerr {
public:
  CapturedCerr()
    : previous_(std::cerr.rdbuf(buffer_.rdbuf())) {}
  ~CapturedCerr() {
    std::cerr.rdbuf(previous_);
  }
  CapturedCerr(const CapturedCerr&) = delete;
  CapturedCerr& operator=(const CapturedCerr&) = delete;

  std::string Text() const {
    return buffer_.str();
  }

private:
  std::ostringstream buffer_;
  std::streambuf* previous_;
};

size_t CountRequests(const std::shared_ptr<FakeConnection>& connection, const std::string& method) {
  size_t count = 0;
  for (const auto& call : connection->RequestCalls()) {
    if (call.method == method) {
      ++count;
    }
  }
  return count;
}

} // namespace

TEST_CASE("MCP service initializes server and publishes initial snapshots", "[mcp][service][lifecycle]") {
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);

  auto connection = std::make_shared<FakeConnection>();
  connection->EnqueueSuccess("initialize", MakeInitializePayload(true, true));
  connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
  connection->EnqueueSuccess("resources/list", MakeResourcesList({"resource://clock"}));
  connection->EnqueueSuccess("resources/templates/list", MakeResourceTemplatesList({"resource://{id}"}));

  service->AddServer("alpha", connection);

  REQUIRE(WaitUntil([&]() {
    return HasServerStatus(observer, McpServerStatusEvent::Status::CONNECTED);
  }));
  REQUIRE(HasNotification(connection, "notifications/initialized"));
  REQUIRE(CountRequests(connection, "initialize") == 1);
  REQUIRE(CountRequests(connection, "tools/list") == 1);
  REQUIRE(CountRequests(connection, "resources/list") == 1);
  REQUIRE(CountRequests(connection, "resources/templates/list") == 1);

  const auto server_ids = service->GetServerIds();
  const auto tools = service->GetToolsSnapshot();
  const auto resources = service->GetResourcesSnapshot();
  const auto templates = service->GetResourceTemplatesSnapshot();
  REQUIRE(server_ids.size() == 1);
  REQUIRE(server_ids[0] == "alpha");
  REQUIRE(tools.size() == 1);
  REQUIRE(resources.size() == 1);
  REQUIRE(templates.size() == 1);
}

TEST_CASE("MCP service ignores a repeated AddServer with the same connection", "[mcp][service][lifecycle]") {
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);

  auto connection = std::make_shared<FakeConnection>();
  connection->EnqueueSuccess("initialize", MakeInitializePayload(false, false));
  connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));

  service->AddServer("alpha", connection);
  {
    CapturedCerr log;
    service->AddServer("alpha", connection);
    REQUIRE(log.Text().find("reconnecting") == std::string::npos);
  }

  REQUIRE(WaitUntil([&]() {
    return HasServerStatus(observer, McpServerStatusEvent::Status::CONNECTED);
  }));
  REQUIRE(CountRequests(connection, "initialize") == 1);
  REQUIRE(connection->CloseCount() == 0);
  REQUIRE(service->GetToolsSnapshot().size() == 1);
}

TEST_CASE("MCP service connects a new connection that took the address of a failed one", "[mcp][service][lifecycle]") {
  // Both connections live in the same memory: by the raw address the new one is indistinguishable from the destroyed
  // one. The memory outlives the service.
  alignas(FakeConnection) unsigned char storage[sizeof(FakeConnection)];
  std::atomic<bool> destroyed{false};
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);

  const auto place = [&storage, &destroyed]() {
    destroyed = false;
    return std::shared_ptr<FakeConnection>(new (storage) FakeConnection(), [&destroyed](FakeConnection* connection) {
      connection->~FakeConnection();
      destroyed = true;
    });
  };

  auto failed = place();
  failed->EnqueueError("initialize", ErrorCode::TRANSPORT_ERROR, "not ready");
  service->AddServer("alpha", failed);
  failed.reset();
  REQUIRE(WaitUntil([&]() {
    return HasServerStatus(observer, McpServerStatusEvent::Status::ERROR);
  }));
  REQUIRE(WaitUntil([&]() {
    return destroyed.load();
  }));

  auto fresh = place();
  fresh->EnqueueSuccess("initialize", MakeInitializePayload(false, false));
  fresh->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
  service->AddServer("alpha", fresh);

  REQUIRE(WaitUntil([&]() {
    return HasServerStatus(observer, McpServerStatusEvent::Status::CONNECTED);
  }));
  REQUIRE(CountRequests(fresh, "initialize") == 1);
  REQUIRE(service->GetToolsSnapshot().size() == 1);
}

TEST_CASE("MCP service reconnects a server added again with a new connection", "[mcp][service][lifecycle]") {
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);
  auto count_status = [&](McpServerStatusEvent::Status status) {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return std::count_if(
      observer->server_status_events.begin(), observer->server_status_events.end(), [&](const auto& event) {
        return event.status == status;
      }
    );
  };

  auto old_connection = std::make_shared<FakeConnection>();
  old_connection->EnqueueSuccess("initialize", MakeInitializePayload(false, false));
  old_connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
  service->AddServer("alpha", old_connection);
  REQUIRE(WaitUntil([&]() {
    return count_status(McpServerStatusEvent::Status::CONNECTED) == 1;
  }));

  auto new_connection = std::make_shared<FakeConnection>();
  new_connection->EnqueueSuccess("initialize", MakeInitializePayload(false, false));
  new_connection->EnqueueSuccess("tools/list", MakeToolsList({"search", "email"}));
  {
    CapturedCerr log;
    service->AddServer("alpha", new_connection);
    REQUIRE(log.Text().find("[INFO] MCP: reconnecting server 'alpha'") != std::string::npos);
  }

  REQUIRE(WaitUntil([&]() {
    return count_status(McpServerStatusEvent::Status::CONNECTED) == 2;
  }));
  REQUIRE(WaitUntil([&]() {
    return service->GetToolsSnapshot().size() == 2;
  }));
  REQUIRE(WaitUntil([&]() {
    return old_connection->CloseCount() == 1;
  }));
  REQUIRE(count_status(McpServerStatusEvent::Status::DISCONNECTED) == 1);
  REQUIRE(CountRequests(new_connection, "initialize") == 1);
  REQUIRE(new_connection->CloseCount() == 0);
}

TEST_CASE("MCP service replaces a server that is still being removed", "[mcp][service][lifecycle]") {
  RequireIsolated([&]() {
    auto service = CreateMcpService(
      McpServiceConfig{
        .thread_pool_size = 1,
      }
    );
    auto observer = std::make_shared<CapturingMcpServiceObserver>();
    service->SetObserver(observer);
    auto count_status = [&](McpServerStatusEvent::Status status) {
      std::lock_guard<std::mutex> lock(observer->mutex);
      return std::count_if(
        observer->server_status_events.begin(), observer->server_status_events.end(), [&](const auto& event) {
          return event.status == status;
        }
      );
    };

    auto old_connection = std::make_shared<FakeConnection>();
    old_connection->EnqueueSuccess("initialize", MakeInitializePayload(false, false));
    old_connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
    old_connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue), "tool-request-1");
    BlockedMethod blocked(old_connection, "tools/call");
    service->AddServer("alpha", old_connection);
    REQUIRE(WaitUntil([&]() {
      return count_status(McpServerStatusEvent::Status::CONNECTED) == 1;
    }));

    REQUIRE(service->CallToolAsync(
      55,
      McpToolCallRequest{
        .call_id = "call-1",
        .server_id = "alpha",
        .tool_name = "search",
      }
    ));
    REQUIRE(WaitUntil([&]() {
      return CountRequests(old_connection, "tools/call") == 1;
    }));

    auto new_connection = std::make_shared<FakeConnection>();
    new_connection->EnqueueSuccess("initialize", MakeInitializePayload(false, false));
    new_connection->EnqueueSuccess("tools/list", MakeToolsList({"search", "email"}));
    service->RemoveServer("alpha");
    service->AddServer("alpha", new_connection);
    REQUIRE(service->GetToolsSnapshot().empty());

    old_connection->UnblockMethod("tools/call");
    REQUIRE(WaitUntil([&]() {
      return count_status(McpServerStatusEvent::Status::CONNECTED) == 2;
    }));
    REQUIRE(WaitUntil([&]() {
      return service->GetToolsSnapshot().size() == 2;
    }));
    REQUIRE(WaitUntil([&]() {
      return old_connection->CloseCount() == 1;
    }));
    REQUIRE(count_status(McpServerStatusEvent::Status::DISCONNECTED) == 1);
    REQUIRE(CountRequests(new_connection, "initialize") == 1);
    REQUIRE(new_connection->CloseCount() == 0);
    std::lock_guard<std::mutex> lock(observer->mutex);
    REQUIRE(observer->tool_results.size() == 1);
    REQUIRE(observer->tool_results[0].token == 55);
    REQUIRE(observer->tool_results[0].result.error_message == "Server disconnected");
  });
}

TEST_CASE("MCP service refreshes tools after list changed notification", "[mcp][service][lifecycle]") {
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);

  auto connection = std::make_shared<FakeConnection>();
  connection->EnqueueSuccess("initialize", MakeInitializePayload(false, false));
  connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
  connection->EnqueueSuccess("tools/list", MakeToolsList({"search", "email"}));

  service->AddServer("alpha", connection);
  REQUIRE(WaitUntil([&]() {
    return HasServerStatus(observer, McpServerStatusEvent::Status::CONNECTED);
  }));

  connection->EmitNotification("notifications/tools/list_changed", Json::Value(Json::objectValue));

  REQUIRE(WaitUntil([&]() {
    return service->GetToolsSnapshot().size() == 2;
  }));
  REQUIRE(CountRequests(connection, "tools/list") == 2);
}

TEST_CASE("MCP service removes server and clears published snapshots", "[mcp][service][lifecycle]") {
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);

  auto connection = std::make_shared<FakeConnection>();
  connection->EnqueueSuccess("initialize", MakeInitializePayload(false, false));
  connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));

  service->AddServer("alpha", connection);
  REQUIRE(WaitUntil([&]() {
    return HasServerStatus(observer, McpServerStatusEvent::Status::CONNECTED);
  }));

  service->RemoveServer("alpha");

  REQUIRE(WaitUntil([&]() {
    return HasServerStatus(observer, McpServerStatusEvent::Status::DISCONNECTED);
  }));
  REQUIRE(service->GetServerIds().empty());
  REQUIRE(service->GetToolsSnapshot().empty());
  // The connection is closed after the status reaches the observer.
  REQUIRE(WaitUntil([&]() {
    return connection->CloseCount() == 1;
  }));
}

// A pool that stops starting new tasks on command: the task of an accepted request is guaranteed not to start before
// Shutdown, like a task that the stop of the pool managed to cancel.
class HoldingThreadPool final : public giga_edge::IThreadPool {
public:
  void Push(giga_edge::TaskPtr task) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (holding_) {
        held_.push_back(std::move(task));
        return;
      }
    }
    pool_.Push(std::move(task));
  }
  bool Cancel(const giga_edge::TaskPtr& task) override {
    return pool_.Cancel(task);
  }
  void Wait(const giga_edge::TaskPtr& task) override {
    pool_.Wait(task);
  }
  void AddEventBarrier(uint32_t id) override {
    pool_.AddEventBarrier(id);
  }
  void CommitEventBarrier(uint32_t id) override {
    pool_.CommitEventBarrier(id);
  }
  void Stop() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      held_.clear();
    }
    pool_.Stop();
  }
  void Hold() {
    std::lock_guard<std::mutex> lock(mutex_);
    holding_ = true;
  }
  void Release() {
    std::vector<giga_edge::TaskPtr> held;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      holding_ = false;
      held.swap(held_);
    }
    for (auto& task : held) {
      pool_.Push(std::move(task));
    }
  }

private:
  giga_edge::ThreadPool pool_{1};
  std::mutex mutex_;
  bool holding_ = false;
  std::vector<giga_edge::TaskPtr> held_;
};

struct HeldServiceFixture {
  HeldServiceFixture()
    : pool(new HoldingThreadPool())
    , service(CreateMcpService(McpServiceConfig{}, std::unique_ptr<giga_edge::IThreadPool>(pool)))
    , observer(std::make_shared<CapturingMcpServiceObserver>())
    , connection(std::make_shared<FakeConnection>()) {
    service->SetObserver(observer);
    connection->EnqueueSuccess("initialize", MakeInitializePayload(true, false));
    connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
    connection->EnqueueSuccess("resources/list", MakeResourcesList({"resource://clock"}));
    connection->EnqueueSuccess("resources/templates/list", MakeResourceTemplatesList({}));
    service->AddServer("alpha", connection);
    REQUIRE(WaitUntil([&]() {
      return HasServerStatus(observer, McpServerStatusEvent::Status::CONNECTED);
    }));
    pool->Hold();
  }

  void SubmitRequests() const {
    REQUIRE(
      service->CallToolAsync(1, McpToolCallRequest{.call_id = "tool", .server_id = "alpha", .tool_name = "search"})
    );
    REQUIRE(service->ReadResourceAsync(
      2, McpReadResourceRequest{.call_id = "read", .server_id = "alpha", .uri = "resource://clock"}
    ));
    REQUIRE(service->PingAsync(3, McpPingRequest{.call_id = "ping", .server_id = "alpha"}));
  }

  void RequireAllCompletedWithError() const {
    std::lock_guard<std::mutex> lock(observer->mutex);
    REQUIRE(observer->tool_results.size() == 1);
    REQUIRE(observer->tool_results[0].token == 1);
    REQUIRE(observer->tool_results[0].result.is_error);
    REQUIRE_FALSE(observer->tool_results[0].result.request_sent);
    REQUIRE(observer->read_results.size() == 1);
    REQUIRE(observer->read_results[0].token == 2);
    REQUIRE(observer->read_results[0].result.is_error);
    REQUIRE(observer->ping_results.size() == 1);
    REQUIRE(observer->ping_results[0].token == 3);
    REQUIRE(observer->ping_results[0].result.is_error);
  }

  HoldingThreadPool* pool;
  std::shared_ptr<IMcpService> service;
  std::shared_ptr<CapturingMcpServiceObserver> observer;
  std::shared_ptr<FakeConnection> connection;
};

TEST_CASE("MCP service completes requests it accepted but never dispatched at Shutdown", "[mcp][service][lifecycle]") {
  HeldServiceFixture f;
  f.SubmitRequests();
  f.service->Shutdown();
  f.RequireAllCompletedWithError();
  std::lock_guard<std::mutex> lock(f.observer->mutex);
  REQUIRE(f.observer->tool_results[0].result.error_code == ErrorCode::CANCELLED);
  REQUIRE(f.observer->tool_results[0].result.error_message == "Service shut down");
  REQUIRE(f.observer->read_results[0].result.error_message == "Service shut down");
  REQUIRE(f.observer->ping_results[0].result.error_message == "Service shut down");
}

TEST_CASE("MCP service completes requests of a replaced server at Shutdown", "[mcp][service][lifecycle]") {
  HeldServiceFixture f;
  f.SubmitRequests();
  f.service->AddServer("alpha", std::make_shared<FakeConnection>());
  f.service->Shutdown();
  f.RequireAllCompletedWithError();
  REQUIRE(HasServerStatus(f.observer, McpServerStatusEvent::Status::DISCONNECTED));
  REQUIRE(f.connection->CloseCount() == 1);
}

size_t CountStatus(const std::shared_ptr<CapturingMcpServiceObserver>& observer, McpServerStatusEvent::Status status) {
  std::lock_guard<std::mutex> lock(observer->mutex);
  return static_cast<size_t>(std::count_if(
    observer->server_status_events.begin(),
    observer->server_status_events.end(),
    [status](const McpServerStatusEvent& event) {
      return event.status == status;
    }
  ));
}

void EnqueueToolsServer(FakeConnection& connection) {
  connection.EnqueueSuccess("initialize", MakeInitializePayload(false, false));
  connection.EnqueueSuccess("tools/list", MakeToolsList({"search"}));
}

TEST_CASE(
  "MCP service drops a server whose transport closed and accepts it back", "[mcp][service][lifecycle][closed]"
) {
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);
  auto connection = std::make_shared<FakeConnection>();
  EnqueueToolsServer(*connection);
  service->AddServer("alpha", connection);
  REQUIRE(WaitUntil([&]() {
    return service->GetServerIds().size() == 1;
  }));

  connection->SimulateClose();
  REQUIRE(WaitUntil([&]() {
    return HasServerStatus(observer, McpServerStatusEvent::Status::DISCONNECTED);
  }));
  REQUIRE(service->GetServerIds().empty());
  REQUIRE(service->GetToolsSnapshot().empty());

  auto reconnected = std::make_shared<FakeConnection>();
  EnqueueToolsServer(*reconnected);
  service->AddServer("alpha", reconnected);
  REQUIRE(WaitUntil([&]() {
    return CountStatus(observer, McpServerStatusEvent::Status::CONNECTED) == 2;
  }));
  REQUIRE(service->GetServerIds() == std::vector<std::string>{"alpha"});
}

TEST_CASE(
  "MCP service drops a server whose transport closed before it was committed", "[mcp][service][lifecycle][closed]"
) {
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);
  auto connection = std::make_shared<FakeConnection>();
  EnqueueToolsServer(*connection);
  {
    const BlockedMethod blocked(connection, "tools/list");
    service->AddServer("alpha", connection);
    REQUIRE(WaitUntil([&]() {
      return CountRequests(connection, "tools/list") == 1;
    }));
    // There is no close handler yet: the transport must report the close when the handler is set.
    connection->SimulateClose();
  }
  REQUIRE(WaitUntil([&]() {
    return HasServerStatus(observer, McpServerStatusEvent::Status::DISCONNECTED);
  }));
  REQUIRE(service->GetServerIds().empty());
}

TEST_CASE(
  "MCP service keeps a server whose transport only reports a lost connection", "[mcp][service][lifecycle][closed]"
) {
  // isConnected()==false does not mean "forever": for the host it is an IPC failure after which the connection is
  // alive.
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);
  auto connection = std::make_shared<FakeConnection>();
  EnqueueToolsServer(*connection);
  service->AddServer("alpha", connection);
  REQUIRE(WaitUntil([&]() {
    return service->GetServerIds().size() == 1;
  }));

  connection->SetConnected(false);
  REQUIRE(
    service->CallToolAsync(1, McpToolCallRequest{.call_id = "lost", .server_id = "alpha", .tool_name = "search"})
  );
  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->tool_results.size() == 1;
  }));
  connection->SetConnected(true);
  connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue));
  REQUIRE(
    service->CallToolAsync(2, McpToolCallRequest{.call_id = "back", .server_id = "alpha", .tool_name = "search"})
  );
  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->tool_results.size() == 2;
  }));
  std::lock_guard<std::mutex> lock(observer->mutex);
  REQUIRE(observer->tool_results[0].result.error_code == ErrorCode::TRANSPORT_ERROR);
  REQUIRE_FALSE(observer->tool_results[1].result.is_error);
  REQUIRE(service->GetServerIds() == std::vector<std::string>{"alpha"});
  REQUIRE(std::none_of(observer->server_status_events.begin(), observer->server_status_events.end(), [](const auto& e) {
    return e.status == McpServerStatusEvent::Status::DISCONNECTED;
  }));
}

TEST_CASE("MCP service ignores a close reported by a replaced server", "[mcp][service][lifecycle][closed]") {
  HeldServiceFixture f;
  // The old server is replaced but its client is not closed yet: the pool tasks are held.
  auto replacement = std::make_shared<FakeConnection>();
  EnqueueToolsServer(*replacement);
  f.service->AddServer("alpha", replacement);
  f.connection->SimulateClose();
  f.pool->Release();
  REQUIRE(WaitUntil([&]() {
    return CountStatus(f.observer, McpServerStatusEvent::Status::CONNECTED) == 2;
  }));
  REQUIRE(WaitUntil([&]() {
    return f.connection->CloseCount() == 1;
  }));
  REQUIRE(f.service->GetServerIds() == std::vector<std::string>{"alpha"});
  REQUIRE(CountStatus(f.observer, McpServerStatusEvent::Status::DISCONNECTED) == 1);
}

TEST_CASE("MCP service reports error when initialize fails", "[mcp][service][lifecycle]") {
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);

  auto connection = std::make_shared<FakeConnection>();
  connection->EnqueueError("initialize", ErrorCode::TRANSPORT_ERROR, "init failed");

  service->AddServer("alpha", connection);

  REQUIRE(WaitUntil([&]() {
    return HasServerStatusWithMessage(observer, McpServerStatusEvent::Status::ERROR, "Initialize failed");
  }));
  REQUIRE(service->GetServerIds().empty());
  REQUIRE(CountRequests(connection, "initialize") == 1);
  REQUIRE(CountRequests(connection, "tools/list") == 0);
}

TEST_CASE("MCP service reports error for a transport that throws", "[mcp][service][lifecycle]") {
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);

  service->AddServer("host", std::make_shared<ThrowingConnection>());

  REQUIRE(WaitUntil([&]() {
    return HasServerStatusWithMessage(
      observer, McpServerStatusEvent::Status::ERROR, "Initialize failed: MCP transport failed: ipc parse failed"
    );
  }));
  REQUIRE(service->GetServerIds().empty());
}

TEST_CASE("MCP service reports error when initialize returns an unexpected shape", "[mcp][service][lifecycle]") {
  const auto payload = GENERATE(std::string(R"("ok")"), std::string(R"({"serverInfo":[1]})"));
  CAPTURE(payload);
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);

  auto connection = std::make_shared<FakeConnection>();
  Json::Value value;
  Json::Reader reader;
  REQUIRE(reader.parse(payload, value));
  connection->EnqueueSuccess("initialize", value);

  service->AddServer("alpha", connection);

  REQUIRE(WaitUntil([&]() {
    return HasServerStatusWithMessage(observer, McpServerStatusEvent::Status::ERROR, "Initialize failed: Invalid");
  }));
  REQUIRE(service->GetServerIds().empty());
  REQUIRE(CountRequests(connection, "tools/list") == 0);
}

TEST_CASE("MCP service refreshes tools changed while it loads the initial catalog", "[mcp][service][lifecycle]") {
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);
  auto connection = std::make_shared<FakeConnection>();
  connection->EnqueueSuccess("initialize", MakeInitializePayload(false, false));
  connection->EnqueueSuccess("tools/list", MakeToolsList({"old"}));
  connection->EnqueueSuccess("tools/list", MakeToolsList({"new"}));
  {
    // list_changed arrives while the server is not committed yet: the initial tools/list already returns a stale list.
    const BlockedMethod blocked(connection, "tools/list");
    service->AddServer("alpha", connection);
    REQUIRE(WaitUntil([&]() {
      return CountRequests(connection, "tools/list") == 1;
    }));
    connection->EmitNotification("notifications/tools/list_changed", Json::Value(Json::objectValue));
  }
  REQUIRE(WaitUntil([&]() {
    const auto tools = service->GetToolsSnapshot();
    return tools.size() == 1 && tools[0].tool_name == "new";
  }));
  REQUIRE(CountRequests(connection, "tools/list") == 2);
}

TEST_CASE("MCP service refreshes resources changed while it loads the initial catalog", "[mcp][service][lifecycle]") {
  McpServiceConfig config;
  config.resource_hydration.read_resources_on_list = true;
  auto service = CreateMcpService(config);
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);
  auto connection = std::make_shared<FakeConnection>();
  connection->EnqueueSuccess("initialize", MakeInitializePayload(true, false));
  connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
  connection->EnqueueSuccess("resources/list", MakeResourcesList({"resource://devices"}));
  connection->EnqueueSuccess("resources/templates/list", MakeResourceTemplatesList({}));
  connection->EnqueueSuccess("resources/list", MakeResourcesList({"resource://devices", "resource://metadata"}));
  connection->EnqueueSuccess("resources/templates/list", MakeResourceTemplatesList({}));
  for (const auto* uri : {"resource://devices", "resource://metadata"}) {
    Json::Value read(Json::objectValue);
    read["contents"][0]["uri"] = uri;
    read["contents"][0]["text"] = "{}";
    connection->EnqueueSuccess("resources/read", read);
  }
  const auto updates = [&observer]() {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->resource_updates.size();
  };
  {
    // list_changed during the initial resources/list: the server is not committed yet, the list is already stale.
    const BlockedMethod blocked(connection, "resources/list");
    service->AddServer("alpha", connection);
    REQUIRE(WaitUntil([&]() {
      return CountRequests(connection, "resources/list") == 1;
    }));
    connection->EmitNotification("notifications/resources/list_changed", Json::Value(Json::objectValue));
  }
  REQUIRE(WaitUntil([&]() {
    return service->GetResourcesSnapshot().size() == 2 && updates() == 2;
  }));
  // The hydration over the fresh list starts once: every resource is read exactly once.
  REQUIRE(StaysFalseFor(
    [&]() {
      return CountRequests(connection, "resources/list") > 2 || CountRequests(connection, "resources/read") > 2;
    },
    std::chrono::milliseconds(200)
  ));
  REQUIRE(CountRequests(connection, "resources/list") == 2);
  REQUIRE(CountRequests(connection, "resources/read") == 2);
}

TEST_CASE("MCP service does not complete a cancelled request again at Shutdown", "[mcp][service][lifecycle]") {
  HeldServiceFixture f;
  f.SubmitRequests();
  REQUIRE(f.service->Cancel(1));
  f.service->Shutdown();
  f.RequireAllCompletedWithError();
  std::lock_guard<std::mutex> lock(f.observer->mutex);
  REQUIRE(f.observer->tool_results[0].result.error_message == "Cancelled");
  REQUIRE(f.observer->read_results[0].result.error_message == "Service shut down");
}

TEST_CASE("MCP service lists tools of a server that declares no capabilities", "[mcp][service][lifecycle]") {
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);

  auto connection = std::make_shared<FakeConnection>();
  auto initialize = MakeInitializePayload(false, false);
  initialize["capabilities"] = Json::Value(Json::nullValue);
  connection->EnqueueSuccess("initialize", initialize);
  connection->EnqueueSuccess("tools/list", MakeToolsList({"get_date"}));

  service->AddServer("host", connection);

  REQUIRE(WaitUntil([&]() {
    return HasServerStatus(observer, McpServerStatusEvent::Status::CONNECTED);
  }));
  REQUIRE(service->GetToolsSnapshot().size() == 1);
  REQUIRE(service->GetToolsSnapshot()[0].tool_name == "get_date");
}

TEST_CASE("MCP service connects a resource-only server that does not know tools/list", "[mcp][service][lifecycle]") {
  const bool json_rpc_code_only = GENERATE(false, true);
  CAPTURE(json_rpc_code_only);
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);

  auto connection = std::make_shared<FakeConnection>();
  auto initialize = MakeInitializePayload(true, false);
  initialize["capabilities"].removeMember("tools");
  connection->EnqueueSuccess("initialize", initialize);
  if (json_rpc_code_only) {
    connection->EnqueueError(
      "tools/list", ErrorCode::SERVER_ERROR, "no tools", Json::Value(Json::nullValue), "", -32601
    );
  } else {
    connection->EnqueueError("tools/list", ErrorCode::METHOD_NOT_FOUND, "no tools");
  }
  connection->EnqueueSuccess("resources/list", MakeResourcesList({"house://state"}));
  connection->EnqueueSuccess("resources/templates/list", MakeResourceTemplatesList({}));

  service->AddServer("house", connection);

  REQUIRE(WaitUntil([&]() {
    return HasServerStatus(observer, McpServerStatusEvent::Status::CONNECTED);
  }));
  REQUIRE(service->GetToolsSnapshot().empty());
  REQUIRE(service->GetResourcesSnapshot().size() == 1);
}

TEST_CASE("MCP service fails a server that declares tools but does not know tools/list", "[mcp][service][lifecycle]") {
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);

  auto connection = std::make_shared<FakeConnection>();
  connection->EnqueueSuccess("initialize", MakeInitializePayload(false, false));
  connection->EnqueueError(
    "tools/list", ErrorCode::METHOD_NOT_FOUND, "no tools", Json::Value(Json::nullValue), "", -32601
  );

  service->AddServer("alpha", connection);

  REQUIRE(WaitUntil([&]() {
    return HasServerStatusWithMessage(observer, McpServerStatusEvent::Status::ERROR, "listTools failed");
  }));
  REQUIRE(service->GetServerIds().empty());
}

TEST_CASE(
  "MCP service fails a server without tools capability when tools/list fails otherwise", "[mcp][service][lifecycle]"
) {
  const auto code = GENERATE(ErrorCode::TIMEOUT, ErrorCode::TRANSPORT_ERROR, ErrorCode::INTERNAL_ERROR);
  CAPTURE(static_cast<int>(code));
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);

  auto connection = std::make_shared<FakeConnection>();
  auto initialize = MakeInitializePayload(false, false);
  initialize["capabilities"] = Json::Value(Json::nullValue);
  connection->EnqueueSuccess("initialize", initialize);
  connection->EnqueueError("tools/list", code, "tools failed");

  service->AddServer("host", connection);

  REQUIRE(WaitUntil([&]() {
    return HasServerStatusWithMessage(observer, McpServerStatusEvent::Status::ERROR, "listTools failed");
  }));
  REQUIRE(service->GetServerIds().empty());
}

TEST_CASE("MCP service reports error when initial tools listing fails", "[mcp][service][lifecycle]") {
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);

  auto connection = std::make_shared<FakeConnection>();
  connection->EnqueueSuccess("initialize", MakeInitializePayload(false, false));
  connection->EnqueueError("tools/list", ErrorCode::TRANSPORT_ERROR, "tools failed");

  service->AddServer("alpha", connection);

  REQUIRE(WaitUntil([&]() {
    return HasServerStatusWithMessage(observer, McpServerStatusEvent::Status::ERROR, "listTools failed");
  }));
  REQUIRE(service->GetServerIds().empty());
  REQUIRE(CountRequests(connection, "tools/list") == 1);
  // The connection is closed after the status reaches the observer.
  REQUIRE(WaitUntil([&]() {
    return connection->CloseCount() == 1;
  }));
}

TEST_CASE("MCP service fails pending tool request when server is removed", "[mcp][service][lifecycle]") {
  RequireIsolated([&]() {
    auto service = CreateMcpService(
      McpServiceConfig{
        .thread_pool_size = 2,
      }
    );
    auto observer = std::make_shared<CapturingMcpServiceObserver>();
    service->SetObserver(observer);

    auto connection = std::make_shared<FakeConnection>();
    connection->EnqueueSuccess("initialize", MakeInitializePayload(false, false));
    connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
    connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue), "tool-request-1");
    BlockedMethod blocked(connection, "tools/call");

    service->AddServer("alpha", connection);
    REQUIRE(WaitUntil([&]() {
      return HasServerStatus(observer, McpServerStatusEvent::Status::CONNECTED);
    }));

    REQUIRE(service->CallToolAsync(
      55,
      McpToolCallRequest{
        .call_id = "call-1",
        .server_id = "alpha",
        .tool_name = "search",
      }
    ));
    REQUIRE(WaitUntil([&]() {
      return CountRequests(connection, "tools/call") == 1;
    }));

    service->RemoveServer("alpha");

    REQUIRE(WaitUntil([&]() {
      std::lock_guard<std::mutex> lock(observer->mutex);
      return observer->tool_results.size() == 1
        && observer->tool_results[0].result.error_message == "Server disconnected";
    }));
    REQUIRE(WaitUntil([&]() {
      return HasServerStatus(observer, McpServerStatusEvent::Status::DISCONNECTED);
    }));

    connection->UnblockMethod("tools/call");
  });
}
