#include "test_config.h"

#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/assistant_core/nodes/tool_loop_guard/node.h"
#include "giga_edge/modules/function_calling/nodes/fc_tool_result/node.h"
#include "giga_edge/modules/function_calling/tool_call_parsers.h"
#include "giga_edge/modules/smart_home_ha/nodes.h"

#include <catch.hpp>
using namespace giga_edge;
using namespace giga_edge::ha_test;

namespace {
McpResolvedAction Next(const McpResolvedAction& action, const Json::Value& data) {
  auto done = ha::Complete(Reply(action, data));
  REQUIRE(done.results.empty());
  REQUIRE(done.continuations.size() == 1);
  return done.continuations.front();
}
void Error(const ha::Completion& done, const std::string& code) {
  REQUIRE(done.continuations.empty());
  REQUIRE(done.results.size() == 1);
  CHECK(done.results[0].is_error);
  CHECK(done.results[0].error == code);
  CHECK(done.results[0].name == "copy_device_parameter");
}
McpResolvedAction Tick(
  NodeContext& node,
  const AssistantResult& result = {},
  const std::vector<McpResolvedAction>& next = {},
  const UserQuery& query = {},
  bool reset = false,
  ha::CatalogPtr catalog = CopyCatalog()
) {
  NodePush(node, "user_query", query);
  NodePush(node, "assistant_result", result);
  NodePush(node, "ha_catalog", catalog);
  NodePush(node, "continuations", next);
  NodePush(node, "dialogue_reset", reset);
  return NodeGet<McpResolvedAction>(node, "mcp_resolved_action");
}
const UserQuery kQuery{.request_id = "query-1", .text = "copy brightness"};
} // namespace

TEST_CASE("HA copy reads the source fresh then checks and writes the destination", "[ha][copy]") {
  const auto catalog = CopyCatalog();
  const auto read = ha::ResolveAction(CopyAction(), *catalog);
  REQUIRE(read.status == AssistantStatus::OK);
  CHECK(read.tool_name == "homeassistant__GetLiveContext");
  CHECK(
    ParseJsonOrThrow(read.arguments_json) == ParseJsonOrThrow(R"({"name":"Desk lamp","domain":"light","area":"Study"})")
  );
  CHECK(ha::NeedsContinuation(read));
  const auto target = Next(read, SourceContext());
  CHECK(target.tool_name == "homeassistant__GetLiveContext");
  CHECK(ParseJsonOrThrow(target.arguments_json)["name"] == "Bedroom lamp");
  CHECK(ParseJsonOrThrow(target.arguments_json)["domain"] == "light");
  CHECK(ha::MatchesTarget(target, *catalog));
  CHECK(ha::NeedsContinuation(target));
  const auto write = Next(target, ReadContext());
  CHECK(write.tool_name == "light__HassLightSet");
  CHECK(ParseJsonOrThrow(write.arguments_json)["brightness"] == 73);
  CHECK(ParseJsonOrThrow(write.arguments_json)["name"] == "Bedroom lamp");
  CHECK(write.call_id == read.call_id);
  CHECK(write.source_action_name == "copy_device_parameter");
  CHECK_FALSE(ha::NeedsContinuation(write));
  CHECK(ha::MatchesTarget(write, *catalog));
  const auto done = ha::Complete(Reply(write, Success()));
  REQUIRE(done.results.size() == 1);
  CHECK_FALSE(done.results[0].is_error);
  CHECK(done.results[0].value_json.empty());
}

