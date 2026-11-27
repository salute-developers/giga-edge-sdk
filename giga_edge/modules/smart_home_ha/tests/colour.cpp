#include "giga_edge/modules/smart_home_ha/colour.h"

#include "test_config.h"

#include "giga_edge/modules/function_calling/compact_tool_call_parser.h"
#include "giga_edge/modules/smart_home_ha/colour_names.h"

#include <catch.hpp>
#include <yaml-cpp/yaml.h>

using namespace giga_edge;
using namespace giga_edge::ha_test;

TEST_CASE("HA colour appears in lexical parameter order only with its write tool", "[ha][colour]") {
  const auto catalog = Catalog();
  const auto& feature = catalog->devices.at("light_0").features.at("light_colour");
  CHECK(feature.writable);
  CHECK(feature.value.isNull());
  CHECK(
    ha::RenderScene(*catalog).text.find(
      "<d_light>0 Bedroom lamp "
      "[Bedroom]<p_rw_light_brightness><p_rw_light_colour><p_rw_light_colour_temp><p_rw_on_off>\n"
    )
    != std::string::npos
  );
  auto snapshot = Snapshot();
  snapshot.tools.erase(snapshot.tools.begin() + 2);
  ha::CatalogBuilder builder;
  CHECK(builder.Build(snapshot)->devices.at("light_0").features.count("light_colour") == 0);
}

TEST_CASE("HA exact palette colours retain their RGB including aliases", "[ha][colour]") {
  for (const auto& colour : ha::kColourNames) {
    const auto rgb = std::to_string(colour.rgb[0]) + ";" + std::to_string(colour.rgb[1]) + ";"
      + std::to_string(colour.rgb[2]);
    CAPTURE(colour.name, rgb);
    const auto action = WriteAction(Action("set_device_parameter", "light_colour", rgb));
    REQUIRE(action.tool_name == "light__HassLightSet");
    const auto name = ParseJsonOrThrow(action.arguments_json)["color"].asString();
    const ha::NamedColour* matched = nullptr;
    for (const auto& candidate : ha::kColourNames) {
      if (candidate.name == name) {
        matched = &candidate;
      }
    }
    REQUIRE(matched != nullptr);
    CHECK(matched->rgb == colour.rgb);
  }
}

TEST_CASE("HA nearby saturated colours and shades use perceptual colour names", "[ha][colour]") {
  for (const auto& [rgb, name] : std::vector<std::pair<std::string, std::string>>{
         {"255;18;0", "red"},
         {"250;10;5", "red"},
         {"255;128;0", "darkorange"},
         {"255;150;0", "darkorange"},
         {"255;165;0", "orange"},
         {"255;255;0", "yellow"},
         {"250;240;5", "yellow"},
         {"0;255;0", "lime"},
         {"18;250;0", "lime"},
         {"0;128;0", "green"},
         {"0;255;255", "aqua"},
         {"0;240;250", "aqua"},
         {"135;206;235", "skyblue"},
         {"0;0;255", "blue"},
         {"10;20;245", "blue"},
         {"128;0;255", "blueviolet"},
         {"138;43;226", "blueviolet"},
         {"148;0;211", "darkviolet"},
         {"255;0;255", "fuchsia"},
         {"255;20;147", "deeppink"},
         {"255;100;180", "hotpink"},
         {"255;192;203", "pink"},
         {"255;255;255", "white"},
         {"254;253;254", "white"},
         {"255;250;240", "floralwhite"},
         {"0;0;0", "black"},
         {"1;1;1", "black"},
         {"128;128;128", "gray"},
         {"190;190;190", "silver"}
       }) {
    CAPTURE(rgb);
    const auto action = WriteAction(Action("set_device_parameter", "light_colour", rgb));
    CHECK(
      ParseJsonOrThrow(action.arguments_json)
      == ParseJsonOrThrow(
        "{\"name\":\"Bedroom lamp\",\"area\":\"Bedroom\",\"domain\":[\"light\"],\"color\":\"" + name + "\"}"
      )
    );
  }
}

