#include "giga_edge/modules/smart_home_ha/catalog.h"

#include "giga_edge/modules/assistant_core/json.h"

#include <catch.hpp>
using namespace giga_edge;
namespace {
McpCatalogSnapshot Snapshot(const std::string& yaml, const std::string& server = "ha") {
  McpCatalogSnapshot out;
  for (const auto* name :
       {"intent__HassTurnOn",
        "intent__HassTurnOff",
        "light__HassLightSet",
        "climate__HassClimateSetTemperature",
        "homeassistant__GetLiveContext"}) {
    out.tools.push_back({.server_id = server, .tool_name = name});
  }
  Json::Value contents(Json::arrayValue), entry;
  entry["uri"] = "homeassistant://assist/context-snapshot";
  entry["mimeType"] = "text/plain";
  entry["text"] = "Live Context: An overview of the areas and the devices in this smart home:\n" + yaml;
  contents.append(entry);
  Json::Value root;
  root = entry; // runtime BuildResourceContentState envelope
  out.latest_resource_states.push_back({server, entry["uri"].asString(), WriteJson(root)});
  return out;
}
const std::string lamp = R"(- names: Bedroom lamp
  domain: light
  state: 'on'
  areas: Bedroom
  attributes:
    brightness: '128'
    brightness_pct: '50'
)";
} // namespace
TEST_CASE("HA snapshot preserves target and translates brightness to model percent", "[ha]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(Snapshot(lamp));
  REQUIRE(c->status == AssistantStatus::OK);
  REQUIRE(c->devices.size() == 1);
  const auto& d = c->devices.at("light_0");
  CHECK(d.name == "Bedroom lamp");
  CHECK(d.area == "Bedroom");
  CHECK(d.features.at("on_off").value == true);
  CHECK(d.features.at("light_brightness").value.asDouble() == 50.0);
  CHECK(d.features.at("light_brightness").writable);
  const auto scene = ha::RenderScene(*c);
  CHECK(scene.text.find("<d_light>0 Bedroom lamp [Bedroom]") != std::string::npos);
  CHECK(scene.text.find("<p_rw_light_brightness>") != std::string::npos);
}
TEST_CASE("HA aliases survive disappearance and never address a different target", "[ha]") {
  ha::CatalogBuilder b;
  const auto first = b.Build(Snapshot(lamp));
  REQUIRE(first->devices.count("light_0") == 1);
  CHECK(b.Build({})->devices.empty());
  auto changed = lamp;
  changed.replace(changed.find("Bedroom lamp"), 12, "Ceiling lamp");
  const auto next = b.Build(Snapshot(changed));
  REQUIRE(next->devices.count("light_1") == 1);
  CHECK(next->devices.count("light_0") == 0);
  CHECK(b.Build(Snapshot(lamp))->devices.count("light_0") == 1);
}
TEST_CASE("HA duplicates and unsupported domains cannot enter scene as targets", "[ha]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(Snapshot(lamp + lamp + R"(- names: Front door
  domain: lock
  state: unlocked
)"));
  CHECK(c->devices.empty());
  CHECK_FALSE(c->issues.empty());
}
TEST_CASE("HA device without an area stays ambiguous after a same-named device with an area is accepted", "[ha]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(Snapshot(R"(- names: Лампа
  domain: light
  state: 'on'
  areas: Кухня
- names: Лампа
  domain: light
  state: 'off'
)"));
  // Without an area HA selects both by the name, so only the one with the area is addressable.
  REQUIRE(c->devices.size() == 1);
  CHECK(c->devices.at("light_0").area == "Кухня");
}
TEST_CASE("HA light that is off has zero brightness, so a relative step can start from it", "[ha]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(Snapshot(R"(- names: Bedroom lamp
  domain: light
  state: 'off'
  areas: Bedroom
  attributes:
    brightness:
- names: Hall lamp
  domain: light
  state: unavailable
  attributes:
    brightness:
- names: Porch lamp
  domain: light
  state: 'off'
)"));
  REQUIRE(c->devices.size() == 3);
  const auto& off = c->devices.at("light_0").features.at("light_brightness").value;
  CHECK(off.isNumeric());
  CHECK(off.asDouble() == 0.0);
  CHECK(c->devices.at("light_1").features.at("light_brightness").value.isNull());
  // Without dimming HA does not return the brightness key: the value is not made up.
  CHECK(c->devices.at("light_2").features.at("light_brightness").value.isNull());
}
TEST_CASE("HA unavailable devices have no fabricated values and reads have learned sensor categories", "[ha]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(Snapshot(R"(- names: Heating
  domain: climate
  state: unavailable
- names: Bedroom temperature
  domain: sensor
  state: '22.5'
  attributes:
    device_class: temperature
    unit_of_measurement: C
)"));
  REQUIRE(c->devices.size() == 2);
  CHECK_FALSE(c->devices.at("hvac_heater_0").online);
  CHECK(c->devices.at("hvac_heater_0").features.at("hvac_temp_set").value.isNull());
  CHECK(c->devices.at("sensor_temp_0").features.at("temperature").value.asDouble() == 22.5);
}
TEST_CASE("HA malformed and oversize snapshots fail closed instead of leaving previous targets", "[ha]") {
  ha::CatalogBuilder b;
  REQUIRE(b.Build(Snapshot(lamp))->devices.size() == 1);
  CHECK(b.Build(Snapshot("- names: [\n"))->status == AssistantStatus::ERROR);
  ha::CatalogParams p;
  p.max_context_bytes = 10;
  CHECK(b.Build(Snapshot(lamp), p)->status == AssistantStatus::ERROR);
}
TEST_CASE("HA scene budget rejects full scene instead of clipping a device identifier", "[ha]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(Snapshot(lamp));
  const auto scene = ha::RenderScene(*c, {.max_chars = 8});
  CHECK(scene.status == AssistantStatus::ERROR);
  CHECK(scene.text.empty());
}
TEST_CASE("HA tool read envelope is decoded without treating unavailable as zero", "[ha]") {
  Json::Value root;
  root["result"]
    = "Live Context: An overview of the areas and the devices in this smart home:\n"
      "- names: Plug\n  domain: switch\n  state: unavailable\n";
  Json::Value block, blocks(Json::arrayValue);
  block["type"] = "text";
  block["text"] = WriteJson(root);
  blocks.append(block);
  const auto devices = ha::ParseContext(blocks, "ha");
  REQUIRE(devices.size() == 1);
  CHECK_FALSE(devices.front().online);
  CHECK(devices.front().features.at("on_off").value.isNull());
}

