#include "demo_graphs.h"

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/runtime/runtime.h"
#include "giga_edge/modules/assistant_core/contracts/session.h"
#include "giga_edge/modules/assistant_core/nodes.h"
#include "giga_edge/modules/common/nodes.h"
#include "giga_edge/modules/events/nodes.h"
#include "giga_edge/modules/spotter/nodes.h"

#include <catch.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <any>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::demo_test;

namespace {

constexpr auto kSubactivation = SpotterEventType::SUBACTIVATION;

// The ASR of the test that gives everything it heard since its reset at the end of the phrase.
constexpr std::string_view kFinalAsr = R"(
nodes:
  - {name: words, type: demo_test_words, inputs: {audio: audio}, outputs: {text: words}}
  - name: text
    type: accumulate_text
    inputs: {input: words, restart: need_reset, push: is_eou}
    outputs: {output: text}
)";

using Events = std::map<int, SpotterEventType>;

Events Raw(std::initializer_list<int> ticks) {
  Events events;
  for (const int tick : ticks) {
    events[tick] = kActivation;
  }
  return events;
}

// The ticks with events of the type at the output of the wake word graph. Its model part is left out: the test
// pushes what the model reports (the stream events), the rest of the graph is the demo's.
std::vector<int> Wake(const Events& raw, SpotterEventType type = kActivation) {
  auto config = Synchronous(Part("spotter"));
  YAML::Node tail(YAML::NodeType::Sequence);
  std::set<std::string> streams = {"events"};
  for (const auto& node : config["nodes"]) {
    bool fed = false;
    for (const auto& input : node["inputs"]) {
      fed = fed || streams.count(input.second.as<std::string>()) > 0;
    }
    if (fed) {
      tail.push_back(node);
      for (const auto& output : node["outputs"]) {
        streams.insert(output.second.as<std::string>());
      }
    }
  }
  config["nodes"] = tail;
  Runtime graph(Reader(YAML::Dump(config)));
  REQUIRE(graph.InputNames() == std::vector<std::string>{"events"});

  std::vector<int> result;
  int tick = 0;
  graph.Subscribe<std::vector<SpotterEvent>>({}, "hotword_events", [&](const auto& batch, const std::any&) {
    for (const auto& events : batch) {
      for (const auto& event : events) {
        if (event.event_type == type) {
          result.push_back(tick);
        }
      }
    }
  });
  for (; tick <= raw.rbegin()->first; ++tick) {
    const auto event = raw.find(tick);
    graph.Push({}, "events", event == raw.end() ? std::vector<SpotterEvent>{} : Batch(event->second), {});
  }
  return result;
}

struct Input {
  bool wake = false;
  bool button = false;
  bool cancel = false;
  float audio = kSilence;
  bool played = false;
};

// The wake word comes in the tick of its last chunk.
constexpr Input kWord = {.wake = true, .audio = kName};

std::map<int, size_t> Chunks(int first, int last) {
  std::map<int, size_t> chunks;
  for (const int tick : Ticks(first, last)) {
    chunks[tick] = kChunk;
  }
  return chunks;
}

// The session graph of the demo with its stub of function calling and the EOU and ASR of the test.
class Session {
public:
  explicit Session(std::string_view asr = kStreamingAsr)
    : graph_(Reader(YAML::Dump(Synchronous(Part("offline_assistant"))))) {
    auto inputs = graph_.InputNames();
    std::sort(inputs.begin(), inputs.end());
    REQUIRE(
      inputs
      == std::vector<std::string>{
        "audio",
        "cancel_session",
        "debug_text",
        "dialogue_reset",
        "events_spotter",
        "gender",
        "manual_activation",
        "pronoun",
        "reply_played",
        "speech_synthesized"
      }
    );
    const auto part = CreateDataReader(Part("offline_assistant").string());
    graph_.AddSubmodelVersion(Slot("eou"), Reader(kEou));
    graph_.AddSubmodelVersion(Slot("asr"), Reader(asr));
    graph_.AddSubmodelVersion(Slot("function_calling"), part, "config_function_calling_stub.yaml");
    graph_.AddSubmodelVersion(Slot("query_text"), part, "config_query_text.yaml");
    graph_.SetActiveSubmodelVersions({Slot("eou"), Slot("asr"), Slot("function_calling"), Slot("query_text")});

    for (const auto* flag :
         {"session_start",
          "session_open",
          "session_finish",
          "recognition_session_finish",
          "cancel_session_finish",
          "asr_reset"}) {
      Watch<bool>(flag);
    }
    Watch<std::vector<float>>("audio_for_eou");
    Watch<std::vector<float>>("audio_for_asr");
    Watch<std::string>("query_text_result");
    Watch<std::string>("user_query_text");
    graph_.Subscribe<AssistantSessionEvents>({}, "assistant_session", [this](const auto& events, const std::any&) {
      for (const auto& event : events) {
        events_.push_back(
          std::to_string(ticks_) + (event.kind == AssistantSessionEvent::Kind::STARTED ? " start" : " reply")
        );
      }
    });
  }

