#pragma once

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/mcp/contracts/types.h"

#include <json/json.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace giga_edge {
namespace mcp {

struct SceneRenderOptions {
  bool include_tools = true;
  bool include_resources = true;
  bool include_resource_templates = false;
  bool include_latest_resource_states = true;
  size_t max_rendered_tools = 64;
  size_t max_rendered_resources = 64;
  size_t max_rendered_resource_templates = 32;
  size_t max_rendered_resource_states = 32;
  size_t max_rendered_chars = 12000;
};

class ISceneFormat {
public:
  virtual ~ISceneFormat() = default;
  virtual std::string Render(const McpCatalogSnapshot& snapshot, const SceneRenderOptions& options) const = 0;
};

class IActionProfile {
public:
  virtual ~IActionProfile() = default;
  virtual McpResolvedAction Resolve(
    const ToolCall& call, const McpCatalogSnapshot& snapshot, const std::vector<std::string>& supported_server_ids
  ) const = 0;
};

class IResultFormat {
public:
  virtual ~IResultFormat() = default;
  virtual std::string FormatToolResult(const Json::Value& content, bool is_error, const std::string& error) const = 0;
  virtual std::string FormatReadResult(const std::string& text, bool is_error, const std::string& error) const = 0;
};

template<class Interface>
class Registry {
public:
  using Factory = std::function<std::shared_ptr<Interface>(const YAML::Node& params)>;

  static bool Register(const std::string& name, Factory factory) {
    if (!Map().emplace(name, std::move(factory)).second) {
      throw std::runtime_error("MCP profile is already registered: " + name);
    }
    return true;
  }

  static std::shared_ptr<Interface> Create(const std::string& name, const YAML::Node& params) {
    const auto it = Map().find(name);
    if (it == Map().end()) {
      throw std::runtime_error("Unknown MCP profile: " + name);
    }
    return it->second(params);
  }

  static bool Has(const std::string& name) {
    return Map().count(name) > 0;
  }

private:
  static std::map<std::string, Factory>& Map() {
    static std::map<std::string, Factory> map;
    return map;
  }
};

using SceneFormatRegistry = Registry<ISceneFormat>;
using ActionProfileRegistry = Registry<IActionProfile>;
using ResultFormatRegistry = Registry<IResultFormat>;

struct NamedParams {
  std::string name;
  YAML::Node params;
};

inline NamedParams ParseNamedParams(const YAML::Node& node, const std::string& default_name) {
  NamedParams result;
  result.name = default_name;
  if (!node) {
    return result;
  }
  if (node.IsScalar()) {
    result.name = node.as<std::string>();
    return result;
  }
  result.name = node["name"].as<std::string>(default_name);
  result.params = node;
  return result;
}

inline McpResolvedAction ActionError(std::string message, std::string source_action_name = "") {
  McpResolvedAction result;
  result.source_action_name = std::move(source_action_name);
  result.status = AssistantStatus::ERROR;
  result.error = std::move(message);
  return result;
}

inline bool CatalogHasTool(
  const McpCatalogSnapshot& snapshot, const std::string& server_id, const std::string& tool_name
) {
  return std::any_of(snapshot.tools.begin(), snapshot.tools.end(), [&](const McpCatalogTool& tool) {
    return tool.server_id == server_id && tool.tool_name == tool_name;
  });
}

// Servers with the tool tool_name in the catalog; an empty supported_server_ids means all servers of the catalog.
inline std::vector<std::string> ServersExposingTool(
  const McpCatalogSnapshot& snapshot, const std::vector<std::string>& supported_server_ids, const std::string& tool_name
) {
  std::vector<std::string> matching;
  if (supported_server_ids.empty()) {
    for (const auto& tool : snapshot.tools) {
      if (
        tool.tool_name == tool_name && std::find(matching.begin(), matching.end(), tool.server_id) == matching.end()
      ) {
        matching.push_back(tool.server_id);
      }
    }
  } else {
    for (const auto& server_id : supported_server_ids) {
      if (CatalogHasTool(snapshot, server_id, tool_name)) {
        matching.push_back(server_id);
      }
    }
  }
  return matching;
}

inline std::optional<std::string> SelectServerForTool(
  const McpCatalogSnapshot& snapshot,
  const std::vector<std::string>& supported_server_ids,
  const std::string& tool_name,
  std::string& error
) {
  const auto matching = ServersExposingTool(snapshot, supported_server_ids, tool_name);
  if (matching.empty()) {
    error = "No configured MCP server exposes tool: " + tool_name;
    return std::nullopt;
  }
  if (matching.size() != 1) {
    error = "Tool is exposed by several servers, add supported_server_ids: " + tool_name;
    return std::nullopt;
  }
  return matching.front();
}

} // namespace mcp
} // namespace giga_edge
