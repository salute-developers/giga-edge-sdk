#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/mcp/client.h"
#include "giga_edge/modules/mcp/environment.h"
#include "giga_edge/modules/mcp/service.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_connection.h"

#include <catch.hpp>

#include <chrono>
#include <stdexcept>
#include <thread>

using namespace giga_edge::mcp;
using giga_edge::mcp::test_utils::FakeConnection;

TEST_CASE("MCP timeout configuration is read from model YAML", "[mcp][timeouts]") {
  const auto defaults = ParseMcpServiceConfig(YAML::Load("{}"));
  REQUIRE(defaults.default_request_timeout_ms == 10000);
  REQUIRE(defaults.default_tool_call_timeout_ms == 10000);
  REQUIRE(defaults.default_read_resource_timeout_ms == 10000);
  REQUIRE(defaults.default_ping_timeout_ms == 10000);
  const auto config = ParseMcpServiceConfig(YAML::Load(R"(
client_name: giga_edge
default_request_timeout_ms: 1234
default_tool_call_timeout_ms: 2345
default_read_resource_timeout_ms: 3456
default_ping_timeout_ms: 4567
)"));
  REQUIRE(config.client_name == "giga_edge");
  REQUIRE(config.default_request_timeout_ms == 1234);
  REQUIRE(config.default_tool_call_timeout_ms == 2345);
  REQUIRE(config.default_read_resource_timeout_ms == 3456);
  REQUIRE(config.default_ping_timeout_ms == 4567);
}

TEST_CASE("MCP client resolves default timeout for every transport request", "[mcp][timeouts]") {
  const auto timeout = GENERATE(10000u, 4321u);
  auto connection = std::make_shared<FakeConnection>();
  auto client = timeout == 10000 ? createMcpClient(connection) : createMcpClient(connection, timeout);
  Json::Value payload(Json::objectValue);
  payload["tools"] = Json::Value(Json::arrayValue);
  payload["resources"] = Json::Value(Json::arrayValue);
  payload["resourceTemplates"] = Json::Value(Json::arrayValue);
  payload["contents"][0]["uri"] = "test://state";
  payload["contents"][0]["text"] = "{}";
  for (const auto* method :
       {"initialize",
        "tools/list",
        "tools/call",
        "resources/list",
        "resources/read",
        "resources/templates/list",
        "resources/subscribe",
        "resources/unsubscribe",
        "ping"}) {
    connection->EnqueueSuccess(method, payload);
  }
  REQUIRE(client->initialize("2025-11-25", "spotter", "1.0").ok());
  REQUIRE(client->listTools().ok());
  std::string request_id;
  REQUIRE(client->callTool("test", Json::Value(Json::objectValue), request_id).ok());
  REQUIRE(client->listResources().ok());
  REQUIRE(client->readResource("test://state").ok());
  REQUIRE(client->listResourceTemplates().ok());
  REQUIRE(client->subscribeResource("test://state", [](const ResourceNotification&) {}).ok());
  REQUIRE(client->unsubscribeResource("test://state").ok());
  REQUIRE(client->ping().ok());
  client->shutdown();
  const auto calls = connection->RequestCalls();
  REQUIRE(calls.size() == 9);
  REQUIRE(calls.front().params["clientInfo"]["name"] == "spotter");
  for (const auto& call : calls) {
    INFO(call.method);
    REQUIRE(call.timeout_ms == timeout);
  }
  REQUIRE(connection->CloseCount() == 1);
}

TEST_CASE("MCP client preserves explicit request timeout", "[mcp][timeouts]") {
  auto connection = std::make_shared<FakeConnection>();
  auto client = createMcpClient(connection);
  std::string request_id;
  client->listTools(123);
  client->callTool("test", {}, request_id, 234);
  client->readResource("test://state", nullptr, 345);
  client->ping(456);
  const auto calls = connection->RequestCalls();
  REQUIRE(calls.size() == 4);
  REQUIRE(calls[0].timeout_ms == 123);
  REQUIRE(calls[1].timeout_ms == 234);
  REQUIRE(calls[2].timeout_ms == 345);
  REQUIRE(calls[3].timeout_ms == 456);
}

TEST_CASE("MCP rejects zero default timeout settings", "[mcp][timeouts]") {
  McpServiceConfig config;
  REQUIRE(config.default_request_timeout_ms == 10000);
  REQUIRE(config.default_tool_call_timeout_ms == 10000);
  REQUIRE(config.default_read_resource_timeout_ms == 10000);
  REQUIRE(config.default_ping_timeout_ms == 10000);
  SECTION("request") {
    config.default_request_timeout_ms = 0;
  }
  SECTION("tool") {
    config.default_tool_call_timeout_ms = 0;
  }
  SECTION("resource") {
    config.default_read_resource_timeout_ms = 0;
  }
  SECTION("ping") {
    config.default_ping_timeout_ms = 0;
  }
  REQUIRE_THROWS_AS(CreateMcpService(config), std::invalid_argument);
  REQUIRE_THROWS_AS(createMcpClient(std::make_shared<FakeConnection>(), 0), std::invalid_argument);
}

TEST_CASE("MCP initialization accepts a response slower than 100 ms", "[mcp][timeouts]") {
  class DelayedConnection : public FakeConnection {
  public:
    Result<Json::Value> sendRequest(
      const std::string& method, const Json::Value& params, std::string& id, uint32_t timeout_ms
    ) override {
      if (method == "initialize") {
        if (timeout_ms < 200) {
          Result<Json::Value> error;
          error.error.code = ErrorCode::TIMEOUT;
          return error;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
      return FakeConnection::sendRequest(method, params, id, timeout_ms);
    }
  };
  auto connection = std::make_shared<DelayedConnection>();
  connection->EnqueueSuccess("initialize", Json::Value(Json::objectValue));
  auto client = createMcpClient(connection);
  REQUIRE(client->initialize("2025-11-25", "spotter", "1.0").ok());
  REQUIRE(connection->RequestCalls().front().timeout_ms == 10000);
}

TEST_CASE("MCP client shutdown closes the transport without a shutdown request", "[mcp][client]") {
  auto connection = std::make_shared<FakeConnection>();
  auto client = createMcpClient(connection);
  client->shutdown();
  client->shutdown();
  REQUIRE(connection->RequestCalls().empty());
  REQUIRE(connection->CloseCount() == 1);
}
