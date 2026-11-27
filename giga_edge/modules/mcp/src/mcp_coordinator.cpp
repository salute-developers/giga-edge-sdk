#include "giga_edge/modules/mcp/coordinator.h"
#include "giga_edge/modules/mcp/coordinator/completion_router.h"
#include "giga_edge/modules/mcp/coordinator/snapshot_builder.h"
#include "giga_edge/modules/mcp/coordinator/snapshot_helpers.h"
#include "giga_edge/modules/mcp/coordinator/subscription_manager.h"
#include "giga_edge/modules/mcp/coordinator/subscription_view_helpers.h"
#include "giga_edge/modules/mcp/log.h"

#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace giga_edge {
namespace mcp {

namespace {

std::string MakeResourceKey(const std::string& server_id, const std::string& uri) {
  return server_id + "\n" + uri;
}

} // namespace

using detail::ApplyResourceSubscriptionEvent;
using detail::BuildLatestResourceStatesSnapshotData;
using detail::BuildResourcesSnapshotData;
using detail::BuildResourceSubscriptionStatesSnapshotData;
using detail::BuildResourceTemplatesSnapshotData;
using detail::BuildToolsSnapshotData;
using detail::DeliverCompletion;
using detail::EnsureSubscriptionIntent;
using detail::ErasePendingRequest;
using detail::EraseServerLatestStates;
using detail::EraseServerResourceSubscriptions;
using detail::ExtractPendingTokensForConsumer;
using detail::IsPendingRequestOwnedBy;
using detail::MarkRejectedResourceOperation;
using detail::MatchesLatestResourceState;
using detail::MatchesResource;
using detail::MatchesResourceSubscriptionState;
using detail::MatchesResourceTemplate;
using detail::MatchesTool;
using detail::PendingRequest;
using detail::PendingRequestKind;
using detail::RemoveConsumerSubscription;
using detail::RemoveServerDataFromConsumer;
using detail::ResolveLatestResourceStateForKey;
using detail::ResourceOperationKind;
using detail::ResourceSubscriptionEntry;
using detail::ScheduledResourceOperation;
using detail::SortLatestResourceStates;
using detail::SortResources;
using detail::SortResourceSubscriptionStates;
using detail::SortResourceTemplates;
using detail::SortTools;
using detail::StorePendingRequest;
using detail::TakeResults;
using detail::TrimQueue;
using detail::UpdateVersionedSnapshot;

class McpCoordinatorImpl : public IMcpCoordinator, public IMcpServiceObserver {
public:
  explicit McpCoordinatorImpl(std::shared_ptr<IMcpService> service)
    : service_(std::move(service)) {
    if (service_) {
      all_tools_ = service_->GetToolsSnapshot();
      all_resources_ = service_->GetResourcesSnapshot();
      all_resource_templates_ = service_->GetResourceTemplatesSnapshot();
    }
    SortTools(all_tools_);
    SortResources(all_resources_);
    SortResourceTemplates(all_resource_templates_);
  }

  McpConsumerId RegisterConsumer(McpConsumerConfig config) override {
    std::lock_guard<std::mutex> lock(mutex_);

    ConsumerState consumer;
    consumer.config = SanitizeConfig(std::move(config));
    DebugLog(
      consumer.config,
      "RegisterConsumer name='" + consumer.config.consumer_name
        + "' visible_servers=" + std::to_string(consumer.config.visible_server_ids.size())
        + " shared_latest=" + std::string(consumer.config.include_shared_latest_resource_states ? "true" : "false")
    );
    SeedLatestResourceStatesLocked(consumer);
    RebuildAllSnapshots(consumer, /*force_increment=*/true);

    const McpConsumerId id = next_consumer_id_++;
    const auto consumer_name = consumer.config.consumer_name;
    consumers_.emplace(id, std::move(consumer));
    if (!consumer_name.empty()) {
      consumers_by_name_[consumer_name] = id;
    }
    return id;
  }

