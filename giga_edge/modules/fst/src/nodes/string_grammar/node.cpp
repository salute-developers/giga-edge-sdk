#include "giga_edge/modules/fst/nodes/string_grammar/node.h"

namespace giga_edge {

StringGrammarNode::StringGrammarNode(
  const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const StringGrammarNodeParams& params
)
  : NodeBase(base_config) {
  fst_rule_ = ReadFstRule(data_reader, params.fst_rule);
}

void StringGrammarNode::ProcessOnce() {
  PushOutput(fst_rule_->Apply(GetText()));
}

} // namespace giga_edge
