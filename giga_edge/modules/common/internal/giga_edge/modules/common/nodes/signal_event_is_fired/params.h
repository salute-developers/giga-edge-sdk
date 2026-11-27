#pragma once

#include <string>

namespace giga_edge {

enum class SignalEventIsFiredResetStrategy { RESET_SIGNAL, IGNORE_RESET };

struct SignalEventIsFiredNodeParams {
  std::string signal_name;
  SignalEventIsFiredResetStrategy reset_strategy;
};

} // namespace giga_edge
