#include "test_config.h"

#include <catch.hpp>

#include <tuple>

using namespace giga_edge;
using namespace giga_edge::ha_test;

namespace {
Json::Value VacuumContext(const std::string& state) {
  Json::Value result;
  result["result"] = "Live Context: devices\n- names: Robot vacuum\n  domain: vacuum\n  state: '" + state
    + "'\n  areas: Hall\n";
  return result;
}
ha::CatalogPtr VacuumCatalog(const std::string& state = "docked", const std::string& missing_tool = "") {
  auto snapshot = Snapshot();
  for (const auto* tool : {"vacuum__HassVacuumStart", "vacuum__HassVacuumReturnToBase"}) {
    if (tool != missing_tool) {
      snapshot.tools.push_back({.server_id = "ha", .tool_name = tool});
    }
  }
  snapshot.latest_resource_states[0].data_json = WriteJson(VacuumContext(state));
  ha::CatalogBuilder builder;
  return builder.Build(snapshot);
}
AssistantResult VacuumAction(const std::string& function, const std::string& parameter, Json::Value value = {}) {
  return Action(function, parameter, std::move(value), "vacuum_cleaner_0");
}
} // namespace

TEST_CASE("HA vacuum scene offers only supported control and state", "[ha][vacuum]") {
  const auto catalog = VacuumCatalog();
  REQUIRE(catalog->devices.size() == 1);
  CHECK(
    ha::RenderScene(*catalog).text
    == "<d_vacuum_cleaner>0 Robot vacuum [Hall]<p_rw_on_off><p_rw_vacuum_cleaner_status>\n"
  );
  const auto& device = catalog->devices.at("vacuum_cleaner_0");
  CHECK(device.features.at("on_off").value == false);
  CHECK(device.features.at("vacuum_cleaner_status").value == "docked");
  CHECK(device.features.size() == 2);
  CHECK(
    ha::ResolveAction(VacuumAction("get_device_parameter", "battery_percentage"), *catalog).error == "UNKNOWN_PARAMETER"
  );
}

TEST_CASE("HA vacuum states translate only to the model vocabulary using fresh reads", "[ha][vacuum]") {
  for (const auto& [state, status, on] : std::vector<std::tuple<std::string, Json::Value, Json::Value>>{
         {"cleaning", "cleaning", true},
         {"docked", "docked", false},
         {"returning", "returning_to_dock", false},
         {"paused", "pause", false},
         {"idle", {}, false},
         {"error", {}, {}},
         {"unknown", {}, {}},
         {"unavailable", {}, {}},
         {"unexpected", {}, {}}
       }) {
    CAPTURE(state);
    const auto catalog = VacuumCatalog(state);
    const auto& device = catalog->devices.at("vacuum_cleaner_0");
    CHECK(device.features.at("on_off").value == on);
    CHECK(device.features.at("vacuum_cleaner_status").value == status);
    const auto read = ha::ResolveAction(
      VacuumAction("get_device_parameter", "vacuum_cleaner_status"), *VacuumCatalog()
    );
    const auto done = ha::Complete(Reply(read, VacuumContext(state)));
    REQUIRE(done.results.size() == 1);
    CHECK(done.results[0].is_error == status.isNull());
    if (!status.isNull()) {
      CHECK(ParseJsonOrThrow(done.results[0].value_json) == status);
    }
  }
}

TEST_CASE("HA vacuum controls use native start and return intents after a target read", "[ha][vacuum]") {
  for (const auto& [parameter, value, tool] : std::vector<std::tuple<std::string, Json::Value, std::string>>{
         {"on_off", true, "vacuum__HassVacuumStart"},
         {"on_off", false, "vacuum__HassVacuumReturnToBase"},
         {"vacuum_cleaner_status", "cleaning", "vacuum__HassVacuumStart"},
         {"vacuum_cleaner_status", "docked", "vacuum__HassVacuumReturnToBase"},
         {"vacuum_cleaner_status", "returning_to_dock", "vacuum__HassVacuumReturnToBase"}
       }) {
    CAPTURE(parameter, value, tool);
    const auto read = ha::ResolveAction(VacuumAction("set_device_parameter", parameter, value), *VacuumCatalog());
    REQUIRE(read.tool_name == "homeassistant__GetLiveContext");
    CHECK(
      ParseJsonOrThrow(read.arguments_json)
      == ParseJsonOrThrow(R"({"name":"Robot vacuum","area":"Hall","domain":"vacuum"})")
    );
    const auto step = ha::Complete(Reply(read, VacuumContext("docked")));
    REQUIRE(step.continuations.size() == 1);
    const auto& write = step.continuations[0];
    CHECK(write.tool_name == tool);
    CHECK(
      ParseJsonOrThrow(write.arguments_json)
      == ParseJsonOrThrow(R"({"name":"Robot vacuum","area":"Hall","domain":["vacuum"]})")
    );
    auto success = Success();
    success["data"]["success"][0]["name"] = "Robot vacuum";
    success["data"]["success"][0]["id"] = "vacuum.robot";
    const auto done = ha::Complete(Reply(write, success));
    REQUIRE(done.results.size() == 1);
    CHECK_FALSE(done.results[0].is_error);
    CHECK(done.results[0].call_id == read.call_id);
  }
}

TEST_CASE("HA generic on off tools do not make vacuum control writable", "[ha][vacuum]") {
  for (const auto* missing : {"vacuum__HassVacuumStart", "vacuum__HassVacuumReturnToBase"}) {
    const auto catalog = VacuumCatalog("cleaning", missing);
    CHECK(
      ha::RenderScene(*catalog).text
      == "<d_vacuum_cleaner>0 Robot vacuum [Hall]<p_ro_on_off><p_ro_vacuum_cleaner_status>\n"
    );
    CHECK(
      ha::ResolveAction(VacuumAction("set_device_parameter", "on_off", true), *catalog).error == "PARAMETER_READ_ONLY"
    );
  }
}

TEST_CASE("HA unsupported vacuum writes and relative changes fail before MCP", "[ha][vacuum]") {
  const auto catalog = VacuumCatalog();
  for (const auto& value : std::vector<Json::Value>{"pause", "paused", "returning", "idle", "error", "", true, 1, {}}) {
    CAPTURE(value);
    const auto action = ha::ResolveAction(
      VacuumAction("set_device_parameter", "vacuum_cleaner_status", value), *catalog
    );
    CHECK(action.error == "BAD_VALUE");
    CHECK(action.kind == McpResolvedActionKind::NONE);
  }
  for (const auto* parameter :
       {"vacuum_cleaner_program", "vacuum_cleaner_cleaning_type", "child_lock", "battery_percentage"}) {
    CHECK(ha::ResolveAction(VacuumAction("set_device_parameter", parameter, 1), *catalog).error == "UNKNOWN_PARAMETER");
  }
  for (const auto* parameter : {"on_off", "vacuum_cleaner_status"}) {
    CHECK(ha::ResolveAction(VacuumAction("relative_change", parameter, "+"), *catalog).error == "RELATIVE_NOT_ALLOWED");
  }
}
