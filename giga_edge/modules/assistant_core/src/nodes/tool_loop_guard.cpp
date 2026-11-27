#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/assistant_core/nodes/tool_loop_guard/node.h"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace giga_edge {
namespace {

std::string CallKey(const ToolCall& call) {
  return call.name + '\n' + call.arguments_json;
}

std::optional<std::string> FailureCode(const std::string& content) {
  Json::Value value;
  std::string error;
  if (!ParseJson(content, value, error) || !value.isObject() || value["status"] != "fail") {
    return std::nullopt;
  }
  return value["error"].isString() ? value["error"].asString() : std::string();
}

} // namespace

template<>
ToolLoopGuardParams ParseConfig(const YAML::Node& config) {
  ToolLoopGuardParams result;
  result.max_tool_rounds = config["max_tool_rounds"].as<size_t>(result.max_tool_rounds);
  result.break_on_repeated_call = config["break_on_repeated_call"].as<bool>(result.break_on_repeated_call);
  result.parallel_tool_calls = config["parallel_tool_calls"].as<bool>(result.parallel_tool_calls);
  result.max_tool_calls = config["max_tool_calls"].as<size_t>(result.max_tool_calls);
  result.failed_call_reply = config["failed_call_reply"].as<std::string>(result.failed_call_reply);
  result.failed_call_replies = config["failed_call_replies"].as<std::map<std::string, std::string>>(
    result.failed_call_replies
  );
  result.local_debug_enabled = config["local_debug_enabled"].as<bool>(result.local_debug_enabled);
  return result;
}

ToolLoopGuard::ToolLoopGuard(const NodeBaseConfig& base_config, const ToolLoopGuardParams& params)
  : NodeBase(base_config)
  , params_(params) {
  for (const auto* input : {"tool_result", "cancel"}) {
    if (params_.parallel_tool_calls && base_config.inputs.count(input) == 0) {
      throw std::invalid_argument(std::string("tool_loop_guard: parallel_tool_calls needs the ") + input + " input");
    }
  }
  if (params_.max_tool_calls > 0 && !params_.parallel_tool_calls) {
    throw std::invalid_argument("tool_loop_guard: max_tool_calls needs parallel_tool_calls");
  }
  if (!params_.failed_call_replies.empty() && params_.failed_call_reply.empty()) {
    throw std::invalid_argument("tool_loop_guard: failed_call_replies needs failed_call_reply");
  }
  if (!params_.failed_call_reply.empty() && base_config.inputs.count("tool_result") == 0) {
    throw std::invalid_argument("tool_loop_guard: failed_call_reply needs the tool_result input");
  }
}

void ToolLoopGuard::Reset() {
  current_request_id_.clear();
  queued_request_id_.clear();
  expected_generation_id_.clear();
  rounds_ = 0;
  calls_.clear();
  reset_pending_ = false;
  batch_.clear();
  released_calls_ = 0;
  running_call_.clear();
  failed_calls_.clear();
}

AssistantResult ToolLoopGuard::NextBatchCall(const std::string& request_id) {
  AssistantResult step = batch_result_;
  step.request_id = request_id;
  step.tool_calls = {std::move(batch_.front())};
  batch_.pop_front();
  step.raw_text = step.tool_calls.front().raw_text;
  step.history_text = step.raw_text;
  step.usage = std::exchange(batch_result_.usage, {});
  step.queued_calls = batch_.size();
  expected_generation_id_ = step.tool_calls.front().id;
  running_call_ = CallKey(step.tool_calls.front());
  ++released_calls_;
  return step;
}