TEST_CASE("HA copy rejects invalid devices parameters and arguments before MCP", "[ha][copy]") {
  for (const auto& item : std::vector<std::pair<AssistantResult, std::string>>{
         {CopyAction("on_off", "missing"), "DEVICE_UNREACHABLE"},
         {CopyAction("on_off", "light_0", "missing"), "DEVICE_UNREACHABLE"},
         {CopyAction("missing"), "UNKNOWN_PARAMETER"},
         {CopyAction("light_brightness", "light_0", "hvac_heater_0"), "UNKNOWN_PARAMETER"},
         {CopyAction("temperature", "hvac_heater_0", "hvac_heater_0"), "PARAMETER_READ_ONLY"}
       }) {
    const auto action = ha::ResolveAction(item.first, *CopyCatalog());
    REQUIRE(action.status == AssistantStatus::ERROR);
    CHECK(action.kind == McpResolvedActionKind::NONE);
    Error(ha::Complete({.action = action, .is_error = true, .error = action.error}), item.second);
  }
  for (const auto& field : {"device_id", "parameter_name", "source_device_id"}) {
    auto result = CopyAction();
    auto args = ParseJsonOrThrow(result.tool_calls[0].arguments_json);
    args.removeMember(field);
    result.tool_calls[0].arguments_json = WriteJson(args);
    CHECK(ha::ResolveAction(result, *CopyCatalog()).error == "MISSING_ARGS");
  }
  auto catalog = std::make_shared<ha::Catalog>(*CopyCatalog());
  catalog->devices.at("light_0").features.at("on_off").writable = false;
  CHECK(ha::ResolveAction(CopyAction("on_off"), *catalog).error == "PARAMETER_READ_ONLY");
  catalog->tools.at("ha").erase("homeassistant__GetLiveContext");
  CHECK(ha::ResolveAction(CopyAction(), *catalog).status == AssistantStatus::ERROR);
}

TEST_CASE("HA copy refuses unknown offline and ambiguous source values", "[ha][copy]") {
  const auto read = ha::ResolveAction(CopyAction(), *CopyCatalog());
  for (const auto& change : std::vector<std::pair<std::string, std::string>>{
         {"    brightness_pct: '73'", "    other: 1"},
         {"state: 'on'", "state: unavailable"},
         {"Desk lamp", "Different lamp"}
       }) {
    auto data = SourceContext();
    auto text = data["result"].asString();
    text.replace(text.find(change.first), change.first.size(), change.second);
    data["result"] = text;
    Error(
      ha::Complete(Reply(read, data)),
      change.first == "    brightness_pct: '73'" ? "UNKNOWN_PARAMETER" : "DEVICE_UNREACHABLE"
    );
  }
  auto data = SourceContext();
  data["result"] = data["result"].asString() + "- names: Desk lamp\n  domain: light\n  state: 'on'\n  areas: Study\n";
  Error(ha::Complete(Reply(read, data)), "DEVICE_UNREACHABLE");
  auto reply = Reply(read, SourceContext());
  reply.is_error = true;
  reply.error = "transport error";
  Error(ha::Complete(reply), "DEVICE_UNREACHABLE");
}

TEST_CASE("HA copy supports a shared parameter across device types and servers", "[ha][copy]") {
  auto catalog = std::make_shared<ha::Catalog>(*CopyCatalog());
  catalog->devices.at("light_1").server_id = "source-ha";
  catalog->devices.at("light_1").features.at("on_off").writable = false;
  catalog->tools["source-ha"] = {"homeassistant__GetLiveContext"};
  const auto source = ha::ResolveAction(CopyAction("on_off", "hvac_heater_0"), *catalog);
  REQUIRE(source.status == AssistantStatus::OK);
  CHECK(source.server_id == "source-ha");
  const auto target = Next(source, SourceContext());
  CHECK(target.server_id == "ha");
  CHECK(ha::MatchesTarget(target, *catalog));
  Json::Value data;
  data["result"] = "Live Context: devices\n- names: Floor\n  domain: climate\n  state: heat\n";
  const auto write = Next(target, data);
  CHECK(write.tool_name == "intent__HassTurnOn");
  CHECK(ParseJsonOrThrow(write.arguments_json) == ParseJsonOrThrow(R"({"name":"Floor","domain":["climate"]})"));
}

