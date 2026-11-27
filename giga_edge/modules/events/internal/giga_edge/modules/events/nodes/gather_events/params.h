#pragma once

#include "giga_edge/modules/spotter/spotter_event.h"

#include <unordered_set>

namespace giga_edge {

struct GatherEventsNodeParams {
  std::unordered_set<SpotterEventType> event_types;
};

} // namespace giga_edge