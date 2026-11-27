#include "giga_edge/modules/web_transport_curl/curl_web_transport.h"

#include "giga_edge/modules/web/tests/web_test_utils/fake_web_transport.h"
#include "giga_edge/modules/web_transport_curl/tests/web_transport_curl_test_utils/local_http_server.h"
#include "giga_edge/modules/web_transport_curl/tests/web_transport_curl_test_utils/wait_for.h"

#include <catch.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

using namespace giga_edge;
using namespace giga_edge::web;
using namespace giga_edge::web::test_utils;
using namespace giga_edge::web_transport_curl;
using namespace giga_edge::web_transport_curl::test_utils;

namespace {

struct CapturingObserver : public IWebTransportObserver {
  struct StreamData {
    std::string body;
    int frame_calls = 0;
    bool last_frame_eos = false;
    std::optional<WebResponseHead> head;
    // The order of the stream deliveries: head, frame, closed.
    std::vector<std::string> events;
  };

  void OnResponseHead(WebStreamId id, WebResponseHead head) override {
    std::lock_guard<std::mutex> lock(mutex_);
    threads_.push_back(std::this_thread::get_id());
    auto& data = streams_[id];
    data.head = std::move(head);
    data.events.emplace_back("head");
  }

  void OnFrame(WebStreamId id, WebFrame frame) override {
    std::lock_guard<std::mutex> lock(mutex_);
    threads_.push_back(std::this_thread::get_id());
    auto& data = streams_[id];
    ++data.frame_calls;
    data.last_frame_eos = frame.end_of_stream;
    data.body.append(frame.bytes.data(), frame.bytes.size());
    data.events.emplace_back("frame");
  }

  void OnStreamClosed(WebStreamId id, WebError error) override {
    std::lock_guard<std::mutex> lock(mutex_);
    threads_.push_back(std::this_thread::get_id());
    closed_.emplace(id, error);
    streams_[id].events.emplace_back("closed");
  }

  int FrameCalls(WebStreamId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = streams_.find(id);
    return it == streams_.end() ? 0 : it->second.frame_calls;
  }

  std::string BodyOf(WebStreamId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = streams_.find(id);
    return it == streams_.end() ? std::string{} : it->second.body;
  }

  bool LastFrameEos(WebStreamId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = streams_.find(id);
    return it != streams_.end() && it->second.last_frame_eos;
  }

  std::optional<WebResponseHead> HeadOf(WebStreamId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = streams_.find(id);
    return it == streams_.end() || !it->second.head ? std::nullopt : it->second.head;
  }

  std::vector<std::string> EventsOf(WebStreamId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = streams_.find(id);
    return it == streams_.end() ? std::vector<std::string>{} : it->second.events;
  }

  bool Closed(WebStreamId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_.count(id) != 0;
  }

  WebError CloseError(WebStreamId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = closed_.find(id);
    return it == closed_.end() ? WebError{} : it->second;
  }

  bool AnyClosed() {
    std::lock_guard<std::mutex> lock(mutex_);
    return !closed_.empty();
  }

  WebError FirstCloseError() {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_.empty() ? WebError{} : closed_.begin()->second;
  }

  // The "callbacks come from the worker thread" contract: the thread ids of all deliveries.
  std::vector<std::thread::id> ThreadIds() {
    std::lock_guard<std::mutex> lock(mutex_);
    return threads_;
  }

  size_t CallbackCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return threads_.size();
  }

private:
  std::mutex mutex_;
  std::map<WebStreamId, StreamData> streams_;
  std::map<WebStreamId, WebError> closed_;
  std::vector<std::thread::id> threads_;
};

struct TransportFixture {
  LocalHttpServer server;
  std::shared_ptr<CapturingObserver> observer = std::make_shared<CapturingObserver>();
  std::shared_ptr<CurlWebTransport> transport;

  // The path suffix of base_url, for example "/api" for the :path tests.
  explicit TransportFixture(std::string base_path = {}) {
    transport = CreateCurlWebTransport(server.BaseUrl() + std::move(base_path));
    REQUIRE(transport != nullptr);
    transport->SetObserver(observer);
  }
};

WebFrame Frame(const std::string& bytes, bool end_of_stream = false) {
  WebFrame frame;
  frame.bytes.assign(bytes.begin(), bytes.end());
  frame.end_of_stream = end_of_stream;
  return frame;
}

WebStreamDescriptor Descriptor(const std::string& method = {}, const std::string& path = {}) {
  WebStreamDescriptor descriptor;
  if (!method.empty()) {
    descriptor.headers.emplace_back(":method", method);
  }
  if (!path.empty()) {
    descriptor.headers.emplace_back(":path", path);
  }
  descriptor.headers.emplace_back("content-type", "text/plain");
  return descriptor;
}

// On a deadlock regression both the Shutdown thread and the following transport teardown hang, so REQUIRE would
// replace one hang with another. Fail fast with a diagnostic instead of a hung binary.
void ExpectCompletes(std::future<void>& future, const char* what) {
  if (future.wait_for(kWaitHangGuard) != std::future_status::ready) {
    std::cerr << "FAIL: " << what << " did not return within " << kWaitHangGuard.count() << "s (deadlock)" << std::endl;
    std::_Exit(1);
  }
}

} // namespace

TEST_CASE("Default stream posts to the base url", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "pong");

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  REQUIRE(id != 0);
  REQUIRE(fx.transport->SendFrame(id, Frame("ping", true)));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE(fx.observer->BodyOf(id) == "pong");
  REQUIRE_FALSE(fx.observer->CloseError(id));

  REQUIRE(WaitFor([&]() {
    return fx.server.RequestCount() == 1;
  }));
  const auto requests = fx.server.TakeRequests();
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0].method == "POST");
  REQUIRE(requests[0].target == "/");
  REQUIRE(requests[0].body == "ping");
}

// HEAD through CUSTOMREQUEST without CURLOPT_NOBODY makes curl wait for a response body according to
// Content-Length; a real server does not send one, and the stream hangs until the timeout (forever without one).
TEST_CASE("HEAD completes with a bodiless response instead of waiting for Content-Length", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "body-that-head-does-not-send");

  const WebStreamId id = fx.transport->OpenStream(Descriptor("HEAD", "/key"));
  REQUIRE(id != 0);
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE_FALSE(fx.observer->CloseError(id));
  REQUIRE(fx.observer->BodyOf(id).empty());

  REQUIRE(WaitFor([&]() {
    return fx.server.RequestCount() == 1;
  }));
  const auto requests = fx.server.TakeRequests();
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0].method == "HEAD");
}

