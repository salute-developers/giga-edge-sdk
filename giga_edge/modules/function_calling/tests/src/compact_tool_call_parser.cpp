#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/function_calling/tool_call_parsers.h"

#include <catch.hpp>

#include <string>

using namespace giga_edge;

namespace {

const char* const kHomeSpecs = R"(
  start_function_call_token: "<start_function_call>"
  function_token_prefixes: ["<f_", "<function_"]
  arg_separator: "<arg_sep>"
  stop_markers: ["</s>", "<|message_sep|>"]
  arg_formats:
    device: {tag_prefixes: ["d_"], indexed: true}
    parameter: {tag_prefixes: ["p_rw_", "p_ro_"]}
    delta: {type: string}
  function_specs:
    - name: set_device_parameter
      token: "<f_set_device_parameter>"
      args: [{name: device_id, format: device}, {name: parameter_name, format: parameter}, value]
    - name: get_device_parameter
      token: "<f_get_device_parameter>"
      args: [{name: device_id, format: device}, {name: parameter_name, format: parameter}]
    - name: relative_change
      token: "<f_relative_change>"
      args: [{name: device_id, format: device}, {name: parameter_name, format: parameter}, {name: value, format: delta}]
    - name: copy_device_parameter
      token: "<f_copy_device_parameter>"
      args: [{name: source_device_id, format: device}, {name: device_id, format: device}, {name: parameter_name, format: parameter}]
)";

CompactToolCallParserParams HomeParams() {
  return ParseCompactToolCallParserParams(YAML::Load(kHomeSpecs));
}

std::shared_ptr<CompactToolCallParser> HomeParser() {
  return std::make_shared<CompactToolCallParser>(HomeParams());
}

CompactToolCallParser DefaultParser() {
  return CompactToolCallParser(HomeParams());
}

std::vector<ToolCall> ParseWhole(const std::string& text) {
  StreamToolCallFilter filter(HomeParser());
  auto out = filter.Feed(text);
  auto tail = filter.Finish();
  for (auto& call : tail.calls) {
    out.calls.push_back(std::move(call));
  }
  return out.calls;
}

} // namespace

TEST_CASE(
  "compact parser preserves copy source destination and parameter in streamed batches",
  "[function_calling][parser][copy]"
) {
  const std::string call
    = "<start_function_call><f_copy_device_parameter><d_light>2<arg_sep><d_light>1<arg_sep><p_rw_light_brightness>";
  for (size_t split = 0; split <= call.size(); ++split) {
    StreamToolCallFilter filter(HomeParser());
    auto first = filter.Feed(call.substr(0, split));
    auto second = filter.Feed(call.substr(split));
    auto tail = filter.Finish();
    std::vector<ToolCall> calls = first.calls;
    calls.insert(calls.end(), second.calls.begin(), second.calls.end());
    calls.insert(calls.end(), tail.calls.begin(), tail.calls.end());
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].name == "copy_device_parameter");
    CHECK(
      calls[0].arguments_json
      == R"({"source_device_id":"light_2","device_id":"light_1","parameter_name":"light_brightness"})"
    );
  }
  auto params = HomeParams();
  params.parallel_tool_calls = true;
  CompactToolCallParser parser(params);
  const auto batch = parser.Parse(call + "<start_function_call><f_copy_device_parameter><d_curtain>0<arg_sep><d_window_blind>1<arg_sep><p_rw_open_percentage><|message_sep|>");
  REQUIRE(batch.size() == 2);
  CHECK(
    batch[1].arguments_json
    == R"({"source_device_id":"curtain_0","device_id":"window_blind_1","parameter_name":"open_percentage"})"
  );
  CHECK(parser.Parse("<f_copy_device_parameter><d_light>2<arg_sep><p_rw_light_brightness>").empty());
}

