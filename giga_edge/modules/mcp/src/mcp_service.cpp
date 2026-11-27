#include "giga_edge/lib/threadpool/threadpool.h"
#include "giga_edge/modules/mcp/client.h"
#include "giga_edge/modules/mcp/log.h"
#include "giga_edge/modules/mcp/resource_state.h"
#include "giga_edge/modules/mcp/service.h"
#include "giga_edge/modules/mcp/service/pending_request_store.h"
#include "giga_edge/modules/mcp/service/refresh_helpers.h"
#include "giga_edge/modules/mcp/service/request_execution.h"
#include "giga_edge/modules/mcp/service/resource_retry.h"
#include "giga_edge/modules/mcp/service/server_lifecycle.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace giga_edge {
namespace mcp {

namespace {

std::vector<McpToolDescriptor> AggregateTools(
  const std::unordered_map<std::string, std::vector<ToolInfo>>& per_server_tools
) {
  std::vector<McpToolDescriptor> result;
  for (const auto& [server_id, tools] : per_server_tools) {
    for (const auto& tool : tools) {
      result.push_back(
        McpToolDescriptor{
          .server_id = server_id,
          .tool_name = tool.name,
          .description = tool.description,
          .input_schema = tool.input_schema,
          .title = tool.title,
        }
      );
    }
  }
  return result;
}

std::vector<McpResourceDescriptor> AggregateResources(
  const std::unordered_map<std::string, std::vector<ResourceInfo>>& per_server_resources
) {
  std::vector<McpResourceDescriptor> result;
  for (const auto& [server_id, resources] : per_server_resources) {
    for (const auto& r : resources) {
      result.push_back(
        McpResourceDescriptor{
          .server_id = server_id,
          .uri = r.uri,
          .name = r.name,
          .mime_type = r.mime_type,
          .description = r.description,
          .subscribable = r.subscribable,
        }
      );
    }
  }
  return result;
}

std::vector<McpResourceTemplateDescriptor> AggregateResourceTemplates(
  const std::unordered_map<std::string, std::vector<ResourceTemplateInfo>>& per_server_templates
) {
  std::vector<McpResourceTemplateDescriptor> result;
  for (const auto& [server_id, templates] : per_server_templates) {
    for (const auto& t : templates) {
      result.push_back(
        McpResourceTemplateDescriptor{
          .server_id = server_id,
          .uri_template = t.uri_template,
          .name = t.name,
          .mime_type = t.mime_type,
          .description = t.description,
        }
      );
    }
  }
  return result;
}

void Enqueue(IThreadPool& pool, std::function<void()> fn, int32_t priority = 1) {
  pool.Push(std::make_shared<Task>(std::move(fn), priority, std::nullopt, std::nullopt));
}

std::string MakeResourceKey(const std::string& server_id, const std::string& uri) {
  return server_id + "\n" + uri;
}

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool ResourceMatchesPhrase(const ResourceInfo& resource, const std::string& phrase) {
  const auto normalized_phrase = ToLowerAscii(phrase);
  if (normalized_phrase.empty()) {
    return false;
  }

  const auto haystack = ToLowerAscii(
    resource.uri + "\n" + resource.name + "\n" + resource.description + "\n" + resource.mime_type
  );
  return haystack.find(normalized_phrase) != std::string::npos;
}

std::optional<std::string> SelectPreferredReadResourceUri(
  const std::vector<ResourceInfo>& resources, const McpResourceHydrationConfig& config
) {
  for (const auto& phrase : config.preferred_read_resource_match_phrases) {
    for (const auto& resource : resources) {
      if (!resource.uri.empty() && ResourceMatchesPhrase(resource, phrase)) {
        return resource.uri;
      }
    }
  }
  return std::nullopt;
}

} // namespace

using detail::AggregatedServiceSnapshots;
using detail::ClearPendingRequest;
using detail::CommitConnectedServer;
using detail::CommitResourcesRefresh;
using detail::CommitToolsRefresh;
using detail::LoadInitialServerData;
using detail::MakePendingPingRequest;
using detail::MakePendingReadResourceRequest;
using detail::MakePendingToolCallRequest;
using detail::MakeServerUnavailablePingResult;
using detail::MakeServerUnavailableReadResult;
using detail::MakeServerUnavailableToolResult;
using detail::MakeSubscriptionEvent;
using detail::PendingClientResolution;
using detail::RefreshResourceData;
using detail::RemovedServerData;
using detail::RemoveServerFromState;
using detail::ResolvePendingReadyClient;
using detail::ResolveReadyServerClient;
using detail::ServicePendingRequest;
using detail::ShutdownServerClient;
using detail::StorePendingRequest;
using detail::TakePendingRequest;

class McpServiceImpl : public IMcpService, public std::enable_shared_from_this<McpServiceImpl> {
public:
  friend size_t GetMcpResourceRetryCount(const IMcpService& service);

  McpServiceImpl(McpServiceConfig config, std::unique_ptr<IThreadPool> thread_pool)
    : config_(std::move(config))
    , thread_pool_(
        thread_pool
          ? std::move(thread_pool)
          : std::make_unique<ThreadPool>(
              static_cast<int>(config_.thread_pool_size),
              /*realtime=*/false,
              /*realtime_threads_count=*/0,
              "mcp"
            )
      ) {
    DebugLog("service created thread_pool_size=" + std::to_string(config_.thread_pool_size));
  }

  ~McpServiceImpl() override {
    McpServiceImpl::Shutdown();
  }