TEST_CASE("HEAD stream rejects body frames and completes header-only", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "ignored");

  const WebStreamId id = fx.transport->OpenStream(Descriptor("HEAD"));
  // HEAD has no body per the RFC: frames are rejected instead of being silently lost or causing a hang (UPLOAD in
  // curl resets NOBODY).
  REQUIRE_FALSE(fx.transport->SendFrame(id, Frame("payload", true)));
  REQUIRE_FALSE(fx.transport->SendFrameShared(id, std::make_shared<const WebFrame>(Frame("more"))));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE_FALSE(fx.observer->CloseError(id));
  const auto requests = fx.server.TakeRequests();
  REQUIRE(requests.size() == 1);
  CHECK(requests[0].method == "HEAD");
  CHECK(requests[0].body.empty());
}

// curl adds Expect: 100-continue to large bodies and waits up to a second for "100 Continue", which the peer may
// never send; an empty Expect disables the wait.
TEST_CASE("A large body is sent without Expect 100-continue", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "ok");
  const std::string big(1536 * 1024, 'x');

  const WebStreamId id = fx.transport->OpenStream(Descriptor("PUT", "/big"));
  REQUIRE(id != 0);
  REQUIRE(fx.transport->SendFrame(id, Frame(big, true)));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE_FALSE(fx.observer->CloseError(id));
  REQUIRE(WaitFor([&]() {
    return fx.server.RequestCount() == 1;
  }));
  const auto requests = fx.server.TakeRequests();
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0].body == big);
  REQUIRE(std::none_of(requests[0].headers.begin(), requests[0].headers.end(), [](const auto& header) {
    return header.first == "expect";
  }));
}

TEST_CASE("Pseudo headers route method and path", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "ok");

  const WebStreamId id = fx.transport->OpenStream(Descriptor("PUT", "/key?partNumber=1"));
  REQUIRE(id != 0);
  REQUIRE(fx.transport->SendFrame(id, Frame("data", true)));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE_FALSE(fx.observer->CloseError(id));
  REQUIRE(WaitFor([&]() {
    return fx.server.RequestCount() == 1;
  }));
  const auto requests = fx.server.TakeRequests();
  REQUIRE(requests[0].method == "PUT");
  REQUIRE(requests[0].target == "/key?partNumber=1");
  bool saw_content_type = false;
  for (const auto& header : requests[0].headers) {
    saw_content_type = saw_content_type || (header.first == "content-type" && header.second == "text/plain");
  }
  REQUIRE(saw_content_type);
}

TEST_CASE("SendFrameShared sends shared frame with exact content length", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "ok");

  const auto frame = std::make_shared<const WebFrame>(Frame("shared-body", true));
  const WebStreamId id = fx.transport->OpenStream(Descriptor("PUT", "/key"));
  REQUIRE(id != 0);
  REQUIRE(fx.transport->SendFrameShared(id, frame));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE_FALSE(fx.observer->CloseError(id));
  REQUIRE(WaitFor([&]() {
    return fx.server.RequestCount() == 1;
  }));
  const auto requests = fx.server.TakeRequests();
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0].method == "PUT");
  REQUIRE(requests[0].target == "/key");
  REQUIRE(requests[0].body == "shared-body");
  const std::string* content_length = HeaderValueOf(requests[0].headers, "content-length");
  REQUIRE(content_length != nullptr);
  REQUIRE(*content_length == "11");
}

TEST_CASE("Shared frame outlives the last caller reference", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "ok");

  const WebStreamId id = fx.transport->OpenStream(Descriptor("PUT", "/key"));
  REQUIRE(id != 0);
  {
    const auto frame = std::make_shared<const WebFrame>(Frame("owned-by-transport", true));
    REQUIRE(fx.transport->SendFrameShared(id, frame));
  }
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE_FALSE(fx.observer->CloseError(id));
  REQUIRE(WaitFor([&]() {
    return fx.server.RequestCount() == 1;
  }));
  const auto requests = fx.server.TakeRequests();
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0].body == "owned-by-transport");
}

TEST_CASE("Unknown pseudo header rejects the stream", "[web_transport_curl]") {
  TransportFixture fx;
  WebStreamDescriptor descriptor;
  descriptor.headers.emplace_back(":metod", "PUT");
  REQUIRE(fx.transport->OpenStream(descriptor) == 0);

  WebStreamDescriptor wrong_path;
  wrong_path.headers.emplace_back(":path", "key");
  REQUIRE(fx.transport->OpenStream(wrong_path) == 0);
}

TEST_CASE("Response body streams as multiple frames", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "0123456789abcdefghij", /*flush_parts=*/4);

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  fx.transport->SendFrame(id, Frame("q", true));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE(fx.observer->BodyOf(id) == "0123456789abcdefghij");
  REQUIRE(fx.observer->FrameCalls(id) >= 2);
  REQUIRE_FALSE(fx.observer->CloseError(id));
}

// Contract: the last frame of the response is marked end_of_stream, and decoders that complete the tail on that
// flag (EchoDecoder) must see it. Multi-part delivery is covered by the test above; only the flag on the final
// frame matters here.
TEST_CASE("Successful stream marks the last frame with end_of_stream", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "0123456789abcdefghij");

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  fx.transport->SendFrame(id, Frame("q", true));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE_FALSE(fx.observer->CloseError(id));
  REQUIRE(fx.observer->LastFrameEos(id));
}

TEST_CASE("Response head arrives once before the first frame", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "pong");
  fx.server.SetResponseHeaders({{"Mcp-Session-Id", "sess-123"}, {"Content-Type", "text/event-stream"}});

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  REQUIRE(fx.transport->SendFrame(id, Frame("ping", true)));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  const auto head = fx.observer->HeadOf(id);
  REQUIRE(head.has_value());
  REQUIRE(head->status == 200);
  REQUIRE(HeaderValuesOf(head->headers, "mcp-session-id") == std::vector<std::string>{"sess-123"});
  REQUIRE(HeaderValuesOf(head->headers, "content-type") == std::vector<std::string>{"text/event-stream"});
  // There is exactly one head: a duplicate would show up in the exact sequence below.
  const auto events = fx.observer->EventsOf(id);
  REQUIRE(events.front() == "head");
  REQUIRE(events.at(1) == "frame");
}

