#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/fst/fst_rule.h"

#include <string>

namespace giga_edge {

class StringGrammarAllNode : public NodeBase {
public:
  StringGrammarAllNode(
    const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const StringGrammarAllNodeParams& params
  );

  void ProcessOnce() override;
  void Reset() override {}

  REGISTER_INPUT(string_grammar_all, text, Text, std::string, "Text to apply grammar to");
  REGISTER_BATCH_OUTPUT(string_grammar_all, output, Output, std::string, "All unique grammar application results");

private:
  FstRulePtr fst_rule_;
  std::size_t max_outputs_;
};

REGISTER_NODE(
  string_grammar_all,
  StringGrammarAllNode,
  StringGrammarAllNodeParams,
  true,
  "Applies a grammar and returns all unique string results as a batch"
);

} // namespace giga_edge
