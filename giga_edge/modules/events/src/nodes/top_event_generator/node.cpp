#include "giga_edge/modules/events/nodes/top_event_generator/node.h"

#include "giga_edge/modules/events/top_event.h"

namespace giga_edge {

TopEventGeneratorNode::TopEventGeneratorNode(
  const NodeBaseConfig& base_config, const TopEventGeneratorNodeParams& params
)
  : NodeBase(base_config)
  , empty_event_(
      params.num_classes,
      {.event_type = SpotterEventType::NONE,
       .hot_word = "",
       .hot_word_length_ms = 0,
       .score = 0.0f,
       .latencies = Json::Value(Json::objectValue)}
    ) {}

void TopEventGeneratorNode::ProcessOnce() {
  const auto& events = GetEvents();

  if (!events.empty()) {
    PushTopEvent(GetTopPriorityEvent(events));
  } else {
    PushTopEvent(empty_event_);
  }
}

} // namespace giga_edge