TEST_CASE("Duplicate response header names are preserved", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "ok");
  fx.server.SetResponseHeaders({{"Set-Cookie", "a=1"}, {"Set-Cookie", "b=2"}});

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  fx.transport->SendFrame(id, Frame("q", true));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  const auto head = fx.observer->HeadOf(id);
  REQUIRE(head.has_value());
  REQUIRE(HeaderValuesOf(head->headers, "set-cookie") == (std::vector<std::string>{"a=1", "b=2"}));
}

TEST_CASE("HTTP 404 head is delivered before the error close", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(404, "not-found-detail");

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  fx.transport->SendFrame(id, Frame("q", true));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE(fx.observer->CloseError(id).code == WebErrorCode::HTTP_ERROR);
  const auto head = fx.observer->HeadOf(id);
  REQUIRE(head.has_value());
  REQUIRE(head->status == 404);
  const auto events = fx.observer->EventsOf(id);
  REQUIRE(events.front() == "head");
  REQUIRE(events.back() == "closed");
}

TEST_CASE("HEAD stream delivers the head without body frames", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "body-that-head-does-not-send");

  const WebStreamId id = fx.transport->OpenStream(Descriptor("HEAD", "/key"));
  REQUIRE(id != 0);
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE_FALSE(fx.observer->CloseError(id));
  const auto head = fx.observer->HeadOf(id);
  REQUIRE(head.has_value());
  REQUIRE(head->status == 200);
  // HEAD has no body, but a successful stream ends with an eos frame: here too the head comes before the frame.
  REQUIRE(fx.observer->EventsOf(id) == std::vector<std::string>{"head", "frame", "closed"});
}

TEST_CASE("Empty-body response still delivers the head", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "");

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  fx.transport->SendFrame(id, Frame("q", true));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE_FALSE(fx.observer->CloseError(id));
  const auto head = fx.observer->HeadOf(id);
  REQUIRE(head.has_value());
  REQUIRE(head->status == 200);
  REQUIRE(fx.observer->EventsOf(id) == std::vector<std::string>{"head", "frame", "closed"});
}

// RFC 9112 5.2: obs-fold (a continuation line with a leading SP/HTAB): the value is unfolded into one line; it is
// neither truncated nor turned into fictitious headers.
TEST_CASE("Folded response header unfolds into a single value", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "ok");
  fx.server.SetResponseHeaders({{"X-Folded", "part1\r\n part2-with:colon"}, {"X-Tabbed", "\tvalue"}});

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  fx.transport->SendFrame(id, Frame("q", true));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  const auto head = fx.observer->HeadOf(id);
  REQUIRE(head.has_value());
  REQUIRE(HeaderValuesOf(head->headers, "x-folded") == std::vector<std::string>{"part1 part2-with:colon"});
  REQUIRE(HeaderValuesOf(head->headers, "x-tabbed") == std::vector<std::string>{"value"});
  // A continuation does not create fictitious names (every name starts with a visible character).
  for (const auto& header : head->headers) {
    REQUIRE(header.first.front() != ' ');
    REQUIRE(header.first.front() != '\t');
  }
}

// The SSE profile: the head is sent and the body has not started yet. The observer needs the head when the header
// block completes, not with the first body byte (Mcp-Session-Id must not wait for an event).
TEST_CASE("Response head arrives when the head block completes before the body", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "event: ready");
  fx.server.HoldBody();

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  fx.transport->SendFrame(id, Frame("q", true));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->HeadOf(id).has_value();
  }));
  // The stream is still open: no body has arrived and there is no close, so the head came ahead of the frame.
  REQUIRE_FALSE(fx.observer->Closed(id));
  REQUIRE(fx.observer->EventsOf(id) == std::vector<std::string>{"head"});
}

TEST_CASE("Interim 1xx block is not a response head on timeout", "[web_transport_curl]") {
  LocalHttpServer server;
  server.SetInterimHeaders({{"X-Interim", "early"}});
  // The main response is stuck: the interim block has arrived and there is no response, so there must be no head.
  server.SetResponse(200, "late");
  server.HoldResponse();
  CurlWebTransportOptions options;
  options.timeout_ms = 300;
  CurlWebTransport transport(server.BaseUrl(), options);
  auto observer = std::make_shared<CapturingObserver>();
  transport.SetObserver(observer);

  const WebStreamId id = transport.OpenStream(WebStreamDescriptor{});
  transport.SendFrame(id, Frame("q", true));
  transport.CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return observer->Closed(id);
  }));
  REQUIRE(observer->CloseError(id).code == WebErrorCode::TIMEOUT);
  REQUIRE_FALSE(observer->HeadOf(id).has_value());
}

TEST_CASE("Interim 1xx headers do not leak into the final head", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetInterimHeaders({{"X-Interim", "early"}});
  fx.server.SetResponse(200, "ok");
  fx.server.SetResponseHeaders({{"X-Final", "done"}});

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  fx.transport->SendFrame(id, Frame("q", true));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  const auto head = fx.observer->HeadOf(id);
  REQUIRE(head.has_value());
  REQUIRE(head->status == 200);
  REQUIRE(HeaderValuesOf(head->headers, "x-final") == std::vector<std::string>{"done"});
  REQUIRE(HeaderValuesOf(head->headers, "x-interim").empty());
}

TEST_CASE("Connect failure maps to TRANSPORT_ERROR", "[web_transport_curl]") {
  // Nobody listens on port 1: connection refused instead of a timeout.
  CurlWebTransport transport("http://127.0.0.1:1", CurlWebTransportOptions{});
  auto observer = std::make_shared<CapturingObserver>();
  transport.SetObserver(observer);
  const WebStreamId id = transport.OpenStream(WebStreamDescriptor{});
  REQUIRE(transport.SendFrame(id, Frame("q", true)));
  transport.CloseSend(id);
  REQUIRE(WaitFor([&]() {
    return observer->Closed(id);
  }));
  REQUIRE(observer->CloseError(id).code == WebErrorCode::TRANSPORT_ERROR);
  // There was no response, so the observer sees no head.
  REQUIRE_FALSE(observer->HeadOf(id).has_value());
}

TEST_CASE("HTTP 401 maps to AUTH_ERROR", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(401, "unauthorized-detail");

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  fx.transport->SendFrame(id, Frame("q", true));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE(fx.observer->CloseError(id).code == WebErrorCode::AUTH_ERROR);
}

TEST_CASE("Observer callbacks run on a single worker thread", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "two parts", /*flush_parts=*/2);

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  fx.transport->SendFrame(id, Frame("q", true));
  fx.transport->CloseSend(id);
  REQUIRE(WaitFor([&fx]() {
    return fx.observer->CallbackCount() >= 3; // at least two frames + the close
  }));

  const auto threads = fx.observer->ThreadIds();
  REQUIRE_FALSE(threads.empty());
  for (const auto& thread_id : threads) {
    REQUIRE(thread_id != std::this_thread::get_id());
    REQUIRE(thread_id == threads.front());
  }
  fx.transport->Shutdown();
}

