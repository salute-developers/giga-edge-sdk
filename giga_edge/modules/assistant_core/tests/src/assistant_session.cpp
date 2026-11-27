#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/assistant_core/nodes.h"
#include "giga_edge/modules/assistant_core/text.h"

#include <catch.hpp>

#include <any>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace giga_edge;

namespace {

using Kind = AssistantSessionEvent::Kind;

// In tests a rules answer is command names: the session does not parse it, only passes it on.
using Commands = std::vector<std::string>;

RulesAnswer Answer(const Commands& commands) {
  if (commands.empty()) {
    return {};
  }
  std::string summary;
  for (const auto& command : commands) {
    summary += (summary.empty() ? "" : ",") + command;
  }
  return RulesAnswer{.commands = commands, .summary = summary};
}

Commands Of(const AssistantSessionEvent& event) {
  return event.rules.commands.has_value() ? std::any_cast<Commands>(event.rules.commands) : Commands{};
}

struct Tick {
  bool start = false;
  bool manual = false;
  bool finish = false;
  std::string query;
  AssistantResult result;
  Commands rules;
  Commands fallback;
  bool cancelled = false;
};

AssistantSessionEvents Run(NodeContext& ctx, const Tick& tick) {
  NodePush(ctx, "session_start", tick.start, true, 0);
  NodePush(ctx, "manual_activation", tick.manual, true, 0);
  NodePush(ctx, "session_finish", tick.finish, true, 0);
  NodePush(ctx, "query_text", tick.query, true, 0);
  if (ctx.inputs.count("quick_commands") > 0) {
    NodePush(ctx, "quick_commands", Answer(tick.rules), true, 0);
    NodePush(ctx, "fallback_quick_commands", Answer(tick.fallback), true, 0);
  }
  if (ctx.inputs.count("session_cancelled") > 0) {
    NodePush(ctx, "session_cancelled", tick.cancelled, true, 0);
  }
  NodePush(ctx, "assistant_result", tick.result);
  return NodeGet<AssistantSessionEvents>(ctx, "events");
}

NodeContext SessionWithRules(const std::string& config) {
  return CreateNode("assistant_session", config, {}, {"quick_commands", "fallback_quick_commands"});
}

Commands Command(const std::string& name) {
  return {name};
}

AssistantResult Declined() {
  auto declined = AssistantResult{.request_id = "q", .is_final = true};
  declined.finish_reason = StreamFinishReason::DECLINED;
  return declined;
}

bool RepliedWith(const AssistantSessionEvents& events, const std::string& command) {
  return events.size() == 1 && events[0].kind == Kind::REPLY && events[0].text.empty() && !events[0].listen_again
    && Of(events[0]) == Commands{command};
}

AssistantResult Final(const std::string& request_id, const std::string& text) {
  return AssistantResult{.request_id = request_id, .text = text, .is_final = true};
}

bool Started(const AssistantSessionEvents& events, bool manual) {
  return events.size() == 1 && events[0].kind == Kind::STARTED && events[0].manual == manual;
}

// The node's debug log is written to std::cerr.
class CapturedCerr {
public:
  CapturedCerr()
    : previous_(std::cerr.rdbuf(buffer_.rdbuf())) {}
  ~CapturedCerr() {
    std::cerr.rdbuf(previous_);
  }
  CapturedCerr(const CapturedCerr&) = delete;
  CapturedCerr& operator=(const CapturedCerr&) = delete;

  bool Has(const std::string& text) const {
    return buffer_.str().find(text) != std::string::npos;
  }

private:
  std::ostringstream buffer_;
  std::streambuf* previous_;
};

bool Replied(const AssistantSessionEvents& events, const std::string& text, bool listen_again = false) {
  return events.size() == 1 && events[0].kind == Kind::REPLY && events[0].text == text
    && events[0].listen_again == listen_again;
}

} // namespace

