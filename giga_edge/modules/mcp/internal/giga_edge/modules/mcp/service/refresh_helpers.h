#pragma once

#include "giga_edge/modules/mcp/client.h"
#include "giga_edge/modules/mcp/types.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace giga_edge {
namespace mcp {
namespace detail {

struct RefreshResourceData {
  std::vector<ResourceInfo> resources;
  std::optional<std::vector<ResourceTemplateInfo>> resource_templates;
};

template<class ServerState, class ServerStatus>
std::shared_ptr<IMcpClient> ResolveReadyServerClient(
  const std::string& server_id, ServerStatus ready_status, std::unordered_map<std::string, ServerState>& servers
) {
  auto it = servers.find(server_id);
  if (it == servers.end() || !it->second.client || it->second.status != ready_status) {
    return {};
  }
  return it->second.client;
}

template<class ServerState, class AggregateToolsFn>
bool CommitToolsRefresh(
  const std::string& server_id,
  std::vector<ToolInfo> tools,
  std::unordered_map<std::string, ServerState>& servers,
  std::unordered_map<std::string, std::vector<ToolInfo>>& per_server_tools,
  AggregateToolsFn aggregate_tools,
  std::vector<McpToolDescriptor>& aggregated
) {
  auto it = servers.find(server_id);
  if (it == servers.end()) {
    return false;
  }

  per_server_tools[server_id] = std::move(tools);
  aggregated = aggregate_tools(per_server_tools);
  return true;
}

template<class ServerState, class AggregateResourcesFn, class AggregateTemplatesFn>
bool CommitResourcesRefresh(
  const std::string& server_id,
  RefreshResourceData refresh_data,
  std::unordered_map<std::string, ServerState>& servers,
  std::unordered_map<std::string, std::vector<ResourceInfo>>& per_server_resources,
  std::unordered_map<std::string, std::vector<ResourceTemplateInfo>>& per_server_resource_templates,
  AggregateResourcesFn aggregate_resources,
  AggregateTemplatesFn aggregate_templates,
  std::vector<McpResourceDescriptor>& aggregated_resources,
  std::vector<McpResourceTemplateDescriptor>& aggregated_templates
) {
  auto it = servers.find(server_id);
  if (it == servers.end()) {
    return false;
  }

  per_server_resources[server_id] = std::move(refresh_data.resources);
  aggregated_resources = aggregate_resources(per_server_resources);

  if (refresh_data.resource_templates.has_value()) {
    per_server_resource_templates[server_id] = std::move(*refresh_data.resource_templates);
  }
  aggregated_templates = aggregate_templates(per_server_resource_templates);
  return true;
}

} // namespace detail
} // namespace mcp
} // namespace giga_edge
