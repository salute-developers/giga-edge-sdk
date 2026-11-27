#include "giga_edge/modules/common/nodes/thresholds_compare/node.h"

namespace giga_edge {

ThresholdsCompareNode::ThresholdsCompareNode(
  const NodeBaseConfig& base_config, const ThresholdsCompareNodeParams& params
)
  : NodeBase(base_config)
  , params_(params) {}

void ThresholdsCompareNode::ProcessOnce() {
  const auto& input = GetInput();

  if (!input.empty() && input.size() != params_.thresholds.size()) {
    throw std::runtime_error(
      id_ + ": got unexpected input size. Expected " + std::to_string(params_.thresholds.size()) + ", got "
      + std::to_string(input.size())
    );
  }

  std::vector<float> result;
  result.reserve(input.size());

  for (int i = 0; i < input.size(); ++i) {
    result.push_back(input[i] > params_.thresholds[i]);
  }

  PushOutput(std::move(result));
}

} // namespace giga_edge