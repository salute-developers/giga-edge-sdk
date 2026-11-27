#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/fst/fst_rule.h"

namespace giga_edge {

class StringGrammarNode : public NodeBase {
public:
  StringGrammarNode(
    const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const StringGrammarNodeParams& params
  );

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(string_grammar, text, Text, std::string, "Text to apply grammar to");
  REGISTER_OUTPUT(string_grammar, output, Output, std::string, "Grammar application result");

private:
  FstRulePtr fst_rule_;
};

REGISTER_NODE(
  string_grammar, StringGrammarNode, StringGrammarNodeParams, true, "Applies grammar which returns a string"
);

} // namespace giga_edge