  void Run(int ticks, const Input& input = {}) {
    for (int i = 0; i < ticks; ++i, ++ticks_) {
      graph_.Push({}, "audio", std::vector<float>(kChunk, input.audio), {});
      graph_.Push({}, "events_spotter", input.wake ? Batch(kActivation) : std::vector<SpotterEvent>{}, {});
      graph_.Push({}, "manual_activation", input.button, {});
      graph_.Push({}, "cancel_session", input.cancel, {});
      graph_.Push({}, "dialogue_reset", false, {});
      graph_.Push({}, "speech_synthesized", false, {});
      graph_.Push({}, "reply_played", input.played, {});
      graph_.Push({}, "debug_text", std::string(), {});
      graph_.Push({}, "gender", std::string(), {});
      graph_.Push({}, "pronoun", std::string(), {});
    }
  }

  std::vector<int> Where(const std::string& flag) const {
    std::vector<int> ticks;
    for (const auto& [tick, _] : Given(flags_, flag)) {
      ticks.push_back(tick);
    }
    return ticks;
  }

  std::map<int, size_t> Audio(const std::string& signal) const {
    return Given(sizes_, signal);
  }

  // The request to function calling, "<tick> <text>"; the same text goes to the client.
  std::vector<std::string> Query() const {
    const auto query = Given(texts_, "query_text_result");
    CHECK(Given(texts_, "user_query_text") == query);
    std::vector<std::string> said;
    for (const auto& [tick, text] : query) {
      const auto first = text.find_first_not_of(' ');
      said.push_back(std::to_string(tick) + " " + text.substr(first, text.find_last_not_of(' ') + 1 - first));
    }
    return said;
  }

  // The session events for the client: "<tick> start" and "<tick> reply". A session with a request waits for the reply
  // of function calling, the stub does not give it.
  const std::vector<std::string>& Events() const {
    return events_;
  }

private:
  template<class T>
  void Watch(const std::string& signal) {
    graph_.Subscribe<T>({}, signal, [this, signal](const T& value, const std::any&) {
      if constexpr (std::is_same_v<T, bool>) {
        flags_[signal].push_back(value);
      } else if constexpr (std::is_same_v<T, std::string>) {
        texts_[signal].push_back(value);
      } else {
        sizes_[signal].push_back(value.size());
      }
    });
  }

  // The ticks with a value of the signal: true, non-empty audio or text.
  template<class T>
  std::map<int, T> Given(const std::map<std::string, std::vector<T>>& values, const std::string& signal) const {
    const auto& ticks = values.at(signal);
    REQUIRE(ticks.size() == static_cast<size_t>(ticks_));
    std::map<int, T> given;
    for (int tick = 0; tick < ticks_; ++tick) {
      if (ticks[tick] != T{}) {
        given.emplace(tick, ticks[tick]);
      }
    }
    return given;
  }

  int ticks_ = 0;
  std::map<std::string, std::vector<bool>> flags_;
  std::map<std::string, std::vector<size_t>> sizes_;
  std::map<std::string, std::vector<std::string>> texts_;
  std::vector<std::string> events_;
  Runtime graph_;
};

// The word, a noise that only the ASR hears, then a command from the tick `first`: speech, a pause, speech again and
// the end of the phrase at first + 7.
void LateCommand(Session& session, int first) {
  session.Run(1, kWord);
  session.Run(4);
  session.Run(2, {.audio = kNoise});
  session.Run(first - 7);
  session.Run(2, {.audio = kSpeech});
  session.Run(3);
  session.Run(2, {.audio = kSpeech});
  session.Run(1, {.audio = kEndOfPhrase});
}

} // namespace

TEST_CASE("demo wake word: a burst of raw events is one activation", "[assistant][demo][wake]") {
  const auto burst = Raw({0, 1, 2, 4, 5, 8, 11, 12, 15});
  CHECK(Wake(burst) == std::vector<int>{0});
  CHECK(Wake(burst, kSubactivation) == std::vector<int>{1, 2, 4, 5, 8, 11, 12, 15});
}