TEST_CASE("assistant_session: every start gets exactly one reply, in order", "[assistant][node][session]") {
  auto ctx = CreateNode(
    "assistant_session",
    "{fallback_text: 'Ошибка', listen_again_marker: '<await_user>', max_listen_again: 2, reply_timeout: 4}"
  );

  SECTION("silence: the session ends without a query") {
    REQUIRE(Started(Run(ctx, {.start = true}), false));
    REQUIRE(Run(ctx, {}).empty());
    REQUIRE(Replied(Run(ctx, {.finish = true}), ""));
  }

  SECTION("a reply comes only after the session's own query") {
    REQUIRE(Started(Run(ctx, {.start = true}), false));
    REQUIRE(Run(ctx, {.result = Final("old", "хвост прошлой сессии")}).empty());
    REQUIRE(Run(ctx, {.finish = true, .query = "включи свет", .result = Final("old", "тот же такт")}).empty());
    auto call = Final("q", "");
    call.tool_calls.push_back(ToolCall{.name = "set"});
    REQUIRE(Run(ctx, {.result = call}).empty());
    auto cancelled = Final("old", "вытеснен");
    cancelled.finish_reason = StreamFinishReason::CANCELLED;
    REQUIRE(Run(ctx, {.result = cancelled}).empty());
    REQUIRE(Replied(Run(ctx, {.result = Final("q/tool/1", "Включил свет.")}), "Включил свет."));
    REQUIRE(Run(ctx, {.result = Final("q", "повтор")}).empty());
  }

  SECTION("a new activation closes the open session first, its late answer is dropped") {
    Run(ctx, {.start = true});
    Run(ctx, {.finish = true, .query = "включи свет"});
    const auto events = Run(ctx, {.start = true});
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].kind == Kind::REPLY);
    REQUIRE(events[0].text.empty());
    REQUIRE(events[1].kind == Kind::STARTED);
    REQUIRE(Run(ctx, {.result = Final("old", "Включил свет.")}).empty());
    REQUIRE(Replied(Run(ctx, {.finish = true}), ""));
  }

  SECTION("manual activation marks the session it starts") {
    REQUIRE(Started(Run(ctx, {.start = true, .manual = true}), true));
    REQUIRE(Replied(Run(ctx, {.finish = true}), ""));
    REQUIRE(Started(Run(ctx, {.start = true}), false));
  }

  SECTION("a query without a recognition session (debug text) opens its own") {
    const auto events = Run(ctx, {.query = "включи свет"});
    REQUIRE(Started(events, false));
    REQUIRE(Replied(Run(ctx, {.result = Final("q0", "Включил свет.")}), "Включил свет."));
  }

  SECTION("errors and a missing answer still reply") {
    Run(ctx, {.start = true});
    Run(ctx, {.finish = true, .query = "включи свет"});
    auto error = Final("q0", "");
    error.status = AssistantStatus::ERROR;
    REQUIRE(Replied(Run(ctx, {.result = error}), "Ошибка"));

    Run(ctx, {.start = true});
    Run(ctx, {.finish = true, .query = "включи свет"});
    for (int tick = 0; tick < 4; ++tick) {
      REQUIRE(Run(ctx, {}).empty());
    }
    REQUIRE(Replied(Run(ctx, {}), "Ошибка"));
  }

  SECTION("the marker asks to listen again, at most twice in a row") {
    const auto turn = [&ctx](bool hotword, const std::string& id) {
      Run(ctx, {.start = true, .manual = !hotword});
      Run(ctx, {.finish = true, .query = "свет"});
      const auto events = Run(ctx, {.result = Final(id, "На кухне или в гостиной? <await_user>")});
      REQUIRE(events.size() == 1);
      REQUIRE(events[0].text == "На кухне или в гостиной?");
      return events[0].listen_again;
    };
    REQUIRE(turn(true, "q0"));
    REQUIRE(turn(false, "q1"));
    REQUIRE_FALSE(turn(false, "q2"));
    REQUIRE(turn(false, "q3"));
    REQUIRE(turn(true, "q4"));
  }
}