  McpConsumerId EnsureConsumer(McpConsumerConfig config) override {
    std::lock_guard<std::mutex> lock(mutex_);

    auto sanitized = SanitizeConfig(std::move(config));
    if (!sanitized.consumer_name.empty()) {
      const auto name_it = consumers_by_name_.find(sanitized.consumer_name);
      if (name_it != consumers_by_name_.end()) {
        return name_it->second;
      }
    }

    ConsumerState consumer;
    consumer.config = std::move(sanitized);
    DebugLog(
      consumer.config,
      "EnsureConsumer name='" + consumer.config.consumer_name
        + "' visible_servers=" + std::to_string(consumer.config.visible_server_ids.size())
        + " shared_latest=" + std::string(consumer.config.include_shared_latest_resource_states ? "true" : "false")
    );
    SeedLatestResourceStatesLocked(consumer);
    RebuildAllSnapshots(consumer, /*force_increment=*/true);

    const McpConsumerId id = next_consumer_id_++;
    const auto consumer_name = consumer.config.consumer_name;
    consumers_.emplace(id, std::move(consumer));
    if (!consumer_name.empty()) {
      consumers_by_name_[consumer_name] = id;
    }
    return id;
  }

  void UnregisterConsumer(McpConsumerId consumer_id) override {
    std::vector<McpRequestToken> tokens_to_cancel;
    std::vector<ScheduledResourceOperation> scheduled_operations;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = consumers_.find(consumer_id);
      if (it == consumers_.end()) {
        return;
      }

      for (const auto& key : it->second.desired_resource_keys) {
        scheduled_operations.push_back(RemoveConsumerSubscriptionLocked(key));
      }

      tokens_to_cancel = ExtractPendingTokensForConsumer(pending_requests_, consumer_id);

      if (!it->second.config.consumer_name.empty()) {
        consumers_by_name_.erase(it->second.config.consumer_name);
      }
      consumers_.erase(it);
    }

    for (auto token : tokens_to_cancel) {
      if (service_) {
        service_->Cancel(token);
      }
    }
    for (const auto& operation : scheduled_operations) {
      SubmitResourceOperation(operation);
    }
  }

  void UpdateConsumerConfig(McpConsumerId consumer_id, McpConsumerConfig config) override {
    std::vector<ScheduledResourceOperation> scheduled_operations;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      UpdateConsumerConfigLocked(consumer_id, std::move(config), scheduled_operations);
    }

