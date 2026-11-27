#include "mcp_stdio_connection.h"

#include <json/json.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <mutex>
#include <poll.h>
#include <pthread.h>
#include <stdexcept>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <utility>

namespace giga_edge {
namespace {

mcp::Error MakeTransportError(const std::string& message) {
  mcp::Error error;
  error.code = mcp::ErrorCode::TRANSPORT_ERROR;
  error.message = message;
  error.data = Json::Value(Json::nullValue);
  return error;
}

mcp::Error MakeTimeoutError(const std::string& message) {
  mcp::Error error;
  error.code = mcp::ErrorCode::TIMEOUT;
  error.message = message;
  error.data = Json::Value(Json::nullValue);
  return error;
}

mcp::Error MakeCancelledError(const std::string& message) {
  mcp::Error error;
  error.code = mcp::ErrorCode::CANCELLED;
  error.message = message;
  error.data = Json::Value(Json::nullValue);
  return error;
}

template<typename T>
mcp::Result<T> MakeErrorResult(mcp::Error error) {
  mcp::Result<T> result;
  result.error = std::move(error);
  return result;
}

std::string JsonIdToString(const Json::Value& id) {
  if (id.isString()) {
    return id.asString();
  }
  if (id.isUInt64()) {
    return std::to_string(id.asUInt64());
  }
  if (id.isInt64()) {
    return std::to_string(id.asInt64());
  }
  if (id.isUInt()) {
    return std::to_string(id.asUInt());
  }
  if (id.isInt()) {
    return std::to_string(id.asInt());
  }
  return "";
}

mcp::Error ErrorFromJson(const Json::Value& error_value) {
  mcp::Error error;
  error.code = mcp::ErrorCode::INTERNAL_ERROR;
  error.message = "MCP server returned an invalid JSON-RPC error";
  error.data = Json::Value(Json::nullValue);

  if (!error_value.isObject()) {
    return error;
  }

  const Json::Value& code_value = error_value["code"];
  const int code = code_value.isInt() ? code_value.asInt() : static_cast<int>(mcp::ErrorCode::INTERNAL_ERROR);
  if (code_value.isInt()) {
    error.json_rpc_code = code;
  }
  switch (code) {
    case static_cast<int>(mcp::ErrorCode::INVALID_REQUEST):
      error.code = mcp::ErrorCode::INVALID_REQUEST;
      break;
    case static_cast<int>(mcp::ErrorCode::METHOD_NOT_FOUND):
      error.code = mcp::ErrorCode::METHOD_NOT_FOUND;
      break;
    case static_cast<int>(mcp::ErrorCode::INVALID_PARAMS):
      error.code = mcp::ErrorCode::INVALID_PARAMS;
      break;
    case static_cast<int>(mcp::ErrorCode::INTERNAL_ERROR):
      error.code = mcp::ErrorCode::INTERNAL_ERROR;
      break;
    default:
      error.code = mcp::ErrorCode::SERVER_ERROR;
      break;
  }

  // Without a non-empty string message the text "invalid JSON-RPC error" stays: otherwise the log has an empty spot.
  const Json::Value& message = error_value["message"];
  if (message.isString() && !message.asString().empty()) {
    error.message = message.asString();
  }
  error.data = error_value.isMember("data") ? error_value["data"] : Json::Value(Json::nullValue);
  return error;
}

std::string WriteJsonLine(const Json::Value& value) {
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["emitUTF8"] = true;
  return Json::writeString(builder, value) + "\n";
}

class StdioProcessMcpConnection final : public mcp::IConnection {
public:
  StdioProcessMcpConnection(std::string server_id, std::string command, bool debug_enabled)
    : server_id_(std::move(server_id))
    , command_(std::move(command))
    , debug_enabled_(debug_enabled) {
    DebugLog("starting stdio command: " + command_);
    StartProcess();
    reader_thread_ = std::thread([this, read_fd = stdout_fd_]() {
      ReaderLoop(read_fd);
    });
    reader_id_ = reader_thread_.get_id();
  }

  ~StdioProcessMcpConnection() override {
    StdioProcessMcpConnection::close();
    if (std::this_thread::get_id() == reader_id_) {
      // The last reference was released by a handler on the reader thread, and a thread cannot join itself. ReaderLoop
      // exits on the flag on its own stack without touching the members again; the read descriptor is closed here.
      *reader_destroyed_ = true;
      reader_thread_.detach();
      CloseFd(stdout_fd_);
    }
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
    if (timeout_ms == 0) {
      mcp::Error error;
      error.code = mcp::ErrorCode::INVALID_PARAMS;
      error.message = "MCP transport timeout must be positive";
      return MakeErrorResult<Json::Value>(std::move(error));
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    const std::string request_id = NextRequestId();
    out_request_id = request_id;

    Json::Value message(Json::objectValue);
    message["jsonrpc"] = "2.0";
    message["id"] = request_id;
    message["method"] = method;
    message["params"] = params;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!connected_) {
        return MakeErrorResult<Json::Value>(MakeTransportError("MCP stdio transport is not connected"));
      }
      pending_[request_id] = PendingResponse{};
    }

