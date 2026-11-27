#include "demo_graphs.h"

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/runtime/runtime.h"
#include "giga_edge/modules/assistant_core/contracts/session.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/assistant_core/nodes.h"
#include "giga_edge/modules/common/nodes.h"
#include "giga_edge/modules/events/nodes.h"
#include "giga_edge/modules/personalization/nodes.h"
#include "giga_edge/modules/spotter/nodes.h"
#include "giga_edge/modules/tts/nodes.h"

#include <catch.hpp>
#include <yaml-cpp/yaml.h>

#include <any>
#include <deque>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::demo_test;

namespace {

// The voice sample rate of tts/config.yaml.
constexpr int kVoiceRate = 24000;

// The wake word model of the test: an activation in a tick of level kName.
class WakeNode : public NodeBase {
public:
  using NodeBase::NodeBase;

  void Reset() override {}

  void ProcessOnce() override {
    PushEvents(GetAudio().front() == kName ? Batch(kActivation) : std::vector<SpotterEvent>{});
  }

  REGISTER_INPUT(demo_test_wake, audio, Audio, std::vector<float>, "")
  REGISTER_OUTPUT(demo_test_wake, events, Events, std::vector<SpotterEvent>, "")
};

REGISTER_NODE(demo_test_wake, WakeNode, "");

// The synthesis of the test: the text is the durations of the phrase pieces in ms separated by spaces, one piece per
// tick (0 is a tick without a ready piece); the phrase ends in the tick after the last piece.
class SynthesisNode : public NodeBase {
public:
  using NodeBase::NodeBase;

  void Reset() override {
    pieces_.clear();
    speaking_ = false;
  }

  void ProcessOnce() override {
    std::istringstream text(GetText());
    for (int ms = 0; text >> ms;) {
      pieces_.push_back(ms);
    }
    if (pieces_.empty()) {
      PushAudioChunk({});
      PushIsEos(std::exchange(speaking_, false));
      return;
    }
    PushAudioChunk(std::vector<float>(static_cast<size_t>(pieces_.front() * kVoiceRate / 1000), 0.1F));
    pieces_.pop_front();
    speaking_ = true;
    PushIsEos(false);
  }

  REGISTER_INPUT(demo_test_synthesis, text, Text, std::string, "")
  // The voice of the persona, as the real synthesis gets it; the test pieces do not depend on it.
  REGISTER_INPUT(demo_test_synthesis, voice, Voice, std::string, "")
  REGISTER_BATCH_OUTPUT(demo_test_synthesis, audio_chunk, AudioChunk, std::vector<float>, "")
  REGISTER_OUTPUT(demo_test_synthesis, is_eos, IsEos, bool, "")

private:
  std::deque<int> pieces_;
  bool speaking_ = false;
};

REGISTER_NODE(demo_test_synthesis, SynthesisNode, "");

// Function calling of the test: the reply to a request is its own text, in the next tick.
class EchoNode : public NodeBase {
public:
  using NodeBase::NodeBase;

  void Reset() override {
    request_.clear();
  }

  void ProcessOnce() override {
    AssistantResult result;
    if (!request_.empty()) {
      result.text = std::exchange(request_, {});
      result.is_final = true;
    }
    request_ = GetText();
    PushAssistantResult(std::move(result));
  }

  REGISTER_INPUT(demo_test_echo, text, Text, std::string, "")
  REGISTER_OUTPUT(demo_test_echo, assistant_result, AssistantResult, AssistantResult, "")

private:
  std::string request_;
};

REGISTER_NODE(demo_test_echo, EchoNode, "");

constexpr std::string_view kEcho = R"(
nodes:
  - {name: echo, type: demo_test_echo, inputs: {text: text}, outputs: {assistant_result: assistant_result}}
)";

// The spotter part with the test model instead of its own: the nodes fed by the model events are the demo's.
std::string SpotterVersion() {
  auto config = YAML::LoadFile((Part("spotter") / "config.yaml").string());
  YAML::Node nodes(YAML::NodeType::Sequence);
  nodes.push_back(YAML::Load("{name: model, type: demo_test_wake, inputs: {audio: audio}, outputs: {events: events}}"));
  std::set<std::string> streams = {"events"};
  for (const auto& node : config["nodes"]) {
    bool fed = false;
    for (const auto& input : node["inputs"]) {
      fed = fed || streams.count(input.second.as<std::string>()) > 0;
    }
    if (fed) {
      nodes.push_back(node);
      for (const auto& output : node["outputs"]) {
        streams.insert(output.second.as<std::string>());
      }
    }
  }
  config["nodes"] = nodes;
  return YAML::Dump(config);
}

