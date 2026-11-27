#include "giga_edge/modules/mcp/client.h"
#include "giga_edge/modules/mcp/log.h"

#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace giga_edge {
namespace mcp {

namespace {

Error MakeError(ErrorCode code, const std::string& message, const Json::Value& data = Json::Value()) {
  Error error;
  error.code = code;
  error.message = message;
  error.data = data;
  return error;
}

template<typename T>
Result<T> MakeErrorResult(ErrorCode code, const std::string& message, const Json::Value& data = Json::Value()) {
  Result<T> result;
  result.error = MakeError(code, message, data);
  return result;
}

template<typename T>
Result<T> MakeErrorResult(const Error& error) {
  Result<T> result;
  result.error = error;
  return result;
}

// The server response is external data: an unexpected shape gives a call error, not an exception in the pool.
template<typename T, typename ParseFn>
Result<T> ParseResponse(const std::string& method, ParseFn&& parse) {
  try {
    return parse();
  } catch (const Json::Exception& e) {
    return MakeErrorResult<T>(ErrorCode::INTERNAL_ERROR, "Invalid " + method + " response: " + e.what());
  }
}

// The transport is host code: its exception must not escape into the thread pool of the service.
template<typename Fn>
void CallTransport(const char* operation, Fn&& fn) {
  try {
    fn();
  } catch (const std::exception& e) {
    detail::WriteMcpLog(std::string("[ERROR] MCP: transport ") + operation + " failed: " + e.what());
  }
}

bool IsConnected(const IConnection& connection) {
  bool connected = false;
  CallTransport("isConnected", [&] {
    connected = connection.isConnected();
  });
  return connected;
}

class BasicMcpClient : public IMcpClient {
public:
  BasicMcpClient(ConnectionPtr connection, uint32_t default_request_timeout_ms)
    : connection_(std::move(connection))
    , default_request_timeout_ms_(default_request_timeout_ms) {
    if (default_request_timeout_ms_ == 0) {
      throw std::invalid_argument("MCP default_request_timeout_ms must be positive");
    }
    if (connection_) {
      CallTransport("setNotificationHandler", [this] {
        connection_->setNotificationHandler([this](const std::string& method, const Json::Value& params) {
          HandleNotification(method, params);
        });
      });
    }
  }

  ~BasicMcpClient() override {
    BasicMcpClient::shutdown();
  }

  Result<InitializeResult> initialize(
    const std::string& protocol_version, const std::string& client_name, const std::string& client_version
  ) override {
    ConnectionPtr local_connection;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      local_connection = connection_;
    }

    if (!local_connection || !IsConnected(*local_connection)) {
      return MakeErrorResult<InitializeResult>(ErrorCode::TRANSPORT_ERROR, "MCP transport is not connected");
    }

    Json::Value params(Json::objectValue);
    params["protocolVersion"] = protocol_version;
    params["clientInfo"]["name"] = client_name;
    params["clientInfo"]["version"] = client_version;
    params["capabilities"] = Json::Value(Json::objectValue);

    std::string request_id;
    auto response = SendRequest(local_connection, "initialize", params, request_id);
    if (!response.ok()) {
      return MakeErrorResult<InitializeResult>(response.error);
    }

    auto result = ParseResponse<InitializeResult>("initialize", [&response] {
      const Json::Value& value = response.value;
      Result<InitializeResult> parsed;
      parsed.value.protocol_version = value.get("protocolVersion", "").asString();

      const Json::Value& server_info = value["serverInfo"];
      parsed.value.server_name = server_info.get("name", "").asString();
      parsed.value.server_version = server_info.get("version", "").asString();

      const Json::Value& capabilities = value["capabilities"];
      if (capabilities.isObject()) {
        parsed.value.capabilities.supports_tools = capabilities.isMember("tools");
        parsed.value.capabilities.supports_resources = capabilities.isMember("resources");
        parsed.value.capabilities.supports_resource_subscriptions = capabilities["resources"].isObject()
          && capabilities["resources"].get("subscribe", false).asBool();
      }
      return parsed;
    });
    if (!result.ok()) {
      return result;
    }

    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      server_capabilities_ = result.value.capabilities;
    }

    return result;
  }

  void notifyInitialized() override {
    ConnectionPtr local_connection;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      local_connection = connection_;
    }