TEST_CASE("HA invalid RGB fails with BAD_VALUE before any MCP request", "[ha][colour]") {
  for (const auto& value : std::vector<Json::Value>{
         "",
         "255",
         "255;0",
         "255;0;0;0",
         "255;;0",
         ";0;0",
         "0;0;",
         "0,0,0",
         "red",
         "256;0;0",
         "0;256;0",
         "0;0;256",
         "-1;0;0",
         "0;-1;0",
         "0;0;-1",
         "-0;0;0",
         "1.5;0;0",
         "1e2;0;0",
         "+1;0;0",
         " 1;0;0",
         "0; 1;0",
         "0;0;1 ",
         "nan;0;0",
         "999999999999999999999;0;0",
         "0;0;1%",
         std::string("0;0;0\0x", 7),
         0,
         255,
         1.5,
         true,
         Json::Value(),
         Json::Value(Json::arrayValue),
         Json::Value(Json::objectValue)
       }) {
    CAPTURE(WriteJson(value));
    const auto action = ha::ResolveAction(Action("set_device_parameter", "light_colour", value), *Catalog());
    CHECK(action.status == AssistantStatus::ERROR);
    CHECK(action.kind == McpResolvedActionKind::NONE);
    CHECK(action.error == "BAD_VALUE");
  }
}

TEST_CASE("HA colour and its temperature have no readable copyable or relative value", "[ha][colour]") {
  for (const auto* parameter : {"light_colour", "light_colour_temp"}) {
    CAPTURE(parameter);
    const auto read = ha::ResolveAction(Action("get_device_parameter", parameter), *Catalog());
    const auto done = ha::Complete(Reply(read, ReadContext()));
    REQUIRE(done.results.size() == 1);
    CHECK(done.results[0].is_error);
    CHECK(done.results[0].error == "UNKNOWN_PARAMETER");
    const auto copy = ha::ResolveAction(CopyAction(parameter), *CopyCatalog());
    CHECK(ha::Complete(Reply(copy, SourceContext())).results.at(0).error == "UNKNOWN_PARAMETER");
  }
  const auto warmer = ha::ResolveAction(Action("relative_change", "light_colour_temp", "+"), *Catalog());
  CHECK(ha::Complete(Reply(warmer, ReadContext())).results.at(0).error == "UNKNOWN_PARAMETER");
  CHECK(ha::ResolveAction(Action("relative_change", "light_colour", "+"), *Catalog()).error == "RELATIVE_NOT_ALLOWED");
}

TEST_CASE("HA colour and warmth reported by a server are read, copied and stepped", "[ha][colour]") {
  // The virtual house of the demo reports them; Home Assistant itself does not.
  const auto lamp = [](const std::string& name, const std::string& area, const std::string& colour) {
    Json::Value v;
    v["result"] = "Live Context: devices\n- names: " + name + "\n  domain: light\n  state: 'on'\n  areas: " + area
      + "\n  attributes:\n    brightness_pct: '30'\n    " + colour + "\n";
    return v;
  };
  const auto red = lamp("Desk lamp", "Study", "rgb_color: [255, 0, 0]");
  const auto warm = lamp("Desk lamp", "Study", "color_temp_kelvin: 4600");

  const auto get = ha::Complete(Reply(
    ha::ResolveAction(Action("get_device_parameter", "light_colour"), *Catalog()),
    lamp("Bedroom lamp", "Bedroom", "rgb_color: [250, 10, 5]")
  ));
  CHECK(get.results.at(0).value_json == "\"красный\"");

  auto copy = ha::Complete(Reply(ha::ResolveAction(CopyAction("light_colour"), *CopyCatalog()), red)).continuations;
  REQUIRE(copy.size() == 1);
  copy = ha::Complete(Reply(copy[0], ReadContext())).continuations;
  REQUIRE(copy.size() == 1);
  CHECK(ParseJsonOrThrow(copy[0].arguments_json)["color"] == "red");

  copy = ha::Complete(Reply(ha::ResolveAction(CopyAction("light_colour_temp"), *CopyCatalog()), warm)).continuations;
  REQUIRE(copy.size() == 1);
  copy = ha::Complete(Reply(copy[0], ReadContext())).continuations;
  REQUIRE(copy.size() == 1);
  CHECK(ParseJsonOrThrow(copy[0].arguments_json)["temperature"] == 4600);

  // A warmer step («+»): 4600 K is 50 %, warmer by 10 is 40 %, that is 4220 K; a colder one is 60 %, 4980 K.
  for (const auto& [sign, kelvin] : std::vector<std::pair<std::string, int>>{{"+", 4220}, {"-", 4980}}) {
    CAPTURE(sign);
    const auto step = ha::Complete(Reply(
      ha::ResolveAction(Action("relative_change", "light_colour_temp", sign), *Catalog()),
      lamp("Bedroom lamp", "Bedroom", "color_temp_kelvin: 4600")
    ));
    REQUIRE(step.continuations.size() == 1);
    CHECK(ParseJsonOrThrow(step.continuations[0].arguments_json)["temperature"] == kelvin);
  }
}

