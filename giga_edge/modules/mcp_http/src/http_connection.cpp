#include "giga_edge/modules/mcp_http/http_connection.h"

#include "giga_edge/modules/assistant_core/json.h"

#include <json/json.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace giga_edge {
namespace mcp_http {
namespace {

using web::WebStreamId;

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

const std::string* FindHeader(const web::WebResponseHead& head, const std::string& name) {
  for (const auto& [key, value] : head.headers) {
    if (Lower(key) == name) {
      return &value;
    }
  }
  return nullptr;
}

// JSON-RPC messages of one reply body: a JSON object or batch, or the data of SSE events.
std::vector<Json::Value> ParseMessages(const std::string* content_type, const std::string& body) {
  std::vector<Json::Value> out;
  const auto append = [&](const std::string& text) {
    auto value = ParseJsonOrThrow(text);
    if (value.isArray()) {
      for (auto& message : value) {
        out.push_back(std::move(message));
      }
    } else {
      out.push_back(std::move(value));
    }
  };
  if (content_type && Lower(*content_type).find("text/event-stream") != std::string::npos) {
    std::istringstream in(body);
    std::string line, data;
    const auto flush = [&] {
      if (!data.empty()) {
        append(data);
      }
      data.clear();
    };
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line.empty()) {
        flush();
      } else if (line.rfind("data:", 0) == 0) {
        auto chunk = line.substr(5);
        if (!chunk.empty() && chunk.front() == ' ') {
          chunk.erase(0, 1);
        }
        data += data.empty() ? chunk : "\n" + chunk;
      }
    }
    flush();
  } else if (body.find_first_not_of(" \t\r\n") != std::string::npos) {
    append(body);
  }
  return out;
}

std::string IdToString(const Json::Value& id) {
  if (id.isString()) {
    return id.asString();
  }
  if (id.isIntegral()) {
    return std::to_string(id.asLargestInt());
  }
  return {};
}

mcp::Error MakeError(mcp::ErrorCode code, std::string message) {
  mcp::Error error;
  error.code = code;
  error.message = std::move(message);
  return error;
}

mcp::Error FromJsonRpc(const Json::Value& value) {
  if (!value.isObject()) {
    return MakeError(mcp::ErrorCode::INTERNAL_ERROR, "MCP server returned an invalid JSON-RPC error");
  }
  const auto code = value.get("code", static_cast<int>(mcp::ErrorCode::INTERNAL_ERROR)).asInt();
  auto error = MakeError(mcp::ErrorCode::SERVER_ERROR, value.get("message", "").asString());
  for (const auto known :
       {mcp::ErrorCode::INVALID_REQUEST,
        mcp::ErrorCode::METHOD_NOT_FOUND,
        mcp::ErrorCode::INVALID_PARAMS,
        mcp::ErrorCode::INTERNAL_ERROR}) {
    if (code == static_cast<int>(known)) {
      error.code = known;
    }
  }
  error.data = value.isMember("data") ? value["data"] : Json::Value();
  return error;
}

mcp::Error FromWeb(const web::WebError& error) {
  switch (error.code) {
    case web::WebErrorCode::TIMEOUT:
      return MakeError(mcp::ErrorCode::TIMEOUT, "MCP HTTP: " + error.message);
    case web::WebErrorCode::CANCELLED:
      return MakeError(mcp::ErrorCode::CANCELLED, "MCP HTTP: " + error.message);
    default:
      return MakeError(mcp::ErrorCode::TRANSPORT_ERROR, "MCP HTTP: " + error.message);
  }
}

// Transport callbacks run on the transport's thread and may arrive before OpenStream returns to
// the caller, so exchanges are created on first touch. Streams the caller gave up on are
// "abandoned": their callbacks are dropped until the transport closes them.
struct Exchanges : web::IWebTransportObserver {
  struct Exchange {
    web::WebResponseHead head;
    std::string body;
    bool closed = false;
    bool overflow = false; // body exceeds max_body: the accumulated part is dropped, the waiter closes the stream
    web::WebError error;
  };

  explicit Exchanges(size_t max_body)
    : max_body(max_body) {}

  const size_t max_body;
  std::mutex mutex;
  std::condition_variable cv;
  std::map<WebStreamId, Exchange> streams;
  std::set<WebStreamId> abandoned;
  // Requests waiting for a reply: request id -> abandoned by abandonRequest.
  std::map<std::string, bool> requests;

