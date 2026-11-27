#include "giga_edge/modules/common/nodes/window_aggregation/node.h"

namespace giga_edge {

WindowAggregationNode::WindowAggregationNode(
  const NodeBaseConfig& base_config, const WindowAggregationNodeParams& params
)
  : NodeBase(base_config)
  , params_(params)
  , batch_num_(0) {
  windows_.reserve(params_.aggregations.size());
  for (const auto& aggregation : params_.aggregations) {
    windows_.emplace_back(aggregation);
  }
}

void WindowAggregationNode::ProcessOnce() {
  const auto& input = GetInput();

  if (!input.empty() && input.size() != windows_.size()) {
    throw std::runtime_error(
      id_ + ": got unexpected input size. Expected " + std::to_string(windows_.size()) + ", got "
      + std::to_string(input.size())
    );
  }

  for (size_t i = 0; i < input.size(); ++i) {
    windows_[i].Push(input[i]);
  }
  std::vector<float> result;
  if (batch_num_ % params_.output_interval == 0) {
    batch_num_ = 0;
    result.reserve(windows_.size());
    for (const auto& window : windows_) {
      result.push_back(window.Get());
    }
  }
  ++batch_num_;

  PushOutput(std::move(result));
}

void WindowAggregationNode::Reset() {
  for (auto& window : windows_) {
    window.Reset();
  }
}

} // namespace giga_edge