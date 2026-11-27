#include "giga_edge/modules/mcp/nodes/mcp_action_executor/node.h"

#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/mcp/log.h"
#include "giga_edge/modules/mcp/nodes/node_coordinator.h"
#include "giga_edge/modules/mcp/nodes/node_debug.h"

#include <sstream>
#include <utility>

namespace giga_edge {
namespace {

McpActionSubmitReport MakeError(const McpResolvedAction& action, std::string error) {
  McpActionSubmitReport result;
  result.action = action;
  result.status = AssistantStatus::ERROR;
  result.error = std::move(error);
  return result;
}

} // namespace

McpActionExecutorNode::McpActionExecutorNode(const NodeBaseConfig& base_config, const McpActionExecutorParams& params)
  : NodeBase(base_config)
  , params_(params)
  , coordinator_(mcp::nodes::ResolveCoordinator(base_config, params_.environment_name, init_error_)) {
  if (coordinator_) {
    mcp::McpConsumerConfig config;
    config.consumer_name = params_.consumer_name;
    config.visible_server_ids = {params_.visible_server_ids.begin(), params_.visible_server_ids.end()};
    config.completion_queue_capacity = params_.completion_queue_capacity;
    config.debug_enabled = params_.local_debug_enabled;
    consumer_id_ = mcp::nodes::EnsureConsumer(coordinator_, config);
  }
}

McpActionExecutorNode::~McpActionExecutorNode() = default;

void McpActionExecutorNode::DoInitRecursiveOutputs() {
  PushMcpActionResult(McpActionSubmitReport{});
}

void McpActionExecutorNode::ProcessOnce() {
  auto action = GetMcpResolvedAction();
  if (
    action.call_id.empty() && (action.kind != McpResolvedActionKind::NONE || action.status == AssistantStatus::ERROR)
  ) {
    action.call_id = NextCallId();
  }
  auto result = ExecuteAction(action);
  result.action = std::move(action);

  if (params_.local_debug_enabled && ShouldLogMcpActionSubmitReportDebug(result)) {
    std::ostringstream line;
    line << "[mcp][node][mcp_action_executor] kind=" << static_cast<int>(result.action.kind) << " call_id='"
         << result.action.call_id << "' server='" << result.action.server_id << "' tool='" << result.action.tool_name
         << "' uri='" << result.action.uri << "' submitted=" << (result.submitted ? "true" : "false")
         << " status=" << ToString(result.status) << " error='" << result.error << "'";
    mcp::detail::WriteMcpLog(line.str());
  }

  PushMcpActionResult(std::move(result));
}

McpActionSubmitReport McpActionExecutorNode::ExecuteAction(const McpResolvedAction& action) {
  if (action.status == AssistantStatus::ERROR) {
    return MakeError(action, action.error.empty() ? "Resolved MCP action error." : action.error);
  }
  if (action.status == AssistantStatus::SKIPPED || action.kind == McpResolvedActionKind::NONE) {
    McpActionSubmitReport result;
    result.status = AssistantStatus::SKIPPED;
    return result;
  }
  if (!coordinator_ || consumer_id_ == 0) {
    return MakeError(action, init_error_.empty() ? "MCP coordinator is not available." : init_error_);
  }

  McpActionSubmitReport result;
  result.action = action;

  switch (action.kind) {
    case McpResolvedActionKind::CALL_TOOL: {
      Json::Value arguments(Json::objectValue);
      std::string parse_error;
      if (!action.arguments_json.empty() && !ParseJson(action.arguments_json, arguments, parse_error)) {
        return MakeError(action, "Failed to parse resolved MCP tool arguments JSON: " + parse_error);
      }
      auto submit_result = coordinator_->SubmitToolCall(
        consumer_id_,
        mcp::McpToolCallRequest{
          .call_id = result.action.call_id,
          .server_id = action.server_id,
          .tool_name = action.tool_name,
          .arguments = std::move(arguments),
          .timeout_ms = params_.tool_call_timeout_ms,
        }
      );
      result.submitted = submit_result.accepted;
      result.request_token = submit_result.token;
      if (!submit_result.accepted) {
        result.status = AssistantStatus::ERROR;
        result.error = submit_result.error_message.empty()
          ? "Failed to submit MCP tool call."
          : submit_result.error_message;
      }
      return result;
    }

    case McpResolvedActionKind::READ_RESOURCE: {
      auto submit_result = coordinator_->SubmitReadResource(
        consumer_id_,
        mcp::McpReadResourceRequest{
          .call_id = result.action.call_id,
          .server_id = action.server_id,
          .uri = action.uri,
          .timeout_ms = params_.read_resource_timeout_ms,
        }
      );
      result.submitted = submit_result.accepted;
      result.request_token = submit_result.token;
      if (!submit_result.accepted) {
        result.status = AssistantStatus::ERROR;
        result.error = submit_result.error_message.empty()
          ? "Failed to submit MCP resource read."
          : submit_result.error_message;
      }
      return result;
    }

    case McpResolvedActionKind::SUBSCRIBE_RESOURCE:
      result.submitted = coordinator_->SubscribeResource(consumer_id_, action.server_id, action.uri);
      if (!result.submitted) {
        result.status = AssistantStatus::ERROR;
        result.error = "Failed to submit MCP resource subscribe.";
      }
      return result;

    case McpResolvedActionKind::UNSUBSCRIBE_RESOURCE:
      coordinator_->UnsubscribeResource(consumer_id_, action.server_id, action.uri);
      result.submitted = true;
      return result;

    case McpResolvedActionKind::NONE:
      result.status = AssistantStatus::SKIPPED;
      return result;
  }

  return result;
}

std::string McpActionExecutorNode::NextCallId() {
  return params_.call_id_prefix + std::to_string(next_call_id_++);
}

} // namespace giga_edge
