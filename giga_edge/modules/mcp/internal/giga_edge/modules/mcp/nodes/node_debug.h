#pragma once

#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/mcp/contracts/types.h"

#include <algorithm>
#include <string>

namespace giga_edge {

inline bool McpCatalogSnapshotHasContent(const McpCatalogSnapshot& snapshot) {
  return !snapshot.tools.empty() || !snapshot.resources.empty() || !snapshot.resource_templates.empty()
    || !snapshot.latest_resource_states.empty();
}

inline bool HasUserQueryText(const UserQuery& user_query) {
  return !user_query.text.empty();
}

inline std::string FormatMcpDebugPreview(const std::string& text, size_t max_chars) {
  if (max_chars == 0 || text.empty()) {
    return {};
  }
  std::string preview;
  preview.reserve(std::min(text.size(), max_chars) + 16);
  bool truncated = false;
  for (const char c : text) {
    if (preview.size() >= max_chars) {
      truncated = true;
      break;
    }
    if (c == '\n') {
      preview += "\\n";
      continue;
    }
    if (c == '\r') {
      preview += "\\r";
      continue;
    }
    if (c == '\t') {
      preview += "\\t";
      continue;
    }
    if (static_cast<unsigned char>(c) < 0x20) {
      continue;
    }
    preview.push_back(c);
  }
  if (!truncated && preview.size() < text.size()) {
    truncated = true;
  }
  if (truncated) {
    preview += "...";
  }
  return preview;
}

// Log when catalog revision changed, user request arrived, or hard error.
inline bool ShouldLogMcpCatalogSnapshotDebug(
  const UserQuery& user_query, const McpCatalogSnapshot& snapshot, const std::string& last_logged_revision
) {
  if (snapshot.status == AssistantStatus::ERROR) {
    return true;
  }
  if (HasUserQueryText(user_query)) {
    return true;
  }
  return snapshot.revision != last_logged_revision;
}

// Log when scene version changed, user request arrived, or non-benign error.
inline bool ShouldLogMcpSceneRendererDebug(
  const McpCatalogSnapshot& snapshot,
  const SceneSnapshot& scene,
  const std::string& last_logged_version,
  bool user_request
) {
  if (user_request) {
    return true;
  }
  if (scene.status == AssistantStatus::ERROR) {
    if (snapshot.status == AssistantStatus::ERROR) {
      return true;
    }
    return McpCatalogSnapshotHasContent(snapshot);
  }
  return scene.version != last_logged_version;
}

inline bool ShouldLogMcpResolvedActionDebug(const McpResolvedAction& action) {
  return action.status != AssistantStatus::SKIPPED;
}

inline bool ShouldLogMcpActionSubmitReportDebug(const McpActionSubmitReport& result) {
  return result.status != AssistantStatus::SKIPPED;
}

} // namespace giga_edge
