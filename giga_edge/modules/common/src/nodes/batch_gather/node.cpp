#include "giga_edge/modules/common/nodes/batch_gather/node.h"

namespace giga_edge {

BatchGatherNode::BatchGatherNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void BatchGatherNode::ProcessOnce() {
  const auto& input = GetInputBatch();

  std::vector<float> result;
  for (auto data : input) {
    result.insert(result.end(), data->begin(), data->end());
  }

  PushOutput(std::move(result));
}

} // namespace giga_edge