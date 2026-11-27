#pragma once

#include "giga_edge/modules/mcp/types.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace giga_edge {
namespace mcp {
namespace detail {

template<class ResourceSubscriptionEntry, class ParseKeyFn>
McpResourceSubscriptionState BuildSubscriptionState(
  const std::string& key,
  const std::unordered_map<std::string, ResourceSubscriptionEntry>& resource_subscriptions,
  ParseKeyFn parse_resource_key
) {
  const auto [server_id, uri] = parse_resource_key(key);
  McpResourceSubscriptionState state{
    .server_id = server_id,
    .uri = uri,
  };

  auto it = resource_subscriptions.find(key);
  if (it == resource_subscriptions.end()) {
    return state;
  }

  const auto& entry = it->second;
  if (entry.subscribe_in_flight) {
    state.status = McpResourceSubscriptionStatus::SUBSCRIBE_PENDING;
  } else if (entry.unsubscribe_in_flight) {
    state.status = McpResourceSubscriptionStatus::UNSUBSCRIBE_PENDING;
  } else if (entry.physically_subscribed) {
    state.status = McpResourceSubscriptionStatus::SUBSCRIBED;
  } else if (!entry.last_error.empty()) {
    state.status = McpResourceSubscriptionStatus::SUBSCRIBE_FAILED;
    state.error_message = entry.last_error;
  }

  return state;
}

template<class ResourceSubscriptionEntry, class VisibleFn>
std::optional<McpLatestResourceState> ResolveLatestResourceStateForKey(
  const std::unordered_set<std::string>& desired_resource_keys,
  const std::unordered_map<std::string, ResourceSubscriptionEntry>& resource_subscriptions,
  const std::unordered_map<std::string, McpLatestResourceState>& latest_resource_states,
  const std::string& key,
  const std::string& server_id,
  bool include_shared_latest_resource_states,
  VisibleFn is_server_visible
) {
  auto subscription_it = resource_subscriptions.find(key);
  if (
    (!include_shared_latest_resource_states && desired_resource_keys.count(key) == 0) || !is_server_visible(server_id)
    || subscription_it == resource_subscriptions.end() || !subscription_it->second.physically_subscribed
  ) {
    return std::nullopt;
  }

  auto latest_it = latest_resource_states.find(key);
  if (latest_it == latest_resource_states.end()) {
    return std::nullopt;
  }

  return latest_it->second;
}

template<class ConsumerState, class ParseKeyFn>
bool RemoveServerDataFromConsumer(
  ConsumerState& consumer, const std::string& server_id, ParseKeyFn parse_resource_key
) {
  bool changed = false;

  for (auto key_it = consumer.desired_resource_keys.begin(); key_it != consumer.desired_resource_keys.end();) {
    const auto [entry_server_id, uri] = parse_resource_key(*key_it);
    static_cast<void>(uri);
    if (entry_server_id == server_id) {
      key_it = consumer.desired_resource_keys.erase(key_it);
      changed = true;
    } else {
      ++key_it;
    }
  }

  for (auto res_it = consumer.latest_resource_states.begin(); res_it != consumer.latest_resource_states.end();) {
    if (res_it->second.server_id == server_id) {
      res_it = consumer.latest_resource_states.erase(res_it);
      changed = true;
    } else {
      ++res_it;
    }
  }

  return changed;
}

} // namespace detail
} // namespace mcp
} // namespace giga_edge
