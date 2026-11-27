#include "giga_edge/modules/mcp/client.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_connection.h"

#include <catch.hpp>

#include <string>

using namespace giga_edge::mcp;
using giga_edge::mcp::test_utils::FakeConnection;

TEST_CASE("MCP tools/call without metadata preserves serialized params", "[mcp][metadata]") {
  auto connection = std::make_shared<FakeConnection>();
  auto client = createMcpClient(connection);
  Json::Value arguments;
  const bool null_arguments = GENERATE(false, true);
  if (!null_arguments) {
    arguments["query"] = "test";
  }
  connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue), "request-1");
  std::string request_id;
  REQUIRE(client->callTool("search", arguments, request_id).ok());
  const auto calls = connection->RequestCalls();
  REQUIRE(calls.size() == 1);
  REQUIRE(calls[0].method == "tools/call");
  REQUIRE(calls[0].timeout_ms == 10000);
  REQUIRE(request_id == "request-1");
  REQUIRE_FALSE(calls[0].params.isMember("_meta"));
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  const auto expected = null_arguments
    ? R"({"arguments":null,"name":"search"})"
    : R"({"arguments":{"query":"test"},"name":"search"})";
  REQUIRE(Json::writeString(writer, calls[0].params) == expected);
}

TEST_CASE("MCP tools/call serializes metadata separately from arguments", "[mcp][metadata]") {
  auto connection = std::make_shared<FakeConnection>();
  auto client = createMcpClient(connection);
  Json::Value arguments(Json::objectValue);
  arguments["query"] = "test";
  Json::Value metadata(Json::objectValue);
  const bool empty_metadata = GENERATE(false, true);
  if (!empty_metadata) {
    metadata["client"]["call_id"] = "call-1";
    metadata["client"]["session_id"] = "session-1";
    metadata["progressToken"] = "progress-1";
  }
  connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue), "request-1");
  std::string request_id;
  std::string sent_id;
  REQUIRE(client
            ->callTool(
              "search",
              arguments,
              request_id,
              300000,
              [&](const std::string& id) {
                sent_id = id;
                return true;
              },
              metadata
            )
            .ok());
  const auto calls = connection->RequestCalls();
  REQUIRE(calls.size() == 1);
  REQUIRE(calls[0].method == "tools/call");
  REQUIRE(calls[0].params["_meta"] == metadata);
  REQUIRE(calls[0].params["arguments"] == arguments);
  REQUIRE(calls[0].params["name"] == "search");
  REQUIRE(calls[0].params.size() == 3);
  REQUIRE(calls[0].timeout_ms == 300000);
  REQUIRE(sent_id == request_id);
  REQUIRE(request_id == "request-1");
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  const auto expected = empty_metadata
    ? R"({"_meta":{},"arguments":{"query":"test"},"name":"search"})"
    : R"({"_meta":{"client":{"call_id":"call-1","session_id":"session-1"},"progressToken":"progress-1"},"arguments":{"query":"test"},"name":"search"})";
  REQUIRE(Json::writeString(writer, calls[0].params) == expected);

  connection->EnqueueSuccess("tools/call", Json::Value(Json::objectValue));
  REQUIRE(client->callTool("search", arguments, request_id, 0, {}, Json::Value()).ok());
  REQUIRE_FALSE(connection->RequestCalls().back().params.isMember("_meta"));
}

TEST_CASE("MCP tools/call rejects non-object metadata before sending", "[mcp][metadata]") {
  auto connection = std::make_shared<FakeConnection>();
  auto client = createMcpClient(connection);
  const auto type = GENERATE(Json::arrayValue, Json::stringValue, Json::intValue, Json::booleanValue);
  std::string request_id;
  const auto result = client->callTool("search", {}, request_id, 0, {}, Json::Value(type));
  REQUIRE_FALSE(result.ok());
  REQUIRE(result.error.code == ErrorCode::INVALID_PARAMS);
  REQUIRE(connection->RequestCalls().empty());
}
