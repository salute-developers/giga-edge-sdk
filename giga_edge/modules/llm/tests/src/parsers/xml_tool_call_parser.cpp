#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/llm/parsers/tool_call_parsers.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("xml parser is registered and parses qwen style calls", "[llm][parser]") {
  REQUIRE(ToolCallParserRegistry::Has("xml_tool_call"));
  auto parser = ToolCallParserRegistry::Create("xml_tool_call", YAML::Node());
  StreamToolCallFilter filter(parser);
  std::string text;
  std::vector<ToolCall> calls;
  for (const char* delta :
       {"Sure. <tool_",
        "call>\n{\"name\": \"send\", \"arguments\": {\"to\": \"x\", \"n\": {\"a\": 1}}}\n</tool_",
        "call> tail"}) {
    auto out = filter.Feed(delta);
    text += out.text;
    for (auto& call : out.calls) {
      calls.push_back(std::move(call));
    }
  }
  auto tail = filter.Finish();
  text += tail.text;
  REQUIRE(text == "Sure.  tail");
  REQUIRE(calls.size() == 1);
  REQUIRE(calls[0].name == "send");
  REQUIRE(calls[0].arguments_json == R"({"to": "x", "n": {"a": 1}})");
  REQUIRE(calls[0].raw_text.rfind("<tool_call>", 0) == 0);
}

TEST_CASE("xml parser closes an unterminated call at the end and keeps markers configurable", "[llm][parser]") {
  auto parser = ToolCallParserRegistry::Create("xml_tool_call", YAML::Load("call_open: '<c>'\ncall_close: '</c>'"));
  StreamToolCallFilter filter(parser);
  auto out = filter.Feed("<c>{\"name\": \"ping\"}");
  REQUIRE(out.calls.empty());
  auto tail = filter.Finish();
  REQUIRE(tail.calls.size() == 1);
  REQUIRE(tail.calls[0].name == "ping");
  REQUIRE(tail.calls[0].arguments_json.empty());
}

TEST_CASE("xml parser flags a nameless call span as malformed", "[llm][parser]") {
  auto parser = ToolCallParserRegistry::Create("xml_tool_call", YAML::Node());
  StreamToolCallFilter filter(parser);
  auto out = filter.Feed("<tool_call>garbage</tool_call>");
  REQUIRE(out.calls.empty());
  REQUIRE(out.text.empty());
  REQUIRE(filter.Finish().malformed_call);
}

namespace {

StreamFilterOutput ParseWhole(const std::string& text) {
  StreamToolCallFilter filter(ToolCallParserRegistry::Create("xml_tool_call", YAML::Node()));
  auto out = filter.Feed(text);
  auto tail = filter.Finish();
  out.calls.insert(out.calls.end(), tail.calls.begin(), tail.calls.end());
  out.malformed_call = tail.malformed_call;
  return out;
}

} // namespace

TEST_CASE("xml parser reads the name of the call from the top level only", "[llm][parser]") {
  const auto out = ParseWhole(R"(<tool_call>{"arguments": {"name": "Маша"}, "name": "call_contact"}</tool_call>)");
  REQUIRE(out.calls.size() == 1);
  REQUIRE(out.calls[0].name == "call_contact");
  REQUIRE(out.calls[0].arguments_json == R"({"name": "Маша"})");
}

TEST_CASE("xml parser keeps the arguments as the model wrote them", "[llm][parser]") {
  const auto out = ParseWhole(
    R"(<tool_call>{"name":"send","arguments":{ "q" : "\u043c" ,"n":[1, 2.50] }}</tool_call>)"
  );
  REQUIRE(out.calls.size() == 1);
  REQUIRE(out.calls[0].arguments_json == R"({ "q" : "\u043c" ,"n":[1, 2.50] })");
}

TEST_CASE("xml parser drops a call left open at the end of the turn", "[llm][parser]") {
  // The end of the turn (EOS, a stop sequence) falls inside the JSON: a "send" call with empty arguments must not
  // go out.
  StreamToolCallFilter filter(ToolCallParserRegistry::Create("xml_tool_call", YAML::Node()));
  REQUIRE(filter.Feed(R"(<tool_call>{"name": "send", "arguments": {"to": "x)").calls.empty());
  const auto tail = filter.Finish();
  REQUIRE(tail.calls.empty());
  REQUIRE(tail.malformed_call);
}

TEST_CASE("xml parser takes only strict JSON with a top-level name and object arguments", "[llm][parser]") {
  for (const char* text :
       {R"(<tool_call>{"arguments": {"name": "Маша"}}</tool_call>)",
        R"(<tool_call>{"name": "send", "arguments": {"to": </tool_call>)",
        R"(<tool_call>{"name": "send"} tail</tool_call>)",
        R"(<tool_call>{"name": "send", "arguments": "{}"}</tool_call>)",
        R"(<tool_call>{"name": "", "arguments": {}}</tool_call>)",
        R"(<tool_call>{"name": 5, "arguments": {}}</tool_call>)",
        R"(<tool_call>["send"]</tool_call>)"}) {
    INFO(text);
    const auto out = ParseWhole(text);
    REQUIRE(out.calls.empty());
    REQUIRE(out.malformed_call);
  }
}
