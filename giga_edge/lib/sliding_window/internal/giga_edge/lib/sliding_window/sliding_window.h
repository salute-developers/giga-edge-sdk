#pragma once

#include "giga_edge/lib/sliding_window/params.h"

#include <cstddef>
#include <deque>

namespace giga_edge {

class SlidingWindow {
public:
  explicit SlidingWindow(SlidingWindowParams params);

  void Push(float item);
  float Get() const;
  size_t Size() const;

  void Reset();

private:
  SlidingWindowParams params_;
  std::deque<float> data_;
};

} // namespace giga_edge
