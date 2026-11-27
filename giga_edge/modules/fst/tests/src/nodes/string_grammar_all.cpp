#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/fst/nodes/string_grammar_all/node.h"

#include <catch.hpp>

#include <algorithm>

using namespace giga_edge;

TEST_CASE("String grammar all node", "[runtime][nodes]") {
  auto node = CreateNode(
    "string_grammar_all",
    R"(
      fst_rule: string_grammar_all.fst
      max_outputs: 32
    )",
    {
      {"string_grammar_all.fst", ReadFileTo<IDataSpanPtr>("data/string_grammar_all/string_grammar_all.fst")},
    }
  );

  NodePush(node, "text", std::string{"джарвис"});
  auto outputs = NodeGetBatch<std::string>(node, "output");
  std::sort(outputs.begin(), outputs.end());
  CHECK(
    outputs
    == std::vector<std::string>{
      "джарвис|",
      "джарвиса|",
      "джарвисе|",
      "джарвисом|",
      "джарвису|",
    }
  );

  NodePush(node, "text", std::string{"алиса"});
  CHECK(NodeGetBatch<std::string>(node, "output") == std::vector<std::string>{""});
}

TEST_CASE("String grammar all node limits output count", "[runtime][nodes]") {
  auto node = CreateNode(
    "string_grammar_all",
    R"(
      fst_rule: string_grammar_all.fst
      max_outputs: 3
    )",
    {
      {"string_grammar_all.fst", ReadFileTo<IDataSpanPtr>("data/string_grammar_all/string_grammar_all.fst")},
    }
  );

  NodePush(node, "text", std::string{"джарвис"});
  CHECK(NodeGetBatch<std::string>(node, "output").size() == 3);
}

TEST_CASE("String grammar all node validates max_outputs", "[runtime][nodes]") {
  CHECK_THROWS_AS(
    CreateNode(
      "string_grammar_all",
      R"(
        fst_rule: string_grammar_all.fst
        max_outputs: 0
      )",
      {
        {"string_grammar_all.fst", ReadFileTo<IDataSpanPtr>("data/string_grammar_all/string_grammar_all.fst")},
      }
    ),
    std::invalid_argument
  );
}

namespace giga_edge {

inline bool operator==(const StringGrammarAllNodeParams& lhs, const StringGrammarAllNodeParams& rhs) {
  return lhs.fst_rule == rhs.fst_rule && lhs.max_outputs == rhs.max_outputs;
}

} // namespace giga_edge

TEST_CASE("String grammar all, config parser", "[runtime][nodes]") {
  CHECK(
    ParseConfig<StringGrammarAllNodeParams>(YAML::Load("fst_rule: string_grammar_all.fst\nmax_outputs: 17"))
    == StringGrammarAllNodeParams{
      .fst_rule = "string_grammar_all.fst",
      .max_outputs = 17,
    }
  );
}
