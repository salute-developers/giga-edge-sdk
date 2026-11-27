#include "giga_edge/lib/threadpool/threadpool.h"
#include "giga_edge/modules/mcp/client.h"
#include "giga_edge/modules/mcp/coordinator.h"
#include "giga_edge/modules/mcp/service.h"
#include "giga_edge/modules/mcp/service/resource_retry.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/capturing_mcp_service_observer.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_connection.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/isolated_scenario.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/wait_utils.h"

#include <catch.hpp>

#include <algorithm>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <utility>

using namespace giga_edge::mcp;
using giga_edge::mcp::test_utils::BlockedMethod;
using giga_edge::mcp::test_utils::CapturingMcpServiceObserver;
using giga_edge::mcp::test_utils::FakeConnection;
using giga_edge::mcp::test_utils::kWaitHangGuard;
using giga_edge::mcp::test_utils::RequireIsolated;
using giga_edge::mcp::test_utils::StaysFalseFor;
using giga_edge::mcp::test_utils::WaitUntil;

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
    item["title"] = std::string("title-") + name;
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

Json::Value MakeResourcesListWithSubscribable(std::initializer_list<std::pair<const char*, bool>> resources) {
  Json::Value payload(Json::objectValue);
  payload["resources"] = Json::Value(Json::arrayValue);
  for (const auto& [uri, subscribable] : resources) {
    Json::Value item(Json::objectValue);
    item["uri"] = uri;
    item["name"] = uri;
    item["mimeType"] = "application/json";
    item["description"] = "resource";
    item["subscribable"] = subscribable;
    payload["resources"].append(item);
  }
  return payload;
}

Json::Value MakeReadResourcePayload(const std::string& uri, const std::string& text) {
  Json::Value payload(Json::objectValue);
  payload["contents"] = Json::Value(Json::arrayValue);
  Json::Value content(Json::objectValue);
  content["uri"] = uri;
  content["mimeType"] = "application/json";
  content["text"] = text;
  payload["contents"].append(std::move(content));
  return payload;
}

struct ReadyServiceFixture {
  std::shared_ptr<IMcpService> service;
  std::shared_ptr<CapturingMcpServiceObserver> observer;
  std::shared_ptr<FakeConnection> connection;
};

ReadyServiceFixture CreateReadyService(
  bool supports_resources = true, bool supports_subscriptions = true, McpServiceConfig config = {}
) {
  ReadyServiceFixture fixture;
  fixture.service = CreateMcpService(std::move(config));
  fixture.observer = std::make_shared<CapturingMcpServiceObserver>();
  fixture.service->SetObserver(fixture.observer);
  fixture.connection = std::make_shared<FakeConnection>();
  fixture.connection->EnqueueSuccess("initialize", MakeInitializePayload(supports_resources, supports_subscriptions));
  fixture.connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
  if (supports_resources) {
    fixture.connection->EnqueueSuccess("resources/list", MakeResourcesList({"resource://clock"}));
    Json::Value templates(Json::objectValue);
    templates["resourceTemplates"] = Json::Value(Json::arrayValue);
    fixture.connection->EnqueueSuccess("resources/templates/list", std::move(templates));
  }
  fixture.service->AddServer("alpha", fixture.connection);

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    for (const auto& event : fixture.observer->server_status_events) {
      if (event.server_id == "alpha" && event.status == McpServerStatusEvent::Status::CONNECTED) {
        return true;
      }
    }
    return false;
  }));

  return fixture;
}

size_t CountReads(const std::shared_ptr<FakeConnection>& connection, const std::string& uri) {
  size_t count = 0;
  for (const auto& call : connection->RequestCalls()) {
    if (call.method == "resources/read" && call.params["uri"].asString() == uri) {
      ++count;
    }
  }
  return count;
}

size_t CountNotifications(const std::shared_ptr<FakeConnection>& connection, const std::string& method) {
  size_t count = 0;
  for (const auto& call : connection->NotificationCalls()) {
    if (call.method == method) {
      ++count;
    }
  }
  return count;
}

} // namespace

TEST_CASE("MCP service resolves operation timeouts before transport", "[mcp][service][timeouts]") {
  const bool configured = GENERATE(false, true);
  const auto override_ms = GENERATE(0u, 777u);
  RequireIsolated([&]() {
    McpServiceConfig config;
    if (configured) {
      config.default_request_timeout_ms = 1234;
      config.default_tool_call_timeout_ms = 2345;
      config.default_read_resource_timeout_ms = 3456;
      config.default_ping_timeout_ms = 4567;
    }
    auto fixture = CreateReadyService(true, true, config);
    fixture.connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue));
    fixture.connection->EnqueueSuccess("resources/read", MakeReadResourcePayload("resource://clock", "{}"));
    fixture.connection->EnqueueSuccess("ping", Json::Value(Json::objectValue));
    REQUIRE(fixture.service->CallToolAsync(
      1, McpToolCallRequest{.call_id = "tool", .server_id = "alpha", .tool_name = "search", .timeout_ms = override_ms}
    ));
    REQUIRE(fixture.service->ReadResourceAsync(
      2,
      McpReadResourceRequest{
        .call_id = "read", .server_id = "alpha", .uri = "resource://clock", .timeout_ms = override_ms
      }
    ));
    REQUIRE(
      fixture.service->PingAsync(3, McpPingRequest{.call_id = "ping", .server_id = "alpha", .timeout_ms = override_ms})
    );
    REQUIRE(WaitUntil([&] {
      std::lock_guard<std::mutex> lock(fixture.observer->mutex);
      return fixture.observer->tool_results.size() == 1 && fixture.observer->read_results.size() == 1
        && fixture.observer->ping_results.size() == 1;
    }));
    fixture.service->Shutdown();
    const auto calls = fixture.connection->RequestCalls();
    REQUIRE(calls.size() == 7);
    for (const auto& call : calls) {
      auto expected = config.default_request_timeout_ms;
      if (call.method == "tools/call") {
        expected = override_ms ? override_ms : config.default_tool_call_timeout_ms;
      } else if (call.method == "resources/read") {
        expected = override_ms ? override_ms : config.default_read_resource_timeout_ms;
      } else if (call.method == "ping") {
        expected = override_ms ? override_ms : config.default_ping_timeout_ms;
      }
      INFO(call.method);
      REQUIRE(call.timeout_ms == expected);
    }
  });
}

TEST_CASE("MCP service completes tool call requests", "[mcp][service][requests]") {
  auto fixture = CreateReadyService(false, false);

  Json::Value payload(Json::objectValue);
  payload["isError"] = false;
  payload["content"] = Json::Value(Json::arrayValue);
  payload["content"].append("done");
  fixture.connection->EnqueueSuccess("tools/call", payload, "tool-request-1");

  REQUIRE(fixture.service->CallToolAsync(
    42,
    McpToolCallRequest{
      .call_id = "call-1",
      .server_id = "alpha",
      .tool_name = "search",
    }
  ));

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->tool_results.size() == 1;
  }));

  std::lock_guard<std::mutex> lock(fixture.observer->mutex);
  REQUIRE(fixture.observer->tool_results[0].token == 42);
  REQUIRE_FALSE(fixture.observer->tool_results[0].result.is_error);
  REQUIRE(fixture.observer->tool_results[0].result.tool_name == "search");
}

TEST_CASE("MCP service forwards optional tool call metadata", "[mcp][service][metadata]") {
  McpServiceConfig config;
  config.default_tool_call_timeout_ms = 300000;
  auto fixture = CreateReadyService(false, false, config);
  McpToolCallRequest request{
    .call_id = "call-1",
    .server_id = "alpha",
    .tool_name = "search",
  };
  request.arguments["query"] = "test";
  SECTION("absent") {}
  SECTION("empty object") {
    request.metadata = Json::Value(Json::objectValue);
  }
  SECTION("context") {
    request.metadata["client"]["session_id"] = "session-1";
    request.metadata["progressToken"] = "progress-1";
  }
  fixture.connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue));
  REQUIRE(fixture.service->CallToolAsync(42, request));
  REQUIRE(WaitUntil([&] {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->tool_results.size() == 1;
  }));
  fixture.service->Shutdown();
  const auto calls = fixture.connection->RequestCalls();
  const auto call = std::find_if(calls.begin(), calls.end(), [](const auto& item) {
    return item.method == "tools/call";
  });
  REQUIRE(call != calls.end());
  Json::Value expected(Json::objectValue);
  expected["name"] = request.tool_name;
  expected["arguments"] = request.arguments;
  if (!request.metadata.isNull()) {
    expected["_meta"] = request.metadata;
  }
  REQUIRE(call->params == expected);
  REQUIRE(call->timeout_ms == 300000);
  std::lock_guard<std::mutex> lock(fixture.observer->mutex);
  REQUIRE(fixture.observer->tool_results[0].token == 42);
  REQUIRE(fixture.observer->tool_results[0].result.call_id == "call-1");
  REQUIRE_FALSE(fixture.observer->tool_results[0].result.is_error);
}

TEST_CASE("MCP service completes read resource and ping requests", "[mcp][service][requests]") {
  auto fixture = CreateReadyService();

  Json::Value read_payload(Json::objectValue);
  read_payload["contents"] = Json::Value(Json::arrayValue);
  Json::Value content(Json::objectValue);
  content["uri"] = "resource://clock";
  content["mimeType"] = "text/plain";
  content["text"] = "10:00";
  read_payload["contents"].append(content);
  fixture.connection->EnqueueSuccess("resources/read", read_payload, "read-request-1");
  fixture.connection->EnqueueSuccess("ping", Json::Value(Json::objectValue));

  REQUIRE(fixture.service->ReadResourceAsync(
    77,
    McpReadResourceRequest{
      .call_id = "read-1",
      .server_id = "alpha",
      .uri = "resource://clock",
    }
  ));
  REQUIRE(fixture.service->PingAsync(
    78,
    McpPingRequest{
      .call_id = "ping-1",
      .server_id = "alpha",
    }
  ));

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->read_results.size() == 1 && fixture.observer->ping_results.size() == 1;
  }));

  std::lock_guard<std::mutex> lock(fixture.observer->mutex);
  REQUIRE(fixture.observer->read_results[0].token == 77);
  REQUIRE(fixture.observer->read_results[0].result.uri == "resource://clock");
  REQUIRE(fixture.observer->read_results[0].result.text == "10:00");
  REQUIRE(fixture.observer->ping_results[0].token == 78);
  REQUIRE_FALSE(fixture.observer->ping_results[0].result.is_error);
}

TEST_CASE("MCP service reports server unavailable for unknown server requests", "[mcp][service][requests]") {
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);

  REQUIRE(service->CallToolAsync(
    99,
    McpToolCallRequest{
      .call_id = "call-1",
      .server_id = "missing",
      .tool_name = "search",
    }
  ));

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->tool_results.size() == 1;
  }));

  std::lock_guard<std::mutex> lock(observer->mutex);
  REQUIRE(observer->tool_results[0].result.is_error);
  REQUIRE(observer->tool_results[0].result.error_message == "Server not available: missing");
}

TEST_CASE("MCP service cancel emits cancelled completion and clears pending request", "[mcp][service][requests]") {
  auto fixture = CreateReadyService(false, false);
  BlockedMethod blocked(fixture.connection, "tools/call");
  fixture.connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue));

  REQUIRE(fixture.service->CallToolAsync(
    101,
    McpToolCallRequest{
      .call_id = "call-1",
      .server_id = "alpha",
      .tool_name = "search",
    }
  ));

  REQUIRE(fixture.service->Cancel(101));
  REQUIRE_FALSE(fixture.service->Cancel(101));

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->tool_results.size() == 1;
  }));

  std::lock_guard<std::mutex> lock(fixture.observer->mutex);
  REQUIRE(fixture.observer->tool_results[0].token == 101);
  REQUIRE(fixture.observer->tool_results[0].result.is_error);
  REQUIRE(fixture.observer->tool_results[0].result.error_message == "Cancelled");
  REQUIRE(fixture.observer->tool_results[0].result.tool_name == "search");
}

TEST_CASE("MCP service rejects duplicate request tokens", "[mcp][service][requests]") {
  auto fixture = CreateReadyService(false, false);
  BlockedMethod blocked(fixture.connection, "tools/call");
  fixture.connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue));

  REQUIRE(fixture.service->CallToolAsync(
    202,
    McpToolCallRequest{
      .call_id = "call-1",
      .server_id = "alpha",
      .tool_name = "search",
    }
  ));
  REQUIRE_FALSE(fixture.service->PingAsync(
    202,
    McpPingRequest{
      .call_id = "ping-1",
      .server_id = "alpha",
    }
  ));

  REQUIRE(fixture.service->Cancel(202));

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->tool_results.size() == 1;
  }));

  std::lock_guard<std::mutex> lock(fixture.observer->mutex);
  REQUIRE(fixture.observer->tool_results.size() == 1);
  REQUIRE(fixture.observer->ping_results.empty());
  REQUIRE(fixture.observer->tool_results[0].token == 202);
  REQUIRE(fixture.observer->tool_results[0].result.error_message == "Cancelled");
}

TEST_CASE("MCP service cancels dispatched requests and drops late responses", "[mcp][service][cancellation]") {
  const std::string method = GENERATE("tools/call", "resources/read", "ping");
  const bool fail = GENERATE(false, true);
  RequireIsolated([&]() {
    auto fixture = CreateReadyService();
    BlockedMethod blocked(fixture.connection, method);
    if (fail) {
      fixture.connection->EnqueueError(method, ErrorCode::TIMEOUT, "timed out", {}, "wire-request");
    } else {
      fixture.connection->EnqueueSuccess(method, MakeReadResourcePayload("resource://clock", "{}"), "wire-request");
    }
    bool accepted = false;
    if (method == "tools/call") {
      accepted = fixture.service->CallToolAsync(
        42, McpToolCallRequest{.call_id = "logical-id", .server_id = "alpha", .tool_name = "search"}
      );
    } else if (method == "resources/read") {
      accepted = fixture.service->ReadResourceAsync(
        42, McpReadResourceRequest{.call_id = "logical-id", .server_id = "alpha", .uri = "resource://clock"}
      );
    } else {
      accepted = fixture.service->PingAsync(42, McpPingRequest{.call_id = "logical-id", .server_id = "alpha"});
    }
    REQUIRE(accepted);
    REQUIRE(WaitUntil([&] {
      const auto calls = fixture.connection->RequestCalls();
      return std::any_of(calls.begin(), calls.end(), [&](const auto& call) {
        return call.method == method && call.request_id == "wire-request";
      });
    }));
    REQUIRE(fixture.service->Cancel(42));
    REQUIRE_FALSE(fixture.service->Cancel(42));
    REQUIRE(WaitUntil([&] {
      return CountNotifications(fixture.connection, "notifications/cancelled") == 1;
    }));
    for (const auto& notification : fixture.connection->NotificationCalls()) {
      if (notification.method == "notifications/cancelled") {
        REQUIRE(notification.params["requestId"].asString() == "wire-request");
      }
    }

    fixture.connection->UnblockMethod(method);
    fixture.service->Shutdown();
    REQUIRE(CountNotifications(fixture.connection, "notifications/cancelled") == 1);
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    REQUIRE(
      fixture.observer->tool_results.size() + fixture.observer->read_results.size()
        + fixture.observer->ping_results.size()
      == 1
    );
    const auto check = [](const auto& results) {
      if (!results.empty()) {
        REQUIRE(results[0].token == 42);
        REQUIRE(results[0].result.call_id == "logical-id");
        REQUIRE(results[0].result.is_error);
        REQUIRE(results[0].result.error_message == "Cancelled");
      }
    };
    check(fixture.observer->tool_results);
    check(fixture.observer->read_results);
    check(fixture.observer->ping_results);
  });
}

TEST_CASE("MCP service never dispatches a cancelled queued request", "[mcp][service][cancellation]") {
  RequireIsolated([&]() {
    McpServiceConfig config;
    config.thread_pool_size = 1;
    auto fixture = CreateReadyService(false, false, config);
    BlockedMethod blocked(fixture.connection, "ping");
    fixture.connection->EnqueueSuccess("ping", Json::Value(Json::objectValue));
    REQUIRE(fixture.service->PingAsync(1, McpPingRequest{.call_id = "blocker", .server_id = "alpha"}));
    REQUIRE(WaitUntil([&] {
      const auto calls = fixture.connection->RequestCalls();
      return std::any_of(calls.begin(), calls.end(), [](const auto& call) {
        return call.method == "ping";
      });
    }));
    REQUIRE(fixture.service->CallToolAsync(
      2, McpToolCallRequest{.call_id = "queued", .server_id = "alpha", .tool_name = "search"}
    ));
    REQUIRE(fixture.service->Cancel(2));
    fixture.connection->EnqueueSuccess("ping", Json::Value(Json::objectValue));
    REQUIRE(fixture.service->PingAsync(3, McpPingRequest{.call_id = "barrier", .server_id = "alpha"}));
    fixture.connection->UnblockMethod("ping");
    REQUIRE(WaitUntil([&] {
      std::lock_guard<std::mutex> lock(fixture.observer->mutex);
      return fixture.observer->ping_results.size() == 2;
    }));
    fixture.service->Shutdown();
    for (const auto& call : fixture.connection->RequestCalls()) {
      REQUIRE(call.method != "tools/call");
    }
    REQUIRE(CountNotifications(fixture.connection, "notifications/cancelled") == 0);
  });
}

TEST_CASE("MCP service rereads a subscribed resource on an update notification", "[mcp][service][requests]") {
  auto fixture = CreateReadyService();

  fixture.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  fixture.connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue));
  fixture.connection->EnqueueSuccess(
    "resources/read", MakeReadResourcePayload("resource://clock", R"({"time":"10:00"})")
  );

  REQUIRE(fixture.service->SubscribeResourceAsync(11, "alpha", "resource://clock"));
  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->resource_subscription_events.size() == 1;
  }));

  // By the protocol the notification carries only the uri: an extra field must not become the resource state.
  Json::Value update(Json::objectValue);
  update["uri"] = "resource://clock";
  update["value"] = "stale";
  fixture.connection->EmitNotification("notifications/resources/updated", update);

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->resource_updates.size() == 1;
  }));
  REQUIRE(CountReads(fixture.connection, "resource://clock") == 1);
  {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    const auto& state = fixture.observer->resource_updates[0].data;
    REQUIRE(state["data"]["time"].asString() == "10:00");
    REQUIRE_FALSE(state.isMember("value"));
  }

  REQUIRE(fixture.service->UnsubscribeResourceAsync(12, "alpha", "resource://clock"));
  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->resource_subscription_events.size() == 2;
  }));

  fixture.connection->EmitNotification("notifications/resources/updated", update);
  REQUIRE(StaysFalseFor(
    [&]() {
      return CountReads(fixture.connection, "resource://clock") > 1;
    },
    std::chrono::milliseconds(100)
  ));

  std::lock_guard<std::mutex> lock(fixture.observer->mutex);
  REQUIRE(
    fixture.observer->resource_subscription_events[0].operation == McpResourceSubscriptionEvent::Operation::SUBSCRIBE
  );
  REQUIRE(
    fixture.observer->resource_subscription_events[1].operation == McpResourceSubscriptionEvent::Operation::UNSUBSCRIBE
  );
  REQUIRE(fixture.observer->resource_updates.size() == 1);
}