TEST_CASE("HA colour is said by the nearest basic colour word", "[ha][colour]") {
  for (const auto& [rgb, word] : std::vector<std::pair<std::string, std::string>>{
         {"255;255;255", "белый"},
         {"255;0;0", "красный"},
         {"255;165;0", "оранжевый"},
         {"0;175;80", "зелёный"},
         {"0;0;255", "синий"},
         {"0;0;139", "синий"},
         {"255;250;240", "белый"},
         {"138;43;226", "фиолетовый"}
       }) {
    CAPTURE(rgb);
    CHECK(ha::SpokenColour(rgb) == word);
  }
  CHECK_THROWS_WITH(ha::SpokenColour("256;0;0"), "BAD_VALUE");
}

TEST_CASE("HA brightness and colour temperature write only their own arguments", "[ha][colour]") {
  for (const auto& [value, kelvin] : std::vector<std::pair<int, int>>{{0, 2700}, {50, 4600}, {100, 6500}}) {
    const auto temperature = WriteAction(Action("set_device_parameter", "light_colour_temp", value));
    const auto brightness = WriteAction(Action("set_device_parameter", "light_brightness", value));
    REQUIRE(temperature.tool_name == "light__HassLightSet");
    REQUIRE(brightness.tool_name == "light__HassLightSet");
    auto expected = ParseJsonOrThrow(R"({"name":"Bedroom lamp","area":"Bedroom","domain":["light"]})");
    expected["temperature"] = kelvin;
    CHECK(ParseJsonOrThrow(temperature.arguments_json) == expected);
    expected.removeMember("temperature");
    expected["brightness"] = value;
    CHECK(ParseJsonOrThrow(brightness.arguments_json) == expected);
  }
}

TEST_CASE("HA graph parser preserves an RGB triplet through read write and confirmation", "[ha][colour]") {
  const auto config = YAML::LoadFile(
    std::string(GIGA_EDGE_TEST_SOURCE_DIR) + "/giga_edge/modules/smart_home_ha/tests/data/function_calling/config.yaml"
  );
  YAML::Node parser_config;
  for (const auto& node : config["nodes"]) {
    if (node["params"] && node["params"]["tool_call_parser"]) {
      parser_config = node["params"]["tool_call_parser"];
    }
  }
  REQUIRE(parser_config.IsMap());
  CompactToolCallParser parser(ParseCompactToolCallParserParams(parser_config));
  auto generation = Action("set_device_parameter", "light_colour", "255;18;0");
  generation.tool_calls = parser.Parse(
    "<start_function_call><f_set_device_parameter><d_light>0<arg_sep><p_rw_light_colour><arg_sep>255;18;0"
  );
  REQUIRE(generation.tool_calls.size() == 1);
  CHECK(ParseJsonOrThrow(generation.tool_calls[0].arguments_json)["value"] == "255;18;0");
  const auto read = ha::ResolveAction(generation, *Catalog());
  REQUIRE(read.tool_name == "homeassistant__GetLiveContext");
  const auto step = ha::Complete(Reply(read, ReadContext()));
  REQUIRE(step.continuations.size() == 1);
  CHECK(ParseJsonOrThrow(step.continuations[0].arguments_json)["color"] == "red");
  const auto done = ha::Complete(Reply(step.continuations[0], Success()));
  REQUIRE(done.results.size() == 1);
  CHECK_FALSE(done.results[0].is_error);
  CHECK(done.continuations.empty());
}