void ToolLoopGuard::ProcessOnce() {
  const auto& query = GetUserQuery();
  const auto& result = GetAssistantResult();

  if (const auto reset = GetDialogueReset(); reset.has_value() && reset->get()) {
    Reset();
    reset_pending_ = true;
    PushGuardedAssistantResult(AssistantResult{});
    return;
  }

  if (!query.text.empty() && query.request_id != current_request_id_) {
    queued_request_id_ = query.request_id;
    batch_.clear();
  }
  const auto cancel = GetCancel();
  const bool cancelled = cancel.has_value() && cancel->get();
  if (cancelled) {
    batch_.clear();
  }
  if (!queued_request_id_.empty() && result.request_id == queued_request_id_) {
    current_request_id_ = queued_request_id_;
    expected_generation_id_ = queued_request_id_;
    queued_request_id_.clear();
    rounds_ = 0;
    calls_.clear();
    reset_pending_ = false;
    batch_.clear();
    released_calls_ = 0;
    failed_calls_.clear();
  }
  const auto done = GetToolResult();
  const bool finished = done.has_value() && IsReadyToolResult(done->get())
    && done->get().call_id == expected_generation_id_;
  if (finished && !params_.failed_call_reply.empty()) {
    if (auto code = FailureCode(done->get().content)) {
      failed_calls_.emplace(running_call_, std::move(*code));
    }
  }
  if (finished && !batch_.empty()) {
    if (params_.max_tool_calls > 0 && released_calls_ >= params_.max_tool_calls) {
      batch_result_.request_id = done->get().call_id;
      PushGuardedAssistantResult(Broken(batch_result_, "Tool call limit exceeded", batch_.front().name));
      batch_.clear();
      return;
    }
    PushGuardedAssistantResult(NextBatchCall(done->get().call_id));
    return;
  }

  // A foreign result (including a warm before the first request) is not passed on, otherwise its error is voiced.
  if (reset_pending_ || (!result.request_id.empty() && result.request_id != expected_generation_id_)) {
    PushGuardedAssistantResult(AssistantResult{});
    return;
  }

  const bool truncated_call = result.is_final && result.finish_reason == StreamFinishReason::MAX_TOKENS
    && (result.status == AssistantStatus::ERROR || !result.tool_calls.empty());
  if (!truncated_call && (!result.is_final || result.tool_calls.empty())) {
    PushGuardedAssistantResult(result);
    return;
  }

  ++rounds_;
  if (params_.parallel_tool_calls && !truncated_call && cancelled) {
    AssistantResult dropped = result;
    dropped.tool_calls.clear();
    dropped.finish_reason = StreamFinishReason::CANCELLED;
    PushGuardedAssistantResult(std::move(dropped));
    return;
  }
  if (!truncated_call && !cancelled && result.status == AssistantStatus::OK) {
    for (const auto& call : result.tool_calls) {
      if (const auto failed = failed_calls_.find(CallKey(call)); failed != failed_calls_.end()) {
        PushGuardedAssistantResult(Refused(result, call, failed->second));
        return;
      }
    }
  }
  if (params_.parallel_tool_calls && !truncated_call) {
    PushGuardedAssistantResult(StartBatch(result));
    return;
  }
  const bool repeated_call = params_.break_on_repeated_call && result.tool_calls.size() == 1
    && !calls_.insert(CallKey(result.tool_calls.front())).second;
  if (!truncated_call && !repeated_call && rounds_ <= params_.max_tool_rounds && result.tool_calls.size() == 1) {
    if (!result.tool_calls.front().id.empty()) {
      expected_generation_id_ = result.tool_calls.front().id;
    }
    running_call_ = CallKey(result.tool_calls.front());
    PushGuardedAssistantResult(result);
    return;
  }
  PushGuardedAssistantResult(Broken(
    result,
    truncated_call                    ? "Tool call exceeded its token limit"
      : result.tool_calls.size() != 1 ? "Expected one tool call per step"
      : repeated_call                 ? "Repeated tool call"
                                      : "Tool loop limit exceeded"
  ));
}

AssistantResult ToolLoopGuard::StartBatch(AssistantResult result) {
  std::set<std::string> seen;
  std::vector<size_t> unique;
  bool repeated_call = false;
  for (size_t i = 0; i < result.tool_calls.size(); ++i) {
    auto key = CallKey(result.tool_calls[i]);
    if (seen.insert(key).second) {
      repeated_call = (params_.break_on_repeated_call && !calls_.insert(std::move(key)).second) || repeated_call;
      unique.push_back(i);
    }
  }
  if (params_.local_debug_enabled) {
    std::cerr << "[assistant][tool_loop_guard] batch request_id='" << result.request_id
              << "' calls=" << result.tool_calls.size() << " unique=" << unique.size()
              << " raw=" << JsonQuote(result.raw_text) << std::endl;
  }
  if (repeated_call) {
    return Broken(result, "Repeated tool call");
  }
  if (rounds_ > params_.max_tool_rounds) {
    return Broken(result, "Tool loop limit exceeded");
  }
  if (params_.max_tool_calls > 0 && released_calls_ >= params_.max_tool_calls) {
    return Broken(result, "Tool call limit exceeded");
  }
  batch_.clear();
  for (const size_t i : unique) {
    batch_.push_back(std::move(result.tool_calls[i]));
  }
  result.tool_calls.clear();
  result.raw_text.clear();
  result.history_text.clear();
  result.text.clear();
  batch_result_ = std::move(result);
  return NextBatchCall(batch_result_.request_id);
}

AssistantResult ToolLoopGuard::Broken(
  const AssistantResult& result, const char* error, std::string_view dropped
) const {
  if (params_.local_debug_enabled) {
    std::cerr << "[assistant][tool_loop_guard] tool loop broken request_id='" << current_request_id_
              << "' rounds=" << rounds_ << " max_tool_rounds=" << params_.max_tool_rounds << " dropped='"
              << (dropped.empty() && !result.tool_calls.empty() ? std::string_view(result.tool_calls.front().name)
                                                                : dropped)
              << "' error='" << error << "'" << std::endl;
  }

  AssistantResult failed = result;
  failed.status = AssistantStatus::ERROR;
  if (failed.error.empty()) {
    failed.error = error;
  }
  failed.tool_calls.clear();
  failed.text.clear();
  return failed;
}

AssistantResult ToolLoopGuard::Refused(
  const AssistantResult& result, const ToolCall& call, const std::string& code
) const {
  if (params_.local_debug_enabled) {
    std::cerr << "[assistant][tool_loop_guard] failed call repeated request_id='" << current_request_id_ << "' call='"
              << call.name << "' error='" << code << "'" << std::endl;
  }
  const auto reply = params_.failed_call_replies.find(code);
  AssistantResult refused = result;
  refused.text = reply != params_.failed_call_replies.end() ? reply->second : params_.failed_call_reply;
  refused.history_text = refused.text;
  refused.tool_calls.clear();
  return refused;
}

} // namespace giga_edge