TEST_CASE(
  "MCP service auto hydrates listed resources and subscribes to subscribable ones",
  "[mcp][service][requests][hydration]"
) {
  auto service = CreateMcpService(
    McpServiceConfig{
      .resource_hydration = {
        .read_resources_on_list = true,
        .subscribe_to_subscribable_resources = true,
      },
    }
  );
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);
  auto connection = std::make_shared<FakeConnection>();

  connection->EnqueueSuccess("initialize", MakeInitializePayload(true, true));
  connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
  connection->EnqueueSuccess(
    "resources/list",
    MakeResourcesListWithSubscribable({
      {"resource://devices", true},
      {"resource://metadata", false},
    })
  );
  Json::Value templates(Json::objectValue);
  templates["resourceTemplates"] = Json::Value(Json::arrayValue);
  connection->EnqueueSuccess("resources/templates/list", std::move(templates));
  connection->EnqueueSuccess("resources/read", MakeReadResourcePayload("resource://devices", R"({"devices":[1]})"));
  connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  connection->EnqueueSuccess("resources/read", MakeReadResourcePayload("resource://metadata", R"({"version":1})"));

  service->AddServer("alpha", connection);

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->resource_updates.size() == 2;
  }));

  const auto calls = connection->RequestCalls();
  size_t read_count = 0;
  size_t subscribe_count = 0;
  for (const auto& call : calls) {
    if (call.method == "resources/read") {
      ++read_count;
      REQUIRE(call.params.isMember("uri"));
      REQUIRE_FALSE(call.params.isMember("device_id"));
    } else if (call.method == "resources/subscribe") {
      ++subscribe_count;
      REQUIRE(call.params["uri"].asString() == "resource://devices");
      REQUIRE_FALSE(call.params.isMember("device_id"));
    }
  }
  REQUIRE(read_count == 2);
  REQUIRE(subscribe_count == 1);

  // Reads of different resources are independent: the order of their publication is not defined.
  std::lock_guard<std::mutex> lock(observer->mutex);
  const auto devices = std::find_if(
    observer->resource_updates.begin(), observer->resource_updates.end(), [](const McpResourceUpdatedEvent& update) {
      return update.uri == "resource://devices";
    }
  );
  REQUIRE(devices != observer->resource_updates.end());
  REQUIRE(devices->server_id == "alpha");
  REQUIRE(devices->data["mimeType"].asString() == "application/json");
  REQUIRE(devices->data["data"]["devices"][0].asInt() == 1);
}

TEST_CASE("MCP service keeps a resource nested too deep for JSON as text", "[mcp][service][requests][hydration]") {
  auto service = CreateMcpService(McpServiceConfig{.resource_hydration = {.read_resources_on_list = true}});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);
  auto connection = std::make_shared<FakeConnection>();

  // Past its stack limit of 1000 nested values jsoncpp throws instead of reporting a parse error.
  const std::string deep(2000, '[');
  connection->EnqueueSuccess("initialize", MakeInitializePayload(true, false));
  connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
  connection->EnqueueSuccess("resources/list", MakeResourcesListWithSubscribable({{"resource://deep", false}}));
  Json::Value templates(Json::objectValue);
  templates["resourceTemplates"] = Json::Value(Json::arrayValue);
  connection->EnqueueSuccess("resources/templates/list", std::move(templates));
  connection->EnqueueSuccess("resources/read", MakeReadResourcePayload("resource://deep", deep));

  service->AddServer("alpha", connection);

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->resource_updates.size() == 1;
  }));
  std::lock_guard<std::mutex> lock(observer->mutex);
  const auto& update = observer->resource_updates[0];
  REQUIRE(update.uri == "resource://deep");
  REQUIRE(update.data["text"].asString() == deep);
  REQUIRE_FALSE(update.data.isMember("data"));
}

TEST_CASE("MCP service can prefer one aggregate resource for auto hydration", "[mcp][service][requests][hydration]") {
  auto service = CreateMcpService(
    McpServiceConfig{
      .resource_hydration = {
        .read_resources_on_list = true,
        .subscribe_to_subscribable_resources = true,
        .preferred_read_resource_match_phrases = {"all_devices", "devices"},
      },
    }
  );
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);
  auto connection = std::make_shared<FakeConnection>();

  connection->EnqueueSuccess("initialize", MakeInitializePayload(true, true));
  connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
  connection->EnqueueSuccess(
    "resources/list",
    MakeResourcesListWithSubscribable({
      {"resource://smart_home/devices", true},
      {"resource://smart_home/device/light_0", true},
      {"resource://smart_home/device/kettle_0", true},
    })
  );
  Json::Value templates(Json::objectValue);
  templates["resourceTemplates"] = Json::Value(Json::arrayValue);
  connection->EnqueueSuccess("resources/templates/list", std::move(templates));
  connection->EnqueueSuccess(
    "resources/read",
    MakeReadResourcePayload("resource://smart_home/devices", R"({"scene_devices":[{"device_id":"kettle_0"}]})")
  );
  connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));

  service->AddServer("alpha", connection);

  REQUIRE(WaitUntil([&]() {
    const auto calls = connection->RequestCalls();
    const auto subscriptions = std::count_if(calls.begin(), calls.end(), [](const auto& call) {
      return call.method == "resources/subscribe";
    });
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->resource_updates.size() == 1 && subscriptions == 3;
  }));

  size_t read_count = 0;
  size_t subscribe_count = 0;
  for (const auto& call : connection->RequestCalls()) {
    if (call.method == "resources/read") {
      ++read_count;
      REQUIRE(call.params["uri"].asString() == "resource://smart_home/devices");
    } else if (call.method == "resources/subscribe") {
      ++subscribe_count;
    }
  }
  REQUIRE(read_count == 1);
  REQUIRE(subscribe_count == 3);

  size_t aggregate_subscribes = 0;
  for (const auto& call : connection->RequestCalls()) {
    if (call.method == "resources/subscribe" && call.params["uri"].asString() == "resource://smart_home/devices") {
      ++aggregate_subscribes;
    }
  }
  REQUIRE(aggregate_subscribes == 1);
}

TEST_CASE(
  "MCP service rereads changed per-device resource after child resource update", "[mcp][service][requests][hydration]"
) {
  auto service = CreateMcpService(
    McpServiceConfig{
      .resource_hydration = {
        .read_resources_on_list = true,
        .subscribe_to_subscribable_resources = true,
        .preferred_read_resource_match_phrases = {"devices"},
      },
    }
  );
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);
  auto connection = std::make_shared<FakeConnection>();

  connection->EnqueueSuccess("initialize", MakeInitializePayload(true, true));
  connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
  connection->EnqueueSuccess(
    "resources/list",
    MakeResourcesListWithSubscribable({
      {"resource://smart_home/devices", true},
      {"resource://smart_home/device/kettle_0", true},
    })
  );
  Json::Value templates(Json::objectValue);
  templates["resourceTemplates"] = Json::Value(Json::arrayValue);
  connection->EnqueueSuccess("resources/templates/list", std::move(templates));
  connection->EnqueueSuccess(
    "resources/read", MakeReadResourcePayload("resource://smart_home/devices", R"({"version":1})")
  );
  connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));

  service->AddServer("alpha", connection);

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->resource_updates.size() == 1;
  }));

  connection->EnqueueSuccess(
    "resources/read",
    MakeReadResourcePayload(
      "resource://smart_home/device/kettle_0",
      R"({"XID":"kettle_0","name":"Чайник","features":[{"name":"on_off","type":"BOOL","value":true}]})"
    )
  );
  Json::Value update(Json::objectValue);
  update["uri"] = "resource://smart_home/device/kettle_0";
  connection->EmitNotification("notifications/resources/updated", update);

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->resource_updates.size() == 2;
  }));

  size_t aggregate_reads = 0;
  size_t child_reads = 0;
  for (const auto& call : connection->RequestCalls()) {
    if (call.method != "resources/read") {
      continue;
    }
    if (call.params["uri"].asString() == "resource://smart_home/devices") {
      ++aggregate_reads;
    } else if (call.params["uri"].asString() == "resource://smart_home/device/kettle_0") {
      ++child_reads;
    }
  }
  REQUIRE(aggregate_reads == 1);
  REQUIRE(child_reads == 1);

  std::lock_guard<std::mutex> lock(observer->mutex);
  REQUIRE(observer->resource_updates.back().uri == "resource://smart_home/device/kettle_0");
  REQUIRE(observer->resource_updates.back().data["data"]["features"][0]["value"].asBool() == true);
}

TEST_CASE(
  "MCP service reads subscribed resource again after update notification", "[mcp][service][requests][hydration]"
) {
  auto service = CreateMcpService(
    McpServiceConfig{
      .resource_hydration = {
        .read_resources_on_list = true,
        .subscribe_to_subscribable_resources = true,
      },
    }
  );
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);
  auto connection = std::make_shared<FakeConnection>();

  connection->EnqueueSuccess("initialize", MakeInitializePayload(true, true));
  connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
  connection->EnqueueSuccess("resources/list", MakeResourcesList({"resource://devices"}));
  Json::Value templates(Json::objectValue);
  templates["resourceTemplates"] = Json::Value(Json::arrayValue);
  connection->EnqueueSuccess("resources/templates/list", std::move(templates));
  connection->EnqueueSuccess("resources/read", MakeReadResourcePayload("resource://devices", R"({"version":1})"));
  connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));

  service->AddServer("alpha", connection);

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->resource_updates.size() == 1;
  }));

  connection->EnqueueSuccess("resources/read", MakeReadResourcePayload("resource://devices", R"({"version":2})"));
  Json::Value update(Json::objectValue);
  update["uri"] = "resource://devices";
  connection->EmitNotification("notifications/resources/updated", update);

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->resource_updates.size() == 2;
  }));

  std::lock_guard<std::mutex> lock(observer->mutex);
  REQUIRE(observer->resource_updates.back().data["data"]["version"].asInt() == 2);
}

TEST_CASE("MCP service hydrates resources added after resources list_changed", "[mcp][service][requests][hydration]") {
  auto service = CreateMcpService(
    McpServiceConfig{
      .resource_hydration = {
        .read_resources_on_list = true,
        .subscribe_to_subscribable_resources = true,
      },
    }
  );
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);
  auto connection = std::make_shared<FakeConnection>();

  connection->EnqueueSuccess("initialize", MakeInitializePayload(true, true));
  connection->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
  connection->EnqueueSuccess("resources/list", MakeResourcesList({"resource://devices"}));
  Json::Value templates(Json::objectValue);
  templates["resourceTemplates"] = Json::Value(Json::arrayValue);
  connection->EnqueueSuccess("resources/templates/list", templates);
  connection->EnqueueSuccess("resources/read", MakeReadResourcePayload("resource://devices", R"({"version":1})"));
  connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));

  service->AddServer("alpha", connection);

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->resource_updates.size() == 1;
  }));

  connection->EnqueueSuccess("resources/list", MakeResourcesList({"resource://devices", "resource://metadata"}));
  connection->EnqueueSuccess("resources/templates/list", templates);
  connection->EnqueueSuccess("resources/read", MakeReadResourcePayload("resource://metadata", R"({"version":2})"));
  connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  connection->EmitNotification("notifications/resources/list_changed", Json::Value(Json::objectValue));

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->resource_updates.size() == 2;
  }));

  size_t read_count = 0;
  for (const auto& call : connection->RequestCalls()) {
    if (call.method == "resources/read") {
      ++read_count;
    }
  }
  REQUIRE(read_count == 2);

  std::lock_guard<std::mutex> lock(observer->mutex);
  REQUIRE(observer->resource_updates.back().uri == "resource://metadata");
}

TEST_CASE(
  "MCP service emits error event for failed subscribe and does not deliver updates",
  "[mcp][service][requests][subscription_lifecycle]"
) {
  const std::string message = GENERATE("subscribe failed", "");
  auto fixture = CreateReadyService();

  fixture.connection->EnqueueError("resources/subscribe", ErrorCode::SERVER_ERROR, message);

  REQUIRE(fixture.service->SubscribeResourceAsync(11, "alpha", "resource://clock"));
  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->resource_subscription_events.size() == 1;
  }));

  Json::Value update(Json::objectValue);
  update["uri"] = "resource://clock";
  update["value"] = "10:00";
  fixture.connection->EmitNotification("notifications/resources/updated", update);

  std::lock_guard<std::mutex> lock(fixture.observer->mutex);
  REQUIRE(fixture.observer->resource_subscription_events[0].is_error);
  REQUIRE(fixture.observer->resource_subscription_events[0].error_message == message);
  REQUIRE(fixture.observer->resource_updates.empty());
}

TEST_CASE(
  "MCP service emits error event for failed unsubscribe and keeps update delivery",
  "[mcp][service][requests][subscription_lifecycle]"
) {
  const std::string message = GENERATE("unsubscribe failed", "");
  auto fixture = CreateReadyService();

  fixture.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  fixture.connection->EnqueueError("resources/unsubscribe", ErrorCode::SERVER_ERROR, message);

  REQUIRE(fixture.service->SubscribeResourceAsync(11, "alpha", "resource://clock"));
  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->resource_subscription_events.size() == 1;
  }));

  REQUIRE(fixture.service->UnsubscribeResourceAsync(12, "alpha", "resource://clock"));
  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->resource_subscription_events.size() == 2;
  }));

  fixture.connection->EnqueueSuccess(
    "resources/read", MakeReadResourcePayload("resource://clock", R"({"time":"10:00"})")
  );
  Json::Value update(Json::objectValue);
  update["uri"] = "resource://clock";
  fixture.connection->EmitNotification("notifications/resources/updated", update);
  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->resource_updates.size() == 1;
  }));

  std::lock_guard<std::mutex> lock(fixture.observer->mutex);
  REQUIRE_FALSE(fixture.observer->resource_subscription_events[0].is_error);
  REQUIRE(fixture.observer->resource_subscription_events[1].is_error);
  REQUIRE(fixture.observer->resource_subscription_events[1].error_message == message);
  // The unsubscribe failed: the subscription is alive, the update is delivered by a re-read.
  REQUIRE(fixture.observer->resource_updates[0].data["data"]["time"].asString() == "10:00");
}

TEST_CASE("MCP service sends MCP cancel notification on tool timeout", "[mcp][service][requests]") {
  auto fixture = CreateReadyService(false, false);

  fixture.connection->EnqueueError(
    "tools/call", ErrorCode::TIMEOUT, "timeout", Json::Value(Json::nullValue), "tool-request-1"
  );

  REQUIRE(fixture.service->CallToolAsync(
    43,
    McpToolCallRequest{
      .call_id = "call-1",
      .server_id = "alpha",
      .tool_name = "search",
      .timeout_ms = 123,
    }
  ));

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->tool_results.size() == 1;
  }));

  REQUIRE(CountNotifications(fixture.connection, "notifications/cancelled") == 1);

  const auto notifications = fixture.connection->NotificationCalls();
  REQUIRE(notifications.back().method == "notifications/cancelled");
  REQUIRE(notifications.back().params["requestId"].asString() == "tool-request-1");
  REQUIRE(notifications.back().params["reason"].asString() == "timeout");
}

TEST_CASE("MCP service executes multi-server mixed happy-path pipeline", "[mcp][service][requests]") {
  auto service = CreateMcpService(McpServiceConfig{});
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);

  auto alpha = std::make_shared<FakeConnection>();
  alpha->EnqueueSuccess("initialize", MakeInitializePayload(true, true));
  alpha->EnqueueSuccess("tools/list", MakeToolsList({"search"}));
  alpha->EnqueueSuccess("resources/list", MakeResourcesList({"resource://clock"}));
  Json::Value alpha_templates(Json::objectValue);
  alpha_templates["resourceTemplates"] = Json::Value(Json::arrayValue);
  alpha->EnqueueSuccess("resources/templates/list", std::move(alpha_templates));
  alpha->EnqueueSuccess("tools/call", Json::Value(Json::objectValue), "alpha-tool-req");
  Json::Value alpha_read(Json::objectValue);
  alpha_read["contents"] = Json::Value(Json::arrayValue);
  Json::Value alpha_content(Json::objectValue);
  alpha_content["uri"] = "resource://clock";
  alpha_content["mimeType"] = "text/plain";
  alpha_content["text"] = "10:00";
  alpha_read["contents"].append(alpha_content);
  alpha->EnqueueSuccess("resources/read", alpha_read, "alpha-read-req");
  alpha->EnqueueSuccess("ping", Json::Value(Json::objectValue));

  auto beta = std::make_shared<FakeConnection>();
  beta->EnqueueSuccess("initialize", MakeInitializePayload(true, true));
  beta->EnqueueSuccess("tools/list", MakeToolsList({"calendar"}));
  beta->EnqueueSuccess("resources/list", MakeResourcesList({"resource://tasks"}));
  Json::Value beta_templates(Json::objectValue);
  beta_templates["resourceTemplates"] = Json::Value(Json::arrayValue);
  beta->EnqueueSuccess("resources/templates/list", std::move(beta_templates));
  beta->EnqueueSuccess("tools/call", Json::Value(Json::objectValue), "beta-tool-req");

  service->AddServer("alpha", alpha);
  service->AddServer("beta", beta);

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(observer->mutex);
    bool alpha_connected = false;
    bool beta_connected = false;
    for (const auto& event : observer->server_status_events) {
      if (event.server_id == "alpha" && event.status == McpServerStatusEvent::Status::CONNECTED) {
        alpha_connected = true;
      }
      if (event.server_id == "beta" && event.status == McpServerStatusEvent::Status::CONNECTED) {
        beta_connected = true;
      }
    }
    return alpha_connected && beta_connected;
  }));

  REQUIRE(service->CallToolAsync(
    301,
    McpToolCallRequest{
      .call_id = "tool-alpha",
      .server_id = "alpha",
      .tool_name = "search",
    }
  ));
  REQUIRE(service->CallToolAsync(
    302,
    McpToolCallRequest{
      .call_id = "tool-beta",
      .server_id = "beta",
      .tool_name = "calendar",
    }
  ));
  REQUIRE(service->ReadResourceAsync(
    303,
    McpReadResourceRequest{
      .call_id = "read-alpha",
      .server_id = "alpha",
      .uri = "resource://clock",
    }
  ));
  REQUIRE(service->PingAsync(
    304,
    McpPingRequest{
      .call_id = "ping-alpha",
      .server_id = "alpha",
    }
  ));

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(observer->mutex);
    return observer->tool_results.size() == 2 && observer->read_results.size() == 1
      && observer->ping_results.size() == 1;
  }));

  std::lock_guard<std::mutex> lock(observer->mutex);
  REQUIRE(observer->tool_results[0].token != observer->tool_results[1].token);
  REQUIRE(observer->read_results[0].token == 303);
  REQUIRE(observer->read_results[0].result.text == "10:00");
  REQUIRE(observer->ping_results[0].token == 304);
  REQUIRE(service->GetServerIds().size() == 2);
  REQUIRE(service->GetToolsSnapshot().size() == 2);
  REQUIRE(service->GetResourcesSnapshot().size() == 2);
}

