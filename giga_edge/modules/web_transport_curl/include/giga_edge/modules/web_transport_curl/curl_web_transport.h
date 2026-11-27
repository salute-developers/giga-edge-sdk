#pragma once

#include "giga_edge/modules/web/web_transport.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace giga_edge {
namespace web_transport_curl {

struct AwsSigV4Params {
  std::string region;
  std::string access_key;
  std::string secret_key;
};

struct CurlWebTransportOptions {
  uint32_t timeout_ms = 0; // limit for the whole request; 0 means no limit
  uint32_t connect_timeout_ms = 10000;
  bool bypass_proxy = false;
  std::string ca_info; // path to the CA bundle; empty means the system one
  bool debug = false;
  std::optional<AwsSigV4Params> aws_sigv4;
  // 0 means no limit; waiting for a connection in curl counts toward timeout_ms.
  uint32_t max_connections = 0;
  // Bytes/s; 0 means no limit. timeout_ms must cover sending the whole body.
  uint64_t max_send_speed_bytes_per_sec = 0;
  // Bytes/s; 0 means no limit. timeout_ms must cover receiving the whole response.
  uint64_t max_recv_speed_bytes_per_sec = 0;
};

// Methods are thread-safe and non-blocking (except Shutdown/destructor: they cancel active
// streams and wait for the internal worker thread, the only curl_multi thread and the only
// one doing network I/O; no callbacks after they return).
// Observer callbacks run on the worker thread, not on the calling thread; exceptions from
// callbacks are caught by the transport (diagnostics to stderr) and do not crash the process.
// Shutdown and the destructor may be called from any thread, including a callback: from another
// thread they wait for delivery to finish; from a callback they do not wait (the worker cannot
// wait for itself): the remaining streams are closed as CANCELLED asynchronously, resources are
// released at the end of Run.
// Pseudo-headers are strictly lowercase: :method (POST by default), :path (replaces the whole
// base_url path rather than being appended to it; without :path the request goes to the base_url path).
// keep_send_open allows sending until CloseSend; while the length is unknown, chunked is used.
// SendFrame after CloseSend returns false.
// CloseStream is asynchronous: the cancel takes effect on the next worker cycle. Until then
// the current curl_multi_perform keeps delivering: more OnFrame may come after CloseStream,
// and a stream finished in the same cycle is closed as a success (NONE), not CANCELLED.
// The body of an HTTP error goes to WebError, not OnFrame (401/403 -> AUTH_ERROR). Redirects are off
// for SigV4; a 3xx response is an error, not a success.
// A stream body cannot be rewound, frames are read destructively: an internal curl retry
// on a dead keep-alive connection ends with CURLE_SEND_FAIL_REWIND (the attempt is lost).
// A sender able to retry with a new stream and the full body loses only the attempt;
// seeking cannot fix it, frames already handed out cannot be restored.
// The transport does not check idle_timeout_ms: idle streams are closed by the owner.
class CurlWebTransport : public web::IWebTransportWithShutdown {
public:
  CurlWebTransport(std::string base_url, CurlWebTransportOptions options);
  ~CurlWebTransport() override;

  CurlWebTransport(const CurlWebTransport&) = delete;
  CurlWebTransport& operator=(const CurlWebTransport&) = delete;

  void SetObserver(std::weak_ptr<web::IWebTransportObserver> observer) override;
  web::WebStreamId OpenStream(const web::WebStreamDescriptor& descriptor) override;
  bool SendFrame(web::WebStreamId id, web::WebFrame frame) override;
  void CloseSend(web::WebStreamId id) override;
  void CloseStream(web::WebStreamId id) override;

  // After Shutdown OpenStream returns 0.
  void Shutdown() noexcept override;

  bool SendFrameShared(web::WebStreamId id, std::shared_ptr<const web::WebFrame> frame);

private:
  struct Impl;
  std::shared_ptr<Impl> impl_; // the worker holds a second ref until Run ends: destroying from a callback is safe
};

// Returns nullptr in the module stub.
std::shared_ptr<CurlWebTransport> CreateCurlWebTransport(std::string base_url, CurlWebTransportOptions options = {});

} // namespace web_transport_curl
} // namespace giga_edge