TEST_CASE("No observer callbacks after Shutdown returns", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "late", /*flush_parts=*/1, /*delay_ms=*/150);

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  fx.transport->SendFrame(id, Frame("q", true));
  fx.transport->CloseSend(id);

  fx.transport->Shutdown(); // cancels in-flight and waits for the worker to exit: silence after it returns
  const size_t after_shutdown = fx.observer->CallbackCount();
  // worker_done is set strictly after all deliveries; this is a tripwire against the "delivery after worker_done"
  // regression: the worker would have enough time to unwind its stack.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  REQUIRE(fx.observer->CallbackCount() == after_shutdown);
}

TEST_CASE("Path pseudo header replaces the base url path", "[web_transport_curl]") {
  TransportFixture fx("/api"); // base with a path prefix
  fx.server.SetResponse(200, "ok");

  const WebStreamId base_id = fx.transport->OpenStream(WebStreamDescriptor{});
  REQUIRE(fx.transport->SendFrame(base_id, Frame("q", true)));
  fx.transport->CloseSend(base_id);
  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(base_id);
  }));

  const WebStreamId path_id = fx.transport->OpenStream(Descriptor("POST", "/resource"));
  REQUIRE(fx.transport->SendFrame(path_id, Frame("q", true)));
  fx.transport->CloseSend(path_id);
  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(path_id);
  }));

  const auto requests = fx.server.TakeRequests();
  REQUIRE(requests.size() == 2);
  CHECK(requests[0].target == "/api");      // without :path: the path of base_url
  CHECK(requests[1].target == "/resource"); // with :path: replacement, not concatenation
}

TEST_CASE("Dead keep-alive replay fails instead of rewinding a consumed body", "[web_transport_curl]") {
  LocalHttpServer server;
  server.SetResponse(200, "ok");
  server.SetCloseAfterResponse(true); // the server drops the connection right after the response
  CurlWebTransport transport(server.BaseUrl(), CurlWebTransportOptions{});
  auto observer = std::make_shared<CapturingObserver>();
  transport.SetObserver(observer);

  const WebStreamId first = transport.OpenStream(WebStreamDescriptor{});
  REQUIRE(transport.SendFrame(first, Frame("body", true)));
  transport.CloseSend(first);
  REQUIRE(WaitFor([&]() {
    return observer->Closed(first);
  }));
  REQUIRE_FALSE(observer->CloseError(first));

  // curl tries to send the second request over the reused (already dead) connection; the frames of the first
  // attempt were consumed destructively, so a rewind is impossible. The contract: the attempt fails with
  // TRANSPORT_ERROR, neither with a hang nor with a "success".
  // Hitting the dead connection relies on the throttling of the liveness check in the curl pool (a connection idle
  // for less than 1 s is considered alive); on other curl builds an EOF detected before the reuse would send the
  // request over a new connection and the test would fail for no reason.
  const WebStreamId second = transport.OpenStream(WebStreamDescriptor{});
  REQUIRE(transport.SendFrame(second, Frame("body2", true)));
  transport.CloseSend(second);
  REQUIRE(WaitFor([&]() {
    return observer->Closed(second);
  }));
  REQUIRE(observer->CloseError(second).code == WebErrorCode::TRANSPORT_ERROR);
}

TEST_CASE("Http errors map to WebError with body", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(500, "boom-detail");

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  fx.transport->SendFrame(id, Frame("q", true));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  const WebError error = fx.observer->CloseError(id);
  REQUIRE(error.code == WebErrorCode::HTTP_ERROR);
  REQUIRE(error.message.find("HTTP 500") == 0);
  REQUIRE(error.message.find("boom-detail") != std::string::npos);
  REQUIRE(fx.observer->FrameCalls(id) == 0);
}

TEST_CASE("Auth errors map to AUTH_ERROR", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(403, "denied");

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  fx.transport->SendFrame(id, Frame("q", true));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE(fx.observer->CloseError(id).code == WebErrorCode::AUTH_ERROR);
}

TEST_CASE("HTTP 3xx maps to HTTP_ERROR instead of success", "[web_transport_curl]") {
  TransportFixture fx;
  // A 3xx body is error diagnostics (in a 301, S3 names the right endpoint): it goes into WebError, like 4xx/5xx
  // bodies, rather than to the observer through OnFrame.
  fx.server.SetResponse(301, "wrong-endpoint");

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  REQUIRE(fx.transport->SendFrame(id, Frame("q", true)));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  const WebError error = fx.observer->CloseError(id);
  REQUIRE(error.code == WebErrorCode::HTTP_ERROR);
  REQUIRE(error.message.find("301") != std::string::npos);
  REQUIRE(error.message.find("redirect") != std::string::npos);
  REQUIRE(error.message.find("wrong-endpoint") != std::string::npos);
  REQUIRE(fx.observer->FrameCalls(id) == 0);
}

TEST_CASE("Sequential streams reuse one connection", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "ok");

  for (int i = 0; i < 3; ++i) {
    const WebStreamId id = fx.transport->OpenStream(Descriptor("PUT", "/obj" + std::to_string(i)));
    REQUIRE(fx.transport->SendFrame(id, Frame("x", true)));
    fx.transport->CloseSend(id);
    REQUIRE(WaitFor([&]() {
      return fx.observer->Closed(id);
    }));
  }

  REQUIRE(WaitFor([&]() {
    return fx.server.RequestCount() == 3;
  }));
  REQUIRE(WaitFor([&]() {
    return fx.server.AcceptedConnections() == 1;
  }));
  REQUIRE(fx.server.AcceptedConnections() == 1);
}

TEST_CASE("Streaming send reaches server before CloseSend", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "done", /*flush_parts=*/1, /*delay_ms=*/100);

  WebStreamDescriptor descriptor = Descriptor("PUT", "/stream");
  descriptor.keep_send_open = true;
  const WebStreamId id = fx.transport->OpenStream(descriptor);
  REQUIRE(id != 0);

  REQUIRE(fx.transport->SendFrame(id, Frame("hello ", false)));
  REQUIRE(WaitFor([&]() {
    return fx.server.BodyBytesSeen() >= 6;
  }));
  REQUIRE(!fx.observer->Closed(id));

  REQUIRE(fx.transport->SendFrame(id, Frame("world", true)));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE_FALSE(fx.observer->CloseError(id));
  REQUIRE(WaitFor([&]() {
    return fx.server.RequestCount() == 1;
  }));
  const auto requests = fx.server.TakeRequests();
  REQUIRE(requests[0].method == "PUT");
  REQUIRE(requests[0].body == "hello world");
}