  void Shutdown() override {
    std::call_once(shutdown_once_, [this]() {
      stopping_ = true;
      std::thread retry_thread;
      {
        std::lock_guard<std::mutex> lock(auto_resource_mutex_);
        retries_.clear();
        retry_thread = std::move(retry_thread_);
      }
      retry_cv_.notify_all();
      if (retry_thread.joinable()) {
        retry_thread.join();
      }
      // The accepted requests are completed here and their waits in the transport are aborted, so that Stop does not
      // wait for responses and timeouts. Stop still waits for the internal requests of the service (initialize, lists,
      // auto-reads, subscriptions).
      decltype(pending_requests_) pending_requests;
      std::vector<std::pair<std::string, std::shared_ptr<IMcpClient>>> abandoned;
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        pending_requests.swap(pending_requests_);
        for (auto& [token, pending] : pending_requests) {
          const auto server = servers_.find(pending.server_id);
          const auto request_id = pending.cancellation->Cancel();
          if (request_id && server != servers_.end() && server->second.client) {
            abandoned.emplace_back(*request_id, server->second.client);
          }
        }
      }
      for (const auto& [request_id, client] : abandoned) {
        client->abandonRequest(request_id);
      }
      thread_pool_->Stop();
      // The stopped pool cancelled the tasks that had not started yet: the release of the replaced servers is also
      // completed here, otherwise their results would not reach the observer.
      decltype(servers_) servers;
      decltype(detached_servers_) detached_servers;
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        servers.swap(servers_);
        detached_servers.swap(detached_servers_);
        per_server_tools_.clear();
        per_server_resources_.clear();
        per_server_resource_templates_.clear();
      }
      for (auto& [token, pending] : pending_requests) {
        EmitCancelledResult(token, pending, "Service shut down");
      }
      for (auto& detached : detached_servers) {
        ReleaseDetachedServer(std::move(detached));
      }
      DrainObserverEvents();
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        observer_.reset();
      }
      for (auto& [server_id, state] : servers) {
        ShutdownServerClient(state.client, state.subscribed_resource_uris);
      }
    });
  }

  void SetObserver(std::weak_ptr<IMcpServiceObserver> observer) override {
    std::lock_guard<std::mutex> lock(state_mutex_);
    observer_ = std::move(observer);
  }

  void AddServer(const std::string& server_id, ConnectionPtr connection) override {
    DebugLog("AddServer server='" + server_id + "'");
    if (stopping_ || server_id.empty() || !connection) {
      return;
    }

    uint64_t generation = 0;
    bool replaced = false;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (stopping_) {
        return;
      }
      if (const auto it = servers_.find(server_id); it != servers_.end()) {
        if (it->second.connection.lock() == connection && it->second.status != ServerStatus::REMOVING) {
          DebugLog("AddServer ignored: same connection server='" + server_id + "'");
          return;
        }
        // The same id with a new connection is a reconnect.
        detached_servers_.push_back(DetachServerLocked(server_id));
        replaced = true;
      }
      auto& state = servers_[server_id];
      generation = state.generation = ++next_generation_;
      state.connection = connection;
    }

    if (replaced) {
      detail::WriteMcpLog(std::string("[INFO] MCP: reconnecting server '") + server_id + "'");
      Enqueue(*thread_pool_, [this]() {
        ReleaseDetachedServers();
      });
    }
    Enqueue(*thread_pool_, [this, server_id, generation, connection = std::move(connection)]() {
      DoAddServer(server_id, generation, connection);
    });
  }

  void RemoveServer(const std::string& server_id) override {
    DebugLog("RemoveServer server='" + server_id + "'");
    if (server_id.empty()) {
      return;
    }

    uint64_t generation = 0;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      auto it = servers_.find(server_id);
      if (it == servers_.end()) {
        return;
      }
      it->second.status = ServerStatus::REMOVING;
      generation = it->second.generation;
      ClearServerRetriesLocked(server_id, generation);
    }

    Enqueue(*thread_pool_, [this, server_id, generation]() {
      DoRemoveServer(server_id, generation);
    });
  }

  bool CallToolAsync(McpRequestToken token, const McpToolCallRequest& request) override {
    DebugLog(
      "CallToolAsync token=" + std::to_string(token) + " call_id='" + request.call_id + "' server='" + request.server_id
      + "' tool='" + request.tool_name + "'"
    );
    if (token == 0 || request.call_id.empty() || request.server_id.empty() || request.tool_name.empty()) {
      return false;
    }
    return SubmitAsyncRequest(token, MakePendingToolCallRequest(request), [this, token, request]() {
      Enqueue(
        *thread_pool_,
        [this, token, request]() {
          DoCallTool(token, request);
        },
        /*priority=*/2
      );
    });
  }

  std::vector<McpToolDescriptor> GetToolsSnapshot() const override {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return AggregateTools(per_server_tools_);
  }

  std::vector<std::string> GetServerIds() const override {
    std::lock_guard<std::mutex> lock(state_mutex_);
    std::vector<std::string> ids;
    ids.reserve(servers_.size());
    for (const auto& [id, state] : servers_) {
      if (state.status == ServerStatus::READY) {
        ids.push_back(id);
      }
    }
    return ids;
  }

  std::vector<McpResourceDescriptor> GetResourcesSnapshot() const override {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return AggregateResources(per_server_resources_);
  }

  bool ReadResourceAsync(McpRequestToken token, const McpReadResourceRequest& request) override {
    DebugLog(
      "ReadResourceAsync token=" + std::to_string(token) + " call_id='" + request.call_id + "' server='"
      + request.server_id + "' uri='" + request.uri + "'"
    );
    if (token == 0 || request.call_id.empty() || request.server_id.empty() || request.uri.empty()) {
      return false;
    }
    return SubmitAsyncRequest(token, MakePendingReadResourceRequest(request), [this, token, request]() {
      Enqueue(
        *thread_pool_,
        [this, token, request]() {
          DoReadResource(token, request);
        },
        /*priority=*/2
      );
    });
  }

  std::vector<McpResourceTemplateDescriptor> GetResourceTemplatesSnapshot() const override {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return AggregateResourceTemplates(per_server_resource_templates_);
  }

  bool SubscribeResourceAsync(
    McpSubscriptionOperationId operation_id, const std::string& server_id, const std::string& uri
  ) override {
    return RequestSubscription(operation_id, server_id, uri, true);
  }

  bool UnsubscribeResourceAsync(
    McpSubscriptionOperationId operation_id, const std::string& server_id, const std::string& uri
  ) override {
    return RequestSubscription(operation_id, server_id, uri, false);
  }

  bool PingAsync(McpRequestToken token, const McpPingRequest& request) override {
    if (token == 0 || request.call_id.empty() || request.server_id.empty()) {
      return false;
    }
    return SubmitAsyncRequest(token, MakePendingPingRequest(request), [this, token, request]() {
      Enqueue(
        *thread_pool_,
        [this, token, request]() {
          DoPing(token, request);
        },
        /*priority=*/2
      );
    });
  }

  bool Cancel(McpRequestToken token) override {
    std::optional<ServicePendingRequest> pending;
    std::shared_ptr<IMcpClient> client;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      pending = TakePendingRequest(token, pending_requests_);
      if (pending) {
        if (const auto server = servers_.find(pending->server_id); server != servers_.end()) {
          client = server->second.client;
        }
      }
    }
    if (!pending) {
      return false;
    }

    // The wait in the transport is aborted immediately and without I/O; notifications/cancelled is sent by the woken
    // pool thread.
    if (const auto request_id = pending->cancellation->Cancel(); request_id && client) {
      client->abandonRequest(*request_id);
    }
    EmitCancelledResult(token, *pending, "Cancelled");
    return true;
  }