  void OnResponseHead(WebStreamId id, web::WebResponseHead head) override {
    std::lock_guard<std::mutex> lock(mutex);
    if (!abandoned.count(id)) {
      streams[id].head = std::move(head);
    }
  }
  void OnFrame(WebStreamId id, web::WebFrame frame) override {
    std::lock_guard<std::mutex> lock(mutex);
    if (abandoned.count(id)) {
      return;
    }
    auto& exchange = streams[id];
    if (exchange.overflow) {
      return;
    }
    if (exchange.body.size() + frame.bytes.size() > max_body) {
      exchange.overflow = true;
      std::string().swap(exchange.body);
      cv.notify_all();
      return;
    }
    exchange.body.append(frame.bytes.begin(), frame.bytes.end());
  }
  void OnStreamClosed(WebStreamId id, web::WebError error) override {
    std::lock_guard<std::mutex> lock(mutex);
    if (abandoned.erase(id)) {
      return;
    }
    auto& exchange = streams[id];
    exchange.closed = true;
    exchange.error = std::move(error);
    cv.notify_all();
  }
  // Caller holds the lock.
  void AbandonLocked(WebStreamId id) {
    const auto it = streams.find(id);
    if (it == streams.end() || !it->second.closed) {
      abandoned.insert(id);
    }
    if (it != streams.end()) {
      streams.erase(it);
    }
  }
};

class HttpConnection : public mcp::IConnection {
public:
  HttpConnection(web::WebTransportPtr transport, HttpConnectionOptions options)
    : transport_(std::move(transport))
    , options_(std::move(options))
    , exchanges_(std::make_shared<Exchanges>(options_.max_response_bytes)) {
    transport_->SetObserver(exchanges_);
  }

  ~HttpConnection() override {
    HttpConnection::close();
  }

  mcp::Result<Json::Value> sendRequest(
    const std::string& method, const Json::Value& params, std::string& out_request_id, uint32_t timeout_ms
  ) override {
    return sendRequestWithGate(method, params, out_request_id, timeout_ms, {});
  }