TEST_CASE("compact parser reads its config", "[function_calling][parser]") {
  const auto params = ParseCompactToolCallParserParams(YAML::Load(R"(
    start_function_call_token: "<call>"
    function_token_prefixes: ["<fn_"]
    arg_separator: "<sep>"
    stop_markers: ["</s>", "<eot>"]
    arg_formats:
      ref: {tag_prefixes: ["r_"], indexed: true}
      text: {type: string}
    function_specs:
      - name: custom_action
        token: "<f_custom_action>"
        args: ["x", {name: y, format: ref}, {name: z, format: text}]
  )"));
  REQUIRE(params.start_function_call_token == "<call>");
  REQUIRE(params.arg_separator == "<sep>");
  REQUIRE(params.stop_markers == std::vector<std::string>{"</s>", "<eot>"});
  REQUIRE(params.function_token_prefixes == std::vector<std::string>{"<fn_"});
  REQUIRE(params.function_specs.size() == 1);
  const auto& spec = params.function_specs[0];
  REQUIRE(spec.name == "custom_action");
  REQUIRE(spec.args.size() == 3);
  REQUIRE(spec.args[0].name == "x");
  REQUIRE(spec.args[0].format.tag_prefixes.empty());
  REQUIRE(spec.args[1].format.tag_prefixes == std::vector<std::string>{"r_"});
  REQUIRE(spec.args[1].format.indexed);
  REQUIRE(spec.args[2].format.as_string);
}

TEST_CASE("compact parser rejects incomplete or inconsistent specs", "[function_calling][parser]") {
  auto make = [](const std::string& yaml) {
    const std::string base
      = "{start_function_call_token: '', function_token_prefixes: [], arg_separator: '|', "
        "stop_markers: [], ";
    return CompactToolCallParser(ParseCompactToolCallParserParams(YAML::Load(base + yaml + "}")));
  };
  REQUIRE_NOTHROW(make("function_specs: [{name: a, token: '<f_a>'}]"));
  REQUIRE_THROWS(make("function_specs: []"));
  REQUIRE_THROWS(ParseCompactToolCallParserParams(YAML::Load("function_specs: [{name: a, token: '<f_a>'}]")));
  REQUIRE_THROWS(make("function_specs: [{name: a, token: '<f_a>', args: [{name: x, format: missing}]}]"));
  REQUIRE_THROWS(make("arg_formats: {bad: {type: number}}, function_specs: [{name: a, token: '<f_a>'}]"));
  REQUIRE_THROWS(make("arg_formats: {bad: {indexed: true}}, function_specs: [{name: a, token: '<f_a>'}]"));
  REQUIRE_THROWS(make("function_specs: [{name: a, token: '<f_a>'}, {name: b, token: '<f_a>'}]"));
}

TEST_CASE("compact parser is registered under its name", "[function_calling][parser]") {
  REQUIRE(ToolCallParserRegistry::Has("compact_tool_call_v1"));
  REQUIRE(ToolCallParserRegistry::Create("compact_tool_call_v1", YAML::Load(kHomeSpecs)) != nullptr);
  REQUIRE_THROWS(ToolCallParserRegistry::Create("compact_tool_call_v1", YAML::Node()));
}

TEST_CASE("compact parser knows nothing about the domain beyond its specs", "[function_calling][parser]") {
  CompactToolCallParser parser(ParseCompactToolCallParserParams(YAML::Load(R"(
    start_function_call_token: "<start_function_call>"
    function_token_prefixes: ["<f_"]
    arg_separator: "<arg_sep>"
    stop_markers: ["</s>"]
    arg_formats:
      contact: {tag_prefixes: ["c_"], indexed: true}
    function_specs:
      - {name: send_message, token: "<f_send_message>", args: [{name: to, format: contact}, text]}
  )")));
  auto calls = parser.Parse("<start_function_call><f_send_message><c_friend>2<arg_sep>Буду в 5");
  REQUIRE(calls.size() == 1);
  REQUIRE(calls[0].name == "send_message");
  REQUIRE(calls[0].arguments_json == R"({"to":"friend_2","text":"Буду в 5"})");
  REQUIRE(parser.Parse("<f_set_device_parameter><d_light>0<arg_sep><p_rw_on_off><arg_sep>true").empty());
}

TEST_CASE(
  "compact parser leaves out trailing optional arguments the call does not give", "[function_calling][parser]"
) {
  CompactToolCallParser parser(ParseCompactToolCallParserParams(YAML::Load(R"(
    start_function_call_token: "<start_function_call>"
    function_token_prefixes: ["<f_"]
    arg_separator: "<arg_sep>"
    stop_markers: ["</s>", "<|message_sep|>"]
    arg_formats:
      text: {type: string}
    function_specs:
      - {name: get_date, token: "<f_get_date>", args: [{name: time_zone, format: text, optional: true}]}
      - name: get_shifted_date
        token: "<f_get_shifted_date>"
        args: [{name: shift, format: text}, {name: time_zone, format: text, optional: true}]
  )")));
  const auto args = [&](const std::string& text) {
    const auto calls = parser.Parse(text);
    return calls.size() == 1 ? calls[0].arguments_json : std::string("<none>");
  };
  REQUIRE(args("<start_function_call><f_get_date>") == "{}");
  REQUIRE(args("<start_function_call><f_get_date><|message_sep|>") == "{}");
  REQUIRE(args("<start_function_call><f_get_date>Лондон") == R"({"time_zone":"Лондон"})");
  REQUIRE(args("<f_get_shifted_date>1d") == R"({"shift":"1d"})");
  REQUIRE(args("<f_get_shifted_date>1d<arg_sep>") == R"({"shift":"1d","time_zone":""})");
  REQUIRE(args("<f_get_shifted_date>-1d<arg_sep>Москва") == R"({"shift":"-1d","time_zone":"Москва"})");
  // A required argument before an optional one must still be given.
  REQUIRE(args("<f_get_shifted_date>") == "<none>");
  REQUIRE_THROWS(ParseCompactToolCallParserParams(YAML::Load(R"(
    start_function_call_token: "<s>"
    function_token_prefixes: ["<f_"]
    arg_separator: "<arg_sep>"
    stop_markers: ["</s>"]
    function_specs:
      - {name: f, token: "<f_f>", args: [{name: a, optional: true}, b]}
  )")));
}

TEST_CASE("only trailing compact arguments may be optional", "[function_calling][parser]") {
  const auto args = [](const std::string& yaml) {
    const auto config = YAML::Load(
      "{start_function_call_token: '', function_token_prefixes: [], arg_separator: '|', stop_markers: [], "
      "function_specs: [{name: f, token: '<f_f>', args: "
      + yaml + "}]}"
    );
    return ParseCompactToolCallParserParams(config).function_specs[0].args;
  };
  REQUIRE(args("[{name: a, optional: true}]")[0].optional);
  const auto tail = args("[a, {name: b, optional: true}, {name: c, optional: true}]");
  REQUIRE_FALSE(tail[0].optional);
  REQUIRE(tail[1].optional);
  REQUIRE(tail[2].optional);
  REQUIRE_FALSE(args("[{name: a, optional: false}, b]")[0].optional);
  REQUIRE_THROWS_WITH(args("[a, {name: b, optional: true}, c]"), Catch::Contains("optional: f"));
  REQUIRE_THROWS(args("[{name: a, optional: true}, {name: b, optional: false}]"));
}

TEST_CASE(
  "compact parser reads several optional arguments, tags and scalars among them", "[function_calling][parser]"
) {
  CompactToolCallParser parser(ParseCompactToolCallParserParams(YAML::Load(R"(
    start_function_call_token: "<start_function_call>"
    function_token_prefixes: ["<f_"]
    arg_separator: "<arg_sep>"
    stop_markers: ["</s>"]
    arg_formats:
      text: {type: string}
      room: {tag_prefixes: ["r_"]}
    function_specs:
      - name: set_timer
        token: "<f_set_timer>"
        args:
          - {name: interval, format: text}
          - {name: name, format: text, optional: true}
          - {name: text, format: text, optional: true}
      - {name: light, token: "<f_light>", args: [value, {name: room, format: room, optional: true}]}
      - {name: volume, token: "<f_volume>", args: [{name: value, optional: true}]}
  )")));
  const auto args = [&](const std::string& text) {
    const auto calls = parser.Parse(text);
    return calls.size() == 1 ? calls[0].arguments_json : std::string("<none>");
  };
  REQUIRE(args("<f_set_timer>10m") == R"({"interval":"10m"})");
  REQUIRE(args("<f_set_timer> 10m </s>") == R"({"interval":"10m"})");
  REQUIRE(args("<f_set_timer>10m<arg_sep>паста") == R"({"interval":"10m","name":"паста"})");
  REQUIRE(
    args("<f_set_timer>10m<arg_sep>паста<arg_sep>помешать") == R"({"interval":"10m","name":"паста","text":"помешать"})"
  );
  REQUIRE(args("<f_set_timer>10m<arg_sep><arg_sep>помешать") == R"({"interval":"10m","name":"","text":"помешать"})");
  REQUIRE(args("<f_set_timer>10m<arg_sep>паста<arg_sep>") == R"({"interval":"10m","name":"паста","text":""})");
  REQUIRE(args(R"(<f_set_timer>1m<arg_sep>"суп" \ борщ)") == R"({"interval":"1m","name":"\"суп\" \\ борщ"})");
  REQUIRE(args("<f_set_timer>") == "<none>");

  REQUIRE(args("<f_light>5") == R"({"value":5})");
  REQUIRE(args("<f_light>true<arg_sep><r_kitchen>") == R"({"value":true,"room":"kitchen"})");
  REQUIRE(args("<f_light>5<arg_sep>") == R"({"value":5,"room":""})");
  REQUIRE(args("<f_light>5<arg_sep><r_kitchen><arg_sep>лишнее") == "<none>");
  // The tag of an optional argument without a separator does not become part of the value.
  REQUIRE(args("<f_light>5<r_kitchen>") == "<none>");
  REQUIRE(args("<f_light>5 <r_kitchen") == "<none>");
  REQUIRE(args("<f_light>5<r_>") == "<none>");

  REQUIRE(args("<f_volume>") == "{}");
  REQUIRE(args("<f_volume>7") == R"({"value":7})");
  REQUIRE(args("<f_volume>-2.5") == R"({"value":-2.5})");
  REQUIRE(args("<f_volume>громче") == R"({"value":"громче"})");
}

TEST_CASE("a given optional compact argument is parsed as a required one", "[function_calling][parser]") {
  const auto make = [](bool optional) {
    auto config = YAML::Load(R"(
      start_function_call_token: "<start_function_call>"
      function_token_prefixes: ["<f_"]
      arg_separator: "<arg_sep>"
      stop_markers: ["</s>"]
      arg_formats:
        text: {type: string}
        room: {tag_prefixes: ["r_"]}
        device: {tag_prefixes: ["d_"], indexed: true}
      function_specs:
        - {name: f, token: "<f_f>", args: [{name: a, format: text}, {name: b}, {name: c, format: room}]}
        - {name: g, token: "<f_g>", args: [{name: a}, {name: b, format: device}]}
    )");
    config["function_specs"][0]["args"][1]["optional"] = optional;
    config["function_specs"][0]["args"][2]["optional"] = optional;
    config["function_specs"][1]["args"][1]["optional"] = optional;
    return CompactToolCallParser(ParseCompactToolCallParserParams(config));
  };
  const auto required = make(false);
  const auto optional = make(true);
  REQUIRE(required.Parse("<f_f>x<arg_sep>1<arg_sep><r_hall>").size() == 1);
  REQUIRE(required.Parse("<f_g>5<arg_sep><d_light>0").size() == 1);
  const auto same = [&](const std::string& call) {
    CAPTURE(call);
    const auto expected = required.Parse(call);
    const auto actual = optional.Parse(call);
    REQUIRE(actual.size() == expected.size());
    if (!expected.empty()) {
      REQUIRE(actual[0].arguments_json == expected[0].arguments_json);
    }
  };
  for (const auto* body : {
         "x<arg_sep>1<arg_sep><r_hall>",
         " x <arg_sep> 1 <arg_sep> <r_hall> ",
         "x<arg_sep>true<arg_sep>hall",
         "x<arg_sep><arg_sep><r_hall>",
         "x<arg_sep>1<arg_sep>",
         "x<arg_sep>1<arg_sep><r_hall>лишнее",
         "x<arg_sep>1<arg_sep><r_hall><arg_sep>лишнее",
         "x<arg_sep>1<r_hall>",
         "x<arg_sep>1 <r_hall",
         "x<r_hall>",
         "x<arg_sep>1<r_hall><arg_sep>",
         "x<arg_sep>1<r_>",
         "x<arg_sep>1<r_",
       }) {
    same(std::string("<f_f>") + body);
  }
  // A tag without an index is a tag too.
  for (const auto* call : {"<f_g>5<d_light>", "<f_g>5<d_light", "<f_g>5<d_light>0", "<f_g>5<arg_sep><d_light>0"}) {
    same(call);
  }
}

TEST_CASE("compact parser is strict about the shape of a call", "[function_calling][parser]") {
  auto parser = DefaultParser();
  REQUIRE(parser.Parse("<f_get_device_parameter><d_light>0").empty());
  REQUIRE(parser.Parse("<f_get_device_parameter><d_light>0<arg_sep><p_rw_on_off> и ещё текст").empty());
  REQUIRE(parser.Parse("<f_set_device_parameter>").empty());
  const auto spaced = parser.Parse("<f_set_device_parameter> <d_light>0 <arg_sep> <p_rw_on_off> <arg_sep> 1 ");
  REQUIRE(spaced.size() == 1);
  REQUIRE(spaced[0].arguments_json == R"({"device_id":"light_0","parameter_name":"on_off","value":1})");
  const auto empty_value = parser.Parse("<f_set_device_parameter><d_light>0<arg_sep><p_rw_light_mode><arg_sep>");
  REQUIRE(empty_value.size() == 1);
  REQUIRE(empty_value[0].arguments_json == R"({"device_id":"light_0","parameter_name":"light_mode","value":""})");
  const auto raw = parser.Parse("<f_set_device_parameter><d_light><arg_sep><p_rw_on_off><arg_sep>1");
  REQUIRE(raw.size() == 1);
  REQUIRE(raw[0].arguments_json == R"({"device_id":"d_light","parameter_name":"on_off","value":1})");
}

TEST_CASE("compact parser rejects an interrupted call but permits a natural end", "[function_calling][parser]") {
  const std::string call
    = "<start_function_call><f_set_device_parameter><d_light>0<arg_sep><p_rw_light_brightness><arg_sep>45";
  StreamToolCallFilter filter(HomeParser());
  for (size_t cut = 1; cut <= call.size(); ++cut) {
    INFO(cut);
    REQUIRE(filter.Feed(call.substr(0, cut)).calls.empty());
    const auto tail = filter.Finish(false);
    REQUIRE(tail.calls.empty());
    REQUIRE(tail.incomplete_call);
  }
  filter.Feed(call);
  const auto natural = filter.Finish();
  REQUIRE_FALSE(natural.incomplete_call);
  REQUIRE(natural.calls.size() == 1);
  const auto complete = filter.Feed(call + "</s>");
  REQUIRE(complete.calls.size() == 1);
  REQUIRE_FALSE(filter.Finish(false).incomplete_call);
  REQUIRE(filter.Feed("Обычный ответ.").text == "Обычный ответ.");
  REQUIRE_FALSE(filter.Finish(false).incomplete_call);
}

TEST_CASE("compact parser parses a full call", "[function_calling][parser]") {
  const auto calls = ParseWhole(
    "<start_function_call><f_relative_change><d_hvac_radiator>0<arg_sep><p_rw_hvac_temp_set><arg_sep>+"
  );
  REQUIRE(calls.size() == 1);
  REQUIRE(calls[0].name == "relative_change");
  REQUIRE(calls[0].arguments_json == R"({"device_id":"hvac_radiator_0","parameter_name":"hvac_temp_set","value":"+"})");
  REQUIRE(calls[0].raw_text.rfind("<start_function_call>", 0) == 0);
}

TEST_CASE("compact parser normalizes truncated tokens", "[function_calling][parser]") {
  const auto calls = ParseWhole("<f_relative_change><d_light>0<arg_sep><p_rw_brightness<arg_sep><+10>");
  REQUIRE(calls.size() == 1);
  REQUIRE(calls[0].arguments_json == R"({"device_id":"light_0","parameter_name":"brightness","value":"+10"})");
}

TEST_CASE("compact parser repairs omitted separators", "[function_calling][parser]") {
  auto calls = ParseWhole("<start_function_call><f_set_device_parameter><d_hvac_radiator>0<p_rw_on_off>1");
  REQUIRE(calls.size() == 1);
  REQUIRE(calls[0].arguments_json == R"({"device_id":"hvac_radiator_0","parameter_name":"on_off","value":1})");

  calls = ParseWhole("<f_get_device_parameter><d_led_strip>0<p_rw_light_mode>");
  REQUIRE(calls.size() == 1);
  REQUIRE(calls[0].arguments_json == R"({"device_id":"led_strip_0","parameter_name":"light_mode"})");

  calls = ParseWhole("<f_relative_change><d_vacuum_cleaner>0<p_rw_vacuum_cleaner_command>start");
  REQUIRE(calls.size() == 1);
  REQUIRE(
    calls[0].arguments_json
    == R"({"device_id":"vacuum_cleaner_0","parameter_name":"vacuum_cleaner_command","value":"start"})"
  );
}

TEST_CASE("compact parser stops at a stop marker and keeps scalar types", "[function_calling][parser]") {
  auto calls = ParseWhole("<f_get_device_parameter><d_hvac_radiator>0<arg_sep><p_ro_temperature></s>ignored");
  REQUIRE(calls.size() == 1);
  REQUIRE(calls[0].arguments_json == R"({"device_id":"hvac_radiator_0","parameter_name":"temperature"})");

  calls = ParseWhole("<f_set_device_parameter><d_hvac_radiator>0<arg_sep><p_rw_on_off><arg_sep>true");
  REQUIRE(calls[0].arguments_json == R"({"device_id":"hvac_radiator_0","parameter_name":"on_off","value":true})");
}

TEST_CASE("compact parser flags unknown calls as malformed and keeps plain text", "[function_calling][parser]") {
  StreamToolCallFilter filter(HomeParser());
  auto out = filter.Feed("<f_unknown_action><arg_sep>payload");
  auto tail = filter.Finish();
  REQUIRE(out.calls.empty());
  REQUIRE(tail.calls.empty());
  REQUIRE((out.text + tail.text).empty());
  REQUIRE(tail.malformed_call);
  REQUIRE_FALSE(filter.Finish().malformed_call);

  StreamToolCallFilter plain(HomeParser());
  out = plain.Feed("<assistant>Привет, чем помочь?</assistant>");
  tail = plain.Finish();
  REQUIRE(out.text + tail.text == "Привет, чем помочь?");
}

TEST_CASE("compact parser streams: nothing leaks before the call is complete", "[function_calling][parser]") {
  StreamToolCallFilter filter(HomeParser());
  std::string text;
  std::vector<ToolCall> calls;
  for (const char* delta :
       {"<start_",
        "function_call><f_set_dev",
        "ice_parameter><d_led_strip>0",
        "<arg_sep><p_rw_on_off><arg_sep>",
        "true"}) {
    auto out = filter.Feed(delta);
    text += out.text;
    REQUIRE(out.calls.empty());
  }
  auto tail = filter.Finish();
  REQUIRE(text.empty());
  REQUIRE(tail.calls.size() == 1);
  REQUIRE(tail.calls[0].name == "set_device_parameter");
  REQUIRE(
    tail.calls[0].raw_text
    == "<start_function_call><f_set_device_parameter><d_led_strip>0<arg_sep><p_rw_on_off><arg_sep>true"
  );
}

TEST_CASE("compact parser streams plain answers word by word", "[function_calling][parser]") {
  StreamToolCallFilter filter(HomeParser());
  std::string text;
  for (const char* delta : {"Вкл", "ючил ", "свет", "."}) {
    text += filter.Feed(delta).text;
  }
  text += filter.Finish().text;
  REQUIRE(text == "Включил свет.");
}

TEST_CASE("compact parser gives the same result for any chunking", "[function_calling][parser]") {
  const std::string stream
    = "Сейчас включу. <start_function_call><f_set_device_parameter><d_led_strip>0<arg_sep>"
      "<p_rw_on_off><arg_sep>true</s>";
  auto run = [&](size_t step) {
    StreamToolCallFilter filter(HomeParser());
    std::string text;
    std::vector<std::string> calls;
    auto take = [&](StreamFilterOutput out) {
      text += out.text;
      for (const auto& call : out.calls) {
        calls.push_back(call.name + call.arguments_json);
      }
    };
    for (size_t at = 0; at < stream.size(); at += step) {
      take(filter.Feed(std::string_view(stream).substr(at, step)));
    }
    take(filter.Finish());
    return std::make_pair(text, calls);
  };
  const auto whole = run(stream.size());
  REQUIRE(whole.second.size() == 1);
  for (const size_t step : {1, 2, 3, 7}) {
    INFO(step);
    REQUIRE(run(step) == whole);
  }
}

TEST_CASE("compact parser ends a call at the nearest stop marker", "[function_calling][parser]") {
  auto parser = DefaultParser();
  REQUIRE(parser.FindEnd("<f_x><|message_sep|>tail</s>", false).value() == 20);
  REQUIRE(parser.FindEnd("<f_x></s>tail<|message_sep|>", false).value() == 9);
  REQUIRE_FALSE(parser.FindEnd("<f_x>", false).has_value());
  REQUIRE(parser.FindEnd("<f_x>", true).value() == 5);
}

TEST_CASE("compact parser recognizes spec tokens even without generic prefixes", "[function_calling][parser]") {
  CompactToolCallParser parser(ParseCompactToolCallParserParams(YAML::Load(R"(
    start_function_call_token: ""
    function_token_prefixes: []
    arg_separator: "|"
    stop_markers: []
    function_specs: [{name: ping, token: "<ping>"}]
  )")));
  REQUIRE(parser.FindStart("ok <ping>").value() == 3);
  REQUIRE_FALSE(parser.FindStart("ok <pong>").has_value());
  REQUIRE(parser.PartialStartLength("ok <pi") == 3);
  REQUIRE(parser.Parse("<ping>").size() == 1);
}

TEST_CASE("compact parser holds back a lone angle bracket until it is resolved", "[function_calling][parser]") {
  auto parser = DefaultParser();
  REQUIRE(parser.PartialStartLength("abc <") == 1);
  REQUIRE(parser.PartialStartLength("abc <start_fun") == 10);
  REQUIRE(parser.PartialStartLength("abc <x>") == 0);
  REQUIRE(parser.FindStart("text <f_get_device_parameter>").value() == 5);
  REQUIRE(parser.FindStart("  <f_get_device_parameter>x").value() == 2);
}

TEST_CASE("compact parser keeps only the listed answer tags in text", "[function_calling][parser][keep_tags]") {
  const auto text = "Включаю: <artist> - <title>.<await_user><|message_sep|>";
  SECTION("by default every tag is cut") {
    REQUIRE(HomeParser()->CleanText(text) == "Включаю:  - .");
  }
  SECTION("listed tags stay, the rest are cut") {
    auto params = HomeParams();
    params.keep_tags = {"<artist>", "<title>"};
    REQUIRE(CompactToolCallParser(params).CleanText(text) == "Включаю: <artist> - <title>.");
    REQUIRE(CompactToolCallParser(params).CleanText("<title><artist><d_light>") == "<title><artist>");
    REQUIRE(CompactToolCallParser(params).CleanText("без тегов") == "без тегов");
  }
  SECTION("read from the config") {
    auto config = YAML::Load(kHomeSpecs);
    config["keep_tags"] = std::vector<std::string>{"<artist>"};
    REQUIRE(ParseCompactToolCallParserParams(config).keep_tags == std::vector<std::string>{"<artist>"});
  }
}

TEST_CASE("compact parser rejects keep_tags that clash with its own markers", "[function_calling][parser][keep_tags]") {
  for (const std::string tag :
       {"artist",
        "<artist",
        "<>",
        "<start_function_call>",
        "<arg_sep>",
        "</s>",
        "<|message_sep|>",
        "<f_set_device_parameter>",
        "<f_new>",
        "<function_x>"}) {
    auto params = HomeParams();
    params.keep_tags = {tag};
    INFO(tag);
    REQUIRE_THROWS_AS(CompactToolCallParser(params), std::invalid_argument);
  }
}

TEST_CASE(
  "stream filter passes kept tags through text deltas and keeps calls apart", "[function_calling][parser][keep_tags]"
) {
  auto params = HomeParams();
  params.keep_tags = {"<artist>", "<title>"};
  StreamToolCallFilter filter(std::make_shared<CompactToolCallParser>(params));
  std::string text;
  for (const auto* piece : {"Включаю", ":", " ", "<artist>", " -", " ", "<title>", "."}) {
    text += filter.Feed(piece).text;
  }
  const auto tail = filter.Finish(true);
  REQUIRE(text + tail.text == "Включаю: <artist> - <title>.");
  REQUIRE(tail.calls.empty());
  REQUIRE_FALSE(tail.malformed_call);
}

TEST_CASE(
  "kept tags: split across pieces, before a call and inside call arguments", "[function_calling][parser][keep_tags]"
) {
  auto params = HomeParams();
  params.keep_tags = {"<artist>", "<title>"};
  const auto run = [](const CompactToolCallParserParams& p, const std::vector<std::string>& pieces) {
    StreamToolCallFilter filter(std::make_shared<CompactToolCallParser>(p));
    StreamFilterOutput all;
    for (const auto& piece : pieces) {
      auto out = filter.Feed(piece);
      all.text += out.text;
      all.calls.insert(all.calls.end(), out.calls.begin(), out.calls.end());
    }
    auto tail = filter.Finish(true);
    all.text += tail.text;
    all.calls.insert(all.calls.end(), tail.calls.begin(), tail.calls.end());
    all.malformed_call = tail.malformed_call;
    return all;
  };
  SECTION("a tag cut into pieces stays whole in the text") {
    REQUIRE(run(params, {"Включаю: <", "art", "ist> - <ti", "tle>."}).text == "Включаю: <artist> - <title>.");
  }
  SECTION("a tag right before the call start stays in the text, the call is parsed") {
    const auto out = run(
      params,
      {"Включаю <artist>",
       "<start_function_call>",
       "<f_get_device_parameter>",
       "<d_light>0",
       "<arg_sep>",
       "<p_rw_on_off>"}
    );
    REQUIRE(out.text == "Включаю <artist>");
    REQUIRE(out.calls.size() == 1);
    REQUIRE(out.calls[0].name == "get_device_parameter");
  }
  SECTION("a tag inside call arguments: parsing is the same as without keep_tags") {
    const std::vector<std::string> call = {
      "<start_function_call>",
      "<f_relative_change>",
      "<d_light>0",
      "<arg_sep>",
      "<p_rw_light_brightness>",
      "<arg_sep>",
      "<artist>+"
    };
    const auto kept = run(params, call);
    const auto plain = run(HomeParams(), call);
    REQUIRE(kept.calls.size() == plain.calls.size());
    REQUIRE(kept.malformed_call == plain.malformed_call);
    for (size_t i = 0; i < kept.calls.size(); ++i) {
      REQUIRE(kept.calls[i].name == plain.calls[i].name);
      REQUIRE(kept.calls[i].arguments_json == plain.calls[i].arguments_json);
    }
    REQUIRE(kept.text == plain.text);
  }
  SECTION("keep_tags must be a list") {
    auto config = YAML::Load(kHomeSpecs);
    config["keep_tags"] = "<artist>";
    REQUIRE_THROWS_AS(ParseCompactToolCallParserParams(config), std::invalid_argument);
  }
}

TEST_CASE(
  "kept tags: a bare «<» does not eat the mask; registry and null config", "[function_calling][parser][keep_tags]"
) {
  auto config = YAML::Load(kHomeSpecs);
  config["keep_tags"] = std::vector<std::string>{"<artist>", "<title>"};
  const auto parser = ToolCallParserRegistry::Create("compact_tool_call_v1", config);
  REQUIRE(parser->CleanText("a < b <artist>") == "a < b <artist>");
  REQUIRE(parser->CleanText("Включаю: <artist> - <title>.<d_light>") == "Включаю: <artist> - <title>.");
  // Without keep_tags everything from "<" to ">" is cut out.
  REQUIRE(HomeParser()->CleanText("a < b <artist>") == "a ");
  auto null_keep = YAML::Load(kHomeSpecs);
  null_keep["keep_tags"] = YAML::Node(YAML::NodeType::Null);
  REQUIRE_THROWS_AS(ParseCompactToolCallParserParams(null_keep), std::invalid_argument);
}

namespace {

const std::string kBatch
  = "<start_function_call><f_set_device_parameter><d_light>0<arg_sep><p_rw_light_brightness><arg_sep>40%"
    "<start_function_call><f_set_device_parameter><d_light>1<arg_sep><p_rw_on_off><arg_sep>false"
    "<start_function_call><f_get_device_parameter><d_sensor_temp>0<arg_sep><p_ro_temperature>";

std::shared_ptr<CompactToolCallParser> BatchParser() {
  auto params = HomeParams();
  params.parallel_tool_calls = true;
  params.function_specs.push_back(
    {.name = "get_date",
     .token = "<f_get_date>",
     .args = {{.name = "time_zone", .format = {.as_string = true}, .optional = true}}}
  );
  params.function_specs.push_back(
    {.name = "note",
     .token = "<f_note>",
     .args = {{.name = "text"}, {.name = "tag", .optional = true}, {.name = "when", .optional = true}}}
  );
  return std::make_shared<CompactToolCallParser>(std::move(params));
}

StreamFilterOutput FeedWhole(
  const std::shared_ptr<CompactToolCallParser>& parser, const std::string& text, bool natural
) {
  StreamToolCallFilter filter(parser);
  auto out = filter.Feed(text);
  auto tail = filter.Finish(natural);
  out.calls.insert(out.calls.end(), tail.calls.begin(), tail.calls.end());
  out.text += tail.text;
  out.incomplete_call = tail.incomplete_call;
  out.malformed_call = tail.malformed_call;
  return out;
}

} // namespace

TEST_CASE("compact parser in batch mode needs the start token", "[function_calling][parser][batch]") {
  auto params = HomeParams();
  params.parallel_tool_calls = true;
  params.start_function_call_token.clear();
  REQUIRE_THROWS_AS(CompactToolCallParser(params), std::invalid_argument);
  params.parallel_tool_calls = false;
  REQUIRE_NOTHROW(CompactToolCallParser(params));
}

TEST_CASE("compact parser in batch mode reads every call of one generation", "[function_calling][parser][batch]") {
  REQUIRE(ParseCompactToolCallParserParams(YAML::Load(std::string(kHomeSpecs) + "  parallel_tool_calls: true\n"))
            .parallel_tool_calls);
  const auto out = FeedWhole(BatchParser(), kBatch + "<|message_sep|>", true);
  REQUIRE_FALSE(out.malformed_call);
  REQUIRE(out.text.empty());
  REQUIRE(out.calls.size() == 3);
  REQUIRE(
    out.calls[0].arguments_json == R"({"device_id":"light_0","parameter_name":"light_brightness","value":"40%"})"
  );
  REQUIRE(out.calls[1].arguments_json == R"({"device_id":"light_1","parameter_name":"on_off","value":false})");
  REQUIRE(out.calls[2].name == "get_device_parameter");
  REQUIRE(out.calls[2].arguments_json == R"({"device_id":"sensor_temp_0","parameter_name":"temperature"})");
  // The history gets the canonical form: the parameter is always p_rw.
  REQUIRE(
    out.calls[0].raw_text
    == "<start_function_call><f_set_device_parameter><d_light>0<arg_sep><p_rw_light_brightness><arg_sep>40%"
  );
  REQUIRE(
    out.calls[1].raw_text
    == "<start_function_call><f_set_device_parameter><d_light>1<arg_sep><p_rw_on_off><arg_sep>false"
  );
  REQUIRE(
    out.calls[2].raw_text == "<start_function_call><f_get_device_parameter><d_sensor_temp>0<arg_sep><p_rw_temperature>"
  );
}

TEST_CASE("compact parser in batch mode: canonical slots", "[function_calling][parser][batch]") {
  // A missing separator and an unclosed tag are restored; an absent optional argument is an empty slot.
  const auto out = FeedWhole(
    BatchParser(),
    "<start_function_call><f_set_device_parameter><d_light>0<p_ro_on_off<arg_sep> true "
    "<start_function_call><f_get_date>"
    "<start_function_call><f_get_date>Москва",
    true
  );
  REQUIRE(out.calls.size() == 3);
  REQUIRE(
    out.calls[0].raw_text
    == "<start_function_call><f_set_device_parameter><d_light>0<arg_sep><p_rw_on_off><arg_sep>true"
  );
  REQUIRE(out.calls[1].arguments_json == "{}");
  REQUIRE(out.calls[1].raw_text == "<start_function_call><f_get_date>");
  REQUIRE(out.calls[2].arguments_json == R"({"time_zone":"Москва"})");
  REQUIRE(out.calls[2].raw_text == "<start_function_call><f_get_date>Москва");
}

TEST_CASE("compact parser in batch mode skips calls it cannot read", "[function_calling][parser][batch]") {
  const auto out = FeedWhole(
    BatchParser(),
    "<start_function_call><f_unknown><d_light>0"
    "<start_function_call><f_get_device_parameter><d_light>0"
    "<start_function_call><f_get_device_parameter><d_light>0<arg_sep><p_rw_on_off><arg_sep>extra<arg_sep>more"
    "<start_function_call><f_set_device_parameter><d_light>2<arg_sep><p_rw_on_off><arg_sep>true",
    true
  );
  REQUIRE_FALSE(out.malformed_call);
  REQUIRE(out.calls.size() == 1);
  REQUIRE(out.calls[0].arguments_json == R"({"device_id":"light_2","parameter_name":"on_off","value":true})");

  const auto none = FeedWhole(BatchParser(), "<start_function_call><f_unknown><start_function_call><f_get_date", true);
  REQUIRE(none.calls.empty());
  REQUIRE(none.malformed_call);
}

TEST_CASE(
  "compact parser in batch mode: exact repeats stay, a cut batch is incomplete", "[function_calling][parser][batch]"
) {
  // Repeats are removed by the guard: the parser returns everything that was generated.
  const auto twice = FeedWhole(
    BatchParser(), "<start_function_call><f_get_date><start_function_call><f_get_date>", true
  );
  REQUIRE(twice.calls.size() == 2);

  for (size_t cut = 1; cut <= kBatch.size(); ++cut) {
    INFO(cut);
    const auto out = FeedWhole(BatchParser(), kBatch.substr(0, cut), false);
    REQUIRE(out.calls.empty());
    REQUIRE(out.incomplete_call);
  }
}

TEST_CASE(
  "compact parser in batch mode: text before, a call without the start token, empty slot in the middle",
  "[function_calling][parser][batch]"
) {
  auto out = FeedWhole(
    BatchParser(), "Сейчас. <start_function_call><f_get_date><start_function_call><f_get_date>Москва", true
  );
  REQUIRE(out.text == "Сейчас. ");
  REQUIRE(out.calls.size() == 2);

  out = FeedWhole(BatchParser(), "<f_get_date><start_function_call><f_get_date>Москва", true);
  REQUIRE(out.calls.size() == 2);
  REQUIRE(out.calls[0].raw_text == "<start_function_call><f_get_date>");
  REQUIRE(out.calls[1].arguments_json == R"({"time_zone":"Москва"})");

  out = FeedWhole(
    BatchParser(),
    "<start_function_call><f_note>купить<arg_sep><arg_sep>завтра<start_function_call><f_note>позвонить",
    true
  );
  REQUIRE(out.calls.size() == 2);
  REQUIRE(out.calls[0].raw_text == "<start_function_call><f_note>купить<arg_sep><arg_sep>завтра");
  REQUIRE(out.calls[1].arguments_json == R"({"text":"позвонить"})");
  REQUIRE(out.calls[1].raw_text == "<start_function_call><f_note>позвонить");
}

TEST_CASE(
  "compact parser in batch mode: canonical text of a call parses back to the same call",
  "[function_calling][parser][batch]"
) {
  const auto parser = BatchParser();
  for (const std::string call : {
         "<start_function_call><f_note>позвонить",
         "<start_function_call><f_note>позвонить<arg_sep>",
         "<start_function_call><f_note>позвонить<arg_sep>работа",
         "<start_function_call><f_note>позвонить<arg_sep><arg_sep>завтра",
         "<start_function_call><f_get_date>",
         "<start_function_call><f_get_date>Москва",
         "<start_function_call><f_set_device_parameter><d_light>0<p_ro_on_off> true",
       }) {
    INFO(call);
    const auto first = parser->Parse(call);
    REQUIRE(first.size() == 1);
    const auto again = parser->Parse(first[0].raw_text);
    REQUIRE(again.size() == 1);
    REQUIRE(again[0].name == first[0].name);
    REQUIRE(again[0].arguments_json == first[0].arguments_json);
    REQUIRE(again[0].raw_text == first[0].raw_text);
  }
  REQUIRE(
    parser->Parse("<start_function_call><f_note>позвонить<arg_sep>")[0].raw_text
    == "<start_function_call><f_note>позвонить<arg_sep>"
  );
}

TEST_CASE(
  "compact parser without batch mode runs only the first call with its generated bytes",
  "[function_calling][parser][batch]"
) {
  // A model without parallel calls: one call, raw_text is the generated bytes.
  for (const std::string single :
       {"<start_function_call><f_get_device_parameter><d_sensor_temp>0<arg_sep><p_ro_temperature><|message_sep|>",
        "<start_function_call><f_set_device_parameter><d_light>0<arg_sep><p_rw_on_off><arg_sep>true",
        "<f_relative_change><d_light>0<arg_sep><p_rw_brightness<arg_sep><+10>"}) {
    INFO(single);
    const auto out = FeedWhole(HomeParser(), single, true);
    REQUIRE(out.calls.size() == 1);
    REQUIRE(out.calls[0].raw_text == single);
  }
  // A batch from a parallel model without the flag: only the first call, the last argument does not swallow the rest.
  const auto out = FeedWhole(HomeParser(), kBatch, true);
  REQUIRE_FALSE(out.malformed_call);
  REQUIRE(out.calls.size() == 1);
  REQUIRE(out.calls[0].name == "set_device_parameter");
  REQUIRE(
    out.calls[0].arguments_json == R"({"device_id":"light_0","parameter_name":"light_brightness","value":"40%"})"
  );
  REQUIRE(out.calls[0].raw_text == kBatch);

  const std::string single = "<start_function_call><f_get_device_parameter><d_sensor_temp>0<arg_sep><p_ro_temperature>";
  REQUIRE(BatchParser()->Parse(single).front().arguments_json == HomeParser()->Parse(single).front().arguments_json);
}

namespace {

struct Sourced {
  std::vector<ToolCall> calls;
  // The pieces of each call's raw_text taken from the stream.
  std::vector<std::vector<std::string>> pieces;
};

Sourced FeedWithSources(const std::shared_ptr<CompactToolCallParser>& parser, const std::string& text) {
  StreamToolCallFilter filter(parser);
  auto out = filter.Feed(text);
  auto tail = filter.Finish(true);
  out.calls.insert(out.calls.end(), tail.calls.begin(), tail.calls.end());
  out.sources.insert(out.sources.end(), tail.sources.begin(), tail.sources.end());
  REQUIRE(out.calls.size() == out.sources.size());
  Sourced sourced{out.calls, {}};
  for (size_t i = 0; i < out.calls.size(); ++i) {
    auto& pieces = sourced.pieces.emplace_back();
    for (const auto& piece : out.sources[i].pieces) {
      const size_t from = out.sources[i].offset + piece.call_begin;
      REQUIRE(from + piece.size <= text.size());
      REQUIRE(out.calls[i].raw_text.compare(piece.raw_begin, piece.size, text, from, piece.size) == 0);
      pieces.push_back(out.calls[i].raw_text.substr(piece.raw_begin, piece.size));
    }
  }
  return sourced;
}

} // namespace

TEST_CASE(
  "compact parser: raw text of a single call comes from the stream verbatim", "[function_calling][parser][sources]"
) {
  const std::string call = "<start_function_call><f_set_device_parameter><d_light>0<arg_sep><p_ro_on_off><arg_sep>true";
  const auto sourced = FeedWithSources(HomeParser(), "ок " + call + "<|message_sep|>");
  REQUIRE(sourced.calls.size() == 1);
  REQUIRE(sourced.calls[0].raw_text == call + "<|message_sep|>");
  REQUIRE(sourced.pieces[0] == std::vector<std::string>{call + "<|message_sep|>"});
}

TEST_CASE(
  "compact parser in batch mode: only plain values come from the stream", "[function_calling][parser][sources]"
) {
  const auto sourced = FeedWithSources(
    BatchParser(),
    "<start_function_call><f_set_device_parameter><d_light>0<p_ro_on_off<arg_sep> true "
    "<start_function_call><f_relative_change><d_light>1<arg_sep><p_rw_light_brightness><arg_sep> <+10> "
    "<start_function_call><f_get_date>"
  );
  REQUIRE(sourced.calls.size() == 3);
  // Tags are rebuilt from the config: p_ro -> p_rw, an unclosed one is closed, the index goes after the tag.
  REQUIRE(
    sourced.calls[0].raw_text
    == "<start_function_call><f_set_device_parameter><d_light>0<arg_sep><p_rw_on_off><arg_sep>true"
  );
  REQUIRE(sourced.pieces[0] == std::vector<std::string>{"true"});
  REQUIRE(sourced.pieces[1] == std::vector<std::string>{"+10"});
  REQUIRE(sourced.pieces[2].empty());
}
