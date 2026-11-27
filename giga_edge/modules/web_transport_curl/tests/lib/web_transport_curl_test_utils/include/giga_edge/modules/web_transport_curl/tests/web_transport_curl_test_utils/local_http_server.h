#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace giga_edge {
namespace web_transport_curl {
namespace test_utils {

#ifdef _WIN32
using Socket = SOCKET;
using SocketIoSize = int;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
inline bool SocketFailed(Socket s) {
  return s == INVALID_SOCKET;
}
constexpr int kShutBoth = SD_BOTH;
constexpr int kShutWrite = SD_SEND;
inline void CloseSocket(Socket s) {
  closesocket(s);
}
// Signals do not interrupt Winsock, and it does not set errno: a stale errno would loop accept forever.
inline bool SocketInterrupted() {
  return false;
}
// Winsock needs WSAStartup before the first socket(); the calls are counted, so nesting is safe.
struct SocketRuntime {
  SocketRuntime() {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
  }
  ~SocketRuntime() {
    WSACleanup();
  }
  SocketRuntime(const SocketRuntime&) = delete;
  SocketRuntime& operator=(const SocketRuntime&) = delete;
};
#else
using Socket = int;
using SocketIoSize = size_t;
constexpr Socket kInvalidSocket = -1;
// < 0 rather than == -1: POSIX only promises a negative result on error, and the analyzer models it so.
inline bool SocketFailed(Socket s) {
  return s < 0;
}
constexpr int kShutBoth = SHUT_RDWR;
constexpr int kShutWrite = SHUT_WR;
inline void CloseSocket(Socket s) {
  close(s);
}
inline bool SocketInterrupted() {
  return errno == EINTR;
}
struct SocketRuntime {};
#endif

// SocketRuntime is a base: it is constructed before and destroyed after all socket members.
class LocalHttpServer : private SocketRuntime {
public:
  struct Request {
    std::string method;
    std::string target;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers; // names in lowercase
  };

  LocalHttpServer() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (SocketFailed(listen_fd_)) {
      throw std::runtime_error("LocalHttpServer: socket() failed");
    }
    int reuse = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);
    if (bind(listen_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0 || listen(listen_fd_, 16) != 0) {
      CloseSocket(listen_fd_);
      throw std::runtime_error("LocalHttpServer: bind()/listen() failed");
    }
    socklen_t len = sizeof(addr);
    if (getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
      CloseSocket(listen_fd_);
      throw std::runtime_error("LocalHttpServer: getsockname() failed");
    }
    port_ = ntohs(addr.sin_port);
    thread_ = std::thread([this]() {
      Serve();
    });
  }

  // The order is mandatory: accept/recv are woken before join, close comes only after it: closing before join
  // leaves the thread with an fd number that may already be reused.
  ~LocalHttpServer() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_.store(true);
    }
    held_cv_.notify_all();
    WakeAccept();
    const Socket conn = conn_fd_.exchange(kInvalidSocket);
    if (conn != kInvalidSocket) {
      shutdown(conn, kShutBoth);
    }
    if (thread_.joinable()) {
      thread_.join();
    }
    CloseSocket(listen_fd_);
  }

  std::string BaseUrl() const {
    return "http://127.0.0.1:" + std::to_string(port_);
  }

  void SetResponse(int status, std::string body, int flush_parts = 1, uint32_t delay_ms = 0) {
    std::lock_guard<std::mutex> lock(mutex_);
    response_status_ = status;
    response_body_ = std::move(body);
    response_flush_parts_ = std::max(1, flush_parts);
    response_delay_ms_ = delay_ms;
  }

  // Extra response headers (duplicate names are allowed: all of them are written).
  void SetResponseHeaders(std::vector<std::pair<std::string, std::string>> headers) {
    std::lock_guard<std::mutex> lock(mutex_);
    response_headers_ = std::move(headers);
  }

  // A "100 Continue" block with the given headers before the main response.
  void SetInterimHeaders(std::vector<std::pair<std::string, std::string>> headers) {
    std::lock_guard<std::mutex> lock(mutex_);
    interim_headers_ = std::move(headers);
  }

  // The response (after the interim block) is not sent until ReleaseHeld: the request is guaranteed to be in flight.
  void HoldResponse() {
    std::lock_guard<std::mutex> lock(mutex_);
    response_held_ = true;
  }

  // The response head is sent and the body waits for ReleaseHeld: the SSE profile, where the head is out and no
  // event has arrived yet.
  void HoldBody() {
    std::lock_guard<std::mutex> lock(mutex_);
    body_held_ = true;
  }

  void ReleaseHeld() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      response_held_ = false;
      body_held_ = false;
    }
    held_cv_.notify_all();
  }

  // One request per connection: the connection is closed right after the response. curl is left unaware that the
  // keep-alive connection died, so the next request goes into a dead connection.
  void SetCloseAfterResponse(bool close_after) {
    std::lock_guard<std::mutex> lock(mutex_);
    close_after_response_ = close_after;
  }

  std::vector<Request> TakeRequests() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(requests_);
  }

  size_t RequestCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_.size();
  }

  int AcceptedConnections() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return accepted_;
  }

  size_t BodyBytesSeen() const {
    return body_bytes_seen_.load();
  }

  static std::string FormatHeaderBlock(const std::vector<std::pair<std::string, std::string>>& headers) {
    std::string block;
    for (const auto& header : headers) {
      block += header.first + ": " + header.second + "\r\n";
    }
    return block;
  }

