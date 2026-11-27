#include "giga_edge/modules/mcp_http/http_connection.h"

#include <catch.hpp>
#include <json/json.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

using namespace giga_edge;

namespace {

Json::Value Parse(const std::string& text) {
  Json::Value value;
  Json::Reader().parse(text, value);
  return value;
}

struct Reply {
  int32_t status = 200;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  web::WebError error;
};

// Answers each request when its body is complete. Replies come from a separate thread unless
// `sync` is set, which delivers them inside CloseSend, before the caller starts waiting.
class ScriptedTransport : public web::IWebTransport {
public:
  std::function<std::optional<Reply>(const web::WebStreamDescriptor&, const Json::Value&)> handler;
  bool sync = false;
  // Replies accumulate until DeliverPending(), and CloseStream closes the stream only there too: like curl, where
  // a cancel is asynchronous and the head and frames already received arrive before OnStreamClosed.
  bool deferred = false;
  // Every stream gets id 1: an entry left over from the previous stream is visible to the next request.
  bool reuse_ids = false;

  struct Request {
    web::WebStreamDescriptor descriptor;
    Json::Value body;
  };
  std::vector<Request> requests;
  std::vector<web::WebStreamId> cancelled;

  ~ScriptedTransport() override {
    for (auto& t : threads_) {
      t.join();
    }
  }
  // For polling from another thread than the one sending.
  size_t RequestCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests.size();
  }
  void SetObserver(std::weak_ptr<web::IWebTransportObserver> observer) override {
    observer_ = std::move(observer);
  }
  web::WebStreamId OpenStream(const web::WebStreamDescriptor& descriptor) override {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto id = reuse_ids ? 1 : ++next_;
    descriptors_[id] = descriptor;
    bodies_[id].clear();
    return id;
  }
  bool SendFrame(web::WebStreamId id, web::WebFrame frame) override {
    std::lock_guard<std::mutex> lock(mutex_);
    bodies_[id].append(frame.bytes.begin(), frame.bytes.end());
    return true;
  }
  void CloseSend(web::WebStreamId id) override {
    Request request;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      request = {descriptors_[id], Parse(bodies_[id])};
      requests.push_back(request);
    }
    auto reply = handler(request.descriptor, request.body);
    if (!reply) {
      return; // never answered
    }
    if (deferred) {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_.emplace_back(id, *reply);
      return;
    }
    if (sync) {
      Deliver(id, *reply);
    } else {
      threads_.emplace_back([this, id, r = *reply] {
        Deliver(id, r);
      });
    }
  }
  void CloseStream(web::WebStreamId id) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      cancelled.push_back(id);
      if (deferred) {
        return;
      }
    }
    if (auto o = observer_.lock()) {
      o->OnStreamClosed(id, {web::WebErrorCode::CANCELLED, "cancelled"});
    }
  }

  void DeliverPending() {
    std::vector<std::pair<web::WebStreamId, Reply>> pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending.swap(pending_);
    }
    for (const auto& [id, reply] : pending) {
      Deliver(id, reply);
    }
  }

private:
  void Deliver(web::WebStreamId id, const Reply& reply) {
    auto o = observer_.lock();
    if (!o) {
      return;
    }
    // Like curl: a reply with an HTTP error comes with a head, a transport failure without one.
    if (!reply.error || reply.status != 0) {
      o->OnResponseHead(id, {reply.status, reply.headers});
      web::WebFrame frame;
      frame.bytes.assign(reply.body.begin(), reply.body.end());
      frame.end_of_stream = true;
      o->OnFrame(id, std::move(frame));
    }
    o->OnStreamClosed(id, reply.error);
  }

  std::mutex mutex_;
  std::weak_ptr<web::IWebTransportObserver> observer_;
  std::map<web::WebStreamId, web::WebStreamDescriptor> descriptors_;
  std::map<web::WebStreamId, std::string> bodies_;
  web::WebStreamId next_ = 0;
  std::vector<std::thread> threads_;
  std::vector<std::pair<web::WebStreamId, Reply>> pending_;
};

std::string Header(const web::WebStreamDescriptor& d, const std::string& name) {
  for (const auto& [key, value] : d.headers) {
    if (key == name) {
      return value;
    }
  }
  return "<none>";
}

