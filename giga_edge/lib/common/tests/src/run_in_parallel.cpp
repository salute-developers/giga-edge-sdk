#include "giga_edge/lib/common/run_in_parallel.h"

#include <catch.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

TEST_CASE("RunInParallel executes each index exactly once", "[common][run_in_parallel]") {
  for (const auto& [count, threads] : std::vector<std::pair<std::size_t, std::size_t>>{
         {0, 0}, {0, 4}, {1, 0}, {1, 1}, {1, 8}, {17, 0}, {17, 1}, {17, 4}, {3, 10}, {10000, 8}
       }) {
    INFO("count=" << count << ", threads=" << threads);
    std::vector<std::atomic<unsigned>> visits(count);
    for (auto& visit : visits) {
      visit.store(0);
    }
    giga_edge::RunInParallel(count, threads, [&](std::size_t i) {
      ++visits.at(i);
      return true;
    });
    for (const auto& visit : visits) {
      REQUIRE(visit.load() == 1);
    }
  }
}

TEST_CASE("RunInParallel runs one thread in order on the caller", "[common][run_in_parallel]") {
  const auto caller = std::this_thread::get_id();
  std::vector<std::size_t> indices;
  bool all_on_caller = true;
  giga_edge::RunInParallel(100, 1, [&](std::size_t i) {
    all_on_caller = all_on_caller && std::this_thread::get_id() == caller;
    indices.push_back(i);
    return true;
  });
  REQUIRE(all_on_caller);
  REQUIRE(indices.size() == 100);
  for (std::size_t i = 0; i < indices.size(); ++i) {
    REQUIRE(indices[i] == i);
  }
}

TEST_CASE("RunInParallel executes concurrently including the caller", "[common][run_in_parallel]") {
  const auto caller = std::this_thread::get_id();
  std::mutex mutex;
  std::condition_variable ready;
  unsigned active = 0;
  unsigned peak = 0;
  bool caller_worked = false;
  bool timed_out = false;
  giga_edge::RunInParallel(2, 2, [&](std::size_t) {
    std::unique_lock<std::mutex> lock(mutex);
    caller_worked = caller_worked || std::this_thread::get_id() == caller;
    ++active;
    peak = std::max(peak, active);
    ready.notify_all();
    if (!ready.wait_for(lock, std::chrono::seconds(10), [&] {
          return peak == 2;
        })) {
      timed_out = true;
    }
    --active;
    return true;
  });
  REQUIRE_FALSE(timed_out);
  REQUIRE(peak == 2);
  REQUIRE(caller_worked);
}

namespace {
struct JobError : std::runtime_error {
  explicit JobError(std::size_t index)
    : std::runtime_error("job failed")
    , index(index) {}
  std::size_t index;
};
} // namespace

TEST_CASE("RunInParallel on one thread stops after a failed job like a loop", "[common][run_in_parallel]") {
  std::vector<std::size_t> indices;
  giga_edge::RunInParallel(10, 1, [&](std::size_t i) {
    indices.push_back(i);
    return i != 3;
  });
  REQUIRE(indices == std::vector<std::size_t>{0, 1, 2, 3});
}

TEST_CASE("RunInParallel on one thread stops after an exception and rethrows it", "[common][run_in_parallel]") {
  std::vector<std::size_t> indices;
  std::optional<std::size_t> thrown;
  try {
    giga_edge::RunInParallel(10, 1, [&](std::size_t i) -> bool {
      indices.push_back(i);
      if (i == 3) {
        throw JobError(i);
      }
      return true;
    });
  } catch (const JobError& error) {
    thrown = error.index;
  }
  REQUIRE(thrown == 3);
  REQUIRE(indices == std::vector<std::size_t>{0, 1, 2, 3});
}

TEST_CASE("RunInParallel finishes started jobs and rethrows one of several exceptions", "[common][run_in_parallel]") {
  constexpr std::size_t count = 100;
  std::vector<std::atomic<unsigned>> visits(count);
  std::atomic<unsigned> started{0};
  std::atomic<unsigned> finished{0};
  std::optional<std::size_t> thrown;
  try {
    giga_edge::RunInParallel(count, 8, [&](std::size_t i) -> bool {
      ++started;
      ++visits.at(i);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      ++finished;
      if (i % 3 == 0) {
        throw JobError(i);
      }
      return true;
    });
  } catch (const JobError& error) {
    thrown = error.index;
  }
  // As in a loop: the error of the failed job with the lowest index.
  REQUIRE(thrown == std::size_t{0});
  // By the return every started job has finished, none ran twice, and no jobs were handed out after the error.
  REQUIRE(finished.load() == started.load());
  REQUIRE(started.load() < count);
  for (const auto& visit : visits) {
    REQUIRE(visit.load() <= 1);
  }
}

TEST_CASE("RunInParallel decides by the lowest failed job like a loop", "[common][run_in_parallel]") {
  // Job 1 returned false while job 5 was throwing: a loop would not have reached 5, so there is no exception.
  std::atomic<bool> five_threw{false};
  REQUIRE_NOTHROW(giga_edge::RunInParallel(8, 8, [&](std::size_t i) {
    if (i == 5) {
      five_threw = true;
      throw JobError(i);
    }
    if (i == 1) {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      while (!five_threw && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      return false;
    }
    return true;
  }));
  REQUIRE(five_threw);
}

TEST_CASE("RunInParallel rethrows the lowest failed job even when it fails last", "[common][run_in_parallel]") {
  std::atomic<bool> five_threw{false};
  std::optional<std::size_t> thrown;
  try {
    giga_edge::RunInParallel(8, 8, [&](std::size_t i) -> bool {
      if (i == 5) {
        five_threw = true;
        throw JobError(i);
      }
      if (i == 1) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!five_threw && std::chrono::steady_clock::now() < deadline) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw JobError(i);
      }
      return true;
    });
  } catch (const JobError& error) {
    thrown = error.index;
  }
  REQUIRE(five_threw);
  REQUIRE(thrown == std::size_t{1});
}

TEST_CASE("RunInParallel on many threads stops handing out jobs after a failed one", "[common][run_in_parallel]") {
  constexpr std::size_t count = 1000;
  std::atomic<std::size_t> started{0};
  giga_edge::RunInParallel(count, 4, [&](std::size_t i) {
    ++started;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return i != 0;
  });
  // Stops after the first job: no more jobs are handed out than the threads had already taken.
  REQUIRE(started.load() < count);
}
