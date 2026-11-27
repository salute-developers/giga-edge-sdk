#pragma once

#include <catch.hpp>

#include <chrono>
#include <cstdint>
#include <thread>

namespace giga_edge {
namespace web {
namespace test_utils {

// A guard against a hang, not a speed measurement: under a sanitizer on a loaded runner the wait takes long.
inline constexpr std::chrono::seconds kWaitHangGuard{60};

template<class Pred>
bool WaitFor(Pred pred, std::chrono::milliseconds timeout = kWaitHangGuard) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return pred();
}

// The idle timeout of a request runs on the real clock from the submission. An attempt receives the timeout and
// returns false if under load the scenario did not work out (the stream missed the timeout) and checked nothing;
// it is then repeated with a doubled timeout. A broken service does not fit the scenario into any timeout and
// fails on the last attempt.
template<class Attempt>
void RepeatWithLongerIdleTimeout(Attempt attempt) {
  for (uint32_t idle_timeout_ms = 500; !attempt(idle_timeout_ms); idle_timeout_ms *= 2) {
    INFO("scenario did not fit into the idle timeout of " << idle_timeout_ms << " ms");
    REQUIRE(idle_timeout_ms < 8000);
  }
}

} // namespace test_utils
} // namespace web
} // namespace giga_edge