TEST_CASE("assistant_session: variations of start, manual flag and closing", "[assistant][node][session]") {
  auto ctx = CreateNode("assistant_session", "{fallback_text: 'Ошибка', reply_timeout: 0}");

  SECTION("hotword and button in one tick — one manual session") {
    REQUIRE(Started(Run(ctx, {.start = true, .manual = true}), true));
    REQUIRE(Replied(Run(ctx, {.finish = true}), ""));
  }

  SECTION("a button that started nothing does not mark a later hotword session") {
    REQUIRE(Run(ctx, {.manual = true}).empty());
    REQUIRE(Run(ctx, {}).empty());
    REQUIRE(Run(ctx, {}).empty());
    REQUIRE(Started(Run(ctx, {.start = true}), false));
  }

  SECTION("without reply_timeout the session waits for its answer however long") {
    Run(ctx, {.start = true});
    Run(ctx, {.finish = true, .query = "включи свет"});
    for (int tick = 0; tick < 1000; ++tick) {
      REQUIRE(Run(ctx, {}).empty());
    }
    REQUIRE(Replied(Run(ctx, {.result = Final("q", "Включил свет.")}), "Включил свет."));
  }

  SECTION("a cancelled final of its own query is not a reply") {
    Run(ctx, {.start = true});
    Run(ctx, {.finish = true, .query = "включи свет"});
    auto cancelled = Final("q", "Включ");
    cancelled.finish_reason = StreamFinishReason::CANCELLED;
    REQUIRE(Run(ctx, {.result = cancelled}).empty());
    REQUIRE(Replied(Run(ctx, {.result = Final("q", "Включил свет.")}), "Включил свет."));
  }

  SECTION("not final and tool steps are not replies, the error is") {
    Run(ctx, {.start = true});
    Run(ctx, {.finish = true, .query = "включи свет"});
    auto partial = Final("q", "Вкл");
    partial.is_final = false;
    REQUIRE(Run(ctx, {.result = partial}).empty());
    for (int hop = 0; hop < 3; ++hop) {
      auto call = Final("q/tool/" + std::to_string(hop), "");
      call.tool_calls.push_back(ToolCall{.name = "set"});
      REQUIRE(Run(ctx, {.result = call}).empty());
    }
    auto error = Final("q/tool/3", "");
    error.status = AssistantStatus::ERROR;
    REQUIRE(Replied(Run(ctx, {.result = error}), "Ошибка"));
  }

  SECTION("debug text while waiting closes the waiting session and opens its own") {
    Run(ctx, {.start = true});
    Run(ctx, {.finish = true, .query = "включи свет"});
    const auto events = Run(ctx, {.query = "выключи свет"});
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].kind == Kind::REPLY);
    REQUIRE(events[0].text.empty());
    REQUIRE(events[1].kind == Kind::STARTED);
    REQUIRE(Replied(Run(ctx, {.result = Final("q2", "Выключил.")}), "Выключил."));
  }

  SECTION("reset closes the open session with an empty reply, a late answer is ignored") {
    Run(ctx, {.start = true});
    Run(ctx, {.finish = true, .query = "включи свет"});
    NodePushEvent(ctx, Event{.event = ResetEvent{}});
    REQUIRE(Replied(Run(ctx, {.result = Final("q", "поздний")}), ""));
    REQUIRE(Run(ctx, {}).empty());
    REQUIRE(Started(Run(ctx, {.start = true}), false));
    REQUIRE(Replied(Run(ctx, {.finish = true}), ""));
  }

  SECTION("reset without an open session still gives an empty reply: a session of the version switched off ends") {
    NodePushEvent(ctx, Event{.event = ResetEvent{}});
    REQUIRE(Replied(Run(ctx, {}), ""));
    REQUIRE(Run(ctx, {}).empty());
  }

  SECTION("a reset and a new start in the next tick: the old reply comes first") {
    Run(ctx, {.start = true});
    NodePushEvent(ctx, Event{.event = ResetEvent{}});
    const auto events = Run(ctx, {.start = true, .manual = true});
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].kind == Kind::REPLY);
    REQUIRE(events[0].text.empty());
    REQUIRE(events[1].kind == Kind::STARTED);
    REQUIRE(events[1].manual);
  }

  SECTION("a query of spaces is no query: silence ends the session") {
    REQUIRE(Started(Run(ctx, {.start = true}), false));
    REQUIRE(Replied(Run(ctx, {.finish = true, .query = "  "}), ""));
  }
}

TEST_CASE("assistant_session: the marker in its forms", "[assistant][node][session]") {
  const auto reply = [](const std::string& config, const AssistantResult& result) {
    auto ctx = CreateNode("assistant_session", config);
    Run(ctx, {.start = true});
    Run(ctx, {.finish = true, .query = "свет"});
    const auto events = Run(ctx, {.result = result});
    REQUIRE(events.size() == 1);
    return std::make_pair(events[0].text, events[0].listen_again);
  };
  const std::string marker = "{listen_again_marker: '<await_user>', max_listen_again: 2}";

  REQUIRE(reply(marker, Final("q", "Где? <await_user>")) == std::make_pair(std::string("Где?"), true));
  REQUIRE(reply(marker, Final("q", "Где?<await_user>  \n")) == std::make_pair(std::string("Где?"), true));
  REQUIRE(reply(marker, Final("q", "<await_user>")) == std::make_pair(std::string(""), false));
  auto raw_only = Final("q", "Где?");
  raw_only.raw_text = "Где?<await_user>";
  REQUIRE(reply(marker, raw_only) == std::make_pair(std::string("Где?"), true));
  REQUIRE(
    reply(marker, Final("q", "<await_user> в начале")) == std::make_pair(std::string("<await_user> в начале"), false)
  );
  REQUIRE(reply("{}", Final("q", "Где? <await_user>")) == std::make_pair(std::string("Где? <await_user>"), false));
  REQUIRE(
    reply("{listen_again_marker: '?', max_listen_again: 2}", Final("q", "На кухне?"))
    == std::make_pair(std::string("На кухне"), true)
  );
  REQUIRE(
    reply("{listen_again_marker: '?', max_listen_again: 0}", Final("q", "На кухне?"))
    == std::make_pair(std::string("На кухне"), false)
  );
}

