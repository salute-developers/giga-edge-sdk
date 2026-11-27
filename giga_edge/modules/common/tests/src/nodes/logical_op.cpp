#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/logical_op/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Logical node, AND", "[runtime][nodes]") {
  auto node = CreateNode(
    "logical_op",
    R"(
      logical_op_type: and
    )",
    {},
    {"second_input"}
  );

  NodePush(node, "first_input", true);
  NodePush(node, "second_input", true);
  REQUIRE(NodeGet<bool>(node, "output") == true);

  NodePush(node, "first_input", false);
  NodePush(node, "second_input", true);
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

TEST_CASE("Logical node, OR", "[runtime][nodes]") {
  auto node = CreateNode(
    "logical_op",
    R"(
      logical_op_type: or
    )",
    {},
    {"second_input"}
  );

  NodePush(node, "first_input", true);
  NodePush(node, "second_input", false);
  REQUIRE(NodeGet<bool>(node, "output") == true);

  NodePush(node, "first_input", false);
  NodePush(node, "second_input", false);
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

TEST_CASE("Logical node, NOT", "[runtime][nodes]") {
  auto node = CreateNode(
    "logical_op",
    R"(
      logical_op_type: not
    )"
  );

  NodePush(node, "first_input", true);
  REQUIRE(NodeGet<bool>(node, "output") == false);

  NodePush(node, "first_input", false);
  REQUIRE(NodeGet<bool>(node, "output") == true);
}

namespace giga_edge {

inline bool operator==(const LogicalOpNodeParams& lhs, const LogicalOpNodeParams& rhs) {
  return lhs.logical_op_type == rhs.logical_op_type;
}

} // namespace giga_edge

TEST_CASE("Logistic node, config parser", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<LogicalOpNodeParams>(YAML::Load("logical_op_type: or\n"))
    == LogicalOpNodeParams{
      .logical_op_type = LogicalOpType::OR,
    }
  );
  REQUIRE(
    ParseConfig<LogicalOpNodeParams>(YAML::Load("logical_op_type: and\n"))
    == LogicalOpNodeParams{
      .logical_op_type = LogicalOpType::AND,
    }
  );
  REQUIRE(
    ParseConfig<LogicalOpNodeParams>(YAML::Load("logical_op_type: not\n"))
    == LogicalOpNodeParams{
      .logical_op_type = LogicalOpType::NOT,
    }
  );
}
