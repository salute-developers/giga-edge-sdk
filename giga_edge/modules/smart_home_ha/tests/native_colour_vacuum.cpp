#include "test_config.h"

#include <catch.hpp>

#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <tuple>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::ha_test;

namespace {
// MCP replies recorded from Home Assistant 2026.11.2 with native demo entities and an on/off template light.
Json::Value Recordings() {
  std::ifstream file(
    std::string(GIGA_EDGE_TEST_SOURCE_DIR) + "/giga_edge/modules/smart_home_ha/tests/data/native_colour_vacuum.json"
  );
  REQUIRE(file.is_open());
  return ParseJsonOrThrow(std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()));
}
ha::CatalogPtr NativeCatalog(const Json::Value& recordings) {
  McpCatalogSnapshot snapshot;
  for (const auto& tool : recordings["tools"]) {
    snapshot.tools.push_back({.server_id = "ha", .tool_name = tool["name"].asString()});
  }
  snapshot.latest_resource_states.push_back(
    {"ha", "homeassistant://assist/context-snapshot", WriteJson(recordings["context"])}
  );
  ha::CatalogBuilder builder;
  const auto catalog = builder.Build(snapshot);
  REQUIRE(catalog->status == AssistantStatus::OK);
  return catalog;
}
std::string AliasOf(const ha::Catalog& catalog, const std::string& name) {
  for (const auto& [alias, device] : catalog.devices) {
    if (device.name == name) {
      return alias;
    }
  }
  FAIL("Missing recorded device: " << name);
  return {};
}
ha::Completion CompleteRecorded(const McpResolvedAction& action, const Json::Value& recording) {
  const auto& result = recording["result"];
  return ha::Complete(
    {.action = action,
     .content_json = WriteJson(result["content"]),
     .is_error = result["isError"].asBool(),
     .error = result["isError"].asBool() ? result["content"][0]["text"].asString() : ""}
  );
}
McpResolvedAction WriteRecorded(
  const Json::Value& recordings, const Json::Value& recording, const std::string& parameter, const Json::Value& value
) {
  const auto catalog = NativeCatalog(recordings);
  const auto name = recording["arguments"]["name"].asString();
  const auto read = ha::ResolveAction(
    Action("set_device_parameter", parameter, value, AliasOf(*catalog, name)), *catalog
  );
  REQUIRE(read.status == AssistantStatus::OK);
  const auto& read_recording = recordings["reads"][name];
  REQUIRE(read.tool_name == read_recording["tool"].asString());
  REQUIRE(ParseJsonOrThrow(read.arguments_json) == read_recording["arguments"]);
  auto step = CompleteRecorded(read, read_recording);
  REQUIRE(step.results.empty());
  REQUIRE(step.continuations.size() == 1);
  return step.continuations[0];
}
void ConfirmRecorded(const McpResolvedAction& action, const Json::Value& recording) {
  const auto done = CompleteRecorded(action, recording);
  REQUIRE(done.results.size() == 1);
  CHECK_FALSE(done.results[0].is_error);
  CHECK(done.continuations.empty());
  CHECK(done.results[0].call_id == action.call_id);
}
} // namespace

TEST_CASE("HA native light and vacuum tool schemas match the adapter scene", "[ha][native]") {
  const auto recordings = Recordings();
  const auto catalog = NativeCatalog(recordings);
  const auto scene = ha::RenderScene(*catalog).text;
  CHECK(scene.find("<d_vacuum_cleaner>") != std::string::npos);
  CHECK(
    scene.find("<p_rw_light_brightness><p_rw_light_colour><p_rw_light_colour_temp><p_rw_on_off>") != std::string::npos
  );
  const auto& vacuum = catalog->devices.at(AliasOf(*catalog, "Робот-пылесос"));
  CHECK(vacuum.features.size() == 2);
  CHECK(vacuum.features.at("on_off").writable);
  CHECK(vacuum.features.at("vacuum_cleaner_status").writable);
  for (const auto* name : {"light__HassLightSet", "vacuum__HassVacuumStart", "vacuum__HassVacuumReturnToBase"}) {
    const Json::Value* found = nullptr;
    for (const auto& tool : recordings["tools"]) {
      if (tool["name"] == name) {
        found = &tool;
      }
    }
    REQUIRE(found);
    const auto& schema = (*found)["inputSchema"]["properties"];
    CHECK(schema["name"]["type"] == "string");
    CHECK(schema["area"]["type"] == "string");
    CHECK(schema["domain"]["type"] == "array");
    CHECK(schema["domain"]["items"]["enum"][0] == (std::string(name).find("vacuum") == 0 ? "vacuum" : "light"));
  }
}