    if (!local_connection || !IsConnected(*local_connection)) {
      return;
    }
    CallTransport("sendNotification", [&local_connection] {
      local_connection->sendNotification("notifications/initialized", Json::Value(Json::objectValue));
    });
  }

  void shutdown() override {
    ConnectionPtr local_connection;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!connection_) {
        return;
      }
      local_connection = connection_;
      connection_.reset();
      on_tools_list_changed_ = {};
      on_resources_list_changed_ = {};
      resource_callbacks_.clear();
    }

    CallTransport("setNotificationHandler", [&local_connection] {
      local_connection->setNotificationHandler({});
    });
    CallTransport("setCloseHandler", [&local_connection] {
      local_connection->setCloseHandler({});
    });
    CallTransport("close", [&local_connection] {
      local_connection->close();
    });
  }

  Result<std::vector<ToolInfo>> listTools(uint32_t timeout_ms = 0) override {
    auto response = Request("tools/list", Json::Value(Json::objectValue), nullptr, timeout_ms);
    if (!response.ok()) {
      return MakeErrorResult<std::vector<ToolInfo>>(response.error);
    }

    return ParseResponse<std::vector<ToolInfo>>("tools/list", [&response] {
      const Json::Value& arr = response.value["tools"];
      if (!arr.isArray()) {
        return MakeErrorResult<std::vector<ToolInfo>>(ErrorCode::INTERNAL_ERROR, "Invalid tools/list response");
      }

      Result<std::vector<ToolInfo>> result;
      result.value.reserve(arr.size());
      for (const auto& item : arr) {
        ToolInfo info;
        info.name = item.get("name", "").asString();
        info.title = item.get("title", "").asString();
        info.description = item.get("description", "").asString();
        info.input_schema = item["inputSchema"];
        result.value.push_back(std::move(info));
      }
      return result;
    });
  }

  Result<ToolResult> callTool(
    const std::string& name,
    const Json::Value& arguments,
    std::string& out_request_id,
    uint32_t timeout_ms,
    const IConnection::SendGate& gate,
    const Json::Value& metadata
  ) override {
    if (!metadata.isNull() && !metadata.isObject()) {
      return MakeErrorResult<ToolResult>(ErrorCode::INVALID_PARAMS, "MCP call metadata must be an object");
    }
    Json::Value params(Json::objectValue);
    params["name"] = name;
    params["arguments"] = arguments;
    if (!metadata.isNull()) {
      params["_meta"] = metadata;
    }

    auto response = Request("tools/call", params, &out_request_id, timeout_ms, gate);
    if (!response.ok()) {
      return MakeErrorResult<ToolResult>(response.error);
    }

    return ParseResponse<ToolResult>("tools/call", [&response] {
      Result<ToolResult> result;
      result.value.is_error = response.value.get("isError", false).asBool();
      result.value.content = response.value["content"];
      result.value.structured_content = response.value["structuredContent"];
      return result;
    });
  }

  Result<std::vector<ResourceInfo>> listResources() override {
    auto response = Request("resources/list", Json::Value(Json::objectValue));
    if (!response.ok()) {
      return MakeErrorResult<std::vector<ResourceInfo>>(response.error);
    }

    const bool subscribable = serverCapabilities().supports_resource_subscriptions;
    return ParseResponse<std::vector<ResourceInfo>>("resources/list", [&response, subscribable] {
      const Json::Value& arr = response.value["resources"];
      if (!arr.isArray()) {
        return MakeErrorResult<std::vector<ResourceInfo>>(ErrorCode::INTERNAL_ERROR, "Invalid resources/list response");
      }

      Result<std::vector<ResourceInfo>> result;
      result.value.reserve(arr.size());
      for (const auto& item : arr) {
        ResourceInfo info;
        info.uri = item.get("uri", "").asString();
        info.name = item.get("name", "").asString();
        info.mime_type = item.get("mimeType", "").asString();
        info.description = item.get("description", "").asString();
        info.subscribable = item.get("subscribable", subscribable).asBool();
        result.value.push_back(std::move(info));
      }
      return result;
    });
  }

  Result<ResourceContent> readResource(
    const std::string& uri, std::string* out_request_id, uint32_t timeout_ms, const IConnection::SendGate& gate
  ) override {
    Json::Value params(Json::objectValue);
    params["uri"] = uri;

    auto response = Request("resources/read", params, out_request_id, timeout_ms, gate);
    if (!response.ok()) {
      return MakeErrorResult<ResourceContent>(response.error);
    }

    return ParseResponse<ResourceContent>("resources/read", [&response, &uri] {
      const Json::Value& contents = response.value["contents"];
      if (!contents.isArray() || contents.empty()) {
        return MakeErrorResult<ResourceContent>(ErrorCode::INTERNAL_ERROR, "Invalid resources/read response");
      }

      const Json::Value& first = contents[0];
      Result<ResourceContent> result;
      result.value.uri = first.get("uri", uri).asString();
      result.value.mime_type = first.get("mimeType", "").asString();
      result.value.text = first.isMember("text") ? first["text"].asString() : first.get("blob", "").asString();
      return result;
    });
  }

  Result<std::vector<ResourceTemplateInfo>> listResourceTemplates() override {
    auto response = Request("resources/templates/list", Json::Value(Json::objectValue));
    if (!response.ok()) {
      return MakeErrorResult<std::vector<ResourceTemplateInfo>>(response.error);
    }

    return ParseResponse<std::vector<ResourceTemplateInfo>>("resources/templates/list", [&response] {
      const Json::Value& arr = response.value["resourceTemplates"];
      if (!arr.isArray()) {
        return MakeErrorResult<std::vector<ResourceTemplateInfo>>(
          ErrorCode::INTERNAL_ERROR, "Invalid resources/templates/list response"
        );
      }

      Result<std::vector<ResourceTemplateInfo>> result;
      result.value.reserve(arr.size());
      for (const auto& item : arr) {
        ResourceTemplateInfo info;
        info.uri_template = item.get("uriTemplate", "").asString();
        info.name = item.get("name", "").asString();
        info.mime_type = item.get("mimeType", "").asString();
        info.description = item.get("description", "").asString();
        result.value.push_back(std::move(info));
      }
      return result;
    });
  }

  Result<void> subscribeResource(const std::string& uri, ResourceUpdateCallback on_update) override {
    Json::Value params(Json::objectValue);
    params["uri"] = uri;
    uint64_t operation = 0;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      operation = ++next_resource_operation_;
      resource_callbacks_[uri].pending.emplace(operation, std::move(on_update));
    }
    auto response = Request("resources/subscribe", params);
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      const auto it = resource_callbacks_.find(uri);
      if (it != resource_callbacks_.end()) {
        auto& callbacks = it->second;
        const auto pending = callbacks.pending.find(operation);
        if (pending != callbacks.pending.end()) {
          if (response.ok() && operation > callbacks.confirmed.operation) {
            callbacks.confirmed = ResourceCallback{operation, std::move(pending->second)};
          }
          callbacks.pending.erase(pending);
        }
        if (!callbacks.confirmed.callback && callbacks.pending.empty()) {
          resource_callbacks_.erase(it);
        }
      }
    }
    if (!response.ok()) {
      return MakeErrorResult<void>(response.error);
    }
    return {};
  }

  Result<void> unsubscribeResource(const std::string& uri) override {
    Json::Value params(Json::objectValue);
    params["uri"] = uri;

    uint64_t operation = 0;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      operation = ++next_resource_operation_;
    }
    auto response = Request("resources/unsubscribe", params);
    if (!response.ok()) {
      return MakeErrorResult<void>(response.error);
    }

    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      const auto it = resource_callbacks_.find(uri);
      if (it != resource_callbacks_.end()) {
        auto& callbacks = it->second;
        if (callbacks.confirmed.operation < operation) {
          callbacks.confirmed = {};
        }
        callbacks.pending.erase(callbacks.pending.begin(), callbacks.pending.lower_bound(operation));
        if (!callbacks.confirmed.callback && callbacks.pending.empty()) {
          resource_callbacks_.erase(it);
        }
      }
    }

    return {};
  }

  void setOnToolsListChanged(ListChangedCallback cb) override {
    std::lock_guard<std::mutex> lock(state_mutex_);
    on_tools_list_changed_ = std::move(cb);
  }

  void setOnResourcesListChanged(ListChangedCallback cb) override {
    std::lock_guard<std::mutex> lock(state_mutex_);
    on_resources_list_changed_ = std::move(cb);
  }

  Result<void> ping(uint32_t timeout_ms, const IConnection::SendGate& gate) override {
    auto response = Request("ping", Json::Value(Json::objectValue), nullptr, timeout_ms, gate);
    if (!response.ok()) {
      return MakeErrorResult<void>(response.error);
    }
    return {};
  }

  void notifyCancelled(const std::string& request_id, const std::string& reason = "") override {
    ConnectionPtr local_connection;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      local_connection = connection_;
    }

    if (!local_connection || !IsConnected(*local_connection)) {
      return;
    }

    Json::Value params(Json::objectValue);
    params["requestId"] = request_id;
    if (!reason.empty()) {
      params["reason"] = reason;
    }
    CallTransport("sendNotification", [&local_connection, &params] {
      local_connection->sendNotification("notifications/cancelled", params);
    });
  }

  void setOnClosed(IConnection::CloseHandler handler) override {
    ConnectionPtr local_connection;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      local_connection = connection_;
    }
    if (local_connection) {
      CallTransport("setCloseHandler", [&local_connection, &handler] {
        local_connection->setCloseHandler(std::move(handler));
      });
    }
  }

  void abandonRequest(const std::string& request_id) override {
    ConnectionPtr local_connection;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      local_connection = connection_;
    }
    if (local_connection) {
      CallTransport("abandonRequest", [&local_connection, &request_id] {
        local_connection->abandonRequest(request_id);
      });
    }
  }

  ServerCapabilities serverCapabilities() const override {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return server_capabilities_;
  }