    DebugLog(
      "request -> id=" + request_id + " method=" + method + " timeout_ms=" + std::to_string(timeout_ms)
      + " params=" + WriteJsonLine(params)
    );
    const auto written = WriteMessage(message, deadline, gate, request_id);
    if (!written.ok()) {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_.erase(request_id);
      return MakeErrorResult<Json::Value>(written.error);
    }

    std::unique_lock<std::mutex> lock(mutex_);
    const auto is_done = [&]() {
      auto it = pending_.find(request_id);
      return it == pending_.end() || it->second.done;
    };

    if (!cv_.wait_until(lock, deadline, is_done)) {
      pending_.erase(request_id);
      return MakeErrorResult<Json::Value>(MakeTimeoutError("Timed out waiting for MCP stdio response"));
    }

    auto it = pending_.find(request_id);
    if (it == pending_.end()) {
      return MakeErrorResult<Json::Value>(MakeTransportError("MCP stdio request was cancelled or disconnected"));
    }

    auto result = std::move(it->second.result);
    pending_.erase(it);
    return result;
  }

  void sendNotification(const std::string& method, const Json::Value& params) override {
    Json::Value message(Json::objectValue);
    message["jsonrpc"] = "2.0";
    message["method"] = method;
    message["params"] = params;
    DebugLog("notification -> method=" + method + " params=" + WriteJsonLine(params));
    WriteMessage(message, std::chrono::steady_clock::now() + std::chrono::seconds(5));
  }

  void setNotificationHandler(NotificationHandler handler) override {
    std::lock_guard<std::mutex> lock(mutex_);
    notification_handler_ = std::move(handler);
  }

  void setCloseHandler(CloseHandler handler) override {
    CloseHandler closed_already;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (handler && !connected_ && !closing_ && !close_reported_) {
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

  void abandonRequest(const std::string& request_id) override {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = pending_.find(request_id);
    if (it == pending_.end() || it->second.done) {
      return;
    }
    it->second.result.error = MakeCancelledError("MCP stdio request abandoned");
    it->second.done = true;
    cv_.notify_all();
  }

  void close() override {
    std::call_once(close_once_, [this]() {
      DebugLog("closing stdio transport");
      {
        std::lock_guard<std::mutex> lock(mutex_);
        connected_ = false;
        closing_ = true;
      }
      FailPending(MakeTransportError("MCP stdio transport closed"));
      TerminateChild();
      std::lock_guard<std::timed_mutex> lock(write_mutex_);
      CloseFd(stdin_fd_);
    });
    if (std::this_thread::get_id() == reader_id_) {
      return;
    }
    std::lock_guard<std::mutex> join_lock(join_mutex_);
    if (reader_thread_.joinable()) {
      reader_thread_.join();
    }
    std::lock_guard<std::timed_mutex> lock(write_mutex_);
    CloseFd(stdout_fd_);
  }

  bool isConnected() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
  }

private:
  struct PendingResponse {
    bool done = false;
    mcp::Result<Json::Value> result;
  };

  void DebugLog(const std::string& message) const {
    if (debug_enabled_) {
      std::cerr << "[mcp][cli][" << server_id_ << "] " << message << '\n';
    }
  }

  static void CloseFd(int& fd) {
    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }
  }

  std::string NextRequestId() {
    std::lock_guard<std::mutex> lock(mutex_);
    return "cli-mcp-" + std::to_string(next_request_id_++);
  }