TEST_CASE("demo wake word: a new activation 14 ticks after the last raw event", "[assistant][demo][wake]") {
  CHECK(Wake(Raw({0, 13})) == std::vector<int>{0});
  CHECK(Wake(Raw({0, 14})) == std::vector<int>{0, 14});
  CHECK(Wake(Raw({3, 4, 17})) == std::vector<int>{3});
  CHECK(Wake(Raw({3, 4, 18})) == std::vector<int>{3, 18});
}

TEST_CASE("demo wake word: a suppressed event extends the window", "[assistant][demo][wake]") {
  CHECK(Wake(Raw({0, 13, 26, 39, 52})) == std::vector<int>{0});
  CHECK(Wake(Raw({0, 13, 27})) == std::vector<int>{0, 27});
}

TEST_CASE("demo wake word: a single event activates, other events do not open the window", "[assistant][demo][wake]") {
  CHECK(Wake(Raw({7})) == std::vector<int>{7});
  const Events events = {{0, kSubactivation}, {2, kActivation}, {9, kSubactivation}};
  CHECK(Wake(events) == std::vector<int>{2});
  CHECK(Wake(events, kSubactivation) == std::vector<int>{0, 9});
}

TEST_CASE("demo session: the word without speech waits for speech 3 s", "[assistant][demo][session]") {
  Session session;
  session.Run(1, kWord);
  session.Run(5);
  session.Run(1, {.audio = kEndOfPhrase});
  session.Run(142);
  CHECK(session.Where("session_finish") == std::vector<int>{60});
  CHECK(session.Where("recognition_session_finish") == std::vector<int>{60});
  CHECK(session.Where("session_open") == Ticks(0, 59));
  CHECK(session.Where("asr_reset") == std::vector<int>{0});
  CHECK(session.Query().empty());
  CHECK(session.Events() == std::vector<std::string>{"0 start", "60 reply"});
}

TEST_CASE("demo session: speech right after the word ends with the phrase", "[assistant][demo][session]") {
  Session session(GENERATE(kStreamingAsr, kFinalAsr));
  session.Run(35);
  session.Run(2, {.audio = kNoise});
  session.Run(1, kWord);
  session.Run(1, {.audio = kEndOfPhrase});
  session.Run(2, {.audio = kSpeech});
  session.Run(1, {.audio = kEndOfPhrase});
  session.Run(2);
  CHECK(session.Where("session_start") == std::vector<int>{37});
  CHECK(session.Where("session_finish") == std::vector<int>{41});
  CHECK(session.Where("recognition_session_finish") == std::vector<int>{41});
  CHECK(session.Where("asr_reset") == std::vector<int>{37});
  CHECK(session.Audio("audio_for_eou") == Chunks(37, 41));
  CHECK(session.Audio("audio_for_asr").at(37) == 27200);
  CHECK(session.Query() == std::vector<std::string>{"41 свет свет"});
}

TEST_CASE("demo session: speech from tick 20 after the word keeps the recognition", "[assistant][demo][session]") {
  Session session(GENERATE(kStreamingAsr, kFinalAsr));
  LateCommand(session, 20);
  CHECK(session.Where("asr_reset") == std::vector<int>{0});
  CHECK(session.Where("session_finish") == std::vector<int>{27});
  CHECK(session.Query() == std::vector<std::string>{"27 мм мм свет свет свет свет"});
}

TEST_CASE(
  "demo session: speech from tick 21 after the word restarts the recognition once", "[assistant][demo][session]"
) {
  Session session(GENERATE(kStreamingAsr, kFinalAsr));
  LateCommand(session, 21);
  CHECK(session.Where("asr_reset") == std::vector<int>{0, 21});
  CHECK(session.Audio("audio_for_asr").at(21) == 8000);
  CHECK(session.Audio("audio_for_asr").at(22) == kChunk);
  CHECK(session.Where("session_finish") == std::vector<int>{28});
  CHECK(session.Where("recognition_session_finish") == std::vector<int>{28});
  CHECK(session.Query() == std::vector<std::string>{"28 свет свет свет свет"});
}

TEST_CASE("demo session: the name is cut off only while the word is in the recognition", "[assistant][demo][session]") {
  const int first = GENERATE(3, 21);
  Session session(GENERATE(kStreamingAsr, kFinalAsr));
  session.Run(1, kWord);
  session.Run(first - 1);
  session.Run(1, {.audio = kName});
  session.Run(1, {.audio = kSpeech});
  session.Run(1, {.audio = kEndOfPhrase});
  CHECK(session.Query() == std::vector<std::string>{std::to_string(first + 2) + (first < 21 ? " свет" : " ёж свет")});
}

