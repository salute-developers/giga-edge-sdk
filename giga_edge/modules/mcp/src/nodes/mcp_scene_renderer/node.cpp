#include "giga_edge/modules/mcp/nodes/mcp_scene_renderer/node.h"

#include "giga_edge/modules/mcp/log.h"
#include "giga_edge/modules/mcp/nodes/node_debug.h"

#include <functional>
#include <sstream>

namespace giga_edge {
namespace {

std::string BuildVersion(
  const std::string& base_version, const std::string& text, const std::string& snapshot_revision, bool include_text_hash
) {
  if (!include_text_hash) {
    return snapshot_revision.empty() ? base_version : base_version + '#' + snapshot_revision;
  }
  std::ostringstream oss;
  oss << base_version << '#' << std::hex << std::hash<std::string>{}(text);
  return oss.str();
}

void AppendLimited(std::string& out, const std::string& text, size_t max_chars) {
  if (max_chars == 0 || out.size() >= max_chars) {
    return;
  }
  out += text.substr(0, max_chars - out.size());
}

class PlainSceneFormat : public mcp::ISceneFormat {
public:
  std::string Render(const McpCatalogSnapshot& snapshot, const mcp::SceneRenderOptions& o) const override {
    std::string out;
    if (o.include_tools && !snapshot.tools.empty()) {
      AppendLimited(out, "Tools:\n", o.max_rendered_chars);
      size_t rendered = 0;
      for (const auto& tool : snapshot.tools) {
        if (rendered++ >= o.max_rendered_tools) {
          break;
        }
        AppendLimited(
          out, "- " + tool.server_id + "/" + tool.tool_name + ": " + tool.description + "\n", o.max_rendered_chars
        );
      }
    }
    if (o.include_resources && !snapshot.resources.empty()) {
      AppendLimited(out, "Resources:\n", o.max_rendered_chars);
      size_t rendered = 0;
      for (const auto& resource : snapshot.resources) {
        if (rendered++ >= o.max_rendered_resources) {
          break;
        }
        AppendLimited(
          out, "- " + resource.server_id + " " + resource.uri + " " + resource.name + "\n", o.max_rendered_chars
        );
      }
    }
    if (o.include_resource_templates && !snapshot.resource_templates.empty()) {
      AppendLimited(out, "Resource templates:\n", o.max_rendered_chars);
      size_t rendered = 0;
      for (const auto& item : snapshot.resource_templates) {
        if (rendered++ >= o.max_rendered_resource_templates) {
          break;
        }
        AppendLimited(
          out, "- " + item.server_id + " " + item.uri_template + " " + item.name + "\n", o.max_rendered_chars
        );
      }
    }
    if (o.include_latest_resource_states && !snapshot.latest_resource_states.empty()) {
      AppendLimited(out, "State:\n", o.max_rendered_chars);
      size_t rendered = 0;
      for (const auto& state : snapshot.latest_resource_states) {
        if (rendered++ >= o.max_rendered_resource_states) {
          break;
        }
        AppendLimited(out, "- " + state.uri + " " + state.data_json + "\n", o.max_rendered_chars);
      }
    }
    return out;
  }
};

const bool kPlainRegistered = mcp::SceneFormatRegistry::Register("plain_v1", [](const YAML::Node&) {
  return std::make_shared<PlainSceneFormat>();
});

} // namespace

McpSceneRendererNode::McpSceneRendererNode(const NodeBaseConfig& base_config, const McpSceneRendererParams& params)
  : NodeBase(base_config)
  , params_(params) {
  (void)kPlainRegistered;
  if (!mcp::SceneFormatRegistry::Has(params_.format.name)) {
    init_error_ = "Unsupported MCP scene format: " + params_.format.name;
    return;
  }
  format_ = mcp::SceneFormatRegistry::Create(params_.format.name, params_.format.params);
}

void McpSceneRendererNode::ProcessOnce() {
  const auto& snapshot = GetMcpCatalogSnapshot();
  const auto user_query = GetUserQuery();
  const bool user_request = user_query.has_value() && HasUserQueryText(user_query->get());
  SceneSnapshot out;
  out.allow_empty = params_.allow_empty;

  if (!init_error_.empty()) {
    out.status = AssistantStatus::ERROR;
    out.error = init_error_;
    PushSceneSnapshot(std::move(out));
    return;
  }
  if (snapshot.status == AssistantStatus::ERROR) {
    out.status = AssistantStatus::ERROR;
    out.error = snapshot.error.empty() ? "MCP catalog snapshot error." : snapshot.error;
    PushSceneSnapshot(std::move(out));
    return;
  }

  out.text = format_->Render(snapshot, params_.options);
  out.version = BuildVersion(
    params_.scene_version, out.text, snapshot.revision, params_.version_includes_scene_text_hash
  );
  if (out.text.empty() && !params_.allow_empty) {
    out.status = AssistantStatus::ERROR;
    out.error = "Rendered MCP scene is empty.";
  }

  if (
    params_.local_debug_enabled
    && ShouldLogMcpSceneRendererDebug(snapshot, out, last_debug_logged_version_, user_request)
  ) {
    std::ostringstream line;
    line << "[mcp][node][mcp_scene_renderer] version='" << out.version << "' chars=" << out.text.size()
         << " status=" << ToString(out.status);
    if (user_request) {
      const auto& query = user_query->get();
      line << " request_id='" << query.request_id << "' query='" << query.text << "'";
    }
    if (!out.text.empty() && (user_request || out.version != last_debug_logged_version_)) {
      line << " scene_preview=\"" << FormatMcpDebugPreview(out.text, params_.debug_scene_preview_max_chars) << "\"";
    }
    if (!out.error.empty()) {
      line << " error='" << out.error << "'";
    }
    mcp::detail::WriteMcpLog(line.str());
    last_debug_logged_version_ = out.version;
  }

  PushSceneSnapshot(std::move(out));
}

} // namespace giga_edge