  mcp::Result<Json::Value> sendRequestWithGate(
    const std::string& method,
    const Json::Value& params,
    std::string& out_request_id,
    uint32_t timeout_ms,
    const SendGate& gate
  ) override {
    mcp::Result<Json::Value> result;
    if (timeout_ms == 0) {
      result.error = MakeError(mcp::ErrorCode::INVALID_PARAMS, "MCP transport timeout must be positive");
      return result;
    }
    out_request_id = "http-mcp-" + std::to_string(next_id_++);
    Json::Value message;
    message["jsonrpc"] = "2.0";
    message["id"] = out_request_id;
    message["method"] = method;
    message["params"] = params;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    const auto& id = out_request_id;
    {
      // Registered before the gate, so an abandonRequest racing the gate is not lost.
      std::lock_guard<std::mutex> lock(exchanges_->mutex);
      exchanges_->requests[id] = false;
    }
    const auto forget = [&] {
      std::lock_guard<std::mutex> lock(exchanges_->mutex);
      exchanges_->requests.erase(id);
    };
    // The gate runs outside our lock: it may take the caller's locks, and abandonRequest takes ours.
    if (gate && !gate(id)) {
      forget();
      result.error = MakeError(mcp::ErrorCode::CANCELLED, "MCP request cancelled before sending");
      return result;
    }
    bool with_session = false;
    const auto stream = Post(message, &with_session);
    if (!stream) {
      forget();
      result.error = MakeError(mcp::ErrorCode::TRANSPORT_ERROR, "MCP HTTP request could not be sent");
      return result;
    }

    Exchanges::Exchange exchange;
    {
      std::unique_lock<std::mutex> lock(exchanges_->mutex);
      const auto abandoned = [&] {
        return exchanges_->requests[id];
      };
      const bool done = exchanges_->cv.wait_until(lock, deadline, [&] {
        const auto it = exchanges_->streams.find(stream);
        return !connected_ || abandoned()
          || (it != exchanges_->streams.end() && (it->second.closed || it->second.overflow));
      });
      const bool cancelled = abandoned();
      exchanges_->requests.erase(id);
      const auto it = exchanges_->streams.find(stream);
      const bool overflow = it != exchanges_->streams.end() && it->second.overflow;
      if (!done || cancelled || overflow || it == exchanges_->streams.end() || !it->second.closed) {
        const bool open = it == exchanges_->streams.end() || !it->second.closed;
        exchanges_->AbandonLocked(stream);
        lock.unlock();
        if (open) {
          transport_->CloseStream(stream);
        }
        if (!done) {
          result.error = MakeError(mcp::ErrorCode::TIMEOUT, "Timed out waiting for MCP HTTP response");
        } else if (cancelled) {
          result.error = MakeError(mcp::ErrorCode::CANCELLED, "MCP HTTP request abandoned");
        } else if (overflow) {
          result.error = MakeError(
            mcp::ErrorCode::TRANSPORT_ERROR,
            "MCP HTTP response exceeds " + std::to_string(options_.max_response_bytes) + " bytes"
          );
        } else {
          result.error = MakeError(mcp::ErrorCode::TRANSPORT_ERROR, "MCP HTTP connection closed");
        }
        return result;
      }
      exchange = std::move(it->second);
      exchanges_->streams.erase(it);
    }
    if (exchange.error) {
      result.error = FromWeb(exchange.error);
      if (with_session && exchange.head.status == 404) {
        // The server does not know the session (for example, it restarted): the specification requires a new one
        // through initialize, so the connection is treated as closed by the server and the owner drops the server.
        result.error.message = "MCP HTTP session expired: " + exchange.error.message;
        CloseByServer();
      }
      return result;
    }

    std::vector<Json::Value> notifications;
    bool answered = false;
    try {
      if (const auto* session = FindHeader(exchange.head, "mcp-session-id")) {
        std::lock_guard<std::mutex> lock(mutex_);
        session_id_ = *session;
      }
      for (auto& reply : ParseMessages(FindHeader(exchange.head, "content-type"), exchange.body)) {
        if (!reply.isObject()) {
          continue;
        }
        if (reply.isMember("id") && (reply.isMember("result") || reply.isMember("error"))) {
          if (IdToString(reply["id"]) != out_request_id) {
            continue;
          }
          answered = true;
          if (reply.isMember("error")) {
            result.error = FromJsonRpc(reply["error"]);
          } else {
            result.value = std::move(reply["result"]);
          }
        } else if (reply["method"].isString() && !reply.isMember("id")) {
          notifications.push_back(std::move(reply));
        }
        // Server-to-client requests (sampling, elicitation) are not supported: they are ignored.
      }
    } catch (const std::exception& e) {
      result.error = MakeError(mcp::ErrorCode::TRANSPORT_ERROR, std::string("Invalid MCP HTTP reply: ") + e.what());
      return result;
    }
    if (!answered) {
      result.error = MakeError(mcp::ErrorCode::TRANSPORT_ERROR, "MCP HTTP reply has no response to the request");
    } else if (method == "initialize" && result.ok() && result.value["protocolVersion"].isString()) {
      std::lock_guard<std::mutex> lock(mutex_);
      protocol_version_ = result.value["protocolVersion"].asString();
    }
    Deliver(notifications);
    return result;
  }

  void sendNotification(const std::string& method, const Json::Value& params) override {
    Json::Value message;
    message["jsonrpc"] = "2.0";
    message["method"] = method;
    message["params"] = params;
    // The server answers 202 Accepted without a body; nothing to wait for.
    if (const auto stream = Post(message)) {
      std::lock_guard<std::mutex> lock(exchanges_->mutex);
      exchanges_->AbandonLocked(stream);
    }
  }

  void setNotificationHandler(NotificationHandler handler) override {
    std::lock_guard<std::mutex> lock(mutex_);
    handler_ = std::move(handler);
  }

  // Wakes the waiter; it closes the stream itself, so this never blocks on the transport.
  void abandonRequest(const std::string& request_id) override {
    std::lock_guard<std::mutex> lock(exchanges_->mutex);
    const auto it = exchanges_->requests.find(request_id);
    if (it != exchanges_->requests.end()) {
      it->second = true;
      exchanges_->cv.notify_all();
    }
  }

  void setCloseHandler(CloseHandler handler) override {
    CloseHandler closed_already;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (handler && closed_by_server_ && !close_reported_) {
        close_reported_ = true;
        closed_already = std::move(handler);
      } else {
        close_handler_ = std::move(handler);
      }
    }
    if (closed_already) {
      closed_already();
    }
  }

  // The handlers (for notifications and for the close by the server) are called on the request thread: they do not
  // run after close() returns. close() called from a handler waits for nothing: otherwise two such close() calls on
  // different threads would wait for each other.
  void close() override {
    Shutdown();
    std::unique_lock<std::mutex> lock(mutex_);
    if (std::find(callbacks_.begin(), callbacks_.end(), std::this_thread::get_id()) != callbacks_.end()) {
      return;
    }
    callbacks_cv_.wait(lock, [&] {
      return callbacks_.empty();
    });
  }

  bool isConnected() const override {
    return connected_;
  }

