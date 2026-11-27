#include "test_config.h"

#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/smart_home_ha/actions.h"

#include <catch.hpp>
using namespace giga_edge;
using namespace giga_edge::ha_test;

namespace {
const std::string kHouse = R"(- names: Living curtain
  domain: cover
  state: open
  areas: Living
  attributes:
    device_class: curtain
    current_position: '60'
- names: Kitchen blind
  domain: cover
  state: closed
  attributes:
    device_class: blind
- names: Living TV
  domain: media_player
  state: playing
  areas: Living
  attributes:
    device_class: tv
    volume_level: 0.2
- names: Kitchen speaker
  domain: media_player
  state: idle
  attributes:
    device_class: speaker
- names: Desk lamp
  domain: light
  state: 'off'
)";
const std::vector<std::string> kAllTools = {
  "homeassistant__GetLiveContext",
  "intent__HassTurnOn",
  "intent__HassTurnOff",
  "intent__HassSetPosition",
  "intent__HassStopMoving",
  "light__HassLightSet",
  "media_player__HassSetVolume",
  "media_player__HassMediaPlayerMute",
  "media_player__HassMediaPlayerUnmute"
};

McpCatalogSnapshot House(const std::vector<std::string>& tools = kAllTools) {
  McpCatalogSnapshot s;
  for (const auto& t : tools) {
    s.tools.push_back({.server_id = "ha", .tool_name = t});
  }
  Json::Value root;
  root["text"] = "Live Context: devices\n" + kHouse;
  s.latest_resource_states.push_back({"ha", "homeassistant://assist/context-snapshot", WriteJson(root)});
  return s;
}
// One-device GetLiveContext reply for the fresh read before every write.
Json::Value Fresh(const std::string& name) {
  const auto start = kHouse.find("- names: " + name);
  const auto end = kHouse.find("\n- names:", start + 1);
  Json::Value v;
  v["result"] = "Live Context: devices\n" + kHouse.substr(start, end == std::string::npos ? end : end - start + 1);
  return v;
}
McpResolvedAction Write(
  const ha::Catalog& catalog,
  const std::string& device,
  const std::string& name,
  const std::string& function,
  const std::string& parameter,
  const Json::Value& value
) {
  const auto read = ha::ResolveAction(Action(function, parameter, value, device), catalog);
  REQUIRE(read.status == AssistantStatus::OK);
  REQUIRE(read.tool_name == "homeassistant__GetLiveContext");
  const auto next = ha::Complete(Reply(read, Fresh(name))).continuations;
  REQUIRE(next.size() == 1);
  return next[0];
}
} // namespace

TEST_CASE("HA covers and televisions use learned categories and parameters", "[ha][devices]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(House());
  REQUIRE(c->status == AssistantStatus::OK);
  CHECK(c->devices.size() == 4); // the speaker has no model category
  const auto& curtain = c->devices.at("curtain_0");
  CHECK(curtain.features.at("open_percentage").value.asDouble() == 60);
  CHECK(curtain.features.at("open_percentage").writable);
  CHECK(curtain.features.at("open_set").value == "open");
  CHECK(c->devices.at("window_blind_0").features.at("open_set").value == "close");
  // Without current_position the cover has no position: the model is not offered one.
  CHECK(c->devices.at("window_blind_0").features.count("open_percentage") == 0);
  const auto& tv = c->devices.at("tv_0");
  CHECK(tv.features.at("on_off").value == true);
  CHECK(tv.features.at("volume_int").value.asDouble() == 20);
  CHECK(tv.features.at("mute").writable);
  CHECK(c->devices.at("light_0").features.at("light_colour_temp").writable);
  const auto scene = ha::RenderScene(*c).text;
  CHECK(scene.find("<d_curtain>0 Living curtain [Living]<p_rw_open_percentage><p_rw_open_set>") != std::string::npos);
  CHECK(scene.find("<d_window_blind>0 Kitchen blind<p_rw_open_set>\n") != std::string::npos);
  CHECK(scene.find("<d_tv>0 Living TV [Living]<p_rw_mute><p_rw_on_off><p_rw_volume_int>") != std::string::npos);
}

