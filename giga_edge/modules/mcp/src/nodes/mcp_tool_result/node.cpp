#include "giga_edge/modules/mcp/nodes/mcp_tool_result/node.h"

#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/mcp/log.h"

#include <sstream>
#include <utility>

namespace giga_edge {

namespace {

class JsonResultFormat : public mcp::IResultFormat {
public:
  std::string FormatToolResult(const Json::Value& content, bool is_error, const std::string& error) const override {
    if (!content.isNull() && !content.empty()) {
      return WriteJson(content);
    }
    if (is_error) {
      Json::Value value(Json::objectValue);
      value["status"] = "fail";
      value["error"] = error.empty() ? "MCP_TOOL_ERROR" : error;
      return WriteJson(value);
    }
    return "{}";
  }

  std::string FormatReadResult(const std::string& text, bool is_error, const std::string& error) const override {
    if (is_error) {
      Json::Value value(Json::objectValue);
      value["status"] = "fail";
      value["error"] = error.empty() ? "MCP_READ_ERROR" : error;
      return WriteJson(value);
    }
    if (text.empty()) {
      return "{}";
    }
    Json::Value parsed;
    std::string parse_error;
    if (ParseJson(text, parsed, parse_error) && (parsed.isObject() || parsed.isArray())) {
      return WriteJson(parsed);
    }
    Json::Value value(Json::objectValue);
    value["text"] = text;
    return WriteJson(value);
  }
};

const bool kJsonRegistered = mcp::ResultFormatRegistry::Register("json_v1", [](const YAML::Node&) {
  return std::make_shared<JsonResultFormat>();
});

Json::Value ToolContent(const McpActionCompletion& completion) {
  const auto& text = completion.structured_content_json.empty()
    ? completion.content_json
    : completion.structured_content_json;
  Json::Value content;
  std::string error;
  if (!text.empty() && !ParseJson(text, content, error)) {
    content = Json::Value(text);
  }
  return content;
}

} // namespace

McpToolResultNode::McpToolResultNode(const NodeBaseConfig& base_config, const McpToolResultParams& params)
  : NodeBase(base_config)
  , params_(params) {
  (void)kJsonRegistered;
  format_ = mcp::ResultFormatRegistry::Create(params_.result_format.name, params_.result_format.params);
}

ToolResult McpToolResultNode::Format(const McpActionCompletion& completion) const {
  const auto& action = completion.action;
  const bool read = action.kind == McpResolvedActionKind::READ_RESOURCE;
  ToolResult result;
  result.call_id = action.call_id;
  result.name = !action.source_action_name.empty() ? action.source_action_name : read ? action.uri : action.tool_name;
  result.content = read ? format_->FormatReadResult(completion.content_json, completion.is_error, completion.error)
                        : format_->FormatToolResult(ToolContent(completion), completion.is_error, completion.error);
  result.is_ready = true;
  return result;
}

void McpToolResultNode::ProcessOnce() {
  for (const auto& completion : GetMcpCompletions()) {
    pending_.push_back(Format(completion));
  }
  if (pending_.empty()) {
    PushToolResult(ToolResult{});
    return;
  }
  auto result = std::move(pending_.front());
  pending_.pop_front();
  if (params_.local_debug_enabled) {
    std::ostringstream line;
    line << "[mcp][node][mcp_tool_result] ready call_id='" << result.call_id << "' name='" << result.name
         << "' bytes=" << result.content.size();
    mcp::detail::WriteMcpLog(line.str());
  }
  PushToolResult(std::move(result));
}

} // namespace giga_edge
