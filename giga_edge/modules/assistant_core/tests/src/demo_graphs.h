#pragma once

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/submodel_version/submodel_version.h"
#include "giga_edge/modules/spotter/spotter_event.h"

#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Helpers of the tests on the graphs of example/offline_assistant.
namespace giga_edge::demo_test {

inline constexpr auto kActivation = SpotterEventType::ACTIVATION;

// A tick is 50 ms of audio, all samples of its chunk have one level. The EOU of the test hears speech from level 1 and
// the end of a phrase from level 2, the ASR of the test writes a word for every chunk of a level from kWords.
inline constexpr size_t kChunk = 800;
inline constexpr float kSilence = 0, kNoise = 0.5F, kSpeech = 1, kName = 1.5F, kEndOfPhrase = 2;
// The name as the recognizer may write it: a spelling the session graph knows and one it does not.
inline constexpr float kNameSpelled = 1.25F, kNameUnknown = 1.75F;
inline const std::map<float, std::string> kWords = {
  {kNoise, "мм"}, {kSpeech, "свет"}, {kName, "ёж"}, {kNameSpelled, "гига йош"}, {kNameUnknown, "йош"}
};

inline constexpr std::string_view kEou = R"(
nodes:
  - {name: level, type: aggregation, inputs: {input: audio}, outputs: {output: level}, params: {aggregation: max}}
  - {name: loud, type: to_bool, inputs: {float_vector: level}, outputs: {output: loud}, params: {threshold: 1}}
  - {name: end, type: to_bool, inputs: {float_vector: level}, outputs: {output: is_eou}, params: {threshold: 2}}
  - name: speech
    type: logical_expression
    inputs: {loud: loud, is_eou: is_eou}
    outputs: {result: is_speech}
    params: {expression: "loud && !is_eou"}
)";

// The ASR of the test writes the words as it hears them.
inline constexpr std::string_view kStreamingAsr = R"(
nodes:
  - {name: words, type: demo_test_words, inputs: {audio: audio}, outputs: {text: text}}
)";

class WordsNode : public NodeBase {
public:
  using NodeBase::NodeBase;

  void Reset() override {}

  void ProcessOnce() override {
    const auto& audio = GetAudio();
    std::string text;
    for (size_t i = 0; i < audio.size(); i += kChunk) {
      if (const auto word = kWords.find(audio[i]); word != kWords.end()) {
        text += " " + word->second;
      }
    }
    PushText(text);
  }

  REGISTER_INPUT(demo_test_words, audio, Audio, std::vector<float>, "")
  REGISTER_OUTPUT(demo_test_words, text, Text, std::string, "")
};

REGISTER_NODE(demo_test_words, WordsNode, "");

// The graphs of the demo come with its models; the tests run their copies in the test data, laid out as the model of
// the demo. They are read from the source tree: a change of a graph is checked without a rebuild.
inline std::filesystem::path Demo(const std::string& path) {
  return std::filesystem::path(GIGA_EDGE_TEST_SOURCE_DIR) / "giga_edge/modules/assistant_core/tests/data/demo" / path;
}

inline std::filesystem::path Part(const std::string& id) {
  return Demo("model") / id / "model";
}

// The graph runs its nodes inside Push: a tick is over when the last input of it is pushed.
inline YAML::Node Synchronous(const std::filesystem::path& part) {
  auto config = YAML::LoadFile((part / "config.yaml").string());
  config["num_threads"] = 0;
  return config;
}

inline IDataReaderPtr Reader(std::string_view config) {
  return CreateCachingDataReader(
    nullptr, {{"config.yaml", Create<IDataSpanPtr>(std::vector<char>(config.begin(), config.end()))}}
  );
}

inline std::vector<SpotterEvent> Batch(SpotterEventType type) {
  SingleSpotterEvent event{};
  event.event_type = type;
  event.hot_word = "wake";
  return {{event}};
}

inline SubmodelVersionChain Slot(const std::string& id) {
  return {.ids_chain = {{.id = id, .version = "test"}}};
}

inline std::vector<int> Ticks(int first, int last) {
  std::vector<int> ticks;
  for (int tick = first; tick <= last; ++tick) {
    ticks.push_back(tick);
  }
  return ticks;
}

} // namespace giga_edge::demo_test
