#include "giga_edge/modules/common/nodes/batch_iterator/node.h"

namespace giga_edge {

BatchIteratorNode::BatchIteratorNode(const NodeBaseConfig& base_config, const BatchIteratorNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void BatchIteratorNode::ProcessOnce() {
  const auto& audio = GetData();

  if (audio.size() + data_.size() < params_.batch_size) {
    PushBatch({});
  }

  for (auto x : audio) {
    data_.push_back(x);
    if (data_.size() == params_.batch_size) {
      PushBatch(std::move(data_));
      data_.clear();
    }
  }
}

void BatchIteratorNode::Reset() {
  data_.clear();
}

} // namespace giga_edge