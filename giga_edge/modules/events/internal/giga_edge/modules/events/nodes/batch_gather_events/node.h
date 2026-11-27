#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/spotter/spotter_event.h"

namespace giga_edge {

class BatchGatherEventsNode : public NodeBase {
public:
  explicit BatchGatherEventsNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_BATCH_INPUT(batch_gather_events, input, Input, std::vector<SpotterEvent>, "Batch of events to gather");
  REGISTER_OUTPUT(batch_gather_events, output, Output, std::vector<SpotterEvent>, "Gathered events");
};

REGISTER_NODE(batch_gather_events, BatchGatherEventsNode, "Gathers batch of events into one");

} // namespace giga_edge