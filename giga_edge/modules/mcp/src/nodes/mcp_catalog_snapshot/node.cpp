#include "giga_edge/modules/mcp/nodes/mcp_catalog_snapshot/node.h"

#include "giga_edge/modules/mcp/log.h"
#include "giga_edge/modules/mcp/nodes/node_coordinator.h"
#include "giga_edge/modules/mcp/nodes/node_debug.h"

#include <json/json.h>

#include <sstream>
#include <utility>

namespace giga_edge {
namespace {

std::string JsonToString(const Json::Value& value) {
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString(builder, value);
}

std::string BuildRevision(
  const mcp::VersionedSnapshot<std::vector<mcp::McpToolDescriptor>>& tools,
  const mcp::VersionedSnapshot<std::vector<mcp::McpResourceDescriptor>>& resources,
  const mcp::VersionedSnapshot<std::vector<mcp::McpResourceTemplateDescriptor>>& resource_templates,
  const mcp::VersionedSnapshot<std::vector<mcp::McpLatestResourceState>>& latest_resource_states
) {
  std::ostringstream oss;
  oss << "tools:" << tools.version << "|resources:" << resources.version << "|resource_templates:"
      << resource_templates.version << "|latest_resource_states:" << latest_resource_states.version;
  return oss.str();
}

} // namespace

McpCatalogSnapshotNode::McpCatalogSnapshotNode(
  const NodeBaseConfig& base_config, const McpCatalogSnapshotParams& params
)
  : NodeBase(base_config)
  , params_(params)
  , init_error_({})
  , coordinator_(mcp::nodes::ResolveCoordinator(base_config, params_.environment_name, init_error_)) {
  if (coordinator_) {
    mcp::McpConsumerConfig config;
    config.consumer_name = params_.consumer_name;
    config.visible_server_ids = {params_.visible_server_ids.begin(), params_.visible_server_ids.end()};
    config.include_shared_latest_resource_states = params_.include_shared_latest_resource_states;
    config.debug_enabled = params_.local_debug_enabled;
    consumer_id_ = coordinator_->RegisterConsumer(std::move(config));
  }
}

McpCatalogSnapshotNode::~McpCatalogSnapshotNode() {
  if (coordinator_ && consumer_id_ != 0) {
    coordinator_->UnregisterConsumer(consumer_id_);
  }
}

void McpCatalogSnapshotNode::ProcessOnce() {
  const auto& user_query = GetUserQuery();

  McpCatalogSnapshot out;
  if (!coordinator_ || consumer_id_ == 0) {
    out.status = AssistantStatus::ERROR;
    out.error = init_error_.empty() ? "MCP coordinator is not available." : init_error_;
    PushMcpCatalogSnapshot(std::move(out));
    return;
  }

  const auto tools = coordinator_->GetToolsSnapshot(consumer_id_);
  const auto resources = coordinator_->GetResourcesSnapshot(consumer_id_);
  const auto resource_templates = coordinator_->GetResourceTemplatesSnapshot(consumer_id_);
  const auto latest_resource_states = coordinator_->GetLatestResourceStatesSnapshot(consumer_id_);

  out.revision = BuildRevision(tools, resources, resource_templates, latest_resource_states);

  if (params_.include_tools && tools.data) {
    out.tools.reserve(tools.data->size());
    for (const auto& tool : *tools.data) {
      out.tools.push_back(
        McpCatalogTool{
          .server_id = tool.server_id,
          .tool_name = tool.tool_name,
          .description = tool.description,
          .input_schema_json = JsonToString(tool.input_schema),
          .title = tool.title,
        }
      );
    }
  }

  if (params_.include_resources && resources.data) {
    out.resources.reserve(resources.data->size());
    for (const auto& resource : *resources.data) {
      out.resources.push_back(
        McpCatalogResource{
          .server_id = resource.server_id,
          .uri = resource.uri,
          .name = resource.name,
          .mime_type = resource.mime_type,
          .description = resource.description,
          .subscribable = resource.subscribable,
        }
      );
    }
  }

  if (params_.include_resource_templates && resource_templates.data) {
    out.resource_templates.reserve(resource_templates.data->size());
    for (const auto& resource_template : *resource_templates.data) {
      out.resource_templates.push_back(
        McpCatalogResourceTemplate{
          .server_id = resource_template.server_id,
          .uri_template = resource_template.uri_template,
          .name = resource_template.name,
          .mime_type = resource_template.mime_type,
          .description = resource_template.description,
        }
      );
    }
  }

  if (params_.include_latest_resource_states && latest_resource_states.data) {
    out.latest_resource_states.reserve(latest_resource_states.data->size());
    for (const auto& state : *latest_resource_states.data) {
      out.latest_resource_states.push_back(
        McpCatalogLatestResourceState{
          .server_id = state.server_id,
          .uri = state.uri,
          .data_json = JsonToString(state.data),
        }
      );
    }
  }

  if (params_.local_debug_enabled && ShouldLogMcpCatalogSnapshotDebug(user_query, out, last_debug_logged_revision_)) {
    std::ostringstream line;
    line << "[mcp][node][mcp_catalog_snapshot]";
    if (HasUserQueryText(user_query)) {
      line << " request_id='" << user_query.request_id << "' query='" << user_query.text << "'";
    }
    line << " revision='" << out.revision << "' tools=" << out.tools.size() << " resources=" << out.resources.size()
         << " resource_templates=" << out.resource_templates.size()
         << " latest_resource_states=" << out.latest_resource_states.size();
    if (out.status == AssistantStatus::ERROR) {
      line << " status=" << ToString(out.status) << " error='" << out.error << "'";
    }
    mcp::detail::WriteMcpLog(line.str());
    last_debug_logged_revision_ = out.revision;
  }

  PushMcpCatalogSnapshot(std::move(out));
}

} // namespace giga_edge
