#include "giga_edge/modules/assistant_core/nodes/assistant_session/node.h"
#include "giga_edge/modules/assistant_core/text.h"

#include <functional>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

namespace giga_edge {
namespace {

RulesAnswer Rules(const std::optional<std::reference_wrapper<const RulesAnswer>>& input) {
  return input.has_value() ? input->get() : RulesAnswer{};
}

// Without rules_on_failure only a decline is answered by rules.
std::string RulesReason(const AssistantResult& result, const AssistantSessionEvent& reply) {
  if (result.status != AssistantStatus::OK) {
    return result.error.empty() ? "error" : result.error;
  }
  if (result.finish_reason == StreamFinishReason::DECLINED) {
    return "declined";
  }
  if (result.finish_reason == StreamFinishReason::MAX_TOKENS) {
    return "max_tokens";
  }
  return reply.text.empty() ? "empty reply" : "";
}

} // namespace

template<>
AssistantSessionParams ParseConfig(const YAML::Node& config) {
  AssistantSessionParams result;
  result.fallback_text = config["fallback_text"].as<std::string>(result.fallback_text);
  result.listen_again_marker = config["listen_again_marker"].as<std::string>(result.listen_again_marker);
  result.max_listen_again = config["max_listen_again"].as<int>(result.max_listen_again);
  result.reply_timeout = config["reply_timeout"].as<int>(result.reply_timeout);
  result.rules_on_failure = config["rules_on_failure"].as<bool>(result.rules_on_failure);
  result.local_debug_enabled = config["local_debug_enabled"].as<bool>(result.local_debug_enabled);
  return result;
}

AssistantSession::AssistantSession(const NodeBaseConfig& base_config, const AssistantSessionParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void AssistantSession::Reset() {
  // A reset, or this version is switched on instead of another one: a session opened in the graph before that is due a
  // result without a reply, first in the next tick (with no session the client skips it).
  close_dropped_ = true;
  open_ = false;
  asked_ = false;
  waited_ = 0;
  listen_again_ = 0;
  fallback_ = {};
}

void AssistantSession::ProcessOnce() {
  AssistantSessionEvents events;
  bool timed_out = false;
  if (std::exchange(close_dropped_, false)) {
    events.push_back(AssistantSessionEvent{.kind = AssistantSessionEvent::Kind::REPLY, .reason = "reset"});
  }
  const bool has_query = !Trim(GetQueryText()).empty();
  if (GetSessionStart()) {
    Start(GetManualActivation(), events);
  } else if (has_query && (!open_ || asked_)) {
    Start(false, events);
  }

  if (open_ && !asked_ && has_query) {
    // The rules answered: reply at once. Otherwise wait for the assistant and keep the rules answer for a decline.
    if (auto answer = Rules(GetRules()); answer.commands.has_value()) {
      Close(
        AssistantSessionEvent{
          .kind = AssistantSessionEvent::Kind::REPLY,
          .rules = std::move(answer),
          .source = AssistantSessionEvent::Source::RULES
        },
        events
      );
    } else {
      asked_ = true;
      fallback_ = Rules(GetFallbackRules());
    }
  } else if (open_ && !asked_ && GetSessionFinish()) {
    const auto cancelled = GetSessionCancelled();
    Close(
      AssistantSessionEvent{.kind = AssistantSessionEvent::Kind::REPLY},
      events,
      cancelled.has_value() && cancelled->get() ? "cancelled" : "no query"
    );
  } else if (open_ && asked_) {
    const auto& result = GetAssistantResult();
    const bool step = result.status == AssistantStatus::OK && !result.tool_calls.empty();
    if (result.is_final && result.finish_reason != StreamFinishReason::CANCELLED && !step) {
      auto reply = Reply(result);
      reply.source = AssistantSessionEvent::Source::ASSISTANT;
      const auto reason = RulesReason(result, reply);
      if (result.finish_reason == StreamFinishReason::DECLINED || (params_.rules_on_failure && !reason.empty())) {
        reply = RulesReply(reason);
      } else if (result.status != AssistantStatus::OK) {
        reply = FallbackReply(reason);
      }
      Close(std::move(reply), events, reason);
    } else if (params_.reply_timeout > 0 && ++waited_ > params_.reply_timeout) {
      timed_out = true;
      Close(
        params_.rules_on_failure ? RulesReply("reply timeout") : FallbackReply("reply timeout"), events, "reply timeout"
      );
    }
  }
  std::string reply_text;
  for (const auto& event : events) {
    if (event.kind == AssistantSessionEvent::Kind::REPLY && !event.text.empty()) {
      reply_text = event.text;
    }
  }
  PushEvents(std::move(events));
  // The counter lives only after a reply with listen_again: the hotword and any other close reset it to zero.
  PushFollowUp(open_ && listen_again_ > 0);
  PushTimedOut(timed_out);
  PushReplyText(std::move(reply_text));
  PushListenAgain(!open_ && listen_again_ > 0);
}

void AssistantSession::Start(bool manual, AssistantSessionEvents& events) {
  if (open_) {
    Close(AssistantSessionEvent{.kind = AssistantSessionEvent::Kind::REPLY}, events, "dropped");
  }
  if (!manual) {
    listen_again_ = 0;
  }
  open_ = true;
  asked_ = false;
  waited_ = 0;
  events.push_back(AssistantSessionEvent{.kind = AssistantSessionEvent::Kind::STARTED, .manual = manual});
}

void AssistantSession::Close(AssistantSessionEvent reply, AssistantSessionEvents& events, std::string reason) {
  if (reply.listen_again && listen_again_ >= params_.max_listen_again) {
    reply.listen_again = false;
  }
  if (reply.source == AssistantSessionEvent::Source::NONE) {
    reply.source = reply.rules.commands.has_value() ? AssistantSessionEvent::Source::FALLBACK_RULES
      : !reply.text.empty()                         ? AssistantSessionEvent::Source::FALLBACK_TEXT
                                                    : AssistantSessionEvent::Source::NONE;
  }
  reply.reason = std::move(reason);
  listen_again_ = reply.listen_again ? listen_again_ + 1 : 0;
  if (params_.local_debug_enabled) {
    std::cerr << "[assistant][session] reply text_bytes=" << reply.text.size() << " listen_again=" << reply.listen_again
              << " rules='" << reply.rules.summary << "'" << std::endl;
  }
  events.push_back(std::move(reply));
  open_ = false;
  asked_ = false;
  waited_ = 0;
  fallback_ = {};
}

AssistantSessionEvent AssistantSession::RulesReply(const std::string& reason) {
  if (!fallback_.commands.has_value()) {
    return FallbackReply(reason);
  }
  LogReplyInstead("rules", reason);
  return AssistantSessionEvent{.kind = AssistantSessionEvent::Kind::REPLY, .rules = std::exchange(fallback_, {})};
}

AssistantSessionEvent AssistantSession::FallbackReply(const std::string& reason) const {
  LogReplyInstead("fallback", reason);
  return AssistantSessionEvent{.kind = AssistantSessionEvent::Kind::REPLY, .text = params_.fallback_text};
}

void AssistantSession::LogReplyInstead(const char* source, const std::string& reason) const {
  if (params_.local_debug_enabled) {
    std::cerr << "[assistant][session] " << source << " reply reason='" << reason << "'" << std::endl;
  }
}

AssistantSessionEvent AssistantSession::Reply(const AssistantResult& result) const {
  AssistantSessionEvent reply{.kind = AssistantSessionEvent::Kind::REPLY};
  reply.text = Trim(result.text);
  const auto& marker = params_.listen_again_marker;
  const auto ends_with_marker = [&marker](const std::string& text) {
    return !marker.empty() && text.size() >= marker.size()
      && text.compare(text.size() - marker.size(), marker.size(), marker) == 0;
  };
  if (ends_with_marker(reply.text)) {
    reply.text = Trim(std::string_view(reply.text).substr(0, reply.text.size() - marker.size()));
    reply.listen_again = true;
  }
  reply.listen_again = reply.listen_again || ends_with_marker(Trim(result.raw_text));
  reply.listen_again = reply.listen_again && !reply.text.empty();
  return reply;
}

} // namespace giga_edge
