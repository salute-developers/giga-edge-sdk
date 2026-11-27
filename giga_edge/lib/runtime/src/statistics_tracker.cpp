#include "giga_edge/lib/runtime/statistics_tracker.h"

#include <iostream>
#include <vector>

namespace giga_edge {

StatisticsTracker::StatisticsTracker(
  std::unique_ptr<BatchStatistics> statistics,
  int statistics_every_batches,
  OnStatisticsReadyCallback on_statistics_ready_callback
)
  : statistics_(std::move(statistics))
  , statistics_every_batches_(statistics_every_batches)
  , batches_since_last_statistics_(0)
  , on_statistics_ready_callback_(std::move(on_statistics_ready_callback)) {}

void StatisticsTracker::AddSubscriber(const NodeSubscriptionId& subscriber_id) {
  if (!statistics_) {
    return;
  }

  std::lock_guard lock(mutex_);
  active_subscriber_ids_.insert(subscriber_id);
  subscriber_pending_seq_ids_.try_emplace(subscriber_id);
}

void StatisticsTracker::RemoveSubscriber(const NodeSubscriptionId& subscriber_id) {
  if (!statistics_) {
    return;
  }

  std::vector<uint32_t> seq_ids_to_trigger;
  {
    std::lock_guard lock(mutex_);
    active_subscriber_ids_.erase(subscriber_id);

    auto pending_seq_it = subscriber_pending_seq_ids_.find(subscriber_id);
    if (pending_seq_it == subscriber_pending_seq_ids_.end()) {
      return;
    }

    for (auto seq_id : pending_seq_it->second) {
      auto seq_pending_it = seq_pending_subscribers_.find(seq_id);
      if (seq_pending_it == seq_pending_subscribers_.end()) {
        continue;
      }

      assert(seq_pending_it->second > 0);
      if (--seq_pending_it->second == 0) {
        seq_pending_subscribers_.erase(seq_pending_it);
        seq_ids_to_trigger.push_back(seq_id);
      }
    }

    subscriber_pending_seq_ids_.erase(pending_seq_it);
  }

  for (auto seq_id : seq_ids_to_trigger) {
    TriggerStatisticsTimer(seq_id);
  }
}

void StatisticsTracker::StartTimer(uint32_t seq_id) {
  if (!statistics_) {
    return;
  }

  std::lock_guard lock(mutex_);
  if (active_subscriber_ids_.empty()) {
    return;
  }

  const auto is_inserted
    = seq_pending_subscribers_.emplace(seq_id, static_cast<int32_t>(active_subscriber_ids_.size())).second;
  if (!is_inserted) {
    // The same logical sequence id can arrive multiple times (e.g. from different graph inputs).
    return;
  }

  for (const auto& subscriber_id : active_subscriber_ids_) {
    subscriber_pending_seq_ids_[subscriber_id].insert(seq_id);
  }

  // We trigger this timer manually when pending subscribers count for seq_id reaches zero.
  statistics_->AddTimer(seq_id, 1);
}

void StatisticsTracker::TriggerTimer(uint32_t seq_id, const NodeSubscriptionId& subscriber_id) {
  if (!statistics_) {
    return;
  }

  bool should_trigger = false;
  {
    std::lock_guard lock(mutex_);
    auto pending_seq_it = subscriber_pending_seq_ids_.find(subscriber_id);
    if (pending_seq_it == subscriber_pending_seq_ids_.end()) {
      return;
    }

    if (pending_seq_it->second.erase(seq_id) == 0) {
      return;
    }

    auto seq_pending_it = seq_pending_subscribers_.find(seq_id);
    if (seq_pending_it == seq_pending_subscribers_.end()) {
      return;
    }

    assert(seq_pending_it->second > 0);
    if (--seq_pending_it->second == 0) {
      seq_pending_subscribers_.erase(seq_pending_it);
      should_trigger = true;
    }
  }

  if (should_trigger) {
    TriggerStatisticsTimer(seq_id);
  }
}

void StatisticsTracker::Fill(SystemStatistics& statistics) const {
  std::lock_guard lock(mutex_);
  if (statistics_ != nullptr) {
    statistics_->Fill(statistics);
  }
}

void StatisticsTracker::TriggerStatisticsTimer(uint32_t seq_id) {
  if (!statistics_) {
    return;
  }

  bool should_print_statistics = false;
  {
    std::lock_guard lock(mutex_);
    if (!statistics_->TriggerTimer(seq_id)) {
      return;
    }

    ++batches_since_last_statistics_;
    if (statistics_every_batches_ <= 0 || batches_since_last_statistics_ >= statistics_every_batches_) {
      batches_since_last_statistics_ = 0;
      should_print_statistics = true;
    }
  }

  if (should_print_statistics && on_statistics_ready_callback_) {
    on_statistics_ready_callback_();
  }
}

} // namespace giga_edge
