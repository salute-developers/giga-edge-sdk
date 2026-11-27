#pragma once

#include "class_meta_info.h"
#include "params.h"

#include "giga_edge/modules/spotter/spotter_event.h"

#include <functional>

namespace giga_edge {

class ISpotterEventProcessor {
public:
  virtual void Process(std::vector<SpotterEvent>& events) = 0;
  virtual void Reset() = 0;
  virtual ~ISpotterEventProcessor() = default;
};

class SleepingSpotterEventProcessor : public ISpotterEventProcessor {
public:
  explicit SleepingSpotterEventProcessor(const SleepTimerParams& params);

  void Process(std::vector<SpotterEvent>& events) override;
  void Reset() override;

private:
  SleepTimerParams params_;
  std::vector<int32_t> sleeping_timer_current_value_;
};

} // namespace giga_edge