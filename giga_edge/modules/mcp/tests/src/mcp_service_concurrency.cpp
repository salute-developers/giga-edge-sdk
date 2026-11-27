#include "giga_edge/modules/mcp/environment.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/capturing_mcp_service_observer.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_connection.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/isolated_scenario.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/wait_utils.h"

#include <catch.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>

using giga_edge::mcp::CreateMcpService;
using giga_edge::mcp::ErrorCode;
using giga_edge::mcp::IMcpService;
using giga_edge::mcp::McpServerStatusEvent;
using giga_edge::mcp::McpServiceConfig;
using giga_edge::mcp::McpServiceEnvironment;
using giga_edge::mcp::McpToolDescriptor;
using giga_edge::mcp::test_utils::CapturingMcpServiceObserver;
using giga_edge::mcp::test_utils::FakeConnection;
using giga_edge::mcp::test_utils::kWaitHangGuard;
using giga_edge::mcp::test_utils::RequireIsolated;
using giga_edge::mcp::test_utils::WaitUntil;

namespace {

Json::Value Tools(const std::string& name) {
  Json::Value payload;
  payload["tools"][0]["name"] = name;
  return payload;
}

std::shared_ptr<FakeConnection> Connection(const std::string& name, bool resources = false) {
  auto connection = std::make_shared<FakeConnection>();
  Json::Value init;
  init["capabilities"]["tools"] = Json::Value(Json::objectValue);
  if (resources) {
    init["capabilities"]["resources"] = Json::Value(Json::objectValue);
  }
  connection->EnqueueSuccess("initialize", init);
  connection->EnqueueSuccess("tools/list", Tools(name));
  if (resources) {
    Json::Value list;
    list["resources"][0]["uri"] = "test://state";
    connection->EnqueueSuccess("resources/list", list);
    Json::Value templates;
    templates["resourceTemplates"] = Json::Value(Json::arrayValue);
    connection->EnqueueSuccess("resources/templates/list", templates);
    Json::Value state;
    state["contents"][0]["uri"] = "test://state";
    state["contents"][0]["text"] = name;
    connection->EnqueueSuccess("resources/read", state);
  }
  return connection;
}

size_t RequestCount(const FakeConnection& connection, const std::string& method) {
  size_t count = 0;
  for (const auto& call : connection.RequestCalls()) {
    count += call.method == method;
  }
  return count;
}

struct ScopeExit {
  std::function<void()> fn;
  ~ScopeExit() {
    fn();
  }
};

class BlockingObserver : public CapturingMcpServiceObserver {
public:
  std::shared_ptr<IMcpService> service;
  std::atomic<bool> entered{false};
  std::atomic<bool> timed_out{false};
  void Release() {
    {
      std::lock_guard<std::mutex> lock(barrier_);
      released_ = true;
    }
    cv_.notify_all();
  }
  void OnToolsSnapshotUpdated(std::vector<McpToolDescriptor> tools) override {
    service->GetToolsSnapshot();
    if (!entered.exchange(true)) {
      std::unique_lock<std::mutex> lock(barrier_);
      timed_out = !cv_.wait_for(lock, kWaitHangGuard, [&] {
        return released_;
      });
    }
    CapturingMcpServiceObserver::OnToolsSnapshotUpdated(std::move(tools));
  }
private:
  std::mutex barrier_;
  std::condition_variable cv_;
  bool released_ = false;
};

} // namespace