std::string Response(const Json::Value& request, const std::string& result) {
  return R"({"jsonrpc":"2.0","id":")" + request["id"].asString() + R"(","result":)" + result + "}";
}

} // namespace

TEST_CASE("MCP HTTP posts JSON-RPC and remembers session and protocol version", "[mcp_http]") {
  for (const bool sync : {false, true}) {
    INFO("sync=" << sync);
    auto transport = std::make_shared<ScriptedTransport>();
    transport->sync = sync;
    transport->handler = [](const web::WebStreamDescriptor&, const Json::Value& request) -> std::optional<Reply> {
      if (request["method"] == "initialize") {
        return Reply{
          200,
          {{"Content-Type", "application/json"}, {"mcp-session-id", "s-1"}},
          Response(request, R"({"protocolVersion":"2025-06-18"})")
        };
      }
      return Reply{200, {{"content-type", "application/json; charset=utf-8"}}, Response(request, R"({"tools":[]})")};
    };
    auto connection = mcp_http::CreateHttpConnection(transport, {{{"Authorization", "Bearer t"}}});
    std::string id;
    auto init = connection->sendRequest("initialize", Json::Value(Json::objectValue), id, 1000);
    REQUIRE(init.ok());
    CHECK(init.value["protocolVersion"] == "2025-06-18");
    auto list = connection->sendRequest("tools/list", Json::Value(Json::objectValue), id, 1000);
    REQUIRE(list.ok());
    CHECK(list.value["tools"].isArray());

    REQUIRE(transport->requests.size() == 2);
    const auto& first = transport->requests[0];
    CHECK(Header(first.descriptor, ":method") == "POST");
    CHECK(Header(first.descriptor, "Accept") == "application/json, text/event-stream");
    CHECK(Header(first.descriptor, "Authorization") == "Bearer t");
    CHECK(Header(first.descriptor, "Mcp-Session-Id") == "<none>");
    CHECK(first.body["jsonrpc"] == "2.0");
    const auto& second = transport->requests[1];
    CHECK(Header(second.descriptor, "Mcp-Session-Id") == "s-1");
    CHECK(Header(second.descriptor, "MCP-Protocol-Version") == "2025-06-18");
    CHECK(second.body["id"] == id);
  }
}

TEST_CASE("MCP HTTP reads SSE replies and hands notifications to the handler", "[mcp_http]") {
  auto transport = std::make_shared<ScriptedTransport>();
  // The response is split over two data: lines, which SSE joins with '\n'.
  transport->handler = [](const web::WebStreamDescriptor&, const Json::Value& request) -> std::optional<Reply> {
    const auto response = Response(request, R"({"content":[{"type":"text","text":"ok"}]})");
    const auto split = response.find("\"result\"");
    return Reply{
      200,
      {{"Content-Type", "text/event-stream"}},
      "event: message\r\ndata: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\",\"params\":{\"p\":1}}\r\n\r\n"
      ": keep-alive comment\n\n"
      "data: "
        + response.substr(0, split) + "\ndata: " + response.substr(split) + "\n\n"
    };
  };
  auto connection = mcp_http::CreateHttpConnection(transport);
  std::vector<std::string> seen;
  connection->setNotificationHandler([&](const std::string& method, const Json::Value& params) {
    seen.push_back(method + ":" + params["p"].asString());
  });
  std::string id;
  const auto result = connection->sendRequest("tools/call", Json::Value(Json::objectValue), id, 1000);
  REQUIRE(result.ok());
  CHECK(result.value["content"][0]["text"] == "ok");
  CHECK(seen == std::vector<std::string>{"notifications/progress:1"});
}

