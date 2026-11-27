#pragma once

#include "giga_edge/modules/mcp/profiles.h"

#include <cstddef>
#include <string>

namespace giga_edge {

struct McpSceneRendererParams {
  mcp::NamedParams format;
  std::string scene_version = "mcp-scene-v1";
  bool allow_empty = false;
  bool version_includes_scene_text_hash = true;
  bool local_debug_enabled = false;
  size_t debug_scene_preview_max_chars = 512;
  mcp::SceneRenderOptions options;
};

} // namespace giga_edge
