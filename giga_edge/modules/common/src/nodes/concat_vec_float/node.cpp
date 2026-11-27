#include "giga_edge/modules/common/nodes/concat_vec_float/node.h"

namespace giga_edge {

ConcatVecFloatNode::ConcatVecFloatNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {
  for (int i = 1; i <= base_config.inputs.size(); ++i) {
    input_names_.push_back("input_" + std::to_string(i));
  }
}

void ConcatVecFloatNode::ProcessOnce() {
  std::vector<float> result;
  for (const auto& input_name : input_names_) {
    const auto& events = inputs_data_->GetSingle<std::vector<float>>(input_name);
    result.insert(result.end(), events.begin(), events.end());
  }

  PushOutput(std::move(result));
}

std::unordered_set<std::string> ConcatVecFloatNode::GetDynamicInputs() {
  return {input_names_.begin(), input_names_.end()};
}

} // namespace giga_edge
