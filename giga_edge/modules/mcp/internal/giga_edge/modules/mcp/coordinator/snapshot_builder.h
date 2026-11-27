#pragma once

#include "giga_edge/modules/mcp/coordinator/snapshot_helpers.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace giga_edge {
namespace mcp {
namespace detail {

template<class VisibleFn>
std::vector<McpToolDescriptor> BuildToolsSnapshotData(
  const std::vector<McpToolDescriptor>& all_tools, VisibleFn is_server_visible
) {
  auto filtered = FilterItems(all_tools, [&](const auto& tool) {
    return is_server_visible(tool.server_id);
  });
  SortTools(filtered);
  return filtered;
}

template<class VisibleFn>
std::vector<McpResourceDescriptor> BuildResourcesSnapshotData(
  const std::vector<McpResourceDescriptor>& all_resources, VisibleFn is_server_visible
) {
  auto filtered = FilterItems(all_resources, [&](const auto& resource) {
    return is_server_visible(resource.server_id);
  });
  SortResources(filtered);
  return filtered;
}

template<class VisibleFn>
std::vector<McpResourceTemplateDescriptor> BuildResourceTemplatesSnapshotData(
  const std::vector<McpResourceTemplateDescriptor>& all_resource_templates, VisibleFn is_server_visible
) {
  auto filtered = FilterItems(all_resource_templates, [&](const auto& resource_template) {
    return is_server_visible(resource_template.server_id);
  });
  SortResourceTemplates(filtered);
  return filtered;
}

template<class BuildStateFn, class VisibleFn>
std::vector<McpResourceSubscriptionState> BuildResourceSubscriptionStatesSnapshotData(
  const std::unordered_set<std::string>& desired_resource_keys, BuildStateFn build_state, VisibleFn is_server_visible
) {
  std::vector<McpResourceSubscriptionState> filtered;
  filtered.reserve(desired_resource_keys.size());
  for (const auto& key : desired_resource_keys) {
    auto state = build_state(key);
    if (!is_server_visible(state.server_id)) {
      continue;
    }
    filtered.push_back(std::move(state));
  }
  SortResourceSubscriptionStates(filtered);
  return filtered;
}

template<class VisibleFn>
std::vector<McpLatestResourceState> BuildLatestResourceStatesSnapshotData(
  const std::unordered_map<std::string, McpLatestResourceState>& latest_resource_states,
  const std::unordered_set<std::string>& desired_resource_keys,
  bool include_shared_latest_resource_states,
  VisibleFn is_server_visible
) {
  std::vector<McpLatestResourceState> filtered;
  filtered.reserve(latest_resource_states.size());
  for (const auto& [key, state] : latest_resource_states) {
    if (
      (!include_shared_latest_resource_states && desired_resource_keys.count(key) == 0)
      || !is_server_visible(state.server_id)
    ) {
      continue;
    }
    filtered.push_back(state);
  }
  SortLatestResourceStates(filtered);
  return filtered;
}

} // namespace detail
} // namespace mcp
} // namespace giga_edge
