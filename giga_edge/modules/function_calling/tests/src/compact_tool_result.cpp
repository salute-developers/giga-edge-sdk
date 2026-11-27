#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/function_calling/nodes/fc_tool_result/node.h"

#include <catch.hpp>

using namespace giga_edge;

namespace {

CompactResultFormat Format() {
  return ParseCompactResultFormat(YAML::Load(R"(
    success: '{"status": "success"}'
    success_with_value: '{"status": "success", "value": {value}}'
    failure: '{"status": "fail", "error": {error}}'
  )"));
}

} // namespace

TEST_CASE("compact result follows the configured texts and keeps correlation", "[function_calling][result]") {
  CompactToolResult input{.call_id = "r1/tool/1", .name = "get_device_parameter"};
  auto result = FormatCompactToolResult(input, Format());
  REQUIRE(result.call_id == input.call_id);
  REQUIRE(result.name == input.name);
  REQUIRE(result.is_ready);
  REQUIRE(result.content == R"({"status": "success"})");
  input.value_json = "70";
  REQUIRE(FormatCompactToolResult(input, Format()).content == R"({"status": "success", "value": 70})");
  input.value_json = "93.4";
  REQUIRE(FormatCompactToolResult(input, Format()).content == R"({"status": "success", "value": 93.4})");
  input.value_json = R"("тёплый")";
  REQUIRE(FormatCompactToolResult(input, Format()).content == R"({"status": "success", "value": "тёплый"})");
  input.content = R"({"status": "success", "artist": "Кино", "title": "Кукушка"})";
  REQUIRE(FormatCompactToolResult(input, Format()).content == input.content);
  input.content.clear();
  input.value_json = "{} garbage";
  REQUIRE(
    FormatCompactToolResult(input, Format()).content == R"({"status": "fail", "error": "Invalid tool result JSON"})"
  );
  input.is_error = true;
  input.error = "offline\n\"lamp\"";
  result = FormatCompactToolResult(input, Format());
  REQUIRE(result.content == R"({"status": "fail", "error": "offline\n\"lamp\""})");
  REQUIRE(result.status == AssistantStatus::OK);

  const auto other = ParseCompactResultFormat(
    YAML::Load("{success: ok, success_with_value: 'v={value}', failure: 'e={error}'}")
  );
  input.is_error = false;
  input.value_json = "1";
  REQUIRE(FormatCompactToolResult(input, other).content == "v=1");
  REQUIRE_THROWS(ParseCompactResultFormat(YAML::Load("success: ok")));
}

TEST_CASE(
  "compact result node retains every completion and reset drops pending results", "[function_calling][result]"
) {
  auto node = CreateNode("fc_tool_result", "{success: ok, success_with_value: '{value}', failure: '{error}'}");
  NodePush(node, "tool_results", CompactToolResults{{.call_id = "one"}, {.call_id = "two"}});
  REQUIRE(NodeGet<ToolResult>(node, "tool_result").call_id == "one");
  NodePush(node, "tool_results", CompactToolResults{});
  REQUIRE(NodeGet<ToolResult>(node, "tool_result").call_id == "two");
  NodePush(node, "tool_results", CompactToolResults{});
  REQUIRE_FALSE(NodeGet<ToolResult>(node, "tool_result").is_ready);
  NodePush(node, "tool_results", CompactToolResults{{.call_id = "one"}, {.call_id = "two"}});
  NodeGet<ToolResult>(node, "tool_result");
  node.node->Reset();
  NodePush(node, "tool_results", CompactToolResults{});
  REQUIRE_FALSE(NodeGet<ToolResult>(node, "tool_result").is_ready);
}

