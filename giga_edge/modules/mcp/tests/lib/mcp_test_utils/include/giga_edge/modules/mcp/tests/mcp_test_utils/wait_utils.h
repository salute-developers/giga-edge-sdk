#pragma once

#include <chrono>
#include <thread>
#include <utility>

namespace giga_edge {
namespace mcp {
namespace test_utils {

// A guard against a hang, not a speed measurement: under sanitizers on a loaded runner the event arrives later.
inline constexpr std::chrono::seconds kWaitHangGuard{60};

template<class Predicate>
bool WaitUntil(
  Predicate predicate,
  std::chrono::milliseconds timeout = kWaitHangGuard,
  std::chrono::milliseconds poll_interval = std::chrono::milliseconds(5)
) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(poll_interval);
  }
  return predicate();
}

// A negative check: the event did not happen during the whole window.
template<class Predicate>
bool StaysFalseFor(Predicate predicate, std::chrono::milliseconds window) {
  return !WaitUntil(std::move(predicate), window);
}

} // namespace test_utils
} // namespace mcp
} // namespace giga_edge