private:
  // The peer may close before the response ends: send must return an error, not kill the tests with SIGPIPE.
  // SIGPIPE is turned off per accepted socket (only they send; MSG_NOSIGNAL in SendAll is a second guard), not
  // with a process-wide SIG_IGN.
  static void IgnoreSigpipeOnSocket(Socket fd) {
#ifdef SO_NOSIGPIPE
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&on), sizeof(on));
#endif
  }

  // accept() is woken by connecting to itself: shutdown on a listening socket is platform-dependent (on some
  // systems accept does not return from it), and a stuck join hangs the test binary.
  void WakeAccept() {
    const Socket wake = socket(AF_INET, SOCK_STREAM, 0);
    if (SocketFailed(wake)) {
      return;
    }
    // Connect with a timeout: the waker must keep the connection until it is established (a close during
    // EINPROGRESS drops it before it reaches the accept queue). SO_SNDTIMEO limits connect on Linux (about 1 s);
    // macOS ignores it, so there a full backlog would stall until the loopback SYN retries (about 8 s). That wait
    // is finite and out of reach in practice: the backlog is 16, the fixture opens 1-2 connections.
#ifdef _WIN32
    const DWORD io_timeout = 1000; // Winsock takes the timeout in milliseconds, not as a timeval
#else
    const timeval io_timeout{1, 0};
#endif
    setsockopt(wake, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&io_timeout), sizeof(io_timeout));
    setsockopt(wake, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&io_timeout), sizeof(io_timeout));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port_);
    if (connect(wake, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0) {
      shutdown(wake, kShutBoth);
    }
    CloseSocket(wake);
  }

  // Strictly single-threaded: concurrent connections are not served, so regressions of the transport pool show up
  // as wait timeouts; this is a deliberate simplification of the fixture.
  void Serve() {
    while (!stopping_.load()) {
      Socket fd;
      do {
        fd = accept(listen_fd_, nullptr, nullptr);
      } while (SocketFailed(fd) && SocketInterrupted()); // a signal must not silently end the fixture thread
      if (SocketFailed(fd)) {
        break;
      }
      IgnoreSigpipeOnSocket(fd);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        ++accepted_;
      }
      conn_fd_.store(fd);
      // The destructor may have exchanged conn_fd_ before this store: then nobody will wake the thread, so it closes
      // the fresh connection itself and join does not wait on a recv that never returns. Before close the atomic is
      // reset with the same compare_exchange: a destructor preempted between wake and exchange does not get a stale
      // fd number (the number may have gone to a new curl socket) and does not call shutdown() on a foreign
      // descriptor.
      if (stopping_.load() || conn_fd_.load() != fd) {
        Socket expected = fd;
        conn_fd_.compare_exchange_strong(expected, kInvalidSocket);
        CloseSocket(fd);
        continue;
      }
      HandleConnection(fd);
      conn_fd_.store(kInvalidSocket);
      CloseSocket(fd);
    }
  }

  void HandleConnection(Socket fd) {
    std::string pending;
    while (!stopping_.load()) {
      Request request;
      if (!ReadRequest(fd, pending, request)) {
        return;
      }
      const bool is_head = request.method == "HEAD";
      {
        std::lock_guard<std::mutex> lock(mutex_);
        requests_.push_back(std::move(request));
      }
      if (!WriteResponse(fd, is_head)) {
        return;
      }
      bool close_after = false;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        close_after = close_after_response_;
      }
      if (close_after) {
        return;
      }
    }
  }

  bool ReadRequest(Socket fd, std::string& pending, Request& request) {
    size_t header_end = pending.find("\r\n\r\n");
    while (header_end == std::string::npos) {
      if (!RecvAppend(fd, pending)) {
        return false;
      }
      header_end = pending.find("\r\n\r\n");
    }
    const std::string head = pending.substr(0, header_end);
    std::string rest = pending.substr(header_end + 4);
    pending.clear();

    const size_t first_line_end = head.find("\r\n");
    const std::string request_line = head.substr(0, first_line_end);
    const size_t sp1 = request_line.find(' ');
    const size_t sp2 = request_line.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
      return false;
    }
    request.method = request_line.substr(0, sp1);
    request.target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);

    bool chunked = false;
    bool has_body = false;
    size_t content_length = 0;
    // Without headers find returns npos; adding 2 would overflow.
    size_t pos = first_line_end == std::string::npos ? head.size() : first_line_end + 2;
    while (pos < head.size()) {
      const size_t eol = head.find("\r\n", pos);
      // head has no trailing CRLF: the last line yields npos, and 2 must not be added to it.
      const size_t line_end = eol == std::string::npos ? head.size() : eol;
      const size_t next_pos = eol == std::string::npos ? head.size() : eol + 2;
      const std::string line = head.substr(pos, line_end - pos);
      const size_t colon = line.find(':');
      if (colon != std::string::npos) {
        std::string name = ToLower(line.substr(0, colon));
        std::string value = TrimSpaces(line.substr(colon + 1));
        if (name == "content-length") {
          content_length = static_cast<size_t>(std::strtoull(value.c_str(), nullptr, 10));
          has_body = true;
        } else if (name == "transfer-encoding" && value.find("chunked") != std::string::npos) {
          chunked = true;
          has_body = true;
        }
        request.headers.emplace_back(std::move(name), std::move(value));
      }
      pos = next_pos;
    }

    if (!has_body) {
      return true;
    }
    if (chunked) {
      return ReadChunkedBody(fd, rest, request);
    }
    while (rest.size() < content_length) {
      if (!RecvAppend(fd, rest)) {
        return false;
      }
    }
    request.body = rest.substr(0, content_length);
    body_bytes_seen_.fetch_add(request.body.size());
    pending = rest.substr(content_length);
    return true;
  }

  bool ReadChunkedBody(Socket fd, std::string& pending, Request& request) {
    while (true) {
      size_t eol = pending.find("\r\n");
      while (eol == std::string::npos) {
        if (!RecvAppend(fd, pending)) {
          return false;
        }
        eol = pending.find("\r\n");
      }
      const size_t chunk_size = static_cast<size_t>(std::strtoull(pending.c_str(), nullptr, 16));
      pending.erase(0, eol + 2);
      if (chunk_size == 0) {
        while (pending.size() < 2) {
          if (!RecvAppend(fd, pending)) {
            return false;
          }
        }
        pending.erase(0, 2);
        return true;
      }
      while (pending.size() < chunk_size + 2) {
        if (!RecvAppend(fd, pending)) {
          return false;
        }
      }
      request.body.append(pending, 0, chunk_size);
      body_bytes_seen_.fetch_add(chunk_size);
      pending.erase(0, chunk_size + 2);
    }
  }

  bool WriteResponse(Socket fd, bool is_head) {
    int status = 200;
    std::string body;
    int flush_parts = 1;
    uint32_t delay_ms = 0;
    std::vector<std::pair<std::string, std::string>> extra_headers;
    std::vector<std::pair<std::string, std::string>> interim_headers;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      status = response_status_;
      body = response_body_;
      flush_parts = response_flush_parts_;
      delay_ms = response_delay_ms_;
      extra_headers = response_headers_;
      interim_headers = interim_headers_;
    }
    if (!interim_headers.empty()) {
      std::string interim = "HTTP/1.1 100 Continue\r\n" + FormatHeaderBlock(interim_headers) + "\r\n";
      if (!SendAll(fd, interim)) {
        return false;
      }
    }
    if (delay_ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    WaitWhileHeld(response_held_);
    std::string extra = FormatHeaderBlock(extra_headers);
    // Per HTTP, HEAD declares Content-Length but sends no body, like a real server.
    const std::string head = "HTTP/1.1 " + std::to_string(status) + " " + ReasonPhrase(status)
      + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + extra + "Connection: keep-alive\r\n\r\n";
    if (!SendAll(fd, head)) {
      return false;
    }
    if (is_head || body.empty()) {
      return true;
    }
    WaitWhileHeld(body_held_);
    const size_t part = (body.size() + static_cast<size_t>(flush_parts) - 1) / static_cast<size_t>(flush_parts);
    for (size_t offset = 0; offset < body.size(); offset += part) {
      const size_t n = std::min(part, body.size() - offset);
      if (!SendAll(fd, body.substr(offset, n))) {
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    return true;
  }

  void WaitWhileHeld(const bool& held) {
    std::unique_lock<std::mutex> lock(mutex_);
    held_cv_.wait(lock, [this, &held]() {
      return !held || stopping_.load();
    });
  }

  static bool RecvAppend(Socket fd, std::string& buffer) {
    char chunk[4096];
    const auto n = recv(fd, chunk, static_cast<SocketIoSize>(sizeof(chunk)), 0);
    if (n > 0) {
      buffer.append(chunk, chunk + n);
      return true;
    }
    if (n < 0 && SocketInterrupted()) {
      return RecvAppend(fd, buffer);
    }
    return false;
  }

  static bool SendAll(Socket fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
#ifdef MSG_NOSIGNAL
      const int flags = MSG_NOSIGNAL;
#else
      const int flags = 0;
#endif
      const auto n = send(fd, data.data() + sent, static_cast<SocketIoSize>(data.size() - sent), flags);
      if (n > 0) {
        sent += static_cast<size_t>(n);
        continue;
      }
      if (n < 0 && SocketInterrupted()) {
        continue;
      }
      return false;
    }
    return true;
  }

  static std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return value;
  }

  static std::string TrimSpaces(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t')) {
      ++begin;
    }
    size_t end = value.size();
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t')) {
      --end;
    }
    return value.substr(begin, end - begin);
  }

  static std::string ReasonPhrase(int status) {
    switch (status) {
      case 200:
        return "OK";
      case 204:
        return "No Content";
      case 301:
        return "Moved Permanently";
      case 302:
        return "Found";
      case 401:
        return "Unauthorized";
      case 403:
        return "Forbidden";
      case 404:
        return "Not Found";
      case 500:
        return "Internal Server Error";
      case 501:
        return "Not Implemented";
      default:
        return "Status";
    }
  }

  // The order follows clang-analyzer optin.performance.Padding.
  std::thread thread_;
  std::atomic<size_t> body_bytes_seen_{0};
  std::vector<std::pair<std::string, std::string>> response_headers_;
  std::vector<std::pair<std::string, std::string>> interim_headers_;
  std::vector<Request> requests_;
  std::string response_body_;
  mutable std::mutex mutex_;
  std::condition_variable held_cv_;
  Socket listen_fd_ = kInvalidSocket;
  std::atomic<Socket> conn_fd_{kInvalidSocket};
  int response_status_ = 200;
  int response_flush_parts_ = 1;
  uint32_t response_delay_ms_ = 0;
  int accepted_ = 0;
  uint16_t port_ = 0;
  std::atomic<bool> stopping_{false};
  bool response_held_ = false;
  bool body_held_ = false;
  bool close_after_response_ = false;
};

} // namespace test_utils
} // namespace web_transport_curl
} // namespace giga_edge
