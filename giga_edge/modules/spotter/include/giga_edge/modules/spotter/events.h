#pragma once

#include "giga_edge/modules/spotter/types.h"

#include <string>

namespace giga_edge {

struct SetActiveNameEvent {
  std::string active_name;
};

struct SetActivationModeEvent {
  std::string model_name;
  ActivationMode activaion_mode;
};

} // namespace giga_edge