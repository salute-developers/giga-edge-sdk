#pragma once

#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace giga_edge {

struct McpCatalogTool {
  std::string server_id;
  std::string tool_name;
  std::string description;
  std::string input_schema_json;
  // The name for people, when the server gives one: a tool can be named by an identifier.
  std::string title;
};

struct McpCatalogResource {
  std::string server_id;
  std::string uri;
  std::string name;
  std::string mime_type;
  std::string description;
  bool subscribable = false;
};

struct McpCatalogResourceTemplate {
  std::string server_id;
  std::string uri_template;
  std::string name;
  std::string mime_type;
  std::string description;
};

struct McpCatalogLatestResourceState {
  std::string server_id;
  std::string uri;
  std::string data_json;
};

struct McpCatalogSnapshot {
  std::vector<McpCatalogTool> tools;
  std::vector<McpCatalogResource> resources;
  std::vector<McpCatalogResourceTemplate> resource_templates;
  std::vector<McpCatalogLatestResourceState> latest_resource_states;
  std::string revision;
  AssistantStatus status = AssistantStatus::OK;
  std::string error;
};

enum class McpResolvedActionKind {
  NONE,
  CALL_TOOL,
  READ_RESOURCE,
  SUBSCRIBE_RESOURCE,
  UNSUBSCRIBE_RESOURCE,
};

struct McpResolvedAction {
  McpResolvedActionKind kind = McpResolvedActionKind::NONE;
  std::string source_action_name;
  std::string assistant_text;
  std::string server_id;
  std::string tool_name;
  std::string uri;
  std::string arguments_json;
  std::string call_id;
  std::string context_json;
  AssistantStatus status = AssistantStatus::OK;
  std::string error;
};

struct McpActionSubmitReport {
  McpResolvedAction action;
  std::uint64_t request_token = 0;
  bool submitted = false;
  AssistantStatus status = AssistantStatus::OK;
  std::string error;
};

struct McpActionCompletion {
  McpResolvedAction action;
  std::string content_json;
  std::string structured_content_json;
  bool is_error = false;
  std::string error;
};

using McpActionCompletions = std::vector<McpActionCompletion>;

} // namespace giga_edge
