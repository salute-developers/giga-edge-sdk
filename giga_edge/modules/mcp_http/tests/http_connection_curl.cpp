#include "giga_edge/modules/mcp_http/http_connection.h"
#include "giga_edge/modules/web_transport_curl/curl_web_transport.h"
#include "giga_edge/modules/web_transport_curl/tests/web_transport_curl_test_utils/local_http_server.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("MCP HTTP works over the curl transport end to end", "[mcp_http][curl]") {
  web_transport_curl::test_utils::LocalHttpServer server;
  server.SetResponseHeaders({{"Content-Type", "text/event-stream"}, {"Mcp-Session-Id", "abc"}});
  // The fixture serves one fixed body; ids are sequential, so the first request is http-mcp-1.
  server.SetResponse(
    200, "data: {\"jsonrpc\":\"2.0\",\"id\":\"http-mcp-1\",\"result\":{\"protocolVersion\":\"2025-06-18\"}}\n\n"
  );
  web_transport_curl::CurlWebTransportOptions options;
  options.bypass_proxy = true; // a developer's HTTP(S)_PROXY must not intercept 127.0.0.1
  auto transport = web_transport_curl::CreateCurlWebTransport(server.BaseUrl() + "/api/mcp/assist", options);
  REQUIRE(transport);
  auto connection = mcp_http::CreateHttpConnection(transport, {{{"Authorization", "Bearer secret"}}});
  std::string id;
  const auto init = connection->sendRequest("initialize", Json::Value(Json::objectValue), id, 5000);
  INFO(init.error.message);
  REQUIRE(init.ok());
  CHECK(init.value["protocolVersion"] == "2025-06-18");
  // Second request: same fixed reply, id mismatch, but the headers must carry the session.
  CHECK_FALSE(connection->sendRequest("tools/list", Json::Value(Json::objectValue), id, 5000).ok());
  const auto requests = server.TakeRequests();
  REQUIRE(requests.size() == 2);
  CHECK(requests[0].target == "/api/mcp/assist");
  CHECK(requests[0].method == "POST");
  const auto header = [](const auto& request, const std::string& name) {
    for (const auto& [key, value] : request.headers) {
      if (key == name) {
        return value;
      }
    }
    return std::string("<none>");
  };
  CHECK(header(requests[0], "authorization") == "Bearer secret");
  CHECK(header(requests[0], "content-type") == "application/json");
  CHECK(header(requests[1], "mcp-session-id") == "abc");
  CHECK(header(requests[1], "mcp-protocol-version") == "2025-06-18");
  connection->close();
  transport->Shutdown();
}
