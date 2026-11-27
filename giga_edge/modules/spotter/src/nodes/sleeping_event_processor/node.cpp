#include "giga_edge/modules/spotter/nodes/sleeping_event_processor/node.h"

namespace giga_edge {

SleepingSpotterEventProcessorNode::SleepingSpotterEventProcessorNode(
  const NodeBaseConfig& base_config, const SleepTimerParams& params
)
  : NodeBase(base_config)
  , event_processor_(params) {}

void SleepingSpotterEventProcessorNode::ProcessOnce() {
  auto events = GetEvents();
  event_processor_.Process(events);
  PushEvents(events);
}

void SleepingSpotterEventProcessorNode::Reset() {
  event_processor_.Reset();
}

} // namespace giga_edge
