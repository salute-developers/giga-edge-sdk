#pragma once

#include "giga_edge/modules/mcp/types.h"

#include <string>
#include <unordered_map>

namespace giga_edge {
namespace mcp {
namespace detail {

enum class ResourceOperationKind {
  NONE,
  SUBSCRIBE,
  UNSUBSCRIBE,
};

struct ScheduledResourceOperation {
  ResourceOperationKind kind = ResourceOperationKind::NONE;
  McpSubscriptionOperationId operation_id = 0;
  std::string server_id;
  std::string uri;
};

struct ResourceSubscriptionEntry {
  size_t desired_refcount = 0;
  bool physically_subscribed = false;
  bool subscribe_in_flight = false;
  bool unsubscribe_in_flight = false;
  McpSubscriptionOperationId active_operation_id = 0;
  std::string last_error;
};

inline bool ShouldEraseSubscriptionEntry(const ResourceSubscriptionEntry& entry) {
  return entry.desired_refcount == 0 && !entry.physically_subscribed && !entry.subscribe_in_flight
    && !entry.unsubscribe_in_flight;
}

template<class ParseKeyFn>
ScheduledResourceOperation EnsureSubscriptionIntent(
  const std::string& key,
  std::unordered_map<std::string, ResourceSubscriptionEntry>& resource_subscriptions,
  McpSubscriptionOperationId& next_subscription_operation_id,
  bool allow_subscribe_retry,
  ParseKeyFn parse_resource_key
) {
  auto it = resource_subscriptions.find(key);
  if (it == resource_subscriptions.end()) {
    return {};
  }

  auto& entry = it->second;
  if (entry.subscribe_in_flight || entry.unsubscribe_in_flight) {
    return {};
  }

  if (entry.desired_refcount > 0 && !entry.physically_subscribed) {
    if (!allow_subscribe_retry && !entry.last_error.empty()) {
      return {};
    }
    entry.subscribe_in_flight = true;
    entry.last_error.clear();
    entry.active_operation_id = next_subscription_operation_id++;
    const auto [server_id, uri] = parse_resource_key(key);
    return ScheduledResourceOperation{
      .kind = ResourceOperationKind::SUBSCRIBE,
      .operation_id = entry.active_operation_id,
      .server_id = server_id,
      .uri = uri,
    };
  }

  if (entry.desired_refcount == 0 && entry.physically_subscribed) {
    if (!entry.last_error.empty()) {
      return {};
    }
    entry.unsubscribe_in_flight = true;
    entry.active_operation_id = next_subscription_operation_id++;
    const auto [server_id, uri] = parse_resource_key(key);
    return ScheduledResourceOperation{
      .kind = ResourceOperationKind::UNSUBSCRIBE,
      .operation_id = entry.active_operation_id,
      .server_id = server_id,
      .uri = uri,
    };
  }

  return {};
}

template<class ParseKeyFn>
ScheduledResourceOperation RemoveConsumerSubscription(
  const std::string& key,
  std::unordered_map<std::string, ResourceSubscriptionEntry>& resource_subscriptions,
  McpSubscriptionOperationId& next_subscription_operation_id,
  ParseKeyFn parse_resource_key
) {
  auto it = resource_subscriptions.find(key);
  if (it == resource_subscriptions.end()) {
    return {};
  }

  auto& entry = it->second;
  if (entry.desired_refcount > 0) {
    --entry.desired_refcount;
  }
  if (ShouldEraseSubscriptionEntry(entry)) {
    resource_subscriptions.erase(it);
    return {};
  }

  return EnsureSubscriptionIntent(
    key,
    resource_subscriptions,
    next_subscription_operation_id,
    /*allow_subscribe_retry=*/false,
    parse_resource_key
  );
}

inline bool MarkRejectedResourceOperation(
  const ScheduledResourceOperation& operation,
  std::unordered_map<std::string, ResourceSubscriptionEntry>& resource_subscriptions,
  const std::string& key
) {
  auto it = resource_subscriptions.find(key);
  if (it == resource_subscriptions.end() || it->second.active_operation_id != operation.operation_id) {
    return false;
  }

  auto& entry = it->second;
  if (operation.kind == ResourceOperationKind::SUBSCRIBE) {
    entry.subscribe_in_flight = false;
    entry.physically_subscribed = false;
    entry.last_error = "Failed to submit MCP subscribe request";
  } else if (operation.kind == ResourceOperationKind::UNSUBSCRIBE) {
    entry.unsubscribe_in_flight = false;
    entry.physically_subscribed = true;
    entry.last_error = "Failed to submit MCP unsubscribe request";
  }
  entry.active_operation_id = 0;
  return true;
}

inline bool ApplyResourceSubscriptionEvent(
  const McpResourceSubscriptionEvent& event,
  std::unordered_map<std::string, ResourceSubscriptionEntry>& resource_subscriptions,
  std::unordered_map<std::string, McpLatestResourceState>& latest_resource_states,
  const std::string& key
) {
  auto entry_it = resource_subscriptions.find(key);
  if (entry_it == resource_subscriptions.end()) {
    return false;
  }

  auto& entry = entry_it->second;
  if (entry.active_operation_id != event.operation_id || event.operation_id == 0) {
    return false;
  }

  if (event.operation == McpResourceSubscriptionEvent::Operation::SUBSCRIBE) {
    entry.subscribe_in_flight = false;
    if (event.is_error) {
      entry.physically_subscribed = false;
      entry.last_error = event.error_message;
    } else {
      entry.physically_subscribed = true;
      entry.last_error.clear();
    }
  } else {
    entry.unsubscribe_in_flight = false;
    if (event.is_error) {
      entry.physically_subscribed = true;
      entry.last_error = event.error_message;
    } else {
      entry.physically_subscribed = false;
      entry.last_error.clear();
      latest_resource_states.erase(key);
    }
  }

  entry.active_operation_id = 0;
  return true;
}

template<class ParseKeyFn>
void EraseServerResourceSubscriptions(
  const std::string& server_id,
  std::unordered_map<std::string, ResourceSubscriptionEntry>& resource_subscriptions,
  ParseKeyFn parse_resource_key
) {
  for (auto it = resource_subscriptions.begin(); it != resource_subscriptions.end();) {
    const auto [entry_server_id, uri] = parse_resource_key(it->first);
    static_cast<void>(uri);
    if (entry_server_id == server_id) {
      it = resource_subscriptions.erase(it);
    } else {
      ++it;
    }
  }
}

inline void EraseServerLatestStates(
  const std::string& server_id, std::unordered_map<std::string, McpLatestResourceState>& latest_resource_states
) {
  for (auto it = latest_resource_states.begin(); it != latest_resource_states.end();) {
    if (it->second.server_id == server_id) {
      it = latest_resource_states.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace detail
} // namespace mcp
} // namespace giga_edge
