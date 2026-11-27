#include "giga_edge/modules/assistant_core/nodes/tool_specs_from_json/node.h"

#include <json/json.h>

#include <functional>
#include <sstream>

namespace giga_edge {

ToolSpecsFromJson::ToolSpecsFromJson(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void ToolSpecsFromJson::ProcessOnce() {
  const std::string& text = GetJson();
  ToolSpecs out;
  if (text.empty()) {
    PushToolSpecs(std::move(out));
    return;
  }

  Json::Value root;
  Json::CharReaderBuilder builder;
  std::string errors;
  std::istringstream stream(text);
  if (!Json::parseFromStream(builder, stream, &root, &errors) || !root.isArray()) {
    out.status = AssistantStatus::ERROR;
    out.error = "tool_specs_from_json: expected a JSON array: " + errors;
    PushToolSpecs(std::move(out));
    return;
  }

  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  writer["emitUTF8"] = true;
  for (const auto& item : root) {
    ToolSpec spec;
    spec.name = item.get("name", "").asString();
    spec.description = item.get("description", "").asString();
    if (item.isMember("parameters")) {
      spec.parameters_json = Json::writeString(writer, item["parameters"]);
    }
    if (!spec.name.empty()) {
      out.tools.push_back(std::move(spec));
    }
  }
  std::ostringstream revision;
  revision << std::hex << std::hash<std::string>{}(text);
  out.revision = revision.str();
  PushToolSpecs(std::move(out));
}

} // namespace giga_edge
