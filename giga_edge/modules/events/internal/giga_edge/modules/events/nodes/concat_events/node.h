#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/spotter/spotter_event.h"

namespace giga_edge {

class ConcatEventsNode : public NodeBase {
public:
  explicit ConcatEventsNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_DYNAMIC_INPUTS(concat_events);
  REGISTER_OUTPUT(concat_events, events, Events, std::vector<SpotterEvent>, "Concatenated spotter events")

private:
  std::unordered_set<std::string> input_names_;
};

REGISTER_NODE(concat_events, ConcatEventsNode, "Concatenates events from different inputs");

} // namespace giga_edge
