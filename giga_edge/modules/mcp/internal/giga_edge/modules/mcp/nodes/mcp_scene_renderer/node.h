#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/mcp/contracts/types.h"
#include "giga_edge/modules/mcp/nodes/mcp_scene_renderer/params.h"

namespace giga_edge {

class McpSceneRendererNode : public NodeBase {
public:
  McpSceneRendererNode(const NodeBaseConfig& base_config, const McpSceneRendererParams& params);

  void ProcessOnce() override;
  void Reset() override {}

  REGISTER_INPUT(
    mcp_scene_renderer,
    mcp_catalog_snapshot,
    McpCatalogSnapshot,
    McpCatalogSnapshot,
    "MCP catalog/resource snapshot independent of prompt format."
  );
  REGISTER_OPTIONAL_INPUT(
    mcp_scene_renderer,
    user_query,
    UserQuery,
    UserQuery,
    "Optional user request tick; enables scene preview logging on real queries."
  );
  REGISTER_OUTPUT(
    mcp_scene_renderer,
    scene_snapshot,
    SceneSnapshot,
    SceneSnapshot,
    "Rendered SceneSnapshot for assistant prompt preparation."
  );

private:
  McpSceneRendererParams params_;
  std::shared_ptr<mcp::ISceneFormat> format_;
  std::string init_error_;
  std::string last_debug_logged_version_;
};

REGISTER_NODE(
  mcp_scene_renderer,
  McpSceneRendererNode,
  McpSceneRendererParams,
  "Renders an MCP catalog snapshot into a SceneSnapshot with a scene format chosen by name."
);

} // namespace giga_edge