TEST_CASE("MCP serializes catalog publication without holding the state lock", "[mcp][service][concurrency]") {
  const bool remove_first = GENERATE(false, true);
  McpServiceConfig config;
  config.thread_pool_size = 2;
  auto service = CreateMcpService(config);
  auto observer = std::make_shared<BlockingObserver>();
  observer->service = service;
  service->SetObserver(observer);
  ScopeExit release{[&] {
    observer->Release();
  }};
  service->AddServer("one", Connection("first", true));
  REQUIRE(WaitUntil([&] {
    return observer->entered.load();
  }));
  service->AddServer("two", Connection("second", true));
  REQUIRE(WaitUntil([&] {
    return service->GetToolsSnapshot().size() == 2;
  }));
  if (remove_first) {
    service->RemoveServer("one");
    REQUIRE(WaitUntil([&] {
      return service->GetToolsSnapshot().size() == 1;
    }));
  }
  observer->Release();
  REQUIRE(WaitUntil([&] {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->server_status_events.size() == (remove_first ? 3 : 2);
  }));
  std::lock_guard<std::mutex> lock(observer->mutex);
  REQUIRE_FALSE(observer->timed_out);
  REQUIRE(observer->tool_snapshots.size() == (remove_first ? 3 : 2));
  REQUIRE(observer->tool_snapshots[0].size() == 1);
  REQUIRE(observer->tool_snapshots[1].size() == 2);
  REQUIRE(observer->resource_snapshots[0].size() == 1);
  REQUIRE(observer->resource_snapshots[1].size() == 2);
  if (remove_first) {
    REQUIRE(observer->tool_snapshots.back().size() == 1);
    REQUIRE(observer->tool_snapshots.back().front().server_id == "two");
    REQUIRE(observer->resource_snapshots.back().size() == 1);
    REQUIRE(observer->resource_snapshots.back().front().server_id == "two");
  }
}

TEST_CASE("MCP cannot hydrate a removed or reconnected server from an old request", "[mcp][service][concurrency]") {
  const bool reconnect = GENERATE(false, true);
  RequireIsolated([&]() {
    McpServiceConfig config;
    config.thread_pool_size = 2;
    config.resource_hydration.read_resources_on_list = true;
    McpServiceEnvironment env(config);
    const auto consumer = env.coordinator->RegisterConsumer(
      {.consumer_name = "review", .include_shared_latest_resource_states = true}
    );
    auto old = Connection("old", true);
    old->BlockMethod("resources/read");
    ScopeExit unblock{[&] {
      old->UnblockMethod("resources/read");
    }};
    env.service->AddServer("same", old);
    REQUIRE(WaitUntil([&] {
      return RequestCount(*old, "resources/read") == 1;
    }));
    env.service->RemoveServer("same");
    REQUIRE(WaitUntil([&] {
      return old->CloseCount() > 0;
    }));
    if (reconnect) {
      env.service->AddServer("same", Connection("new", true));
      REQUIRE(WaitUntil([&] {
        const auto latest = env.coordinator->GetLatestResourceStatesSnapshot(consumer);
        return latest.data && latest.data->size() == 1 && latest.data->front().data["text"] == "new";
      }));
    }
    old->UnblockMethod("resources/read");
    env.service->Shutdown();
    const auto latest = env.coordinator->GetLatestResourceStatesSnapshot(consumer);
    REQUIRE(latest.data);
    REQUIRE(latest.data->size() == (reconnect ? 1 : 0));
    if (reconnect) {
      REQUIRE(latest.data->front().data["text"] == "new");
    }
  });
}

TEST_CASE("MCP ignores a tools refresh from the previous connection", "[mcp][service][concurrency]") {
  RequireIsolated([&]() {
    McpServiceConfig config;
    config.thread_pool_size = 2;
    McpServiceEnvironment env(config);
    const auto consumer = env.coordinator->RegisterConsumer({.consumer_name = "review"});
    auto old = Connection("old");
    env.service->AddServer("same", old);
    REQUIRE(WaitUntil([&] {
      return env.service->GetToolsSnapshot().size() == 1;
    }));
    old->EnqueueSuccess("tools/list", Tools("late"));
    old->BlockMethod("tools/list");
    ScopeExit unblock{[&] {
      old->UnblockMethod("tools/list");
    }};
    old->EmitNotification("notifications/tools/list_changed", Json::Value(Json::objectValue));
    REQUIRE(WaitUntil([&] {
      return RequestCount(*old, "tools/list") == 2;
    }));
    env.service->RemoveServer("same");
    REQUIRE(WaitUntil([&] {
      return old->CloseCount() > 0;
    }));
    env.service->AddServer("same", Connection("new"));
    REQUIRE(WaitUntil([&] {
      const auto tools = env.coordinator->GetToolsSnapshot(consumer);
      return tools.data && tools.data->size() == 1 && tools.data->front().tool_name == "new";
    }));
    old->UnblockMethod("tools/list");
    env.service->Shutdown();
    const auto tools = env.coordinator->GetToolsSnapshot(consumer);
    REQUIRE(tools.data->size() == 1);
    REQUIRE(tools.data->front().tool_name == "new");
  });
}

