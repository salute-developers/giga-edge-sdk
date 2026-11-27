#include "giga_edge/modules/assistant_core/request_profile.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/assistant_core/nodes.h"

#include <catch.hpp>

#include <algorithm>

using namespace giga_edge;

namespace {

class TagProfile : public IRequestProfile {
public:
  explicit TagProfile(std::string tag)
    : tag_(std::move(tag)) {}

  std::string SystemPrompt(const std::vector<std::string>& instructions) const override {
    std::string prompt = tag_;
    for (const auto& instruction : instructions) {
      prompt += "|" + instruction;
    }
    return prompt;
  }

  std::string UserMessage(const std::vector<std::string>& instructions, const std::string& text) const override {
    return std::to_string(instructions.size()) + ":" + text;
  }

private:
  std::string tag_;
};

const bool kTagRegistered = RequestProfileRegistry::Register("test_tag", [](const YAML::Node& params) {
  return std::make_shared<TagProfile>(params["tag"].as<std::string>("tag"));
});

UserQuery Query(const std::string& text) {
  return UserQuery{.request_id = text.empty() ? "" : "r1", .text = text};
}

} // namespace

TEST_CASE("request profiles are created by name with their params", "[assistant][profile]") {
  REQUIRE(kTagRegistered);
  REQUIRE(RequestProfileRegistry::Has("test_tag"));
  const auto names = RequestProfileRegistry::Names();
  REQUIRE(std::find(names.begin(), names.end(), "test_tag") != names.end());
  REQUIRE(RequestProfileRegistry::Create("test_tag", YAML::Load("tag: X"))->SystemPrompt({"a"}) == "X|a");
  REQUIRE_THROWS(RequestProfileRegistry::Create("no_such_profile", YAML::Node()));
  REQUIRE_THROWS(RequestProfileRegistry::Register("test_tag", [](const YAML::Node&) {
    return std::make_shared<TagProfile>("again");
  }));
}

TEST_CASE("builder: a request profile renders instructions into both messages", "[assistant][node][builder]") {
  for (const auto* params : {"profile: test_tag", "profile: {name: test_tag, tag: S}"}) {
    INFO(params);
    auto ctx = CreateNode(
      "dialogue_request_builder", std::string(params) + "\ninclude_scene: false", {}, {"instructions"}
    );
    const std::string tag = std::string(params).find("tag: S") != std::string::npos ? "S" : "tag";

    NodePush(ctx, "instructions", std::vector<std::string>{"a", "b"}, true, 0);
    NodePush(ctx, "user_query", Query("привет"));
    auto request = NodeGet<AssistantRequest>(ctx, "assistant_request");
    REQUIRE(request.messages.size() == 2);
    REQUIRE(request.messages[0].content == tag + "|a|b");
    REQUIRE(request.messages[1].content == "2:привет");

    NodePush(ctx, "instructions", std::vector<std::string>{}, true, 0);
    NodePush(ctx, "user_query", Query("привет"));
    request = NodeGet<AssistantRequest>(ctx, "assistant_request");
    REQUIRE(request.messages[0].content == tag);
    REQUIRE(request.messages[1].content == "0:привет");

    NodePush(ctx, "instructions", std::vector<std::string>{"a"}, true, 0);
    NodePush(ctx, "user_query", Query(""));
    REQUIRE(NodeGet<AssistantRequest>(ctx, "assistant_request").kind == AssistantRequestKind::NONE);
  }
}

TEST_CASE("builder: an explicit system prompt wins over the profile", "[assistant][node][builder]") {
  auto ctx = CreateNode(
    "dialogue_request_builder", "profile: test_tag\nsystem_prompt: fixed\ninclude_scene: false", {}, {"instructions"}
  );
  NodePush(ctx, "instructions", std::vector<std::string>{"a"}, true, 0);
  NodePush(ctx, "user_query", Query("q"));
  const auto request = NodeGet<AssistantRequest>(ctx, "assistant_request");
  REQUIRE(request.messages[0].content == "fixed");
  REQUIRE(request.messages[1].content == "1:q");
}

TEST_CASE("builder: an unknown profile is a config error", "[assistant][node][builder]") {
  REQUIRE_THROWS(CreateNode("dialogue_request_builder", "profile: unknown_v1", {}, {"instructions"}));
}
