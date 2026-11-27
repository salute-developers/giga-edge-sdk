#pragma once

#include "giga_edge/lib/aggregation/params.h"

namespace giga_edge {

template<class C>
float Aggregate(const C& data, AggregationType aggregation_type);

} // namespace giga_edge