TEST_CASE(
  "MCP service coalesces a burst of update notifications into one follow-up reread", "[mcp][service][requests]"
) {
  McpServiceConfig config;
  config.thread_pool_size = 1;
  auto fixture = CreateReadyService(true, true, config);

  fixture.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  fixture.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  fixture.connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue));

  REQUIRE(fixture.service->SubscribeResourceAsync(401, "alpha", "resource://clock"));
  REQUIRE(fixture.service->SubscribeResourceAsync(402, "alpha", "resource://weather"));
  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->resource_subscription_events.size() == 2;
  }));

  // The only pool thread: the first read of the clock hangs, the weather queues up behind it, and the repeated read
  // of the clock comes last.
  fixture.connection->EnqueueSuccess(
    "resources/read", MakeReadResourcePayload("resource://clock", R"({"time":"10:00"})")
  );
  fixture.connection->EnqueueSuccess(
    "resources/read", MakeReadResourcePayload("resource://weather", R"({"sky":"sunny"})")
  );
  fixture.connection->EnqueueSuccess(
    "resources/read", MakeReadResourcePayload("resource://clock", R"({"time":"10:01"})")
  );
  Json::Value clock(Json::objectValue);
  clock["uri"] = "resource://clock";
  Json::Value weather(Json::objectValue);
  weather["uri"] = "resource://weather";
  {
    const BlockedMethod blocked_read(fixture.connection, "resources/read");
    fixture.connection->EmitNotification("notifications/resources/updated", clock);
    REQUIRE(WaitUntil([&]() {
      return CountReads(fixture.connection, "resource://clock") == 1;
    }));
    for (int i = 0; i < 10; ++i) {
      fixture.connection->EmitNotification("notifications/resources/updated", clock);
    }
    fixture.connection->EmitNotification("notifications/resources/updated", weather);
  }

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->resource_updates.size() == 3;
  }));
  REQUIRE(CountReads(fixture.connection, "resource://clock") == 2);
  REQUIRE(CountReads(fixture.connection, "resource://weather") == 1);

  REQUIRE(fixture.service->UnsubscribeResourceAsync(403, "alpha", "resource://clock"));
  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->resource_subscription_events.size() == 3;
  }));

  fixture.connection->EmitNotification("notifications/resources/updated", clock);
  REQUIRE(StaysFalseFor(
    [&]() {
      return CountReads(fixture.connection, "resource://clock") > 2;
    },
    std::chrono::milliseconds(100)
  ));

  std::lock_guard<std::mutex> lock(fixture.observer->mutex);
  REQUIRE(
    fixture.observer->resource_subscription_events[2].operation == McpResourceSubscriptionEvent::Operation::UNSUBSCRIBE
  );
  REQUIRE(fixture.observer->resource_updates.size() == 3);
  REQUIRE(fixture.observer->resource_updates.back().uri == "resource://clock");
  REQUIRE(fixture.observer->resource_updates.back().data["data"]["time"].asString() == "10:01");
}

TEST_CASE("MCP service refreshes the tool list of a server one request at a time", "[mcp][service][requests]") {
  McpServiceConfig config;
  config.thread_pool_size = 2;
  auto fixture = CreateReadyService(false, false, config);
  const auto tools_list_calls = [&fixture]() {
    const auto calls = fixture.connection->RequestCalls();
    return std::count_if(calls.begin(), calls.end(), [](const FakeConnection::RequestCall& call) {
      return call.method == "tools/list";
    });
  };
  fixture.connection->EnqueueSuccess("tools/list", MakeToolsList({"old"}));
  fixture.connection->EnqueueSuccess("tools/list", MakeToolsList({"new"}));
  {
    const BlockedMethod blocked(fixture.connection, "tools/list");
    fixture.connection->EmitNotification("notifications/tools/list_changed", Json::Value(Json::objectValue));
    REQUIRE(WaitUntil([&]() {
      return tools_list_calls() == 2;
    }));
    // A second notification during a refresh: a free thread must not start a parallel tools/list, otherwise an
    // earlier response could be committed over a fresh one.
    fixture.connection->EmitNotification("notifications/tools/list_changed", Json::Value(Json::objectValue));
    fixture.connection->EmitNotification("notifications/tools/list_changed", Json::Value(Json::objectValue));
    REQUIRE(StaysFalseFor(
      [&]() {
        return tools_list_calls() > 2;
      },
      std::chrono::milliseconds(100)
    ));
  }
  REQUIRE(WaitUntil([&]() {
    const auto tools = fixture.service->GetToolsSnapshot();
    return tools.size() == 1 && tools[0].tool_name == "new" && tools[0].title == "title-new";
  }));
  REQUIRE(tools_list_calls() == 3);
}

TEST_CASE("MCP service keeps request completions stable under interleaved operations", "[mcp][service][requests]") {
  auto fixture = CreateReadyService();

  Json::Value tool_payload(Json::objectValue);
  tool_payload["isError"] = false;
  tool_payload["content"] = Json::Value(Json::arrayValue);
  tool_payload["content"].append("ok");
  fixture.connection->EnqueueSuccess("tools/call", tool_payload, "tool-1");
  fixture.connection->EnqueueSuccess("tools/call", tool_payload, "tool-2");

  Json::Value read_payload(Json::objectValue);
  read_payload["contents"] = Json::Value(Json::arrayValue);
  Json::Value content(Json::objectValue);
  content["uri"] = "resource://clock";
  content["mimeType"] = "text/plain";
  content["text"] = "10:00";
  read_payload["contents"].append(content);
  fixture.connection->EnqueueSuccess("resources/read", read_payload, "read-1");
  fixture.connection->EnqueueSuccess("ping", Json::Value(Json::objectValue));
  fixture.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  fixture.connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue));

  REQUIRE(fixture.service->SubscribeResourceAsync(501, "alpha", "resource://clock"));
  REQUIRE(fixture.service->CallToolAsync(
    502,
    McpToolCallRequest{
      .call_id = "tool-1",
      .server_id = "alpha",
      .tool_name = "search",
    }
  ));
  REQUIRE(fixture.service->ReadResourceAsync(
    503,
    McpReadResourceRequest{
      .call_id = "read-1",
      .server_id = "alpha",
      .uri = "resource://clock",
    }
  ));
  REQUIRE(fixture.service->PingAsync(
    504,
    McpPingRequest{
      .call_id = "ping-1",
      .server_id = "alpha",
    }
  ));
  REQUIRE(fixture.service->CallToolAsync(
    505,
    McpToolCallRequest{
      .call_id = "tool-2",
      .server_id = "alpha",
      .tool_name = "search",
    }
  ));

  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->tool_results.size() == 2 && fixture.observer->read_results.size() == 1
      && fixture.observer->ping_results.size() == 1 && fixture.observer->resource_subscription_events.size() == 1;
  }));

  fixture.connection->EnqueueSuccess("resources/read", read_payload, "read-2");
  Json::Value update(Json::objectValue);
  update["uri"] = "resource://clock";
  fixture.connection->EmitNotification("notifications/resources/updated", update);
  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->resource_updates.size() == 1;
  }));
  REQUIRE(fixture.service->UnsubscribeResourceAsync(506, "alpha", "resource://clock"));
  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(fixture.observer->mutex);
    return fixture.observer->resource_subscription_events.size() == 2;
  }));

  std::lock_guard<std::mutex> lock(fixture.observer->mutex);
  // Tool calls are independent: with several pool threads the order of their completion is not defined.
  std::vector<McpRequestToken> tool_tokens;
  for (const auto& result : fixture.observer->tool_results) {
    tool_tokens.push_back(result.token);
  }
  std::sort(tool_tokens.begin(), tool_tokens.end());
  REQUIRE(tool_tokens == std::vector<McpRequestToken>{502, 505});
  REQUIRE(fixture.observer->read_results[0].token == 503);
  REQUIRE(fixture.observer->ping_results[0].token == 504);
  REQUIRE(fixture.observer->resource_updates.size() == 1);
}

namespace {

constexpr const char* kHouseUri = "house://devices/state";

Json::Value MakeResourcesListWithoutSubscribable(std::initializer_list<const char*> uris) {
  Json::Value payload(Json::objectValue);
  payload["resources"] = Json::Value(Json::arrayValue);
  for (const auto* uri : uris) {
    Json::Value item(Json::objectValue);
    item["uri"] = uri;
    item["name"] = uri;
    item["mimeType"] = "application/json";
    payload["resources"].append(item);
  }
  return payload;
}

McpServiceConfig HydrationConfig(uint32_t retry_initial_ms = 10, uint32_t retry_max_ms = 40) {
  return McpServiceConfig{
    .resource_hydration = {
      .read_resources_on_list = true,
      .subscribe_to_subscribable_resources = true,
      .retry_initial_ms = retry_initial_ms,
      .retry_max_ms = retry_max_ms,
    },
  };
}

void EnqueueServerStart(FakeConnection& connection, bool supports_subscriptions, Json::Value resources) {
  connection.EnqueueSuccess("initialize", MakeInitializePayload(true, supports_subscriptions));
  connection.EnqueueSuccess("tools/list", MakeToolsList({"search"}));
  connection.EnqueueSuccess("resources/list", std::move(resources));
  Json::Value templates(Json::objectValue);
  templates["resourceTemplates"] = Json::Value(Json::arrayValue);
  connection.EnqueueSuccess("resources/templates/list", std::move(templates));
}

void EnqueueHouse(FakeConnection& connection, int version) {
  connection.EnqueueSuccess(
    "resources/read", MakeReadResourcePayload(kHouseUri, R"({"version":)" + std::to_string(version) + "}")
  );
}

void EnqueueUnavailable(FakeConnection& connection) {
  connection.EnqueueError(
    "resources/read", ErrorCode::INTERNAL_ERROR, "HOME_VIEW_UNAVAILABLE", Json::Value(), "", -31901
  );
}

void EmitHouseUpdated(FakeConnection& connection) {
  Json::Value update(Json::objectValue);
  update["uri"] = kHouseUri;
  connection.EmitNotification("notifications/resources/updated", update);
}

void EnqueueResourcesRefresh(FakeConnection& connection, Json::Value resources) {
  connection.EnqueueSuccess("resources/list", std::move(resources));
  Json::Value templates(Json::objectValue);
  templates["resourceTemplates"] = Json::Value(Json::arrayValue);
  connection.EnqueueSuccess("resources/templates/list", std::move(templates));
  connection.EmitNotification("notifications/resources/list_changed", Json::Value(Json::objectValue));
}

size_t CountCalls(FakeConnection& connection, const std::string& method) {
  size_t count = 0;
  for (const auto& call : connection.RequestCalls()) {
    count += call.method == method;
  }
  return count;
}

size_t CountUpdates(CapturingMcpServiceObserver& observer) {
  std::lock_guard<std::mutex> lock(observer.mutex);
  return observer.resource_updates.size();
}

int LastHouseVersion(CapturingMcpServiceObserver& observer) {
  std::lock_guard<std::mutex> lock(observer.mutex);
  return observer.resource_updates.back().data["data"]["version"].asInt();
}

class ObservedThreadPool final : public giga_edge::IThreadPool {
public:
  explicit ObservedThreadPool(unsigned threads)
    : owner_(std::this_thread::get_id())
    , pool_(threads) {}
  ~ObservedThreadPool() override {
    Stop();
  }

  void Push(giga_edge::TaskPtr task) override {
    const bool retry = std::this_thread::get_id() != owner_ && active_pool_ != this;
    task->callback = [this, fn = std::move(task->callback)]() {
      active_pool_ = this;
      fn();
    };
    {
      std::lock_guard<std::mutex> lock(mutex_);
      tasks_.emplace_back(task, retry);
      if (retry && hold_retries_) {
        held_retries_.push_back(std::move(task));
        return;
      }
      if (!retry && hold_tasks_) {
        held_tasks_.push_back(std::move(task));
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
      for (const auto& task : held_retries_) {
        task->status = giga_edge::Task::Status::CANCEL;
      }
      held_retries_.clear();
      for (const auto& task : held_tasks_) {
        task->status = giga_edge::Task::Status::CANCEL;
      }
      held_tasks_.clear();
    }
    pool_.Stop();
  }
  void HoldRetries() {
    std::lock_guard<std::mutex> lock(mutex_);
    hold_retries_ = true;
  }
  void ReleaseRetries(bool keep_holding = false) {
    std::vector<giga_edge::TaskPtr> tasks;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      hold_retries_ = keep_holding;
      tasks.swap(held_retries_);
    }
    for (auto& task : tasks) {
      pool_.Push(std::move(task));
    }
  }

  // New tasks, except retries, wait for ReleaseHeldTask(s): the test sets the order of tasks for any number of
  // threads.
  void HoldTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    hold_tasks_ = true;
  }
  size_t HeldTaskCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return held_tasks_.size();
  }
  // Releases one task by its index in the queueing order; the others keep waiting.
  void ReleaseHeldTask(size_t index) {
    giga_edge::TaskPtr task;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      task = held_tasks_.at(index);
      held_tasks_.erase(held_tasks_.begin() + static_cast<std::ptrdiff_t>(index));
    }
    pool_.Push(std::move(task));
  }
  void ReleaseHeldTasks() {
    std::vector<giga_edge::TaskPtr> tasks;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      hold_tasks_ = false;
      tasks.swap(held_tasks_);
    }
    for (auto& task : tasks) {
      pool_.Push(std::move(task));
    }
  }

  size_t RetryCount(bool completed = false) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::count_if(tasks_.begin(), tasks_.end(), [&](const auto& entry) {
      return entry.second && (!completed || entry.first->status == giga_edge::Task::Status::DONE);
    });
  }
  bool Idle(size_t running = 0) const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t active = 0;
    for (const auto& [task, retry] : tasks_) {
      const auto status = task->status.load();
      if (status == giga_edge::Task::Status::IN_QUEUE) {
        return false;
      }
      active += status == giga_edge::Task::Status::RUNNING;
    }
    return active == running;
  }
private:
  inline static thread_local const ObservedThreadPool* active_pool_ = nullptr;
  const std::thread::id owner_;
  mutable std::mutex mutex_;
  std::vector<std::pair<giga_edge::TaskPtr, bool>> tasks_;
  bool hold_retries_ = false;
  std::vector<giga_edge::TaskPtr> held_retries_;
  bool hold_tasks_ = false;
  std::vector<giga_edge::TaskPtr> held_tasks_;
  giga_edge::ThreadPool pool_;
};

struct BlockedRequest {
  std::shared_ptr<FakeConnection> connection;
  std::string id;
  BlockedRequest(std::shared_ptr<FakeConnection> connection, std::string id)
    : connection(std::move(connection))
    , id(std::move(id)) {
    this->connection->BlockRequest(this->id);
  }
  ~BlockedRequest() {
    connection->UnblockRequest(id);
  }
};

struct HydrationFixture {
  explicit HydrationFixture(McpServiceConfig config = HydrationConfig())
    : pool(new ObservedThreadPool(config.thread_pool_size))
    , service(CreateMcpService(std::move(config), std::unique_ptr<giga_edge::IThreadPool>(pool)))
    , observer(std::make_shared<CapturingMcpServiceObserver>())
    , connection(std::make_shared<FakeConnection>()) {
    service->SetObserver(observer);
  }

  void WaitIdle(size_t running = 0) {
    REQUIRE(WaitUntil([&]() {
      return pool->Idle(running);
    }));
  }
  void WaitRetry(size_t count = 1, bool completed = false) {
    REQUIRE(WaitUntil([&]() {
      return pool->RetryCount(completed) >= count;
    }));
  }
  void NoMoreCalls(const std::string& method, size_t count, int milliseconds) {
    REQUIRE(StaysFalseFor(
      [&]() {
        return CountCalls(*connection, method) != count;
      },
      std::chrono::milliseconds(milliseconds)
    ));
  }

  ObservedThreadPool* pool;
  std::shared_ptr<IMcpService> service;
  std::shared_ptr<CapturingMcpServiceObserver> observer;
  std::shared_ptr<FakeConnection> connection;
};

// The MCP log goes to std::cerr; declare the capture before the service and read it when the service is quiet.
struct CapturedLog {
  std::ostringstream text;
  std::streambuf* previous = std::cerr.rdbuf(text.rdbuf());
  ~CapturedLog() {
    std::cerr.rdbuf(previous);
  }
};

// The "retry in N ms" pauses from the lines about a failed automatic read of the house.
std::vector<uint32_t> HouseReadRetryDelays(const std::string& log) {
  const std::string prefix = "[WARNING] MCP: auto readResource failed for 'house' uri '" + std::string(kHouseUri)
    + "': HOME_VIEW_UNAVAILABLE, retry in ";
  std::vector<uint32_t> delays;
  std::istringstream lines(log);
  for (std::string line; std::getline(lines, line);) {
    if (line.rfind(prefix, 0) == 0) {
      delays.push_back(static_cast<uint32_t>(std::stoul(line.substr(prefix.size()))));
    }
  }
  return delays;
}

} // namespace

TEST_CASE(
  "MCP service subscribes a resource without the subscribable field when the server supports subscriptions",
  "[mcp][service][requests][hydration]"
) {
  HydrationFixture f;
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  EnqueueHouse(*f.connection, 1);

  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 1;
  }));
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 1);

  EnqueueHouse(*f.connection, 2);
  EmitHouseUpdated(*f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 2;
  }));
  REQUIRE(LastHouseVersion(*f.observer) == 2);
}

TEST_CASE(
  "MCP service does not subscribe when the server lacks subscriptions or the resource refuses them",
  "[mcp][service][requests][hydration]"
) {
  const bool server_subscribes = GENERATE(false, true);
  CAPTURE(server_subscribes);
  HydrationFixture f;
  EnqueueServerStart(
    *f.connection,
    server_subscribes,
    server_subscribes ? MakeResourcesListWithSubscribable({{kHouseUri, false}})
                      : MakeResourcesListWithoutSubscribable({kHouseUri})
  );
  EnqueueHouse(*f.connection, 1);

  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 1;
  }));
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 0);
}

TEST_CASE(
  "MCP service retries the automatic read until the resource becomes available", "[mcp][service][requests][hydration]"
) {
  HydrationFixture f;
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  for (int i = 0; i < 3; ++i) {
    EnqueueUnavailable(*f.connection);
  }
  EnqueueHouse(*f.connection, 1);

  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 1;
  }));
  REQUIRE(CountCalls(*f.connection, "resources/read") == 4);

  f.NoMoreCalls("resources/read", 4, 120);
  REQUIRE(LastHouseVersion(*f.observer) == 1);
}