TEST_CASE("compact result rounds decimal JSON values without binary tails", "[function_calling][result]") {
  auto format = Format();
  format.success_with_value = "{value}";
  struct Example {
    const char* input;
    int decimals;
    const char* expected;
  };
  for (const auto& sample : std::vector<Example>{
         {"33.0", 2, "33"},
         {"35.630000000000003", 2, "35.63"},
         {"21.999", 2, "22"},
         {"93.4", 2, "93.4"},
         {"12.3456", 0, "12"},
         {"12.3456", 1, "12.3"},
         {"12.3456", 2, "12.35"},
         {"12.3456", 3, "12.346"},
         {"-12.3456", 3, "-12.346"},
         {"0.005", 2, "0.01"},
         {"2.675", 2, "2.68"},
         {"1.005", 2, "1.01"},
         {"-0.004", 2, "0"},
         {"-0.005", 2, "-0.01"},
         {"-2.675", 2, "-2.68"},
         {"-0.0", 2, "0"},
         {"0.0", 0, "0"},
         {"-0.5", 0, "-1"},
         {"2.5", 0, "3"},
         {"5e-1", 2, "0.5"},
         {"9.99", 1, "10"},
         {"1.25", 1, "1.3"},
         {"-1.25", 1, "-1.3"},
         {"0.0049", 2, "0"},
         {"9007199254740993.0", 2, "9007199254740993"},
         {"18446744073709551615.0", 2, "18446744073709551615"},
         {"1.25e+20", 2, "125000000000000000000"},
         {"1.25E2", 2, "125"},
         {"5e-3", 2, "0.01"},
         {"0.005e3", 2, "5"},
         {"0.5e1", 2, "5"},
         {"-0.125e2", 2, "-12.5"},
         {"1e-300", 2, "0"},
         {"-1e-300", 2, "0"},
         {"1e300", 2, "1e300"},
         {"-1.7976931348623157e+308", 2, "-1.7976931348623157e+308"},
         {"1e-130", 200, "1e-130"},
         {"0.125", 2147483647, "0.125"},
       }) {
    CAPTURE(sample.input, sample.decimals);
    format.value_decimals = sample.decimals;
    REQUIRE(FormatCompactToolResult({.value_json = sample.input}, format).content == sample.expected);
  }
  format.value_decimals = 2;
  REQUIRE(FormatCompactToolResult({.value_json = "1e127"}, format).content == "1" + std::string(127, '0'));
  REQUIRE(FormatCompactToolResult({.value_json = "1e128"}, format).content == "1e128");
}

TEST_CASE("compact result preserves JSON types and rounds nested real values", "[function_calling][result]") {
  auto format = Format();
  format.success_with_value = "{value}";
  for (const auto* json :
       {"33",
        "-42",
        "18446744073709551615",
        "true",
        "false",
        "null",
        R"("35.630000000000003")",
        R"({ "x": [1, true, null, "2.675", "a\\\"35.635"] })",
        "[]",
        "{}"}) {
    CAPTURE(json);
    REQUIRE(FormatCompactToolResult({.value_json = json}, format).content == json);
  }
  REQUIRE(
    FormatCompactToolResult({.value_json = R"({ "2.675": [35.630000000000003, {"n": -0.004}, 33.0, 7] })"}, format)
      .content
    == R"({ "2.675": [35.63, {"n": 0}, 33, 7] })"
  );
  REQUIRE(FormatCompactToolResult({.value_json = " \n35.630000000000003\t"}, format).content == " \n35.63\t");
}

TEST_CASE("compact result node validates and applies value_decimals", "[function_calling][result]") {
  REQUIRE(Format().value_decimals == 2);
  for (int decimals : {0, 1, 2, 3}) {
    const auto config = "{success: ok, success_with_value: '{value}', failure: '{error}', value_decimals: "
      + std::to_string(decimals) + "}";
    auto node = CreateNode("fc_tool_result", config);
    NodePush(node, "tool_results", CompactToolResults{{.value_json = "1.2345"}});
    REQUIRE(
      NodeGet<ToolResult>(node, "tool_result").content
      == std::vector<std::string>{"1", "1.2", "1.23", "1.235"}[decimals]
    );
  }
  REQUIRE_THROWS_WITH(
    CreateNode(
      "fc_tool_result", "{success: ok, success_with_value: '{value}', failure: '{error}', value_decimals: -1}"
    ),
    Catch::Contains("fc_tool_result: value_decimals must be non-negative")
  );
}
