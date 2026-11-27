#pragma once

#include "giga_edge/modules/spotter/spotter_event.h"

#include <string>

namespace giga_edge {

struct ChangeEventTypeNodeParams {
  SpotterEventType input_event_type;
  SpotterEventType output_event_type;
};

} // namespace giga_edge
