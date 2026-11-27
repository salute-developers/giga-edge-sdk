#include "giga_edge/modules/smart_home_ha/actions.h"

#include "test_config.h"

#include "giga_edge/modules/assistant_core/json.h"

#include <catch.hpp>
using namespace giga_edge;
using namespace giga_edge::ha_test;

TEST_CASE("HA on and off target one named light with its area and domain", "[ha]") {
  for (bool state : {true, false}) {
    const auto action = WriteAction(Action("set_device_parameter", "on_off", state));
    REQUIRE(action.status == AssistantStatus::OK);
    REQUIRE(action.kind == McpResolvedActionKind::CALL_TOOL);
    CHECK(action.server_id == "ha");
    CHECK(action.tool_name == (state ? "intent__HassTurnOn" : "intent__HassTurnOff"));
    CHECK(
      ParseJsonOrThrow(action.arguments_json)
      == ParseJsonOrThrow(R"({"name":"Bedroom lamp","area":"Bedroom","domain":["light"]})")
    );
    CHECK(action.call_id == "query-1/tool/1");
  }
}
TEST_CASE("HA absolute brightness and temperature use native tool units", "[ha]") {
  const auto light = WriteAction(Action("set_device_parameter", "light_brightness", "25%"));
  REQUIRE(light.status == AssistantStatus::OK);
  CHECK(light.tool_name == "light__HassLightSet");
  CHECK(ParseJsonOrThrow(light.arguments_json)["brightness"] == 25);
  const auto climate = WriteAction(Action("set_device_parameter", "hvac_temp_set", 22.5, "hvac_heater_0"));
  REQUIRE(climate.status == AssistantStatus::OK);
  CHECK(climate.tool_name == "climate__HassClimateSetTemperature");
  CHECK(ParseJsonOrThrow(climate.arguments_json) == ParseJsonOrThrow(R"({"name":"Floor","temperature":22.5})"));
}
TEST_CASE("HA turns a climate on and off by its name and domain", "[ha]") {
  for (bool state : {true, false}) {
    const auto action = WriteAction(Action("set_device_parameter", "on_off", state, "hvac_heater_0"));
    REQUIRE(action.status == AssistantStatus::OK);
    CHECK(action.tool_name == (state ? "intent__HassTurnOn" : "intent__HassTurnOff"));
    CHECK(ParseJsonOrThrow(action.arguments_json) == ParseJsonOrThrow(R"({"name":"Floor","domain":["climate"]})"));
  }
}
TEST_CASE("HA sets a fan speed by HA's percentage of a named speed and a humidifier by its name", "[ha]") {
  const auto fan_text = std::string("- names: Fan\n  domain: fan\n  state: 'on'\n  areas: Bedroom\n  attributes:\n")
    + "    percentage: '50'\n";
  const auto humidifier_text
    = std::string("- names: Humidifier\n  domain: humidifier\n  state: 'on'\n  areas: Bedroom\n  attributes:\n")
    + "    humidity: '55'\n";
  auto snapshot = Snapshot();
  for (const auto* tool : {"fan__HassFanSetSpeed", "humidifier__HassHumidifierSetpoint"}) {
    snapshot.tools.push_back({.server_id = "ha", .tool_name = tool});
  }
  Json::Value root;
  root["text"] = "Live Context: devices\n" + fan_text + humidifier_text;
  snapshot.latest_resource_states[0].data_json = WriteJson(root);
  ha::CatalogBuilder b;
  const auto catalog = b.Build(snapshot);
  // The call, then the write that follows the fresh read of the device (the fan at 50 %, the humidifier at 55 %).
  const auto call = [&](const std::string& name, const std::string& device, const Json::Value& value) {
    const bool fan = device == "hvac_fan_0";
    auto read = ha::ResolveAction(
      Action(name, fan ? "hvac_air_flow_power" : "hvac_humidity_set", value, device), *catalog
    );
    if (read.status != AssistantStatus::OK) {
      return read;
    }
    Json::Value data;
    data["result"] = "Live Context: devices\n" + (fan ? fan_text : humidifier_text);
    return ha::Complete(Reply(read, data)).continuations.at(0);
  };
  const auto percentage = [](const McpResolvedAction& action) {
    REQUIRE(action.status == AssistantStatus::OK);
    CHECK(action.tool_name == "fan__HassFanSetSpeed");
    return ParseJsonOrThrow(action.arguments_json)["percentage"].asInt();
  };
  CHECK(
    ParseJsonOrThrow(call("set_device_parameter", "hvac_fan_0", "low").arguments_json)
    == ParseJsonOrThrow(R"({"name":"Fan","area":"Bedroom","domain":["fan"],"percentage":21})")
  );
  // The lowest percentage of each band: a fan rounds it up to a speed of its own.
  for (const auto& [speed, expected] : std::vector<std::pair<std::string, int>>{
         {"quiet", 1}, {"low", 21}, {"medium", 41}, {"high", 61}, {"turbo", 81}
       }) {
    CHECK(percentage(call("set_device_parameter", "hvac_fan_0", speed)) == expected);
  }
  // 50 % is medium: a relative change steps one speed and stays within them.
  CHECK(percentage(call("relative_change", "hvac_fan_0", "+")) == 61);
  CHECK(percentage(call("relative_change", "hvac_fan_0", "-")) == 21);
  // At the top speed + stays there.
  Json::Value top;
  top["result"] = "Live Context: devices\n" + fan_text.substr(0, fan_text.find("'50'")) + "'100'\n";
  const auto from_top = ha::ResolveAction(
    Action("relative_change", "hvac_air_flow_power", "+", "hvac_fan_0"), *catalog
  );
  CHECK(percentage(ha::Complete(Reply(from_top, top)).continuations.at(0)) == 81);
  // auto is a preset mode in HA, which MCP cannot set; a speed has no number to add.
  CHECK(call("set_device_parameter", "hvac_fan_0", "auto").error == "BAD_VALUE");
  CHECK(call("set_device_parameter", "hvac_fan_0", 50).error == "BAD_VALUE");
  CHECK(call("relative_change", "hvac_fan_0", "+10").error == "BAD_VALUE");

  const auto humidifier = call("set_device_parameter", "hvac_humidifier_0", "45%");
  REQUIRE(humidifier.status == AssistantStatus::OK);
  CHECK(humidifier.tool_name == "humidifier__HassHumidifierSetpoint");
  CHECK(ParseJsonOrThrow(humidifier.arguments_json) == ParseJsonOrThrow(R"({"name":"Humidifier","humidity":45})"));
  CHECK(call("set_device_parameter", "hvac_humidifier_0", 101).error == "BAD_VALUE");
  CHECK(ParseJsonOrThrow(call("relative_change", "hvac_humidifier_0", "+").arguments_json)["humidity"] == 65);
  // HassHumidifierSetpoint replies with no targets; an error or a failed target is still an error.
  const auto done = ha::Complete(Reply(
    humidifier, ParseJsonOrThrow(R"({"response_type":"action_done",
    "speech":{"plain":{"speech":"The Humidifier is set to 45%","extra_data":null}},"data":{"success":[],"failed":[]}})")
  ));
  REQUIRE(done.results.size() == 1);
  CHECK_FALSE(done.results[0].is_error);
  for (const auto& body :
       {R"({"response_type":"error","data":{"code":"no_valid_targets"}})",
        R"({"response_type":"action_done","data":{"success":[],"failed":[{"name":"Humidifier"}]}})"}) {
    CHECK(ha::Complete(Reply(humidifier, ParseJsonOrThrow(body))).results.at(0).is_error);
  }
  // HassFanSetSpeed replies as HassTurnOn does: a fan without the turn on feature is listed in failed.
  const auto fan = call("set_device_parameter", "hvac_fan_0", "high");
  const auto confirmed = ha::Complete(Reply(fan, ParseJsonOrThrow(R"({"speech":{},"response_type":"action_done",
    "data":{"success":[{"name":"Bedroom","type":"area","id":"bedroom"},{"name":"Fan","type":"entity","id":"fan.fan"}],
    "failed":[]}})")));
  CHECK_FALSE(confirmed.results.at(0).is_error);
  const auto refused = ha::Complete(Reply(fan, ParseJsonOrThrow(R"({"speech":{},"response_type":"action_done",
    "data":{"success":[{"name":"Bedroom","type":"area","id":"bedroom"}],
    "failed":[{"name":"Fan","type":"entity","id":"fan.fan"}]}})")));
  CHECK(refused.results.at(0).is_error);
  CHECK(refused.results.at(0).error == "DEVICE_UNREACHABLE");
}
TEST_CASE("HA invalid targets values and parameters never become tool calls", "[ha]") {
  for (const auto& r :
       {Action("set_device_parameter", "light_brightness", 101),
        Action("set_device_parameter", "light_brightness", "nan"),
        Action("set_device_parameter", "light_brightness", -1),
        Action("set_device_parameter", "on_off", "perhaps"),
        Action("set_device_parameter", "unknown", true),
        Action("set_device_parameter", "on_off", true, "light_999"),
        Action("set_device_parameter", "hvac_temp_set", "50%", "hvac_heater_0"),
        Action("run_arbitrary_tool", "on_off", true)}) {
    const auto a = ha::ResolveAction(r, *Catalog());
    CHECK(a.status == AssistantStatus::ERROR);
    CHECK(a.kind == McpResolvedActionKind::NONE);
    CHECK(a.call_id == "query-1/tool/1");
  }
}
TEST_CASE("HA reads fresh MCP values instead of returning a startup snapshot", "[ha]") {
  auto action = ha::ResolveAction(Action("get_device_parameter", "light_brightness"), *Catalog());
  REQUIRE(action.tool_name == "homeassistant__GetLiveContext");
  CHECK(
    ParseJsonOrThrow(action.arguments_json)
    == ParseJsonOrThrow(R"({"name":"Bedroom lamp","domain":"light","area":"Bedroom"})")
  );
  auto data = ReadContext();
  auto text = data["result"].asString();
  text.replace(text.find("'30'"), 4, "'70'");
  data["result"] = text;
  const auto done = ha::Complete(Reply(action, data));
  REQUIRE(done.results.size() == 1);
  CHECK_FALSE(done.results[0].is_error);
  CHECK(ParseJsonOrThrow(done.results[0].value_json).asDouble() == 70);
}
TEST_CASE("HA relative brightness reads then writes once retaining the logical call id", "[ha]") {
  const auto read = ha::ResolveAction(Action("relative_change", "light_brightness", "+10%"), *Catalog());
  REQUIRE(read.tool_name == "homeassistant__GetLiveContext");
  CHECK(ha::NeedsContinuation(read));
  const auto step = ha::Complete(Reply(read, ReadContext()));
  REQUIRE(step.continuations.size() == 1);
  CHECK(step.results.empty());
  const auto& write = step.continuations[0];
  CHECK(write.tool_name == "light__HassLightSet");
  CHECK(write.call_id == read.call_id);
  CHECK(ParseJsonOrThrow(write.arguments_json)["brightness"] == 40);
  CHECK_FALSE(ha::NeedsContinuation(write));
  const auto done = ha::Complete(Reply(write, Success()));
  REQUIRE(done.results.size() == 1);
  CHECK_FALSE(done.results[0].is_error);
  CHECK(done.continuations.empty());
}
TEST_CASE("HA errors and unconfirmed intent responses are never reported as success", "[ha]") {
  const auto action = WriteAction(Action("set_device_parameter", "on_off", true));
  for (const auto& body :
       {R"({"response_type":"error","data":{"code":"no_valid_targets"}})",
        R"({"response_type":"action_done","data":{"success":[],"failed":[]}})",
        R"({"response_type":"action_done","data":{"success":[{}],"failed":[{"name":"Bedroom lamp"}]}})",
        R"({"unexpected":true})"}) {
    const auto done = ha::Complete(Reply(action, ParseJsonOrThrow(body)));
    REQUIRE(done.results.size() == 1);
    CHECK(done.results[0].is_error);
    CHECK(done.results[0].call_id == action.call_id);
    CHECK(done.continuations.empty());
  }
  auto reply = Reply(action, Success());
  reply.is_error = true;
  REQUIRE(ha::Complete(reply).results.size() == 1);
  CHECK(ha::Complete(reply).results[0].is_error);
}
TEST_CASE("HA read ambiguity unavailable states and missing properties abort relative writes", "[ha]") {
  const auto action = ha::ResolveAction(Action("relative_change", "light_brightness", "+"), *Catalog());
  for (const auto& text :
       {"Live Context: devices\n- names: Bedroom lamp\n  domain: light\n  state: unavailable\n  areas: Bedroom\n",
        "Live Context: devices\n- names: Bedroom lamp\n  domain: light\n  state: 'on'\n  areas: Bedroom\n",
        "Live Context: devices\n- names: Other lamp\n  domain: light\n  state: 'on'\n  areas: Bedroom\n"}) {
    Json::Value data;
    data["result"] = text;
    auto done = ha::Complete(Reply(action, data));
    REQUIRE(done.results.size() == 1);
    CHECK(done.results[0].is_error);
    CHECK(done.continuations.empty());
  }
}
TEST_CASE("HA never executes incomplete cancelled or truncated generations", "[ha]") {
  auto r = Action("set_device_parameter", "on_off", true);
  r.is_final = false;
  CHECK(ha::ResolveAction(r, *Catalog()).status == AssistantStatus::SKIPPED);
  r.is_final = true;
  for (const auto reason : {StreamFinishReason::CANCELLED, StreamFinishReason::MAX_TOKENS, StreamFinishReason::ERROR}) {
    r.finish_reason = reason;
    CHECK(ha::ResolveAction(r, *Catalog()).status == AssistantStatus::SKIPPED);
  }
}

TEST_CASE("HA checks current target uniqueness before an absolute write", "[ha]") {
  const auto action = ha::ResolveAction(Action("set_device_parameter", "on_off", true), *Catalog());
  CHECK(action.tool_name == "homeassistant__GetLiveContext");
  CHECK(ha::NeedsContinuation(action));
}
TEST_CASE("HA refuses relative continuation when fresh context adds a matching alias", "[ha]") {
  const auto action = ha::ResolveAction(Action("relative_change", "light_brightness", "+"), *Catalog());
  auto data = ReadContext();
  data["result"]=data["result"].asString()+"- names: Ceiling, Bedroom lamp\n  domain: light\n  state: 'on'\n  areas: Bedroom\n  attributes:\n    brightness_pct: '20'\n";
  auto done = ha::Complete(Reply(action, data));
  CHECK(done.continuations.empty());
  REQUIRE(done.results.size() == 1);
  CHECK(done.results[0].is_error);
}

TEST_CASE("HA unknown function reaches the model as UNKNOWN_FUNCTION", "[ha]") {
  const auto a = ha::ResolveAction(Action("search_and_play", "on_off", true), *Catalog());
  REQUIRE(a.status == AssistantStatus::ERROR);
  const auto out = ha::Complete({.action = a, .is_error = true, .error = a.error});
  REQUIRE(out.results.size() == 1);
  CHECK(out.results[0].is_error);
  CHECK(out.results[0].error == "UNKNOWN_FUNCTION");
}

TEST_CASE("HA reads online fresh even of an unavailable device and never writes it", "[ha][online]") {
  ha::CatalogBuilder builder;
  const auto catalog = builder.Build(Snapshot(), {.include_online = true});
  for (const std::string state : {"unavailable", "unknown", "off", "on"}) {
    const auto read = ha::ResolveAction(Action("get_device_parameter", "online"), *catalog);
    REQUIRE(read.status == AssistantStatus::OK);
    REQUIRE(read.tool_name == "homeassistant__GetLiveContext");
    auto data = ReadContext();
    auto text = data["result"].asString();
    data["result"] = text.replace(text.find("state: 'on'"), 11, "state: '" + state + "'");
    const auto done = ha::Complete(Reply(read, data));
    REQUIRE(done.results.size() == 1);
    CHECK_FALSE(done.results[0].is_error);
    CHECK(ParseJsonOrThrow(done.results[0].value_json) == (state != "unavailable"));
  }
  for (const auto& call :
       {Action("set_device_parameter", "online", false),
        Action("relative_change", "online", "+"),
        CopyAction("online", "light_0", "light_0")}) {
    CHECK(ha::ResolveAction(call, *catalog).error == "PARAMETER_READ_ONLY");
  }
  CHECK(ha::ResolveAction(Action("get_device_parameter", "online"), *Catalog()).error == "UNKNOWN_PARAMETER");
}