  void StartProcess() {
    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    if (::pipe(stdin_pipe) != 0 || ::pipe(stdout_pipe) != 0) {
      CloseFd(stdin_pipe[0]);
      CloseFd(stdin_pipe[1]);
      CloseFd(stdout_pipe[0]);
      CloseFd(stdout_pipe[1]);
      throw std::runtime_error("Failed to create MCP stdio pipes: " + std::string(std::strerror(errno)));
    }
    const auto flags = ::fcntl(stdin_pipe[1], F_GETFL);
    if (flags < 0 || ::fcntl(stdin_pipe[1], F_SETFL, flags | O_NONBLOCK) != 0) {
      CloseFd(stdin_pipe[0]);
      CloseFd(stdin_pipe[1]);
      CloseFd(stdout_pipe[0]);
      CloseFd(stdout_pipe[1]);
      throw std::runtime_error("Failed to make MCP stdin nonblocking");
    }
#ifdef F_SETNOSIGPIPE
    if (::fcntl(stdin_pipe[1], F_SETNOSIGPIPE, 1) != 0) {
      CloseFd(stdin_pipe[0]);
      CloseFd(stdin_pipe[1]);
      CloseFd(stdout_pipe[0]);
      CloseFd(stdout_pipe[1]);
      throw std::runtime_error("Failed to disable SIGPIPE for MCP stdio transport");
    }
#endif

    child_pid_ = ::fork();
    if (child_pid_ < 0) {
      CloseFd(stdin_pipe[0]);
      CloseFd(stdin_pipe[1]);
      CloseFd(stdout_pipe[0]);
      CloseFd(stdout_pipe[1]);
      throw std::runtime_error("Failed to fork MCP stdio process: " + std::string(std::strerror(errno)));
    }

    if (child_pid_ == 0) {
      ::setpgid(0, 0);
      ::dup2(stdin_pipe[0], STDIN_FILENO);
      ::dup2(stdout_pipe[1], STDOUT_FILENO);
      CloseFd(stdin_pipe[0]);
      CloseFd(stdin_pipe[1]);
      CloseFd(stdout_pipe[0]);
      CloseFd(stdout_pipe[1]);
      ::execl("/bin/sh", "sh", "-c", command_.c_str(), static_cast<char*>(nullptr));
      _exit(127);
    }

    ::setpgid(child_pid_, child_pid_);
    stdin_fd_ = stdin_pipe[1];
    stdout_fd_ = stdout_pipe[0];
    CloseFd(stdin_pipe[0]);
    CloseFd(stdout_pipe[1]);
  }

  mcp::Result<void> WriteMessage(
    const Json::Value& message,
    std::chrono::steady_clock::time_point deadline,
    const SendGate& gate = {},
    const std::string& request_id = {}
  ) {
    const std::string line = WriteJsonLine(message);
    std::unique_lock<std::timed_mutex> lock(write_mutex_, std::defer_lock);
    if (!lock.try_lock_until(deadline)) {
      return MakeErrorResult<void>(MakeTimeoutError("Timed out waiting to write MCP stdio message"));
    }
    if (stdin_fd_ < 0 || !isConnected()) {
      return MakeErrorResult<void>(MakeTransportError("MCP stdio transport is not connected"));
    }
    if (gate && !gate(request_id)) {
      return MakeErrorResult<void>(MakeCancelledError("MCP request cancelled before sending"));
    }
    const auto fail = [&](mcp::Error error) {
      {
        std::lock_guard<std::mutex> state_lock(mutex_);
        connected_ = false;
      }
      // An unfinished JSON line must not be continued with a new request.
      FailPending(MakeTransportError("Failed to write MCP stdio message"));
      return MakeErrorResult<void>(std::move(error));
    };
#ifndef F_SETNOSIGPIPE
    struct BlockSigPipe {
      sigset_t signals;
      sigset_t previous;
      bool pending;
      BlockSigPipe() {
        sigemptyset(&signals);
        sigaddset(&signals, SIGPIPE);
        ::pthread_sigmask(SIG_BLOCK, &signals, &previous);
        sigset_t current;
        ::sigpending(&current);
        pending = sigismember(&current, SIGPIPE) == 1;
      }
      ~BlockSigPipe() {
        sigset_t current;
        ::sigpending(&current);
        if (!pending && sigismember(&current, SIGPIPE) == 1) {
          int signal = 0;
          ::sigwait(&signals, &signal);
        }
        ::pthread_sigmask(SIG_SETMASK, &previous, nullptr);
      }
    } block_sigpipe;
#endif
    const char* data = line.data();
    size_t remaining = line.size();
    while (remaining > 0) {
      const auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
      if (left.count() <= 0) {
        return fail(MakeTimeoutError("Timed out writing MCP stdio message"));
      }
      pollfd descriptor{stdin_fd_, POLLOUT, 0};
      const auto ready = ::poll(
        &descriptor, 1, static_cast<int>(std::min<int64_t>(left.count(), std::numeric_limits<int>::max()))
      );
      if (ready == 0 || (ready < 0 && errno == EINTR)) {
        continue;
      }
      if (ready < 0) {
        return fail(MakeTransportError("Failed to poll MCP stdin"));
      }
      const ssize_t written = ::write(stdin_fd_, data, remaining);
      if (written < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
        continue;
      }
      if (written <= 0) {
        return fail(MakeTransportError("Failed to write MCP stdio message"));
      }
      data += written;
      remaining -= static_cast<size_t>(written);
    }
    return {};
  }