TEST_CASE("HA copy to a script uses its own tool and cannot read a script", "[ha][copy]") {
  auto catalog = std::make_shared<ha::Catalog>(*CopyCatalog());
  auto& script = catalog->devices["socket_0"];
  script = catalog->devices.at("light_0");
  script.domain = "script";
  script.name = "Morning";
  script.tool = "script__morning";
  catalog->tools["ha"].insert(script.tool);
  const auto read = ha::ResolveAction(CopyAction("on_off", "socket_0"), *catalog);
  const auto write = Next(read, SourceContext());
  CHECK(write.tool_name == "script__morning");
  CHECK(ParseJsonOrThrow(write.arguments_json).empty());
  CHECK(ha::MatchesTarget(write, *catalog));
  CHECK(ha::ResolveAction(CopyAction("on_off", "light_0", "socket_0"), *catalog).error == "UNKNOWN_PARAMETER");
}

TEST_CASE("HA copy graph preserves both continuations and invalidates either on reset", "[ha][copy][nodes]") {
  for (const int stop_after : {0, 1}) {
    for (const bool reset : {false, true}) {
      auto resolver = CreateNode("ha_action_resolver", "");
      const auto source = Tick(resolver, CopyAction(), {}, kQuery);
      auto next = Next(source, SourceContext());
      if (stop_after != 0) {
        next = Tick(resolver, {}, {next});
        REQUIRE(next.tool_name == "homeassistant__GetLiveContext");
        next = Next(next, ReadContext());
      }
      const UserQuery query = reset ? UserQuery{} : UserQuery{.request_id = "new", .text = "stop"};
      CHECK(Tick(resolver, {}, {next}, query, reset).status == AssistantStatus::SKIPPED);
      CHECK(Tick(resolver, {}, {next}).status == AssistantStatus::SKIPPED);
    }
  }
}

TEST_CASE("HA copy graph rejects changed destination and missing write tool", "[ha][copy][nodes]") {
  for (const bool remove_tool : {false, true}) {
    auto resolver = CreateNode("ha_action_resolver", "");
    const auto source = Tick(resolver, CopyAction(), {}, kQuery);
    const auto target = Tick(resolver, {}, {Next(source, SourceContext())});
    const auto write = Next(target, ReadContext());
    auto changed = std::make_shared<ha::Catalog>(*CopyCatalog());
    if (remove_tool) {
      changed->tools.at("ha").erase("light__HassLightSet");
    } else {
      changed->devices.at("light_0").name = "Different lamp";
    }
    const auto rejected = Tick(resolver, {}, {write}, {}, false, changed);
    CHECK(rejected.status == AssistantStatus::ERROR);
    CHECK(rejected.kind == McpResolvedActionKind::NONE);
  }
  const auto source = ha::ResolveAction(CopyAction(), *CopyCatalog());
  const auto target = Next(source, SourceContext());
  auto data = ReadContext();
  data["result"] = data["result"].asString()
    + "- names: Bedroom lamp\n  domain: light\n  state: 'on'\n  areas: Bedroom\n";
  Error(ha::Complete(Reply(target, data)), "DEVICE_UNREACHABLE");
}

TEST_CASE("HA copy does not rearm a rejected destination read", "[ha][copy][nodes]") {
  auto resolver = CreateNode("ha_action_resolver", "");
  const auto source = Tick(resolver, CopyAction(), {}, kQuery);
  const auto target = Next(source, SourceContext());
  auto changed = std::make_shared<ha::Catalog>(*CopyCatalog());
  changed->devices.at("light_0").name = "Different lamp";
  CHECK(Tick(resolver, {}, {target}, {}, false, changed).status == AssistantStatus::ERROR);
  CHECK(Tick(resolver, {}, {Next(target, ReadContext())}).status == AssistantStatus::SKIPPED);
}

