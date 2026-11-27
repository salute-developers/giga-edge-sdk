#include "giga_edge/modules/fst/nodes/float_grammar_by_key/node.h"

#include "giga_edge/lib/common/str_utils.h"

namespace giga_edge {

FloatGrammarByKeyNode::FloatGrammarByKeyNode(
  const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const FloatGrammarByKeyNodeParams& params
)
  : NodeBase(base_config) {
  for (const auto& [intent, rule] : params.key_to_fst_rule) {
    key_to_fst_rule_[intent] = ReadFstRule(data_reader, rule);
  }
}

void FloatGrammarByKeyNode::ProcessOnce() {
  const auto& key = GetKey();

  const auto& text = GetText();
  if (key.empty() || text.empty()) {
    PushOutput({});
    return;
  }

  const auto& fst_rules_it = key_to_fst_rule_.find(key);
  if (fst_rules_it == key_to_fst_rule_.end()) {
    PushOutput({});
    return;
  }

  PushOutput(Split(fst_rules_it->second->Apply(text)));
}

} // namespace giga_edge
