#pragma once

#include "giga_edge/lib/sliding_window/params.h"

#include <cstdint>
#include <vector>

namespace giga_edge {

struct WindowAggregationNodeParams {
  std::vector<SlidingWindowParams> aggregations;
  int32_t output_interval;
};

} // namespace giga_edge