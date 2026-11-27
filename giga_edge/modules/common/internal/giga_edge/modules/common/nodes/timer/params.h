#pragma once

#include <cstdint>
#include <vector>

namespace giga_edge {

struct TimerNodeParams {
  enum class RestartStrategy {
    RESTART,
    IGNORE,
  } restart_strategy = RestartStrategy::RESTART;

  bool continuous_alarm = false;

  int32_t timer;
};

} // namespace giga_edge