TEST_CASE("SendFrame after CloseSend is rejected", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "ok");

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  REQUIRE(fx.transport->SendFrame(id, Frame("q", true)));
  fx.transport->CloseSend(id);
  REQUIRE_FALSE(fx.transport->SendFrame(id, Frame("late")));
}

TEST_CASE("CloseStream cancels an in-flight request", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "late");
  fx.server.HoldResponse();

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  fx.transport->SendFrame(id, Frame("q", true));
  fx.transport->CloseSend(id);
  fx.transport->CloseStream(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE(fx.observer->CloseError(id).code == WebErrorCode::CANCELLED);
}

TEST_CASE("Destructor cancels in-flight and joins worker", "[web_transport_curl]") {
  LocalHttpServer server;
  server.SetResponse(200, "late");
  server.HoldResponse();
  auto observer = std::make_shared<CapturingObserver>();
  {
    auto transport = CreateCurlWebTransport(server.BaseUrl());
    REQUIRE(transport != nullptr);
    transport->SetObserver(observer);
    const WebStreamId id = transport->OpenStream(WebStreamDescriptor{});
    transport->SendFrame(id, Frame("q", true));
    transport->CloseSend(id);
  }
  REQUIRE(WaitFor([&]() {
    return observer->AnyClosed();
  }));
  REQUIRE(observer->FirstCloseError().code == WebErrorCode::CANCELLED);
}

TEST_CASE("Shutdown cancels in-flight synchronously and forbids new streams", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "late");
  fx.server.HoldResponse();

  const WebStreamId id = fx.transport->OpenStream(WebStreamDescriptor{});
  fx.transport->SendFrame(id, Frame("q", true));
  fx.transport->CloseSend(id);

  fx.transport->Shutdown();

  REQUIRE(fx.observer->Closed(id));
  REQUIRE(fx.observer->CloseError(id).code == WebErrorCode::CANCELLED);
  REQUIRE(fx.transport->OpenStream(WebStreamDescriptor{}) == 0);
  fx.transport->Shutdown();
}

