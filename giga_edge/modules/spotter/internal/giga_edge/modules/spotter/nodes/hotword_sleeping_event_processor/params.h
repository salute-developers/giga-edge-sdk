#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace giga_edge {

struct HotwordSleepingEventProcessorParams {
  std::unordered_map<std::string, int32_t> timers;
  int32_t default_timer = 0;
};

} // namespace giga_edge
