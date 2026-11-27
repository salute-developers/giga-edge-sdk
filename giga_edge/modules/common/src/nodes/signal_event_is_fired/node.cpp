#include "giga_edge/modules/common/nodes/signal_event_is_fired/node.h"

#include "giga_edge/lib/common/events.h"

namespace giga_edge {

SignalEventIsFiredNode::SignalEventIsFiredNode(
  const NodeBaseConfig& base_config, const SignalEventIsFiredNodeParams& params
)
  : NodeBase(base_config)
  , signal_name_(params.signal_name)
  , reset_strategy_(params.reset_strategy) {}

void SignalEventIsFiredNode::ProcessOnce() {
  PushOutput(event_was_fired_);
  event_was_fired_ = false;
}

std::unordered_set<std::string> SignalEventIsFiredNode::GetDynamicInputs() {
  std::unordered_set<std::string> inputs;
  for (const auto& input : Inputs()) {
    inputs.insert(input.first);
  }
  return inputs;
}

void SignalEventIsFiredNode::ProcessEventImpl(const std::any& event) {
  if (event.type() == typeid(SignalEvent)) {
    auto signal = std::any_cast<const SignalEvent&>(event);
    if (signal.signal_name == signal_name_) {
      event_was_fired_ = true;
    }
  }
}

void SignalEventIsFiredNode::Reset() {
  if (reset_strategy_ == SignalEventIsFiredResetStrategy::IGNORE_RESET) {
    return;
  }

  event_was_fired_ = false;
}

} // namespace giga_edge
