#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/spotter/event_generator.h"
#include "giga_edge/modules/spotter/events.h"
#include "giga_edge/modules/spotter/params.h"
#include "giga_edge/modules/spotter/spotter_event.h"
#include "giga_edge/modules/synthesis_text/events.h"

namespace giga_edge {

class EventGeneratorNode : public NodeBase {
public:
  EventGeneratorNode(const NodeBaseConfig& base_config, const EventGeneratorParams& params);

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(event_generator, predictions, Predictions, std::vector<float>, "Predictions");
  REGISTER_OUTPUT(event_generator, events, Events, std::vector<SpotterEvent>, "Spotter events");

private:
  void ProcessEventImpl(const std::any& event) override;

  void ProcessSetActiveName(const SetActiveNameEvent& event);
  void ProcessSetSynthesisText(const SetSynthesisTextEvent& event);
  void ProcessResetSynthesisText(const ResetSynthesisTextEvent& event);
  void ProcessSetActivationMode(const SetActivationModeEvent& event);

private:
  SpotterEventGenerator event_generator_;

  size_t num_frames_after_reset_synthesis_text_;
  bool need_force_reset_synthesis_text_;
};

REGISTER_NODE(
  event_generator, EventGeneratorNode, EventGeneratorParams, "Generates spotter events based on prediction scores"
);

} // namespace giga_edge