private:
  // false: the connection was already closed.
  bool Shutdown() {
    std::vector<WebStreamId> open;
    {
      std::lock_guard<std::mutex> lock(exchanges_->mutex);
      if (!connected_.exchange(false)) {
        return false;
      }
      for (const auto& [id, exchange] : exchanges_->streams) {
        if (!exchange.closed) {
          open.push_back(id);
        }
      }
      exchanges_->cv.notify_all();
    }
    for (const auto id : open) {
      transport_->CloseStream(id);
    }
    return true;
  }

  void CloseByServer() {
    {
      // The mark is set before Shutdown: a close() that returned earlier has already closed the connection, and the
      // handler will not be called.
      std::lock_guard<std::mutex> lock(mutex_);
      callbacks_.push_back(std::this_thread::get_id());
    }
    const CallbackScope scope{*this};
    if (!Shutdown()) {
      return;
    }
    CloseHandler handler;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_by_server_ = true;
      // Without a handler the close is reported by setCloseHandler when one is set.
      if (!close_handler_) {
        return;
      }
      close_reported_ = true;
      handler = std::move(close_handler_);
    }
    handler();
  }

  // Removes the thread mark from callbacks_ that was set under mutex_ together with the check that the connection
  // is open.
  struct CallbackScope {
    HttpConnection& connection;
    ~CallbackScope() {
      std::lock_guard<std::mutex> lock(connection.mutex_);
      auto& callbacks = connection.callbacks_;
      callbacks.erase(std::find(callbacks.begin(), callbacks.end(), std::this_thread::get_id()));
      connection.callbacks_cv_.notify_all();
    }
  };

  // Opens one POST carrying `message`; 0 if the transport refused it.
  WebStreamId Post(const Json::Value& message, bool* with_session = nullptr) {
    if (!connected_) {
      return 0;
    }
    web::WebStreamDescriptor descriptor;
    descriptor.headers = {
      {":method", "POST"}, {"Content-Type", "application/json"}, {"Accept", "application/json, text/event-stream"}
    };
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!session_id_.empty()) {
        descriptor.headers.emplace_back("Mcp-Session-Id", session_id_);
        if (with_session) {
          *with_session = true;
        }
      }
      if (!protocol_version_.empty()) {
        descriptor.headers.emplace_back("MCP-Protocol-Version", protocol_version_);
      }
    }
    descriptor.headers.insert(descriptor.headers.end(), options_.headers.begin(), options_.headers.end());
    const auto stream = transport_->OpenStream(descriptor);
    if (!stream) {
      return 0;
    }
    const auto body = WriteJson(message);
    web::WebFrame frame;
    frame.bytes.assign(body.begin(), body.end());
    if (!transport_->SendFrame(stream, std::move(frame))) {
      {
        std::lock_guard<std::mutex> lock(exchanges_->mutex);
        exchanges_->AbandonLocked(stream);
      }
      transport_->CloseStream(stream);
      return 0;
    }
    transport_->CloseSend(stream);
    return stream;
  }

  void Deliver(const std::vector<Json::Value>& notifications) {
    NotificationHandler handler;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (notifications.empty() || !handler_ || !connected_) {
        return;
      }
      handler = handler_;
      callbacks_.push_back(std::this_thread::get_id());
    }
    const CallbackScope scope{*this};
    for (const auto& notification : notifications) {
      if (!connected_) {
        break;
      }
      handler(
        notification["method"].asString(),
        notification.isMember("params") ? notification["params"] : Json::Value(Json::objectValue)
      );
    }
  }

  web::WebTransportPtr transport_;
  HttpConnectionOptions options_;
  std::shared_ptr<Exchanges> exchanges_;
  std::atomic<bool> connected_{true};
  std::atomic<uint64_t> next_id_{1};
  std::mutex mutex_; // guards everything below
  std::string session_id_, protocol_version_;
  NotificationHandler handler_;
  CloseHandler close_handler_;
  bool closed_by_server_ = false;
  bool close_reported_ = false;
  std::vector<std::thread::id> callbacks_; // threads currently running the owner's handlers
  std::condition_variable callbacks_cv_;
};

} // namespace

mcp::ConnectionPtr CreateHttpConnection(web::WebTransportPtr transport, HttpConnectionOptions options) {
  if (!transport) {
    throw std::invalid_argument("MCP HTTP connection needs a transport");
  }
  return std::make_shared<HttpConnection>(std::move(transport), std::move(options));
}

} // namespace mcp_http
} // namespace giga_edge