    for (const auto& operation : scheduled_operations) {
      SubmitResourceOperation(operation);
    }
  }

  void UpdateConsumerConfigLocked(
    McpConsumerId consumer_id, McpConsumerConfig config, std::vector<ScheduledResourceOperation>& scheduled_operations
  ) {
    auto it = consumers_.find(consumer_id);
    if (it == consumers_.end()) {
      return;
    }

    auto sanitized = SanitizeConfig(std::move(config));
    for (auto sub_it = it->second.desired_resource_keys.begin(); sub_it != it->second.desired_resource_keys.end();) {
      const auto [server_id, uri] = ParseResourceKey(*sub_it);
      if (IsServerVisible(sanitized, server_id)) {
        ++sub_it;
        continue;
      }

      const std::string key = *sub_it;
      sub_it = it->second.desired_resource_keys.erase(sub_it);
      it->second.latest_resource_states.erase(key);
      scheduled_operations.push_back(RemoveConsumerSubscriptionLocked(key));
    }

    it->second.config = std::move(sanitized);
    TrimCompletionQueuesIfNeeded(it->second);
    RebuildAllSnapshots(it->second, /*force_increment=*/true);
  }

  SubmitResult SubmitToolCall(McpConsumerId consumer_id, const McpToolCallRequest& request) override {
    DebugLog(
      consumer_id,
      "SubmitToolCall call_id='" + request.call_id + "' server='" + request.server_id + "' tool='" + request.tool_name
        + "'"
    );
    if (request.call_id.empty() || request.server_id.empty() || request.tool_name.empty()) {
      return {.accepted = false, .error_message = "Invalid tool call request"};
    }
    return SubmitValidatedRequest(
      consumer_id,
      request.server_id,
      PendingRequestKind::TOOL_CALL,
      "Failed to submit MCP tool call",
      [&](McpRequestToken token) {
        return service_->CallToolAsync(token, request);
      }
    );
  }

  SubmitResult SubmitReadResource(McpConsumerId consumer_id, const McpReadResourceRequest& request) override {
    DebugLog(
      consumer_id,
      "SubmitReadResource call_id='" + request.call_id + "' server='" + request.server_id + "' uri='" + request.uri
        + "'"
    );
    if (request.call_id.empty() || request.server_id.empty() || request.uri.empty()) {
      return {.accepted = false, .error_message = "Invalid resource read request"};
    }
    return SubmitValidatedRequest(
      consumer_id,
      request.server_id,
      PendingRequestKind::READ_RESOURCE,
      "Failed to submit MCP resource read",
      [&](McpRequestToken token) {
        return service_->ReadResourceAsync(token, request);
      }
    );
  }

  SubmitResult SubmitPing(McpConsumerId consumer_id, const McpPingRequest& request) override {
    if (request.call_id.empty() || request.server_id.empty()) {
      return {.accepted = false, .error_message = "Invalid ping request"};
    }
    return SubmitValidatedRequest(
      consumer_id,
      request.server_id,
      PendingRequestKind::PING,
      "Failed to submit MCP ping",
      [&](McpRequestToken token) {
        return service_->PingAsync(token, request);
      }
    );
  }

  bool CancelRequest(McpConsumerId consumer_id, McpRequestToken token) override {
    if (!service_) {
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!IsPendingRequestOwnedBy(pending_requests_, token, consumer_id)) {
        return false;
      }
    }

    return service_->Cancel(token);
  }

  TakeBatchResult<McpToolCallResultEvent> TakeToolCallResults(McpConsumerId consumer_id, size_t limit) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = consumers_.find(consumer_id);
    if (it == consumers_.end()) {
      return {};
    }
    return TakeResults(it->second.tool_call_results, it->second.tool_call_overflow, limit);
  }

  TakeBatchResult<McpReadResourceResultEvent> TakeReadResourceResults(
    McpConsumerId consumer_id, size_t limit
  ) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = consumers_.find(consumer_id);
    if (it == consumers_.end()) {
      return {};
    }
    return TakeResults(it->second.read_resource_results, it->second.read_resource_overflow, limit);
  }

  TakeBatchResult<McpPingResultEvent> TakePingResults(McpConsumerId consumer_id, size_t limit) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = consumers_.find(consumer_id);
    if (it == consumers_.end()) {
      return {};
    }
    return TakeResults(it->second.ping_results, it->second.ping_overflow, limit);
  }

  VersionedSnapshot<std::vector<McpToolDescriptor>> GetToolsSnapshot(McpConsumerId consumer_id) const override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = consumers_.find(consumer_id); it != consumers_.end()) {
      return it->second.tools_snapshot;
    }
    return {};
  }

  VersionedSnapshot<std::vector<McpResourceDescriptor>> GetResourcesSnapshot(McpConsumerId consumer_id) const override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = consumers_.find(consumer_id); it != consumers_.end()) {
      return it->second.resources_snapshot;
    }
    return {};
  }

  VersionedSnapshot<std::vector<McpResourceTemplateDescriptor>> GetResourceTemplatesSnapshot(
    McpConsumerId consumer_id
  ) const override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = consumers_.find(consumer_id); it != consumers_.end()) {
      return it->second.resource_templates_snapshot;
    }
    return {};
  }

  bool SubscribeResource(McpConsumerId consumer_id, const std::string& server_id, const std::string& uri) override {
    DebugLog(consumer_id, "SubscribeResource server='" + server_id + "' uri='" + uri + "'");
    if (!service_) {
      return false;
    }
    if (server_id.empty() || uri.empty()) {
      return false;
    }

    ScheduledResourceOperation scheduled_operation;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!PrepareSubscribeResourceLocked(consumer_id, server_id, uri, scheduled_operation)) {
        return false;
      }
    }
    return SubmitResourceOperation(scheduled_operation);
  }

  void UnsubscribeResource(McpConsumerId consumer_id, const std::string& server_id, const std::string& uri) override {
    DebugLog(consumer_id, "UnsubscribeResource server='" + server_id + "' uri='" + uri + "'");
    if (!service_) {
      return;
    }
    if (server_id.empty() || uri.empty()) {
      return;
    }

    ScheduledResourceOperation scheduled_operation;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!PrepareUnsubscribeResourceLocked(consumer_id, server_id, uri, scheduled_operation)) {
        return;
      }
    }
    SubmitResourceOperation(scheduled_operation);
  }

  VersionedSnapshot<std::vector<McpResourceSubscriptionState>> GetResourceSubscriptionStatesSnapshot(
    McpConsumerId consumer_id
  ) const override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = consumers_.find(consumer_id); it != consumers_.end()) {
      return it->second.resource_subscription_states_snapshot;
    }
    return {};
  }

  VersionedSnapshot<std::vector<McpLatestResourceState>> GetLatestResourceStatesSnapshot(
    McpConsumerId consumer_id
  ) const override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = consumers_.find(consumer_id); it != consumers_.end()) {
      return it->second.latest_resource_states_snapshot;
    }
    return {};
  }

  void OnToolsSnapshotUpdated(std::vector<McpToolDescriptor> tools) override {
    std::lock_guard<std::mutex> lock(mutex_);
    HandleToolsSnapshotUpdatedLocked(std::move(tools));
  }

  void OnResourcesSnapshotUpdated(std::vector<McpResourceDescriptor> resources) override {
    std::lock_guard<std::mutex> lock(mutex_);
    HandleResourcesSnapshotUpdatedLocked(std::move(resources));
  }

  void OnResourceTemplatesSnapshotUpdated(std::vector<McpResourceTemplateDescriptor> resource_templates) override {
    std::lock_guard<std::mutex> lock(mutex_);
    HandleResourceTemplatesSnapshotUpdatedLocked(std::move(resource_templates));
  }

  void OnToolCallCompleted(McpRequestToken token, McpToolCallResultEvent result) override {
    std::lock_guard<std::mutex> lock(mutex_);
    DeliverCompletion(
      consumers_,
      pending_requests_,
      token,
      PendingRequestKind::TOOL_CALL,
      std::move(result),
      [](auto& consumer) -> auto& {
        return consumer.tool_call_results;
      },
      [](auto& consumer) -> auto& {
        return consumer.tool_call_overflow;
      },
      "tool_call_results"
    );
  }

  void OnReadResourceCompleted(McpRequestToken token, McpReadResourceResultEvent result) override {
    std::lock_guard<std::mutex> lock(mutex_);
    DeliverCompletion(
      consumers_,
      pending_requests_,
      token,
      PendingRequestKind::READ_RESOURCE,
      std::move(result),
      [](auto& consumer) -> auto& {
        return consumer.read_resource_results;
      },
      [](auto& consumer) -> auto& {
        return consumer.read_resource_overflow;
      },
      "read_resource_results"
    );
  }

  void OnPingCompleted(McpRequestToken token, McpPingResultEvent result) override {
    std::lock_guard<std::mutex> lock(mutex_);
    DeliverCompletion(
      consumers_,
      pending_requests_,
      token,
      PendingRequestKind::PING,
      std::move(result),
      [](auto& consumer) -> auto& {
        return consumer.ping_results;
      },
      [](auto& consumer) -> auto& {
        return consumer.ping_overflow;
      },
      "ping_results"
    );
  }

  void OnResourceUpdated(McpResourceUpdatedEvent event) override {
    std::lock_guard<std::mutex> lock(mutex_);
    ApplyLatestResourceUpdateLocked(std::move(event));
  }

  void OnResourceSubscriptionUpdated(McpResourceSubscriptionEvent event) override {
    ScheduledResourceOperation scheduled_operation;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!ApplySubscriptionEventLocked(event, scheduled_operation)) {
        return;
      }
    }
    SubmitResourceOperation(scheduled_operation);
  }

  void OnServerStatusChanged(McpServerStatusEvent event) override {
    if (event.status != McpServerStatusEvent::Status::DISCONNECTED) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    HandleDisconnectedServerLocked(event.server_id);
  }

