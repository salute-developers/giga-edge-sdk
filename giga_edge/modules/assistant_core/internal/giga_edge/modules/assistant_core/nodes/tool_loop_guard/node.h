#pragma once

#include "giga_edge/lib/config_parser/config_parser_fwd.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <cstddef>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace giga_edge {

struct ToolLoopGuardParams {
  size_t max_tool_rounds = 8;
  bool break_on_repeated_call = false;
  // Several calls in one generation: one at a time, the next one after the result of the previous one.
  bool parallel_tool_calls = false;
  // Only with parallel_tool_calls; 0: no limit; at the limit the batch is cut off and the turn ends with an error.
  size_t max_tool_calls = 0;
  // A generation that repeats a call of the turn whose result failed ({"status": "fail", "error": code}) ends the turn
  // with a reply instead of an error: the reply for the code, failed_call_reply for other codes. Empty: off.
  std::string failed_call_reply;
  std::map<std::string, std::string> failed_call_replies;
  bool local_debug_enabled = false;
};

template<>
ToolLoopGuardParams ParseConfig(const YAML::Node& config);

class ToolLoopGuard : public NodeBase {
public:
  ToolLoopGuard(const NodeBaseConfig& base_config, const ToolLoopGuardParams& params);

  void ProcessOnce() override;
  void Reset() override;

private:
  REGISTER_INPUT(
    tool_loop_guard, assistant_result, AssistantResult, AssistantResult, "Accumulated answer of the current hop."
  );
  REGISTER_INPUT(tool_loop_guard, user_query, UserQuery, UserQuery, "A new request_id resets the round counter.");
  REGISTER_OPTIONAL_INPUT(tool_loop_guard, dialogue_reset, DialogueReset, bool, "Drops results of the reset dialogue.");
  REGISTER_OPTIONAL_INPUT(
    tool_loop_guard,
    tool_result,
    ToolResult,
    ToolResult,
    "Result of the running call releases the next call of a batch and tells a failed call."
  );
  REGISTER_OPTIONAL_INPUT(tool_loop_guard, cancel, Cancel, bool, "Drops the calls of a batch not yet released.");
  REGISTER_OUTPUT(
    tool_loop_guard,
    assistant_result,
    GuardedAssistantResult,
    AssistantResult,
    "Same result, or an error once the limit is exceeded or a tool call is truncated, or the reply to a repeated "
    "failed call."
  );

  AssistantResult StartBatch(AssistantResult result);
  AssistantResult NextBatchCall(const std::string& request_id);
  AssistantResult Broken(const AssistantResult& result, const char* error, std::string_view dropped = {}) const;
  AssistantResult Refused(const AssistantResult& result, const ToolCall& call, const std::string& code) const;

  ToolLoopGuardParams params_;
  std::string current_request_id_;
  std::string queued_request_id_;
  std::string expected_generation_id_;
  size_t rounds_ = 0;
  std::set<std::string> calls_;
  bool reset_pending_ = false;
  AssistantResult batch_result_;
  std::deque<ToolCall> batch_;
  size_t released_calls_ = 0;
  // The call being run and the failed calls of the request with their error codes.
  std::string running_call_;
  std::map<std::string, std::string> failed_calls_;
};

REGISTER_NODE(
  tool_loop_guard,
  ToolLoopGuard,
  ToolLoopGuardParams,
  "Turns runaway tool loops and truncated tool calls into errors. failed_call_reply (default empty, needs the "
  "tool_result input) ends the turn with a reply instead when a generation repeats a call whose result failed "
  "({\"status\": \"fail\", \"error\": code}): the reply for the code from failed_call_replies or failed_call_reply; "
  "the reply goes to the dialogue history."
);

} // namespace giga_edge