TEST_CASE("MCP service does not retry an error that a retry cannot fix", "[mcp][service][requests][hydration]") {
  HydrationFixture f;
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueError("resources/subscribe", ErrorCode::METHOD_NOT_FOUND, "no subscriptions");
  f.connection->EnqueueError("resources/read", ErrorCode::INVALID_PARAMS, "unknown resource");

  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/read") == 1;
  }));
  f.NoMoreCalls("resources/read", 1, 100);
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 1);
}

TEST_CASE("MCP service retries a failed read after an update notification", "[mcp][service][requests][hydration]") {
  HydrationFixture f;
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  EnqueueHouse(*f.connection, 1);

  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 1;
  }));

  f.connection->EnqueueError("resources/read", ErrorCode::TIMEOUT, "timeout");
  EnqueueHouse(*f.connection, 2);
  EmitHouseUpdated(*f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 2;
  }));
  REQUIRE(LastHouseVersion(*f.observer) == 2);
}

TEST_CASE(
  "MCP service retries a failed subscription and rereads what it may have missed", "[mcp][service][requests][hydration]"
) {
  HydrationFixture f;
  // The subscription retry comes after the read by the list: had it come earlier, its re-read would replace this
  // read.
  f.pool->HoldRetries();
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueError("resources/subscribe", ErrorCode::INTERNAL_ERROR, "not ready");
  EnqueueHouse(*f.connection, 1);
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  EnqueueHouse(*f.connection, 2);

  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 1;
  }));
  f.WaitRetry();
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 1);
  REQUIRE(LastHouseVersion(*f.observer) == 1);
  f.pool->ReleaseRetries();
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 2;
  }));
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 2);
  REQUIRE(LastHouseVersion(*f.observer) == 2);

  EnqueueHouse(*f.connection, 3);
  EmitHouseUpdated(*f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 3;
  }));
  REQUIRE(LastHouseVersion(*f.observer) == 3);
}

TEST_CASE("MCP service stops retrying a resource of a removed server", "[mcp][service][requests][hydration]") {
  HydrationFixture f(HydrationConfig(5, 5));
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));

  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/read") >= 3;
  }));
  f.service->RemoveServer("house");
  REQUIRE(WaitUntil([&]() {
    return f.connection->CloseCount() == 1;
  }));
  f.WaitIdle();
  const auto reads = CountCalls(*f.connection, "resources/read");
  f.NoMoreCalls("resources/read", reads, 60);
}

TEST_CASE("MCP service stops retrying a resource gone from the list", "[mcp][service][requests][hydration]") {
  HydrationFixture f(HydrationConfig(200, 200));
  f.pool->HoldRetries();
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  EnqueueUnavailable(*f.connection);

  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/read") == 1;
  }));
  f.WaitRetry();
  f.connection->EnqueueSuccess("resources/list", MakeResourcesListWithoutSubscribable({}));
  Json::Value templates(Json::objectValue);
  templates["resourceTemplates"] = Json::Value(Json::arrayValue);
  f.connection->EnqueueSuccess("resources/templates/list", templates);
  f.connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue));
  f.connection->EmitNotification("notifications/resources/list_changed", Json::Value(Json::objectValue));
  REQUIRE(WaitUntil([&]() {
    return f.service->GetResourcesSnapshot().empty();
  }));
  // The retry is removed together with the resource, not because its read missed.
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
  f.pool->ReleaseRetries();
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/read") == 1);
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
}

TEST_CASE("MCP service starts the retry pause anew for a new connection", "[mcp][service][requests][hydration]") {
  HydrationFixture f(HydrationConfig(20, 5000));
  f.pool->HoldRetries();
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  for (int i = 0; i < 5; ++i) {
    EnqueueUnavailable(*f.connection);
  }
  f.service->AddServer("house", f.connection);
  for (size_t retry = 1; retry <= 5; ++retry) {
    f.WaitRetry(retry);
    if (retry < 5) {
      f.pool->ReleaseRetries(true);
    }
  }
  REQUIRE(CountCalls(*f.connection, "resources/read") == 5);

  auto restarted = std::make_shared<FakeConnection>();
  EnqueueServerStart(*restarted, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  restarted->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  EnqueueUnavailable(*restarted);
  EnqueueHouse(*restarted, 1);
  f.service->AddServer("house", restarted);
  f.pool->ReleaseRetries();
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 1;
  }));
  REQUIRE(CountCalls(*restarted, "resources/read") == 2);
}

TEST_CASE("MCP service does not retry when retries are off", "[mcp][service][requests][hydration]") {
  HydrationFixture f(HydrationConfig(0, 0));
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));

  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/read") == 1;
  }));
  f.NoMoreCalls("resources/read", 1, 60);
}

TEST_CASE("MCP service coalesces a burst of update notifications", "[mcp][service][requests][hydration]") {
  HydrationFixture f;
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  EnqueueHouse(*f.connection, 1);

  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 1;
  }));

  BlockedMethod blocked_read(f.connection, "resources/read");
  EnqueueHouse(*f.connection, 2);
  EnqueueHouse(*f.connection, 3);
  EmitHouseUpdated(*f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/read") == 2;
  }));
  for (int i = 0; i < 5; ++i) {
    EmitHouseUpdated(*f.connection);
  }
  f.WaitIdle(1);
  REQUIRE(CountCalls(*f.connection, "resources/read") == 2);

  f.connection->UnblockMethod("resources/read");
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 3;
  }));
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/read") == 3);
  REQUIRE(CountUpdates(*f.observer) == 3);
  REQUIRE(LastHouseVersion(*f.observer) == 3);
}

TEST_CASE("MCP service keeps one pending retry per resource", "[mcp][service][requests][hydration]") {
  HydrationFixture f(HydrationConfig(60, 60));
  f.pool->HoldRetries();
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  EnqueueUnavailable(*f.connection);
  EnqueueUnavailable(*f.connection);
  EnqueueHouse(*f.connection, 1);

  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/read") == 1;
  }));
  f.WaitRetry();
  EmitHouseUpdated(*f.connection);
  f.WaitRetry(2);
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 1);
  f.pool->ReleaseRetries();
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 1;
  }));
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/read") == 3);
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
}

TEST_CASE(
  "MCP service drops a pending retry when the resource reads on its own", "[mcp][service][requests][hydration]"
) {
  HydrationFixture f(HydrationConfig(200, 200));
  f.pool->HoldRetries();
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  EnqueueUnavailable(*f.connection);

  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/read") == 1;
  }));
  f.WaitRetry();
  EnqueueHouse(*f.connection, 1);
  EmitHouseUpdated(*f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 1;
  }));
  REQUIRE(WaitUntil([&]() {
    return GetMcpResourceRetryCount(*f.service) == 0;
  }));
  f.pool->ReleaseRetries();
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/read") == 2);
}

TEST_CASE("MCP service shuts down promptly with a retry pending", "[mcp][service][requests][hydration]") {
  // The retry is queued in the pool or still waits out a pause longer than the RequireIsolated guard: the shutdown
  // neither waits for it nor sends it.
  const bool queued = GENERATE(true, false);
  RequireIsolated([&]() {
    const uint32_t pause_ms = queued ? 10 : 600000;
    HydrationFixture f(HydrationConfig(pause_ms, pause_ms));
    f.pool->HoldRetries();
    EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
    f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    EnqueueUnavailable(*f.connection);

    f.service->AddServer("house", f.connection);
    REQUIRE(WaitUntil([&]() {
      return GetMcpResourceRetryCount(*f.service) == 1;
    }));
    if (queued) {
      f.WaitRetry();
    }
    const auto started = std::chrono::steady_clock::now();
    f.service->Shutdown();
    REQUIRE(f.pool->RetryCount() == (queued ? 1u : 0u));
    f.service.reset();
    REQUIRE(std::chrono::steady_clock::now() - started < kWaitHangGuard);
  });
}

TEST_CASE(
  "MCP service rereads after a late subscription when it reads only on list", "[mcp][service][requests][hydration]"
) {
  auto config = HydrationConfig();
  HydrationFixture f(std::move(config));
  f.pool->HoldRetries();
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueError("resources/subscribe", ErrorCode::INTERNAL_ERROR, "not ready");
  EnqueueHouse(*f.connection, 1);
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  EnqueueHouse(*f.connection, 2);

  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 1;
  }));
  f.WaitRetry();
  f.pool->ReleaseRetries();
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 2;
  }));
  REQUIRE(LastHouseVersion(*f.observer) == 2);
}

TEST_CASE(
  "MCP service rereads any resource after a late subscription, not only the preferred one",
  "[mcp][service][requests][hydration]"
) {
  constexpr const char* kRoomsUri = "house://rooms/state";
  auto config = HydrationConfig();
  config.resource_hydration.preferred_read_resource_match_phrases = {kHouseUri};
  HydrationFixture f(std::move(config));
  f.pool->HoldRetries();
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri, kRoomsUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  f.connection->EnqueueError("resources/subscribe", ErrorCode::INTERNAL_ERROR, "not ready");
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  EnqueueHouse(*f.connection, 1);
  f.connection->EnqueueSuccess("resources/read", MakeReadResourcePayload(kRoomsUri, R"({"rooms":1})"));

  f.service->AddServer("house", f.connection);
  f.WaitRetry();
  REQUIRE(CountReads(f.connection, kHouseUri) == 1);
  REQUIRE(CountReads(f.connection, kRoomsUri) == 0);
  f.pool->ReleaseRetries();
  REQUIRE(WaitUntil([&]() {
    return CountReads(f.connection, kRoomsUri) == 1;
  }));
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 3);
  REQUIRE(CountUpdates(*f.observer) == 2);
}

TEST_CASE(
  "MCP service drops a pending retry after an error a retry cannot fix", "[mcp][service][requests][hydration]"
) {
  HydrationFixture f(HydrationConfig(300, 300));
  f.pool->HoldRetries();
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  EnqueueUnavailable(*f.connection);
  f.connection->EnqueueError("resources/read", ErrorCode::INVALID_PARAMS, "unknown resource");

  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/read") == 1;
  }));
  f.WaitRetry();
  EmitHouseUpdated(*f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/read") == 2;
  }));
  REQUIRE(WaitUntil([&]() {
    return GetMcpResourceRetryCount(*f.service) == 0;
  }));
  f.pool->ReleaseRetries();
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/read") == 2);
}

TEST_CASE(
  "MCP service rereads after list refresh restores a failed subscription before its timer",
  "[mcp][service][requests][hydration][hydration_races]"
) {
  const auto mode = GENERATE(0, 1, 2);
  const bool reads_on_list = mode != 0;
  const bool preferred_uri = mode == 2;
  const auto retry_ms = GENERATE(0u, 10u);
  CAPTURE(reads_on_list, preferred_uri, retry_ms);
  auto config = HydrationConfig(retry_ms, retry_ms);
  config.resource_hydration.read_resources_on_list = reads_on_list;
  if (preferred_uri) {
    config.resource_hydration.preferred_read_resource_match_phrases = {kHouseUri};
  }
  HydrationFixture f(std::move(config));
  f.pool->HoldRetries();
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueError("resources/subscribe", ErrorCode::INTERNAL_ERROR, "not ready");
  if (reads_on_list) {
    EnqueueHouse(*f.connection, 1);
  }
  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/subscribe") == 1
      && CountUpdates(*f.observer) == static_cast<size_t>(reads_on_list);
  }));

  if (retry_ms != 0) {
    f.WaitRetry();
  }
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  EnqueueHouse(*f.connection, 2);
  EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri}));
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 1 + static_cast<size_t>(reads_on_list);
  }));
  f.pool->ReleaseRetries();
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 2);
  REQUIRE(CountCalls(*f.connection, "resources/read") == 1 + static_cast<size_t>(reads_on_list));
  REQUIRE(LastHouseVersion(*f.observer) == 2);
}

TEST_CASE(
  "MCP service cancels a dispatched retry after the current read succeeds or fails permanently",
  "[mcp][service][requests][hydration][hydration_races]"
) {
  const auto threads = GENERATE(1u, 4u);
  const bool permanent_error = GENERATE(false, true);
  CAPTURE(threads, permanent_error);
  auto config = HydrationConfig(20, 20);
  config.thread_pool_size = threads;
  HydrationFixture f(std::move(config));
  f.pool->HoldRetries();
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  EnqueueUnavailable(*f.connection);
  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/read") == 1;
  }));
  f.WaitRetry();

  BlockedMethod blocked(f.connection, "resources/read");
  if (permanent_error) {
    f.connection->EnqueueError("resources/read", ErrorCode::INVALID_PARAMS, "unknown resource");
  } else {
    EnqueueHouse(*f.connection, 1);
  }
  EnqueueHouse(*f.connection, 2);
  EmitHouseUpdated(*f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/read") == 2;
  }));
  f.pool->ReleaseRetries();
  if (threads > 1) {
    f.WaitRetry(1, true);
  }
  REQUIRE(CountCalls(*f.connection, "resources/read") == 2);
  f.connection->UnblockMethod("resources/read");
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/read") == 2);
  REQUIRE(CountUpdates(*f.observer) == (permanent_error ? 0 : 1));

  EmitHouseUpdated(*f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == (permanent_error ? 1 : 2);
  }));
  REQUIRE(LastHouseVersion(*f.observer) == 2);
}

TEST_CASE(
  "MCP service serializes list hydration and notifications with multiple workers and publishes in read order",
  "[mcp][service][requests][hydration][hydration_races]"
) {
  RequireIsolated([&]() {
    auto config = HydrationConfig();
    config.thread_pool_size = 4;
    config.resource_hydration.preferred_read_resource_match_phrases = {kHouseUri};
    HydrationFixture f(std::move(config));
    EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
    f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    EnqueueHouse(*f.connection, 1);
    EnqueueHouse(*f.connection, 2);
    BlockedMethod blocked_read(f.connection, "resources/read");
    f.service->AddServer("house", f.connection);
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/read") == 1;
    }));

    BlockedMethod blocked_subscribe(f.connection, "resources/subscribe");
    f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri, "house://marker"}));
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/subscribe") == 2;
    }));
    for (int i = 0; i < 10; ++i) {
      EmitHouseUpdated(*f.connection);
    }
    f.WaitIdle(2);
    REQUIRE(CountCalls(*f.connection, "resources/read") == 1);
    REQUIRE(CountUpdates(*f.observer) == 0);

    f.connection->UnblockMethod("resources/subscribe");
    f.connection->UnblockMethod("resources/read");
    REQUIRE(WaitUntil([&]() {
      return CountUpdates(*f.observer) == 2;
    }));
    f.service->Shutdown();
    REQUIRE(CountCalls(*f.connection, "resources/read") == 2);
    std::lock_guard<std::mutex> lock(f.observer->mutex);
    REQUIRE(f.observer->resource_updates[0].data["data"]["version"].asInt() == 1);
    REQUIRE(f.observer->resource_updates[1].data["data"]["version"].asInt() == 2);
  });
}

TEST_CASE(
  "MCP service preserves an update coalesced with a cancelled retry",
  "[mcp][service][requests][hydration][hydration_races]"
) {
  const bool notify_before_retry = GENERATE(false, true);
  RequireIsolated([&]() {
    CAPTURE(notify_before_retry);
    auto config = HydrationConfig(50, 50);
    config.thread_pool_size = 4;
    HydrationFixture f(std::move(config));
    f.pool->HoldRetries();
    EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
    f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    EnqueueUnavailable(*f.connection);
    f.service->AddServer("house", f.connection);
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/read") == 1;
    }));
    f.WaitRetry();

    BlockedMethod blocked(f.connection, "resources/read");
    EnqueueHouse(*f.connection, 1);
    EnqueueHouse(*f.connection, 2);
    EmitHouseUpdated(*f.connection);
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/read") == 2;
    }));
    if (notify_before_retry) {
      EmitHouseUpdated(*f.connection);
    }
    f.pool->ReleaseRetries();
    f.WaitRetry(1, true);
    if (!notify_before_retry) {
      EmitHouseUpdated(*f.connection);
    }
    f.connection->UnblockMethod("resources/read");
    REQUIRE(WaitUntil([&]() {
      return CountUpdates(*f.observer) == 2;
    }));
    f.service->Shutdown();
    REQUIRE(CountCalls(*f.connection, "resources/read") == 3);
    REQUIRE(LastHouseVersion(*f.observer) == 2);
  });
}

TEST_CASE("MCP service uses the configured permanent error codes", "[mcp][service][requests][hydration]") {
  const bool permanent_error = GENERATE(false, true);
  CAPTURE(permanent_error);
  auto config = HydrationConfig();
  config.resource_hydration.no_retry_error_codes = {-31901};
  HydrationFixture f(std::move(config));
  EnqueueServerStart(*f.connection, false, MakeResourcesListWithoutSubscribable({kHouseUri}));
  if (permanent_error) {
    f.connection->EnqueueError(
      "resources/read", ErrorCode::SERVER_ERROR, "HOME_VIEW_UNAVAILABLE", Json::Value(Json::nullValue), "read", -31901
    );
  } else {
    f.connection->EnqueueError(
      "resources/read", ErrorCode::INVALID_PARAMS, "unavailable", Json::Value(Json::nullValue), "read", -32602
    );
  }
  EnqueueHouse(*f.connection, 1);
  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/read") >= 1;
  }));
  if (!permanent_error) {
    REQUIRE(WaitUntil([&]() {
      return CountUpdates(*f.observer) == 1;
    }));
  }
  f.NoMoreCalls("resources/read", permanent_error ? 1 : 2, 80);
}

TEST_CASE("MCP retry delay doubles, caps, resets and cannot overflow", "[mcp][hydration][retry_delay]") {
  using giga_edge::mcp::detail::NextResourceRetryDelay;
  uint32_t delay = 0;
  for (const auto expected : {30u, 60u, 90u, 90u, 90u}) {
    delay = NextResourceRetryDelay(delay, 30, 90);
    REQUIRE(delay == expected);
  }
  delay = 0;
  REQUIRE(NextResourceRetryDelay(delay, 30, 90) == 30);
  REQUIRE(NextResourceRetryDelay(30, 30, 10) == 30);
  REQUIRE(NextResourceRetryDelay(30, 0, 90) == 0);
  const auto maximum = std::numeric_limits<uint32_t>::max();
  REQUIRE(NextResourceRetryDelay(maximum / 2 + 1, 1, maximum) == maximum);
  REQUIRE(NextResourceRetryDelay(maximum, 1, maximum) == maximum);
  REQUIRE(NextResourceRetryDelay(0, maximum, maximum) == maximum);
}

