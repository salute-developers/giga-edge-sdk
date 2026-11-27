#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/fst/fst_rule.h"

namespace giga_edge {

class FloatGrammarNode : public NodeBase {
public:
  FloatGrammarNode(
    const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const FloatGrammarNodeParams& params
  );

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(float_grammar, text, Text, std::string, "Text to apply grammar to");
  REGISTER_OUTPUT(float_grammar, output, Output, std::vector<float>, "Grammar application result");

private:
  FstRulePtr fst_rule_;
};

REGISTER_NODE(
  float_grammar, FloatGrammarNode, FloatGrammarNodeParams, true, "Applies grammar which returns a list of float"
);

} // namespace giga_edge
