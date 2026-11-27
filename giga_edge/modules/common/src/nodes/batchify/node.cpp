#include "giga_edge/modules/common/nodes/batchify/node.h"

namespace giga_edge {

BatchifyNode::BatchifyNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void BatchifyNode::ProcessOnce() {
  auto batch = GetBatchBatch();
  auto val = GetValue();

  for (auto& data : batch) {
    PushOutput({val});
  }
}

void BatchifyNode::Reset() {}

} // namespace giga_edge