// The tts part with the test synthesis instead of the real one.
std::string TtsVersion() {
  auto config = YAML::LoadFile(Demo("tts/config.yaml").string());
  config.remove("model_environments");
  for (auto node : config["nodes"]) {
    if (node["type"].as<std::string>() == "tts_streaming_audio_output") {
      node["type"] = "demo_test_synthesis";
      node.remove("params");
    }
  }
  return YAML::Dump(config);
}

SubmodelVersionChain Nested(const std::string& id) {
  return {.ids_chain = {{.id = "offline_assistant", .version = "test"}, {.id = id, .version = "test"}}};
}

struct Turn {
  bool wake = false;
  bool button = false;
  // A text request: the reply is the same text, its numbers are the pieces of the synthesis.
  std::string ask;
};

// The demo core with the real offline_assistant part: the wake word model, function calling and the synthesis are test
// nodes, and the reply audio of a tick goes to tts_audio as the client gets it. With synthesis it runs the tts part,
// otherwise the synthesis stub of offline_assistant.
class Assistant {
public:
  explicit Assistant(bool synthesis)
    : graph_(Reader(YAML::Dump(Core()))) {
    const auto part = CreateDataReader(Part("offline_assistant").string());
    graph_.AddSubmodelVersion(Slot("spotter"), Reader(SpotterVersion()));
    graph_.AddSubmodelVersion(Slot("offline_assistant"), part);
    graph_.AddSubmodelVersion(Nested("eou"), Reader(kEou));
    graph_.AddSubmodelVersion(Nested("asr"), Reader(kStreamingAsr));
    graph_.AddSubmodelVersion(Nested("function_calling"), Reader(kEcho));
    graph_.AddSubmodelVersion(Nested("query_text"), part, "config_query_text_debug.yaml");
    if (synthesis) {
      graph_.AddSubmodelVersion(Slot("tts"), Reader(TtsVersion()));
    } else {
      graph_.AddSubmodelVersion(Slot("tts"), part, "config_tts_stub.yaml");
    }
    graph_.SetActiveSubmodelVersions(
      {Slot("spotter"),
       Slot("offline_assistant"),
       Nested("eou"),
       Nested("asr"),
       Nested("function_calling"),
       Nested("query_text"),
       Slot("tts")}
    );

    // playing of a tick turns the wake word off in the next one.
    graph_.Subscribe<bool>({}, "reply_playing_now", [this](const bool& playing, const std::any&) {
      if (playing) {
        playing_.push_back(ticks_ + 1);
      }
    });
    graph_.Subscribe<AssistantSessionEvents>({}, "assistant_session", [this](const auto& events, const std::any&) {
      for (const auto& event : events) {
        (event.kind == AssistantSessionEvent::Kind::STARTED ? starts_ : replies_).push_back(ticks_);
      }
    });
  }

  void Run(int ticks, const Turn& turn = {}) {
    for (int i = 0; i < ticks; ++i, ++ticks_) {
      graph_.Push({}, "audio", std::vector<float>(kChunk, turn.wake ? kName : kSilence), {});
      graph_.Push({}, "debug_text", turn.ask, {});
      graph_.Push({}, "manual_activation", turn.button, {});
      graph_.Push({}, "cancel_session", false, {});
      graph_.Push({}, "dialogue_reset", false, {});
    }
  }

  // The ticks in which the wake word is off.
  const std::vector<int>& Playing() const {
    return playing_;
  }

  // The ticks in which sessions start.
  const std::vector<int>& Starts() const {
    return starts_;
  }

  // The ticks of the session results.
  const std::vector<int>& Replies() const {
    return replies_;
  }

private:
  static YAML::Node Core() {
    auto config = Synchronous(Demo("model/core"));
    // Only the real function_calling needs MCP.
    config.remove("model_environments");
    return config;
  }

