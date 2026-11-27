#include "giga_edge/modules/assistant_core/turn_trace.h"

#include <utility>

namespace giga_edge {
namespace {

double MsBetween(TurnTraceRecorder::TimePoint from, TurnTraceRecorder::TimePoint to) {
  return std::chrono::duration<double, std::milli>(to - from).count();
}

} // namespace

void TurnTraceRecorder::Queue(TimePoint now) {
  queued_at_ = now;
}

void TurnTraceRecorder::Start(const std::string& request_id, const std::string& user, TimePoint now) {
  trace_ = TurnTrace{.request_id = request_id, .user = user, .wait_ms = MsBetween(queued_at_, now)};
  started_at_ = queued_at_;
  mark_ = now;
}

bool TurnTraceRecorder::IsActive() const {
  return !trace_.request_id.empty();
}

bool TurnTraceRecorder::ToolResult(const std::string& content, TimePoint now) {
  if (!IsActive() || trace_.steps.empty() || !trace_.steps.back().has_call) {
    return false;
  }
  auto& step = trace_.steps.back();
  step.has_result = true;
  step.result = content;
  step.result_ms = MsBetween(mark_, now);
  mark_ = now;
  return true;
}

void TurnTraceRecorder::Round(const AssistantResult& result, TimePoint now) {
  if (!IsActive()) {
    return;
  }
  TurnStep step{.ms = MsBetween(mark_, now), .usage = result.usage, .raw_text = result.raw_text};
  if (result.status == AssistantStatus::OK && !result.tool_calls.empty()) {
    step.has_call = true;
    step.call = result.tool_calls.front();
  }
  trace_.steps.push_back(std::move(step));
  mark_ = now;
}

TurnTrace TurnTraceRecorder::Snapshot(TimePoint now) {
  if (!IsActive()) {
    return {};
  }
  trace_.ms = MsBetween(started_at_, now);
  return trace_;
}

TurnTrace TurnTraceRecorder::Finish(const AssistantResult& result, TimePoint now) {
  if (!IsActive()) {
    return {};
  }
  trace_.finished = true;
  trace_.status = result.status;
  trace_.finish_reason = result.finish_reason;
  trace_.error = result.error;
  auto out = Snapshot(now);
  trace_ = TurnTrace{};
  return out;
}

} // namespace giga_edge