TEST_CASE("MCP HTTP maps JSON-RPC, HTTP and missing-response failures to errors", "[mcp_http]") {
  auto transport = std::make_shared<ScriptedTransport>();
  std::string mode;
  transport->handler = [&](const web::WebStreamDescriptor&, const Json::Value& request) -> std::optional<Reply> {
    if (mode == "rpc") {
      return Reply{
        200,
        {{"Content-Type", "application/json"}},
        R"({"jsonrpc":"2.0","id":")" + request["id"].asString()
          + R"(","error":{"code":-32602,"message":"bad","data":{"x":1}}})"
      };
    }
    if (mode == "http") {
      return Reply{0, {}, "", {web::WebErrorCode::AUTH_ERROR, "401 Unauthorized"}};
    }
    if (mode == "other_id") {
      return Reply{200, {{"Content-Type", "application/json"}}, R"({"jsonrpc":"2.0","id":"x","result":{}})"};
    }
    return Reply{200, {{"Content-Type", "application/json"}}, "{not json"};
  };
  auto connection = mcp_http::CreateHttpConnection(transport);
  std::string id;
  mode = "rpc";
  auto r = connection->sendRequest("tools/call", {}, id, 1000);
  CHECK(r.error.code == mcp::ErrorCode::INVALID_PARAMS);
  CHECK(r.error.message == "bad");
  CHECK(r.error.data["x"] == 1);
  mode = "http";
  r = connection->sendRequest("tools/call", {}, id, 1000);
  CHECK(r.error.code == mcp::ErrorCode::TRANSPORT_ERROR);
  CHECK(r.error.message.find("401") != std::string::npos);
  mode = "other_id";
  CHECK(connection->sendRequest("tools/call", {}, id, 1000).error.code == mcp::ErrorCode::TRANSPORT_ERROR);
  mode = "garbage";
  CHECK(connection->sendRequest("tools/call", {}, id, 1000).error.code == mcp::ErrorCode::TRANSPORT_ERROR);
  CHECK(connection->isConnected());
}

TEST_CASE("MCP HTTP times out, cancels the stream and survives its late reply", "[mcp_http]") {
  auto transport = std::make_shared<ScriptedTransport>();
  transport->handler = [](const web::WebStreamDescriptor&, const Json::Value&) -> std::optional<Reply> {
    return std::nullopt;
  };
  auto connection = mcp_http::CreateHttpConnection(transport);
  std::string id;
  const auto r = connection->sendRequest("tools/call", {}, id, 20);
  CHECK(r.error.code == mcp::ErrorCode::TIMEOUT);
  CHECK(transport->cancelled.size() == 1);
}

TEST_CASE("MCP HTTP notifications do not wait for a reply", "[mcp_http]") {
  auto transport = std::make_shared<ScriptedTransport>();
  transport->handler = [](const web::WebStreamDescriptor&, const Json::Value&) -> std::optional<Reply> {
    return Reply{202, {}, ""};
  };
  auto connection = mcp_http::CreateHttpConnection(transport);
  connection->sendNotification("notifications/initialized", Json::Value(Json::objectValue));
  REQUIRE(transport->requests.size() == 1);
  CHECK(transport->requests[0].body["method"] == "notifications/initialized");
  CHECK_FALSE(transport->requests[0].body.isMember("id"));
}

TEST_CASE("MCP HTTP close wakes a waiting request", "[mcp_http]") {
  auto transport = std::make_shared<ScriptedTransport>();
  transport->handler = [](const web::WebStreamDescriptor&, const Json::Value&) -> std::optional<Reply> {
    return std::nullopt;
  };
  auto connection = mcp_http::CreateHttpConnection(transport);
  mcp::Result<Json::Value> result;
  std::thread waiter([&] {
    std::string id;
    result = connection->sendRequest("tools/call", {}, id, 10000);
  });
  while (transport->RequestCount() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto started = std::chrono::steady_clock::now();
  connection->close();
  waiter.join();
  CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(2));
  CHECK_FALSE(result.ok());
  CHECK_FALSE(connection->isConnected());
  std::string id;
  CHECK(connection->sendRequest("tools/list", {}, id, 100).error.code == mcp::ErrorCode::TRANSPORT_ERROR);
}

TEST_CASE("MCP HTTP gate sees the request id and a closed gate posts nothing", "[mcp_http]") {
  auto transport = std::make_shared<ScriptedTransport>();
  transport->handler = [](const web::WebStreamDescriptor&, const Json::Value& request) -> std::optional<Reply> {
    return Reply{200, {{"Content-Type", "application/json"}}, Response(request, "{}")};
  };
  auto connection = mcp_http::CreateHttpConnection(transport);
  std::string id, gated;
  const auto closed = connection->sendRequestWithGate("tools/call", {}, id, 1000, [&](const std::string& rid) {
    gated = rid;
    return false;
  });
  CHECK(closed.error.code == mcp::ErrorCode::CANCELLED);
  CHECK_FALSE(gated.empty());
  CHECK(gated == id);
  CHECK(transport->requests.empty());

  const auto open = connection->sendRequestWithGate("tools/call", {}, id, 1000, [](const std::string&) {
    return true;
  });
  CHECK(open.ok());
  CHECK(transport->requests.size() == 1);
}