private:
  void EmitCancelledResult(McpRequestToken token, const ServicePendingRequest& pending, const std::string& message) {
    switch (pending.kind) {
      case ServicePendingRequest::Kind::TOOL_CALL:
        EmitToolCallResult(
          token,
          McpToolCallResultEvent{
            .call_id = pending.call_id,
            .server_id = pending.server_id,
            .tool_name = pending.tool_name,
            .is_error = true,
            .error_message = message,
            .error_code = ErrorCode::CANCELLED,
            .request_sent = pending.cancellation->WasSent(),
          }
        );
        break;
      case ServicePendingRequest::Kind::READ_RESOURCE:
        EmitReadResourceResult(
          token,
          McpReadResourceResultEvent{
            .call_id = pending.call_id,
            .server_id = pending.server_id,
            .uri = pending.uri,
            .is_error = true,
            .error_message = message,
          }
        );
        break;
      case ServicePendingRequest::Kind::PING:
        EmitPingResult(
          token,
          McpPingResultEvent{
            .call_id = pending.call_id,
            .server_id = pending.server_id,
            .is_error = true,
            .error_message = message,
          }
        );
        break;
    }
  }

  void DebugLog(const std::string& message) const {
    if (config_.debug_enabled) {
      detail::WriteMcpLog(std::string("[mcp][service] ") + message);
    }
  }

  template<class EnqueueFn>
  bool SubmitAsyncRequest(McpRequestToken token, ServicePendingRequest pending_request, EnqueueFn&& enqueue_fn) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (stopping_ || !StorePendingRequest(token, std::move(pending_request), pending_requests_)) {
        return false;
      }
    }

    enqueue_fn();
    return true;
  }

  void InstallServerCallbacks(IMcpClient& client, const std::string& server_id, uint64_t generation) {
    auto weak_self = weak_from_this();
    client.setOnToolsListChanged([weak_self, server_id, generation]() {
      if (auto self = weak_self.lock()) {
        self->ScheduleRefreshTools(server_id, generation);
      }
    });
    client.setOnResourcesListChanged([weak_self, server_id, generation]() {
      if (auto self = weak_self.lock()) {
        self->ScheduleRefreshResources(server_id, generation);
      }
    });
  }

  template<class WorkFn>
  void ScheduleServerWork(const std::string& server_id, uint64_t generation, WorkFn&& work_fn) {
    if (stopping_) {
      return;
    }
    Enqueue(*thread_pool_, [server_id, generation, work_fn = std::forward<WorkFn>(work_fn)]() mutable {
      work_fn(server_id, generation);
    });
  }

  void DoAddServer(const std::string& server_id, uint64_t generation, const ConnectionPtr& connection) {
    DebugLog("DoAddServer begin server='" + server_id + "'");
    auto client = createMcpClient(connection, config_.default_request_timeout_ms);
    if (!client) {
      SetServerError(server_id, generation, "Failed to create MCP client");
      return;
    }

    auto init_result = client->initialize(config_.protocol_version, config_.client_name, config_.client_version);
    if (!init_result.ok()) {
      SetServerError(server_id, generation, "Initialize failed: " + init_result.error.message);
      client->shutdown();
      return;
    }
    DebugLog("initialized server='" + server_id + "'");

    if (!IsServerWanted(server_id, generation)) {
      client->shutdown();
      return;
    }

    client->notifyInitialized();
    // The callbacks are installed before the initial read of the catalog: a list_changed that arrives before the
    // server commit is deferred and starts a refresh right after the commit.
    InstallServerCallbacks(*client, server_id, generation);

    auto initial_data_result = LoadInitialServerData(*client);
    if (!initial_data_result.ok()) {
      SetServerError(server_id, generation, "listTools failed: " + initial_data_result.error.message);
      client->shutdown();
      return;
    }
    DebugLog(
      "loaded catalog server='" + server_id + "' tools=" + std::to_string(initial_data_result.value.tools.size())
      + " resources=" + std::to_string(initial_data_result.value.resources.size())
      + " resource_templates=" + std::to_string(initial_data_result.value.resource_templates.size())
    );

    if (!IsServerWanted(server_id, generation)) {
      client->shutdown();
      return;
    }

    const bool supports_resource_subscriptions = client->serverCapabilities().supports_resource_subscriptions;
    AggregatedServiceSnapshots snapshots;
    bool committed = false;
    bool refresh_tools = false;
    bool refresh_resources = false;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (
        MatchesServerLocked(server_id, generation)
        && CommitConnectedServer(
          server_id,
          client,
          std::move(initial_data_result.value),
          ServerStatus::REMOVING,
          ServerStatus::READY,
          servers_,
          per_server_tools_,
          per_server_resources_,
          per_server_resource_templates_,
          AggregateTools,
          AggregateResources,
          AggregateResourceTemplates,
          snapshots
        )
      ) {
        auto& server = servers_.at(server_id);
        server.supports_resource_subscriptions = supports_resource_subscriptions;
        refresh_tools = std::exchange(server.tools_refresh.pending, false);
        refresh_resources = std::exchange(server.resource_refresh.pending, false);
        PruneServerAutoResourceKeysLocked(server_id);
        QueueCatalogLocked(std::move(snapshots));
        QueueServerStatusLocked(server_id, McpServerStatusEvent::Status::CONNECTED, "");
        committed = true;
      }
    }
    if (!committed) {
      ShutdownServerClient(client, {});
      return;
    }
    DrainObserverEvents();
    DebugLog("server ready server='" + server_id + "'");
    // The handler is set after the commit and outside the locks: a close that happened before this point is reported
    // by the transport right when the handler is set.
    client->setOnClosed([weak_self = weak_from_this(), server_id, generation]() {
      if (auto self = weak_self.lock()) {
        self->ScheduleServerClosed(server_id, generation);
      }
    });
    if (refresh_tools) {
      ScheduleRefreshTools(server_id, generation);
    }
    if (refresh_resources) {
      ScheduleRefreshResources(server_id, generation);
    }
    ScheduleAutoHydrateResources(server_id, generation);
  }

  void ScheduleServerClosed(const std::string& server_id, uint64_t generation) {
    ScheduleServerWork(server_id, generation, [this](const std::string& id, uint64_t current) {
      RemovedServerData removed_server;
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!MatchesServerLocked(id, current) || servers_.at(id).status != ServerStatus::READY) {
          return;
        }
        removed_server = DetachServerLocked(id);
      }
      detail::WriteMcpLog(std::string("[WARNING] MCP: server '") + id + "' closed its transport");
      ReleaseDetachedServer(std::move(removed_server));
    });
  }

  void DoRemoveServer(const std::string& server_id, uint64_t generation) {
    DebugLog("DoRemoveServer server='" + server_id + "'");
    RemovedServerData removed_server;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!MatchesServerLocked(server_id, generation)) {
        return;
      }
      removed_server = DetachServerLocked(server_id);
    }
    ReleaseDetachedServer(std::move(removed_server));
  }

  RemovedServerData DetachServerLocked(const std::string& server_id) {
    const auto server = servers_.find(server_id);
    std::set<std::string> busy_subscriptions;
    if (server != servers_.end()) {
      ClearServerRetriesLocked(server_id, server->second.generation);
      for (const auto& [uri, subscription] : server->second.subscriptions) {
        if (subscription.busy) {
          busy_subscriptions.insert(uri);
        }
        for (const auto& request : subscription.requests) {
          QueueSubscriptionResultLocked(
            server_id,
            uri,
            request,
            request.subscribe ? std::optional<std::string>("Server disconnected") : std::nullopt
          );
        }
      }
    }
    auto removed_server = RemoveServerFromState(
      server_id,
      servers_,
      per_server_tools_,
      per_server_resources_,
      per_server_resource_templates_,
      pending_requests_,
      AggregateTools,
      AggregateResources,
      AggregateResourceTemplates
    );
    for (const auto& uri : busy_subscriptions) {
      removed_server.subscribed_uris.erase(uri);
    }
    if (removed_server.found) {
      QueueCatalogLocked(std::move(removed_server.snapshots));
      QueueServerStatusLocked(server_id, McpServerStatusEvent::Status::DISCONNECTED, "");
    }
    return removed_server;
  }

  void ReleaseDetachedServer(RemovedServerData removed_server) {
    if (!removed_server.found) {
      return;
    }
    for (auto& [token, result] : removed_server.failed_tool_results) {
      EmitToolCallResult(token, std::move(result));
    }
    for (auto& [token, result] : removed_server.failed_read_results) {
      EmitReadResourceResult(token, std::move(result));
    }
    for (auto& [token, result] : removed_server.failed_ping_results) {
      EmitPingResult(token, std::move(result));
    }

    DrainObserverEvents();
    ShutdownServerClient(removed_server.client, removed_server.subscribed_uris);
  }

  void ReleaseDetachedServers() {
    decltype(detached_servers_) detached_servers;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      detached_servers.swap(detached_servers_);
    }
    for (auto& detached : detached_servers) {
      ReleaseDetachedServer(std::move(detached));
    }
  }

  // The server may have started executing a request cancelled after it was sent: it is notified before the result is
  // dropped.
  static void NotifyCancelledAfterSend(const PendingClientResolution& resolution, ErrorCode code) {
    if (code != ErrorCode::CANCELLED) {
      return;
    }
    if (const auto request_id = resolution.cancellation->CancelledAfterSendId()) {
      resolution.client->notifyCancelled(*request_id, "cancelled");
    }
  }

  void DoCallTool(McpRequestToken token, const McpToolCallRequest& request) {
    PendingClientResolution resolution;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      resolution = ResolvePendingReadyClient(
        token, request.server_id, ServerStatus::READY, servers_, pending_requests_
      );
    }
    if (resolution.request_missing) {
      return;
    }
    if (resolution.server_unavailable) {
      EmitToolCallResult(token, MakeServerUnavailableToolResult(request));
      return;
    }

    uint32_t timeout = request.timeout_ms > 0 ? request.timeout_ms : config_.default_tool_call_timeout_ms;

    std::string mcp_request_id;
    auto call_result = resolution.client->callTool(
      request.tool_name, request.arguments, mcp_request_id, timeout, detail::MakeSendGate(resolution), request.metadata
    );
    resolution.cancellation->Finish();
    NotifyCancelledAfterSend(resolution, call_result.error.code);

    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!ClearPendingRequest(token, pending_requests_)) {
        return;
      }
    }

    if (!call_result.ok()) {
      if (call_result.error.code == ErrorCode::TIMEOUT && !mcp_request_id.empty()) {
        resolution.client->notifyCancelled(mcp_request_id, "timeout");
      }
      EmitToolCallResult(
        token,
        McpToolCallResultEvent{
          .call_id = request.call_id,
          .server_id = request.server_id,
          .tool_name = request.tool_name,
          .is_error = true,
          .error_message = call_result.error.message,
          .error_code = call_result.error.code,
          .request_sent = resolution.cancellation->WasSent(),
        }
      );
      return;
    }

    EmitToolCallResult(
      token,
      McpToolCallResultEvent{
        .call_id = request.call_id,
        .server_id = request.server_id,
        .tool_name = request.tool_name,
        .is_error = call_result.value.is_error,
        .content = call_result.value.content,
        .structured_content = call_result.value.structured_content,
        .request_sent = resolution.cancellation->WasSent(),
      }
    );
  }

  void DoRefreshTools(const std::string& server_id, uint64_t generation) {
    std::shared_ptr<IMcpClient> client;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      client = ReadyClientLocked(server_id, generation);
    }
    if (!client) {
      return;
    }

    auto tools_result = client->listTools();
    if (!tools_result.ok()) {
      detail::WriteMcpLog(
        std::string("[WARNING] MCP: Failed to refresh tools for server '") + server_id
        + "': " + tools_result.error.message
      );
      return;
    }

    std::vector<McpToolDescriptor> aggregated;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (
        !ReadyClientLocked(server_id, generation)
        || !CommitToolsRefresh(
          server_id, std::move(tools_result.value), servers_, per_server_tools_, AggregateTools, aggregated
        )
      ) {
        return;
      }
      QueueObserverEventLocked([tools = std::move(aggregated)](IMcpServiceObserver& observer) mutable {
        observer.OnToolsSnapshotUpdated(std::move(tools));
      });
    }
    DrainObserverEvents();
  }

  void ScheduleRefreshTools(const std::string& server_id, uint64_t generation) {
    ScheduleRefresh(
      server_id,
      generation,
      &ServerState::tools_refresh,
      [this](const std::string& id, uint64_t current) {
        DoRefreshTools(id, current);
      },
      [](const std::string&, uint64_t) {}
    );
  }

  void DoReadResource(McpRequestToken token, const McpReadResourceRequest& request) {
    PendingClientResolution resolution;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      resolution = ResolvePendingReadyClient(
        token, request.server_id, ServerStatus::READY, servers_, pending_requests_
      );
    }
    if (resolution.request_missing) {
      return;
    }
    if (resolution.server_unavailable) {
      EmitReadResourceResult(token, MakeServerUnavailableReadResult(request));
      return;
    }

    uint32_t timeout = request.timeout_ms > 0 ? request.timeout_ms : config_.default_read_resource_timeout_ms;

    std::string mcp_request_id;
    auto read_result = resolution.client->readResource(
      request.uri, &mcp_request_id, timeout, detail::MakeSendGate(resolution)
    );
    resolution.cancellation->Finish();
    NotifyCancelledAfterSend(resolution, read_result.error.code);

    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!ClearPendingRequest(token, pending_requests_)) {
        return;
      }
    }

    if (!read_result.ok()) {
      if (read_result.error.code == ErrorCode::TIMEOUT && !mcp_request_id.empty()) {
        resolution.client->notifyCancelled(mcp_request_id, "timeout");
      }
      EmitReadResourceResult(
        token,
        McpReadResourceResultEvent{
          .call_id = request.call_id,
          .server_id = request.server_id,
          .uri = request.uri,
          .is_error = true,
          .error_message = read_result.error.message,
        }
      );
      return;
    }

    EmitReadResourceResult(
      token,
      McpReadResourceResultEvent{
        .call_id = request.call_id,
        .server_id = request.server_id,
        .uri = read_result.value.uri,
        .is_error = false,
        .mime_type = read_result.value.mime_type,
        .text = read_result.value.text,
      }
    );
  }

  struct SubscriptionRequest {
    McpSubscriptionOperationId id = 0;
    bool subscribe = false;
  };

  struct SubscriptionState {
    uint64_t explicit_generation = 0;
    bool busy = false;
    bool reread_after_subscribe = false;
    std::deque<SubscriptionRequest> requests;
  };

  bool SubscriptionNeededLocked(const std::string& server_id, const std::string& uri) const {
    return HasExplicitSubscriptionLocked(server_id, uri) || AutoSubscriptionNeededLocked(server_id, uri);
  }

  bool AutoSubscriptionNeededLocked(const std::string& server_id, const std::string& uri) const {
    const auto& server = servers_.at(server_id);
    if (
      !config_.resource_hydration.subscribe_to_subscribable_resources || !server.client
      || !server.supports_resource_subscriptions
    ) {
      return false;
    }
    const auto resources = per_server_resources_.find(server_id);
    return resources != per_server_resources_.end()
      && std::any_of(resources->second.begin(), resources->second.end(), [&](const ResourceInfo& resource) {
             return resource.uri == uri && resource.subscribable;
           });
  }

  bool HasExplicitSubscriptionLocked(const std::string& server_id, const std::string& uri) const {
    const auto& subscriptions = servers_.at(server_id).subscriptions;
    const auto it = subscriptions.find(uri);
    return it != subscriptions.end() && it->second.explicit_generation != 0;
  }

  void QueueSubscriptionResultLocked(
    const std::string& server_id,
    const std::string& uri,
    const SubscriptionRequest& request,
    const std::optional<std::string>& error = std::nullopt
  ) {
    QueueObserverEventLocked(
      [event = MakeSubscriptionEvent(
         request.id,
         server_id,
         uri,
         request.subscribe ? McpResourceSubscriptionEvent::Operation::SUBSCRIBE
                           : McpResourceSubscriptionEvent::Operation::UNSUBSCRIBE,
         error.has_value(),
         error.value_or("")
       )](IMcpServiceObserver& observer) mutable {
        observer.OnResourceSubscriptionUpdated(std::move(event));
      }
    );
  }

  bool RequestSubscription(
    McpSubscriptionOperationId operation_id, const std::string& server_id, const std::string& uri, bool subscribe
  ) {
    if (stopping_ || operation_id == 0 || server_id.empty() || uri.empty()) {
      return false;
    }
    bool schedule = false;
    uint64_t generation = 0;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      const auto server = servers_.find(server_id);
      bool has_pending = false;
      if (server != servers_.end()) {
        const auto subscription = server->second.subscriptions.find(uri);
        has_pending = subscription != server->second.subscriptions.end() && !subscription->second.requests.empty();
      }
      if (server == servers_.end() || (server->second.status == ServerStatus::REMOVING && !has_pending)) {
        QueueSubscriptionResultLocked(
          server_id,
          uri,
          SubscriptionRequest{operation_id, subscribe},
          subscribe ? std::optional<std::string>("Server unavailable") : std::nullopt
        );
      } else {
        server->second.subscriptions[uri].requests.push_back({operation_id, subscribe});
        generation = server->second.generation;
        schedule = BeginSubscriptionLocked(server_id, uri);
      }
    }
    if (schedule) {
      ScheduleSubscriptionWork(server_id, generation, uri);
    }
    DrainObserverEvents();
    return true;
  }

  bool BeginSubscriptionLocked(const std::string& server_id, const std::string& uri) {
    auto& server = servers_.at(server_id);
    auto& subscription = server.subscriptions[uri];
    if (subscription.busy) {
      return false;
    }
    if (
      subscription.requests.empty()
      && SubscriptionNeededLocked(server_id, uri) == (server.subscribed_resource_uris.count(uri) != 0)
    ) {
      ResetResourceRetry(server_id, server.generation, uri, 0, RetryKind::SUBSCRIBE);
      if (!SubscriptionNeededLocked(server_id, uri)) {
        server.subscriptions.erase(uri);
      }
      return false;
    }
    subscription.busy = true;
    return true;
  }

  void ScheduleSubscriptionWork(const std::string& server_id, uint64_t generation, const std::string& uri) {
    ScheduleServerWork(server_id, generation, [this, uri](const std::string& id, uint64_t current) {
      DoReconcileSubscription(id, current, uri);
    });
  }

  bool ReconcileSubscription(
    const std::string& server_id, uint64_t generation, const std::string& uri, uint64_t retry_id = 0
  ) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (
        !ReadyClientLocked(server_id, generation)
        || (retry_id != 0 && !IsCurrentResourceRetry(server_id, generation, uri, 0, RetryKind::SUBSCRIBE, retry_id))
      ) {
        return false;
      }
      auto& subscription = servers_.at(server_id).subscriptions[uri];
      if (retry_id == 0 && subscription.busy) {
        subscription.reread_after_subscribe = true;
      }
      if (!BeginSubscriptionLocked(server_id, uri)) {
        return false;
      }
    }
    return DoReconcileSubscription(server_id, generation, uri, retry_id);
  }

  bool DoReconcileSubscription(
    const std::string& server_id, uint64_t generation, const std::string& uri, uint64_t retry_id = 0
  ) {
    std::shared_ptr<IMcpClient> client;
    std::optional<SubscriptionRequest> request;
    bool subscribe = false;
    bool needs_operation = false;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!MatchesServerLocked(server_id, generation)) {
        return false;
      }
      auto& server = servers_.at(server_id);
      const auto it = server.subscriptions.find(uri);
      if (it == server.subscriptions.end()) {
        return false;
      }
      client = ReadyClientLocked(server_id, generation);
      const bool active = server.subscribed_resource_uris.count(uri) != 0;
      if (!it->second.requests.empty()) {
        request = it->second.requests.front();
        subscribe = request->subscribe;
        needs_operation = subscribe ? !active : active && !AutoSubscriptionNeededLocked(server_id, uri);
        if (subscribe && it->second.explicit_generation == 0) {
          it->second.explicit_generation = ++next_resource_generation_;
        }
      } else {
        subscribe = SubscriptionNeededLocked(server_id, uri);
        needs_operation = subscribe != active;
      }
    }
    Result<void> result;
    if (subscribe && !client) {
      result.error.code = ErrorCode::TRANSPORT_ERROR;
      result.error.message = "Server unavailable";
    } else if (subscribe && !client->serverCapabilities().supports_resource_subscriptions) {
      result.error.code = ErrorCode::METHOD_NOT_FOUND;
      result.error.message = "Server does not support resource subscriptions: " + server_id;
    } else if (client && needs_operation) {
      if (subscribe) {
        auto weak_self = weak_from_this();
        result = client->subscribeResource(uri, [weak_self, server_id, generation](const ResourceNotification& event) {
          if (auto self = weak_self.lock()) {
            self->OnResourceNotification(server_id, generation, event);
          }
        });
      } else {
        result = client->unsubscribeResource(uri);
      }
    }

    bool reread = false;
    bool schedule_next = false;
    uint32_t retry_delay = 0;
    uint64_t resource_generation = 0;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!MatchesServerLocked(server_id, generation)) {
        return false;
      }
      auto& server = servers_.at(server_id);
      auto& subscription = server.subscriptions.at(uri);
      subscription.busy = false;
      const bool ready = static_cast<bool>(ReadyClientLocked(server_id, generation));
      if (request) {
        subscription.requests.pop_front();
        std::optional<std::string> error;
        if (!result.ok()) {
          error = result.error.message;
        } else if (subscribe && !ready) {
          error = "Server disconnected";
        }
        if ((subscribe && error) || (!subscribe && !error)) {
          ResetResourceRetry(server_id, generation, uri, subscription.explicit_generation, RetryKind::READ);
          subscription.explicit_generation = 0;
        }
        QueueSubscriptionResultLocked(server_id, uri, *request, error);
      }
      if (result.ok()) {
        if (client && needs_operation) {
          if (subscribe) {
            server.subscribed_resource_uris.insert(uri);
          } else {
            server.subscribed_resource_uris.erase(uri);
          }
        }
        if (subscribe && subscription.reread_after_subscribe) {
          reread = SubscriptionNeededLocked(server_id, uri);
          subscription.reread_after_subscribe = false;
          const auto resource = server.resource_generations.find(uri);
          resource_generation = resource != server.resource_generations.end()
            ? resource->second
            : subscription.explicit_generation;
        }
        ResetResourceRetry(server_id, generation, uri, 0, RetryKind::SUBSCRIBE);
      } else if (!request && subscribe) {
        subscription.reread_after_subscribe = true;
      }
      const bool needed = SubscriptionNeededLocked(server_id, uri);
      const bool active = server.subscribed_resource_uris.count(uri) != 0;
      if (!ready) {
        ResetResourceRetry(server_id, generation, uri, 0, RetryKind::SUBSCRIBE);
        if (!subscription.requests.empty()) {
          schedule_next = BeginSubscriptionLocked(server_id, uri);
        } else if (!active && subscription.explicit_generation == 0) {
          server.subscriptions.erase(uri);
        }
      } else if (request || result.ok() || !subscription.requests.empty() || needed == active) {
        schedule_next = BeginSubscriptionLocked(server_id, uri);
      } else {
        retry_delay = ScheduleResourceRetryLocked(
          server_id, generation, uri, 0, RetryKind::SUBSCRIBE, result.error, retry_id
        );
      }
    }
    if (!request && !result.ok()) {
      detail::WriteMcpLog(
        std::string("[WARNING] MCP: auto ") + (subscribe ? "subscribeResource" : "unsubscribeResource")
        + " failed for '" + server_id + "' uri '" + uri + "': " + result.error.message + RetrySuffix(retry_delay)
      );
    }
    if (schedule_next) {
      ScheduleSubscriptionWork(server_id, generation, uri);
    }
    DrainObserverEvents();
    if (reread) {
      ScheduleAutoReadResource(server_id, generation, uri, resource_generation);
      return true;
    }
    return false;
  }

  void DoPing(McpRequestToken token, const McpPingRequest& request) {
    PendingClientResolution resolution;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      resolution = ResolvePendingReadyClient(
        token, request.server_id, ServerStatus::READY, servers_, pending_requests_
      );
    }
    if (resolution.request_missing) {
      return;
    }
    if (resolution.server_unavailable) {
      EmitPingResult(token, MakeServerUnavailablePingResult(request));
      return;
    }

    uint32_t timeout = request.timeout_ms > 0 ? request.timeout_ms : config_.default_ping_timeout_ms;

    auto ping_result = resolution.client->ping(timeout, detail::MakeSendGate(resolution));
    resolution.cancellation->Finish();
    NotifyCancelledAfterSend(resolution, ping_result.error.code);

    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!ClearPendingRequest(token, pending_requests_)) {
        return;
      }
    }

    if (!ping_result.ok()) {
      EmitPingResult(
        token,
        McpPingResultEvent{
          .call_id = request.call_id,
          .server_id = request.server_id,
          .is_error = true,
          .error_message = ping_result.error.message,
        }
      );
      return;
    }

    EmitPingResult(
      token,
      McpPingResultEvent{
        .call_id = request.call_id,
        .server_id = request.server_id,
        .is_error = false,
      }
    );
  }

  void DoRefreshResources(const std::string& server_id, uint64_t generation) {
    std::shared_ptr<IMcpClient> client;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      client = ReadyClientLocked(server_id, generation);
    }
    if (!client || !client->serverCapabilities().supports_resources) {
      return;
    }

    auto resources_result = client->listResources();
    if (!resources_result.ok()) {
      detail::WriteMcpLog(
        std::string("[WARNING] MCP: Failed to refresh resources for server '") + server_id
        + "': " + resources_result.error.message
      );
      return;
    }

    auto refresh_data = RefreshResourceData{
      .resources = std::move(resources_result.value),
    };

    auto templates_result = client->listResourceTemplates();
    if (!templates_result.ok()) {
      detail::WriteMcpLog(
        std::string("[WARNING] MCP: Failed to refresh resource templates for server '") + server_id
        + "': " + templates_result.error.message
      );
    } else {
      refresh_data.resource_templates = std::move(templates_result.value);
    }

    std::vector<std::string> subscription_work;
    std::vector<McpResourceDescriptor> aggregated_resources;
    std::vector<McpResourceTemplateDescriptor> aggregated_templates;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (
        !ReadyClientLocked(server_id, generation)
        || !CommitResourcesRefresh(
          server_id,
          std::move(refresh_data),
          servers_,
          per_server_resources_,
          per_server_resource_templates_,
          AggregateResources,
          AggregateResourceTemplates,
          aggregated_resources,
          aggregated_templates
        )
      ) {
        return;
      }
      PruneServerAutoResourceKeysLocked(server_id);
      std::vector<std::string> subscriptions;
      for (const auto& [uri, state] : servers_.at(server_id).subscriptions) {
        if (!SubscriptionNeededLocked(server_id, uri)) {
          subscriptions.push_back(uri);
        }
      }
      for (const auto& uri : subscriptions) {
        if (BeginSubscriptionLocked(server_id, uri)) {
          subscription_work.push_back(uri);
        }
      }
      QueueObserverEventLocked([resources = std::move(aggregated_resources),
                                templates = std::move(aggregated_templates)](IMcpServiceObserver& observer) mutable {
        observer.OnResourcesSnapshotUpdated(std::move(resources));
        observer.OnResourceTemplatesSnapshotUpdated(std::move(templates));
      });
    }
    for (const auto& uri : subscription_work) {
      ScheduleSubscriptionWork(server_id, generation, uri);
    }
    DrainObserverEvents();
  }

  void ScheduleAutoHydrateResources(const std::string& server_id, uint64_t generation) {
    if (
      !config_.resource_hydration.read_resources_on_list
      && !config_.resource_hydration.subscribe_to_subscribable_resources
    ) {
      return;
    }
    ScheduleServerWork(server_id, generation, [this](const std::string& id, uint64_t current) {
      DoAutoHydrateResources(id, current);
    });
  }

  static std::string AutoResourceKey(const std::string& server_id, uint64_t generation, const std::string& uri) {
    return MakeResourceKey(server_id, uri) + "#" + std::to_string(generation);
  }

  void OnResourceNotification(
    const std::string& server_id, uint64_t generation, const ResourceNotification& notification
  ) {
    uint64_t resource_generation = 0;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!ReadyClientLocked(server_id, generation)) {
        return;
      }
      const auto& resources = servers_.at(server_id).resource_generations;
      const auto it = resources.find(notification.uri);
      if (it != resources.end()) {
        resource_generation = it->second;
      } else if (HasExplicitSubscriptionLocked(server_id, notification.uri)) {
        resource_generation = servers_.at(server_id).subscriptions.at(notification.uri).explicit_generation;
      } else {
        return;
      }
    }
    // resources/updated carries only the uri: the resource state is whatever a re-read returns.
    ScheduleAutoReadResource(server_id, generation, notification.uri, resource_generation);
  }

  struct AutoReadRequest {
    uint64_t resource_generation = 0;
    uint64_t retry_id = 0;
  };

  bool MatchesResourceLocked(
    const std::string& server_id, uint64_t generation, const std::string& uri, uint64_t resource_generation
  ) {
    if (!ReadyClientLocked(server_id, generation)) {
      return false;
    }
    const auto& server = servers_.at(server_id);
    const auto it = server.resource_generations.find(uri);
    const auto subscription = server.subscriptions.find(uri);
    return resource_generation != 0
      && ((it != server.resource_generations.end() && it->second == resource_generation)
          || (subscription != server.subscriptions.end() && subscription->second.explicit_generation == resource_generation));
  }

  void ScheduleAutoReadResource(
    const std::string& server_id,
    uint64_t generation,
    const std::string& uri,
    uint64_t resource_generation,
    uint64_t retry_id = 0
  ) {
    {
      std::lock_guard<std::mutex> state_lock(state_mutex_);
      if (!MatchesResourceLocked(server_id, generation, uri, resource_generation)) {
        if (retry_id != 0) {
          ResetResourceRetry(server_id, generation, uri, resource_generation, RetryKind::READ, retry_id);
        }
        return;
      }
      std::lock_guard<std::mutex> lock(auto_resource_mutex_);
      if (retry_id != 0) {
        const auto retry = retries_.find(RetryKey(server_id, generation, uri, resource_generation, RetryKind::READ));
        if (retry == retries_.end() || retry->second.id != retry_id) {
          return;
        }
      }
      const auto [it, inserted] = auto_reads_.emplace(AutoResourceKey(server_id, generation, uri), std::nullopt);
      if (!inserted) {
        if (!it->second || retry_id == 0 || it->second->retry_id != 0) {
          it->second = AutoReadRequest{resource_generation, retry_id};
        }
        return;
      }
    }
    ScheduleAutoReadWork(server_id, generation, uri, AutoReadRequest{resource_generation, retry_id});
  }

  void ScheduleAutoReadWork(
    const std::string& server_id, uint64_t generation, const std::string& uri, AutoReadRequest request
  ) {
    ScheduleServerWork(server_id, generation, [this, uri, request](const std::string& id, uint64_t current) {
      DoAutoReadResource(id, current, uri, request.resource_generation, request.retry_id);
      AutoReadRequest next_request;
      {
        std::lock_guard<std::mutex> lock(auto_resource_mutex_);
        const auto it = auto_reads_.find(AutoResourceKey(id, current, uri));
        if (it == auto_reads_.end()) {
          return;
        }
        if (!it->second) {
          auto_reads_.erase(it);
          return;
        }
        next_request = *it->second;
        it->second.reset();
      }
      ScheduleAutoReadWork(id, current, uri, next_request);
    });
  }

  void DoAutoHydrateResources(const std::string& server_id, uint64_t generation) {
    std::vector<ResourceInfo> resources;
    std::optional<std::string> preferred_read_uri;
    uint64_t revision = 0;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!ReadyClientLocked(server_id, generation)) {
        return;
      }
      const auto resources_it = per_server_resources_.find(server_id);
      if (resources_it == per_server_resources_.end()) {
        return;
      }
      resources = resources_it->second;
      preferred_read_uri = SelectPreferredReadResourceUri(resources, config_.resource_hydration);
      revision = servers_.at(server_id).resources_revision;
    }
    if (preferred_read_uri) {
      std::stable_partition(resources.begin(), resources.end(), [&](const ResourceInfo& resource) {
        return resource.uri == *preferred_read_uri;
      });
    }
    DoAutoHydrateResource(
      server_id,
      generation,
      revision,
      std::make_shared<const std::vector<ResourceInfo>>(std::move(resources)),
      preferred_read_uri,
      0
    );
  }

  void DoAutoHydrateResource(
    const std::string& server_id,
    uint64_t generation,
    uint64_t revision,
    const std::shared_ptr<const std::vector<ResourceInfo>>& resources,
    const std::optional<std::string>& preferred_read_uri,
    size_t index
  ) {
    if (index >= resources->size()) {
      return;
    }
    const auto& resource = resources->at(index);
    uint64_t resource_generation = 0;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!ReadyClientLocked(server_id, generation)) {
        return;
      }
      // While a list refresh is in flight, a step over the old list could subscribe to an already removed resource:
      // the chain stops, and the finished refresh starts the hydration over the fresh list.
      const auto& server = servers_.at(server_id);
      if (server.resources_revision != revision || server.resource_refresh.running) {
        return;
      }
      resource_generation = server.resource_generations.at(resource.uri);
    }
    bool reread_scheduled = false;
    if (config_.resource_hydration.subscribe_to_subscribable_resources && resource.subscribable) {
      reread_scheduled = ReconcileSubscription(server_id, generation, resource.uri);
    }
    if (config_.resource_hydration.read_resources_on_list && !reread_scheduled) {
      if (preferred_read_uri) {
        if (resource.uri == *preferred_read_uri) {
          ScheduleAutoReadResource(server_id, generation, resource.uri, resource_generation);
        }
      } else if (MarkAutoResourceReadScheduled(server_id, generation, resource.uri, resource_generation)) {
        ScheduleAutoReadResource(server_id, generation, resource.uri, resource_generation);
      }
    }
    ScheduleServerWork(
      server_id,
      generation,
      [this, revision, resources, preferred_read_uri, index](const std::string& id, uint64_t current) {
        DoAutoHydrateResource(id, current, revision, resources, preferred_read_uri, index + 1);
      }
    );
  }

  bool MarkAutoResourceReadScheduled(
    const std::string& server_id, uint64_t generation, const std::string& uri, uint64_t resource_generation
  ) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!MatchesResourceLocked(server_id, generation, uri, resource_generation)) {
      return false;
    }
    return servers_.at(server_id).auto_read_resource_keys.insert(MakeResourceKey(server_id, uri)).second;
  }

  void PruneServerAutoResourceKeysLocked(const std::string& server_id) {
    auto server_it = servers_.find(server_id);
    if (server_it == servers_.end()) {
      return;
    }

    std::set<std::string> current_keys;
    auto resources_it = per_server_resources_.find(server_id);
    if (resources_it != per_server_resources_.end()) {
      for (const auto& resource : resources_it->second) {
        current_keys.insert(MakeResourceKey(server_id, resource.uri));
      }
    }

    auto& server = server_it->second;
    ++server.resources_revision;
    {
      std::lock_guard<std::mutex> lock(auto_resource_mutex_);
      for (auto it = server.resource_generations.begin(); it != server.resource_generations.end();) {
        if (current_keys.count(MakeResourceKey(server_id, it->first)) != 0) {
          ++it;
          continue;
        }
        retries_.erase(RetryKey(server_id, server.generation, it->first, it->second, RetryKind::READ));
        const auto read = auto_reads_.find(AutoResourceKey(server_id, server.generation, it->first));
        if (read != auto_reads_.end() && read->second && read->second->resource_generation == it->second) {
          read->second.reset();
        }
        it = server.resource_generations.erase(it);
      }
    }
    if (resources_it != per_server_resources_.end()) {
      for (const auto& resource : resources_it->second) {
        if (server.resource_generations.count(resource.uri) == 0) {
          server.resource_generations.emplace(resource.uri, ++next_resource_generation_);
        }
      }
    }

    for (auto it = server_it->second.auto_read_resource_keys.begin();
         it != server_it->second.auto_read_resource_keys.end();) {
      if (current_keys.count(*it) == 0) {
        it = server_it->second.auto_read_resource_keys.erase(it);
      } else {
        ++it;
      }
    }
  }

  void ClearAutoResourceReadScheduled(
    const std::string& server_id, uint64_t generation, const std::string& uri, uint64_t resource_generation
  ) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = servers_.find(server_id);
    if (MatchesResourceLocked(server_id, generation, uri, resource_generation)) {
      it->second.auto_read_resource_keys.erase(MakeResourceKey(server_id, uri));
    }
  }

  void DoAutoReadResource(
    const std::string& server_id,
    uint64_t generation,
    const std::string& uri,
    uint64_t resource_generation,
    uint64_t retry_id
  ) {
    if (
      retry_id != 0
      && !IsCurrentResourceRetry(server_id, generation, uri, resource_generation, RetryKind::READ, retry_id)
    ) {
      return;
    }
    std::shared_ptr<IMcpClient> client;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (MatchesResourceLocked(server_id, generation, uri, resource_generation)) {
        client = ReadyClientLocked(server_id, generation);
      }
    }
    if (!client) {
      ClearAutoResourceReadScheduled(server_id, generation, uri, resource_generation);
      ResetResourceRetry(server_id, generation, uri, resource_generation, RetryKind::READ);
      return;
    }

    auto read_result = client->readResource(uri, nullptr, config_.default_read_resource_timeout_ms);
    if (!read_result.ok()) {
      ClearAutoResourceReadScheduled(server_id, generation, uri, resource_generation);
      const auto delay = ScheduleResourceRetry(
        server_id, generation, uri, resource_generation, RetryKind::READ, read_result.error, retry_id
      );
      detail::WriteMcpLog(
        std::string("[WARNING] MCP: auto readResource failed for '") + server_id + "' uri '" + uri
        + "': " + read_result.error.message + RetrySuffix(delay)
      );
      return;
    }
    ResetResourceRetry(server_id, generation, uri, resource_generation, RetryKind::READ);
    PublishResourceUpdate(
      generation,
      McpResourceUpdatedEvent{
        .server_id = server_id,
        .uri = read_result.value.uri,
        .data = BuildResourceContentState(read_result.value),
      },
      resource_generation,
      true
    );
  }

  enum class RetryKind { READ, SUBSCRIBE };

  struct PendingRetry {
    std::string server_id;
    uint64_t generation = 0;
    std::string uri;
    uint64_t resource_generation = 0;
    RetryKind kind = RetryKind::READ;
    uint64_t id = 0;
    std::chrono::steady_clock::time_point due;
    uint32_t delay_ms = 0;
    bool queued = false;
  };

  static std::string RetryKey(
    const std::string& server_id,
    uint64_t generation,
    const std::string& uri,
    uint64_t resource_generation,
    RetryKind kind
  ) {
    return AutoResourceKey(server_id, generation, uri) + "#" + std::to_string(resource_generation)
      + (kind == RetryKind::READ ? "#read" : "#subscribe");
  }

  static std::string RetrySuffix(uint32_t delay_ms) {
    return delay_ms == 0 ? ", no retry" : ", retry in " + std::to_string(delay_ms) + " ms";
  }

  void ClearServerRetriesLocked(const std::string& server_id, uint64_t generation) {
    std::lock_guard<std::mutex> lock(auto_resource_mutex_);
    for (auto it = retries_.begin(); it != retries_.end();) {
      if (it->second.server_id == server_id && it->second.generation == generation) {
        it = retries_.erase(it);
      } else {
        ++it;
      }
    }
    retry_cv_.notify_one();
  }

  uint32_t ScheduleResourceRetry(
    const std::string& server_id,
    uint64_t generation,
    const std::string& uri,
    uint64_t resource_generation,
    RetryKind kind,
    const Error& error,
    uint64_t retry_id = 0
  ) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return ScheduleResourceRetryLocked(server_id, generation, uri, resource_generation, kind, error, retry_id);
  }

  uint32_t ScheduleResourceRetryLocked(
    const std::string& server_id,
    uint64_t generation,
    const std::string& uri,
    uint64_t resource_generation,
    RetryKind kind,
    const Error& error,
    uint64_t retry_id
  ) {
    const bool current = kind == RetryKind::READ
      ? MatchesResourceLocked(server_id, generation, uri, resource_generation)
      : static_cast<bool>(ReadyClientLocked(server_id, generation));
    if (!current) {
      ResetResourceRetry(server_id, generation, uri, resource_generation, kind, retry_id);
      return 0;
    }
    const auto& hydration = config_.resource_hydration;
    const auto& permanent = hydration.no_retry_error_codes;
    const auto key = RetryKey(server_id, generation, uri, resource_generation, kind);
    std::lock_guard<std::mutex> lock(auto_resource_mutex_);
    if (retry_id != 0) {
      const auto pending = retries_.find(key);
      if (pending == retries_.end() || pending->second.id != retry_id) {
        return 0;
      }
    }
    if (
      stopping_ || hydration.retry_initial_ms == 0
      || std::find(permanent.begin(), permanent.end(), error.json_rpc_code.value_or(static_cast<int32_t>(error.code)))
        != permanent.end()
    ) {
      retries_.erase(key);
      return 0;
    }
    const auto now = std::chrono::steady_clock::now();
    auto& pending = retries_[key];
    if (pending.queued) {
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(pending.due - now).count();
      return static_cast<uint32_t>(std::max<int64_t>(1, left));
    }
    pending.delay_ms = detail::NextResourceRetryDelay(
      pending.delay_ms, hydration.retry_initial_ms, hydration.retry_max_ms
    );
    pending.server_id = server_id;
    pending.generation = generation;
    pending.uri = uri;
    pending.resource_generation = resource_generation;
    pending.kind = kind;
    pending.id = ++next_retry_id_;
    pending.due = now + std::chrono::milliseconds(pending.delay_ms);
    pending.queued = true;
    if (!retry_thread_.joinable()) {
      retry_thread_ = std::thread([this]() {
        TrySetCurrentThreadName("mcp_retry");
        RunResourceRetries();
      });
    }
    retry_cv_.notify_one();
    return pending.delay_ms;
  }

  void ResetResourceRetry(
    const std::string& server_id,
    uint64_t generation,
    const std::string& uri,
    uint64_t resource_generation,
    RetryKind kind,
    uint64_t retry_id = 0
  ) {
    std::lock_guard<std::mutex> lock(auto_resource_mutex_);
    const auto it = retries_.find(RetryKey(server_id, generation, uri, resource_generation, kind));
    if (it != retries_.end() && (retry_id == 0 || it->second.id == retry_id)) {
      retries_.erase(it);
    }
  }

  bool IsCurrentResourceRetry(
    const std::string& server_id,
    uint64_t generation,
    const std::string& uri,
    uint64_t resource_generation,
    RetryKind kind,
    uint64_t retry_id
  ) {
    std::lock_guard<std::mutex> lock(auto_resource_mutex_);
    const auto it = retries_.find(RetryKey(server_id, generation, uri, resource_generation, kind));
    return it != retries_.end() && it->second.id == retry_id;
  }

  void RunResourceRetries() {
    std::unique_lock<std::mutex> lock(auto_resource_mutex_);
    while (!stopping_) {
      PendingRetry* next = nullptr;
      for (auto& [key, pending] : retries_) {
        if (pending.queued && (next == nullptr || pending.due < next->due)) {
          next = &pending;
        }
      }
      if (next == nullptr) {
        retry_cv_.wait(lock);
        continue;
      }
      // The entry may be erased during the wait.
      const auto due = next->due;
      if (std::chrono::steady_clock::now() < due) {
        retry_cv_.wait_until(lock, due);
        continue;
      }
      next->queued = false;
      const auto retry = *next;
      lock.unlock();
      ScheduleServerWork(retry.server_id, retry.generation, [this, retry](const std::string& id, uint64_t current) {
        DoResourceRetry(id, current, retry.uri, retry.resource_generation, retry.kind, retry.id);
      });
      lock.lock();
    }
  }

  void DoResourceRetry(
    const std::string& server_id,
    uint64_t generation,
    const std::string& uri,
    uint64_t resource_generation,
    RetryKind kind,
    uint64_t retry_id
  ) {
    if (!IsCurrentResourceRetry(server_id, generation, uri, resource_generation, kind, retry_id)) {
      return;
    }
    if (kind == RetryKind::SUBSCRIBE) {
      ReconcileSubscription(server_id, generation, uri, retry_id);
      return;
    }
    ScheduleAutoReadResource(server_id, generation, uri, resource_generation, retry_id);
  }

  void ScheduleRefreshResources(const std::string& server_id, uint64_t generation) {
    ScheduleRefresh(
      server_id,
      generation,
      &ServerState::resource_refresh,
      [this](const std::string& id, uint64_t current) {
        DoRefreshResources(id, current);
      },
      [this](const std::string& id, uint64_t current) {
        ScheduleAutoHydrateResources(id, current);
      }
    );
  }

  bool MatchesServerLocked(const std::string& server_id, uint64_t generation) const {
    const auto it = servers_.find(server_id);
    return it != servers_.end() && it->second.generation == generation;
  }

  std::shared_ptr<IMcpClient> ReadyClientLocked(const std::string& server_id, uint64_t generation) {
    return MatchesServerLocked(server_id, generation)
      ? ResolveReadyServerClient(server_id, ServerStatus::READY, servers_)
      : nullptr;
  }

  bool IsServerWanted(const std::string& server_id, uint64_t generation) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return MatchesServerLocked(server_id, generation) && servers_.at(server_id).status != ServerStatus::REMOVING;
  }

  void SetServerError(const std::string& server_id, uint64_t generation, const std::string& message) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!MatchesServerLocked(server_id, generation) || servers_.at(server_id).status == ServerStatus::REMOVING) {
        return;
      }
      servers_.at(server_id).status = ServerStatus::ERROR;
      QueueServerStatusLocked(server_id, McpServerStatusEvent::Status::ERROR, message);
    }
    detail::WriteMcpLog(std::string("[WARNING] MCP server '") + server_id + "': " + message);
    DrainObserverEvents();
  }

  void QueueObserverEventLocked(std::function<void(IMcpServiceObserver&)> event) {
    observer_events_.emplace_back([observer = observer_, event = std::move(event)]() {
      if (auto target = observer.lock()) {
        event(*target);
      }
    });
  }

  void DrainObserverEvents() {
    std::unique_lock<std::mutex> lock(state_mutex_);
    if (dispatching_events_) {
      return;
    }
    dispatching_events_ = true;
    while (!observer_events_.empty()) {
      auto event = std::move(observer_events_.front());
      observer_events_.pop_front();
      lock.unlock();
      try {
        event();
      } catch (const std::exception& e) {
        detail::WriteMcpLog(std::string("[WARNING] MCP observer: ") + e.what());
      } catch (...) {
        detail::WriteMcpLog(std::string("[WARNING] MCP observer failed"));
      }
      lock.lock();
    }
    dispatching_events_ = false;
  }

  void EmitObserverEvent(std::function<void(IMcpServiceObserver&)> event) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      QueueObserverEventLocked(std::move(event));
    }
    DrainObserverEvents();
  }

  void QueueCatalogLocked(AggregatedServiceSnapshots snapshots) {
    QueueObserverEventLocked([snapshots = std::move(snapshots)](IMcpServiceObserver& observer) mutable {
      observer.OnToolsSnapshotUpdated(std::move(snapshots.tools));
      observer.OnResourcesSnapshotUpdated(std::move(snapshots.resources));
      observer.OnResourceTemplatesSnapshotUpdated(std::move(snapshots.resource_templates));
    });
  }

  void QueueServerStatusLocked(
    const std::string& server_id, McpServerStatusEvent::Status status, const std::string& message
  ) {
    QueueObserverEventLocked([event = McpServerStatusEvent{
                                .server_id = server_id, .status = status, .message = message
                              }](IMcpServiceObserver& observer) mutable {
      observer.OnServerStatusChanged(std::move(event));
    });
  }

  void PublishResourceUpdate(
    uint64_t generation, McpResourceUpdatedEvent event, uint64_t resource_generation, bool mark_read = false
  ) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!MatchesResourceLocked(event.server_id, generation, event.uri, resource_generation)) {
        return;
      }
      if (mark_read && servers_.at(event.server_id).resource_generations.count(event.uri) != 0) {
        servers_.at(event.server_id).auto_read_resource_keys.insert(MakeResourceKey(event.server_id, event.uri));
      }
      QueueObserverEventLocked([event = std::move(event)](IMcpServiceObserver& observer) mutable {
        observer.OnResourceUpdated(std::move(event));
      });
    }
    DrainObserverEvents();
  }

  void EmitToolCallResult(McpRequestToken token, McpToolCallResultEvent result) {
    DebugLog(
      "ToolCallCompleted token=" + std::to_string(token) + " call_id='" + result.call_id + "' server='"
      + result.server_id + "' tool='" + result.tool_name
      + "' is_error=" + std::string(result.is_error ? "true" : "false")
    );
    EmitObserverEvent([token, result = std::move(result)](IMcpServiceObserver& observer) mutable {
      observer.OnToolCallCompleted(token, std::move(result));
    });
  }

  void EmitReadResourceResult(McpRequestToken token, McpReadResourceResultEvent result) {
    EmitObserverEvent([token, result = std::move(result)](IMcpServiceObserver& observer) mutable {
      observer.OnReadResourceCompleted(token, std::move(result));
    });
  }

  void EmitPingResult(McpRequestToken token, McpPingResultEvent result) {
    EmitObserverEvent([token, result = std::move(result)](IMcpServiceObserver& observer) mutable {
      observer.OnPingCompleted(token, std::move(result));
    });
  }

  enum class ServerStatus {
    INITIALIZING,
    READY,
    REMOVING,
    ERROR,
  };

  struct RefreshFlight {
    bool running = false;
    bool pending = false;
  };

  struct ServerState {
    uint64_t generation = 0;
    // Only for comparison; the client owns it.
    std::weak_ptr<IConnection> connection;
    std::shared_ptr<IMcpClient> client;
    std::set<std::string> subscribed_resource_uris;
    uint64_t resources_revision = 0;
    bool supports_resource_subscriptions = false;
    RefreshFlight tools_refresh;
    RefreshFlight resource_refresh;
    std::unordered_map<std::string, uint64_t> resource_generations;
    std::set<std::string> auto_read_resource_keys;
    std::unordered_map<std::string, SubscriptionState> subscriptions;
    ServerStatus status = ServerStatus::INITIALIZING;
  };

  // Refreshes of one list of a server run one at a time: the response of a refresh started earlier is not committed
  // over a fresher one. Notifications during a refresh collapse into a single repeated refresh; finish_fn runs after
  // the last one.
  template<class RefreshFn, class FinishFn>
  void ScheduleRefresh(
    const std::string& server_id,
    uint64_t generation,
    RefreshFlight ServerState::* flight,
    RefreshFn refresh_fn,
    FinishFn finish_fn
  ) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      if (!MatchesServerLocked(server_id, generation)) {
        return;
      }
      auto& server = servers_.at(server_id);
      auto& state = server.*flight;
      if (state.running || server.status == ServerStatus::INITIALIZING) {
        state.pending = true;
        return;
      }
      if (!ReadyClientLocked(server_id, generation)) {
        return;
      }
      state.running = true;
    }
    RunRefresh(server_id, generation, flight, std::move(refresh_fn), std::move(finish_fn));
  }

  template<class RefreshFn, class FinishFn>
  void RunRefresh(
    const std::string& server_id,
    uint64_t generation,
    RefreshFlight ServerState::* flight,
    RefreshFn refresh_fn,
    FinishFn finish_fn
  ) {
    ScheduleServerWork(
      server_id, generation, [this, flight, refresh_fn, finish_fn](const std::string& id, uint64_t current) {
        refresh_fn(id, current);
        bool again = false;
        {
          std::lock_guard<std::mutex> lock(state_mutex_);
          if (!MatchesServerLocked(id, current)) {
            return;
          }
          auto& state = servers_.at(id).*flight;
          again = state.pending && static_cast<bool>(ReadyClientLocked(id, current));
          state.pending = false;
          state.running = again;
        }
        if (again) {
          RunRefresh(id, current, flight, refresh_fn, finish_fn);
        } else {
          finish_fn(id, current);
        }
      }
    );
  }

  McpServiceConfig config_;
  std::once_flag shutdown_once_;
  std::atomic<bool> stopping_{false};
  std::unique_ptr<IThreadPool> thread_pool_;

  mutable std::mutex state_mutex_;
  std::weak_ptr<IMcpServiceObserver> observer_;
  std::deque<std::function<void()>> observer_events_;
  bool dispatching_events_ = false;
  uint64_t next_generation_ = 0;
  uint64_t next_resource_generation_ = 0;
  std::unordered_map<std::string, ServerState> servers_;
  std::unordered_map<std::string, std::vector<ToolInfo>> per_server_tools_;
  std::unordered_map<std::string, std::vector<ResourceInfo>> per_server_resources_;
  std::unordered_map<std::string, std::vector<ResourceTemplateInfo>> per_server_resource_templates_;
  std::unordered_map<McpRequestToken, ServicePendingRequest> pending_requests_;
  std::vector<RemovedServerData> detached_servers_;

  mutable std::mutex auto_resource_mutex_;
  std::unordered_map<std::string, std::optional<AutoReadRequest>> auto_reads_;
  std::unordered_map<std::string, PendingRetry> retries_;
  uint64_t next_retry_id_ = 0;
  std::condition_variable retry_cv_;
  std::thread retry_thread_;
};

