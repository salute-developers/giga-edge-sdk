#include "giga_edge/modules/mcp_http/http_connection.h"

namespace giga_edge {
namespace mcp_http {

mcp::ConnectionPtr CreateHttpConnection(web::WebTransportPtr, HttpConnectionOptions) {
  return nullptr;
}

} // namespace mcp_http
} // namespace giga_edge
