#include "giga_edge/modules/mcp/nodes/mcp_scene_renderer/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
McpSceneRendererParams ParseConfig(const YAML::Node& config) {
  McpSceneRendererParams result;
  result.format = mcp::ParseNamedParams(config["format"], "plain_v1");
  result.scene_version = config["scene_version"].as<std::string>(result.scene_version);
  result.allow_empty = config["allow_empty"].as<bool>(result.allow_empty);
  result.version_includes_scene_text_hash = config["version_includes_scene_text_hash"].as<bool>(
    result.version_includes_scene_text_hash
  );
  result.local_debug_enabled = config["local_debug_enabled"].as<bool>(result.local_debug_enabled);
  result.debug_scene_preview_max_chars = config["debug_scene_preview_max_chars"].as<size_t>(
    result.debug_scene_preview_max_chars
  );

  auto& o = result.options;
  o.include_tools = config["include_tools"].as<bool>(o.include_tools);
  o.include_resources = config["include_resources"].as<bool>(o.include_resources);
  o.include_resource_templates = config["include_resource_templates"].as<bool>(o.include_resource_templates);
  o.include_latest_resource_states = config["include_latest_resource_states"].as<bool>(
    o.include_latest_resource_states
  );
  o.max_rendered_tools = config["max_rendered_tools"].as<size_t>(o.max_rendered_tools);
  o.max_rendered_resources = config["max_rendered_resources"].as<size_t>(o.max_rendered_resources);
  o.max_rendered_resource_templates = config["max_rendered_resource_templates"].as<size_t>(
    o.max_rendered_resource_templates
  );
  o.max_rendered_resource_states = config["max_rendered_resource_states"].as<size_t>(o.max_rendered_resource_states);
  o.max_rendered_chars = config["max_rendered_chars"].as<size_t>(o.max_rendered_chars);
  return result;
}

} // namespace giga_edge
