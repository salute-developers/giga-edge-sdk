#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_output.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"

#include <catch.hpp>

using namespace giga_edge;

namespace {

class TestOptionalOutputNode : public NodeBase {
public:
  using NodeBase::NodeBase;

  void Reset() override {}

  bool ExtraOutputSeenWhileProcessing() const {
    return extra_output_seen_;
  }

  REGISTER_INPUT(test_optional_output_node, input, Input, int32_t, "");
  REGISTER_OUTPUT(test_optional_output_node, required, Required, int32_t, "");
  REGISTER_OPTIONAL_OUTPUT(test_optional_output_node, extra, Extra, int32_t, "");

private:
  void ProcessOnce() override {
    extra_output_seen_ = HasExtraOutput();
    PushRequired(GetInput() + 1);
    PushExtra(GetInput() * 2);
  }

  bool extra_output_seen_ = false;
};

REGISTER_NODE(test_optional_output_node, TestOptionalOutputNode, "");

} // namespace

TEST_CASE("Optional output: node works without mapping it", "[runtime][optional_output]") {
  auto node = CreateNode("test_optional_output_node", "");

  REQUIRE(node.outputs.find("extra") == node.outputs.end());

  // PushExtra is called unconditionally inside ProcessOnce and must be a no-op here.
  NodePush(node, "input", 3);
  REQUIRE(NodeGet<int32_t>(node, "required") == 4);
  REQUIRE_FALSE(std::static_pointer_cast<TestOptionalOutputNode>(node.node)->ExtraOutputSeenWhileProcessing());
}

TEST_CASE("Optional output: mapped output receives values", "[runtime][optional_output]") {
  auto node = CreateNode("test_optional_output_node", "", {}, {}, {}, {}, nullptr, {"extra"});

  NodePush(node, "input", 3);
  REQUIRE(NodeGet<int32_t>(node, "required") == 4);
  REQUIRE(NodeGet<int32_t>(node, "extra") == 6);
  REQUIRE(std::static_pointer_cast<TestOptionalOutputNode>(node.node)->ExtraOutputSeenWhileProcessing());
}

TEST_CASE("Optional output: required output is still validated", "[runtime][optional_output]") {
  NodeOutputPtr events = std::make_shared<NodeOutput>("events");
  CommonNodeParams common_node_params{};

  NodeBaseConfig config{
    .submodel_version_chain = {},
    .events = events,
    .enqueue_callback = [](NodePtr) {},
    .common_node_params = common_node_params,
    .next_seq_id = 0,
    .environments = nullptr,
    .base_id = "test_optional_output_node",
    .inputs = {{"input", std::make_shared<NodeOutput>("input")}},
    .outputs = {},
    .priority = 0,
  };

  REQUIRE_THROWS_WITH(
    NodeRegistry::CreateNode("test_optional_output_node", config, nullptr, YAML::Node()),
    Catch::Contains("Missing") && Catch::Contains("required")
  );
}