TEST_CASE("HA write-only parameters disappear without their tool; others become read-only", "[ha][devices]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(House({"homeassistant__GetLiveContext", "intent__HassTurnOn", "intent__HassTurnOff"}));
  CHECK(c->devices.at("tv_0").features.count("mute") == 0);
  CHECK(c->devices.at("light_0").features.count("light_colour_temp") == 0);
  CHECK_FALSE(c->devices.at("tv_0").features.at("volume_int").writable);
  // open_set also needs HassStopMoving.
  CHECK_FALSE(c->devices.at("curtain_0").features.at("open_set").writable);
  CHECK(c->devices.at("tv_0").features.at("on_off").writable);
}

TEST_CASE("HA cover writes map to position, open, close and stop intents", "[ha][devices]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(House());
  const auto position = Write(*c, "curtain_0", "Living curtain", "set_device_parameter", "open_percentage", "30%");
  CHECK(position.tool_name == "intent__HassSetPosition");
  CHECK(
    ParseJsonOrThrow(position.arguments_json)
    == ParseJsonOrThrow(R"({"name":"Living curtain","area":"Living","domain":["cover"],"position":30})")
  );
  for (const auto& [command, tool] : std::vector<std::pair<std::string, std::string>>{
         {"open", "intent__HassTurnOn"}, {"close", "intent__HassTurnOff"}, {"stop", "intent__HassStopMoving"}
       }) {
    INFO(command);
    CHECK(Write(*c, "curtain_0", "Living curtain", "set_device_parameter", "open_set", command).tool_name == tool);
  }
  CHECK(ha::ResolveAction(Action("set_device_parameter", "open_set", "ajar", "curtain_0"), *c).error == "BAD_VALUE");
  const auto relative = Write(*c, "curtain_0", "Living curtain", "relative_change", "open_percentage", "-");
  CHECK(ParseJsonOrThrow(relative.arguments_json)["position"] == 50);
}

TEST_CASE("HA television volume, mute and relative volume", "[ha][devices]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(House());
  const auto volume = Write(*c, "tv_0", "Living TV", "set_device_parameter", "volume_int", 35);
  CHECK(volume.tool_name == "media_player__HassSetVolume");
  CHECK(ParseJsonOrThrow(volume.arguments_json)["volume_level"] == 35);
  CHECK(
    Write(*c, "tv_0", "Living TV", "set_device_parameter", "mute", true).tool_name
    == "media_player__HassMediaPlayerMute"
  );
  CHECK(
    Write(*c, "tv_0", "Living TV", "set_device_parameter", "mute", false).tool_name
    == "media_player__HassMediaPlayerUnmute"
  );
  const auto louder = Write(*c, "tv_0", "Living TV", "relative_change", "volume_int", "+");
  CHECK(ParseJsonOrThrow(louder.arguments_json)["volume_level"] == 30);
  const auto read = ha::ResolveAction(Action("get_device_parameter", "volume_int", {}, "tv_0"), *c);
  const auto got = ha::Complete(Reply(read, Fresh("Living TV")));
  REQUIRE(got.results.size() == 1);
  CHECK(got.results[0].value_json == "20.0");
  // HA never reports mute state.
  const auto mute = ha::Complete(
    Reply(ha::ResolveAction(Action("get_device_parameter", "mute", {}, "tv_0"), *c), Fresh("Living TV"))
  );
  CHECK(mute.results.at(0).error == "UNKNOWN_PARAMETER");
}

TEST_CASE("HA colour temperature maps model percent onto kelvin, larger is colder", "[ha][devices]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(House());
  for (const auto& [percent, kelvin] : std::vector<std::pair<int, int>>{{0, 2700}, {50, 4600}, {100, 6500}}) {
    const auto write = Write(*c, "light_0", "Desk lamp", "set_device_parameter", "light_colour_temp", percent);
    CHECK(write.tool_name == "light__HassLightSet");
    CHECK(ParseJsonOrThrow(write.arguments_json)["temperature"] == kelvin);
  }
  // HA reports no colour temperature, so a warmer step has nothing to start from.
  const auto warmer = ha::ResolveAction(Action("relative_change", "light_colour_temp", "+", "light_0"), *c);
  CHECK(ha::Complete(Reply(warmer, Fresh("Desk lamp"))).results.at(0).error == "UNKNOWN_PARAMETER");
}
