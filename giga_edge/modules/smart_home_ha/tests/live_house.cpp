// Replies of a live Home Assistant 2026.11.2 (the demo integration, Russian names and areas), recorded through
// /api/mcp/assist.
#include "test_config.h"

#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/smart_home_ha/actions.h"
#include "giga_edge/modules/smart_home_ha/catalog.h"

#include <catch.hpp>

#include <string>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::ha_test;

namespace {
const std::vector<std::string> kTools = {
  "assist_satellite__HassBroadcast",
  "climate__HassClimateSetTemperature",
  "homeassistant__GetLiveContext",
  "intent__HassTurnOn",
  "intent__HassTurnOff",
  "intent__HassSetPosition",
  "intent__HassStopMoving",
  "intent__HassCancelAllTimers",
  "light__HassLightSet",
  "llm__GetDateTime",
  "media_player__HassMediaUnpause",
  "media_player__HassMediaPause",
  "media_player__HassMediaNext",
  "media_player__HassMediaPrevious",
  "media_player__HassSetVolume",
  "media_player__HassSetVolumeRelative",
  "media_player__HassMediaPlayerMute",
  "media_player__HassMediaPlayerUnmute",
};

// resources/read homeassistant://assist/context-snapshot
const char* const kContext = R"yaml(Live Context: An overview of the areas and the devices in this smart home:
- names: Ворота гаража
  domain: cover
  state: closed
  areas: Гараж
  attributes:
    device_class: garage
- names: Датчик влажности
  domain: sensor
  state: '54'
  areas: Спальня
  attributes:
    unit_of_measurement: '%'
    device_class: humidity
- names: Датчик температуры
  domain: sensor
  state: '15.6'
  areas: Гостиная
  attributes:
    unit_of_measurement: °C
    device_class: temperature
- names: Обогреватель
  domain: climate
  state: heat
  areas: Спальня
  attributes:
    current_temperature: 25.0
    temperature_unit: °C
    temperature: 20.0
- names: Розетка на кухне
  domain: switch
  state: 'on'
  areas: Кухня
- names: Свет в гостиной
  domain: light
  state: 'on'
  areas: Гостиная
  attributes:
    brightness: '180'
    brightness_pct: '71'
- names: Свет в спальне
  domain: light
  state: 'off'
  areas: Спальня
  attributes:
    brightness:
- names: Свет на кухне
  domain: light
  state: 'on'
  areas: Кухня
  attributes:
    brightness: '180'
    brightness_pct: '71'
- names: Телевизор
  domain: media_player
  state: playing
  attributes:
    volume_level: 1.0
    media_title: ♥♥ The Best Fireplace Video (3 hours)
    device_class: tv
- names: Шторы в гостиной
  domain: cover
  state: open
  areas: Гостиная
  attributes:
    current_position: '70'
- names: Шторы на кухне
  domain: cover
  state: open
  areas: Кухня
  attributes:
    current_position: '10'
)yaml";

// tools/call GetLiveContext with the name, domain and area of the kitchen light: content.
const char* const kKitchenLightRead
  = R"json([{"text": "{\"result\": \"Live Context: An overview of the areas and the devices in this smart home:\\n- names: Свет на кухне\\n  domain: light\\n  state: 'on'\\n  areas: Кухня\\n  attributes:\\n    brightness: '180'\\n    brightness_pct: '71'\\n\"}", "type": "text"}])json";

// tools/call intent__HassTurnOn with the name, area and domain of the kitchen light: content.
const char* const kKitchenLightOn
  = R"json([{"text": "{\"speech\": {}, \"response_type\": \"action_done\", \"data\": {\"success\": [{\"name\": \"Кухня\", \"type\": \"area\", \"id\": \"kukhnia\"}, {\"name\": \"Свет на кухне\", \"type\": \"entity\", \"id\": \"light.kitchen_lights\"}], \"failed\": []}}", "type": "text"}])json";

ha::CatalogPtr LiveCatalog(ha::CatalogBuilder& builder) {
  McpCatalogSnapshot s;
  for (const auto& t : kTools) {
    s.tools.push_back({.server_id = "ha", .tool_name = t});
  }
  // The way the mcp environment stores a resource (resource_state.cpp).
  Json::Value state;
  state["uri"] = "homeassistant://assist/context-snapshot";
  state["mimeType"] = "text/plain";
  state["text"] = kContext;
  s.latest_resource_states.push_back({"ha", "homeassistant://assist/context-snapshot", WriteJson(state)});
  return builder.Build(s);
}

std::string AliasOf(const ha::Catalog& catalog, const std::string& name) {
  for (const auto& [alias, device] : catalog.devices) {
    if (device.name == name) {
      return alias;
    }
  }
  FAIL("no device " << name);
  return {};
}

