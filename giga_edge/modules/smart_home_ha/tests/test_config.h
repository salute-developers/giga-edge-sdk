#pragma once
#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/smart_home_ha/actions.h"
namespace giga_edge::ha_test {
inline McpCatalogSnapshot Snapshot() {
  McpCatalogSnapshot s;
  for (const auto* t :
       {"intent__HassTurnOn",
        "intent__HassTurnOff",
        "light__HassLightSet",
        "homeassistant__GetLiveContext",
        "climate__HassClimateSetTemperature"}) {
    s.tools.push_back({.server_id = "ha", .tool_name = t});
  }
  Json::Value root;
  root["text"]
    = "Live Context: devices\n- names: Bedroom lamp\n  domain: light\n  state: 'on'\n"
      "  areas: Bedroom\n  attributes:\n    brightness_pct: '30'\n"
      "- names: Floor\n  domain: climate\n  state: heat\n  attributes:\n    temperature: 21\n";
  s.latest_resource_states.push_back({"ha", "homeassistant://assist/context-snapshot", WriteJson(root)});
  return s;
}
inline ha::CatalogPtr Catalog() {
  ha::CatalogBuilder b;
  return b.Build(Snapshot());
}
inline AssistantResult Action(
  const std::string& name, const std::string& parameter, Json::Value value = {}, const std::string& device = "light_0"
) {
  Json::Value args;
  args["device_id"] = device;
  args["parameter_name"] = parameter;
  args["value"] = std::move(value);
  AssistantResult r;
  r.is_final = true;
  r.finish_reason = StreamFinishReason::EOS;
  r.request_id = "query-1";
  r.tool_calls = {{.id = "query-1/tool/1", .name = name, .arguments_json = WriteJson(args)}};
  return r;
}
inline McpActionCompletion Reply(McpResolvedAction action, const Json::Value& value) {
  Json::Value blocks(Json::arrayValue), text;
  text["type"] = "text";
  text["text"] = WriteJson(value);
  blocks.append(text);
  return {.action = std::move(action), .content_json = WriteJson(blocks)};
}
inline Json::Value Success() {
  return ParseJsonOrThrow(R"({"speech":{},"response_type":"action_done","data":{
    "success":[{"type":"entity","name":"Bedroom lamp","id":"light.bedroom"}],"failed":[]}})");
}
inline Json::Value ReadContext() {
  Json::Value v;
  v["result"]
    = "Live Context: devices\n- names: Bedroom lamp\n  domain: light\n"
      "  state: 'on'\n  areas: Bedroom\n  attributes:\n    brightness_pct: '30'\n";
  return v;
}
inline McpResolvedAction WriteAction(const AssistantResult& result) {
  const auto read = ha::ResolveAction(result, *Catalog());
  auto data = ReadContext();
  if (ParseJsonOrThrow(result.tool_calls[0].arguments_json)["device_id"] == "hvac_heater_0") {
    data["result"]
      = "Live Context: devices\n- names: Floor\n  domain: climate\n  state: heat\n  attributes:\n    temperature: 21\n";
  }
  return ha::Complete(Reply(read, data)).continuations.at(0);
}

inline Json::Value SourceContext() {
  Json::Value value;
  value["result"]
    = "Live Context: devices\n- names: Desk lamp\n  domain: light\n  state: 'on'\n"
      "  areas: Study\n  attributes:\n    brightness_pct: '73'\n";
  return value;
}
inline McpCatalogSnapshot CopySnapshot() {
  auto snapshot = Snapshot();
  auto data = ParseJsonOrThrow(snapshot.latest_resource_states[0].data_json);
  data["text"] = data["text"].asString()
    + "- names: Desk lamp\n  domain: light\n  state: 'off'\n  areas: Study\n  attributes:\n    brightness_pct: '10'\n";
  snapshot.latest_resource_states[0].data_json = WriteJson(data);
  return snapshot;
}
inline ha::CatalogPtr CopyCatalog() {
  ha::CatalogBuilder builder;
  return builder.Build(CopySnapshot());
}
inline AssistantResult CopyAction(
  const std::string& parameter = "light_brightness",
  const std::string& destination = "light_0",
  const std::string& source = "light_1"
) {
  auto result = Action("copy_device_parameter", parameter, {}, destination);
  auto args = ParseJsonOrThrow(result.tool_calls[0].arguments_json);
  args.removeMember("value");
  args["source_device_id"] = source;
  result.tool_calls[0].arguments_json = WriteJson(args);
  return result;
}

} // namespace giga_edge::ha_test
