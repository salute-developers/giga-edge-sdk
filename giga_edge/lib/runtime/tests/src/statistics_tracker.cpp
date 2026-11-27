#include "giga_edge/lib/runtime/statistics_tracker.h"

#include <catch.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

using namespace giga_edge;

TEST_CASE("StatisticsTracker, duplicate StartTimer for same seq_id is safe", "[runtime][statistics]") {
  std::atomic<int> on_statistics_ready_count{0};
  StatisticsTracker tracker(std::make_unique<BatchStatistics>("runtime", 16), 1, [&on_statistics_ready_count]() {
    ++on_statistics_ready_count;
  });

  const NodeSubscriptionId subscriber_id = "subscriber_1";
  const uint32_t seq_id = 42;

  tracker.AddSubscriber(subscriber_id);
  tracker.StartTimer(seq_id);
  tracker.StartTimer(seq_id);
  tracker.TriggerTimer(seq_id, subscriber_id);

  REQUIRE(on_statistics_ready_count.load() == 1);
}
