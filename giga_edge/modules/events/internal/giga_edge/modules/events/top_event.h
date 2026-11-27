#pragma once

#include "giga_edge/modules/spotter/spotter_event.h"

namespace giga_edge {

SpotterEvent GetTopPriorityEvent(const std::vector<SpotterEvent>& events);

} // namespace giga_edge