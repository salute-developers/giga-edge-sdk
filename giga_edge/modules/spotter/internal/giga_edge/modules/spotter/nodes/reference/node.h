#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/spotter/spotter_event.h"

namespace giga_edge {

class ReferenceEventsNode : public NodeBase {
public:
  ReferenceEventsNode(const NodeBaseConfig& base_config, const ReferenceEventsNodeParams& params);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(reference, events, Events, std::vector<SpotterEvent>, "Regular events");
  REGISTER_INPUT(reference, reference_events, ReferenceEvents, std::vector<SpotterEvent>, "Reference events");
  REGISTER_OUTPUT(reference, events, Events, std::vector<SpotterEvent>, "Processed events");

private:
  ReferenceEventsNodeParams params_;
  std::vector<int32_t> reference_activation_timeout_;
  std::vector<float> reference_activation_scores_;
  std::vector<std::string> reference_model_names_;
};

REGISTER_NODE(
  reference,
  ReferenceEventsNode,
  ReferenceEventsNodeParams,
  "Converts activations to subactivations if has recent reference event"
);

} // namespace giga_edge