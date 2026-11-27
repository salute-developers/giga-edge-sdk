#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/tool_call_parser.h"

#include <catch.hpp>

#include <string>

using namespace giga_edge;

namespace {

class XmlParser : public IToolCallParser {
public:
  std::optional<size_t> FindStart(std::string_view text) const override {
    const auto at = text.find("<call>");
    return at == std::string_view::npos ? std::nullopt : std::optional<size_t>(at);
  }
  std::optional<size_t> FindEnd(std::string_view call_text, bool is_final) const override {
    const auto at = call_text.find("</call>");
    if (at != std::string_view::npos) {
      return at + 7;
    }
    return is_final ? std::optional<size_t>(call_text.size()) : std::nullopt;
  }
  size_t PartialStartLength(std::string_view text) const override {
    return PartialMarkerSuffix(text, "<call>");
  }
  std::vector<ToolCall> Parse(std::string_view call_text) const override {
    auto body = call_text.substr(6);
    if (body.size() >= 7 && body.substr(body.size() - 7) == "</call>") {
      body = body.substr(0, body.size() - 7);
    }
    if (body.empty()) {
      return {};
    }
    return {ToolCall{.name = std::string(body)}};
  }
  std::string CleanText(std::string_view text) const override {
    return std::string(text);
  }
};

StreamFilterOutput FeedAll(StreamToolCallFilter& filter, std::initializer_list<const char*> deltas) {
  StreamFilterOutput total;
  for (const char* delta : deltas) {
    auto out = filter.Feed(delta);
    total.text += out.text;
    total.raw += out.raw;
    for (auto& call : out.calls) {
      total.calls.push_back(std::move(call));
    }
  }
  auto tail = filter.Finish();
  total.text += tail.text;
  for (auto& call : tail.calls) {
    total.calls.push_back(std::move(call));
  }
  return total;
}

} // namespace

TEST_CASE("filter without parser passes text through", "[assistant][filter]") {
  StreamToolCallFilter filter;
  const auto out = FeedAll(filter, {"hel", "lo"});
  REQUIRE(out.text == "hello");
  REQUIRE(out.raw == "hello");
  REQUIRE(out.calls.empty());
}

TEST_CASE("filter extracts a call split across deltas and keeps surrounding text", "[assistant][filter]") {
  StreamToolCallFilter filter(std::make_shared<XmlParser>());
  const auto out = FeedAll(filter, {"say ", "<ca", "ll>pi", "ng</ca", "ll> done"});
  REQUIRE(out.text == "say  done");
  REQUIRE(out.raw == "say <call>ping</call> done");
  REQUIRE(out.calls.size() == 1);
  REQUIRE(out.calls[0].name == "ping");
  REQUIRE(out.calls[0].raw_text == "<call>ping</call>");
}

TEST_CASE("filter holds back a partial marker and releases it when it turns out to be text", "[assistant][filter]") {
  StreamToolCallFilter filter(std::make_shared<XmlParser>());
  auto out = filter.Feed("a <c");
  REQUIRE(out.text == "a ");
  out = filter.Feed("at");
  REQUIRE(out.text == "<cat");
  out = filter.Finish();
  REQUIRE(out.text.empty());
}

TEST_CASE("filter closes an unterminated call at the end of the stream", "[assistant][filter]") {
  StreamToolCallFilter filter(std::make_shared<XmlParser>());
  const auto out = FeedAll(filter, {"<call>open"});
  REQUIRE(out.text.empty());
  REQUIRE(out.calls.size() == 1);
  REQUIRE(out.calls[0].raw_text == "<call>open");
}

TEST_CASE("partial marker suffix never matches the whole marker", "[assistant][filter]") {
  REQUIRE(PartialMarkerSuffix("abc<ca", "<call>") == 3);
  REQUIRE(PartialMarkerSuffix("abc<call>", "<call>") == 0);
  REQUIRE(PartialMarkerSuffix("abc", "<call>") == 0);
  REQUIRE(PartialMarkerSuffix("", "<call>") == 0);
}

TEST_CASE("parser registry rejects duplicates and unknown names", "[assistant][filter]") {
  REQUIRE_FALSE(ToolCallParserRegistry::Has("no_such_parser"));
  REQUIRE_THROWS(ToolCallParserRegistry::Create("no_such_parser", YAML::Node()));
  REQUIRE(ToolCallParserRegistry::Register("filter_test_parser", [](const YAML::Node&) {
    return std::make_shared<XmlParser>();
  }));
  REQUIRE(ToolCallParserRegistry::Has("filter_test_parser"));
  REQUIRE_THROWS(ToolCallParserRegistry::Register("filter_test_parser", [](const YAML::Node&) {
    return std::make_shared<XmlParser>();
  }));
}