TEST_CASE(
  "MCP service retries reads in order and starts a new series after success",
  "[mcp][service][hydration][hydration_timing]"
) {
  CapturedLog log;
  HydrationFixture f(HydrationConfig(10, 40));
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  for (int i = 0; i < 7; ++i) {
    EnqueueUnavailable(*f.connection);
  }
  EnqueueHouse(*f.connection, 1);
  const auto await_updates = [&](size_t count) {
    REQUIRE(WaitUntil([&]() {
      return CountUpdates(*f.observer) == count;
    }));
  };
  f.service->AddServer("house", f.connection);
  await_updates(1);
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/read") == 8);
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
  EnqueueUnavailable(*f.connection);
  EnqueueHouse(*f.connection, 2);
  EmitHouseUpdated(*f.connection);
  await_updates(2);
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/read") == 10);
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
  // The pause before a retry is counted from the end of the failed read: 10, 20, 40...; after a success a new series
  // starts from 10.
  const std::vector<int> pauses_ms{0, 10, 20, 40, 40, 40, 40, 40, 0, 10};
  auto completed = std::chrono::steady_clock::time_point{};
  size_t read = 0;
  for (const auto& call : f.connection->RequestCalls()) {
    if (call.method == "resources/read") {
      CAPTURE(read);
      REQUIRE(call.params["uri"].asString() == kHouseUri);
      REQUIRE(call.started_at >= completed + std::chrono::milliseconds(pauses_ms.at(read)));
      completed = call.completed_at;
      ++read;
    }
  }
  REQUIRE(read == pauses_ms.size());
  REQUIRE(LastHouseVersion(*f.observer) == 2);
  // The failed read writes the line after the retry is scheduled, so neighboring lines may swap.
  auto delays = HouseReadRetryDelays(log.text.str());
  std::sort(delays.begin(), delays.end());
  REQUIRE(delays == std::vector<uint32_t>{10, 10, 20, 40, 40, 40, 40, 40});
}

TEST_CASE(
  "MCP service keeps the pending retry pause for a read that fails before the retry",
  "[mcp][service][hydration][hydration_timing]"
) {
  CapturedLog log;
  std::vector<FakeConnection::RequestCall> reads;
  {
    HydrationFixture f(HydrationConfig(1000, 5000));
    EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
    f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    EnqueueUnavailable(*f.connection);
    f.service->AddServer("house", f.connection);
    REQUIRE(WaitUntil([&]() {
      return GetMcpResourceRetryCount(*f.service) == 1;
    }));
    // The read by the notification fails while the retry still waits for its deadline: the deadline stays the same,
    // the pause is not doubled.
    EnqueueUnavailable(*f.connection);
    EnqueueHouse(*f.connection, 1);
    EmitHouseUpdated(*f.connection);
    REQUIRE(WaitUntil([&]() {
      return CountUpdates(*f.observer) == 1;
    }));
    f.WaitIdle();
    for (const auto& call : f.connection->RequestCalls()) {
      if (call.method == "resources/read") {
        reads.push_back(call);
      }
    }
  }
  REQUIRE(reads.size() == 3);
  REQUIRE(reads[2].started_at >= reads[0].completed_at + std::chrono::milliseconds(1000));

  // The second failure reports the rest of the same pause; a doubled one would give 2000 ms.
  const auto delays = HouseReadRetryDelays(log.text.str());
  REQUIRE(delays.size() == 2);
  REQUIRE(delays[0] == 1000);
  REQUIRE(delays[1] > 0);
  REQUIRE(delays[1] <= 1000);
}

TEST_CASE(
  "MCP service reads the preferred aggregate before devices even when it is listed last",
  "[mcp][service][hydration][hydration_races][review4]"
) {
  auto config = HydrationConfig();
  config.resource_hydration.preferred_read_resource_match_phrases = {kHouseUri};
  HydrationFixture f(std::move(config));
  EnqueueServerStart(
    *f.connection,
    true,
    MakeResourcesListWithSubscribable({{"house://device1", true}, {"house://device2", true}, {kHouseUri, false}})
  );
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  EnqueueHouse(*f.connection, 1);
  BlockedMethod blocked(f.connection, "resources/subscribe");
  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/subscribe") == 1;
  }));
  // The read and its update run in parallel with the subscription, not before it.
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/read") == 1 && CountUpdates(*f.observer) == 1;
  }));
  REQUIRE(LastHouseVersion(*f.observer) == 1);
  f.connection->UnblockMethod("resources/subscribe");
  f.WaitIdle();
  std::vector<std::string> subscriptions;
  for (const auto& call : f.connection->RequestCalls()) {
    if (call.method == "resources/subscribe") {
      subscriptions.push_back(call.params["uri"].asString());
    }
  }
  REQUIRE(subscriptions == std::vector<std::string>{"house://device1", "house://device2"});
}

TEST_CASE(
  "MCP service cancels a subscription retry after the resource opts out", "[mcp][service][hydration][hydration_races]"
) {
  const auto threads = GENERATE(1u, 4u);
  auto config = HydrationConfig(150, 150);
  config.thread_pool_size = threads;
  config.resource_hydration.read_resources_on_list = false;
  HydrationFixture f(std::move(config));
  f.pool->HoldRetries();
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueError("resources/subscribe", ErrorCode::INTERNAL_ERROR, "unavailable");
  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/subscribe") == 1;
  }));
  f.WaitRetry();
  EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithSubscribable({{kHouseUri, false}}));
  REQUIRE(WaitUntil([&]() {
    const auto resources = f.service->GetResourcesSnapshot();
    return resources.size() == 1 && !resources.front().subscribable;
  }));
  f.pool->ReleaseRetries();
  f.WaitIdle();
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 1);
}

TEST_CASE(
  "MCP service keeps successful retry hydration on an unchanged list", "[mcp][service][hydration][hydration_races]"
) {
  const auto threads = GENERATE(1u, 4u);
  auto config = HydrationConfig();
  config.thread_pool_size = threads;
  HydrationFixture f(std::move(config));
  EnqueueServerStart(*f.connection, false, MakeResourcesListWithoutSubscribable({kHouseUri}));
  EnqueueUnavailable(*f.connection);
  EnqueueHouse(*f.connection, 1);
  EnqueueHouse(*f.connection, 2);
  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 1;
  }));
  EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri}));
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/templates/list") == 2;
  }));
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/read") == 2);
  REQUIRE(CountUpdates(*f.observer) == 1);
}

TEST_CASE(
  "MCP service serializes list recovery with a dispatched subscription retry",
  "[mcp][service][hydration][hydration_races]"
) {
  const auto threads = GENERATE(1u, 4u);
  const bool permanent_error = GENERATE(false, true);
  CAPTURE(threads, permanent_error);
  auto config = HydrationConfig(60, 60);
  config.thread_pool_size = threads;
  config.resource_hydration.read_resources_on_list = false;
  HydrationFixture f(std::move(config));
  f.pool->HoldRetries();
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueError("resources/subscribe", ErrorCode::INTERNAL_ERROR, "unavailable");
  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/subscribe") == 1;
  }));
  f.WaitRetry();
  BlockedMethod blocked(f.connection, "resources/subscribe");
  if (permanent_error) {
    f.connection->EnqueueError("resources/subscribe", ErrorCode::INVALID_PARAMS, "cannot subscribe");
  } else {
    f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  }
  EnqueueHouse(*f.connection, 1);
  EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri}));
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/subscribe") == 2;
  }));
  f.pool->ReleaseRetries();
  if (threads > 1) {
    f.WaitRetry(1, true);
  }
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 2);
  f.connection->UnblockMethod("resources/subscribe");
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 2);
  REQUIRE(CountUpdates(*f.observer) == (permanent_error ? 0 : 1));
}

TEST_CASE(
  "MCP service shuts down during resource retries with running or queued work",
  "[mcp][service][hydration][hydration_races]"
) {
  const auto threads = GENERATE(1u, 4u);
  const bool subscribe = GENERATE(false, true);
  const bool occupied_pool = GENERATE(false, true);
  RequireIsolated([&]() {
    CAPTURE(threads, subscribe, occupied_pool);
    auto config = HydrationConfig(60, 60);
    config.thread_pool_size = threads;
    config.resource_hydration.read_resources_on_list = !subscribe;
    config.resource_hydration.subscribe_to_subscribable_resources = subscribe;
    HydrationFixture f(std::move(config));
    const std::string method = subscribe ? "resources/subscribe" : "resources/read";
    f.pool->HoldRetries();
    EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
    f.connection->EnqueueError(method, ErrorCode::INTERNAL_ERROR, "unavailable");
    f.service->AddServer("house", f.connection);
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, method) == 1;
    }));
    f.WaitRetry();
    const std::string blocked_method = occupied_pool ? "ping" : method;
    BlockedMethod blocked(f.connection, blocked_method);
    if (occupied_pool) {
      for (uint32_t i = 0; i < threads; ++i) {
        f.connection->EnqueueSuccess("ping", Json::Value(Json::objectValue));
        REQUIRE(f.service->PingAsync(i + 1, {.call_id = std::to_string(i), .server_id = "house"}));
      }
    } else {
      f.connection->EnqueueError(method, ErrorCode::TIMEOUT, "retry timed out");
    }
    if (!occupied_pool) {
      f.pool->ReleaseRetries();
    }
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, blocked_method) == (occupied_pool ? threads : 2);
    }));
    if (occupied_pool) {
      f.pool->ReleaseRetries();
    }
    REQUIRE(GetMcpResourceRetryCount(*f.service) == 1);
    struct ShutdownTask {
      std::shared_ptr<FakeConnection> connection;
      std::string method;
      std::future<void> task;
      ~ShutdownTask() {
        connection->UnblockMethod(method);
        if (task.valid()) {
          task.wait();
        }
      }
    };
    // A separate statement: a lambda in the declaration together with a local class breaks MSVC (C2011).
    ShutdownTask shutdown{f.connection, blocked_method, std::async(std::launch::async, [&]() {
                            f.service->Shutdown();
                          })};
    // Free the pool only after Shutdown has removed the retry: otherwise the retry rightly runs before the stop.
    REQUIRE(WaitUntil([&]() {
      return GetMcpResourceRetryCount(*f.service) == 0;
    }));
    if (occupied_pool) {
      // Shutdown completes the accepted pings by itself, aborting their waits instead of waiting for the server
      // response.
      REQUIRE(shutdown.task.wait_for(kWaitHangGuard) == std::future_status::ready);
    } else {
      // Stop still waits for an internal request of the service.
      REQUIRE(shutdown.task.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
      f.connection->UnblockMethod(blocked_method);
      REQUIRE(shutdown.task.wait_for(kWaitHangGuard) == std::future_status::ready);
    }
    shutdown.task.get();
    REQUIRE(CountCalls(*f.connection, method) == (occupied_pool ? 1 : 2));
    REQUIRE(f.connection->CloseCount() == 1);
  });
}

TEST_CASE(
  "MCP service discards a removed resource's late read and hydrates its next incarnation",
  "[mcp][service][hydration][hydration_races][resource_generation]"
) {
  const auto threads = GENERATE(2u, 4u);
  const bool reappear_before_completion = GENERATE(false, true);
  RequireIsolated([&]() {
    CAPTURE(threads, reappear_before_completion);
    auto config = HydrationConfig();
    config.thread_pool_size = threads;
    HydrationFixture f(std::move(config));
    EnqueueServerStart(*f.connection, false, MakeResourcesListWithoutSubscribable({kHouseUri}));
    EnqueueHouse(*f.connection, 1);
    EnqueueHouse(*f.connection, 2);
    BlockedMethod blocked(f.connection, "resources/read");
    f.service->AddServer("house", f.connection);
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/read") == 1;
    }));
    EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({}));
    REQUIRE(WaitUntil([&]() {
      std::lock_guard<std::mutex> lock(f.observer->mutex);
      return f.observer->resource_snapshots.size() == 2 && f.observer->resource_snapshots.back().empty();
    }));
    if (reappear_before_completion) {
      EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri}));
      REQUIRE(WaitUntil([&]() {
        std::lock_guard<std::mutex> lock(f.observer->mutex);
        return f.observer->resource_snapshots.size() == 3;
      }));
      REQUIRE(CountCalls(*f.connection, "resources/read") == 1);
    }
    f.connection->UnblockMethod("resources/read");
    if (!reappear_before_completion) {
      BlockedMethod completed_reads(f.connection, "ping");
      for (unsigned i = 0; i < threads; ++i) {
        f.connection->EnqueueSuccess("ping", Json::Value(Json::objectValue));
        REQUIRE(
          f.service->PingAsync(100 + i, {.call_id = "old-read-finished-" + std::to_string(i), .server_id = "house"})
        );
      }
      REQUIRE(WaitUntil([&]() {
        return CountCalls(*f.connection, "ping") == threads;
      }));
      REQUIRE(CountUpdates(*f.observer) == 0);
      EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri}));
    }
    REQUIRE(WaitUntil([&]() {
      return CountUpdates(*f.observer) == 1;
    }));
    f.service->Shutdown();
    REQUIRE(CountCalls(*f.connection, "resources/read") == 2);
    REQUIRE(CountUpdates(*f.observer) == 1);
    REQUIRE(LastHouseVersion(*f.observer) == 2);
    std::vector<FakeConnection::RequestCall> reads;
    for (const auto& call : f.connection->RequestCalls()) {
      if (call.method == "resources/read") {
        reads.push_back(call);
      }
    }
    REQUIRE(reads[1].started_at >= reads[0].completed_at);
  });
}

TEST_CASE(
  "MCP service removes a resource between dispatching its retry and executing the read",
  "[mcp][service][hydration][hydration_races][resource_generation]"
) {
  HydrationFixture f(HydrationConfig(50, 50));
  f.pool->HoldRetries();
  EnqueueServerStart(*f.connection, false, MakeResourcesListWithoutSubscribable({kHouseUri}));
  EnqueueUnavailable(*f.connection);
  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/read") == 1;
  }));
  f.WaitRetry();
  // The retry has already put the read into the pool, but the read waits; the list refresh removes the resource
  // before the read starts.
  f.pool->HoldTasks();
  f.pool->ReleaseRetries();
  REQUIRE(WaitUntil([&]() {
    return f.pool->HeldTaskCount() == 1;
  }));
  EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({}));
  REQUIRE(f.pool->HeldTaskCount() == 2);
  f.pool->ReleaseHeldTask(1);
  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(f.observer->mutex);
    return f.observer->resource_snapshots.size() == 2 && f.observer->resource_snapshots.back().empty();
  }));
  f.pool->ReleaseHeldTasks();
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/read") == 1);
  REQUIRE(CountUpdates(*f.observer) == 0);

  EnqueueHouse(*f.connection, 2);
  EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri}));
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 1;
  }));
  REQUIRE(LastHouseVersion(*f.observer) == 2);
}

TEST_CASE(
  "MCP service replaces an obsolete hydration continuation after the resource list changes",
  "[mcp][service][hydration][hydration_races][resource_generation]"
) {
  RequireIsolated([&]() {
    HydrationFixture f;
    EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri, "house://removed"}));
    f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    EnqueueHouse(*f.connection, 1);
    f.connection->EnqueueSuccess("resources/read", MakeReadResourcePayload("house://added", R"({"version":2})"));
    BlockedMethod blocked(f.connection, "resources/subscribe");
    f.service->AddServer("house", f.connection);
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/subscribe") == 1;
    }));
    // The list refresh and the continuation of the old chain (the read of the house, the step over the removed
    // resource) are queued in the pool; the list changes first, and the revision must cut off the step of the old
    // chain for any number of threads.
    f.pool->HoldTasks();
    EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri, "house://added"}));
    f.connection->UnblockMethod("resources/subscribe");
    REQUIRE(WaitUntil([&]() {
      return f.pool->HeldTaskCount() == 3;
    }));
    f.pool->ReleaseHeldTask(0);
    REQUIRE(WaitUntil([&]() {
      return f.service->GetResourcesSnapshot().size() == 2 && f.pool->HeldTaskCount() == 3;
    }));
    f.pool->ReleaseHeldTasks();
    REQUIRE(WaitUntil([&]() {
      return CountUpdates(*f.observer) == 2;
    }));
    f.WaitIdle();
    f.service->Shutdown();
    REQUIRE(CountCalls(*f.connection, "resources/read") == 2);
    REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 2);
    for (const auto& call : f.connection->RequestCalls()) {
      if (call.method == "resources/read" || call.method == "resources/subscribe") {
        REQUIRE(call.params["uri"].asString() != "house://removed");
      }
    }
    // Updates of different resources are independent: the order of their publication is not defined.
    std::lock_guard<std::mutex> lock(f.observer->mutex);
    std::set<std::string> updated;
    for (const auto& update : f.observer->resource_updates) {
      updated.insert(update.uri);
    }
    REQUIRE(updated == std::set<std::string>{kHouseUri, "house://added"});
  });
}

TEST_CASE(
  "MCP service stops a stale hydration chain while the resource list refresh is in flight",
  "[mcp][service][hydration][hydration_races][resource_generation]"
) {
  RequireIsolated([&]() {
    // The second thread is needed so that the list refresh runs while the hydration chain is stuck in the subscription.
    auto config = HydrationConfig();
    config.thread_pool_size = 2;
    HydrationFixture f(std::move(config));
    EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri, "house://removed"}));
    f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    EnqueueHouse(*f.connection, 1);
    f.connection->EnqueueSuccess("resources/read", MakeReadResourcePayload("house://added", R"({"version":2})"));
    BlockedMethod blocked_subscribe(f.connection, "resources/subscribe");
    f.service->AddServer("house", f.connection);
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/subscribe") == 1;
    }));

    // The list refresh hangs in resources/list: the new list is not committed yet.
    f.connection->BlockMethod("resources/list");
    EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri, "house://added"}));
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/list") == 2;
    }));
    f.connection->UnblockMethod("resources/subscribe");
    f.WaitIdle(1);
    f.connection->UnblockMethod("resources/list");
    REQUIRE(WaitUntil([&]() {
      return CountUpdates(*f.observer) == 2;
    }));
    f.WaitIdle();
    REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 2);
    REQUIRE(CountCalls(*f.connection, "resources/read") == 2);
    for (const auto& call : f.connection->RequestCalls()) {
      if (call.method == "resources/read" || call.method == "resources/subscribe") {
        REQUIRE(call.params["uri"].asString() != "house://removed");
      }
    }
  });
}

TEST_CASE(
  "MCP service resumes hydration after a failed resource list refresh",
  "[mcp][service][hydration][hydration_races][resource_generation]"
) {
  RequireIsolated([&]() {
    auto config = HydrationConfig();
    config.thread_pool_size = 2;
    HydrationFixture f(std::move(config));
    EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri, "house://rooms"}));
    f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    EnqueueHouse(*f.connection, 1);
    f.connection->EnqueueSuccess("resources/read", MakeReadResourcePayload("house://rooms", R"({"rooms":1})"));
    BlockedMethod blocked_subscribe(f.connection, "resources/subscribe");
    f.service->AddServer("house", f.connection);
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/subscribe") == 1;
    }));

    // The list refresh hangs and then fails: the chain stopped for the time of the refresh must continue.
    f.connection->EnqueueError("resources/list", ErrorCode::TIMEOUT, "refresh failed");
    f.connection->BlockMethod("resources/list");
    f.connection->EmitNotification("notifications/resources/list_changed", Json::Value(Json::objectValue));
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/list") == 2;
    }));
    f.connection->UnblockMethod("resources/subscribe");
    f.WaitIdle(1);
    REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 1);
    f.connection->UnblockMethod("resources/list");
    REQUIRE(WaitUntil([&]() {
      return CountUpdates(*f.observer) == 2;
    }));
    f.WaitIdle();
    REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 2);
    REQUIRE(CountCalls(*f.connection, "resources/read") == 2);
  });
}

