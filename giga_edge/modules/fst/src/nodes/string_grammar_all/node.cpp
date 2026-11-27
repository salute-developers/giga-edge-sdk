#include "giga_edge/modules/fst/nodes/string_grammar_all/node.h"

#include <stdexcept>
#include <utility>

namespace giga_edge {

StringGrammarAllNode::StringGrammarAllNode(
  const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const StringGrammarAllNodeParams& params
)
  : NodeBase(base_config)
  , fst_rule_(ReadFstRule(data_reader, params.fst_rule))
  , max_outputs_(params.max_outputs) {
  if (max_outputs_ == 0) {
    throw std::invalid_argument("StringGrammarAllNode: max_outputs must be greater than zero");
  }
}

void StringGrammarAllNode::ProcessOnce() {
  auto outputs = fst_rule_->ApplyAll(GetText(), max_outputs_);
  if (outputs.empty()) {
    PushOutput("");
    return;
  }

  for (auto& output : outputs) {
    PushOutput(std::move(output));
  }
}

} // namespace giga_edge