TEST_CASE("HA primary names cannot also select another device alias", "[ha]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(Snapshot(R"(- names: ЛАМПА
  domain: light
  state: 'on'
  areas: Bedroom
- names: Ceiling, лампа
  domain: light
  state: 'on'
  areas: Bedroom
)"));
  REQUIRE(c->devices.size() == 1);
  CHECK(c->devices.begin()->second.name == "Ceiling");
  CHECK_FALSE(c->issues.empty());
}
TEST_CASE("HA area aliases and missing areas cannot conceal ambiguous targets", "[ha]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(Snapshot(R"(- names: Lamp
  domain: light
  state: 'on'
  areas: Bedroom
- names: Lamp
  domain: light
  state: 'on'
  areas: Upstairs, bedroom
- names: Lamp
  domain: light
  state: 'on'
)"));
  REQUIRE(c->devices.size() == 1);
  CHECK(c->devices.begin()->second.area == "Upstairs");
}
TEST_CASE("HA same name in distinct areas remains addressable", "[ha]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(Snapshot(R"(- names: Lamp
  domain: light
  state: 'on'
  areas: Bedroom
- names: Lamp
  domain: light
  state: 'on'
  areas: Kitchen
)"));
  CHECK(c->devices.size() == 2);
}

TEST_CASE("HA malformed context entry is skipped and reported, not the whole house", "[ha]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(Snapshot(lamp + "- domain: light\n  state: 'on'\n"));
  REQUIRE(c->status == AssistantStatus::OK);
  CHECK(c->devices.size() == 1);
  REQUIRE(c->issues.size() == 1);
  CHECK(c->issues[0].find("no target name") != std::string::npos);
  // Reads must stay strict: a skipped row could hide a second match.
  CHECK_THROWS(ha::ParseContext(Json::Value("- domain: light\n  state: 'on'\n"), "ha"));
}

TEST_CASE("HA cover that changes its class gets a new alias and the scene still renders", "[ha]") {
  ha::CatalogBuilder b;
  const std::string cover = "- names: Garage door\n  domain: cover\n  state: closed\n  attributes:\n    device_class: ";
  REQUIRE(b.Build(Snapshot(cover + "garage\n"))->devices.count("gate_0") == 1);
  const auto c = b.Build(Snapshot(cover + "curtain\n"));
  REQUIRE(c->devices.count("curtain_0") == 1);
  CHECK_NOTHROW(ha::RenderScene(*c));
}