TEST_CASE(
  "assistant_session: the rules answer in the query tick, the assistant is not waited for", "[assistant][node][session]"
) {
  auto ctx = SessionWithRules("{fallback_text: 'Ошибка', listen_again_marker: '<await_user>', max_listen_again: 2}");
  const auto get_time = Command("get_time");

  SECTION("a hotword session") {
    REQUIRE(Started(Run(ctx, {.start = true}), false));
    REQUIRE(RepliedWith(Run(ctx, {.finish = true, .query = "который час", .rules = get_time}), "get_time"));
    REQUIRE(Run(ctx, {.result = Final("q", "поздний")}).empty());
    REQUIRE(Run(ctx, {.result = Declined()}).empty());
  }

  SECTION("both in one tick: the rules answer wins, the fallback is not needed") {
    Run(ctx, {.start = true});
    const auto get_date = Command("get_date");
    REQUIRE(RepliedWith(
      Run(ctx, {.finish = true, .query = "который час", .rules = get_time, .fallback = get_date}), "get_time"
    ));
  }

  SECTION("all the commands of the answer go out, in order") {
    Run(ctx, {.start = true});
    const Commands three = {"get_time", "get_date", "home"};
    const auto events = Run(ctx, {.finish = true, .query = "время и дата", .rules = three});
    REQUIRE(events.size() == 1);
    REQUIRE(Of(events[0]) == three);
    REQUIRE(events[0].rules.summary == "get_time,get_date,home");
    REQUIRE(events[0].source == AssistantSessionEvent::Source::RULES);
  }

  SECTION("commands of other ticks are not an answer") {
    Run(ctx, {.start = true, .rules = get_time});
    REQUIRE(Run(ctx, {.rules = get_time}).empty());
    Run(ctx, {.finish = true, .query = "свет"});
    REQUIRE(Run(ctx, {.rules = get_time}).empty());
    REQUIRE(Replied(Run(ctx, {.result = Final("q", "Готово.")}), "Готово."));
  }

  SECTION("debug text answered by the rules while the assistant thinks: the waiting session closes empty first") {
    Run(ctx, {.start = true});
    Run(ctx, {.finish = true, .query = "включи свет"});
    const auto events = Run(ctx, {.query = "который час", .rules = get_time});
    REQUIRE(events.size() == 3);
    REQUIRE(events[0].kind == Kind::REPLY);
    REQUIRE(events[0].text.empty());
    REQUIRE(!events[0].rules.commands.has_value());
    REQUIRE(events[1].kind == Kind::STARTED);
    REQUIRE(RepliedWith({events[2]}, "get_time"));
    REQUIRE(Run(ctx, {.result = Final("q", "Включил свет.")}).empty());
  }
}

TEST_CASE(
  "assistant_session: a declined query gets the fallback rules answer of its own tick", "[assistant][node][session]"
) {
  auto ctx = SessionWithRules("{fallback_text: 'Ошибка'}");
  const auto greeting = Command("greeting");

  SECTION("held from the query tick, given on the decline") {
    Run(ctx, {.start = true});
    REQUIRE(Run(ctx, {.finish = true, .query = "привет", .fallback = greeting}).empty());
    REQUIRE(Run(ctx, {.fallback = Command("get_time")}).empty());
    const auto events = Run(ctx, {.result = Declined()});
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].text.empty());
    REQUIRE(Of(events[0]) == greeting);
    REQUIRE(events[0].rules.summary == "greeting");
  }

  SECTION("without a fallback answer — fallback_text") {
    Run(ctx, {.start = true});
    Run(ctx, {.finish = true, .query = "привет"});
    REQUIRE(Replied(Run(ctx, {.result = Declined()}), "Ошибка"));
  }

  SECTION("a decline in the query tick belongs to the query before") {
    Run(ctx, {.start = true});
    auto both = Tick{.finish = true, .query = "привет", .result = Declined(), .fallback = greeting};
    REQUIRE(Run(ctx, both).empty());
    REQUIRE(Replied(Run(ctx, {.result = Final("q", "Здравствуйте!")}), "Здравствуйте!"));
  }

  SECTION("an answer of the assistant is taken, the fallback is dropped") {
    Run(ctx, {.start = true});
    Run(ctx, {.finish = true, .query = "привет", .fallback = greeting});
    REQUIRE(Replied(Run(ctx, {.result = Final("q", "Здравствуйте!")}), "Здравствуйте!"));
    Run(ctx, {.start = true});
    Run(ctx, {.finish = true, .query = "свет"});
    REQUIRE(Replied(Run(ctx, {.result = Declined()}), "Ошибка"));
  }

  SECTION("a decline in the tick of a new hotword belongs to the session it closes: nobody gets the fallback") {
    Run(ctx, {.start = true});
    Run(ctx, {.finish = true, .query = "привет", .fallback = greeting});
    const auto events = Run(ctx, {.start = true, .result = Declined()});
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].kind == Kind::REPLY);
    REQUIRE(events[0].text.empty());
    REQUIRE(!events[0].rules.commands.has_value());
    REQUIRE(events[1].kind == Kind::STARTED);
    const auto silence = Run(ctx, {.finish = true});
    REQUIRE(Replied(silence, ""));
    REQUIRE(!silence[0].rules.commands.has_value());
  }

  SECTION("a decline with the end of recognition in one tick still gets the fallback") {
    Run(ctx, {.start = true});
    Run(ctx, {.query = "привет", .fallback = greeting});
    const auto events = Run(ctx, {.finish = true, .result = Declined()});
    REQUIRE(events.size() == 1);
    REQUIRE(Of(events[0]).size() == 1);
  }

  SECTION("without the inputs a decline is fallback_text too") {
    auto plain = CreateNode("assistant_session", "{fallback_text: 'Ошибка'}");
    Run(plain, {.start = true});
    Run(plain, {.finish = true, .query = "привет"});
    REQUIRE(Replied(Run(plain, {.result = Declined()}), "Ошибка"));
  }
}

