#include "giga_edge/modules/events/nodes/batch_gather_events/node.h"

namespace giga_edge {

BatchGatherEventsNode::BatchGatherEventsNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void BatchGatherEventsNode::ProcessOnce() {
  const auto& input = GetInputBatch();

  std::vector<SpotterEvent> result;
  for (auto data : input) {
    result.insert(result.end(), data->begin(), data->end());
  }

  PushOutput(std::move(result));
}

} // namespace giga_edge