TEST_CASE("MCP ignores initialization of a removed connection", "[mcp][service][concurrency]") {
  RequireIsolated([&]() {
    McpServiceConfig config;
    config.thread_pool_size = 2;
    auto service = CreateMcpService(config);
    auto observer = std::make_shared<CapturingMcpServiceObserver>();
    service->SetObserver(observer);
    auto old = Connection("old");
    old->BlockMethod("initialize");
    ScopeExit unblock{[&] {
      old->UnblockMethod("initialize");
    }};
    service->AddServer("same", old);
    REQUIRE(WaitUntil([&] {
      return RequestCount(*old, "initialize") == 1;
    }));
    service->RemoveServer("same");
    REQUIRE(WaitUntil([&] {
      std::lock_guard<std::mutex> lock(observer->mutex);
      return !observer->server_status_events.empty()
        && observer->server_status_events.back().status == McpServerStatusEvent::Status::DISCONNECTED;
    }));
    service->AddServer("same", Connection("new"));
    REQUIRE(WaitUntil([&] {
      std::lock_guard<std::mutex> lock(observer->mutex);
      return !observer->tool_snapshots.empty() && observer->tool_snapshots.back().size() == 1;
    }));
    old->UnblockMethod("initialize");
    service->Shutdown();
    REQUIRE(RequestCount(*old, "tools/list") == 0);
    std::lock_guard<std::mutex> lock(observer->mutex);
    REQUIRE(observer->tool_snapshots.back().front().tool_name == "new");
  });
}

TEST_CASE("MCP reports concurrent initialization failures", "[mcp][service][concurrency][logging]") {
  McpServiceConfig config;
  config.thread_pool_size = 2;
  auto service = CreateMcpService(config);
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);
  auto first = std::make_shared<FakeConnection>();
  auto second = std::make_shared<FakeConnection>();
  for (const auto& connection : {first, second}) {
    connection->EnqueueError("initialize", giga_edge::mcp::ErrorCode::INTERNAL_ERROR, "initialization rejected");
    connection->BlockMethod("initialize");
  }
  ScopeExit release{[&] {
    first->UnblockMethod("initialize");
    second->UnblockMethod("initialize");
  }};
  service->AddServer("first", first);
  service->AddServer("second", second);
  REQUIRE(WaitUntil([&] {
    return RequestCount(*first, "initialize") == 1 && RequestCount(*second, "initialize") == 1;
  }));
  first->UnblockMethod("initialize");
  second->UnblockMethod("initialize");
  REQUIRE(WaitUntil([&] {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->server_status_events.size() == 2;
  }));
  service->Shutdown();
  std::lock_guard<std::mutex> lock(observer->mutex);
  for (const auto& event : observer->server_status_events) {
    REQUIRE(event.status == McpServerStatusEvent::Status::ERROR);
    REQUIRE(event.message == "Initialize failed: initialization rejected");
  }
  REQUIRE(service->GetToolsSnapshot().empty());
  REQUIRE(RequestCount(*first, "tools/list") == 0);
  REQUIRE(RequestCount(*second, "tools/list") == 0);
}

TEST_CASE("MCP fake connection answers a resource read with the response for its uri", "[mcp][test_utils]") {
  FakeConnection connection;
  const auto payload = [](const char* uri) {
    Json::Value value(Json::objectValue);
    value["contents"][0]["uri"] = uri;
    value["contents"][0]["text"] = uri;
    return value;
  };
  connection.EnqueueSuccess("resources/read", payload("resource://a"));
  connection.EnqueueSuccess("resources/read", payload("resource://b"));
  connection.EnqueueError("resources/read", ErrorCode::TIMEOUT, "scripted");
  Json::Value params(Json::objectValue);
  std::string request_id;
  params["uri"] = "resource://b";
  REQUIRE(
    connection.sendRequest("resources/read", params, request_id, 100).value["contents"][0]["text"] == "resource://b"
  );
  params["uri"] = "resource://c";
  REQUIRE(connection.sendRequest("resources/read", params, request_id, 100).error.code == ErrorCode::TIMEOUT);
  params["uri"] = "resource://a";
  REQUIRE(
    connection.sendRequest("resources/read", params, request_id, 100).value["contents"][0]["text"] == "resource://a"
  );
}
