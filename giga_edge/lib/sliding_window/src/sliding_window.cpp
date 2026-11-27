#include "giga_edge/lib/sliding_window/sliding_window.h"

#include "giga_edge/lib/aggregation/aggregation.h"

#include <algorithm>
#include <functional>
#include <numeric>
#include <stdexcept>

namespace giga_edge {

SlidingWindow::SlidingWindow(SlidingWindowParams params)
  : params_(params) {
  Reset();
}

void SlidingWindow::Push(float item) {
  data_.push_back(item);
  if (data_.size() > params_.window_size) {
    data_.pop_front();
  }
}

float SlidingWindow::Get() const {
  if (data_.size() < params_.window_size) {
    if (params_.default_value.strategy == DefaultValueStrategy::CONST) {
      return params_.default_value.value;
    } else {
      throw std::runtime_error(
        "SlidingWindow: should have less data than required for sliding window only for CONST default value"
      );
    }
  }

  return Aggregate(data_, params_.aggregation_type);
}

size_t SlidingWindow::Size() const {
  return data_.size();
}

void SlidingWindow::Reset() {
  data_.clear();
  if (params_.default_value.strategy == DefaultValueStrategy::FILL) {
    data_.assign(params_.window_size, params_.default_value.value);
  }
}

} // namespace giga_edge