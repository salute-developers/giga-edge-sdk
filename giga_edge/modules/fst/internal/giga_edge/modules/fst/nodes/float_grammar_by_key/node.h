#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/fst/fst_rule.h"

namespace giga_edge {

class FloatGrammarByKeyNode : public NodeBase {
public:
  FloatGrammarByKeyNode(
    const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const FloatGrammarByKeyNodeParams& params
  );

  void ProcessOnce() override;

  void Reset() override {};

  REGISTER_INPUT(float_grammar_by_key, text, Text, std::string, "Text to apply grammar to");
  REGISTER_INPUT(float_grammar_by_key, key, Key, std::string, "Key to find appropriate rule");
  REGISTER_OUTPUT(float_grammar_by_key, output, Output, std::vector<float>, "Grammar application result");

private:
  std::unordered_map<std::string, FstRulePtr> key_to_fst_rule_;
};

REGISTER_NODE(
  float_grammar_by_key,
  FloatGrammarByKeyNode,
  FloatGrammarByKeyNodeParams,
  true,
  "Applies grammar by key - returns a list of float"
);

} // namespace giga_edge
