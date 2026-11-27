#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace giga_edge {

// Runs job(0..count-1) on `threads` threads, one of them the calling thread. Once a job returns false or throws, no
// new jobs are handed out and the started ones run to completion. The outcome matches a plain loop: the failure with
// the lowest index wins (its exception is rethrown). With one thread it is exactly a loop in order.
inline void RunInParallel(std::size_t count, std::size_t threads, const std::function<bool(std::size_t)>& job) {
  std::atomic<std::size_t> next{0};
  // Indices below the failure are already handed out and run to completion, as in a loop; higher ones do not start.
  std::atomic<std::size_t> first_failure{count};
  std::mutex failure_mutex;
  std::exception_ptr error;
  const auto worker = [&] {
    for (std::size_t i = next++; i < first_failure; i = next++) {
      bool done = false;
      std::exception_ptr thrown;
      try {
        done = job(i);
      } catch (...) {
        thrown = std::current_exception();
      }
      if (!done) {
        const std::lock_guard lock(failure_mutex);
        if (i < first_failure) {
          first_failure = i;
          error = thrown;
        }
      }
    }
  };
  std::vector<std::thread> pool;
  pool.reserve(std::min(threads, count));
  for (std::size_t t = 1; t < std::min(threads, count); ++t) {
    try {
      pool.emplace_back(worker);
    } catch (...) {
      break; // no thread can be created: the started ones and the caller finish the jobs
    }
  }
  worker();
  for (auto& thread : pool) {
    thread.join();
  }
  if (error) {
    std::rethrow_exception(error);
  }
}

} // namespace giga_edge
