#include "test_config.h"

#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/smart_home_ha/actions.h"

#include <catch.hpp>
using namespace giga_edge;
using namespace giga_edge::ha_test;
namespace {
const std::string scene_context = "Live Context: devices\n- names: Good night\n  domain: scene\n  state: unknown\n";
// A house as HA 2026.11 lists it: the scene is an entity of the context, a script is only a tool with a title.
McpCatalogSnapshot House() {
  auto s = Snapshot();
  Json::Value root;
  root["text"] = scene_context + "- names: Bedroom lamp\n  domain: light\n  state: 'on'\n  areas: Bedroom\n";
  s.latest_resource_states.at(0).data_json = WriteJson(root);
  s.tools.push_back(
    {.server_id = "ha",
     .tool_name = "script__good_morning",
     .input_schema_json = R"({"type":"object","properties":{}})",
     .title = "Good morning"}
  );
  // Not offered: a script that needs an argument, and a tool the server gave no title.
  s.tools.push_back(
    {.server_id = "ha",
     .tool_name = "script__water",
     .input_schema_json = R"({"type":"object","properties":{"zone":{}},"required":["zone"]})",
     .title = "Water"}
  );
  s.tools.push_back({.server_id = "ha", .tool_name = "script__untitled"});
  return s;
}
ha::CatalogPtr Scenarios() {
  ha::CatalogBuilder b;
  return b.Build(House());
}
Json::Value Confirmed(const std::string& name) {
  auto reply = Success();
  reply["data"]["success"][0]["name"] = name;
  return reply;
}
Json::Value SceneContext() {
  Json::Value v;
  v["result"] = scene_context;
  return v;
}
} // namespace

