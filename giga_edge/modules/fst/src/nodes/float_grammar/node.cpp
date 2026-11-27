#include "giga_edge/modules/fst/nodes/float_grammar/node.h"

#include "giga_edge/lib/common/str_utils.h"

namespace giga_edge {

FloatGrammarNode::FloatGrammarNode(
  const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const FloatGrammarNodeParams& params
)
  : NodeBase(base_config) {
  fst_rule_ = ReadFstRule(data_reader, params.fst_rule);
}

void FloatGrammarNode::ProcessOnce() {
  PushOutput(Split(fst_rule_->Apply(GetText())));
}

} // namespace giga_edge