TEST_CASE("Concurrent Shutdown calls from two threads do not terminate", "[web_transport_curl]") {
  LocalHttpServer server;
  server.SetResponse(200, "late", /*flush_parts=*/1, /*delay_ms=*/300);
  for (int i = 0; i < 25; ++i) {
    auto observer = std::make_shared<CapturingObserver>();
    auto transport = CreateCurlWebTransport(server.BaseUrl());
    transport->SetObserver(observer);
    const WebStreamId id = transport->OpenStream(WebStreamDescriptor{});
    REQUIRE(transport->SendFrame(id, Frame("q", true)));
    transport->CloseSend(id);

    std::atomic<bool> go{false};
    // The promise lives until the detached thread releases it as well: set_value is still running when the future
    // is already ready.
    const auto first_done = std::make_shared<std::promise<void>>();
    const auto second_done = std::make_shared<std::promise<void>>();
    std::thread first([&go, &transport, first_done]() {
      while (!go.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      transport->Shutdown();
      first_done->set_value();
    });
    std::thread second([&go, &transport, second_done]() {
      while (!go.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      transport->Shutdown();
      second_done->set_value();
    });
    first.detach(); // on a deadlock regression the threads hang: fail fast instead of hanging
    second.detach();
    go.store(true, std::memory_order_release);
    auto first_future = first_done->get_future();
    auto second_future = second_done->get_future();
    ExpectCompletes(first_future, "concurrent Shutdown (thread 1)");
    ExpectCompletes(second_future, "concurrent Shutdown (thread 2)");
    transport.reset();
  }
}

TEST_CASE("Observer may vanish before close", "[web_transport_curl]") {
  LocalHttpServer server;
  server.SetResponse(200, "ok");
  auto observer = std::make_shared<CapturingObserver>();
  auto transport = CreateCurlWebTransport(server.BaseUrl());
  REQUIRE(transport != nullptr);
  transport->SetObserver(observer);
  const WebStreamId id = transport->OpenStream(WebStreamDescriptor{});
  transport->SendFrame(id, Frame("q", true));
  transport->CloseSend(id);
  REQUIRE(WaitFor([&]() {
    return observer->Closed(id);
  }));
  transport->SetObserver({});
  transport.reset();
}

namespace {

// The last reference to the transport is dropped right in the callback, on the worker thread of the transport.
struct LastRefDroppingObserver : public IWebTransportObserver {
  explicit LastRefDroppingObserver(std::shared_ptr<CurlWebTransport>& owner)
    : owner_(owner) {}

  void OnFrame(WebStreamId, WebFrame) override {}

  void OnStreamClosed(WebStreamId, WebError) override {
    owner_.get().reset();
    // store comes after reset: the store/load pair also orders the write to owner above for the reader; without it
    // reading owner after the flag would be a race (TSAN).
    closed.store(true);
  }

  std::atomic<bool> closed{false};

private:
  std::reference_wrapper<std::shared_ptr<CurlWebTransport>> owner_;
};

// The first frame parks the worker thread in the callback until the test opens the gate; after that the callback
// calls a nested Shutdown from the worker thread while an outer Shutdown runs in parallel.
struct GatedNestedShutdownObserver : public IWebTransportObserver {
  CurlWebTransport* transport = nullptr; // non-owning: the transport is alive while the test holds it
  std::atomic<bool> in_frame{false};
  std::atomic<bool> gate_open{false};
  std::atomic<bool> nested_shutdown_returned{false};

  void OnFrame(WebStreamId, WebFrame) override {
    in_frame.store(true);
    while (!gate_open.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    transport->Shutdown();
    nested_shutdown_returned.store(true);
  }

  void OnStreamClosed(WebStreamId, WebError) override {}
};

} // namespace

TEST_CASE("Last transport reference may die inside the close callback", "[web_transport_curl]") {
  LocalHttpServer server;
  server.SetResponse(200, "ok");
  std::shared_ptr<CurlWebTransport> owner = CreateCurlWebTransport(server.BaseUrl());
  REQUIRE(owner != nullptr);
  auto observer = std::make_shared<LastRefDroppingObserver>(owner);
  owner->SetObserver(observer);
  const WebStreamId id = owner->OpenStream(WebStreamDescriptor{});
  REQUIRE(owner->SendFrame(id, Frame("q", true)));
  owner->CloseSend(id);
  REQUIRE(WaitFor([&]() {
    return observer->closed.load();
  }));
  REQUIRE(owner == nullptr);

  // The process is alive and the transport layer works: a full round trip on a new transport.
  auto fresh = CreateCurlWebTransport(server.BaseUrl());
  REQUIRE(fresh != nullptr);
  auto fresh_observer = std::make_shared<CapturingObserver>();
  fresh->SetObserver(fresh_observer);
  const WebStreamId fresh_id = fresh->OpenStream(WebStreamDescriptor{});
  REQUIRE(fresh->SendFrame(fresh_id, Frame("ping", true)));
  fresh->CloseSend(fresh_id);
  REQUIRE(WaitFor([&]() {
    return fresh_observer->Closed(fresh_id);
  }));
  REQUIRE_FALSE(fresh_observer->CloseError(fresh_id));
}

TEST_CASE("Shutdown from observer callback does not deadlock with concurrent Shutdown", "[web_transport_curl]") {
  LocalHttpServer server;
  server.SetResponse(200, "two parts", /*flush_parts=*/2);
  auto transport = CreateCurlWebTransport(server.BaseUrl());
  REQUIRE(transport != nullptr);
  auto observer = std::make_shared<GatedNestedShutdownObserver>();
  observer->transport = transport.get();
  transport->SetObserver(observer);
  const WebStreamId id = transport->OpenStream(WebStreamDescriptor{});
  REQUIRE(transport->SendFrame(id, Frame("q", true)));
  transport->CloseSend(id);
  REQUIRE(WaitFor([&]() {
    return observer->in_frame.load();
  }));

  // The promise lives until the detached thread releases it as well: set_value is still running when the future is
  // already ready.
  const auto outer_done = std::make_shared<std::promise<void>>();
  std::thread outer([&transport, outer_done]() {
    transport->Shutdown();
    outer_done->set_value();
  });
  outer.detach(); // on a regression the thread hangs: detach so that a test failure does not turn into terminate
  // stopping is visible as OpenStream being rejected; after that both interleavings are safe: the outer Shutdown
  // waits on a predicate, and a detectable regression is caught by the future timeout.
  REQUIRE(WaitFor([&transport]() {
    return transport->OpenStream(WebStreamDescriptor{}) == 0;
  }));
  observer->gate_open.store(true);

  // On a regression both threads hang and so does the transport teardown, hence the exit without destructors.
  auto outer_future = outer_done->get_future();
  ExpectCompletes(outer_future, "concurrent Shutdown");
  REQUIRE(observer->nested_shutdown_returned.load());
}

namespace {

// All callbacks throw: the transport must catch the exceptions and not bring down the worker or the process.
struct ThrowingObserver : public IWebTransportObserver {
  void OnResponseHead(WebStreamId, WebResponseHead) override {
    throw std::runtime_error("observer head failure");
  }

  void OnFrame(WebStreamId, WebFrame) override {
    throw std::runtime_error("observer frame failure");
  }

  void OnStreamClosed(WebStreamId, WebError) override {
    closed.store(true);
    throw std::runtime_error("observer close failure");
  }

  std::atomic<bool> closed{false};
};

} // namespace

TEST_CASE("Observer exceptions do not kill the worker", "[web_transport_curl]") {
  LocalHttpServer server;
  server.SetResponse(200, "two parts", /*flush_parts=*/2);
  auto transport = CreateCurlWebTransport(server.BaseUrl());
  REQUIRE(transport != nullptr);
  auto observer = std::make_shared<ThrowingObserver>();
  transport->SetObserver(observer);
  const WebStreamId id = transport->OpenStream(WebStreamDescriptor{});
  REQUIRE(transport->SendFrame(id, Frame("q", true)));
  transport->CloseSend(id);
  // The stream completes both after a throwing OnStreamClosed and after throwing OnFrame calls.
  REQUIRE(WaitFor([&observer]() {
    return observer->closed.load();
  }));
  // Shutdown returning means the worker lived until a normal exit instead of dying on an exception.
  transport->Shutdown();
  REQUIRE(transport->OpenStream(WebStreamDescriptor{}) == 0);
  transport.reset();
}

// Not built on Windows: the case runs a child process (posix_spawn + pipe). The header redaction it checks is the
// same on every platform.
#ifndef _WIN32
namespace {

// The path to the test binary itself: the parent test reruns it with a filter for the hidden child case to check
// the real stderr of the child process.
std::string SelfExecutablePath() {
#if defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size); // returns -1 and writes the required size
  if (size == 0) {
    return {};
  }
  std::vector<char> buffer(size);
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return {};
  }
  return buffer.data();
#else
  std::vector<char> buffer(4096);
  const ssize_t n = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (n <= 0) {
    return {};
  }
  buffer[n] = '\0';
  return buffer.data();
#endif
}

} // namespace

TEST_CASE("Verbose debug output redacts sensitive headers", "[web_transport_curl]") {
  // Capturing stderr in the same process is unreliable: the buffering of curl output depends on the environment,
  // and fork() on macOS gets killed by the ObjC runtime. The child process is a rerun of this same binary with the
  // hidden case below; its stderr goes into a pipe.
  const std::string exe = SelfExecutablePath();
  REQUIRE(!exe.empty());

  int pipe_fds[2];
  REQUIRE(pipe(pipe_fds) == 0);
  // ObjC fork safety kills forked children on macOS; a fresh process reads the variable at startup, which is why it
  // is in the spawn environment rather than set with setenv.
  setenv("OBJC_DISABLE_INITIALIZE_FORK_SAFETY", "YES", /*overwrite=*/1);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
  posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);

  const char* const argv[] = {exe.c_str(), "verbose redaction child process", nullptr};
  pid_t child = -1;
  const int spawn_rc = posix_spawn(&child, exe.c_str(), &actions, nullptr, const_cast<char* const*>(argv), environ);
  posix_spawn_file_actions_destroy(&actions);
  REQUIRE(spawn_rc == 0);

  close(pipe_fds[1]);
  int status = 0;
  REQUIRE(waitpid(child, &status, 0) == child);

  std::string captured;
  char buffer[4096];
  ssize_t read_n = 0;
  while ((read_n = read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
    captured.append(buffer, buffer + read_n);
  }
  close(pipe_fds[0]);

  REQUIRE(WIFEXITED(status));
  REQUIRE(WEXITSTATUS(status) == 0);
  REQUIRE(captured.find("POST / HTTP") != std::string::npos); // verbose was really on
  REQUIRE(captured.find("secret-token-xyz") == std::string::npos);
  REQUIRE(captured.find("[redacted]") != std::string::npos);
  REQUIRE(captured.find("content-type") != std::string::npos);
}

// Run only by the parent test above, which reruns the binary with this case name. It has no [web_transport_curl]
// tag: a tag filter in Catch2 matches hidden cases too, so the case would run in a common run with the tag and its
// std::exit(0) would silently swallow the rest of the suite.
TEST_CASE("verbose redaction child process", "[.]") {
  LocalHttpServer server;
  server.SetResponse(200, "ok");
  CurlWebTransportOptions options;
  options.debug = true;
  CurlWebTransport transport(server.BaseUrl(), options);
  auto observer = std::make_shared<CapturingObserver>();
  transport.SetObserver(observer);
  WebStreamDescriptor descriptor = Descriptor();
  descriptor.headers.emplace_back("authorization", "Bearer secret-token-xyz");
  const WebStreamId id = transport.OpenStream(descriptor);
  if (!transport.SendFrame(id, Frame("q", true))) {
    std::exit(1);
  }
  transport.CloseSend(id);
  if (!WaitFor([&observer, id]() {
        return observer->Closed(id);
      })) {
    std::exit(1);
  }
  transport.Shutdown();
  std::exit(0);
}
#endif // _WIN32

namespace {

CurlWebTransportOptions SigV4Options() {
  CurlWebTransportOptions options;
  options.aws_sigv4 = AwsSigV4Params{"us-east-1", "AKIDEXAMPLE", "secretdummy"};
  return options;
}

} // namespace

TEST_CASE("Caller's x-amz-content-sha256 is not duplicated by the internal SigV4 header", "[web_transport_curl]") {
  LocalHttpServer server;
  CurlWebTransport transport(server.BaseUrl(), SigV4Options());

  WebStreamDescriptor descriptor;
  descriptor.headers.emplace_back(":method", "PUT");
  descriptor.headers.emplace_back(":path", "/bucket/obj.bin");
  descriptor.headers.emplace_back("x-amz-content-sha256", "caller-payload-hash");
  const WebStreamId id = transport.OpenStream(descriptor);
  REQUIRE(id != 0);
  REQUIRE(transport.SendFrame(id, Frame("data", true)));
  transport.CloseSend(id);

  REQUIRE(WaitFor([&server]() {
    return server.RequestCount() >= 1;
  }));
  const auto requests = server.TakeRequests();
  REQUIRE(requests.size() == 1);
  int sha_headers = 0;
  std::string sha_value;
  for (const auto& header : requests[0].headers) {
    if (header.first == "x-amz-content-sha256") {
      ++sha_headers;
      sha_value = header.second;
    }
  }
  // A duplicate (the internal UNSIGNED-PAYLOAD + the value of the caller) breaks S3: the signature uses the first
  // match while two headers go on the wire.
  CHECK(sha_headers == 1);
  CHECK(sha_value == "caller-payload-hash");
}

TEST_CASE("Caller's Expect header reaches the wire as the only one", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "ok");
  WebStreamDescriptor descriptor = Descriptor();
  descriptor.headers.emplace_back("Expect", "100-continue");

  const WebStreamId id = fx.transport->OpenStream(descriptor);
  REQUIRE(fx.transport->SendFrame(id, Frame("q", true)));
  fx.transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  const auto requests = fx.server.TakeRequests();
  REQUIRE(requests.size() == 1);
  int expect_headers = 0;
  std::string expect_value;
  for (const auto& header : requests[0].headers) {
    if (header.first == "expect") {
      ++expect_headers;
      expect_value = header.second;
    }
  }
  CHECK(expect_headers == 1);
  CHECK(expect_value == "100-continue");
}

TEST_CASE("curl transport signs PUT with AWS SigV4 when aws_sigv4 option is set", "[web_transport_curl]") {
  LocalHttpServer server;
  CurlWebTransport transport(server.BaseUrl(), SigV4Options());

  const WebStreamDescriptor descriptor{{{":method", "PUT"}, {":path", "/bucket/obj.bin"}}, false};
  const WebStreamId id = transport.OpenStream(descriptor);
  REQUIRE(id != 0);
  WebFrame frame;
  const std::string body(2048, 'x');
  frame.bytes.assign(body.begin(), body.end());
  frame.end_of_stream = true;
  const bool frame_sent = transport.SendFrame(id, std::move(frame));
  REQUIRE(frame_sent);
  transport.CloseSend(id);

  REQUIRE(WaitFor([&server]() {
    return server.RequestCount() >= 1;
  }));
  const auto requests = server.TakeRequests();
  REQUIRE(requests.size() == 1);
  CHECK(requests[0].method == "PUT");
  CHECK(requests[0].target == "/bucket/obj.bin");
  const std::string* authorization = HeaderValueOf(requests[0].headers, "authorization");
  REQUIRE(authorization != nullptr);
  CHECK(authorization->substr(0, 16) == "AWS4-HMAC-SHA256");
  CHECK(HeaderValueOf(requests[0].headers, "x-amz-date") != nullptr);
  const std::string* payload_hash = HeaderValueOf(requests[0].headers, "x-amz-content-sha256");
  REQUIRE(payload_hash != nullptr);
  CHECK(*payload_hash == "UNSIGNED-PAYLOAD");
  size_t hash_headers = 0;
  for (const auto& header : requests[0].headers) {
    hash_headers += header.first == "x-amz-content-sha256" ? 1 : 0;
  }
  CHECK(hash_headers == 1);
}

TEST_CASE("curl transport rejects incomplete aws_sigv4 params", "[web_transport_curl]") {
  CurlWebTransportOptions options;
  options.aws_sigv4 = AwsSigV4Params{"", "key", "secret"};
  REQUIRE_THROWS_AS((CurlWebTransport{"http://127.0.0.1:1", std::move(options)}), std::runtime_error);
}

TEST_CASE("curl transport sends no Authorization without aws_sigv4 option", "[web_transport_curl]") {
  LocalHttpServer server;
  CurlWebTransport transport(server.BaseUrl(), CurlWebTransportOptions{});

  const WebStreamDescriptor descriptor{{{":method", "PUT"}, {":path", "/obj"}}, false};
  const WebStreamId id = transport.OpenStream(descriptor);
  REQUIRE(id != 0);
  WebFrame frame;
  frame.bytes.assign(1, 'x');
  const bool frame_sent = transport.SendFrame(id, std::move(frame));
  REQUIRE(frame_sent);
  transport.CloseSend(id);

  REQUIRE(WaitFor([&server]() {
    return server.RequestCount() >= 1;
  }));
  const auto requests = server.TakeRequests();
  REQUIRE(requests.size() == 1);
  CHECK(HeaderValueOf(requests[0].headers, "authorization") == nullptr);
}

TEST_CASE("Request timeout maps to WebErrorCode TIMEOUT", "[web_transport_curl]") {
  LocalHttpServer server;
  server.SetResponse(200, "late");
  server.HoldResponse();
  auto observer = std::make_shared<CapturingObserver>();
  CurlWebTransportOptions options;
  options.timeout_ms = 200;
  CurlWebTransport transport(server.BaseUrl(), options);
  transport.SetObserver(observer);

  const WebStreamId id = transport.OpenStream(WebStreamDescriptor{});
  REQUIRE(transport.SendFrame(id, Frame("q", true)));
  transport.CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return observer->Closed(id);
  }));
  CHECK(observer->CloseError(id).code == WebErrorCode::TIMEOUT);
}

