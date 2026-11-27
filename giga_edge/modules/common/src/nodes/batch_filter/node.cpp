#include "giga_edge/modules/common/nodes/batch_filter/node.h"

#include <stdexcept>

namespace giga_edge {

BatchFilterNode::BatchFilterNode(const NodeBaseConfig& base_config, BatchFilterNodeParams params)
  : NodeBase(base_config)
  , params_(params) {}

void BatchFilterNode::ProcessOnce() {
  auto input = GetInputBatch();

  bool pushed = false;
  for (auto& id : params_.ids_to_take) {
    int id_to_take = id;
    if (id < 0) {
      id_to_take = input.size() + id;
    }

    if (id_to_take >= 0 && id_to_take < input.size()) {
      PushOutput(*input[id_to_take]);
      pushed = true;
    }
  }
  if (!pushed) {
    PushOutput({});
  }
}

} // namespace giga_edge