TEST_CASE("HA scenes and scripts are devices that are switched on, named with the prefix of the config", "[ha]") {
  const auto c = Scenarios();
  REQUIRE(c->status == AssistantStatus::OK);
  const auto scene = ha::RenderScene(*c, {.scenario_prefix = "Scenario"}).text;
  CHECK(scene.find("<d_socket>0 Scenario Good night<p_rw_on_off>\n") != std::string::npos);
  CHECK(scene.find("<d_socket>1 Scenario Good morning<p_rw_on_off>\n") != std::string::npos);
  CHECK(scene.find("<d_light>0 Bedroom lamp [Bedroom]") != std::string::npos);
  CHECK(scene.find("<d_socket>2") == std::string::npos);
  CHECK(ha::RenderScene(*c).text.find("<d_socket>0 Good night<p_rw_on_off>\n") != std::string::npos);

  // A scene is activated by the on intent: without it the scene has nothing to offer, a script still has its tool.
  auto s = House();
  s.tools.erase(s.tools.begin());
  ha::CatalogBuilder b;
  const auto without_intent = ha::RenderScene(*b.Build(s)).text;
  CHECK(without_intent.find("Good night") == std::string::npos);
  CHECK(without_intent.find("<d_socket>0 Good morning<p_rw_on_off>\n") != std::string::npos);
}
TEST_CASE("HA activates a scene by the on intent and has nothing to switch off", "[ha]") {
  const auto c = Scenarios();
  auto read = ha::ResolveAction(Action("set_device_parameter", "on_off", true, "socket_0"), *c);
  REQUIRE(read.status == AssistantStatus::OK);
  CHECK(read.tool_name == "homeassistant__GetLiveContext");
  CHECK(ParseJsonOrThrow(read.arguments_json) == ParseJsonOrThrow(R"({"name":"Good night","domain":"scene"})"));
  const auto write = ha::Complete(Reply(read, SceneContext())).continuations.at(0);
  CHECK(write.tool_name == "intent__HassTurnOn");
  CHECK(ParseJsonOrThrow(write.arguments_json) == ParseJsonOrThrow(R"({"name":"Good night","domain":["scene"]})"));
  CHECK_FALSE(ha::Complete(Reply(write, Confirmed("Good night"))).results.at(0).is_error);

  // Off: once the scene is found the request is done, without a call HA would reject.
  read = ha::ResolveAction(Action("set_device_parameter", "on_off", false, "socket_0"), *c);
  REQUIRE(read.status == AssistantStatus::OK);
  const auto off = ha::Complete(Reply(read, SceneContext()));
  CHECK(off.continuations.empty());
  REQUIRE(off.results.size() == 1);
  CHECK_FALSE(off.results[0].is_error);
}
TEST_CASE("HA runs a script by its own tool and stops it by the off intent", "[ha]") {
  const auto c = Scenarios();
  const auto run = ha::ResolveAction(Action("set_device_parameter", "on_off", true, "socket_1"), *c);
  REQUIRE(run.status == AssistantStatus::OK);
  CHECK(run.tool_name == "script__good_morning");
  CHECK(ParseJsonOrThrow(run.arguments_json) == Json::Value(Json::objectValue));
  // The context does not list a script, so there is no read before it.
  CHECK_FALSE(ha::NeedsContinuation(run));
  Json::Value reply;
  reply["result"] = Json::Value(Json::objectValue);
  CHECK_FALSE(ha::Complete(Reply(run, reply)).results.at(0).is_error);
  CHECK(ha::Complete(Reply(run, Json::Value(Json::objectValue))).results.at(0).is_error);
  auto failed = Reply(run, reply);
  failed.is_error = true;
  failed.error = "Error calling tool: the script failed";
  CHECK(ha::Complete(failed).results.at(0).error == "DEVICE_UNREACHABLE");

  const auto stop = ha::ResolveAction(Action("set_device_parameter", "on_off", false, "socket_1"), *c);
  REQUIRE(stop.status == AssistantStatus::OK);
  CHECK(stop.tool_name == "intent__HassTurnOff");
  CHECK(ParseJsonOrThrow(stop.arguments_json) == ParseJsonOrThrow(R"({"name":"Good morning","domain":["script"]})"));
  CHECK_FALSE(ha::Complete(Reply(stop, Confirmed("Good morning"))).results.at(0).is_error);
  CHECK(ha::Complete(Reply(stop, Confirmed("Another script"))).results.at(0).is_error);

  const auto get = ha::ResolveAction(Action("get_device_parameter", "on_off", {}, "socket_1"), *c);
  CHECK(get.status == AssistantStatus::ERROR);
  CHECK(get.error == "UNKNOWN_PARAMETER");
}

TEST_CASE("HA online is shown for a scene, which has an entity, and not for a script", "[ha][online]") {
  ha::CatalogBuilder builder;
  const auto catalog = builder.Build(House(), {.include_online = true});
  const auto scene = ha::RenderScene(*catalog).text;
  CHECK(scene.find("<d_socket>0 Good night<p_rw_on_off><p_ro_online>\n") != std::string::npos);
  CHECK(scene.find("<d_socket>1 Good morning<p_rw_on_off>\n") != std::string::npos);
  const auto read = ha::ResolveAction(Action("get_device_parameter", "online", {}, "socket_0"), *catalog);
  REQUIRE(read.tool_name == "homeassistant__GetLiveContext");
  const auto done = ha::Complete(Reply(read, SceneContext()));
  REQUIRE(done.results.size() == 1);
  CHECK(done.results[0].value_json == "true");
  CHECK(
    ha::ResolveAction(Action("get_device_parameter", "online", {}, "socket_1"), *catalog).error == "UNKNOWN_PARAMETER"
  );

  // Without the on intent a scene has nothing to offer: online alone does not keep it.
  auto s = House();
  s.tools.erase(s.tools.begin());
  ha::CatalogBuilder without_intent;
  CHECK(
    ha::RenderScene(*without_intent.Build(s, {.include_online = true})).text.find("Good night") == std::string::npos
  );
}
