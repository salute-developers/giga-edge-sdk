#include "giga_edge/modules/mcp/client.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_connection.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/throwing_connection.h"

#include <catch.hpp>

#include <string>

using namespace giga_edge::mcp;
using giga_edge::mcp::test_utils::FakeConnection;
using giga_edge::mcp::test_utils::ThrowingConnection;

namespace {

Json::Value Parse(const std::string& text) {
  Json::Value value;
  Json::Reader reader;
  REQUIRE(reader.parse(text, value));
  return value;
}

void RequireInvalidResponse(const Error& error) {
  REQUIRE(error.code == ErrorCode::INTERNAL_ERROR);
  REQUIRE(error.message.find("Invalid") == 0);
}

} // namespace

TEST_CASE("MCP initialize of unexpected shape is an error, not a crash", "[mcp][client][malformed]") {
  const auto payload = GENERATE(
    std::string(R"("ok")"),
    std::string(R"([1])"),
    std::string(R"({"protocolVersion":{"v":1}})"),
    std::string(R"({"serverInfo":[1]})"),
    std::string(R"({"serverInfo":{"name":{"x":1}}})"),
    std::string(R"({"capabilities":{"resources":{"subscribe":"yes"}}})")
  );
  CAPTURE(payload);
  auto connection = std::make_shared<FakeConnection>();
  auto client = createMcpClient(connection);
  connection->EnqueueSuccess("initialize", Parse(payload));
  const auto result = client->initialize("2024-11-05", "test", "1.0");
  REQUIRE_FALSE(result.ok());
  RequireInvalidResponse(result.error);
}

TEST_CASE("MCP initialize accepts null capabilities", "[mcp][client][malformed]") {
  auto connection = std::make_shared<FakeConnection>();
  auto client = createMcpClient(connection);
  connection->EnqueueSuccess("initialize", Parse(R"({"protocolVersion":"2024-11-05","capabilities":null})"));
  const auto result = client->initialize("2024-11-05", "test", "1.0");
  REQUIRE(result.ok());
  REQUIRE_FALSE(result.value.capabilities.supports_tools);
}