TEST_CASE("HA graph runs a batch of copies including a failed first call", "[ha][copy][nodes]") {
  const bool first_fails = GENERATE(false, true);
  auto guard = CreateNode(
    "tool_loop_guard", "{parallel_tool_calls: true}", {}, {"tool_result", "cancel", "dialogue_reset"}
  );
  auto resolver = CreateNode("ha_action_resolver", "");
  auto format = CreateNode("fc_tool_result", R"(success: '{"status": "success"}'
success_with_value: '{"status": "success", "value": {value}}'
failure: '{"status": "fail", "error": {error}}')");
  auto batch = first_fails ? CopyAction("mute") : CopyAction();
  auto second = CopyAction("on_off");
  second.tool_calls[0].id = "query-1/tool/2";
  batch.tool_calls.push_back(second.tool_calls[0]);
  ToolResult done;
  for (int i = 0; i < 2; ++i) {
    NodePush(guard, "dialogue_reset", false);
    NodePush(guard, "cancel", false);
    NodePush(guard, "tool_result", done);
    NodePush(guard, "user_query", i == 0 ? kQuery : UserQuery{});
    NodePush(guard, "assistant_result", i == 0 ? batch : AssistantResult{});
    const auto step = NodeGet<AssistantResult>(guard, "assistant_result");
    REQUIRE(step.tool_calls.size() == 1);
    CHECK(step.tool_calls[0].id == batch.tool_calls[i].id);
    const auto source = Tick(resolver, step, {}, i == 0 ? kQuery : UserQuery{});
    ha::Completion result;
    if (i == 0 && first_fails) {
      REQUIRE(source.status == AssistantStatus::ERROR);
      result = ha::Complete({.action = source, .is_error = true, .error = source.error});
    } else {
      const auto target = Tick(resolver, {}, {Next(source, SourceContext())});
      const auto write = Tick(resolver, {}, {Next(target, ReadContext())});
      CHECK(write.tool_name == (i == 0 ? "light__HassLightSet" : "intent__HassTurnOn"));
      result = ha::Complete(Reply(write, Success()));
    }
    NodePush(format, "tool_results", result.results);
    done = NodeGet<ToolResult>(format, "tool_result");
    CHECK(done.call_id == step.tool_calls[0].id);
    CHECK(ParseJsonOrThrow(done.content)["status"] == (i == 0 && first_fails ? "fail" : "success"));
  }
}

TEST_CASE("HA copy shares set conversion for cover commands and target temperature", "[ha][copy]") {
  for (const std::string parameter : {"open_set", "open_percentage", "hvac_temp_set"}) {
    const bool climate = parameter == "hvac_temp_set";
    auto catalog = std::make_shared<ha::Catalog>();
    catalog->tools["ha"] = {
      "homeassistant__GetLiveContext",
      "intent__HassTurnOff",
      "intent__HassSetPosition",
      "climate__HassClimateSetTemperature"
    };
    ha::Device source;
    source.server_id = "ha";
    source.domain = climate ? "climate" : "cover";
    source.name = "Source";
    source.features[parameter] = {false, {}};
    auto destination = source;
    destination.name = "Target";
    destination.domain = climate ? "climate" : "valve";
    destination.features[parameter].writable = true;
    catalog->devices["source"] = source;
    catalog->devices["target"] = destination;
    const auto data = [&](const ha::Device& device) {
      Json::Value value;
      value["result"] = "Live Context: devices\n- names: " + device.name + "\n  domain: " + device.domain
        + (climate ? "\n  state: heat\n  attributes:\n    temperature: 22.5\n"
                   : "\n  state: closed\n  attributes:\n    current_position: 63\n");
      return value;
    };
    const auto read = ha::ResolveAction(CopyAction(parameter, "target", "source"), *catalog);
    const auto target = Next(read, data(source));
    const auto write = Next(target, data(destination));
    CHECK(ha::MatchesTarget(write, *catalog));
    const auto args = ParseJsonOrThrow(write.arguments_json);
    CHECK(args["name"] == "Target");
    if (climate) {
      CHECK(write.tool_name == "climate__HassClimateSetTemperature");
      CHECK(args["temperature"] == 22.5);
    } else if (parameter == "open_percentage") {
      CHECK(write.tool_name == "intent__HassSetPosition");
      CHECK(args["position"] == 63);
    } else {
      CHECK(write.tool_name == "intent__HassTurnOff");
    }
  }
}