TEST_CASE("Empty header name rejects the stream", "[web_transport_curl]") {
  TransportFixture fx;
  WebStreamDescriptor descriptor;
  descriptor.headers.emplace_back("", "value");
  REQUIRE(fx.transport->OpenStream(descriptor) == 0);
}

TEST_CASE("Base url without scheme is rejected at construction", "[web_transport_curl]") {
  REQUIRE_THROWS_AS((CurlWebTransport{"s3.example.org:8080/logs", CurlWebTransportOptions{}}), std::runtime_error);
}

TEST_CASE("Base url with unsupported scheme is rejected at construction", "[web_transport_curl]") {
  REQUIRE_THROWS_AS((CurlWebTransport{"ftp://s3.example.org/logs", CurlWebTransportOptions{}}), std::runtime_error);
}

TEST_CASE("max_connections routes concurrent streams through one connection", "[web_transport_curl]") {
  LocalHttpServer server;
  server.SetResponse(200, "ok");
  const auto observer = std::make_shared<CapturingObserver>();
  CurlWebTransportOptions options;
  // No timeout: waiting for a connection in multi already consumes CURLOPT_TIMEOUT_MS.
  options.max_connections = 1;
  CurlWebTransport transport(server.BaseUrl(), options);
  transport.SetObserver(observer);

  const WebStreamId first = transport.OpenStream(Descriptor("PUT", "/one"));
  const WebStreamId second = transport.OpenStream(Descriptor("PUT", "/two"));
  REQUIRE(transport.SendFrame(first, Frame("body-one", true)));
  REQUIRE(transport.SendFrame(second, Frame("body-two", true)));
  transport.CloseSend(first);
  transport.CloseSend(second);

  REQUIRE(WaitFor([&]() {
    return observer->Closed(first) && observer->Closed(second);
  }));
  REQUIRE_FALSE(observer->CloseError(first));
  REQUIRE_FALSE(observer->CloseError(second));
  REQUIRE(WaitFor([&]() {
    return server.RequestCount() == 2;
  }));
  const auto requests = server.TakeRequests();
  REQUIRE(requests.size() == 2);
  REQUIRE(server.AcceptedConnections() == 1);
}