TEST_CASE(
  "MCP service reconciles a late subscription with resource removal and reappearance",
  "[mcp][service][hydration][hydration_races][review4][subscription_lifecycle]"
) {
  const bool reappear = GENERATE(false, true);
  CAPTURE(reappear);
  auto config = HydrationConfig();
  config.thread_pool_size = 4;
  config.resource_hydration.read_resources_on_list = false;
  HydrationFixture f(std::move(config));
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue), "old-subscription");
  BlockedRequest blocked(f.connection, "old-subscription");
  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/subscribe") == 1;
  }));
  f.WaitIdle(1);
  EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({}));
  f.WaitIdle(1);
  REQUIRE(f.service->GetResourcesSnapshot().empty());
  if (reappear) {
    EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri}));
    f.WaitIdle(1);
    REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 1);
  }
  if (reappear) {
    EnqueueHouse(*f.connection, 1);
  }
  f.connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue));
  f.connection->UnblockRequest("old-subscription");
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/unsubscribe") == static_cast<size_t>(!reappear));
  REQUIRE(CountUpdates(*f.observer) == static_cast<size_t>(reappear));
  EnqueueHouse(*f.connection, 2);
  Json::Value update;
  update["uri"] = kHouseUri;
  update["version"] = 2;
  f.connection->EmitNotification("notifications/resources/updated", update);
  f.WaitIdle();
  REQUIRE(CountUpdates(*f.observer) == 2 * static_cast<size_t>(reappear));
  REQUIRE(CountCalls(*f.connection, "resources/read") == (reappear ? 2 : 0));
  if (reappear) {
    std::lock_guard<std::mutex> lock(f.observer->mutex);
    const auto& event = f.observer->resource_updates.back();
    REQUIRE(event.uri == kHouseUri);
    REQUIRE(event.data["data"]["version"].asInt() == 2);
  }
}

TEST_CASE(
  "MCP service keeps an explicit subscription across resource removal and reappearance",
  "[mcp][service][hydration][hydration_races][review4]"
) {
  auto config = HydrationConfig();
  config.resource_hydration.read_resources_on_list = false;
  config.resource_hydration.subscribe_to_subscribable_resources = false;
  HydrationFixture f(std::move(config));
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.service->AddServer("house", f.connection);
  f.WaitIdle();
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
  f.WaitIdle();
  EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({}));
  f.WaitIdle();
  REQUIRE(f.service->GetResourcesSnapshot().empty());
  EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.WaitIdle();
  REQUIRE(f.service->SubscribeResourceAsync(2, "house", kHouseUri));
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 1);
  EnqueueHouse(*f.connection, 3);
  EmitHouseUpdated(*f.connection);
  f.WaitIdle();
  REQUIRE(CountUpdates(*f.observer) == 1);
  REQUIRE(CountCalls(*f.connection, "resources/read") == 1);
  REQUIRE(LastHouseVersion(*f.observer) == 3);
}

TEST_CASE(
  "MCP service retries a notified read for an explicitly subscribed resource outside the catalog",
  "[mcp][service][hydration][hydration_races][review4]"
) {
  auto config = HydrationConfig();
  config.resource_hydration.read_resources_on_list = false;
  config.resource_hydration.subscribe_to_subscribable_resources = false;
  HydrationFixture f(std::move(config));
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({}));
  f.service->AddServer("house", f.connection);
  f.WaitIdle();
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
  f.WaitIdle();
  f.connection->EnqueueError("resources/read", ErrorCode::TIMEOUT, "timeout");
  EnqueueHouse(*f.connection, 4);
  EmitHouseUpdated(*f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountUpdates(*f.observer) == 1;
  }));
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/read") == 2);
  REQUIRE(LastHouseVersion(*f.observer) == 4);
}

TEST_CASE(
  "MCP service rejects removed automatic resource updates during unsubscribe",
  "[mcp][hydration][subscription_lifecycle]"
) {
  const bool fail_unsubscribe = GENERATE(false, true);
  auto config = HydrationConfig();
  config.thread_pool_size = 4;
  config.resource_hydration.read_resources_on_list = false;
  HydrationFixture f(std::move(config));
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  f.service->AddServer("house", f.connection);
  f.WaitIdle();
  BlockedMethod blocked(f.connection, "resources/unsubscribe");
  if (fail_unsubscribe) {
    f.connection->EnqueueError("resources/unsubscribe", ErrorCode::INVALID_PARAMS, "cannot unsubscribe");
  } else {
    f.connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue));
  }
  EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({}));
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/unsubscribe") == 1;
  }));
  f.WaitIdle(1);
  REQUIRE(f.service->GetResourcesSnapshot().empty());
  EnqueueHouse(*f.connection, 1);
  EmitHouseUpdated(*f.connection);
  f.WaitIdle(1);
  REQUIRE(CountUpdates(*f.observer) == 0);
  REQUIRE(CountCalls(*f.connection, "resources/read") == 0);
  f.connection->UnblockMethod("resources/unsubscribe");
  f.WaitIdle();
  EmitHouseUpdated(*f.connection);
  f.WaitIdle();
  REQUIRE(CountUpdates(*f.observer) == 0);
  REQUIRE(CountCalls(*f.connection, "resources/read") == 0);
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
}

TEST_CASE(
  "MCP service resubscribes after a late unsubscribe when interest returns", "[mcp][hydration][subscription_lifecycle]"
) {
  const bool explicit_interest = GENERATE(false, true);
  auto config = HydrationConfig();
  config.thread_pool_size = 4;
  config.resource_hydration.read_resources_on_list = false;
  HydrationFixture f(std::move(config));
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  f.service->AddServer("house", f.connection);
  f.WaitIdle();
  BlockedMethod blocked(f.connection, "resources/unsubscribe");
  f.connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue));
  EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({}));
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/unsubscribe") == 1;
  }));
  if (explicit_interest) {
    REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
  } else {
    EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri}));
  }
  f.WaitIdle(1);
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 1);
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  f.connection->UnblockMethod("resources/unsubscribe");
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 2);
  REQUIRE(CountCalls(*f.connection, "resources/unsubscribe") == 1);
  auto completed = std::chrono::steady_clock::time_point{};
  for (const auto& call : f.connection->RequestCalls()) {
    if (call.method == "resources/subscribe" || call.method == "resources/unsubscribe") {
      REQUIRE(call.started_at >= completed);
      completed = call.completed_at;
    }
  }
  EnqueueHouse(*f.connection, 2);
  EmitHouseUpdated(*f.connection);
  f.WaitIdle();
  REQUIRE(CountUpdates(*f.observer) == 1);
  REQUIRE(LastHouseVersion(*f.observer) == 2);
}

TEST_CASE("MCP service delivers updates while registration is pending", "[mcp][hydration][subscription_lifecycle]") {
  const bool explicit_interest = GENERATE(false, true);
  auto config = HydrationConfig();
  config.thread_pool_size = 4;
  config.resource_hydration.read_resources_on_list = false;
  config.resource_hydration.subscribe_to_subscribable_resources = !explicit_interest;
  HydrationFixture f(std::move(config));
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  BlockedMethod blocked(f.connection, "resources/subscribe");
  f.service->AddServer("house", f.connection);
  if (explicit_interest) {
    f.WaitIdle();
    REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
  }
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/subscribe") == 1;
  }));
  EnqueueHouse(*f.connection, 1);
  EmitHouseUpdated(*f.connection);
  f.WaitIdle(1);
  REQUIRE(CountUpdates(*f.observer) == 1);
  f.connection->UnblockMethod("resources/subscribe");
  f.WaitIdle();
  REQUIRE(CountUpdates(*f.observer) == 1);
}

TEST_CASE(
  "MCP service preserves updates during failed unsubscribe and restores explicit intent",
  "[mcp][hydration][subscription_lifecycle]"
) {
  auto config = HydrationConfig();
  config.thread_pool_size = 4;
  config.resource_hydration.read_resources_on_list = false;
  config.resource_hydration.subscribe_to_subscribable_resources = false;
  HydrationFixture f(std::move(config));
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.service->AddServer("house", f.connection);
  f.WaitIdle();
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
  f.WaitIdle();
  BlockedMethod blocked(f.connection, "resources/unsubscribe");
  f.connection->EnqueueError("resources/unsubscribe", ErrorCode::SERVER_ERROR, "unsubscribe failed");
  REQUIRE(f.service->UnsubscribeResourceAsync(2, "house", kHouseUri));
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/unsubscribe") == 1;
  }));
  EnqueueHouse(*f.connection, 1);
  EmitHouseUpdated(*f.connection);
  f.WaitIdle(1);
  REQUIRE(CountUpdates(*f.observer) == 1);
  f.connection->UnblockMethod("resources/unsubscribe");
  f.WaitIdle();
  {
    std::lock_guard<std::mutex> lock(f.observer->mutex);
    REQUIRE(f.observer->resource_subscription_events.size() == 2);
    REQUIRE(f.observer->resource_subscription_events.back().is_error);
  }
  EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({}));
  f.WaitIdle();
  EnqueueHouse(*f.connection, 2);
  EmitHouseUpdated(*f.connection);
  f.WaitIdle();
  REQUIRE(CountUpdates(*f.observer) == 2);
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 1);
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
}

TEST_CASE(
  "MCP service keeps the subscription until both explicit and automatic interest end",
  "[mcp][hydration][subscription_lifecycle]"
) {
  const bool explicit_first = GENERATE(false, true);
  auto config = HydrationConfig();
  config.resource_hydration.read_resources_on_list = false;
  HydrationFixture f(std::move(config));
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  f.service->AddServer("house", f.connection);
  f.WaitIdle();
  REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
  f.WaitIdle();
  const auto release_explicit = [&]() {
    REQUIRE(f.service->UnsubscribeResourceAsync(2, "house", kHouseUri));
  };
  const auto release_automatic = [&]() {
    EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({}));
  };
  if (explicit_first) {
    release_explicit();
  } else {
    release_automatic();
  }
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/unsubscribe") == 0);
  EnqueueHouse(*f.connection, 1);
  EmitHouseUpdated(*f.connection);
  f.WaitIdle();
  REQUIRE(CountUpdates(*f.observer) == 1);
  f.connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue));
  if (explicit_first) {
    release_automatic();
  } else {
    release_explicit();
  }
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 1);
  REQUIRE(CountCalls(*f.connection, "resources/unsubscribe") == 1);
}

TEST_CASE(
  "MCP service does not retain a failed explicit request during initialization",
  "[mcp][hydration][subscription_lifecycle]"
) {
  const bool unsubscribe = GENERATE(false, true);
  McpServiceConfig config;
  config.thread_pool_size = 4;
  HydrationFixture f(std::move(config));
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({}));
  BlockedMethod blocked(f.connection, "initialize");
  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "initialize") == 1;
  }));
  REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
  f.WaitIdle(1);
  {
    std::lock_guard<std::mutex> lock(f.observer->mutex);
    REQUIRE(f.observer->resource_subscription_events.size() == 1);
    REQUIRE(f.observer->resource_subscription_events.back().is_error);
    REQUIRE(f.observer->resource_subscription_events.back().error_message == "Server unavailable");
  }
  if (unsubscribe) {
    REQUIRE(f.service->UnsubscribeResourceAsync(2, "house", kHouseUri));
    f.WaitIdle(1);
    std::lock_guard<std::mutex> lock(f.observer->mutex);
    REQUIRE(f.observer->resource_subscription_events.size() == 2);
    REQUIRE_FALSE(f.observer->resource_subscription_events.back().is_error);
  }
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  f.connection->UnblockMethod("initialize");
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 0);
  EmitHouseUpdated(*f.connection);
  f.WaitIdle();
  REQUIRE(CountUpdates(*f.observer) == 0);
}

TEST_CASE(
  "MCP service removes retry records when a server generation ends during a read", "[mcp][hydration][retry_lifetime]"
) {
  const bool reconnect = GENERATE(false, true);
  auto config = HydrationConfig(10, 10);
  config.thread_pool_size = 4;
  HydrationFixture f(std::move(config));
  for (int iteration = 0; iteration < 3; ++iteration) {
    CAPTURE(iteration, reconnect);
    f.connection = std::make_shared<FakeConnection>();
    const auto retries = f.pool->RetryCount();
    f.pool->HoldRetries();
    EnqueueServerStart(*f.connection, false, MakeResourcesListWithoutSubscribable({kHouseUri}));
    EnqueueUnavailable(*f.connection);
    f.connection->EnqueueError("resources/read", ErrorCode::TIMEOUT, "late timeout", Json::Value(), "late-read");
    BlockedRequest blocked(f.connection, "late-read");
    f.service->AddServer("house", f.connection);
    f.WaitRetry(retries + 1);
    REQUIRE(GetMcpResourceRetryCount(*f.service) == 1);
    f.pool->ReleaseRetries();
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/read") == 2;
    }));
    if (reconnect) {
      auto replacement = std::make_shared<FakeConnection>();
      EnqueueServerStart(*replacement, false, MakeResourcesListWithoutSubscribable({}));
      f.service->AddServer("house", replacement);
    } else {
      f.service->RemoveServer("house");
    }
    REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
    f.WaitIdle(1);
    f.connection->UnblockRequest("late-read");
    f.WaitIdle();
    REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
  }
}

TEST_CASE(
  "MCP service completes opposing explicit requests in FIFO order", "[mcp][hydration][subscription_lifecycle][review6]"
) {
  const bool capable = GENERATE(false, true);
  HydrationFixture f(McpServiceConfig{});
  EnqueueServerStart(*f.connection, capable, MakeResourcesListWithoutSubscribable({}));
  f.service->AddServer("house", f.connection);
  f.WaitIdle();
  BlockedMethod blocked(f.connection, "ping");
  f.connection->EnqueueSuccess("ping", Json::Value(Json::objectValue));
  REQUIRE(f.service->PingAsync(99, McpPingRequest{.call_id = "busy", .server_id = "house"}));
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "ping") == 1;
  }));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  f.connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue));
  REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
  REQUIRE(f.service->UnsubscribeResourceAsync(2, "house", kHouseUri));
  f.connection->UnblockMethod("ping");
  f.WaitIdle();
  std::lock_guard<std::mutex> lock(f.observer->mutex);
  REQUIRE(f.observer->resource_subscription_events.size() == 2);
  const auto& subscribed = f.observer->resource_subscription_events[0];
  const auto& unsubscribed = f.observer->resource_subscription_events[1];
  REQUIRE(subscribed.operation_id == 1);
  REQUIRE(subscribed.operation == McpResourceSubscriptionEvent::Operation::SUBSCRIBE);
  REQUIRE(subscribed.is_error == !capable);
  if (!capable) {
    REQUIRE(subscribed.error_message == "Server does not support resource subscriptions: house");
  }
  REQUIRE(unsubscribed.operation_id == 2);
  REQUIRE(unsubscribed.operation == McpResourceSubscriptionEvent::Operation::UNSUBSCRIBE);
  REQUIRE_FALSE(unsubscribed.is_error);
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == static_cast<size_t>(capable));
  REQUIRE(CountCalls(*f.connection, "resources/unsubscribe") == static_cast<size_t>(capable));
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
}

TEST_CASE(
  "MCP service and coordinator do not retain a failed explicit subscription",
  "[mcp][hydration][subscription_lifecycle][review6]"
) {
  auto config = HydrationConfig(10, 10);
  config.resource_hydration.read_resources_on_list = false;
  config.resource_hydration.subscribe_to_subscribable_resources = false;
  HydrationFixture f(std::move(config));
  f.pool->HoldRetries();
  auto coordinator = CreateMcpCoordinator(f.service);
  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{.consumer_name = "explicit", .visible_server_ids = {"house"}}
  );
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({}));
  f.service->AddServer("house", f.connection);
  f.WaitIdle();
  f.connection->EnqueueError("resources/subscribe", ErrorCode::TIMEOUT, "temporary failure");
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  REQUIRE(coordinator->SubscribeResource(consumer, "house", kHouseUri));
  REQUIRE(WaitUntil([&]() {
    const auto states = coordinator->GetResourceSubscriptionStatesSnapshot(consumer);
    return states.data && states.data->size() == 1
      && states.data->front().status == McpResourceSubscriptionStatus::SUBSCRIBE_FAILED;
  }));
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
  coordinator->UnsubscribeResource(consumer, "house", kHouseUri);
  f.pool->ReleaseRetries();
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 1);
  REQUIRE(CountCalls(*f.connection, "resources/unsubscribe") == 0);
  EmitHouseUpdated(*f.connection);
  REQUIRE(coordinator->GetLatestResourceStatesSnapshot(consumer).data->empty());
  REQUIRE(coordinator->SubscribeResource(consumer, "house", kHouseUri));
  f.WaitIdle();
  REQUIRE(
    coordinator->GetResourceSubscriptionStatesSnapshot(consumer).data->front().status
    == McpResourceSubscriptionStatus::SUBSCRIBED
  );
  EnqueueHouse(*f.connection, 1);
  EmitHouseUpdated(*f.connection);
  REQUIRE(WaitUntil([&]() {
    return coordinator->GetLatestResourceStatesSnapshot(consumer).data->size() == 1;
  }));
  f.connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue));
  coordinator->UnsubscribeResource(consumer, "house", kHouseUri);
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/unsubscribe") == 1);
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
}

TEST_CASE(
  "MCP service keeps out-of-catalog updates until unsubscribe succeeds",
  "[mcp][hydration][subscription_lifecycle][review6]"
) {
  const bool fail = GENERATE(false, true);
  auto config = HydrationConfig();
  config.thread_pool_size = 4;
  config.resource_hydration.read_resources_on_list = false;
  config.resource_hydration.subscribe_to_subscribable_resources = false;
  HydrationFixture f(std::move(config));
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({}));
  f.service->AddServer("house", f.connection);
  f.WaitIdle();
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
  f.WaitIdle();
  BlockedMethod blocked(f.connection, "resources/unsubscribe");
  if (fail) {
    f.connection->EnqueueError("resources/unsubscribe", ErrorCode::TIMEOUT, "temporary failure");
  } else {
    f.connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue));
  }
  REQUIRE(f.service->UnsubscribeResourceAsync(2, "house", kHouseUri));
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/unsubscribe") == 1;
  }));
  EnqueueHouse(*f.connection, 1);
  EmitHouseUpdated(*f.connection);
  f.WaitIdle(1);
  REQUIRE(CountUpdates(*f.observer) == 1);
  f.connection->UnblockMethod("resources/unsubscribe");
  f.WaitIdle();
  EnqueueHouse(*f.connection, 2);
  EmitHouseUpdated(*f.connection);
  f.WaitIdle();
  REQUIRE(CountUpdates(*f.observer) == (fail ? 2 : 1));
  REQUIRE(CountCalls(*f.connection, "resources/read") == (fail ? 2 : 1));
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
}

