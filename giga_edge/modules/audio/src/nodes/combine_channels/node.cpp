#include "giga_edge/modules/audio/nodes/combine_channels/node.h"

namespace giga_edge {

CombineChannelsNode::CombineChannelsNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {
  for (int i = 0; i < base_config.inputs.size(); ++i) {
    input_names_.push_back("channel_" + std::to_string(i));
  }
}

void CombineChannelsNode::ProcessOnce() {
  std::vector<std::vector<float>> result;
  result.reserve(input_names_.size());
  for (const auto& input_name : input_names_) {
    const auto& channel = inputs_data_->GetSingle<std::vector<float>>(input_name);
    result.push_back(channel);
  }
  PushAudio(std::move(result));
}

std::unordered_set<std::string> CombineChannelsNode::GetDynamicInputs() {
  return {input_names_.begin(), input_names_.end()};
}

} // namespace giga_edge