  int ticks_ = 0;
  std::vector<int> playing_;
  std::vector<int> starts_;
  std::vector<int> replies_;
  Runtime graph_;
};

} // namespace

TEST_CASE("demo reply: the wake word is off while the reply sounds and its tail", "[assistant][demo][reply]") {
  // A request in tick 9 gets a 2 s reply in tick 10, its audio comes in ticks 10 and 11 without a gap, the client plays
  // it from tick 11, and with the 700 ms tail it lasts to tick 64.
  const int word = GENERATE(11, 35, 64, 65);
  Assistant assistant(true);
  for (int tick = 0; tick < 70; ++tick) {
    assistant.Run(1, {.wake = tick == word, .ask = tick == 9 ? "1000 1000" : ""});
  }
  CHECK(assistant.Playing() == Ticks(11, 64));
  CHECK(assistant.Starts() == (word == 65 ? std::vector<int>{9, 65} : std::vector<int>{9}));
}

TEST_CASE("demo reply: the window of a reply in pieces has no gaps", "[assistant][demo][reply]") {
  // The reply comes in tick 10. Pieces of 200 and 200 ms accumulate up to the 400 ms lead and go to the client in tick
  // 11, then 1000 ms in tick 13 and 800 ms in tick 17: 2.2 s of audio from tick 12 and the tail last to tick 69.
  const int word = GENERATE(11, 12, 15, 30, 69, 70);
  Assistant assistant(true);
  for (int tick = 0; tick < 80; ++tick) {
    assistant.Run(1, {.wake = tick == word, .ask = tick == 9 ? "200 200 0 1000 0 0 0 800" : ""});
  }
  CHECK(assistant.Playing() == Ticks(12, 69));
  CHECK(assistant.Starts() == (word == 11 || word == 70 ? std::vector<int>{9, word} : std::vector<int>{9}));
}

TEST_CASE("demo reply: without the part tts the wake word is never off", "[assistant][demo][reply]") {
  Assistant assistant(false);
  assistant.Run(9);
  assistant.Run(1, {.ask = "1000 1000"});
  assistant.Run(20);
  assistant.Run(1, {.wake = true});
  assistant.Run(20);
  CHECK(assistant.Playing().empty());
  CHECK(assistant.Starts() == std::vector<int>{9, 30});
}

TEST_CASE("demo reply: a button opens a session while the reply sounds", "[assistant][demo][reply]") {
  Assistant assistant(true);
  assistant.Run(9);
  assistant.Run(1, {.ask = "1000 1000"});
  assistant.Run(20);
  assistant.Run(1, {.button = true});
  assistant.Run(9);
  assistant.Run(1, {.wake = true});
  assistant.Run(30);
  CHECK(assistant.Playing() == Ticks(11, 64));
  CHECK(assistant.Starts() == std::vector<int>{9, 30});
}

TEST_CASE("demo reply: the listening after a question opens while the wake word is off", "[assistant][demo][reply]") {
  // A 1 s question in tick 10 is played by tick 33 with the 200 ms tail of played, and the session listens again in
  // tick 34; the wake word is off to tick 44.
  const int word = GENERATE(40, 44, 45);
  Assistant assistant(true);
  for (int tick = 0; tick < 60; ++tick) {
    assistant.Run(1, {.wake = tick == word, .ask = tick == 9 ? "1000 <await_user>" : ""});
  }
  CHECK(assistant.Playing() == Ticks(11, 44));
  CHECK(assistant.Starts() == (word == 45 ? std::vector<int>{9, 34, 45} : std::vector<int>{9, 34}));
}

TEST_CASE(
  "demo reply: the listening after a question does not end after the wait for speech", "[assistant][demo][reply]"
) {
  // The session that listens again in tick 34 is opened as by a button: without speech only the 7 s limit ends it. The
  // result in tick 0 is the one assistant_session gives when its version is switched on.
  Assistant assistant(true);
  for (int tick = 0; tick < 180; ++tick) {
    assistant.Run(1, {.ask = tick == 9 ? "1000 <await_user>" : ""});
  }
  CHECK(assistant.Starts() == std::vector<int>{9, 34});
  CHECK(assistant.Replies() == std::vector<int>{0, 10, 174});
}
