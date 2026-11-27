#pragma once

#include "giga_edge/modules/mcp/connection.h"

#include <vector>

namespace giga_edge {
namespace mcp {

struct ToolInfo {
  std::string name;
  std::string description;
  Json::Value input_schema;
  std::string title;
};

struct ToolResult {
  bool is_error;
  Json::Value content;
  Json::Value structured_content;

  ToolResult()
    : is_error(false) {}
};

struct ResourceInfo {
  std::string uri;
  std::string name;
  std::string mime_type;
  std::string description;
  bool subscribable;

  ResourceInfo()
    : subscribable(false) {}
};

struct ResourceContent {
  std::string uri;
  std::string mime_type;
  std::string text;
};

struct ResourceTemplateInfo {
  std::string uri_template;
  std::string name;
  std::string mime_type;
  std::string description;
};

struct ServerCapabilities {
  bool supports_tools;
  bool supports_resources;
  bool supports_resource_subscriptions;

  ServerCapabilities()
    : supports_tools(false)
    , supports_resources(false)
    , supports_resource_subscriptions(false) {}
};

struct InitializeResult {
  std::string protocol_version;
  std::string server_name;
  std::string server_version;
  ServerCapabilities capabilities;
};

struct ResourceNotification {
  std::string uri;
};

class IMcpClient {
public:
  virtual ~IMcpClient() = default;

  virtual Result<InitializeResult> initialize(
    const std::string& protocol_version, const std::string& client_name, const std::string& client_version
  ) = 0;

  virtual void notifyInitialized() = 0;
  virtual void shutdown() = 0;

  virtual Result<std::vector<ToolInfo>> listTools(uint32_t timeout_ms = 0) = 0;
  // metadata is the params._meta object; null does not change the request format.
  virtual Result<ToolResult> callTool(
    const std::string& name,
    const Json::Value& arguments,
    std::string& out_request_id,
    uint32_t timeout_ms = 0,
    const IConnection::SendGate& gate = {},
    const Json::Value& metadata = {}
  ) = 0;

  virtual Result<std::vector<ResourceInfo>> listResources() = 0;
  virtual Result<ResourceContent> readResource(
    const std::string& uri,
    std::string* out_request_id = nullptr,
    uint32_t timeout_ms = 0,
    const IConnection::SendGate& gate = {}
  ) = 0;
  virtual Result<std::vector<ResourceTemplateInfo>> listResourceTemplates() = 0;

  using ResourceUpdateCallback = std::function<void(const ResourceNotification&)>;
  virtual Result<void> subscribeResource(const std::string& uri, ResourceUpdateCallback on_update) = 0;
  virtual Result<void> unsubscribeResource(const std::string& uri) = 0;

  using ListChangedCallback = std::function<void()>;
  virtual void setOnToolsListChanged(ListChangedCallback cb) = 0;
  virtual void setOnResourcesListChanged(ListChangedCallback cb) = 0;

  virtual Result<void> ping(uint32_t timeout_ms = 0, const IConnection::SendGate& gate = {}) = 0;
  // Sends notifications/cancelled to the server.
  virtual void notifyCancelled(const std::string& request_id, const std::string& reason = "") = 0;
  // Aborts the wait for the response in the transport (IConnection::abandonRequest); sends nothing to the server.
  virtual void abandonRequest(const std::string& request_id) = 0;
  // Handler of the final close of the transport (IConnection::setCloseHandler); shutdown() removes it.
  virtual void setOnClosed(IConnection::CloseHandler handler) = 0;

  // A copy: initialize may overwrite the server capabilities while another thread reads them.
  virtual ServerCapabilities serverCapabilities() const = 0;
};

std::shared_ptr<IMcpClient> createMcpClient(
  ConnectionPtr connection, uint32_t default_request_timeout_ms = kDefaultRequestTimeoutMs
);

} // namespace mcp
} // namespace giga_edge