TEST_CASE(
  "assistant_session: a failed turn gets the rules answer, a text answer is the assistant's",
  "[assistant][node][session]"
) {
  auto ctx = CreateNode(
    "assistant_session",
    "{fallback_text: 'Ошибка', listen_again_marker: '<await_user>', max_listen_again: 2, reply_timeout: 3, "
    "rules_on_failure: true, local_debug_enabled: true}",
    {},
    {"quick_commands", "fallback_quick_commands"},
    {},
    {},
    nullptr,
    {"timed_out"}
  );
  const auto get_time = Command("get_time");
  bool timed_out = false;
  const auto step = [&ctx, &timed_out](const Tick& tick) {
    auto events = Run(ctx, tick);
    timed_out = NodeGet<bool>(ctx, "timed_out");
    return events;
  };
  const auto ask = [&step, &get_time](const std::string& query) {
    step({.start = true});
    REQUIRE(step({.finish = true, .query = query, .fallback = get_time}).empty());
  };
  const auto failed = [](const std::string& error) {
    auto result = Final("q", "");
    result.status = AssistantStatus::ERROR;
    result.error = error;
    result.finish_reason = StreamFinishReason::ERROR;
    return result;
  };

  SECTION("any error of the turn") {
    for (const auto& error :
         {"Malformed tool call",
          "Incomplete tool call",
          "Repeated tool call",
          "Home scene snapshot error",
          "upstream generation error"}) {
      ask("который час");
      REQUIRE(RepliedWith(step({.result = failed(error)}), "get_time"));
    }
  }

  SECTION("an error after tool steps") {
    ask("включи свет");
    auto call = Final("q", "");
    call.tool_calls.push_back(ToolCall{.name = "set_device_parameter"});
    REQUIRE(step({.result = call}).empty());
    REQUIRE(RepliedWith(step({.result = failed("Repeated tool call")}), "get_time"));
  }

  SECTION("a text cut by the token limit") {
    ask("который час");
    auto cut = Final("q", "D D D D D D");
    cut.finish_reason = StreamFinishReason::MAX_TOKENS;
    REQUIRE(RepliedWith(step({.result = cut}), "get_time"));
  }

  SECTION("an empty answer, also blank or only the marker") {
    for (const auto& text : {"", "  \n", "<await_user>"}) {
      ask("который час");
      REQUIRE(RepliedWith(step({.result = Final("q", text)}), "get_time"));
    }
  }

  SECTION("no rules answer — fallback_text, logged as the fallback") {
    step({.start = true});
    step({.finish = true, .query = "сколько будет семь на восемь"});
    {
      CapturedCerr log;
      REQUIRE(Replied(step({.result = failed("Malformed tool call")}), "Ошибка"));
      REQUIRE(log.Has("fallback reply reason='Malformed tool call'"));
      REQUIRE_FALSE(log.Has("rules reply"));
    }
    step({.start = true});
    step({.finish = true, .query = "сколько будет семь на восемь"});
    for (int tick = 0; tick < 3; ++tick) {
      step({});
    }
    CapturedCerr log;
    REQUIRE(Replied(step({}), "Ошибка"));
    REQUIRE(timed_out);
    REQUIRE(log.Has("fallback reply reason='reply timeout'"));
    REQUIRE_FALSE(log.Has("rules reply"));
  }

  SECTION("a refusal in text and a question stay the assistant's answer") {
    ask("расскажи новости");
    REQUIRE(Replied(
      step({.result = Final("q", "Я пока не умею рассказывать новости.")}), "Я пока не умею рассказывать новости."
    ));
    ask("свет");
    REQUIRE(Replied(step({.result = Final("q", "Где? <await_user>")}), "Где?", true));
  }

  SECTION("no answer in time: the rules answer, and the turn is to be cancelled once") {
    ask("включи свет");
    for (int tick = 0; tick < 3; ++tick) {
      REQUIRE(step({}).empty());
      REQUIRE_FALSE(timed_out);
    }
    {
      CapturedCerr log;
      REQUIRE(RepliedWith(step({}), "get_time"));
      REQUIRE(log.Has("rules reply reason='reply timeout'"));
    }
    REQUIRE(timed_out);
    REQUIRE(step({.result = Final("q", "Включил свет.")}).empty());
    REQUIRE_FALSE(timed_out);
  }

  SECTION("an answer in time does not time out") {
    ask("включи свет");
    step({});
    step({});
    REQUIRE(Replied(step({.result = Final("q", "Включил свет.")}), "Включил свет."));
    REQUIRE_FALSE(timed_out);
    for (int tick = 0; tick < 5; ++tick) {
      REQUIRE(step({}).empty());
      REQUIRE_FALSE(timed_out);
    }
  }
}

