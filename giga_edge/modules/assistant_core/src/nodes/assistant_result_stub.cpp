#include "giga_edge/modules/assistant_core/nodes/assistant_result_stub/node.h"

#include <stdexcept>
#include <utility>

namespace giga_edge {

AssistantResultStub::AssistantResultStub(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {
  if (Outputs().count("events") != 0 && Inputs().count("manual_activation") == 0) {
    throw std::invalid_argument("assistant_result_stub: session events need the manual_activation input");
  }
}

void AssistantResultStub::Reset() {
  close_dropped_ = true;
}

void AssistantResultStub::ProcessOnce() {
  (void)GetInput();
  PushAssistantResult(AssistantResult{});
  if (HasEventsOutput()) {
    AssistantSessionEvents events;
    if (std::exchange(close_dropped_, false)) {
      events.push_back({.kind = AssistantSessionEvent::Kind::REPLY});
    }
    if (const auto manual = GetManualActivation(); manual.has_value() && manual->get()) {
      events.push_back({.kind = AssistantSessionEvent::Kind::STARTED, .manual = true});
      events.push_back({.kind = AssistantSessionEvent::Kind::REPLY});
    }
    PushEvents(std::move(events));
  }
}

} // namespace giga_edge