  void ReaderLoop(int read_fd) {
    bool destroyed = false;
    reader_destroyed_ = &destroyed;
    std::string buffer;
    char chunk[4096];
    while (isConnected()) {
      pollfd descriptor{read_fd, POLLIN, 0};
      const auto ready = ::poll(&descriptor, 1, 100);
      if (ready == 0 || (ready < 0 && errno == EINTR)) {
        continue;
      }
      if (ready < 0) {
        break;
      }
      const ssize_t read_bytes = ::read(read_fd, chunk, sizeof(chunk));
      if (read_bytes < 0 && errno == EINTR) {
        continue;
      }
      if (read_bytes <= 0) {
        break;
      }
      buffer.append(chunk, static_cast<size_t>(read_bytes));
      size_t line_end = std::string::npos;
      while ((line_end = buffer.find('\n')) != std::string::npos) {
        std::string line = buffer.substr(0, line_end);
        buffer.erase(0, line_end + 1);
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        if (!line.empty()) {
          HandleLine(line);
          if (destroyed) {
            return;
          }
        }
      }
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      connected_ = false;
    }
    FailPending(MakeTransportError("MCP stdio process exited or closed stdout"));
    NotifyClosed();
  }

  // The final close caused by the server process or a write error, not by a close() call. The last action of the
  // reader thread: the handler may release the last reference to the connection.
  void NotifyClosed() {
    CloseHandler handler;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      // Without a handler the close is reported by setCloseHandler when one is set.
      if (closing_ || close_reported_ || !close_handler_) {
        return;
      }
      close_reported_ = true;
      handler = std::move(close_handler_);
    }
    handler();
  }

  void HandleLine(const std::string& line) {
    Json::Value message;
    Json::Reader reader;
    if (!reader.parse(line, message) || !message.isObject()) {
      std::cerr << "[mcp] Ignoring invalid JSON-RPC line from stdio server: " << line << '\n';
      return;
    }
    DebugLog("message <- " + line);

    if (message.isMember("id") && (message.isMember("result") || message.isMember("error"))) {
      HandleResponse(message);
      return;
    }

    const Json::Value& method_value = message["method"];
    if (!method_value.isString() || method_value.asString().empty()) {
      return;
    }
    const std::string method = method_value.asString();

    NotificationHandler handler;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      handler = notification_handler_;
    }
    if (handler) {
      handler(method, message.isMember("params") ? message["params"] : Json::Value(Json::objectValue));
    }
  }

  void HandleResponse(const Json::Value& message) {
    const std::string request_id = JsonIdToString(message["id"]);
    if (request_id.empty()) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_.find(request_id);
    if (it == pending_.end()) {
      return;
    }

    if (message.isMember("error")) {
      it->second.result.error = ErrorFromJson(message["error"]);
    } else {
      it->second.result.value = message["result"];
    }
    it->second.done = true;
    cv_.notify_all();
  }

  void FailPending(const mcp::Error& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& item : pending_) {
      if (!item.second.done) {
        item.second.result.error = error;
        item.second.done = true;
      }
    }
    cv_.notify_all();
  }

  void TerminateChild() {
    if (child_pid_ <= 0) {
      return;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    ::kill(-child_pid_, SIGTERM);
    bool reaped = false;
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
      if (!reaped) {
        const auto waited = ::waitpid(child_pid_, &status, WNOHANG);
        reaped = waited == child_pid_ || (waited < 0 && errno == ECHILD);
      }
      if (::kill(-child_pid_, 0) < 0 && errno == ESRCH) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // The group includes the shell and the descendants that hold the pipes.
    ::kill(-child_pid_, SIGKILL);
    if (!reaped) {
      while (::waitpid(child_pid_, &status, 0) < 0 && errno == EINTR) {
      }
    }
    child_pid_ = -1;
  }

  std::string server_id_;
  std::string command_;
  mutable std::mutex mutex_;
  std::timed_mutex write_mutex_;
  std::mutex join_mutex_;
  std::once_flag close_once_;
  std::condition_variable cv_;
  NotificationHandler notification_handler_;
  std::unordered_map<std::string, PendingResponse> pending_;
  uint64_t next_request_id_ = 1;
  bool connected_ = true;
  bool closing_ = false;
  bool close_reported_ = false;
  CloseHandler close_handler_;
  // A flag on the ReaderLoop stack: a destructor called on the reader thread tells it that the members are destroyed.
  bool* reader_destroyed_ = nullptr;
  int stdin_fd_ = -1;
  int stdout_fd_ = -1;
  pid_t child_pid_ = -1;
  std::thread reader_thread_;
  std::thread::id reader_id_;
  bool debug_enabled_ = false;
};

} // namespace

mcp::ConnectionPtr CreateStdioMcpConnection(
  const std::string& server_id, const std::string& command, bool debug_enabled
) {
  return std::make_shared<StdioProcessMcpConnection>(server_id, command, debug_enabled);
}

} // namespace giga_edge