TEST_CASE("MCP HTTP abandonRequest wakes the waiter with CANCELLED and closes its stream", "[mcp_http]") {
  auto transport = std::make_shared<ScriptedTransport>();
  transport->handler = [](const web::WebStreamDescriptor&, const Json::Value&) -> std::optional<Reply> {
    return std::nullopt;
  };
  auto connection = mcp_http::CreateHttpConnection(transport);
  connection->abandonRequest("unknown"); // no-op
  std::mutex mutex;
  std::string id;
  mcp::Result<Json::Value> result;
  std::thread waiter([&] {
    std::string out;
    result = connection->sendRequestWithGate("tools/call", {}, out, 10000, [&](const std::string& rid) {
      std::lock_guard<std::mutex> lock(mutex);
      id = rid;
      return true;
    });
  });
  while (transport->RequestCount() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto started = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(mutex);
    connection->abandonRequest(id);
  }
  waiter.join();
  CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(2));
  CHECK(result.error.code == mcp::ErrorCode::CANCELLED);
  CHECK(transport->cancelled.size() == 1);
  CHECK(connection->isConnected());
  connection->close();
  connection->abandonRequest(id); // safe after close
}

TEST_CASE("MCP HTTP abandonRequest racing the gate still cancels the request", "[mcp_http]") {
  auto transport = std::make_shared<ScriptedTransport>();
  transport->handler = [](const web::WebStreamDescriptor&, const Json::Value&) -> std::optional<Reply> {
    return std::nullopt;
  };
  auto connection = mcp_http::CreateHttpConnection(transport);
  std::string id;
  const auto started = std::chrono::steady_clock::now();
  // Abandoned after the gate opened but before the reply wait: the wait must not run to its timeout.
  const auto r = connection->sendRequestWithGate("tools/call", {}, id, 10000, [&](const std::string& rid) {
    connection->abandonRequest(rid);
    return true;
  });
  CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(2));
  CHECK(r.error.code == mcp::ErrorCode::CANCELLED);
}

TEST_CASE("MCP HTTP fails a response over max_response_bytes and stays usable", "[mcp_http]") {
  constexpr size_t limit = 200;
  for (const bool sync : {false, true}) {
    INFO("sync=" << sync);
    auto transport = std::make_shared<ScriptedTransport>();
    transport->sync = sync;
    size_t size = 0;
    transport->handler = [&](const web::WebStreamDescriptor&, const Json::Value& request) -> std::optional<Reply> {
      // The body is exactly size bytes.
      const auto base = Response(request, R"({"p":""})");
      return Reply{
        200,
        {{"Content-Type", "application/json"}},
        Response(request, R"({"p":")" + std::string(size - base.size(), 'x') + R"("})")
      };
    };
    mcp_http::HttpConnectionOptions options;
    options.max_response_bytes = limit;
    auto connection = mcp_http::CreateHttpConnection(transport, options);
    std::string id;
    size = limit + 1;
    const auto over = connection->sendRequest("tools/call", {}, id, 1000);
    CHECK(over.error.code == mcp::ErrorCode::TRANSPORT_ERROR);
    CHECK(over.error.message.find("exceeds 200 bytes") != std::string::npos);
    size = limit;
    const auto exact = connection->sendRequest("tools/call", {}, id, 1000);
    REQUIRE(exact.ok());
    CHECK(connection->isConnected());
  }
}