TEST_CASE(
  "assistant_session: without rules_on_failure only a decline gets the rules, as in released configs",
  "[assistant][node][session]"
) {
  auto ctx = SessionWithRules("{fallback_text: 'Ошибка', reply_timeout: 2, local_debug_enabled: true}");
  const auto get_time = Command("get_time");
  const auto ask = [&ctx, &get_time] {
    Run(ctx, {.start = true});
    Run(ctx, {.finish = true, .query = "который час", .fallback = get_time});
  };

  ask();
  auto error = Final("q", "");
  error.status = AssistantStatus::ERROR;
  {
    CapturedCerr log;
    REQUIRE(Replied(Run(ctx, {.result = error}), "Ошибка"));
    REQUIRE(log.Has("fallback reply reason='error'"));
  }

  ask();
  auto cut = Final("q", "D D D");
  cut.finish_reason = StreamFinishReason::MAX_TOKENS;
  REQUIRE(Replied(Run(ctx, {.result = cut}), "D D D"));

  ask();
  REQUIRE(Replied(Run(ctx, {.result = Final("q", "")}), ""));

  ask();
  Run(ctx, {});
  Run(ctx, {});
  {
    CapturedCerr log;
    REQUIRE(Replied(Run(ctx, {}), "Ошибка"));
    REQUIRE(log.Has("fallback reply reason='reply timeout'"));
    REQUIRE_FALSE(log.Has("rules reply"));
  }

  ask();
  REQUIRE(RepliedWith(Run(ctx, {.result = Declined()}), "get_time"));
}

TEST_CASE("assistant_session: follow_up marks the session that continues the dialogue", "[assistant][node][session]") {
  auto ctx = CreateNode(
    "assistant_session",
    "{listen_again_marker: '<await_user>', max_listen_again: 1}",
    {},
    {},
    {},
    {},
    nullptr,
    {"follow_up"}
  );
  const auto follow_up = [&ctx](const Tick& tick) {
    Run(ctx, tick);
    return NodeGet<bool>(ctx, "follow_up");
  };
  const auto ask = Final("q", "Где? <await_user>");

  REQUIRE_FALSE(follow_up({.start = true}));
  REQUIRE_FALSE(follow_up({.finish = true, .query = "свет"}));
  REQUIRE_FALSE(follow_up({.result = ask}));
  REQUIRE(follow_up({.start = true, .manual = true}));
  REQUIRE(follow_up({.finish = true, .query = "на кухне"}));
  REQUIRE_FALSE(follow_up({.result = ask}));
  REQUIRE_FALSE(follow_up({.start = true, .manual = true}));
  REQUIRE_FALSE(follow_up({.finish = true}));

  SECTION("a hotword after the question starts a new dialogue") {
    follow_up({.start = true});
    follow_up({.finish = true, .query = "свет"});
    follow_up({.result = ask});
    REQUIRE_FALSE(follow_up({.start = true}));
  }

  SECTION("a reply without listen_again ends the continuation") {
    follow_up({.start = true});
    follow_up({.finish = true, .query = "свет"});
    follow_up({.result = ask});
    REQUIRE(follow_up({.start = true, .manual = true}));
    REQUIRE(follow_up({.finish = true, .query = "на кухне"}));
    REQUIRE_FALSE(follow_up({.result = Final("q", "Включил.")}));
    REQUIRE_FALSE(follow_up({.start = true, .manual = true}));
  }
}

TEST_CASE("assistant_session: a rules answer ends the continuation", "[assistant][node][session]") {
  auto ctx = CreateNode(
    "assistant_session",
    "{listen_again_marker: '<await_user>', max_listen_again: 2}",
    {},
    {"quick_commands", "fallback_quick_commands"},
    {},
    {},
    nullptr,
    {"follow_up"}
  );
  const auto follow_up = [&ctx](const Tick& tick) {
    Run(ctx, tick);
    return NodeGet<bool>(ctx, "follow_up");
  };
  follow_up({.start = true});
  follow_up({.finish = true, .query = "свет"});
  REQUIRE_FALSE(follow_up({.result = Final("q", "Где? <await_user>")}));
  REQUIRE(follow_up({.start = true, .manual = true}));
  REQUIRE_FALSE(follow_up({.finish = true, .query = "который час", .rules = Command("get_time")}));
  REQUIRE_FALSE(follow_up({.start = true, .manual = true}));
}

