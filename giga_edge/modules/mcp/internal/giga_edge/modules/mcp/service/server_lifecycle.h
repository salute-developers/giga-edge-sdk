#pragma once

#include "giga_edge/modules/mcp/client.h"
#include "giga_edge/modules/mcp/types.h"

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace giga_edge {
namespace mcp {
namespace detail {

struct InitialServerData {
  std::vector<ToolInfo> tools;
  std::vector<ResourceInfo> resources;
  std::vector<ResourceTemplateInfo> resource_templates;
};

struct AggregatedServiceSnapshots {
  std::vector<McpToolDescriptor> tools;
  std::vector<McpResourceDescriptor> resources;
  std::vector<McpResourceTemplateDescriptor> resource_templates;
};

struct RemovedServerData {
  bool found = false;
  std::shared_ptr<IMcpClient> client;
  std::set<std::string> subscribed_uris;
  std::vector<std::pair<McpRequestToken, McpToolCallResultEvent>> failed_tool_results;
  std::vector<std::pair<McpRequestToken, McpReadResourceResultEvent>> failed_read_results;
  std::vector<std::pair<McpRequestToken, McpPingResultEvent>> failed_ping_results;
  AggregatedServiceSnapshots snapshots;
};

inline bool IsMethodNotFound(const Error& error) {
  return error.json_rpc_code.value_or(static_cast<int32_t>(error.code))
    == static_cast<int32_t>(ErrorCode::METHOD_NOT_FOUND);
}

inline Result<InitialServerData> LoadInitialServerData(IMcpClient& client) {
  InitialServerData data;
  // tools/list is called even without the tools capability: the built-in servers of the host send capabilities: null.
  // A server that did not declare tools and does not know the method is left without tools.
  auto tools_result = client.listTools();
  if (tools_result.ok()) {
    data.tools = std::move(tools_result.value);
  } else if (client.serverCapabilities().supports_tools || !IsMethodNotFound(tools_result.error)) {
    return {.error = tools_result.error};
  }

  if (!client.serverCapabilities().supports_resources) {
    return {.value = std::move(data)};
  }

  auto resources_result = client.listResources();
  if (resources_result.ok()) {
    data.resources = std::move(resources_result.value);
  }

  auto templates_result = client.listResourceTemplates();
  if (templates_result.ok()) {
    data.resource_templates = std::move(templates_result.value);
  }

  return {.value = std::move(data)};
}

template<
  class ServerState,
  class ServerStatus,
  class AggregateToolsFn,
  class AggregateResourcesFn,
  class AggregateTemplatesFn>
bool CommitConnectedServer(
  const std::string& server_id,
  std::shared_ptr<IMcpClient> client,
  InitialServerData initial_data,
  ServerStatus removing_status,
  ServerStatus ready_status,
  std::unordered_map<std::string, ServerState>& servers,
  std::unordered_map<std::string, std::vector<ToolInfo>>& per_server_tools,
  std::unordered_map<std::string, std::vector<ResourceInfo>>& per_server_resources,
  std::unordered_map<std::string, std::vector<ResourceTemplateInfo>>& per_server_resource_templates,
  AggregateToolsFn aggregate_tools,
  AggregateResourcesFn aggregate_resources,
  AggregateTemplatesFn aggregate_templates,
  AggregatedServiceSnapshots& snapshots
) {
  auto it = servers.find(server_id);
  if (it == servers.end() || it->second.status == removing_status) {
    return false;
  }

  it->second.client = std::move(client);
  it->second.status = ready_status;
  per_server_tools[server_id] = std::move(initial_data.tools);
  per_server_resources[server_id] = std::move(initial_data.resources);
  per_server_resource_templates[server_id] = std::move(initial_data.resource_templates);

  snapshots.tools = aggregate_tools(per_server_tools);
  snapshots.resources = aggregate_resources(per_server_resources);
  snapshots.resource_templates = aggregate_templates(per_server_resource_templates);
  return true;
}

template<
  class ServerState,
  class PendingRequest,
  class AggregateToolsFn,
  class AggregateResourcesFn,
  class AggregateTemplatesFn>
RemovedServerData RemoveServerFromState(
  const std::string& server_id,
  std::unordered_map<std::string, ServerState>& servers,
  std::unordered_map<std::string, std::vector<ToolInfo>>& per_server_tools,
  std::unordered_map<std::string, std::vector<ResourceInfo>>& per_server_resources,
  std::unordered_map<std::string, std::vector<ResourceTemplateInfo>>& per_server_resource_templates,
  std::unordered_map<McpRequestToken, PendingRequest>& pending_requests,
  AggregateToolsFn aggregate_tools,
  AggregateResourcesFn aggregate_resources,
  AggregateTemplatesFn aggregate_templates
) {
  RemovedServerData data;

  auto it = servers.find(server_id);
  if (it == servers.end()) {
    return data;
  }

  data.found = true;
  data.client = std::move(it->second.client);
  data.subscribed_uris = it->second.subscribed_resource_uris;
  servers.erase(it);
  per_server_tools.erase(server_id);
  per_server_resources.erase(server_id);
  per_server_resource_templates.erase(server_id);

  for (auto pit = pending_requests.begin(); pit != pending_requests.end();) {
    if (pit->second.server_id == server_id) {
      // A request not yet written to the transport will never go there: request_sent below is final.
      pit->second.cancellation->Cancel();
      switch (pit->second.kind) {
        case PendingRequest::Kind::TOOL_CALL:
          data.failed_tool_results.push_back(
            {pit->first,
             McpToolCallResultEvent{
               .call_id = pit->second.call_id,
               .server_id = server_id,
               .tool_name = pit->second.tool_name,
               .is_error = true,
               .error_message = "Server disconnected",
               .error_code = ErrorCode::TRANSPORT_ERROR,
               .request_sent = pit->second.cancellation->WasSent(),
             }}
          );
          break;
        case PendingRequest::Kind::READ_RESOURCE:
          data.failed_read_results.push_back(
            {pit->first,
             McpReadResourceResultEvent{
               .call_id = pit->second.call_id,
               .server_id = server_id,
               .uri = pit->second.uri,
               .is_error = true,
               .error_message = "Server disconnected",
             }}
          );
          break;
        case PendingRequest::Kind::PING:
          data.failed_ping_results.push_back(
            {pit->first,
             McpPingResultEvent{
               .call_id = pit->second.call_id,
               .server_id = server_id,
               .is_error = true,
               .error_message = "Server disconnected",
             }}
          );
          break;
      }
      pit = pending_requests.erase(pit);
    } else {
      ++pit;
    }
  }

  data.snapshots.tools = aggregate_tools(per_server_tools);
  data.snapshots.resources = aggregate_resources(per_server_resources);
  data.snapshots.resource_templates = aggregate_templates(per_server_resource_templates);
  return data;
}

inline void ShutdownServerClient(
  const std::shared_ptr<IMcpClient>& client, const std::set<std::string>& subscribed_uris
) {
  if (!client) {
    return;
  }

  client->setOnToolsListChanged({});
  client->setOnResourcesListChanged({});
  for (const auto& uri : subscribed_uris) {
    client->unsubscribeResource(uri);
  }
  client->shutdown();
}

} // namespace detail
} // namespace mcp
} // namespace giga_edge