TEST_CASE("demo graph config parses a copy with source first", "[ha][parser][copy]") {
  const auto graph = YAML::LoadFile(
    std::string(GIGA_EDGE_TEST_SOURCE_DIR) + "/giga_edge/modules/smart_home_ha/tests/data/function_calling/config.yaml"
  );
  YAML::Node config;
  for (const auto& node : graph["nodes"]) {
    if (node["name"].as<std::string>() == "llm") {
      config = node["params"]["tool_call_parser"];
    }
  }
  CompactToolCallParser parser(ParseCompactToolCallParserParams(config));
  const auto calls = parser.Parse(
    "<start_function_call><f_copy_device_parameter><d_light>2<arg_sep><d_light>1<arg_sep><p_rw_light_brightness><|mes"
    "sage_sep|>"
  );
  REQUIRE(calls.size() == 1);
  CHECK(calls[0].name == "copy_device_parameter");
  CHECK(
    calls[0].arguments_json
    == R"({"source_device_id":"light_2","device_id":"light_1","parameter_name":"light_brightness"})"
  );
}

TEST_CASE("HA copy requires the read tool on the source server", "[ha][copy]") {
  auto catalog = std::make_shared<ha::Catalog>(*CopyCatalog());
  catalog->devices.at("light_1").server_id = "source-ha";
  catalog->tools["source-ha"] = {};
  REQUIRE(catalog->tools.at("ha").count("homeassistant__GetLiveContext") == 1);
  const auto action = ha::ResolveAction(CopyAction(), *catalog);
  CHECK(action.status == AssistantStatus::ERROR);
  CHECK(action.kind == McpResolvedActionKind::NONE);
  CHECK(action.error == "DEVICE_UNREACHABLE");
}

TEST_CASE("HA pending set checks the read tool before issuing a target check", "[ha][copy]") {
  auto catalog = std::make_shared<ha::Catalog>(*CopyCatalog());
  catalog->tools["ha"].erase("homeassistant__GetLiveContext");
  const auto action = ha::ResolveAction(Action("set_device_parameter", "light_brightness", 45), *catalog);
  CHECK(action.status == AssistantStatus::ERROR);
  CHECK(action.kind == McpResolvedActionKind::NONE);
  CHECK(action.error == "DEVICE_UNREACHABLE");
}

TEST_CASE("HA copy rejects write-only sources before calling MCP", "[ha][copy]") {
  for (const auto& item : std::vector<std::pair<std::string, std::string>>{
         {"media_player", "mute"}, {"scene", "on_off"}, {"script", "on_off"}
       }) {
    auto catalog = std::make_shared<ha::Catalog>(*CopyCatalog());
    auto& source = catalog->devices.at("light_1");
    source.domain = item.first;
    source.features[item.second] = {true, {}};
    catalog->devices.at("light_0").features[item.second] = {true, {}};
    const auto action = ha::ResolveAction(CopyAction(item.second), *catalog);
    REQUIRE(action.status == AssistantStatus::ERROR);
    CHECK(action.kind == McpResolvedActionKind::NONE);
    CHECK(action.error == "UNKNOWN_PARAMETER");
    Error(ha::Complete({.action = action, .is_error = true, .error = action.error}), "UNKNOWN_PARAMETER");
  }
}