TEST_CASE("MCP HTTP 404 to a request with the session closes the connection and reports it once", "[mcp_http]") {
  auto transport = std::make_shared<ScriptedTransport>();
  transport->handler = [](const web::WebStreamDescriptor&, const Json::Value& request) -> std::optional<Reply> {
    if (request["method"] == "initialize") {
      return Reply{200, {{"Content-Type", "application/json"}, {"Mcp-Session-Id", "s-1"}}, Response(request, "{}")};
    }
    return Reply{404, {}, "", {web::WebErrorCode::HTTP_ERROR, "HTTP 404: session not found"}};
  };
  for (const bool handler_first : {true, false}) {
    INFO("handler_first=" << handler_first);
    auto connection = mcp_http::CreateHttpConnection(transport);
    int closed = 0;
    const auto count = [&] {
      ++closed;
    };
    if (handler_first) {
      connection->setCloseHandler(count);
    }
    std::string id;
    // Without a session a 404 is an ordinary error: the connection stays alive.
    CHECK(connection->sendRequest("tools/list", {}, id, 1000).error.code == mcp::ErrorCode::TRANSPORT_ERROR);
    CHECK(connection->isConnected());
    CHECK(closed == 0);
    REQUIRE(connection->sendRequest("initialize", {}, id, 1000).ok());
    const auto expired = connection->sendRequest("tools/list", {}, id, 1000);
    CHECK(expired.error.code == mcp::ErrorCode::TRANSPORT_ERROR);
    CHECK(expired.error.message.find("session expired") != std::string::npos);
    CHECK_FALSE(connection->isConnected());
    if (!handler_first) {
      CHECK(closed == 0);
      connection->setCloseHandler(count); // closed earlier: reported right away
    }
    CHECK(closed == 1);
    connection->setCloseHandler(count);
    connection->close();
    CHECK(closed == 1);
    const auto posted = transport->RequestCount();
    CHECK(connection->sendRequest("tools/list", {}, id, 1000).error.code == mcp::ErrorCode::TRANSPORT_ERROR);
    CHECK(transport->RequestCount() == posted);
  }
}

TEST_CASE("MCP HTTP close by the owner is not reported", "[mcp_http]") {
  auto transport = std::make_shared<ScriptedTransport>();
  auto connection = mcp_http::CreateHttpConnection(transport);
  int closed = 0;
  connection->setCloseHandler([&] {
    ++closed;
  });
  connection->close();
  connection->setCloseHandler([&] {
    ++closed;
  });
  CHECK(closed == 0);
}

TEST_CASE("MCP HTTP close waits for a notification being delivered on the request thread", "[mcp_http]") {
  auto transport = std::make_shared<ScriptedTransport>();
  transport->handler = [](const web::WebStreamDescriptor&, const Json::Value& request) -> std::optional<Reply> {
    return Reply{
      200,
      {{"Content-Type", "text/event-stream"}},
      "data: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\",\"params\":{}}\n\n"
      "data: "
        + Response(request, "{}") + "\n\n"
    };
  };
  auto connection = mcp_http::CreateHttpConnection(transport);
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false, released = false, close_returned = false;
  int order = 0, handler_end = 0, close_end = 0;
  connection->setNotificationHandler([&](const std::string&, const Json::Value&) {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    cv.notify_all();
    cv.wait(lock, [&] {
      return released;
    });
    handler_end = ++order;
  });
  std::thread request([&] {
    std::string id;
    connection->sendRequest("tools/call", {}, id, 10000);
  });
  {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&] {
      return entered;
    });
  }
  std::thread closer([&] {
    connection->close();
    std::lock_guard<std::mutex> lock(mutex);
    close_returned = true;
    close_end = ++order;
    cv.notify_all();
  });
  {
    // The time only gives a wrong close() a chance to return earlier; a correct one waits for the handler.
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::milliseconds(200), [&] {
      return close_returned;
    });
    released = true;
    cv.notify_all();
  }
  request.join();
  closer.join();
  CHECK(handler_end < close_end);
}

TEST_CASE("MCP HTTP close from the notification handler does not wait for itself", "[mcp_http]") {
  auto transport = std::make_shared<ScriptedTransport>();
  transport->handler = [](const web::WebStreamDescriptor&, const Json::Value& request) -> std::optional<Reply> {
    return Reply{
      200,
      {{"Content-Type", "text/event-stream"}},
      "data: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/a\",\"params\":{}}\n\n"
      "data: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/b\",\"params\":{}}\n\n"
      "data: "
        + Response(request, "{}") + "\n\n"
    };
  };
  auto connection = mcp_http::CreateHttpConnection(transport);
  std::vector<std::string> seen;
  connection->setNotificationHandler([&](const std::string& method, const Json::Value&) {
    seen.push_back(method);
    connection->close();
  });
  std::string id;
  CHECK(connection->sendRequest("tools/call", {}, id, 1000).ok());
  // After close() the second notification is not delivered.
  CHECK(seen == std::vector<std::string>{"notifications/a"});
  CHECK_FALSE(connection->isConnected());
}

