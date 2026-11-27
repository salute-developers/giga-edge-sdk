#include "giga_edge/modules/events/nodes/events_stub/node.h"

namespace giga_edge {

EventsStubNode::EventsStubNode(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void EventsStubNode::ProcessOnce() {
  PushEvents({});
}

} // namespace giga_edge
