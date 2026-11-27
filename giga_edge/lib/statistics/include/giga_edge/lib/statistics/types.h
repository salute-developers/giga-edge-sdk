#pragma once

#include <chrono>

namespace giga_edge {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = TimePoint::duration;

} // namespace giga_edge