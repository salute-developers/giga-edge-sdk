#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/fst/nodes/float_grammar/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Float grammar node", "[fst][nodes]") {
  auto node = CreateNode(
    "float_grammar",
    R"(
      fst_rule: float_grammar.fst
    )",
    {
      {"float_grammar.fst", ReadFileTo<IDataSpanPtr>("data/float_grammar/float_grammar.fst")},
    }
  );

  NodePush(node, "text", std::string{"один два три"});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{1.0, 2.0, 3.0}));

  NodePush(node, "text", std::string{"вектор"});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{7.49, 8.111}));

  NodePush(node, "text", std::string{""});
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));
}

namespace giga_edge {

inline bool operator==(const FloatGrammarNodeParams& lhs, const FloatGrammarNodeParams& rhs) {
  return lhs.fst_rule == rhs.fst_rule;
}

} // namespace giga_edge

TEST_CASE("Float grammar, config parser", "[fst][nodes]") {
  REQUIRE(
    ParseConfig<FloatGrammarNodeParams>(YAML::Load("fst_rule: float_grammar.fst\n"))
    == FloatGrammarNodeParams{
      .fst_rule = "float_grammar.fst",
    }
  );
}