TEST_CASE("assistant_session: random activity keeps starts and replies strictly paired", "[assistant][node][session]") {
  for (const unsigned seed : {1u, 7u, 42u, 65u, 2026u}) {
    INFO("seed " << seed);
    auto ctx = SessionWithRules(
      "{listen_again_marker: '?', max_listen_again: 2, reply_timeout: 30, fallback_text: 'x'}"
    );
    std::mt19937 rng(seed);
    const auto chance = [&rng](int percent) {
      return static_cast<int>(rng() % 100) < percent;
    };
    int open = 0;
    int listen_in_a_row = 0;
    const auto check = [&](const AssistantSessionEvents& events) {
      for (const auto& event : events) {
        if (event.kind == Kind::STARTED) {
          REQUIRE(open == 0);
          ++open;
          if (!event.manual) {
            listen_in_a_row = 0;
          }
        } else {
          REQUIRE(open == 1);
          --open;
          listen_in_a_row = event.listen_again ? listen_in_a_row + 1 : 0;
          REQUIRE(listen_in_a_row <= 2);
        }
      }
    };
    for (int tick = 0; tick < 3000; ++tick) {
      Tick t;
      t.start = chance(4);
      t.manual = chance(3);
      t.finish = chance(5);
      if (chance(4)) {
        t.query = "запрос";
      }
      if (chance(20)) {
        t.rules = Command("get_time");
      }
      if (chance(50)) {
        t.fallback = Command("get_date");
      }
      if (chance(10)) {
        t.result = Final(std::to_string(tick), chance(30) ? "Где?" : "Готово.");
        t.result.finish_reason = chance(10) ? StreamFinishReason::CANCELLED
          : chance(10)                      ? StreamFinishReason::DECLINED
                                            : StreamFinishReason::EOS;
        t.result.status = chance(5) ? AssistantStatus::ERROR : AssistantStatus::OK;
        if (chance(30)) {
          t.result.tool_calls.push_back(ToolCall{.name = "set"});
        }
      }
      check(Run(ctx, t));
    }
    check(Run(ctx, {.finish = true}));
    for (int tick = 0; tick < 40; ++tick) {
      check(Run(ctx, {}));
    }
    REQUIRE(open == 0);
  }
}

TEST_CASE("assistant_session: reply source and reason for rules and assistant outcomes", "[assistant][node][session]") {
  using Source = AssistantSessionEvent::Source;
  for (const bool rules_on_failure : {false, true}) {
    for (const bool has_fallback : {false, true}) {
      for (const std::string outcome :
           {"rules", "answer", "declined", "error", "error text", "max_tokens", "empty reply"}) {
        CAPTURE(rules_on_failure, has_fallback, outcome);
        auto ctx = SessionWithRules(
          std::string("{fallback_text: 'Ошибка', rules_on_failure: ") + (rules_on_failure ? "true}" : "false}")
        );
        const auto commands = Command("get_time");
        const auto started = Run(ctx, {.start = true});
        REQUIRE(Started(started, false));
        REQUIRE(started[0].source == Source::NONE);
        REQUIRE(started[0].reason.empty());
        auto events = Run(
          ctx,
          {.finish = true,
           .query = "свет",
           .rules = outcome == "rules" ? commands : Commands{},
           .fallback = has_fallback ? commands : Commands{}}
        );
        Source source = Source::RULES;
        std::string reason;
        if (outcome != "rules") {
          REQUIRE(events.empty());
          auto result = Final("q", "Готово.");
          if (outcome == "declined") {
            result.finish_reason = StreamFinishReason::DECLINED;
            reason = "declined";
          } else if (outcome == "error" || outcome == "error text") {
            result.status = AssistantStatus::ERROR;
            result.finish_reason = StreamFinishReason::ERROR;
            result.error = outcome == "error text" ? "Malformed tool call" : "";
            reason = result.error.empty() ? "error" : result.error;
          } else if (outcome == "max_tokens") {
            result.finish_reason = StreamFinishReason::MAX_TOKENS;
            reason = "max_tokens";
          } else if (outcome == "empty reply") {
            result.text = "  ";
            reason = "empty reply";
          }
          const bool fallback = outcome == "declined" || (rules_on_failure && !reason.empty());
          source = fallback && has_fallback ? Source::FALLBACK_RULES
            : fallback || result.status == AssistantStatus::ERROR
            ? Source::FALLBACK_TEXT
            : Source::ASSISTANT;
          events = Run(ctx, {.result = result});
        }
        REQUIRE(events.size() == 1);
        REQUIRE(events[0].kind == Kind::REPLY);
        REQUIRE(events[0].source == source);
        REQUIRE(events[0].reason == reason);
      }
    }
  }
}

TEST_CASE("assistant_session: timeout source and reason", "[assistant][node][session]") {
  using Source = AssistantSessionEvent::Source;
  for (const bool rules_on_failure : {false, true}) {
    for (const bool has_fallback : {false, true}) {
      CAPTURE(rules_on_failure, has_fallback);
      auto ctx = SessionWithRules(
        std::string("{fallback_text: 'Ошибка', reply_timeout: 1, rules_on_failure: ")
        + (rules_on_failure ? "true}" : "false}")
      );
      Run(ctx, {.start = true});
      REQUIRE(
        Run(ctx, {.finish = true, .query = "свет", .fallback = has_fallback ? Command("get_time") : Commands{}}).empty()
      );
      REQUIRE(Run(ctx, {}).empty());
      const auto events = Run(ctx, {});
      REQUIRE(events.size() == 1);
      REQUIRE(events[0].kind == Kind::REPLY);
      REQUIRE(events[0].source == (rules_on_failure && has_fallback ? Source::FALLBACK_RULES : Source::FALLBACK_TEXT));
      REQUIRE(events[0].reason == "reply timeout");
    }
  }
}

