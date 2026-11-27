#include "giga_edge/modules/mcp/nodes/mcp_action_resolver/node.h"

#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/mcp/log.h"
#include "giga_edge/modules/mcp/nodes/node_debug.h"

#include <sstream>

namespace giga_edge {
namespace {

std::string StringField(const Json::Value& payload, const std::string& field) {
  return payload.isObject() && payload[field].isString() ? payload[field].asString() : "";
}

class GenericProfile : public mcp::IActionProfile {
public:
  explicit GenericProfile(McpGenericProfileParams params)
    : p_(std::move(params)) {}

  McpResolvedAction Resolve(
    const ToolCall& call, const McpCatalogSnapshot& snapshot, const std::vector<std::string>& supported_server_ids
  ) const override {
    Json::Value payload(Json::objectValue);
    std::string parse_error;
    if (
      (!call.arguments_json.empty() && !ParseJson(call.arguments_json, payload, parse_error)) || !payload.isObject()
    ) {
      return mcp::ActionError("Failed to parse tool call arguments JSON: " + parse_error, call.name);
    }

    McpResolvedAction result;
    result.source_action_name = call.name;
    result.status = AssistantStatus::OK;

    // A server tool from the catalog wins over the envelope action of the same name (call_tool, read_resource, ...).
    const bool catalog_tool = p_.allow_direct_tool_actions
      && !mcp::ServersExposingTool(snapshot, supported_server_ids, call.name).empty();
    if (!catalog_tool && call.name == p_.call_tool_action_name) {
      result.kind = McpResolvedActionKind::CALL_TOOL;
      result.server_id = StringField(payload, p_.server_id_field);
      result.tool_name = StringField(payload, p_.tool_name_field);
      if (payload[p_.arguments_json_field].isString()) {
        Json::Value arguments(Json::objectValue);
        const auto text = payload[p_.arguments_json_field].asString();
        if (!text.empty() && !ParseJson(text, arguments, parse_error)) {
          return mcp::ActionError("Failed to parse MCP tool arguments JSON: " + parse_error, call.name);
        }
        result.arguments_json = WriteJson(arguments);
      } else if (payload[p_.arguments_field].isObject()) {
        result.arguments_json = WriteJson(payload[p_.arguments_field]);
      } else {
        result.arguments_json = "{}";
      }
      return result;
    }

    if (
      !catalog_tool
      && (call.name == p_.read_resource_action_name || call.name == p_.subscribe_resource_action_name || call.name == p_.unsubscribe_resource_action_name)
    ) {
      result.kind = call.name == p_.read_resource_action_name ? McpResolvedActionKind::READ_RESOURCE
        : call.name == p_.subscribe_resource_action_name
        ? McpResolvedActionKind::SUBSCRIBE_RESOURCE
        : McpResolvedActionKind::UNSUBSCRIBE_RESOURCE;
      result.server_id = StringField(payload, p_.server_id_field);
      result.uri = StringField(payload, p_.uri_field);
      return result;
    }

    if (!p_.allow_direct_tool_actions) {
      return mcp::ActionError("Unsupported tool call for MCP resolver: " + call.name, call.name);
    }
    result.kind = McpResolvedActionKind::CALL_TOOL;
    result.tool_name = call.name;
    result.server_id = StringField(payload, p_.server_id_field);
    if (result.server_id.empty()) {
      std::string error;
      const auto server = mcp::SelectServerForTool(snapshot, supported_server_ids, call.name, error);
      if (!server) {
        return mcp::ActionError(error, call.name);
      }
      result.server_id = *server;
    }
    result.arguments_json = WriteJson(payload);
    return result;
  }

private:
  McpGenericProfileParams p_;
};

const bool kGenericRegistered = mcp::ActionProfileRegistry::Register("generic_mcp_v1", [](const YAML::Node& params) {
  return std::make_shared<GenericProfile>(ParseMcpGenericProfileParams(params));
});

} // namespace

McpActionResolverNode::McpActionResolverNode(const NodeBaseConfig& base_config, const McpActionResolverParams& params)
  : NodeBase(base_config)
  , params_(params) {
  (void)kGenericRegistered;
  profile_ = mcp::ActionProfileRegistry::Create(params_.profile.name, params_.profile.params);
}

void McpActionResolverNode::ProcessOnce() {
  auto result = ResolveAction(GetAssistantResult(), GetMcpCatalogSnapshot());
  if (params_.local_debug_enabled && ShouldLogMcpResolvedActionDebug(result)) {
    std::ostringstream line;
    line << "[mcp][node][mcp_action_resolver] source='" << result.source_action_name << "' kind="
         << static_cast<int>(result.kind) << " server='" << result.server_id << "' tool='" << result.tool_name
         << "' uri='" << result.uri << "' status=" << ToString(result.status) << " error='" << result.error << "'";
    mcp::detail::WriteMcpLog(line.str());
  }
  PushMcpResolvedAction(std::move(result));
}

McpResolvedAction McpActionResolverNode::ResolveAction(
  const AssistantResult& assistant_result, const McpCatalogSnapshot& snapshot
) const {
  if (
    !assistant_result.is_final || assistant_result.status != AssistantStatus::OK
    || assistant_result.finish_reason == StreamFinishReason::CANCELLED
    || assistant_result.finish_reason == StreamFinishReason::MAX_TOKENS || assistant_result.tool_calls.empty()
  ) {
    McpResolvedAction result;
    result.status = AssistantStatus::SKIPPED;
    result.assistant_text = assistant_result.text;
    return result;
  }
  const auto& call = assistant_result.tool_calls.front();
  auto action = params_.require_single_action && assistant_result.tool_calls.size() != 1
    ? mcp::ActionError("MCP resolver expects exactly one tool call.", call.name)
    : snapshot.status == AssistantStatus::ERROR
    ? mcp::ActionError(snapshot.error.empty() ? "MCP catalog snapshot error." : snapshot.error, call.name)
    : Validate(profile_->Resolve(call, snapshot, params_.supported_server_ids), snapshot);
  action.call_id = call.id;
  return action;
}

McpResolvedAction McpActionResolverNode::Validate(McpResolvedAction action, const McpCatalogSnapshot& snapshot) const {
  if (action.status != AssistantStatus::OK) {
    return action;
  }
  const auto& name = action.source_action_name;
  if (action.server_id.empty()) {
    return mcp::ActionError("MCP action is missing server_id.", name);
  }
  if (action.kind == McpResolvedActionKind::CALL_TOOL) {
    if (action.tool_name.empty()) {
      return mcp::ActionError("MCP tool action is missing tool_name.", name);
    }
    if (!mcp::CatalogHasTool(snapshot, action.server_id, action.tool_name)) {
      return mcp::ActionError(
        "MCP tool is not present in the current catalog: " + action.server_id + "/" + action.tool_name, name
      );
    }
    if (action.arguments_json.empty()) {
      action.arguments_json = "{}";
    }
    return action;
  }
  if (action.uri.empty()) {
    return mcp::ActionError("MCP resource action is missing uri.", name);
  }
  const McpCatalogResource* resource = nullptr;
  for (const auto& candidate : snapshot.resources) {
    if (candidate.server_id == action.server_id && candidate.uri == action.uri) {
      resource = &candidate;
      break;
    }
  }
  if (resource == nullptr) {
    return mcp::ActionError(
      "MCP resource is not present in the current catalog: " + action.server_id + "/" + action.uri, name
    );
  }
  const bool subscription = action.kind == McpResolvedActionKind::SUBSCRIBE_RESOURCE
    || action.kind == McpResolvedActionKind::UNSUBSCRIBE_RESOURCE;
  if (subscription && !resource->subscribable) {
    return mcp::ActionError("MCP resource is not subscribable: " + action.server_id + "/" + action.uri, name);
  }
  return action;
}

} // namespace giga_edge
