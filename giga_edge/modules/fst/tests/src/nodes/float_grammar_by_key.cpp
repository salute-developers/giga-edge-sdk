#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/fst/nodes/float_grammar_by_key/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Float grammar by key node", "[fst][nodes]") {
  auto node = CreateNode(
    "float_grammar_by_key",
    R"(
      key_to_fst_rule:
        stop: float_grammar_first.fst
        turn_on_light: float_grammar_second.fst
    )",
    {{"float_grammar_first.fst", ReadFileTo<IDataSpanPtr>("data/float_grammar_by_key/float_grammar_first.fst")},
     {"float_grammar_second.fst", ReadFileTo<IDataSpanPtr>("data/float_grammar_by_key/float_grammar_second.fst")}},
    {"key"}
  );

  NodePush(node, "text", std::string{"один"});
  NodePush(node, "key", std::string("stop"));
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{1.0}));

  NodePush(node, "text", std::string{"один"});
  NodePush(node, "key", std::string("turn_on_light"));
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{1.1}));

  NodePush(node, "text", std::string{""});
  NodePush(node, "key", std::string("stop"));
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));

  NodePush(node, "text", std::string{"один"});
  NodePush(node, "key", std::string("pause"));
  REQUIRE_THAT(NodeGet<std::vector<float>>(node, "output"), Catch::Approx(std::vector<float>{}));
}

namespace giga_edge {

inline bool operator==(const FloatGrammarByKeyNodeParams& lhs, const FloatGrammarByKeyNodeParams& rhs) {
  return lhs.key_to_fst_rule == rhs.key_to_fst_rule;
}

} // namespace giga_edge

TEST_CASE("Float grammar by key, config parser", "[fst][nodes]") {
  REQUIRE(
    ParseConfig<FloatGrammarByKeyNodeParams>(YAML::Load(
      "key_to_fst_rule:\n"
      "  first: first.fst\n"
      "  second: second.fst\n"
    ))
    == FloatGrammarByKeyNodeParams{
      .key_to_fst_rule = {{"first", "first.fst"}, {"second", "second.fst"}},
    }
  );
}