TEST_CASE(
  "MCP service removes explicit subscription retries on the final unsubscribe",
  "[mcp][hydration][retry_lifetime][review6]"
) {
  const bool running = GENERATE(false, true);
  auto config = HydrationConfig(10, 10);
  config.thread_pool_size = 4;
  config.resource_hydration.read_resources_on_list = false;
  config.resource_hydration.subscribe_to_subscribable_resources = false;
  HydrationFixture f(std::move(config));
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({}));
  f.service->AddServer("house", f.connection);
  f.WaitIdle();
  f.pool->HoldRetries();
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
  f.WaitIdle();
  EnqueueUnavailable(*f.connection);
  f.connection->EnqueueError("resources/read", ErrorCode::TIMEOUT, "late failure", Json::Value(), "late-retry");
  BlockedRequest blocked(f.connection, "late-retry");
  EmitHouseUpdated(*f.connection);
  f.WaitRetry();
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 1);
  if (running) {
    f.pool->ReleaseRetries();
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/read") == 2;
    }));
  }
  f.connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue));
  REQUIRE(f.service->UnsubscribeResourceAsync(2, "house", kHouseUri));
  REQUIRE(WaitUntil([&]() {
    std::lock_guard<std::mutex> lock(f.observer->mutex);
    return f.observer->resource_subscription_events.size() == 2;
  }));
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
  f.connection->UnblockRequest("late-retry");
  f.pool->ReleaseRetries();
  f.WaitIdle();
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
  REQUIRE(CountCalls(*f.connection, "resources/read") == (running ? 2 : 1));
  REQUIRE(CountUpdates(*f.observer) == 0);
}

TEST_CASE(
  "MCP service drains explicit requests while initialization is blocked",
  "[mcp][hydration][subscription_lifecycle][review7]"
) {
  const bool initialize_ok = GENERATE(false, true);
  auto config = McpServiceConfig{};
  config.thread_pool_size = 2;
  HydrationFixture f(std::move(config));
  auto blocker = std::make_shared<FakeConnection>();
  EnqueueServerStart(*blocker, false, MakeResourcesListWithoutSubscribable({}));
  f.service->AddServer("blocker", blocker);
  f.WaitIdle();
  BlockedMethod ping(blocker, "ping");
  blocker->EnqueueSuccess("ping", Json::Value(Json::objectValue));
  REQUIRE(f.service->PingAsync(99, {.call_id = "busy", .server_id = "blocker"}));
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*blocker, "ping") == 1;
  }));
  if (initialize_ok) {
    EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({}));
  } else {
    f.connection->EnqueueError("initialize", ErrorCode::INTERNAL_ERROR, "initialization failed");
  }
  BlockedMethod initialize(f.connection, "initialize");
  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "initialize") == 1;
  }));
  REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
  REQUIRE(f.service->UnsubscribeResourceAsync(2, "house", kHouseUri));
  blocker->UnblockMethod("ping");
  f.WaitIdle(1);
  {
    std::lock_guard<std::mutex> lock(f.observer->mutex);
    REQUIRE(f.observer->resource_subscription_events.size() == 2);
    const auto& subscribed = f.observer->resource_subscription_events[0];
    const auto& unsubscribed = f.observer->resource_subscription_events[1];
    REQUIRE(subscribed.operation_id == 1);
    REQUIRE(subscribed.is_error);
    REQUIRE(subscribed.error_message == "Server unavailable");
    REQUIRE(unsubscribed.operation_id == 2);
    REQUIRE_FALSE(unsubscribed.is_error);
  }
  f.connection->UnblockMethod("initialize");
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 0);
  REQUIRE(CountCalls(*f.connection, "resources/unsubscribe") == 0);
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
}

TEST_CASE(
  "MCP service rereads hydration performed before a pending subscription completes",
  "[mcp][hydration][subscription_lifecycle][review7]"
) {
  auto config = HydrationConfig();
  config.thread_pool_size = 4;
  HydrationFixture f(std::move(config));
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  BlockedMethod subscribed(f.connection, "resources/subscribe");
  EnqueueHouse(*f.connection, 1);
  f.service->AddServer("house", f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/subscribe") == 1;
  }));
  EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.WaitIdle(1);
  REQUIRE(CountUpdates(*f.observer) == 1);
  REQUIRE(LastHouseVersion(*f.observer) == 1);
  EnqueueHouse(*f.connection, 2);
  f.connection->UnblockMethod("resources/subscribe");
  f.WaitIdle();
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 1);
  REQUIRE(CountCalls(*f.connection, "resources/read") == 2);
  REQUIRE(CountUpdates(*f.observer) == 2);
  REQUIRE(LastHouseVersion(*f.observer) == 2);
}

TEST_CASE(
  "MCP service discards reads from a previous explicit subscription period",
  "[mcp][hydration][subscription_lifecycle][retry_lifetime][review7]"
) {
  const bool fail = GENERATE(false, true);
  auto config = HydrationConfig(10, 10);
  config.thread_pool_size = 4;
  config.resource_hydration.read_resources_on_list = true;
  config.resource_hydration.subscribe_to_subscribable_resources = false;
  HydrationFixture f(std::move(config));
  f.pool->HoldRetries();
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({}));
  f.service->AddServer("house", f.connection);
  f.WaitIdle();
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
  f.WaitIdle();
  if (fail) {
    f.connection->EnqueueError("resources/read", ErrorCode::TIMEOUT, "old period timed out");
  } else {
    EnqueueHouse(*f.connection, 1);
  }
  BlockedMethod read(f.connection, "resources/read");
  EmitHouseUpdated(*f.connection);
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/read") == 1;
  }));
  f.connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue));
  f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  REQUIRE(f.service->UnsubscribeResourceAsync(2, "house", kHouseUri));
  REQUIRE(f.service->SubscribeResourceAsync(3, "house", kHouseUri));
  f.WaitIdle(1);
  {
    std::lock_guard<std::mutex> lock(f.observer->mutex);
    REQUIRE(f.observer->resource_subscription_events.size() == 3);
    REQUIRE_FALSE(f.observer->resource_subscription_events[1].is_error);
    REQUIRE_FALSE(f.observer->resource_subscription_events[2].is_error);
  }
  f.connection->UnblockMethod("resources/read");
  f.WaitIdle();
  REQUIRE(CountUpdates(*f.observer) == 0);
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
  EnqueueHouse(*f.connection, 2);
  EmitHouseUpdated(*f.connection);
  f.WaitIdle();
  REQUIRE(CountUpdates(*f.observer) == 1);
  REQUIRE(LastHouseVersion(*f.observer) == 2);
  REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
  EnqueueHouse(*f.connection, 3);
  EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.WaitIdle();
  REQUIRE(CountUpdates(*f.observer) == 2);
  REQUIRE(LastHouseVersion(*f.observer) == 3);
}

TEST_CASE(
  "MCP service completes queued unsubscribe successfully when replacing a server",
  "[mcp][hydration][subscription_lifecycle][review7]"
) {
  const bool subscribe_first = GENERATE(false, true);
  HydrationFixture f(McpServiceConfig{});
  EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({}));
  f.service->AddServer("house", f.connection);
  f.WaitIdle();
  // The subscription operations are accepted, but their tasks do not start before the server is replaced for any
  // number of pool threads.
  f.pool->HoldTasks();
  if (subscribe_first) {
    REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
    REQUIRE(f.service->UnsubscribeResourceAsync(2, "house", kHouseUri));
  } else {
    REQUIRE(f.service->UnsubscribeResourceAsync(1, "house", kHouseUri));
    REQUIRE(f.service->SubscribeResourceAsync(2, "house", kHouseUri));
  }
  auto replacement = std::make_shared<FakeConnection>();
  EnqueueServerStart(*replacement, true, MakeResourcesListWithoutSubscribable({}));
  f.service->AddServer("house", replacement);
  f.pool->ReleaseHeldTasks();
  f.WaitIdle();
  std::lock_guard<std::mutex> lock(f.observer->mutex);
  REQUIRE(f.observer->resource_subscription_events.size() == 2);
  const auto& subscribed = f.observer->resource_subscription_events[subscribe_first ? 0 : 1];
  const auto& unsubscribed = f.observer->resource_subscription_events[subscribe_first ? 1 : 0];
  REQUIRE(subscribed.operation_id == (subscribe_first ? 1 : 2));
  REQUIRE(subscribed.is_error);
  REQUIRE(subscribed.error_message == "Server disconnected");
  REQUIRE(unsubscribed.operation_id == (subscribe_first ? 2 : 1));
  REQUIRE_FALSE(unsubscribed.is_error);
  REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 0);
  REQUIRE(CountCalls(*f.connection, "resources/unsubscribe") == 0);
}

TEST_CASE(
  "MCP service serializes resource refreshes and coalesces a pending list change",
  "[mcp][hydration][resource_generation][review8]"
) {
  McpServiceConfig config;
  config.thread_pool_size = 4;
  HydrationFixture f(std::move(config));
  EnqueueServerStart(*f.connection, false, MakeResourcesListWithoutSubscribable({kHouseUri}));
  f.service->AddServer("house", f.connection);
  f.WaitIdle();
  f.connection->EnqueueSuccess("resources/list", MakeResourcesListWithoutSubscribable({kHouseUri}));
  Json::Value templates(Json::objectValue);
  templates["resourceTemplates"] = Json::Value(Json::arrayValue);
  f.connection->EnqueueSuccess("resources/templates/list", templates, "old-templates");
  BlockedRequest blocked(f.connection, "old-templates");
  f.connection->EmitNotification("notifications/resources/list_changed", Json::Value(Json::objectValue));
  REQUIRE(WaitUntil([&]() {
    return CountCalls(*f.connection, "resources/templates/list") == 2;
  }));
  EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({}));
  for (int i = 0; i < 10; ++i) {
    f.connection->EmitNotification("notifications/resources/list_changed", Json::Value(Json::objectValue));
  }
  f.WaitIdle(1);
  const auto lists_before_release = CountCalls(*f.connection, "resources/list");
  f.connection->UnblockRequest("old-templates");
  f.WaitIdle();
  REQUIRE(f.service->GetResourcesSnapshot().empty());
  {
    std::lock_guard<std::mutex> lock(f.observer->mutex);
    REQUIRE(f.observer->resource_snapshots.back().empty());
  }
  REQUIRE(lists_before_release == 2);
  REQUIRE(CountCalls(*f.connection, "resources/list") == 3);
  REQUIRE(CountCalls(*f.connection, "resources/templates/list") == 3);
}

TEST_CASE(
  "MCP client keeps a successful handler when an older subscribe fails", "[mcp][subscription_lifecycle][review8]"
) {
  const bool previous = GENERATE(false, true);
  RequireIsolated([&]() {
    auto connection = std::make_shared<FakeConnection>();
    auto client = createMcpClient(connection);
    int old_updates = 0;
    int new_updates = 0;
    if (previous) {
      connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
      REQUIRE(client
                ->subscribeResource(
                  kHouseUri,
                  [&](const ResourceNotification&) {
                    ++old_updates;
                  }
                )
                .ok());
    }
    connection->EnqueueError("resources/subscribe", ErrorCode::TIMEOUT, "old failure", Json::Value(), "subscribe-A");
    connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    std::future<Result<void>> first;
    BlockedRequest blocked(connection, "subscribe-A");
    first = std::async(std::launch::async, [&]() {
      return client->subscribeResource(kHouseUri, [&](const ResourceNotification&) {
        ++old_updates;
      });
    });
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*connection, "resources/subscribe") == (previous ? 2 : 1);
    }));
    REQUIRE(client
              ->subscribeResource(
                kHouseUri,
                [&](const ResourceNotification&) {
                  ++new_updates;
                }
              )
              .ok());
    EmitHouseUpdated(*connection);
    REQUIRE(new_updates == 1);
    connection->UnblockRequest("subscribe-A");
    REQUIRE_FALSE(first.get().ok());
    EmitHouseUpdated(*connection);
    REQUIRE(new_updates == 2);
    REQUIRE(old_updates == 0);
  });
}

TEST_CASE(
  "MCP client keeps the newer handler when an older subscribe succeeds late", "[mcp][subscription_lifecycle][review12]"
) {
  RequireIsolated([&]() {
    auto connection = std::make_shared<FakeConnection>();
    auto client = createMcpClient(connection);
    int old_updates = 0;
    int new_updates = 0;
    connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue), "subscribe-A");
    connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    std::future<Result<void>> first;
    BlockedRequest blocked(connection, "subscribe-A");
    first = std::async(std::launch::async, [&]() {
      return client->subscribeResource(kHouseUri, [&](const ResourceNotification&) {
        ++old_updates;
      });
    });
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*connection, "resources/subscribe") == 1;
    }));
    REQUIRE(client
              ->subscribeResource(
                kHouseUri,
                [&](const ResourceNotification&) {
                  ++new_updates;
                }
              )
              .ok());
    connection->UnblockRequest("subscribe-A");
    REQUIRE(first.get().ok());
    EmitHouseUpdated(*connection);
    REQUIRE(new_updates == 1);
    REQUIRE(old_updates == 0);
  });
}

TEST_CASE(
  "MCP client drops an older pending subscribe after a successful unsubscribe",
  "[mcp][subscription_lifecycle][review12]"
) {
  RequireIsolated([&]() {
    auto connection = std::make_shared<FakeConnection>();
    auto client = createMcpClient(connection);
    int updates = 0;
    connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue), "subscribe-A");
    connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue));
    std::future<Result<void>> first;
    BlockedRequest blocked(connection, "subscribe-A");
    first = std::async(std::launch::async, [&]() {
      return client->subscribeResource(kHouseUri, [&](const ResourceNotification&) {
        ++updates;
      });
    });
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*connection, "resources/subscribe") == 1;
    }));
    REQUIRE(client->unsubscribeResource(kHouseUri).ok());
    EmitHouseUpdated(*connection);
    connection->UnblockRequest("subscribe-A");
    REQUIRE(first.get().ok());
    EmitHouseUpdated(*connection);
    REQUIRE(updates == 0);
  });
}

TEST_CASE(
  "MCP service orders immediate subscription results without relying on pool order",
  "[mcp][subscription_lifecycle][review8]"
) {
  const bool removing = GENERATE(false, true);
  class ReversedPool : public giga_edge::DummyThreadPool {
  public:
    void Push(giga_edge::TaskPtr task) override {
      tasks.push_back(std::move(task));
    }
    void Release() {
      while (!tasks.empty()) {
        auto task = std::move(tasks.back());
        tasks.pop_back();
        task->status = giga_edge::Task::Status::RUNNING;
        task->callback();
        task->status = giga_edge::Task::Status::DONE;
      }
    }
    std::vector<giga_edge::TaskPtr> tasks;
  };
  auto owned_pool = std::make_unique<ReversedPool>();
  auto* pool = owned_pool.get();
  auto service = CreateMcpService(McpServiceConfig{}, std::move(owned_pool));
  auto observer = std::make_shared<CapturingMcpServiceObserver>();
  service->SetObserver(observer);
  if (removing) {
    auto connection = std::make_shared<FakeConnection>();
    EnqueueServerStart(*connection, false, MakeResourcesListWithoutSubscribable({}));
    service->AddServer("house", connection);
    pool->Release();
    service->RemoveServer("house");
  }
  REQUIRE(service->SubscribeResourceAsync(1, "house", kHouseUri));
  REQUIRE(service->UnsubscribeResourceAsync(2, "house", kHouseUri));
  pool->Release();
  std::lock_guard<std::mutex> lock(observer->mutex);
  REQUIRE(observer->resource_subscription_events.size() == 2);
  REQUIRE(observer->resource_subscription_events[0].operation_id == 1);
  REQUIRE(observer->resource_subscription_events[0].is_error);
  REQUIRE(observer->resource_subscription_events[1].operation_id == 2);
  REQUIRE_FALSE(observer->resource_subscription_events[1].is_error);
}

TEST_CASE(
  "MCP service dispatches subscriptions outside its locks with a synchronous pool",
  "[mcp][subscription_lifecycle][review8]"
) {
  const bool exists = GENERATE(false, true);
  RequireIsolated([&]() {
    auto service = CreateMcpService(McpServiceConfig{}, std::make_unique<giga_edge::DummyThreadPool>());
    auto observer = std::make_shared<CapturingMcpServiceObserver>();
    service->SetObserver(observer);
    auto connection = std::make_shared<FakeConnection>();
    if (exists) {
      EnqueueServerStart(*connection, true, MakeResourcesListWithoutSubscribable({}));
      service->AddServer("house", connection);
      connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
      connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue));
    }
    REQUIRE(service->SubscribeResourceAsync(1, "house", kHouseUri));
    REQUIRE(service->UnsubscribeResourceAsync(2, "house", kHouseUri));
    std::lock_guard<std::mutex> lock(observer->mutex);
    REQUIRE(observer->resource_subscription_events.size() == 2);
    REQUIRE(observer->resource_subscription_events[0].operation_id == 1);
    REQUIRE(observer->resource_subscription_events[0].is_error == !exists);
    REQUIRE(observer->resource_subscription_events[1].operation_id == 2);
    REQUIRE_FALSE(observer->resource_subscription_events[1].is_error);
  });
}