TEST_CASE("HA copy activates a scene and treats copying off as the usual scene set", "[ha][copy]") {
  for (const bool on : {false, true}) {
    auto catalog = std::make_shared<ha::Catalog>(*CopyCatalog());
    auto& scene = catalog->devices.at("light_0");
    scene.domain = "scene";
    scene.features["on_off"] = {true, {}};
    auto source_data = SourceContext();
    auto text = source_data["result"].asString();
    if (!on) {
      text.replace(text.find("state: 'on'"), 11, "state: 'off'");
    }
    source_data["result"] = text;
    const auto read = ha::ResolveAction(CopyAction("on_off"), *catalog);
    const auto target = Next(read, source_data);
    Json::Value scene_data;
    scene_data["result"]
      = "Live Context: devices\n- names: Bedroom lamp\n  domain: scene\n  state: unknown\n  areas: Bedroom\n";
    const auto done = ha::Complete(Reply(target, scene_data));
    if (on) {
      REQUIRE(done.continuations.size() == 1);
      CHECK(done.results.empty());
      const auto& write = done.continuations[0];
      CHECK(write.tool_name == "intent__HassTurnOn");
      CHECK(ParseJsonOrThrow(write.arguments_json)["domain"][0] == "scene");
      CHECK_FALSE(ha::Complete(Reply(write, Success())).results.at(0).is_error);
    } else {
      REQUIRE(done.results.size() == 1);
      CHECK_FALSE(done.results[0].is_error);
      CHECK(done.continuations.empty());
    }
  }
}

TEST_CASE("HA copy transfers TV volume through the regular percent conversion", "[ha][copy]") {
  auto catalog = std::make_shared<ha::Catalog>(*CopyCatalog());
  for (const auto* id : {"light_0", "light_1"}) {
    auto& tv = catalog->devices.at(id);
    tv.domain = "media_player";
    tv.features["volume_int"] = {true, 10};
  }
  catalog->tools["ha"].insert("media_player__HassSetVolume");
  const auto read = ha::ResolveAction(CopyAction("volume_int"), *catalog);
  Json::Value source, destination;
  source["result"]
    = "Live Context: devices\n- names: Desk lamp\n  domain: media_player\n  state: playing\n  areas: Study\n  "
      "attributes:\n    device_class: tv\n    volume_level: 0.63\n";
  destination["result"]
    = "Live Context: devices\n- names: Bedroom lamp\n  domain: media_player\n  state: playing\n  areas: Bedroom\n  "
      "attributes:\n    device_class: tv\n    volume_level: 0.1\n";
  const auto write = Next(Next(read, source), destination);
  CHECK(write.tool_name == "media_player__HassSetVolume");
  CHECK(ParseJsonOrThrow(write.arguments_json)["volume_level"] == 63);
  CHECK_FALSE(ha::Complete(Reply(write, Success())).results.at(0).is_error);
}

TEST_CASE("HA copy of an off light transfers zero brightness just like get", "[ha][copy]") {
  Json::Value data;
  data["result"]
    = "Live Context: devices\n- names: Desk lamp\n  domain: light\n  state: 'off'\n  areas: Study\n  attributes:\n    "
      "brightness:\n";
  const auto get = ha::ResolveAction(Action("get_device_parameter", "light_brightness", {}, "light_1"), *CopyCatalog());
  const auto read_value = ha::Complete(Reply(get, data)).results.at(0).value_json;
  CHECK(ParseJsonOrThrow(read_value) == 0.0);
  const auto read = ha::ResolveAction(CopyAction(), *CopyCatalog());
  const auto write = Next(Next(read, data), ReadContext());
  CHECK(write.tool_name == "light__HassLightSet");
  CHECK(ParseJsonOrThrow(write.arguments_json)["brightness"] == 0);
}

TEST_CASE("HA copy reports failure when its write is refused", "[ha][copy]") {
  const auto read = ha::ResolveAction(CopyAction(), *CopyCatalog());
  const auto write = Next(Next(read, SourceContext()), ReadContext());
  Error(
    ha::Complete(Reply(write, ParseJsonOrThrow(R"({"response_type":"error","data":{"code":"no_valid_targets"}})"))),
    "DEVICE_UNREACHABLE"
  );
  auto partial = Success();
  partial["data"]["failed"].append(ParseJsonOrThrow(R"({"name":"Other target","id":"light.other","type":"entity"})"));
  Error(ha::Complete(Reply(write, partial)), "DEVICE_UNREACHABLE");
  auto failed = Reply(write, Success());
  failed.is_error = true;
  failed.error = "transport error";
  Error(ha::Complete(failed), "DEVICE_UNREACHABLE");
}