TEST_CASE("demo session: the name is cut off in a spelling the graph lists", "[assistant][demo][session]") {
  const bool listed = GENERATE(true, false);
  Session session(GENERATE(kStreamingAsr, kFinalAsr));
  session.Run(1, {.wake = true, .audio = listed ? kNameSpelled : kNameUnknown});
  session.Run(1, {.audio = kSpeech});
  session.Run(1, {.audio = kEndOfPhrase});
  CHECK(session.Query() == std::vector<std::string>{listed ? "2 свет" : "2 йош свет"});
}

TEST_CASE("demo session: the restart of the recognition restarts the session timer", "[assistant][demo][session]") {
  const int first = GENERATE(30, 55);
  Session session;
  session.Run(1, kWord);
  session.Run(first - 1);
  session.Run(20, {.audio = kSpeech});
  session.Run(121);
  CHECK(session.Where("asr_reset") == std::vector<int>{0, first});
  CHECK(session.Where("session_finish") == std::vector<int>{first + 140});
  CHECK(session.Where("recognition_session_finish") == std::vector<int>{first + 140});
}

TEST_CASE("demo session: a button session keeps the recognition", "[assistant][demo][session]") {
  const bool word = GENERATE(false, true);
  Session session;
  session.Run(1, {.wake = word, .button = true});
  session.Run(29);
  session.Run(5, {.audio = kSpeech});
  session.Run(1, {.audio = kEndOfPhrase});
  session.Run(2);
  CHECK(session.Where("session_start") == std::vector<int>{0});
  CHECK(session.Where("asr_reset") == std::vector<int>{0});
  CHECK(session.Audio("audio_for_eou") == Chunks(1, 35));
  // The chunk of the button tick does not reach the ASR either.
  CHECK(session.Audio("audio_for_asr").count(0) == 0);
  CHECK(session.Audio("audio_for_asr").at(1) == kChunk);
  CHECK(session.Where("session_finish") == std::vector<int>{35});
}

TEST_CASE("demo session: a cancel closes the session without a result", "[assistant][demo][session]") {
  const int cancel = GENERATE(5, 35, 45, 51);
  Session session;
  for (int tick = 0; tick < 55; ++tick) {
    const float audio = tick >= 31 && tick < 41 ? kSpeech : tick == 51 ? kEndOfPhrase : kSilence;
    session.Run(1, {.wake = tick == 1, .cancel = tick == 0 || tick == cancel, .audio = audio});
  }
  CHECK(session.Where("session_finish") == std::vector<int>{cancel});
  CHECK(session.Where("cancel_session_finish") == std::vector<int>{cancel});
  CHECK(session.Where("recognition_session_finish").empty());
  CHECK(session.Where("session_open") == Ticks(1, cancel - 1));
  CHECK(session.Where("asr_reset") == (cancel > 31 ? std::vector<int>{1, 31} : std::vector<int>{1}));
  CHECK(session.Query().empty());
  CHECK(session.Events() == std::vector<std::string>{"1 start", std::to_string(cancel) + " reply"});
}

TEST_CASE("demo session: a repeated word starts a new session and its window", "[assistant][demo][session]") {
  const int first = GENERATE(30, 31);
  Session session(GENERATE(kStreamingAsr, kFinalAsr));
  session.Run(1, kWord);
  session.Run(9);
  session.Run(1, kWord);
  session.Run(first - 11);
  session.Run(2, {.audio = kSpeech});
  session.Run(1, {.audio = kEndOfPhrase});
  CHECK(session.Where("session_start") == std::vector<int>{0, 10});
  CHECK(session.Where("session_finish") == std::vector<int>{first + 2});
  CHECK(session.Where("asr_reset") == (first > 30 ? std::vector<int>{0, 10, 31} : std::vector<int>{0, 10}));
  CHECK(session.Events() == std::vector<std::string>{"0 start", "10 reply", "10 start"});
  CHECK(session.Query() == std::vector<std::string>{std::to_string(first + 2) + " свет свет"});
}