private:
  void DebugLog(const McpConsumerConfig& config, const std::string& message) const {
    if (config.debug_enabled) {
      detail::WriteMcpLog(std::string("[mcp][coordinator][") + config.consumer_name + "] " + message);
    }
  }

  void DebugLog(McpConsumerId consumer_id, const std::string& message) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = consumers_.find(consumer_id);
    if (it == consumers_.end()) {
      return;
    }
    DebugLog(it->second.config, message);
  }

  struct ConsumerState {
    McpConsumerConfig config;

    std::deque<McpCompletion<McpToolCallResultEvent>> tool_call_results;
    std::deque<McpCompletion<McpReadResourceResultEvent>> read_resource_results;
    std::deque<McpCompletion<McpPingResultEvent>> ping_results;

    bool tool_call_overflow = false;
    bool read_resource_overflow = false;
    bool ping_overflow = false;

    std::unordered_set<std::string> desired_resource_keys;
    std::unordered_map<std::string, McpLatestResourceState> latest_resource_states;

    VersionedSnapshot<std::vector<McpToolDescriptor>> tools_snapshot;
    VersionedSnapshot<std::vector<McpResourceDescriptor>> resources_snapshot;
    VersionedSnapshot<std::vector<McpResourceTemplateDescriptor>> resource_templates_snapshot;
    VersionedSnapshot<std::vector<McpResourceSubscriptionState>> resource_subscription_states_snapshot;
    VersionedSnapshot<std::vector<McpLatestResourceState>> latest_resource_states_snapshot;
  };

  static McpConsumerConfig SanitizeConfig(McpConsumerConfig config) {
    if (config.completion_queue_capacity == 0) {
      config.completion_queue_capacity = 1;
    }
    return config;
  }

  static bool IsServerVisible(const McpConsumerConfig& config, const std::string& server_id) {
    return config.visible_server_ids.empty() || config.visible_server_ids.count(server_id) > 0;
  }

  static std::pair<std::string, std::string> ParseResourceKey(const std::string& key) {
    const size_t pos = key.find('\n');
    if (pos == std::string::npos) {
      return {key, ""};
    }
    return {key.substr(0, pos), key.substr(pos + 1)};
  }

  static std::unordered_set<std::string> BuildKnownResourceKeys(const std::vector<McpResourceDescriptor>& resources) {
    std::unordered_set<std::string> result;
    result.reserve(resources.size());
    for (const auto& resource : resources) {
      result.insert(MakeResourceKey(resource.server_id, resource.uri));
    }
    return result;
  }

  bool EraseUnknownLatestResourceStatesLocked(const std::unordered_set<std::string>& valid_resource_keys) {
    bool changed = false;
    for (auto it = latest_resource_states_.begin(); it != latest_resource_states_.end();) {
      if (valid_resource_keys.count(it->first) == 0) {
        it = latest_resource_states_.erase(it);
        changed = true;
      } else {
        ++it;
      }
    }
    for (auto& [consumer_id, consumer] : consumers_) {
      static_cast<void>(consumer_id);
      for (auto it = consumer.latest_resource_states.begin(); it != consumer.latest_resource_states.end();) {
        if (valid_resource_keys.count(it->first) == 0) {
          it = consumer.latest_resource_states.erase(it);
          changed = true;
        } else {
          ++it;
        }
      }
    }
    return changed;
  }

  SubmitResult ReservePendingRequestLocked(
    McpConsumerId consumer_id, const std::string& server_id, PendingRequestKind kind
  ) {
    auto it = consumers_.find(consumer_id);
    if (it == consumers_.end()) {
      return {.accepted = false, .error_message = "Unknown MCP consumer"};
    }
    if (!IsServerVisible(it->second.config, server_id)) {
      return {.accepted = false, .error_message = "MCP server is not visible for this consumer"};
    }

    return {
      .accepted = true,
      .token = StorePendingRequest(next_request_token_, pending_requests_, consumer_id, kind),
    };
  }

  SubmitResult RejectSubmittedRequestLocked(McpRequestToken token, const char* error_message) {
    ErasePendingRequest(pending_requests_, token);
    return {.accepted = false, .error_message = error_message};
  }

  template<class SubmitFn>
  SubmitResult SubmitValidatedRequest(
    McpConsumerId consumer_id,
    const std::string& server_id,
    PendingRequestKind kind,
    const char* submit_error_message,
    SubmitFn&& submit_fn
  ) {
    if (!service_) {
      return {.accepted = false, .error_message = "MCP service is not available"};
    }

    SubmitResult reserved;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      reserved = ReservePendingRequestLocked(consumer_id, server_id, kind);
    }
    if (!reserved.accepted) {
      return reserved;
    }

    if (!submit_fn(reserved.token)) {
      std::lock_guard<std::mutex> lock(mutex_);
      return RejectSubmittedRequestLocked(reserved.token, submit_error_message);
    }

    return reserved;
  }

  void TrimCompletionQueuesIfNeeded(ConsumerState& consumer) {
    TrimQueue(
      consumer.config.consumer_name,
      consumer.config.completion_queue_capacity,
      consumer.tool_call_results,
      consumer.tool_call_overflow,
      "tool_call_results"
    );
    TrimQueue(
      consumer.config.consumer_name,
      consumer.config.completion_queue_capacity,
      consumer.read_resource_results,
      consumer.read_resource_overflow,
      "read_resource_results"
    );
    TrimQueue(
      consumer.config.consumer_name,
      consumer.config.completion_queue_capacity,
      consumer.ping_results,
      consumer.ping_overflow,
      "ping_results"
    );
  }

  void UpdateConsumersForResourceKeyLocked(
    const std::string& key, bool force_subscription_increment, bool force_latest_increment
  ) {
    for (auto& [consumer_id, consumer] : consumers_) {
      static_cast<void>(consumer_id);
      if (
        consumer.desired_resource_keys.count(key) == 0 && consumer.latest_resource_states.count(key) == 0
        && !consumer.config.include_shared_latest_resource_states
      ) {
        continue;
      }

      const auto [server_id, uri] = ParseResourceKey(key);
      static_cast<void>(uri);
      const auto& consumer_config = consumer.config;
      auto latest_state = ResolveLatestResourceStateForKey(
        consumer.desired_resource_keys,
        resource_subscriptions_,
        latest_resource_states_,
        key,
        server_id,
        consumer_config.include_shared_latest_resource_states,
        [&](const std::string& visible_server_id) {
          return IsServerVisible(consumer_config, visible_server_id);
        }
      );
      if (latest_state) {
        consumer.latest_resource_states[key] = std::move(*latest_state);
      } else {
        consumer.latest_resource_states.erase(key);
      }
      RebuildResourceSubscriptionStatesSnapshot(consumer, force_subscription_increment);
      RebuildLatestResourceStatesSnapshot(consumer, force_latest_increment);
    }
  }

  void RefreshResourceKeySnapshotsLocked(const std::string& key) {
    UpdateConsumersForResourceKeyLocked(
      key,
      /*force_subscription_increment=*/true,
      /*force_latest_increment=*/true
    );
  }

  void HandleToolsSnapshotUpdatedLocked(std::vector<McpToolDescriptor> tools) {
    SortTools(tools);
    all_tools_ = std::move(tools);
    for (auto& [consumer_id, consumer] : consumers_) {
      static_cast<void>(consumer_id);
      RebuildToolsSnapshot(consumer, /*force_increment=*/false);
    }
  }

  void HandleResourcesSnapshotUpdatedLocked(std::vector<McpResourceDescriptor> resources) {
    SortResources(resources);
    all_resources_ = std::move(resources);
    const auto valid_resource_keys = BuildKnownResourceKeys(all_resources_);
    const bool latest_states_changed = EraseUnknownLatestResourceStatesLocked(valid_resource_keys);
    for (auto& [consumer_id, consumer] : consumers_) {
      static_cast<void>(consumer_id);
      RebuildResourcesSnapshot(consumer, /*force_increment=*/false);
      if (latest_states_changed) {
        RebuildLatestResourceStatesSnapshot(consumer, /*force_increment=*/false);
      }
    }
  }

  void HandleResourceTemplatesSnapshotUpdatedLocked(std::vector<McpResourceTemplateDescriptor> resource_templates) {
    SortResourceTemplates(resource_templates);
    all_resource_templates_ = std::move(resource_templates);
    for (auto& [consumer_id, consumer] : consumers_) {
      static_cast<void>(consumer_id);
      RebuildResourceTemplatesSnapshot(consumer, /*force_increment=*/false);
    }
  }

  void ApplyLatestResourceUpdateLocked(McpResourceUpdatedEvent event) {
    const std::string key = MakeResourceKey(event.server_id, event.uri);
    latest_resource_states_[key] = McpLatestResourceState{
      .server_id = event.server_id,
      .uri = event.uri,
      .data = std::move(event.data),
    };

    auto subscription_it = resource_subscriptions_.find(key);
    const bool physically_subscribed = subscription_it != resource_subscriptions_.end()
      && subscription_it->second.physically_subscribed;

    for (auto& [consumer_id, consumer] : consumers_) {
      static_cast<void>(consumer_id);
      const bool consumer_requested_resource = consumer.desired_resource_keys.count(key) > 0;
      if (!IsServerVisible(consumer.config, event.server_id)) {
        continue;
      }
      if (!consumer_requested_resource && !consumer.config.include_shared_latest_resource_states) {
        continue;
      }
      if (!physically_subscribed && !consumer.config.include_shared_latest_resource_states) {
        continue;
      }

      consumer.latest_resource_states[key] = latest_resource_states_[key];
      RebuildLatestResourceStatesSnapshot(consumer, /*force_increment=*/false);
    }
  }

  bool PrepareSubscribeResourceLocked(
    McpConsumerId consumer_id,
    const std::string& server_id,
    const std::string& uri,
    ScheduledResourceOperation& scheduled_operation
  ) {
    auto it = consumers_.find(consumer_id);
    if (it == consumers_.end() || !IsServerVisible(it->second.config, server_id)) {
      return false;
    }

    const std::string key = MakeResourceKey(server_id, uri);
    auto desired_inserted = it->second.desired_resource_keys.insert(key);
    if (desired_inserted.second) {
      auto& entry = resource_subscriptions_[key];
      ++entry.desired_refcount;
    }
    scheduled_operation = EnsureSubscriptionIntentLocked(key, /*allow_subscribe_retry=*/true);
    RefreshResourceKeySnapshotsLocked(key);
    return true;
  }

  bool PrepareUnsubscribeResourceLocked(
    McpConsumerId consumer_id,
    const std::string& server_id,
    const std::string& uri,
    ScheduledResourceOperation& scheduled_operation
  ) {
    auto it = consumers_.find(consumer_id);
    if (it == consumers_.end()) {
      return false;
    }

    const std::string key = MakeResourceKey(server_id, uri);
    if (it->second.desired_resource_keys.erase(key) == 0) {
      return false;
    }

    it->second.latest_resource_states.erase(key);
    scheduled_operation = RemoveConsumerSubscriptionLocked(key);
    RefreshResourceKeySnapshotsLocked(key);
    return true;
  }

  bool ApplySubscriptionEventLocked(
    const McpResourceSubscriptionEvent& event, ScheduledResourceOperation& scheduled_operation
  ) {
    const std::string key = MakeResourceKey(event.server_id, event.uri);
    if (!ApplyResourceSubscriptionEvent(event, resource_subscriptions_, latest_resource_states_, key)) {
      return false;
    }

    scheduled_operation = EnsureSubscriptionIntentLocked(key, /*allow_subscribe_retry=*/false);
    RefreshResourceKeySnapshotsLocked(key);
    auto entry_it = resource_subscriptions_.find(key);
    if (entry_it != resource_subscriptions_.end() && detail::ShouldEraseSubscriptionEntry(entry_it->second)) {
      resource_subscriptions_.erase(key);
    }
    return true;
  }

  void HandleDisconnectedServerLocked(const std::string& server_id) {
    EraseServerResourceSubscriptions(server_id, resource_subscriptions_, ParseResourceKey);
    EraseServerLatestStates(server_id, latest_resource_states_);

    for (auto& [id, consumer] : consumers_) {
      static_cast<void>(id);
      if (RemoveServerDataFromConsumer(consumer, server_id, ParseResourceKey)) {
        RebuildResourceSubscriptionStatesSnapshot(consumer, /*force_increment=*/true);
        RebuildLatestResourceStatesSnapshot(consumer, /*force_increment=*/true);
      }
    }
  }

  ScheduledResourceOperation EnsureSubscriptionIntentLocked(const std::string& key, bool allow_subscribe_retry) {
    return EnsureSubscriptionIntent(
      key, resource_subscriptions_, next_subscription_operation_id_, allow_subscribe_retry, ParseResourceKey
    );
  }

  ScheduledResourceOperation RemoveConsumerSubscriptionLocked(const std::string& key) {
    return RemoveConsumerSubscription(key, resource_subscriptions_, next_subscription_operation_id_, ParseResourceKey);
  }

  bool SubmitResourceOperation(const ScheduledResourceOperation& operation) {
    if (operation.kind == ResourceOperationKind::NONE) {
      return true;
    }
    if (!service_) {
      return false;
    }

    bool accepted = false;
    if (operation.kind == ResourceOperationKind::SUBSCRIBE) {
      accepted = service_->SubscribeResourceAsync(operation.operation_id, operation.server_id, operation.uri);
    } else {
      accepted = service_->UnsubscribeResourceAsync(operation.operation_id, operation.server_id, operation.uri);
    }

    if (accepted) {
      return true;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = MakeResourceKey(operation.server_id, operation.uri);
    if (!MarkRejectedResourceOperation(operation, resource_subscriptions_, key)) {
      return false;
    }

    UpdateConsumersForResourceKeyLocked(
      key,
      /*force_subscription_increment=*/true,
      /*force_latest_increment=*/true
    );
    return false;
  }

  // Consumers registered after a resource was read would otherwise never see its state:
  // updates are only pushed to consumers that already exist.
  void SeedLatestResourceStatesLocked(ConsumerState& consumer) const {
    if (!consumer.config.include_shared_latest_resource_states) {
      return;
    }
    for (const auto& [key, state] : latest_resource_states_) {
      if (!IsServerVisible(consumer.config, state.server_id)) {
        continue;
      }
      consumer.latest_resource_states[key] = state;
    }
  }

  void RebuildAllSnapshots(ConsumerState& consumer, bool force_increment) {
    RebuildToolsSnapshot(consumer, force_increment);
    RebuildResourcesSnapshot(consumer, force_increment);
    RebuildResourceTemplatesSnapshot(consumer, force_increment);
    RebuildResourceSubscriptionStatesSnapshot(consumer, force_increment);
    RebuildLatestResourceStatesSnapshot(consumer, force_increment);
  }

  void RebuildToolsSnapshot(ConsumerState& consumer, bool force_increment) {
    auto filtered = BuildToolsSnapshotData(all_tools_, [&](const std::string& server_id) {
      return IsServerVisible(consumer.config, server_id);
    });
    UpdateVersionedSnapshot(consumer.tools_snapshot, std::move(filtered), force_increment, MatchesTool, SortTools);
  }

  void RebuildResourcesSnapshot(ConsumerState& consumer, bool force_increment) {
    auto filtered = BuildResourcesSnapshotData(all_resources_, [&](const std::string& server_id) {
      return IsServerVisible(consumer.config, server_id);
    });
    UpdateVersionedSnapshot(
      consumer.resources_snapshot, std::move(filtered), force_increment, MatchesResource, SortResources
    );
  }

  void RebuildResourceTemplatesSnapshot(ConsumerState& consumer, bool force_increment) {
    auto filtered = BuildResourceTemplatesSnapshotData(all_resource_templates_, [&](const std::string& server_id) {
      return IsServerVisible(consumer.config, server_id);
    });
    UpdateVersionedSnapshot(
      consumer.resource_templates_snapshot,
      std::move(filtered),
      force_increment,
      MatchesResourceTemplate,
      SortResourceTemplates
    );
  }

  void RebuildResourceSubscriptionStatesSnapshot(ConsumerState& consumer, bool force_increment) {
    auto filtered = BuildResourceSubscriptionStatesSnapshotData(
      consumer.desired_resource_keys,
      [&](const std::string& key) {
        return detail::BuildSubscriptionState(key, resource_subscriptions_, ParseResourceKey);
      },
      [&](const std::string& server_id) {
        return IsServerVisible(consumer.config, server_id);
      }
    );
    UpdateVersionedSnapshot(
      consumer.resource_subscription_states_snapshot,
      std::move(filtered),
      force_increment,
      MatchesResourceSubscriptionState,
      SortResourceSubscriptionStates
    );
  }

  void RebuildLatestResourceStatesSnapshot(ConsumerState& consumer, bool force_increment) {
    auto filtered = BuildLatestResourceStatesSnapshotData(
      consumer.latest_resource_states,
      consumer.desired_resource_keys,
      consumer.config.include_shared_latest_resource_states,
      [&](const std::string& server_id) {
        return IsServerVisible(consumer.config, server_id);
      }
    );
    UpdateVersionedSnapshot(
      consumer.latest_resource_states_snapshot,
      std::move(filtered),
      force_increment,
      MatchesLatestResourceState,
      SortLatestResourceStates
    );
  }

  std::shared_ptr<IMcpService> service_;

  mutable std::mutex mutex_;
  McpConsumerId next_consumer_id_ = 1;
  McpRequestToken next_request_token_ = 1;
  McpSubscriptionOperationId next_subscription_operation_id_ = 1;
  std::unordered_map<std::string, McpConsumerId> consumers_by_name_;
  std::unordered_map<McpConsumerId, ConsumerState> consumers_;
  std::unordered_map<McpRequestToken, PendingRequest> pending_requests_;

  std::vector<McpToolDescriptor> all_tools_;
  std::vector<McpResourceDescriptor> all_resources_;
  std::vector<McpResourceTemplateDescriptor> all_resource_templates_;
  std::unordered_map<std::string, McpLatestResourceState> latest_resource_states_;
  std::unordered_map<std::string, ResourceSubscriptionEntry> resource_subscriptions_;
};

std::shared_ptr<IMcpCoordinator> CreateMcpCoordinator(std::shared_ptr<IMcpService> service) {
  auto service_ptr = service;
  auto coordinator = std::make_shared<McpCoordinatorImpl>(std::move(service));
  if (service_ptr) {
    std::shared_ptr<IMcpServiceObserver> observer = coordinator;
    service_ptr->SetObserver(observer);
  }
  return coordinator;
}

} // namespace mcp
} // namespace giga_edge