TEST_CASE("assistant_session: empty closing source and reason", "[assistant][node][session]") {
  using Source = AssistantSessionEvent::Source;
  auto ctx = CreateNode("assistant_session", "{}", {}, {"session_cancelled"});
  const auto check = [](const AssistantSessionEvent& event, const std::string& reason) {
    REQUIRE(event.kind == Kind::REPLY);
    REQUIRE(event.source == Source::NONE);
    REQUIRE(event.reason == reason);
    REQUIRE(event.text.empty());
    REQUIRE(!event.rules.commands.has_value());
  };

  SECTION("silence and cancellation through the input") {
    for (const bool cancelled : {false, true}) {
      Run(ctx, {.start = true});
      const auto events = Run(ctx, {.finish = true, .cancelled = cancelled});
      REQUIRE(events.size() == 1);
      check(events[0], cancelled ? "cancelled" : "no query");
    }
  }

  SECTION("a new activation or text query drops the waiting session") {
    for (const bool activation : {false, true}) {
      auto fresh = CreateNode("assistant_session", "{}");
      Run(fresh, {.query = "включи свет"});
      const auto events = Run(fresh, {.start = activation, .query = activation ? "" : "выключи свет"});
      REQUIRE(events.size() == 2);
      check(events[0], "dropped");
      REQUIRE(events[1].kind == Kind::STARTED);
      REQUIRE(events[1].source == Source::NONE);
      REQUIRE(events[1].reason.empty());
    }
  }

  SECTION("reset replies first in the next tick, with or without an open session") {
    for (const bool open : {false, true}) {
      auto fresh = CreateNode("assistant_session", "{}");
      if (open) {
        Run(fresh, {.query = "свет"});
      }
      NodePushEvent(fresh, Event{.event = ResetEvent{}});
      const auto events = Run(fresh, {.start = true, .manual = true});
      REQUIRE(events.size() == 2);
      check(events[0], "reset");
      REQUIRE(events[1].kind == Kind::STARTED);
      REQUIRE(events[1].manual);
      REQUIRE(events[1].source == Source::NONE);
      REQUIRE(events[1].reason.empty());
      REQUIRE(Run(fresh, {}).empty());
    }
  }
}

TEST_CASE(
  "assistant_session: the marker is read from the model's text, not from the history", "[assistant][node][session]"
) {
  auto ctx = CreateNode(
    "assistant_session",
    "{fallback_text: 'Ошибка', listen_again_marker: '<await_user>', max_listen_again: 2, reply_timeout: 4}"
  );
  Run(ctx, {.start = true});
  Run(ctx, {.finish = true, .query = "свет"});
  // A marker the model built from ordinary tokens: it is escaped in the history, listening again still works.
  auto result = Final("q0", "На кухне?");
  result.raw_text = "На кухне? <await_user>";
  result.history_text
    = "На кухне? <\xE2\x80\x8B"
      "await_user>";
  const auto events = Run(ctx, {.result = result});
  REQUIRE(events.size() == 1);
  REQUIRE(events[0].text == "На кухне?");
  REQUIRE(events[0].listen_again);
}

TEST_CASE(
  "assistant_session: the reply text and the wait for a follow-up go out for the graph", "[assistant][node][session]"
) {
  auto ctx = CreateNode(
    "assistant_session",
    "{fallback_text: 'Ошибка', listen_again_marker: '<await_user>', max_listen_again: 1, reply_timeout: 2}",
    {},
    {},
    {},
    {},
    nullptr,
    {"reply_text", "listen_again"}
  );
  using Out = std::pair<std::string, bool>;
  const auto run = [&ctx](const Tick& tick) {
    Run(ctx, tick);
    return Out{NodeGet<std::string>(ctx, "reply_text"), NodeGet<bool>(ctx, "listen_again")};
  };

  REQUIRE(run({.start = true}) == Out{"", false});
  REQUIRE(run({.finish = true, .query = "свет"}) == Out{"", false});

  SECTION("a question is said once and waits for a manual start") {
    REQUIRE(run({.result = Final("q", "Где? <await_user>")}) == Out{"Где?", true});
    REQUIRE(run({}) == Out{"", true});
    REQUIRE(run({.start = true, .manual = true}) == Out{"", false});
    REQUIRE(run({.finish = true, .query = "на кухне"}) == Out{"", false});
    REQUIRE(run({.result = Final("q", "Включил.")}) == Out{"Включил.", false});
  }

  SECTION("a hotword after the question ends the wait") {
    REQUIRE(run({.result = Final("q", "Где? <await_user>")}) == Out{"Где?", true});
    REQUIRE(run({.start = true}) == Out{"", false});
  }

  SECTION("the fallback text is a reply too") {
    REQUIRE(run({}) == Out{"", false});
    REQUIRE(run({}) == Out{"", false});
    REQUIRE(run({}) == Out{"Ошибка", false});
  }
}
