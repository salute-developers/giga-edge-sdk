#pragma once

#include "types.h"

#include "giga_edge/lib/common/non_copyable.h"

#include "json/value.h"

#include <deque>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace giga_edge {

struct QuantileStatistic {
  float mean;
  float q95;
  float q99;
  float max;
};

class SystemStatistics {
public:
  void Add(std::string key, QuantileStatistic quantiles);

  void AddNodePriority(const std::string& node_name, int priority);

  void Print(const std::string& model_version = "") const;

  Json::Value GetJson() const;

  std::string GetJsonString() const;

private:
  std::vector<std::pair<std::string, QuantileStatistic>> statistics_;
  std::unordered_map<std::string, int> node_priorities_;
};

class Timer : public NonCopyable {
public:
  using Callback = std::function<void(Duration duration)>;

  explicit Timer(Callback callback, int32_t required_triggers = 1);
  bool Trigger();
  ~Timer();

private:
  Callback callback_;
  TimePoint start_;
  int32_t triggers_left_;
};

class BatchStatistics : public NonCopyable {
public:
  BatchStatistics(const std::string& name, int32_t n_batches);

  void Add(int32_t batch_id, Duration duration);

  void AddTimer(int32_t batch_id, int32_t required_triggers = 1);
  bool TriggerTimer(int32_t batch_id);

  void Fill(SystemStatistics& system_statistics) const;

private:
  void AddLocked(int32_t batch_id, Duration duration);

private:
  mutable std::mutex mutex_;

  std::string name_;
  int32_t n_batches_;
  bool is_batch_;

  std::deque<std::pair<int32_t, Duration>> batch_durations_;
  std::deque<std::pair<int32_t, Duration>> item_durations_;

  std::unordered_map<int32_t, Timer> timers_;
};

} // namespace giga_edge