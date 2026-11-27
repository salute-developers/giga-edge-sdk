#include "giga_edge/modules/web_transport_curl/tests/web_transport_curl_test_utils/local_http_server.h"

#include "giga_edge/modules/web_transport_curl/tests/web_transport_curl_test_utils/wait_for.h"

#include <catch.hpp>

#include <string>

using namespace giga_edge::web::test_utils;
using namespace giga_edge::web_transport_curl::test_utils;

namespace {

std::string RawExchange(const LocalHttpServer& server, const std::string& request) {
  const size_t colon = server.BaseUrl().rfind(':');
  REQUIRE(colon != std::string::npos);
  const uint16_t port = static_cast<uint16_t>(std::stoi(server.BaseUrl().substr(colon + 1)));

  const Socket fd = socket(AF_INET, SOCK_STREAM, 0);
  REQUIRE_FALSE(SocketFailed(fd));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  REQUIRE(connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
  REQUIRE(
    send(fd, request.data(), static_cast<SocketIoSize>(request.size()), 0) == static_cast<long long>(request.size())
  );
  // The EOF lets the server close the keep-alive connection and end the recv of the client.
  shutdown(fd, kShutWrite);

  std::string response;
  char buf[4096];
  long long n = 0;
  while ((n = recv(fd, buf, static_cast<SocketIoSize>(sizeof(buf)), 0)) > 0) {
    response.append(buf, buf + n);
  }
  CloseSocket(fd);
  return response;
}

} // namespace

TEST_CASE("LocalHttpServer responds to a plain request", "[web_transport_curl][fixture]") {
  LocalHttpServer server;
  server.SetResponse(200, "pong", /*flush_parts=*/2);

  const std::string response = RawExchange(
    server, "POST /echo?x=1 HTTP/1.1\r\nHost: t\r\nContent-Length: 4\r\n\r\nbody"
  );
  REQUIRE(response.find("HTTP/1.1 200") == 0);
  REQUIRE(response.find("pong") != std::string::npos);

  REQUIRE(WaitFor([&]() {
    return server.RequestCount() >= 1;
  }));
  const auto taken = server.TakeRequests();
  REQUIRE(taken.size() == 1);
}

TEST_CASE("LocalHttpServer counts connections and parses requests", "[web_transport_curl][fixture]") {
  LocalHttpServer server;
  server.SetResponse(204, "");

  const std::string response = RawExchange(server, "PUT /k?a=1 HTTP/1.1\r\nHost: t\r\nContent-Length: 0\r\n\r\n");
  REQUIRE(response.find("HTTP/1.1 204") == 0);

  REQUIRE(WaitFor([&]() {
    return server.AcceptedConnections() == 1;
  }));
  const auto requests = server.TakeRequests();
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0].method == "PUT");
  REQUIRE(requests[0].target == "/k?a=1");
  REQUIRE(requests[0].body.empty());
}

TEST_CASE("LocalHttpServer parses request without headers", "[web_transport_curl][fixture]") {
  LocalHttpServer server;
  server.SetResponse(200, "ok");

  const std::string response = RawExchange(server, "GET /a:b HTTP/1.1\r\n\r\n");
  REQUIRE(response.find("HTTP/1.1 200") == 0);
  REQUIRE(WaitFor([&]() {
    return server.RequestCount() == 1;
  }));
  const auto requests = server.TakeRequests();
  REQUIRE(requests.size() == 1);
  CHECK(requests[0].method == "GET");
  CHECK(requests[0].target == "/a:b");
  CHECK(requests[0].headers.empty());
}
