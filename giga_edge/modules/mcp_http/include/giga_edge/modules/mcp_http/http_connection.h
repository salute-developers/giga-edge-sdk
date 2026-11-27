#pragma once

#include "giga_edge/modules/mcp/connection.h"
#include "giga_edge/modules/web/web_transport.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace giga_edge {
namespace mcp_http {

struct HttpConnectionOptions {
  // Sent with every request, e.g. {"Authorization", "Bearer <token>"}.
  std::vector<std::pair<std::string, std::string>> headers;
  // A longer response body fails the request with TRANSPORT_ERROR and closes its stream.
  size_t max_response_bytes = 16 * 1024 * 1024;
};

// MCP Streamable HTTP client side (spec 2025-03-26+): every JSON-RPC message is one POST to the
// transport's base URL; the reply is application/json or a text/event-stream that ends with the
// response. Server notifications inside such a stream reach the notification handler, on the thread of
// the request. A session id returned by the server is echoed back; the standalone GET stream is not opened.
// HTTP 404 to a request with the session id means the server dropped the session: the connection closes
// and reports it through the close handler, since a new session needs a new initialize.
// The connection observes `transport` exclusively and never shuts it down: the owner does.
// Returns nullptr when the mcp_http module is not built.
mcp::ConnectionPtr CreateHttpConnection(web::WebTransportPtr transport, HttpConnectionOptions options = {});

} // namespace mcp_http
} // namespace giga_edge
