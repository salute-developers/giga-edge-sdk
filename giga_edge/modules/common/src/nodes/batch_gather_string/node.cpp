#include "giga_edge/modules/common/nodes/batch_gather_string/node.h"

#include <vector>

namespace giga_edge {

BatchGatherStringNode::BatchGatherStringNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void BatchGatherStringNode::ProcessOnce() {
  const auto& input_batch = GetInputBatch();

  std::string result;
  for (const auto& data_ptr : input_batch) {
    if (data_ptr) {
      result += *data_ptr;
    }
  }

  PushOutput(std::move(result));
}

} // namespace giga_edge
