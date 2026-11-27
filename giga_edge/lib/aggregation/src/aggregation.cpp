#include "giga_edge/lib/aggregation/aggregation.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace giga_edge {

template<class C>
float Aggregate(const C& data, AggregationType aggregation_type) {
  switch (aggregation_type) {
    case AggregationType::MAX:
      return *std::max_element(data.begin(), data.end());
    case AggregationType::MIN:
      return *std::min_element(data.begin(), data.end());
    case AggregationType::SUM:
      return std::accumulate(data.begin(), data.end(), 0.0, std::plus<float>());
    case AggregationType::AVG:
      return std::accumulate(data.begin(), data.end(), 0.0, std::plus<float>()) / data.size();
    case AggregationType::LAST:
      return data.back();
  }

  throw std::invalid_argument("Invalid aggregation type");
}

template float Aggregate(const std::vector<float>& data, AggregationType aggregation_type);
template float Aggregate(const std::deque<float>& data, AggregationType aggregation_type);

} // namespace giga_edge
