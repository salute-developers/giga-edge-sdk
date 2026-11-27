#include "giga_edge/lib/statistics/statistics.h"

#include "json/value.h"
#include "json/writer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>

namespace giga_edge {
namespace {

float ToMilliseconds(const Duration& d) {
  return 0.001 * std::chrono::duration_cast<std::chrono::microseconds>(d).count();
}

std::vector<float> ExtractDurations(const std::deque<std::pair<int32_t, Duration>>& durations) {
  std::vector<float> result;
  result.reserve(durations.size());
  std::transform(durations.begin(), durations.end(), std::back_inserter(result), [](const auto& duration) {
    return ToMilliseconds(duration.second);
  });
  return result;
}

QuantileStatistic ExtractQuantiles(std::vector<float> durations) {
  if (durations.empty()) {
    return {0.0, 0.0, 0.0, 0.0};
  }

  QuantileStatistic result{};
  result.mean = std::accumulate(durations.begin(), durations.end(), 0.0f) / durations.size();

  int q95_index = std::floor(0.95 * durations.size());
  int q99_index = std::floor(0.99 * durations.size());

  std::sort(durations.begin(), durations.end());
  result.q95 = durations[q95_index];
  result.q99 = durations[q99_index];
  result.max = durations.back();

  return result;
}
} // namespace

void SystemStatistics::Add(std::string key, QuantileStatistic quantiles) {
  statistics_.emplace_back(std::move(key), quantiles);
}

void SystemStatistics::AddNodePriority(const std::string& node_name, int priority) {
  node_priorities_[node_name] = priority;
}

void SystemStatistics::Print(const std::string& model_version) const {
  std::stringstream ss;
  ss << "Giga Edge SDK runtime " << model_version << " latencies:" << "\n";
  ss << std::setw(35) << "mean" << " |" << std::setw(13) << "q95" << " |" << std::setw(13) << "q99" << " |"
     << std::setw(13) << "max" << " |" << std::setw(13) << "node_priority" << "\n";
  for (auto& statistic : statistics_) {
    ss << std::setw(20) << statistic.first << " |" << std::setprecision(3) << std::setw(10) << std::fixed
       << statistic.second.mean << " ms |" << std::setw(10) << std::fixed << statistic.second.q95 << " ms |"
       << std::setw(10) << std::fixed << statistic.second.q99 << " ms |" << std::setw(10) << std::fixed
       << statistic.second.max << " ms |";

    auto it = node_priorities_.find(statistic.first);
    int priority = 0;
    if (it != node_priorities_.end()) {
      priority = it->second;
    }
    ss << std::setw(10) << std::fixed << priority << "\n";
  }
  std::cerr << ss.str();
}

Json::Value SystemStatistics::GetJson() const {
  Json::Value json(Json::objectValue);

  if (statistics_.empty()) {
    return json;
  }

  for (const auto& statistic : statistics_) {
    auto& stats = json[statistic.first];
    stats["mean"] = statistic.second.mean;
    stats["q95"] = statistic.second.q95;
    stats["q99"] = statistic.second.q99;
    stats["max"] = statistic.second.max;
  }

  return json;
}

std::string SystemStatistics::GetJsonString() const {
  if (statistics_.empty()) {
    return "";
  }

  Json::Value json = GetJson();

  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString(builder, json);
}

Timer::Timer(Callback callback, int32_t required_triggers)
  : callback_(std::move(callback))
  , start_(Clock::now())
  , triggers_left_(required_triggers) {}

bool Timer::Trigger() {
  return --triggers_left_ == 0;
}

Timer::~Timer() {
  callback_(Clock::now() - start_);
}

BatchStatistics::BatchStatistics(const std::string& name, int32_t n_batches)
  : name_(name)
  , n_batches_(n_batches)
  , is_batch_(false) {}

void BatchStatistics::Add(int32_t batch_id, Duration duration) {
  std::lock_guard guard(mutex_);
  AddLocked(batch_id, duration);
}

void BatchStatistics::AddLocked(int32_t batch_id, Duration duration) {
  if (!batch_durations_.empty() && batch_durations_.back().first == batch_id) {
    batch_durations_.back().second += duration;
  } else {
    batch_durations_.emplace_back(batch_id, duration);

    while (batch_durations_.size() > n_batches_) {
      batch_durations_.pop_front();
    }
  }

  item_durations_.emplace_back(batch_id, duration);

  while (item_durations_.front().first != batch_durations_.front().first) {
    item_durations_.pop_front();
  }

  if (item_durations_.size() != batch_durations_.size()) {
    is_batch_ = true;
  }
}

void BatchStatistics::AddTimer(int32_t batch_id, int32_t required_triggers) {
  std::lock_guard guard(mutex_);
  timers_.try_emplace(
    batch_id,
    [this, batch_id](const auto& duration) {
      AddLocked(batch_id, duration);
    },
    required_triggers
  );
}

bool BatchStatistics::TriggerTimer(int32_t batch_id) {
  std::lock_guard guard(mutex_);
  auto timer = timers_.find(batch_id);
  assert(timer != timers_.end());
  if (timer == timers_.end()) {
    return false;
  }
  if (timer->second.Trigger()) {
    timers_.erase(timer);
    return true;
  }
  return false;
}

void BatchStatistics::Fill(SystemStatistics& system_statistics) const {
  std::lock_guard guard(mutex_);

  system_statistics.Add(name_, ExtractQuantiles(ExtractDurations(item_durations_)));
  if (is_batch_) {
    system_statistics.Add(name_ + "_batch", ExtractQuantiles(ExtractDurations(batch_durations_)));
  }
}

} // namespace giga_edge