TEST_CASE("upload completes with max_send_speed set", "[web_transport_curl]") {
  TransportFixture fx;
  fx.server.SetResponse(200, "ok");
  CurlWebTransportOptions options;
  options.max_send_speed_bytes_per_sec = 1 << 20;
  const auto transport = CreateCurlWebTransport(fx.server.BaseUrl(), options);
  transport->SetObserver(fx.observer);

  const WebStreamId id = transport->OpenStream(Descriptor("PUT", "/key"));
  REQUIRE(transport->SendFrame(id, Frame("throttled-body", true)));
  transport->CloseSend(id);

  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE_FALSE(fx.observer->CloseError(id));
  REQUIRE(WaitFor([&]() {
    return fx.server.RequestCount() == 1;
  }));
  const auto requests = fx.server.TakeRequests();
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0].body == "throttled-body");
}

TEST_CASE("download completes with max_recv_speed set", "[web_transport_curl]") {
  TransportFixture fx;
  const std::string body(40, 'x');
  fx.server.SetResponse(200, body);
  CurlWebTransportOptions options;
  options.max_recv_speed_bytes_per_sec = 20;
  const auto transport = CreateCurlWebTransport(fx.server.BaseUrl(), options);
  transport->SetObserver(fx.observer);

  const WebStreamId id = transport->OpenStream(Descriptor("GET", "/key"));
  transport->CloseSend(id);
  const auto started = std::chrono::steady_clock::now();
  REQUIRE(WaitFor([&]() {
    return fx.observer->Closed(id);
  }));
  REQUIRE_FALSE(fx.observer->CloseError(id));
  // Only the lower bound is checked: under load the transfer may take longer.
  CHECK(std::chrono::steady_clock::now() - started >= std::chrono::seconds(1));
  REQUIRE(fx.observer->BodyOf(id) == body);
  REQUIRE(WaitFor([&]() {
    return fx.server.RequestCount() == 1;
  }));
  const auto requests = fx.server.TakeRequests();
  REQUIRE(requests.size() == 1);
}

TEST_CASE("CloseStream on a streaming committed stream closes cleanly", "[web_transport_curl]") {
  LocalHttpServer server;
  server.SetResponse(200, "ok");
  auto observer = std::make_shared<CapturingObserver>();
  auto transport = CreateCurlWebTransport(server.BaseUrl());
  REQUIRE(transport != nullptr);
  transport->SetObserver(observer);

  WebStreamDescriptor descriptor;
  descriptor.headers.emplace_back(":method", "PUT");
  descriptor.headers.emplace_back(":path", "/stream");
  descriptor.keep_send_open = true;
  const WebStreamId id = transport->OpenStream(descriptor);
  REQUIRE(id != 0);
  REQUIRE(transport->SendFrame(id, Frame("hello ", false)));
  REQUIRE(WaitFor([&]() {
    return server.BodyBytesSeen() >= 6;
  }));
  REQUIRE(transport->SendFrame(id, Frame("again", false)));
  transport->CloseStream(id);
  REQUIRE(WaitFor([&]() {
    return observer->Closed(id);
  }));
  CHECK(observer->CloseError(id).code == WebErrorCode::CANCELLED);
  transport.reset();
}