TEST_CASE("HA climate is on unless its mode is off, and its humidity is the target", "[ha]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(Snapshot(R"(- names: Тёплый пол Ванная
  domain: climate
  state: heat
  areas: Ванная
  attributes:
    current_temperature: 33.4
    temperature: 33.0
- names: Кондиционер
  domain: climate
  state: 'off'
  attributes:
    temperature: 22
    humidity: 45
- names: Термостат
  domain: climate
  state: unavailable
)"));
  REQUIRE(c->devices.size() == 3);
  // HA tells nothing about what a climate heats: an underfloor heating is a heater as any other climate.
  const auto& floor = c->devices.at("hvac_heater_0");
  CHECK(floor.features.at("on_off").value == true);
  CHECK(floor.features.at("on_off").writable);
  const auto& ac = c->devices.at("hvac_heater_1");
  CHECK(ac.features.at("on_off").value == false);
  // HA's humidity of a climate is its target, not a measurement.
  CHECK(ac.features.count("humidity") == 0);
  CHECK(ac.features.at("hvac_humidity_set").value.asDouble() == 45);
  CHECK_FALSE(ac.features.at("hvac_humidity_set").writable);
  CHECK(c->devices.at("hvac_heater_2").features.at("on_off").value.isNull());
  CHECK(
    ha::RenderScene(*c).text.find(
      "<d_hvac_heater>0 Тёплый пол Ванная [Ванная]<p_rw_hvac_temp_set><p_rw_on_off><p_ro_temperature>"
    )
    != std::string::npos
  );
}
TEST_CASE("HA binary sensors of a learned class report on as true, other classes are left out", "[ha]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(Snapshot(R"(- names: Входная дверь
  domain: binary_sensor
  state: 'on'
  attributes:
    device_class: door
- names: Движение в коридоре
  domain: binary_sensor
  state: 'off'
  attributes:
    device_class: motion
- names: Дым на кухне
  domain: binary_sensor
  state: 'off'
  attributes:
    device_class: smoke
- names: Газ на кухне
  domain: binary_sensor
  state: unavailable
  attributes:
    device_class: gas
- names: Протечка в ванной
  domain: binary_sensor
  state: 'on'
  attributes:
    device_class: moisture
- names: Окно в спальне
  domain: binary_sensor
  state: 'on'
  attributes:
    device_class: window
- names: Присутствие в кабинете
  domain: binary_sensor
  state: 'on'
  attributes:
    device_class: occupancy
- names: Батарейка замка
  domain: binary_sensor
  state: 'off'
  attributes:
    device_class: battery
)"));
  REQUIRE(c->devices.size() == 5);
  CHECK(c->devices.at("sensor_door_0").features.at("doorcontact_state").value == true);
  CHECK_FALSE(c->devices.at("sensor_door_0").features.at("doorcontact_state").writable);
  CHECK(c->devices.at("sensor_pir_0").features.at("pir").value == false);
  CHECK(c->devices.at("sensor_smoke_0").features.at("smoke_state").value == false);
  CHECK(c->devices.at("sensor_gas_0").features.at("gas_leak_state").value.isNull());
  CHECK(c->devices.at("sensor_water_leak_0").features.at("water_leak_state").value == true);
  CHECK(ha::RenderScene(*c).text.find("<d_sensor_door>0 Входная дверь<p_ro_doorcontact_state>\n") != std::string::npos);
}
TEST_CASE("HA fans, humidifiers and valves get their learned categories, helpers and switches do not", "[ha]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(Snapshot(R"(- names: Вентилятор
  domain: fan
  state: 'on'
  attributes:
    percentage: '50'
- names: Увлажнитель
  domain: humidifier
  state: 'off'
  attributes:
    humidity: '45'
- names: Кран
  domain: valve
  state: closed
  attributes:
    current_position: '0'
- names: Гостевой режим
  domain: input_boolean
  state: 'on'
- names: Реле насоса
  domain: switch
  state: 'off'
  attributes:
    device_class: switch
- names: Розетка
  domain: switch
  state: 'on'
  attributes:
    device_class: outlet
)"));
  REQUIRE(c->devices.size() == 5);
  CHECK(c->devices.at("hvac_fan_0").features.at("on_off").value == true);
  CHECK(c->devices.at("hvac_fan_0").features.at("on_off").writable);
  // Without fan__HassFanSetSpeed on the server the speed is only read.
  CHECK(c->devices.at("hvac_fan_0").features.at("hvac_air_flow_power").value == "medium");
  CHECK_FALSE(c->devices.at("hvac_fan_0").features.at("hvac_air_flow_power").writable);
  const auto& humidifier = c->devices.at("hvac_humidifier_0");
  CHECK(humidifier.features.at("on_off").value == false);
  CHECK(humidifier.features.at("hvac_humidity_set").value.asDouble() == 45);
  CHECK_FALSE(humidifier.features.at("hvac_humidity_set").writable);
  const auto& valve = c->devices.at("valve_0");
  CHECK(valve.features.at("open_set").value == "close");
  CHECK(valve.features.at("open_percentage").value.asDouble() == 0);
  // HA documents a switch of class switch as a generic switch, as one without a class: both stay sockets; an
  // input_boolean is a helper, not a device.
  CHECK(c->devices.at("socket_0").name == "Реле насоса");
  CHECK(c->devices.at("socket_1").name == "Розетка");
}
TEST_CASE("HA fan speed is a named speed of its percentage, humidity setpoint is written by a unique name", "[ha]") {
  std::string yaml;
  for (const auto* percentage : {"0", "20", "21", "40", "41", "50", "61", "80", "81", "100"}) {
    yaml += std::string("- names: Вентилятор ") + percentage + "\n  domain: fan\n  state: 'on'\n  attributes:\n"
      + "    percentage: '" + percentage + "'\n";
  }
  yaml += R"(- names: Вентилятор без скоростей
  domain: fan
  state: 'on'
- names: Увлажнитель
  domain: humidifier
  state: 'on'
  areas: Спальня
  attributes:
    humidity: '55'
- names: Осушитель
  domain: humidifier
  state: 'on'
  areas: Спальня
  attributes:
    humidity: '40'
- names: Осушитель
  domain: humidifier
  state: 'off'
  areas: Подвал
  attributes:
    humidity: '50'
- names: Кондиционер
  domain: climate
  state: cool
  attributes:
    temperature: 22
    humidity: 45
)";
  auto snapshot = Snapshot(yaml);
  for (const auto* tool : {"fan__HassFanSetSpeed", "humidifier__HassHumidifierSetpoint"}) {
    snapshot.tools.push_back({.server_id = "ha", .tool_name = tool});
  }
  ha::CatalogBuilder b;
  const auto c = b.Build(snapshot);
  // HA's percentage_to_ordered_list_item over quiet, low, medium, high, turbo: 1-20, 21-40, ..., 81-100.
  std::map<std::string, Json::Value> speeds;
  for (const auto& [alias, d] : c->devices) {
    if (d.domain == "fan" && d.features.count("hvac_air_flow_power")) {
      CHECK(d.features.at("hvac_air_flow_power").writable);
      speeds[d.name] = d.features.at("hvac_air_flow_power").value;
    }
  }
  CHECK(speeds.size() == 10);
  CHECK(speeds["Вентилятор 0"].isNull());
  CHECK(speeds["Вентилятор 20"] == "quiet");
  CHECK(speeds["Вентилятор 21"] == "low");
  CHECK(speeds["Вентилятор 40"] == "low");
  CHECK(speeds["Вентилятор 41"] == "medium");
  CHECK(speeds["Вентилятор 50"] == "medium");
  CHECK(speeds["Вентилятор 61"] == "high");
  CHECK(speeds["Вентилятор 80"] == "high");
  CHECK(speeds["Вентилятор 81"] == "turbo");
  CHECK(speeds["Вентилятор 100"] == "turbo");
  std::map<std::string, const ha::Device*> humidifiers;
  for (const auto& [alias, d] : c->devices) {
    humidifiers[d.name + " " + d.area] = &d;
  }
  CHECK(humidifiers.at("Увлажнитель Спальня")->features.at("hvac_humidity_set").writable);
  // HassHumidifierSetpoint takes no area: HA refuses a name that humidifiers of two areas share.
  CHECK_FALSE(humidifiers.at("Осушитель Спальня")->features.at("hvac_humidity_set").writable);
  CHECK_FALSE(humidifiers.at("Осушитель Подвал")->features.at("hvac_humidity_set").writable);
  // The humidity of a climate has no intent: the tool of the humidifier integration does not set it.
  CHECK_FALSE(humidifiers.at("Кондиционер ")->features.at("hvac_humidity_set").writable);
  CHECK(
    ha::RenderScene(*c).text.find("<d_hvac_humidifier>0 Увлажнитель [Спальня]<p_rw_hvac_humidity_set><p_rw_on_off>")
    != std::string::npos
  );
}
TEST_CASE("HA categories stay inside the model vocabulary", "[ha]") {
  ha::CatalogBuilder b;
  const auto c = b.Build(Snapshot(R"(- names: Hall humidity
  domain: sensor
  state: '40'
  attributes:
    device_class: humidity
- names: Lock battery
  domain: sensor
  state: '90'
  attributes:
    device_class: battery
- names: Floor
  domain: climate
  state: heat
  attributes:
    temperature: 21
)"));
  REQUIRE(c->devices.size() == 2);
  CHECK(c->devices.at("sensor_temp_0").features.count("humidity"));
  CHECK(c->devices.count("hvac_heater_0"));
}

TEST_CASE("HA scene orders categories and indices lexically without renumbering gaps", "[ha][scene]") {
  ha::CatalogBuilder builder;
  std::string lights;
  for (int i = 0; i < 13; ++i) {
    lights += "- names: Lamp " + std::to_string(i) + "\n  domain: light\n  state: 'on'\n";
  }
  const std::string others
    = "- names: Socket\n  domain: switch\n  state: 'on'\n"
      "- names: Fan\n  domain: fan\n  state: 'on'\n"
      "- names: Heater\n  domain: climate\n  state: heat\n";
  auto catalog = builder.Build(Snapshot(others + lights));
  const auto check = [](const ha::Catalog& catalog, const std::vector<int>& indices) {
    std::string expected
      = "<d_hvac_fan>0 Fan<p_rw_on_off>\n"
        "<d_hvac_heater>0 Heater<p_rw_hvac_temp_set><p_rw_on_off><p_ro_temperature>\n";
    for (int i : indices) {
      const auto index = std::to_string(i);
      REQUIRE(catalog.devices.at("light_" + index).name == "Lamp " + index);
      expected += "<d_light>" + index + " Lamp " + index
        + "<p_rw_light_brightness><p_rw_light_colour><p_rw_light_colour_temp><p_rw_on_off>\n";
    }
    expected += "<d_socket>0 Socket<p_rw_on_off>\n";
    REQUIRE(ha::RenderScene(catalog).text == expected);
  };
  check(*catalog, {0, 1, 10, 11, 12, 2, 3, 4, 5, 6, 7, 8, 9});
  const std::string removed = "- names: Lamp 1\n  domain: light\n  state: 'on'\n";
  lights.erase(lights.find(removed), removed.size());
  catalog = builder.Build(Snapshot(others + lights));
  REQUIRE(catalog->devices.count("light_1") == 0);
  check(*catalog, {0, 10, 11, 12, 2, 3, 4, 5, 6, 7, 8, 9});
}

TEST_CASE("HA online is an optional read-only parameter of every entity, sorted by its bare name", "[ha][online]") {
  const auto house = Snapshot(lamp + R"(- names: Curtain
  domain: cover
  state: closed
  attributes:
    current_position: 0
- names: Plug
  domain: switch
  state: unavailable
)");
  ha::CatalogBuilder plain;
  const auto baseline = plain.Build(house);
  REQUIRE(baseline->devices.size() == 3);
  for (const auto& [alias, device] : baseline->devices) {
    CHECK(device.features.count("online") == 0);
  }
  ha::CatalogBuilder builder;
  const auto catalog = builder.Build(house, {.include_online = true});
  CHECK(
    ha::RenderScene(*catalog).text
    == "<d_curtain>0 Curtain<p_ro_online><p_ro_open_percentage><p_ro_open_set>\n"
       "<d_light>0 Bedroom lamp "
       "[Bedroom]<p_rw_light_brightness><p_rw_light_colour><p_rw_light_colour_temp><p_rw_on_off><p_ro_online>\n"
       "<d_socket>0 Plug<p_rw_on_off><p_ro_online>\n"
  );
  CHECK(ha::RenderScene(*baseline).text.find("online") == std::string::npos);
  CHECK(catalog->devices.at("light_0").features.at("online").value == true);
  CHECK(catalog->devices.at("socket_0").features.at("online").value == false);
}

TEST_CASE("HA online is false only for an unavailable entity, whatever its other values", "[ha][online]") {
  for (const std::string state : {"unavailable", "unknown", "", "on", "off"}) {
    const auto devices = ha::ParseContext(
      Json::Value("- names: Plug\n  domain: switch\n  state: '" + state + "'\n"), "ha"
    );
    REQUIRE(devices.size() == 1);
    const auto& online = devices.front().features.at("online");
    CHECK_FALSE(online.writable);
    CHECK(online.value == (state != "unavailable"));
  }
}
