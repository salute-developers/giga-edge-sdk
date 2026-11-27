#pragma once

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/session.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <string>
#include <vector>

namespace giga_edge {

struct AssistantSessionParams {
  std::string fallback_text;
  std::string listen_again_marker;
  int max_listen_again = 0;
  int reply_timeout = 0;
  bool rules_on_failure = false;
  bool local_debug_enabled = false;
};

template<>
AssistantSessionParams ParseConfig(const YAML::Node& config);

class AssistantSession : public NodeBase {
public:
  AssistantSession(const NodeBaseConfig& base_config, const AssistantSessionParams& params);

  void ProcessOnce() override;
  void Reset() override;

private:
  void Start(bool manual, AssistantSessionEvents& events);
  void Close(AssistantSessionEvent reply, AssistantSessionEvents& events, std::string reason = {});
  AssistantSessionEvent Reply(const AssistantResult& result) const;
  AssistantSessionEvent RulesReply(const std::string& reason);
  AssistantSessionEvent FallbackReply(const std::string& reason) const;
  // source: who replied instead of the assistant, rules or fallback.
  void LogReplyInstead(const char* source, const std::string& reason) const;

  REGISTER_INPUT(assistant_session, session_start, SessionStart, bool, "Recognition session starts.");
  REGISTER_INPUT(
    assistant_session,
    manual_activation,
    ManualActivation,
    bool,
    "Manual activation: marks the session started in the same tick."
  );
  REGISTER_INPUT(assistant_session, session_finish, SessionFinish, bool, "Recognition session ends.");
  REGISTER_INPUT(assistant_session, query_text, QueryText, std::string, "Query text sent to the assistant.");
  REGISTER_INPUT(assistant_session, assistant_result, AssistantResult, AssistantResult, "Assistant results.");
  REGISTER_OPTIONAL_INPUT(
    assistant_session,
    quick_commands,
    Rules,
    RulesAnswer,
    "Rules answer to this tick's query: if not empty, the session replies with it at once, the assistant is not "
    "waited for."
  );
  REGISTER_OPTIONAL_INPUT(
    assistant_session,
    fallback_quick_commands,
    FallbackRules,
    RulesAnswer,
    "Rules answer to this tick's query in case the assistant declines it (with rules_on_failure also fails, gives "
    "an empty answer, runs into the token limit or out of time); without it the reply is fallback_text."
  );
  REGISTER_OPTIONAL_INPUT(
    assistant_session,
    session_cancelled,
    SessionCancelled,
    bool,
    "The recognition session ended by cancel, not by the end of speech: the reason of the reply."
  );
  REGISTER_OUTPUT(
    assistant_session,
    events,
    Events,
    AssistantSessionEvents,
    "Session starts and replies, in order; a reply tells who answered and why not the assistant."
  );
  REGISTER_OPTIONAL_OUTPUT(
    assistant_session,
    follow_up,
    FollowUp,
    bool,
    "The open session continues the dialogue: started manually right after a reply with listen_again."
  );
  REGISTER_OPTIONAL_OUTPUT(
    assistant_session,
    timed_out,
    TimedOut,
    bool,
    "The session stopped waiting for the assistant (reply_timeout): its unfinished turn should be cancelled."
  );
  REGISTER_OPTIONAL_OUTPUT(
    assistant_session,
    reply_text,
    ReplyText,
    std::string,
    "The text of the reply given in this tick, for the synthesis; empty in every other tick."
  );
  REGISTER_OPTIONAL_OUTPUT(
    assistant_session,
    listen_again,
    ListenAgain,
    bool,
    "The last reply asked to listen again and no session has started since: a manual start now continues the dialogue."
  );

  AssistantSessionParams params_;
  bool open_ = false;
  bool close_dropped_ = false;
  bool asked_ = false;
  int waited_ = 0;
  int listen_again_ = 0;
  // Rules answer to the session's query in case the assistant declines it.
  RulesAnswer fallback_;
};

REGISTER_NODE(
  assistant_session,
  AssistantSession,
  AssistantSessionParams,
  "Activation sessions for the client: every start gets exactly one reply, in order. The reply is the first final "
  "assistant result after the tick of the session's query: a result in that very tick is taken for a stale one, so the "
  "runner must answer later (asynchronously), otherwise only reply_timeout closes the session (0, the default, waits "
  "for the next start). After a reset (or when this version replaces another) an empty reply closes the session left "
  "open in the graph."
);

} // namespace giga_edge