TEST_CASE("HA native vacuum start and return accept the resolved target and confirm its entity", "[ha][native]") {
  const auto recordings = Recordings();
  for (const auto& record : recordings["vacuum_writes"]) {
    const bool start = record["tool"] == "vacuum__HassVacuumStart";
    for (const auto& [parameter, value] : std::vector<std::pair<std::string, Json::Value>>{
           {"on_off", start}, {"vacuum_cleaner_status", start ? "cleaning" : "returning_to_dock"}
         }) {
      const auto write = WriteRecorded(recordings, record, parameter, value);
      CHECK(write.tool_name == record["tool"].asString());
      CHECK(ParseJsonOrThrow(write.arguments_json) == record["arguments"]);
      ConfirmRecorded(write, record);
    }
  }
}

TEST_CASE("HA native vacuum context exposes states without capabilities or battery", "[ha][native]") {
  const auto recordings = Recordings();
  const auto catalog = NativeCatalog(recordings);
  const auto alias = AliasOf(*catalog, "Робот-пылесос");
  for (const auto& [state, status, on] : std::vector<std::tuple<std::string, Json::Value, Json::Value>>{
         {"docked", "docked", false},
         {"cleaning", "cleaning", true},
         {"returning", "returning_to_dock", false},
         {"paused", "pause", false},
         {"idle", {}, false},
         {"error", {}, {}}
       }) {
    CAPTURE(state);
    for (const auto& [parameter, expected] : std::vector<std::pair<std::string, Json::Value>>{
           {"on_off", on}, {"vacuum_cleaner_status", status}, {"battery_percentage", {}}
         }) {
      const auto read = ha::ResolveAction(Action("get_device_parameter", parameter, {}, alias), *catalog);
      if (parameter == "battery_percentage") {
        CHECK(read.error == "UNKNOWN_PARAMETER");
        continue;
      }
      const auto done = CompleteRecorded(read, recordings["vacuum_states"][state]);
      REQUIRE(done.results.size() == 1);
      CHECK(done.results[0].is_error == expected.isNull());
      if (expected.isNull()) {
        CHECK(done.results[0].error == "UNKNOWN_PARAMETER");
      } else {
        CHECK(ParseJsonOrThrow(done.results[0].value_json) == expected);
      }
    }
  }
}

TEST_CASE("HA native vacuum capability failures reach the model as errors", "[ha][native]") {
  const auto recordings = Recordings();
  for (const auto& record : recordings["vacuum_errors"]) {
    const auto write = WriteRecorded(recordings, record, "on_off", record["tool"] == "vacuum__HassVacuumStart");
    CHECK(write.tool_name == record["tool"].asString());
    CHECK(ParseJsonOrThrow(write.arguments_json) == record["arguments"]);
    const auto done = CompleteRecorded(write, record);
    REQUIRE(done.results.size() == 1);
    CHECK(done.results[0].is_error);
    CHECK(done.results[0].error == "DEVICE_UNREACHABLE");
    CHECK(done.continuations.empty());
  }
}

TEST_CASE("HA native named colours accept the adapter palette and confirm writes", "[ha][native]") {
  const auto recordings = Recordings();
  REQUIRE(recordings["colours"].size() == 20);
  for (const auto& record : recordings["colours"]) {
    const auto& rgb = record["rgb"];
    const auto value = rgb[0].asString() + ";" + rgb[1].asString() + ";" + rgb[2].asString();
    CAPTURE(value);
    const auto write = WriteRecorded(recordings, record, "light_colour", value);
    CHECK(write.tool_name == record["tool"].asString());
    auto expected = record["arguments"];
    const std::map<std::string, std::string> aliases = {{"cyan", "aqua"}, {"magenta", "fuchsia"}, {"grey", "gray"}};
    const auto alias = aliases.find(expected["color"].asString());
    if (alias != aliases.end()) {
      expected["color"] = alias->second;
    }
    CHECK(ParseJsonOrThrow(write.arguments_json) == expected);
    ConfirmRecorded(write, record);
  }
}

TEST_CASE("HA native white only light acknowledges colour without reporting its capability", "[ha][native]") {
  const auto recordings = Recordings();
  const auto& record = recordings["white_write"];
  const auto write = WriteRecorded(recordings, record, "light_colour", "255;18;0");
  CHECK(write.tool_name == "light__HassLightSet");
  CHECK(ParseJsonOrThrow(write.arguments_json) == record["arguments"]);
  ConfirmRecorded(write, record);
}

TEST_CASE("HA native light temperatures retain kelvin conversion and confirmation", "[ha][native]") {
  const auto recordings = Recordings();
  const std::vector<int> percent = {0, 50, 100};
  for (Json::ArrayIndex i = 0; i < 3; ++i) {
    const auto& record = recordings["temperatures"][i];
    const auto write = WriteRecorded(recordings, record, "light_colour_temp", percent[i]);
    CHECK(write.tool_name == record["tool"].asString());
    CHECK(ParseJsonOrThrow(write.arguments_json) == record["arguments"]);
    ConfirmRecorded(write, record);
  }
}
