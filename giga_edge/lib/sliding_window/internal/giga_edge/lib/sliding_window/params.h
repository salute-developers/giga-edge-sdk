#pragma once

#include "giga_edge/lib/aggregation/params.h"

#include <cstdint>
#include <vector>

namespace giga_edge {

enum class DefaultValueStrategy { FILL = 0, CONST = 1 };

struct DefaultValueParams {
  DefaultValueStrategy strategy;
  float value;
};

struct SlidingWindowParams {
  int32_t window_size;
  AggregationType aggregation_type;
  DefaultValueParams default_value;
};

}; // namespace giga_edge