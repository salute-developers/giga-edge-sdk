#pragma once

#include "giga_edge/lib/common/non_copyable.h"
#include "giga_edge/lib/node/types.h"
#include "giga_edge/lib/statistics/statistics.h"

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace giga_edge {

class StatisticsTracker : public NonCopyable {
public:
  using OnStatisticsReadyCallback = std::function<void()>;

  StatisticsTracker(
    std::unique_ptr<BatchStatistics> statistics,
    int statistics_every_batches,
    OnStatisticsReadyCallback on_statistics_ready_callback = {}
  );

  void AddSubscriber(const NodeSubscriptionId& subscriber_id);
  void RemoveSubscriber(const NodeSubscriptionId& subscriber_id);

  void StartTimer(uint32_t seq_id);
  void TriggerTimer(uint32_t seq_id, const NodeSubscriptionId& subscriber_id);

  void Fill(SystemStatistics& statistics) const;

private:
  void TriggerStatisticsTimer(uint32_t seq_id);

private:
  mutable std::mutex mutex_;

  std::unique_ptr<BatchStatistics> statistics_;
  int statistics_every_batches_;
  int batches_since_last_statistics_;
  OnStatisticsReadyCallback on_statistics_ready_callback_;

  std::unordered_set<NodeSubscriptionId> active_subscriber_ids_;
  std::unordered_map<NodeSubscriptionId, std::unordered_set<uint32_t>> subscriber_pending_seq_ids_;
  std::unordered_map<uint32_t, int32_t> seq_pending_subscribers_;
};

} // namespace giga_edge