TEST_CASE(
  "MCP service warns about automatic subscription failures and their retry policy",
  "[mcp][hydration][subscription_lifecycle][review8]"
) {
  const bool unsubscribe = GENERATE(false, true);
  const bool retry = GENERATE(false, true);
  RequireIsolated([&]() {
    auto config = HydrationConfig(60000, 60000);
    config.resource_hydration.read_resources_on_list = false;
    auto service = CreateMcpService(std::move(config), std::make_unique<giga_edge::DummyThreadPool>());
    auto connection = std::make_shared<FakeConnection>();
    EnqueueServerStart(*connection, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
    const std::string method = unsubscribe ? "resources/unsubscribe" : "resources/subscribe";
    if (unsubscribe) {
      connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    }
    connection->EnqueueError(method, retry ? ErrorCode::TIMEOUT : ErrorCode::METHOD_NOT_FOUND, "subscription failed");
    CapturedLog captured;
    service->AddServer("house", connection);
    if (unsubscribe) {
      EnqueueResourcesRefresh(*connection, MakeResourcesListWithoutSubscribable({}));
    }
    const auto text = captured.text.str();
    REQUIRE(text.find(unsubscribe ? "auto unsubscribeResource" : "auto subscribeResource") != std::string::npos);
    REQUIRE(text.find("'house' uri '" + std::string(kHouseUri) + "'") != std::string::npos);
    REQUIRE(text.find("subscription failed") != std::string::npos);
    REQUIRE(text.find(retry ? ", retry in 60000 ms" : ", no retry") != std::string::npos);
    REQUIRE(GetMcpResourceRetryCount(*service) == (retry ? 1 : 0));
  });
}

TEST_CASE(
  "MCP client keeps the confirmed handler after overlapping subscribes both fail",
  "[mcp][subscription_lifecycle][review9]"
) {
  const bool a_first = GENERATE(false, true);
  RequireIsolated([&]() {
    auto connection = std::make_shared<FakeConnection>();
    auto client = createMcpClient(connection);
    std::vector<int> updates;
    connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    REQUIRE(client
              ->subscribeResource(
                kHouseUri,
                [&](const ResourceNotification&) {
                  updates.push_back(0);
                }
              )
              .ok());
    EmitHouseUpdated(*connection);
    connection->EnqueueError("resources/subscribe", ErrorCode::TIMEOUT, "A failed", Json::Value(), "subscribe-A");
    connection->EnqueueError("resources/subscribe", ErrorCode::TIMEOUT, "B failed", Json::Value(), "subscribe-B");
    std::future<Result<void>> a;
    std::future<Result<void>> b;
    BlockedRequest blocked_a(connection, "subscribe-A");
    BlockedRequest blocked_b(connection, "subscribe-B");
    a = std::async(std::launch::async, [&]() {
      return client->subscribeResource(kHouseUri, [&](const ResourceNotification&) {
        updates.push_back(1);
      });
    });
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*connection, "resources/subscribe") == 2;
    }));
    EmitHouseUpdated(*connection);
    b = std::async(std::launch::async, [&]() {
      return client->subscribeResource(kHouseUri, [&](const ResourceNotification&) {
        updates.push_back(2);
      });
    });
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*connection, "resources/subscribe") == 3;
    }));
    EmitHouseUpdated(*connection);
    connection->UnblockRequest(a_first ? "subscribe-A" : "subscribe-B");
    REQUIRE_FALSE((a_first ? a : b).get().ok());
    EmitHouseUpdated(*connection);
    connection->UnblockRequest(a_first ? "subscribe-B" : "subscribe-A");
    REQUIRE_FALSE((a_first ? b : a).get().ok());
    EmitHouseUpdated(*connection);
    const std::vector<int> expected{0, 1, 2, a_first ? 2 : 1, 0};
    REQUIRE(updates == expected);
  });
}

TEST_CASE(
  "MCP client preserves a newer subscribe across a successful unsubscribe", "[mcp][subscription_lifecycle][review9]"
) {
  const bool subscribe_finishes_first = GENERATE(false, true);
  RequireIsolated([&]() {
    auto connection = std::make_shared<FakeConnection>();
    auto client = createMcpClient(connection);
    std::vector<int> updates;
    connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    REQUIRE(client
              ->subscribeResource(
                kHouseUri,
                [&](const ResourceNotification&) {
                  updates.push_back(0);
                }
              )
              .ok());
    connection->EnqueueSuccess("resources/unsubscribe", Json::Value(Json::objectValue), "unsubscribe-U");
    connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue), "subscribe-S");
    std::future<Result<void>> unsubscribed;
    std::future<Result<void>> subscribed;
    BlockedRequest blocked_u(connection, "unsubscribe-U");
    BlockedRequest blocked_s(connection, "subscribe-S");
    unsubscribed = std::async(std::launch::async, [&]() {
      return client->unsubscribeResource(kHouseUri);
    });
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*connection, "resources/unsubscribe") == 1;
    }));
    EmitHouseUpdated(*connection);
    subscribed = std::async(std::launch::async, [&]() {
      return client->subscribeResource(kHouseUri, [&](const ResourceNotification&) {
        updates.push_back(1);
      });
    });
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*connection, "resources/subscribe") == 2;
    }));
    EmitHouseUpdated(*connection);
    connection->UnblockRequest(subscribe_finishes_first ? "subscribe-S" : "unsubscribe-U");
    REQUIRE((subscribe_finishes_first ? subscribed : unsubscribed).get().ok());
    EmitHouseUpdated(*connection);
    connection->UnblockRequest(subscribe_finishes_first ? "unsubscribe-U" : "subscribe-S");
    REQUIRE((subscribe_finishes_first ? unsubscribed : subscribed).get().ok());
    EmitHouseUpdated(*connection);
    const std::vector<int> expected{0, 1, 1, 1};
    REQUIRE(updates == expected);
  });
}

TEST_CASE(
  "MCP service accepts an explicit read when its resource joins the catalog",
  "[mcp][hydration][subscription_lifecycle][resource_generation][review9]"
) {
  RequireIsolated([&]() {
    auto config = HydrationConfig();
    config.thread_pool_size = 4;
    config.resource_hydration.read_resources_on_list = false;
    config.resource_hydration.subscribe_to_subscribable_resources = false;
    HydrationFixture f(std::move(config));
    EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({}));
    f.service->AddServer("house", f.connection);
    f.WaitIdle();
    f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
    f.WaitIdle();
    EnqueueHouse(*f.connection, 1);
    BlockedMethod blocked(f.connection, "resources/read");
    EmitHouseUpdated(*f.connection);
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/read") == 1;
    }));
    EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri}));
    f.WaitIdle(1);
    REQUIRE(f.service->GetResourcesSnapshot().size() == 1);
    f.connection->UnblockMethod("resources/read");
    f.WaitIdle();
    REQUIRE(CountCalls(*f.connection, "resources/read") == 1);
    REQUIRE(CountUpdates(*f.observer) == 1);
    REQUIRE(LastHouseVersion(*f.observer) == 1);
    REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
  });
}

TEST_CASE(
  "MCP service accepts out-of-catalog updates while an explicit subscribe is confirmed",
  "[mcp][hydration][subscription_lifecycle][review13]"
) {
  RequireIsolated([&]() {
    auto config = HydrationConfig();
    config.thread_pool_size = 4;
    config.resource_hydration.read_resources_on_list = false;
    config.resource_hydration.subscribe_to_subscribable_resources = false;
    HydrationFixture f(std::move(config));
    EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({}));
    f.service->AddServer("house", f.connection);
    f.WaitIdle();
    BlockedMethod blocked(f.connection, "resources/subscribe");
    f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/subscribe") == 1;
    }));
    EnqueueHouse(*f.connection, 1);
    EmitHouseUpdated(*f.connection);
    f.connection->UnblockMethod("resources/subscribe");
    f.WaitIdle();
    REQUIRE(CountUpdates(*f.observer) == 1);
    REQUIRE(CountCalls(*f.connection, "resources/read") == 1);
  });
}

TEST_CASE(
  "MCP service keeps a notified read queued before a retry of an explicit period",
  "[mcp][hydration][resource_generation][review13]"
) {
  RequireIsolated([&]() {
    auto config = HydrationConfig(50, 50);
    config.thread_pool_size = 4;
    config.resource_hydration.read_resources_on_list = false;
    config.resource_hydration.subscribe_to_subscribable_resources = false;
    HydrationFixture f(std::move(config));
    f.pool->HoldRetries();
    EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({}));
    f.service->AddServer("house", f.connection);
    f.WaitIdle();
    f.connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
    REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
    f.WaitIdle();
    f.connection->EnqueueError("resources/read", ErrorCode::TIMEOUT, "timeout");
    EmitHouseUpdated(*f.connection);
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/read") == 1;
    }));
    f.WaitRetry();
    BlockedMethod blocked(f.connection, "resources/read");
    EnqueueHouse(*f.connection, 1);
    EnqueueHouse(*f.connection, 2);
    EmitHouseUpdated(*f.connection);
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "resources/read") == 2;
    }));
    EnqueueResourcesRefresh(*f.connection, MakeResourcesListWithoutSubscribable({kHouseUri}));
    REQUIRE(WaitUntil([&]() {
      return f.service->GetResourcesSnapshot().size() == 1;
    }));
    EmitHouseUpdated(*f.connection);
    f.pool->ReleaseRetries();
    f.WaitRetry(1, true);
    f.connection->UnblockMethod("resources/read");
    REQUIRE(WaitUntil([&]() {
      return CountUpdates(*f.observer) == 2;
    }));
    f.service->Shutdown();
    REQUIRE(CountCalls(*f.connection, "resources/read") == 3);
    REQUIRE(LastHouseVersion(*f.observer) == 2);
  });
}

TEST_CASE(
  "MCP service queues removing server results behind pending subscription requests",
  "[mcp][subscription_lifecycle][review9]"
) {
  RequireIsolated([&]() {
    HydrationFixture f(McpServiceConfig{});
    EnqueueServerStart(*f.connection, true, MakeResourcesListWithoutSubscribable({}));
    f.service->AddServer("house", f.connection);
    f.WaitIdle();
    // The subscription is accepted, but its task does not start before the server is removed for any number of pool
    // threads.
    f.pool->HoldTasks();
    REQUIRE(f.service->SubscribeResourceAsync(1, "house", kHouseUri));
    f.service->RemoveServer("house");
    REQUIRE(f.service->UnsubscribeResourceAsync(2, "house", kHouseUri));
    f.pool->ReleaseHeldTasks();
    f.WaitIdle();
    std::lock_guard<std::mutex> lock(f.observer->mutex);
    REQUIRE(f.observer->resource_subscription_events.size() == 2);
    REQUIRE(f.observer->resource_subscription_events[0].operation_id == 1);
    REQUIRE(f.observer->resource_subscription_events[0].is_error);
    REQUIRE(f.observer->resource_subscription_events[1].operation_id == 2);
    REQUIRE_FALSE(f.observer->resource_subscription_events[1].is_error);
    REQUIRE(CountCalls(*f.connection, "resources/subscribe") == 0);
    REQUIRE(CountCalls(*f.connection, "resources/unsubscribe") == 0);
  });
}

namespace {

McpToolCallRequest SearchCall(const std::string& call_id, const Json::Value& metadata = {}) {
  McpToolCallRequest request{.call_id = call_id, .server_id = "house", .tool_name = "search", .metadata = metadata};
  request.arguments["query"] = call_id;
  return request;
}

McpToolCallResultEvent ToolResultOf(CapturingMcpServiceObserver& observer, McpRequestToken token) {
  std::lock_guard<std::mutex> lock(observer.mutex);
  const auto it = std::find_if(observer.tool_results.begin(), observer.tool_results.end(), [&](const auto& item) {
    return item.token == token;
  });
  REQUIRE(it != observer.tool_results.end());
  return it->result;
}

size_t CountToolResults(CapturingMcpServiceObserver& observer) {
  std::lock_guard<std::mutex> lock(observer.mutex);
  return observer.tool_results.size();
}

Json::Value SentParams(FakeConnection& connection, const std::string& request_id) {
  for (const auto& call : connection.RequestCalls()) {
    if (call.request_id == request_id) {
      return call.params;
    }
  }
  FAIL("request " << request_id << " was not sent");
  return {};
}

} // namespace

TEST_CASE(
  "MCP tool calls cancelled before and after sending leave a pending resource retry intact",
  "[mcp][service][hydration][cancellation]"
) {
  RequireIsolated([&]() {
    HydrationFixture f(HydrationConfig(5, 5));
    f.pool->HoldRetries();
    EnqueueServerStart(*f.connection, false, MakeResourcesListWithoutSubscribable({kHouseUri}));
    EnqueueUnavailable(*f.connection);
    f.service->AddServer("house", f.connection);
    f.WaitRetry();
    REQUIRE(GetMcpResourceRetryCount(*f.service) == 1);

    Json::Value metadata;
    metadata["client"]["session_id"] = "session-1";
    BlockedMethod blocked(f.connection, "tools/call");
    f.connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue), "sent-call");
    REQUIRE(f.service->CallToolAsync(1, SearchCall("sent", metadata)));
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "tools/call") == 1;
    }));
    // The second call is accepted, but its task does not start before the cancellation for any number of pool threads.
    f.pool->HoldTasks();
    REQUIRE(f.service->CallToolAsync(2, SearchCall("queued")));
    REQUIRE(f.service->Cancel(2));
    REQUIRE(f.service->Cancel(1));
    f.pool->ReleaseHeldTasks();
    // The cancellation aborts the wait for the response; notifications/cancelled is sent by the woken pool thread.
    REQUIRE(WaitUntil([&]() {
      return CountNotifications(f.connection, "notifications/cancelled") == 1;
    }));
    REQUIRE(GetMcpResourceRetryCount(*f.service) == 1);

    f.connection->EnqueueError("tools/call", ErrorCode::SERVER_ERROR, "busy", Json::Value(), "", -32001);
    f.connection->UnblockMethod("tools/call");
    REQUIRE(f.service->CallToolAsync(3, SearchCall("failed")));
    REQUIRE(WaitUntil([&]() {
      return CountToolResults(*f.observer) == 3;
    }));

    EnqueueHouse(*f.connection, 1);
    f.pool->ReleaseRetries();
    REQUIRE(WaitUntil([&]() {
      return CountUpdates(*f.observer) == 1;
    }));
    REQUIRE(WaitUntil([&]() {
      return GetMcpResourceRetryCount(*f.service) == 0;
    }));
    f.service->Shutdown();

    const auto queued = ToolResultOf(*f.observer, 2);
    REQUIRE(queued.is_error);
    REQUIRE(queued.error_message == "Cancelled");
    REQUIRE(queued.error_code == ErrorCode::CANCELLED);
    REQUIRE_FALSE(queued.request_sent);
    const auto sent = ToolResultOf(*f.observer, 1);
    REQUIRE(sent.error_message == "Cancelled");
    REQUIRE(sent.error_code == ErrorCode::CANCELLED);
    REQUIRE(sent.request_sent);
    const auto failed = ToolResultOf(*f.observer, 3);
    REQUIRE(failed.error_message == "busy");
    REQUIRE(failed.error_code == ErrorCode::SERVER_ERROR);
    REQUIRE(failed.request_sent);
    // The late response of the cancelled call is not published; tool calls are not retried.
    REQUIRE(CountToolResults(*f.observer) == 3);
    REQUIRE(CountCalls(*f.connection, "tools/call") == 2);
    REQUIRE(CountCalls(*f.connection, "resources/read") == 2);
    REQUIRE(SentParams(*f.connection, "sent-call")["_meta"] == metadata);
    for (const auto& notification : f.connection->NotificationCalls()) {
      if (notification.method == "notifications/cancelled") {
        REQUIRE(notification.params["requestId"].asString() == "sent-call");
      }
    }
  });
}

TEST_CASE(
  "MCP reconnect fails in-flight tool calls by send state and drops stale replies and retries",
  "[mcp][service][hydration][lifecycle]"
) {
  RequireIsolated([&]() {
    HydrationFixture f(HydrationConfig(5, 5));
    f.pool->HoldRetries();
    EnqueueServerStart(*f.connection, false, MakeResourcesListWithoutSubscribable({kHouseUri}));
    EnqueueUnavailable(*f.connection);
    f.service->AddServer("house", f.connection);
    f.WaitRetry();

    Json::Value metadata;
    metadata["progressToken"] = "progress-1";
    BlockedMethod blocked(f.connection, "tools/call");
    f.connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue), "old-call");
    REQUIRE(f.service->CallToolAsync(1, SearchCall("sent", metadata)));
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*f.connection, "tools/call") == 1;
    }));
    // The second call is accepted, but its task does not start before the reconnect for any number of pool threads.
    f.pool->HoldTasks();
    REQUIRE(f.service->CallToolAsync(2, SearchCall("queued")));
    REQUIRE(f.pool->HeldTaskCount() == 1);

    auto replacement = std::make_shared<FakeConnection>();
    EnqueueServerStart(*replacement, false, MakeResourcesListWithoutSubscribable({kHouseUri}));
    EnqueueHouse(*replacement, 7);
    f.service->AddServer("house", replacement);
    REQUIRE(GetMcpResourceRetryCount(*f.service) == 0);
    f.pool->ReleaseHeldTasks();

    f.connection->UnblockMethod("tools/call");
    f.pool->ReleaseRetries();
    REQUIRE(WaitUntil([&]() {
      return CountUpdates(*f.observer) == 1 && CountToolResults(*f.observer) == 2;
    }));
    f.WaitIdle();
    f.service->Shutdown();

    REQUIRE(LastHouseVersion(*f.observer) == 7);
    for (const McpRequestToken token : {1, 2}) {
      CAPTURE(token);
      const auto result = ToolResultOf(*f.observer, token);
      REQUIRE(result.is_error);
      REQUIRE(result.error_message == "Server disconnected");
      REQUIRE(result.error_code == ErrorCode::TRANSPORT_ERROR);
      REQUIRE(result.request_sent == (token == 1));
    }
    REQUIRE(CountToolResults(*f.observer) == 2);
    REQUIRE(CountCalls(*f.connection, "tools/call") == 1);
    REQUIRE(CountCalls(*replacement, "tools/call") == 0);
    REQUIRE(CountCalls(*f.connection, "resources/read") == 1);
    REQUIRE(CountCalls(*replacement, "resources/read") == 1);
    REQUIRE(SentParams(*f.connection, "old-call")["_meta"] == metadata);
  });
}

TEST_CASE(
  "MCP automatic resource failures go to the MCP log one whole line each", "[mcp][service][hydration][logging]"
) {
  CapturedLog log;
  auto config = HydrationConfig(5, 5);
  config.thread_pool_size = 3;
  {
    HydrationFixture f(std::move(config));
    f.pool->HoldRetries();
    auto retried = std::make_shared<FakeConnection>();
    EnqueueServerStart(*retried, false, MakeResourcesListWithoutSubscribable({kHouseUri}));
    EnqueueUnavailable(*retried);
    auto refused = std::make_shared<FakeConnection>();
    EnqueueServerStart(*refused, false, MakeResourcesListWithoutSubscribable({kHouseUri}));
    refused->EnqueueError("resources/read", ErrorCode::INVALID_PARAMS, "unknown resource");
    auto unsubscribed = std::make_shared<FakeConnection>();
    EnqueueServerStart(*unsubscribed, true, MakeResourcesListWithoutSubscribable({kHouseUri}));
    unsubscribed->EnqueueError("resources/subscribe", ErrorCode::INTERNAL_ERROR, "not ready");
    EnqueueHouse(*unsubscribed, 1);
    f.service->AddServer("retried", retried);
    f.service->AddServer("refused", refused);
    f.service->AddServer("unsubscribed", unsubscribed);
    f.WaitRetry(2);
    REQUIRE(WaitUntil([&]() {
      return CountCalls(*refused, "resources/read") == 1 && CountUpdates(*f.observer) == 1;
    }));
    f.service->Shutdown();
  }
  std::vector<std::string> lines;
  std::istringstream input(log.text.str());
  for (std::string line; std::getline(input, line);) {
    lines.push_back(line);
  }
  std::sort(lines.begin(), lines.end());
  REQUIRE(
    lines
    == std::vector<std::string>{
      "[WARNING] MCP: auto readResource failed for 'refused' uri 'house://devices/state': unknown resource, no retry",
      "[WARNING] MCP: auto readResource failed for 'retried' uri 'house://devices/state': HOME_VIEW_UNAVAILABLE, retry "
      "in 5 ms",
      "[WARNING] MCP: auto subscribeResource failed for 'unsubscribed' uri 'house://devices/state': not ready, retry "
      "in 5 ms",
    }
  );
}