private:
  Result<Json::Value> SendRequest(
    const ConnectionPtr& connection,
    const std::string& method,
    const Json::Value& params,
    std::string& request_id,
    uint32_t timeout_ms = 0,
    const IConnection::SendGate& gate = {}
  ) {
    const uint32_t timeout = timeout_ms == 0 ? default_request_timeout_ms_ : timeout_ms;
    try {
      return connection->sendRequestWithGate(method, params, request_id, timeout, gate);
    } catch (const std::exception& e) {
      detail::WriteMcpLog("[ERROR] MCP: transport request '" + method + "' failed: " + e.what());
      return MakeErrorResult<Json::Value>(ErrorCode::TRANSPORT_ERROR, std::string("MCP transport failed: ") + e.what());
    }
  }

  Result<Json::Value> Request(
    const std::string& method,
    const Json::Value& params,
    std::string* out_request_id = nullptr,
    uint32_t timeout_ms = 0,
    const IConnection::SendGate& gate = {}
  ) {
    ConnectionPtr local_connection;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      local_connection = connection_;
    }

    if (!local_connection || !IsConnected(*local_connection)) {
      return MakeErrorResult<Json::Value>(ErrorCode::TRANSPORT_ERROR, "MCP transport is not connected");
    }

    std::string request_id;
    auto response = SendRequest(local_connection, method, params, request_id, timeout_ms, gate);
    if (out_request_id != nullptr) {
      *out_request_id = request_id;
    }
    return response;
  }

  void HandleNotification(const std::string& method, const Json::Value& params) {
    if (method == "notifications/tools/list_changed") {
      ListChangedCallback callback;
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        callback = on_tools_list_changed_;
      }
      if (callback) {
        callback();
      }
      return;
    }

    if (method == "notifications/resources/list_changed") {
      ListChangedCallback callback;
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        callback = on_resources_list_changed_;
      }
      if (callback) {
        callback();
      }
      return;
    }

    if (method == "notifications/resources/updated") {
      const Json::Value& uri_value = params.isObject() ? params["uri"] : Json::Value::nullSingleton();
      if (!uri_value.isString() || uri_value.asString().empty()) {
        detail::WriteMcpLog("[WARNING] MCP: ignoring notifications/resources/updated without a string uri");
        return;
      }
      const std::string uri = uri_value.asString();

      ResourceUpdateCallback callback;
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = resource_callbacks_.find(uri);
        if (it == resource_callbacks_.end()) {
          return;
        }
        const auto& callbacks = it->second;
        callback = !callbacks.pending.empty() && callbacks.pending.rbegin()->first > callbacks.confirmed.operation
          ? callbacks.pending.rbegin()->second
          : callbacks.confirmed.callback;
      }

      if (callback) {
        callback(ResourceNotification{.uri = uri});
      }
    }
  }

private:
  ConnectionPtr connection_;
  const uint32_t default_request_timeout_ms_;

  mutable std::mutex state_mutex_;
  ServerCapabilities server_capabilities_;
  ListChangedCallback on_tools_list_changed_;
  ListChangedCallback on_resources_list_changed_;
  struct ResourceCallback {
    uint64_t operation = 0;
    ResourceUpdateCallback callback;
  };
  struct ResourceCallbacks {
    ResourceCallback confirmed;
    std::map<uint64_t, ResourceUpdateCallback> pending;
  };
  uint64_t next_resource_operation_ = 0;
  std::unordered_map<std::string, ResourceCallbacks> resource_callbacks_;
};

} // namespace

std::shared_ptr<IMcpClient> createMcpClient(ConnectionPtr connection, uint32_t default_request_timeout_ms) {
  if (!connection) {
    return nullptr;
  }
  return std::make_shared<BasicMcpClient>(std::move(connection), default_request_timeout_ms);
}

} // namespace mcp
} // namespace giga_edge