TEST_CASE("demo session: a word after speech starts a session without it", "[assistant][demo][session]") {
  Session session;
  session.Run(1, kWord);
  session.Run(1);
  session.Run(2, {.audio = kSpeech});
  session.Run(36);
  session.Run(1, kWord);
  session.Run(1, {.audio = kEndOfPhrase});
  session.Run(2, {.audio = kSpeech});
  session.Run(1, {.audio = kEndOfPhrase});
  CHECK(session.Where("session_start") == std::vector<int>{0, 40});
  CHECK(session.Where("session_finish") == std::vector<int>{44});
  CHECK(session.Events() == std::vector<std::string>{"0 start", "40 reply", "40 start"});
  CHECK(session.Query() == std::vector<std::string>{"44 свет свет"});
}

TEST_CASE("demo session: a repeated word restarts the session timer", "[assistant][demo][session]") {
  Session session;
  session.Run(1, kWord);
  session.Run(99, {.audio = kSpeech});
  session.Run(1, kWord);
  session.Run(150, {.audio = kSpeech});
  CHECK(session.Where("session_finish") == std::vector<int>{240});
}

TEST_CASE("demo session: a button in a word session starts a button session", "[assistant][demo][session]") {
  Session session;
  session.Run(1, kWord);
  session.Run(9);
  session.Run(1, {.button = true});
  session.Run(24);
  session.Run(2, {.audio = kSpeech});
  session.Run(1, {.audio = kEndOfPhrase});
  CHECK(session.Where("session_start") == std::vector<int>{0, 10});
  CHECK(session.Where("asr_reset") == std::vector<int>{0, 10});
  CHECK(session.Where("session_finish") == std::vector<int>{37});
  CHECK(session.Events() == std::vector<std::string>{"0 start", "10 reply", "10 start"});
  CHECK(session.Query() == std::vector<std::string>{"37 свет свет"});
}

TEST_CASE("demo session: a word in a button session starts a word session", "[assistant][demo][session]") {
  Session session;
  session.Run(1, {.button = true});
  session.Run(9);
  session.Run(1, kWord);
  session.Run(20);
  session.Run(2, {.audio = kSpeech});
  session.Run(1, {.audio = kEndOfPhrase});
  CHECK(session.Where("session_start") == std::vector<int>{0, 10});
  CHECK(session.Where("asr_reset") == std::vector<int>{0, 10, 31});
  CHECK(session.Where("session_finish") == std::vector<int>{33});
  CHECK(session.Events() == std::vector<std::string>{"0 start", "10 reply", "10 start"});
  CHECK(session.Query() == std::vector<std::string>{"33 свет свет"});
}

TEST_CASE("demo session: the next session starts clean", "[assistant][demo][session]") {
  // Session 1: the word at 0, a restart at 25, the end at 32. Session 2: the word at 33, the end of a phrase before
  // speech, a restart at 54, the timeout at 54 + 140. Session 3: the button at 195, a command, the end at 230.
  Session session;
  LateCommand(session, 25);
  session.Run(1, kWord);
  session.Run(1, {.audio = kEndOfPhrase});
  session.Run(19);
  session.Run(5, {.audio = kSpeech});
  session.Run(136);
  session.Run(1, {.button = true});
  session.Run(29);
  session.Run(5, {.audio = kSpeech});
  session.Run(1, {.audio = kEndOfPhrase});
  CHECK(session.Where("session_start") == std::vector<int>{0, 33, 195});
  CHECK(session.Where("asr_reset") == std::vector<int>{0, 25, 33, 54, 195});
  CHECK(session.Where("session_finish") == std::vector<int>{32, 194, 230});
}

TEST_CASE("demo session: a played reply without a question opens no session", "[assistant][demo][session]") {
  Session session;
  session.Run(1, kWord);
  session.Run(3, {.played = true});
  CHECK(session.Where("session_start") == std::vector<int>{0});
  CHECK(session.Events() == std::vector<std::string>{"0 start"});
}

namespace {

// 1.5 s of silence, the word in 4 ticks and a command of 2 ticks right after it; the wake word fires in the first
// silent tick after them, as with a short command said together with the word: EOU hears none of it.
int GluedCommand(Session& session) {
  session.Run(30);
  session.Run(4, {.audio = kName});
  session.Run(2, {.audio = kSpeech});
  session.Run(1, {.wake = true, .audio = kSilence});
  return 36;
}

} // namespace

TEST_CASE("demo session: a command said with the word ends after the wait for speech", "[assistant][demo][session]") {
  Session session(GENERATE(kStreamingAsr, kFinalAsr));
  const int word = GluedCommand(session);
  session.Run(70);
  CHECK(session.Where("session_start") == std::vector<int>{word});
  CHECK(session.Where("session_finish") == std::vector<int>{word + 60});
  CHECK(session.Where("recognition_session_finish") == std::vector<int>{word + 60});
  CHECK(session.Query() == std::vector<std::string>{std::to_string(word + 60) + " свет свет"});
}