TEST_CASE("MCP HTTP late callbacks of abandoned streams leave nothing behind", "[mcp_http]") {
  auto transport = std::make_shared<ScriptedTransport>();
  transport->deferred = true;
  transport->reuse_ids = true;
  transport->handler = [](const web::WebStreamDescriptor&, const Json::Value& request) -> std::optional<Reply> {
    if (!request.isMember("id")) {
      return Reply{202, {}, ""};
    }
    return Reply{200, {{"Content-Type", "application/json"}}, Response(request, R"({"late":true})")};
  };
  auto connection = mcp_http::CreateHttpConnection(transport);
  // The notification is abandoned right away, the 202 arrives later.
  connection->sendNotification("notifications/initialized", Json::Value(Json::objectValue));
  transport->DeliverPending();
  // A request with the same stream id: an entry left over from the notification would answer it at once instead of
  // letting it time out.
  std::string id;
  CHECK(connection->sendRequest("tools/call", {}, id, 20).error.code == mcp::ErrorCode::TIMEOUT);
  transport->DeliverPending(); // a late reply after the timeout
  transport->deferred = false;
  const auto next = connection->sendRequest("tools/list", {}, id, 1000);
  INFO(next.error.message);
  REQUIRE(next.ok());
  CHECK(next.value["late"] == true);
}

TEST_CASE("MCP HTTP close from handlers on two threads does not deadlock", "[mcp_http]") {
  auto transport = std::make_shared<ScriptedTransport>();
  transport->sync = true; // reply on the request thread: the fake spawns no threads for two concurrent requests
  transport->handler = [](const web::WebStreamDescriptor&, const Json::Value& request) -> std::optional<Reply> {
    return Reply{
      200,
      {{"Content-Type", "text/event-stream"}},
      "data: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\",\"params\":{}}\n\n"
      "data: "
        + Response(request, "{}") + "\n\n"
    };
  };
  auto connection = mcp_http::CreateHttpConnection(transport);
  std::mutex mutex;
  std::condition_variable cv;
  int inside = 0;
  connection->setNotificationHandler([&](const std::string&, const Json::Value&) {
    {
      // Both handlers are inside before either one closes.
      std::unique_lock<std::mutex> lock(mutex);
      ++inside;
      cv.notify_all();
      cv.wait(lock, [&] {
        return inside == 2;
      });
    }
    connection->close();
  });
  std::vector<std::thread> requests;
  requests.reserve(2);
  for (int i = 0; i < 2; ++i) {
    requests.emplace_back([&] {
      std::string id;
      connection->sendRequest("tools/call", {}, id, 10000);
    });
  }
  for (auto& t : requests) {
    t.join();
  }
  CHECK_FALSE(connection->isConnected());
}

TEST_CASE("MCP HTTP close waits for the close handler called on the request thread", "[mcp_http]") {
  auto transport = std::make_shared<ScriptedTransport>();
  transport->handler = [](const web::WebStreamDescriptor&, const Json::Value& request) -> std::optional<Reply> {
    if (request["method"] == "initialize") {
      return Reply{200, {{"Content-Type", "application/json"}, {"Mcp-Session-Id", "s-1"}}, Response(request, "{}")};
    }
    return Reply{404, {}, "", {web::WebErrorCode::HTTP_ERROR, "HTTP 404"}};
  };
  auto connection = mcp_http::CreateHttpConnection(transport);
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false, released = false, close_returned = false;
  int order = 0, handler_end = 0, close_end = 0;
  connection->setCloseHandler([&] {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    cv.notify_all();
    cv.wait(lock, [&] {
      return released;
    });
    handler_end = ++order;
  });
  std::string id;
  REQUIRE(connection->sendRequest("initialize", {}, id, 1000).ok());
  std::thread request([&] {
    std::string out;
    connection->sendRequest("tools/list", {}, out, 10000);
  });
  {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&] {
      return entered;
    });
  }
  std::thread closer([&] {
    connection->close();
    std::lock_guard<std::mutex> lock(mutex);
    close_returned = true;
    close_end = ++order;
    cv.notify_all();
  });
  {
    // The time only gives a wrong close() a chance to return earlier.
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::milliseconds(200), [&] {
      return close_returned;
    });
    released = true;
    cv.notify_all();
  }
  request.join();
  closer.join();
  CHECK(handler_end < close_end);
}