size_t GetMcpResourceRetryCount(const IMcpService& service) {
  const auto& impl = dynamic_cast<const McpServiceImpl&>(service);
  std::lock_guard<std::mutex> lock(impl.auto_resource_mutex_);
  return impl.retries_.size();
}

std::shared_ptr<IMcpService> CreateMcpService(McpServiceConfig config) {
  return CreateMcpService(std::move(config), nullptr);
}

std::shared_ptr<IMcpService> CreateMcpService(McpServiceConfig config, std::unique_ptr<IThreadPool> thread_pool) {
  if (
    config.default_request_timeout_ms == 0 || config.default_tool_call_timeout_ms == 0
    || config.default_read_resource_timeout_ms == 0 || config.default_ping_timeout_ms == 0
  ) {
    throw std::invalid_argument("MCP default timeouts must be positive");
  }
  // A ThreadPool with zero threads does not run tasks: the service would silently hang on the very first server.
  if (
    !thread_pool
    && (config.thread_pool_size == 0 || config.thread_pool_size > static_cast<uint32_t>(std::numeric_limits<int>::max()))
  ) {
    throw std::invalid_argument("MCP thread_pool_size must be in [1, INT_MAX]");
  }
  return std::make_shared<McpServiceImpl>(std::move(config), std::move(thread_pool));
}

} // namespace mcp
} // namespace giga_edge