TEST_CASE("MCP list responses of unexpected shape are errors", "[mcp][client][malformed]") {
  auto connection = std::make_shared<FakeConnection>();
  auto client = createMcpClient(connection);

  SECTION("tools/list") {
    const auto payload = GENERATE(
      std::string(R"("tools")"), std::string(R"({"tools":["search"]})"), std::string(R"({"tools":[{"name":{"x":1}}]})")
    );
    CAPTURE(payload);
    connection->EnqueueSuccess("tools/list", Parse(payload));
    const auto result = client->listTools();
    REQUIRE_FALSE(result.ok());
    RequireInvalidResponse(result.error);
  }
  SECTION("resources/list") {
    const auto payload = GENERATE(
      std::string(R"({"resources":["house://state"]})"),
      std::string(R"({"resources":[{"uri":"house://state","subscribable":"yes"}]})")
    );
    CAPTURE(payload);
    connection->EnqueueSuccess("resources/list", Parse(payload));
    const auto result = client->listResources();
    REQUIRE_FALSE(result.ok());
    RequireInvalidResponse(result.error);
  }
  SECTION("resources/templates/list") {
    connection->EnqueueSuccess("resources/templates/list", Parse(R"({"resourceTemplates":[7]})"));
    const auto result = client->listResourceTemplates();
    REQUIRE_FALSE(result.ok());
    RequireInvalidResponse(result.error);
  }
}

TEST_CASE("MCP call and read responses of unexpected shape are errors", "[mcp][client][malformed]") {
  auto connection = std::make_shared<FakeConnection>();
  auto client = createMcpClient(connection);

  SECTION("tools/call") {
    const auto payload = GENERATE(std::string(R"("done")"), std::string(R"({"isError":"true","content":[]})"));
    CAPTURE(payload);
    connection->EnqueueSuccess("tools/call", Parse(payload));
    std::string request_id;
    const auto result = client->callTool("search", Json::Value(Json::objectValue), request_id);
    REQUIRE_FALSE(result.ok());
    RequireInvalidResponse(result.error);
  }
  SECTION("resources/read") {
    const auto payload = GENERATE(
      std::string(R"({"contents":["text"]})"), std::string(R"({"contents":[{"text":{"x":1}}]})")
    );
    CAPTURE(payload);
    connection->EnqueueSuccess("resources/read", Parse(payload));
    const auto result = client->readResource("house://state");
    REQUIRE_FALSE(result.ok());
    RequireInvalidResponse(result.error);
  }
}

TEST_CASE("MCP result with an error member is a successful result", "[mcp][client][malformed]") {
  // JSON-RPC separates an error from result at the transport level: an error field inside result is server data.
  auto connection = std::make_shared<FakeConnection>();
  auto client = createMcpClient(connection);
  connection->EnqueueSuccess(
    "tools/call", Parse(R"({"content":[{"type":"text","text":"ok"}],"error":{"code":-31901,"message":"x"}})")
  );
  std::string request_id;
  const auto result = client->callTool("search", Json::Value(Json::objectValue), request_id);
  REQUIRE(result.ok());
  REQUIRE_FALSE(result.value.is_error);
  REQUIRE(result.value.content[0]["text"].asString() == "ok");
}

TEST_CASE("MCP resource notification of unexpected shape is ignored", "[mcp][client][malformed]") {
  auto connection = std::make_shared<FakeConnection>();
  auto client = createMcpClient(connection);
  connection->EnqueueSuccess("resources/subscribe", Json::Value(Json::objectValue));
  int updates = 0;
  REQUIRE(client
            ->subscribeResource(
              "house://state",
              [&updates](const ResourceNotification& notification) {
                REQUIRE(notification.uri == "house://state");
                ++updates;
              }
            )
            .ok());

  // This is how params arrive from the host adapter: JSON text as a string.
  connection->EmitNotification("notifications/resources/updated", Json::Value(R"({"uri":"house://state"})"));
  connection->EmitNotification("notifications/resources/updated", Parse(R"([{"uri":"house://state"}])"));
  connection->EmitNotification("notifications/resources/updated", Parse(R"({"uri":{"x":1}})"));
  connection->EmitNotification("notifications/resources/updated", Parse(R"({"uri":""})"));
  REQUIRE(updates == 0);

  connection->EmitNotification("notifications/resources/updated", Parse(R"({"uri":"house://state"})"));
  REQUIRE(updates == 1);
}

TEST_CASE("MCP client turns a throwing transport into transport errors", "[mcp][client][malformed]") {
  auto client = createMcpClient(std::make_shared<ThrowingConnection>());
  const auto initialized = client->initialize("2024-11-05", "test", "1.0");
  REQUIRE(initialized.error.code == ErrorCode::TRANSPORT_ERROR);
  REQUIRE(initialized.error.message.find("ipc parse failed") != std::string::npos);

  std::string request_id;
  const auto called = client->callTool("search", Json::Value(Json::objectValue), request_id);
  REQUIRE(called.error.code == ErrorCode::TRANSPORT_ERROR);

  client->notifyInitialized();
  client->notifyCancelled("request-1", "cancelled");
  client->abandonRequest("request-1");
  client->shutdown();
}

TEST_CASE("MCP client treats a throwing connection status as disconnected", "[mcp][client][malformed]") {
  auto client = createMcpClient(std::make_shared<ThrowingConnection>(/*is_connected_throws=*/true));
  const auto initialized = client->initialize("2024-11-05", "test", "1.0");
  REQUIRE(initialized.error.code == ErrorCode::TRANSPORT_ERROR);
  REQUIRE(initialized.error.message == "MCP transport is not connected");

  std::string request_id;
  const auto called = client->callTool("search", Json::Value(Json::objectValue), request_id);
  REQUIRE(called.error.code == ErrorCode::TRANSPORT_ERROR);
  REQUIRE(called.error.message == "MCP transport is not connected");

  client->notifyInitialized();
  client->notifyCancelled("request-1", "cancelled");
  client->abandonRequest("request-1");
  client->shutdown();
}
