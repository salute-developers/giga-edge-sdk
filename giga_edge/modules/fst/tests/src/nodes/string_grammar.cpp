#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/fst/nodes/string_grammar/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("String grammar node", "[runtime][nodes]") {
  auto node = CreateNode(
    "string_grammar",
    R"(
      fst_rule: string_grammar.fst
    )",
    {
      {"string_grammar.fst", ReadFileTo<IDataSpanPtr>("data/float_grammar/float_grammar.fst")},
    }
  );

  NodePush(node, "text", std::string{"один два три"});
  REQUIRE(NodeGet<std::string>(node, "output") == "1 2 3");

  NodePush(node, "text", std::string{"вектор"});
  REQUIRE(NodeGet<std::string>(node, "output") == "7.49 8.111");

  NodePush(node, "text", std::string{""});
  REQUIRE(NodeGet<std::string>(node, "output").empty());
}

namespace giga_edge {

inline bool operator==(const StringGrammarNodeParams& lhs, const StringGrammarNodeParams& rhs) {
  return lhs.fst_rule == rhs.fst_rule;
}

} // namespace giga_edge

TEST_CASE("String grammar, config parser", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<StringGrammarNodeParams>(YAML::Load("fst_rule: string_grammar.fst"))
    == StringGrammarNodeParams{
      .fst_rule = "string_grammar.fst",
    }
  );
}