#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/spotter/spotter_event.h"

namespace giga_edge {

class TopEventGeneratorNode : public NodeBase {
public:
  TopEventGeneratorNode(const NodeBaseConfig& base_config, const TopEventGeneratorNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_INPUT(top_event_generator, events, Events, std::vector<SpotterEvent>, "Spotter events");
  REGISTER_OUTPUT(top_event_generator, top_event, TopEvent, SpotterEvent, "Top spotter event");

private:
  const std::vector<SingleSpotterEvent> empty_event_;
};

REGISTER_NODE(top_event_generator, TopEventGeneratorNode, TopEventGeneratorNodeParams, "Selects top spotter event");

} // namespace giga_edge