// The "turn on the kitchen light" write, before HA replies to HassTurnOn itself.
McpResolvedAction KitchenLightOn(const ha::Catalog& catalog) {
  const auto read = ha::ResolveAction(
    Action("set_device_parameter", "on_off", true, AliasOf(catalog, "Свет на кухне")), catalog
  );
  REQUIRE(read.tool_name == "homeassistant__GetLiveContext");
  const auto step = ha::Complete({.action = read, .content_json = kKitchenLightRead});
  REQUIRE(step.results.empty());
  REQUIRE(step.continuations.size() == 1);
  return step.continuations[0];
}

CompactToolResult Confirm(
  const McpResolvedAction& write, const std::string& success, const std::string& failed = "[]"
) {
  auto body = ParseJsonOrThrow(R"({"speech":{},"response_type":"action_done","data":{}})");
  body["data"]["success"] = ParseJsonOrThrow(success);
  body["data"]["failed"] = ParseJsonOrThrow(failed);
  const auto done = ha::Complete(Reply(write, body));
  REQUIRE(done.results.size() == 1);
  REQUIRE(done.continuations.empty());
  return done.results[0];
}
} // namespace

TEST_CASE("HA live demo house becomes the learned scene", "[ha][live_house]") {
  ha::CatalogBuilder builder;
  const auto catalog = LiveCatalog(builder);
  REQUIRE(catalog->status == AssistantStatus::OK);
  CHECK(catalog->issues.empty());
  CHECK(
    ha::RenderScene(*catalog).text
    == "<d_curtain>0 Шторы в гостиной [Гостиная]<p_rw_open_percentage><p_rw_open_set>\n"
       "<d_curtain>1 Шторы на кухне [Кухня]<p_rw_open_percentage><p_rw_open_set>\n"
       "<d_gate>0 Ворота гаража [Гараж]<p_rw_open_set>\n"
       "<d_hvac_heater>0 Обогреватель [Спальня]<p_rw_hvac_temp_set><p_rw_on_off><p_ro_temperature>\n"
       "<d_light>0 Свет в гостиной [Гостиная]<p_rw_light_brightness><p_rw_light_colour><p_rw_light_colour_temp><p_rw_on_off>\n"
       "<d_light>1 Свет в спальне [Спальня]<p_rw_light_brightness><p_rw_light_colour><p_rw_light_colour_temp><p_rw_on_off>\n"
       "<d_light>2 Свет на кухне [Кухня]<p_rw_light_brightness><p_rw_light_colour><p_rw_light_colour_temp><p_rw_on_off>\n"
       "<d_sensor_temp>0 Датчик влажности [Спальня]<p_ro_humidity>\n"
       "<d_sensor_temp>1 Датчик температуры [Гостиная]<p_ro_temperature>\n"
       "<d_socket>0 Розетка на кухне [Кухня]<p_rw_on_off>\n"
       "<d_tv>0 Телевизор<p_rw_mute><p_rw_on_off><p_rw_volume_int>\n"
  );
}

TEST_CASE("HA confirms a write to a device with an area as [area, entity]", "[ha][live_house]") {
  ha::CatalogBuilder builder;
  const auto catalog = LiveCatalog(builder);
  const auto write = KitchenLightOn(*catalog);
  CHECK(write.tool_name == "intent__HassTurnOn");
  CHECK(
    ParseJsonOrThrow(write.arguments_json)
    == ParseJsonOrThrow(R"({"name":"Свет на кухне","area":"Кухня","domain":["light"]})")
  );
  const auto done = ha::Complete({.action = write, .content_json = kKitchenLightOn});
  REQUIRE(done.results.size() == 1);
  CHECK_FALSE(done.results[0].is_error);
  CHECK(done.results[0].call_id == write.call_id);
}

TEST_CASE("HA write is unconfirmed unless exactly the target entity succeeded", "[ha][live_house]") {
  ha::CatalogBuilder builder;
  const auto catalog = LiveCatalog(builder);
  const auto write = KitchenLightOn(*catalog);
  const std::string area = R"({"name":"Кухня","type":"area","id":"kukhnia"})";
  const std::string entity = R"({"name":"Свет на кухне","type":"entity","id":"light.kitchen_lights"})";
  CHECK_FALSE(Confirm(write, "[" + entity + "," + area + "]").is_error);
  for (const auto& success :
       {"[" + area + "]",
        "[" + area + R"(,{"name":"Свет в гостиной","type":"entity","id":"light.ceiling_lights"}])",
        "[" + area + "," + entity + R"(,{"name":"Свет на кухне","type":"entity","id":"light.kitchen_2"}])",
        "[" + area + R"(,{"name":"Свет на кухне","type":"entity","id":""}])",
        "[" + area + R"(,{"name":"Свет на кухне","type":"device","id":"light.kitchen_lights"}])"}) {
    INFO(success);
    CHECK(Confirm(write, success).is_error);
  }
  // The target is confirmed, but something of what was addressed failed (this is how HA replies, for example, to
  // stop on a gate).
  CHECK(Confirm(
          write,
          "[" + area + "," + entity + "]",
          R"([{"name":"Свет в гостиной","type":"entity","id":"light.ceiling_lights"}])"
  )
          .is_error);
}
