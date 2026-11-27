#include "giga_edge/modules/mcp/resource_state.h"

#include <optional>
#include <sstream>
#include <utility>

namespace giga_edge {
namespace mcp {
namespace {

std::optional<Json::Value> ParseJsonText(const std::string& text) {
  Json::CharReaderBuilder builder;
  std::string errors;
  std::istringstream stream(text);
  Json::Value parsed;
  try {
    if (!Json::parseFromStream(builder, stream, &parsed, &errors)) {
      return std::nullopt;
    }
  } catch (const Json::Exception&) {
    // nesting deeper than the stack limit of jsoncpp: not JSON for us
    return std::nullopt;
  }
  return parsed;
}

} // namespace

Json::Value BuildResourceContentState(const ResourceContent& content) {
  Json::Value state(Json::objectValue);
  state["uri"] = content.uri;
  state["mimeType"] = content.mime_type;
  state["text"] = content.text;

  if (!content.text.empty()) {
    if (auto parsed = ParseJsonText(content.text)) {
      state["data"] = std::move(*parsed);
    }
  }
  return state;
}

} // namespace mcp
} // namespace giga_edge
