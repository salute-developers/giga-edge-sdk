#include "giga_edge/modules/tts/nodes/tts_streaming_audio_output/node.h"

#include "node_ticks.h"

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/interpreter.h"

#include <catch.hpp>
#include <yaml-cpp/yaml.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::test;

// The node over the vits backend on the synthetic models: the sound of the node is the sound of the backend.
namespace {

const std::string kParams
  = "{models_path: vits/synthetic, text_path: vits/text/ru, voice: one, voices: {one: 3, two: 5}, noise_scale: 0,"
    " noise_scale_w: 0}";
const std::string kSync = "backend_type: vits\ntts_interpreter_params: " + kParams + "\n";
const std::string kAsync
  = "backend_type: vits\ninference_strategy: {type: wait_till_inferring, thread_pool_name: tts_pool}\n"
    "tts_interpreter_params: "
  + kParams + "\n";

EnvironmentsMap TtsPool() {
  EnvironmentParams params;
  params.type = EnvironmentParams::Type::THREAD_POOL;
  params.thread_pool_params.threads_count = 1;
  return {{"tts_pool", MakeEnvironment(params)}};
}

std::vector<float> Direct(const std::string& text, const std::string& voice = "") {
  VitsTtsInterpreter tts;
  REQUIRE(
    tts.Initialize(ParseConfig<VitsTtsInterpreterParams>(YAML::Load(kParams)), CreateDataReader("data/"), nullptr)
  );
  REQUIRE(tts.SetVoice(voice));
  REQUIRE(tts.StartSynthesis(text));
  std::vector<float> audio;
  for (int step = 0; !tts.IsFinished(); ++step) {
    REQUIRE(step < 100'000);
    const auto chunk = tts.GetAudioChunk();
    audio.insert(audio.end(), chunk.begin(), chunk.end());
  }
  return audio;
}

// Everything the node has output so far: the audio and the number of is_eos == true.
struct Drained {
  std::vector<float> audio;
  int eos = 0;
};

Drained Drain(NodeContext& node) {
  Drained result;
  while (node.outputs.at("is_eos")->HasNext("test")) {
    result.eos += NodeGet<bool>(node, "is_eos") ? 1 : 0;
  }
  while (node.outputs.at("audio_chunk")->HasNext("test", true)) {
    auto chunks = NodeGetBatch<std::vector<float>>(node, "audio_chunk");
    const auto audio = FlattenAudio(chunks);
    result.audio.insert(result.audio.end(), audio.begin(), audio.end());
  }
  return result;
}

// A tick with the text and ticks without it until the phrase ends.
Drained Speak(NodeContext& node, const std::string& text, bool with_cancel = false) {
  if (with_cancel) {
    NodePush(node, "cancel", false, true, 0);
  }
  NodePush(node, "text", text);
  auto phrase = Drain(node);
  for (int tick = 0; phrase.eos == 0; ++tick) {
    REQUIRE(tick < 100'000);
    std::this_thread::sleep_for(std::chrono::microseconds(200));
    if (with_cancel) {
      NodePush(node, "cancel", false, true, 0);
    }
    NodePush(node, "text", std::string());
    const auto more = Drain(node);
    phrase.audio.insert(phrase.audio.end(), more.audio.begin(), more.audio.end());
    phrase.eos += more.eos;
  }
  return phrase;
}

// Words of the letter 'а' only: the synthetic BERT knows no other (see the tests of the backend).
const std::vector<std::string> kTexts = {"а аа.", "а аа. ааа.", "ааа аа, а ааа аааа. аа: а, ааа."};

} // namespace

TEST_CASE("TtsStreamingAudioOutput over vits gives the sound of the backend in both modes", "[tts][vits][nodes]") {
  auto environments = TtsPool();
  for (const bool async : {false, true}) {
    CAPTURE(async);
    auto node = CreateNodeWithDataReader(
      "tts_streaming_audio_output", async ? kAsync : kSync, CreateDataReader("data/"), {}, {}, {}, &environments
    );
    for (const auto& text : kTexts) {
      CAPTURE(text);
      const auto phrase = Speak(node, text);
      REQUIRE(phrase.eos == 1);
      REQUIRE(phrase.audio == Direct(text));
    }
    // A text without words ends at once and without sound.
    const auto empty = Speak(node, "?!");
    REQUIRE(empty.eos == 1);
    REQUIRE(empty.audio.empty());
    REQUIRE(Speak(node, kTexts[0]).audio == Direct(kTexts[0]));
  }
}

TEST_CASE("TtsStreamingAudioOutput over vits speaks with the voice of the input", "[tts][vits][nodes]") {
  auto node = CreateNodeWithDataReader("tts_streaming_audio_output", kSync, CreateDataReader("data/"), {"voice"});
  NodePush(node, "voice", std::string("two"), true, 0);
  NodePush(node, "text", kTexts[1]);
  auto phrase = Drain(node);
  for (int tick = 0; phrase.eos == 0; ++tick) {
    REQUIRE(tick < 100'000);
    NodePush(node, "voice", std::string("two"), true, 0);
    NodePush(node, "text", std::string());
    const auto more = Drain(node);
    phrase.audio.insert(phrase.audio.end(), more.audio.begin(), more.audio.end());
    phrase.eos += more.eos;
  }
  REQUIRE(phrase.audio == Direct(kTexts[1], "two"));
  REQUIRE(phrase.audio != Direct(kTexts[1]));
}

TEST_CASE("TtsStreamingAudioOutput over vits survives cancels and many phrases", "[tts][vits][nodes]") {
  auto environments = TtsPool();
  const auto expected = Direct(kTexts[1]);
  const auto full = Direct(kTexts[2]);
  for (const bool async : {false, true}) {
    CAPTURE(async);
    auto node = CreateNodeWithDataReader(
      "tts_streaming_audio_output", async ? kAsync : kSync, CreateDataReader("data/"), {"cancel"}, {}, {}, &environments
    );
    for (int round = 0; round < 200; ++round) {
      CAPTURE(round);
      // The beginning of a long phrase, then the cancel after `round % 5` more ticks.
      NodePush(node, "cancel", false, true, 0);
      NodePush(node, "text", kTexts[2], true, 1);
      for (int tick = 0; tick < round % 5; ++tick) {
        NodePush(node, "cancel", false, true, 0);
        NodePush(node, "text", std::string());
      }
      NodePush(node, "cancel", true, true, 0);
      NodePush(node, "text", std::string());
      auto cancelled = Drain(node);
      for (int tick = 0; cancelled.eos == 0; ++tick) {
        REQUIRE(tick < 100'000);
        NodePush(node, "cancel", false, true, 0);
        NodePush(node, "text", std::string());
        const auto more = Drain(node);
        cancelled.audio.insert(cancelled.audio.end(), more.audio.begin(), more.audio.end());
        cancelled.eos += more.eos;
      }
      REQUIRE(cancelled.eos == 1);
      if (round % 5 == 0) {
        // Cut right after its beginning: not the whole phrase.
        REQUIRE(cancelled.audio.size() < full.size());
      }
      if (round % 20 == 0) {
        const auto next = Speak(node, kTexts[1], true);
        REQUIRE(next.eos == 1);
        REQUIRE(next.audio == expected);
      }
    }
    for (int phrase = 0; phrase < 500; ++phrase) {
      const auto spoken = Speak(node, kTexts[1], true);
      REQUIRE(spoken.eos == 1);
      REQUIRE(spoken.audio == expected);
    }
  }
}
