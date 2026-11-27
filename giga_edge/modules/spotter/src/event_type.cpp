#include "giga_edge/lib/enum/enum.h"
#include "giga_edge/modules/spotter/spotter_event.h"

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(
  SpotterEventType, "none", "subactivation", "activation", "disabled", "quick_command"
);