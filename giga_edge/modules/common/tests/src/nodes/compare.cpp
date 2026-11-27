#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/compare/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Compare node, EQUAL", "[runtime][nodes]") {
  auto node = CreateNode(
    "compare_op",
    R"(
      compare_op_type: "=="
    )"
  );

  NodePush(node, "first_input", 0.1f);
  NodePush(node, "second_input", 0.1f);
  REQUIRE(NodeGet<bool>(node, "output") == true);

  NodePush(node, "first_input", 0.1f);
  NodePush(node, "second_input", 0.2f);
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

TEST_CASE("Compare node, NOT_EQUAL", "[runtime][nodes]") {
  auto node = CreateNode(
    "compare_op",
    R"(
      compare_op_type: "!="
    )"
  );

  NodePush(node, "first_input", 0.1f);
  NodePush(node, "second_input", 0.2f);
  REQUIRE(NodeGet<bool>(node, "output") == true);

  NodePush(node, "first_input", 0.2f);
  NodePush(node, "second_input", 0.2f);
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

TEST_CASE("Compare node, LESS", "[runtime][nodes]") {
  auto node = CreateNode(
    "compare_op",
    R"(
      compare_op_type: "<"
    )"
  );

  NodePush(node, "first_input", 0.1f);
  NodePush(node, "second_input", 0.2f);
  REQUIRE(NodeGet<bool>(node, "output") == true);

  NodePush(node, "first_input", 0.2f);
  NodePush(node, "second_input", 0.2f);
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

TEST_CASE("Compare node, LESS_EQUAL", "[runtime][nodes]") {
  auto node = CreateNode(
    "compare_op",
    R"(
      compare_op_type: "<="
    )"
  );

  NodePush(node, "first_input", 0.2f);
  NodePush(node, "second_input", 0.2f);
  REQUIRE(NodeGet<bool>(node, "output") == true);

  NodePush(node, "first_input", 0.3f);
  NodePush(node, "second_input", 0.2f);
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

TEST_CASE("Compare node, GREATER", "[runtime][nodes]") {
  auto node = CreateNode(
    "compare_op",
    R"(
      compare_op_type: ">"
    )"
  );

  NodePush(node, "first_input", 0.3f);
  NodePush(node, "second_input", 0.2f);
  REQUIRE(NodeGet<bool>(node, "output") == true);

  NodePush(node, "first_input", 0.2f);
  NodePush(node, "second_input", 0.2f);
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

TEST_CASE("Compare node, GREATER_EQUAL", "[runtime][nodes]") {
  auto node = CreateNode(
    "compare_op",
    R"(
      compare_op_type: ">="
    )"
  );

  NodePush(node, "first_input", 0.2f);
  NodePush(node, "second_input", 0.2f);
  REQUIRE(NodeGet<bool>(node, "output") == true);

  NodePush(node, "first_input", 0.1f);
  NodePush(node, "second_input", 0.2f);
  REQUIRE(NodeGet<bool>(node, "output") == false);
}

namespace giga_edge {

inline bool operator==(const CompareOpNodeParams& lhs, const CompareOpNodeParams& rhs) {
  return lhs.compare_op_type == rhs.compare_op_type;
}

} // namespace giga_edge

TEST_CASE("Compare node, config parser", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<CompareOpNodeParams>(YAML::Load("compare_op_type: \"==\"\n"))
    == CompareOpNodeParams{
      .compare_op_type = CompareOpType::EQUAL,
    }
  );
  REQUIRE(
    ParseConfig<CompareOpNodeParams>(YAML::Load("compare_op_type: \"!=\"\n"))
    == CompareOpNodeParams{
      .compare_op_type = CompareOpType::NOT_EQUAL,
    }
  );
  REQUIRE(
    ParseConfig<CompareOpNodeParams>(YAML::Load("compare_op_type: \">=\"\n"))
    == CompareOpNodeParams{
      .compare_op_type = CompareOpType::GREATER_EQUAL,
    }
  );
  REQUIRE(
    ParseConfig<CompareOpNodeParams>(YAML::Load("compare_op_type: \">\"\n"))
    == CompareOpNodeParams{
      .compare_op_type = CompareOpType::GREATER,
    }
  );
  REQUIRE(
    ParseConfig<CompareOpNodeParams>(YAML::Load("compare_op_type: \"<=\"\n"))
    == CompareOpNodeParams{
      .compare_op_type = CompareOpType::LESS_EQUAL,
    }
  );
  REQUIRE(
    ParseConfig<CompareOpNodeParams>(YAML::Load("compare_op_type: \"<\"\n"))
    == CompareOpNodeParams{
      .compare_op_type = CompareOpType::LESS,
    }
  );
}
