#pragma once

#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <chrono>
#include <string>

namespace giga_edge {

class TurnTraceRecorder {
public:
  using TimePoint = std::chrono::steady_clock::time_point;

  void Queue(TimePoint now);
  void Start(const std::string& request_id, const std::string& user, TimePoint now);
  bool IsActive() const;
  bool ToolResult(const std::string& content, TimePoint now);
  void Round(const AssistantResult& result, TimePoint now);
  TurnTrace Snapshot(TimePoint now);
  TurnTrace Finish(const AssistantResult& result, TimePoint now);

private:
  TurnTrace trace_;
  TimePoint queued_at_;
  TimePoint started_at_;
  TimePoint mark_;
};

} // namespace giga_edge