TEST_CASE("demo session: speech that starts by the end of the wait keeps the session", "[assistant][demo][session]") {
  const int first = GENERATE(10, 40, 58, 60, 61);
  Session session(GENERATE(kStreamingAsr, kFinalAsr));
  session.Run(1, kWord);
  session.Run(first - 1);
  session.Run(5, {.audio = kSpeech});
  session.Run(1, {.audio = kEndOfPhrase});
  session.Run(5);
  const int finish = first > 60 ? 60 : first + 5;
  CHECK(session.Where("session_finish") == std::vector<int>{finish});
  CHECK(session.Where("asr_reset") == (first > 20 && first <= 60 ? std::vector<int>{0, first} : std::vector<int>{0}));
  CHECK(
    session.Query()
    == (first > 60 ? std::vector<std::string>{} : std::vector<std::string>{std::to_string(finish) + " свет свет свет свет свет"})
  );
}

TEST_CASE("demo session: a long command right after the word is not cut by the wait", "[assistant][demo][session]") {
  Session session;
  session.Run(1, kWord);
  session.Run(100, {.audio = kSpeech});
  session.Run(1, {.audio = kEndOfPhrase});
  session.Run(2);
  CHECK(session.Where("session_finish") == std::vector<int>{101});
}

TEST_CASE("demo session: heard speech without an end of phrase waits for the 7 s limit", "[assistant][demo][session]") {
  Session session;
  session.Run(1, kWord);
  session.Run(2, {.audio = kSpeech});
  session.Run(142);
  CHECK(session.Where("session_finish") == std::vector<int>{140});
  CHECK(session.Query() == std::vector<std::string>{"140 свет свет"});
}

TEST_CASE("demo session: a button session does not wait for speech 3 s", "[assistant][demo][session]") {
  Session session;
  session.Run(1, {.button = true});
  session.Run(142);
  CHECK(session.Where("session_finish") == std::vector<int>{140});
  CHECK(session.Events() == std::vector<std::string>{"0 start", "140 reply"});
}

TEST_CASE("demo session: a button in a word session ends its wait for speech", "[assistant][demo][session]") {
  Session session;
  session.Run(1, kWord);
  session.Run(9);
  session.Run(1, {.button = true});
  session.Run(142);
  CHECK(session.Where("session_start") == std::vector<int>{0, 10});
  CHECK(session.Where("session_finish") == std::vector<int>{150});
}

TEST_CASE("demo session: a repeated word restarts the wait for speech", "[assistant][demo][session]") {
  Session session;
  session.Run(1, kWord);
  session.Run(39);
  session.Run(1, kWord);
  session.Run(70);
  CHECK(session.Where("session_start") == std::vector<int>{0, 40});
  CHECK(session.Where("session_finish") == std::vector<int>{100});
  CHECK(session.Events() == std::vector<std::string>{"0 start", "40 reply", "40 start", "100 reply"});
}

TEST_CASE("demo session: a cancel ends the wait for speech", "[assistant][demo][session]") {
  const int cancel = GENERATE(30, 60);
  Session session;
  for (int tick = 0; tick < 100; ++tick) {
    session.Run(1, {.wake = tick == 0, .cancel = tick == cancel, .audio = tick == 0 ? kName : kSilence});
  }
  CHECK(session.Where("session_finish") == std::vector<int>{cancel});
  CHECK(session.Where("cancel_session_finish") == std::vector<int>{cancel});
  CHECK(session.Where("recognition_session_finish").empty());
  CHECK(session.Events() == std::vector<std::string>{"0 start", std::to_string(cancel) + " reply"});
}

TEST_CASE(
  "demo session: a button session after a cancelled word session is not ended by its wait", "[assistant][demo][session]"
) {
  Session session;
  for (int tick = 0; tick < 185; ++tick) {
    session.Run(
      1, {.wake = tick == 0, .button = tick == 40, .cancel = tick == 30, .audio = tick == 0 ? kName : kSilence}
    );
  }
  CHECK(session.Where("session_start") == std::vector<int>{0, 40});
  CHECK(session.Where("session_finish") == std::vector<int>{30, 180});
  CHECK(session.Events() == std::vector<std::string>{"0 start", "30 reply", "40 start", "180 reply"});
}
