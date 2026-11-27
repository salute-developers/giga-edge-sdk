#include "giga_edge/modules/tts/nodes/tts_streaming_audio_output/node.h"

#include "node_ticks.h"

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"

#include <catch.hpp>

#include <string>
#include <vector>

using namespace giga_edge;
using namespace giga_edge::test;

// The fake backend is tests/src/interpreter/fake/interpreter.cpp: one chunk per character, 2 samples of
// "character code / 256" each.
namespace {

constexpr auto kFake = R"(
    backend_type: fake
    tts_interpreter_params: {samples_per_char: 2, fail_text: broken, fail_char: "!"}
)";

constexpr auto kAsyncFake = R"(
    backend_type: fake
    inference_strategy: {type: wait_till_inferring, thread_pool_name: tts_pool}
    tts_interpreter_params: {samples_per_char: 2, fail_text: broken, fail_char: "!"}
)";

std::vector<float> Expected(const std::string& text, float sign = 1.0F) {
  std::vector<float> audio;
  for (const char c : text) {
    audio.insert(audio.end(), 2, sign * static_cast<float>(static_cast<unsigned char>(c)) / 256.0F);
  }
  return audio;
}

EnvironmentsMap TtsPool() {
  EnvironmentParams params;
  params.type = EnvironmentParams::Type::THREAD_POOL;
  params.thread_pool_params.threads_count = 1;
  return {{"tts_pool", MakeEnvironment(params)}};
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

// A whole phrase in async mode: a tick with the text and ticks without it until is_eos.
Phrase AsyncPhrase(NodeContext& node, const std::string& text) {
  NodePush(node, "text", text);
  auto started = NodeGetBatch<std::vector<float>>(node, "audio_chunk");
  auto head = FlattenAudio(started);
  if (NodeGet<bool>(node, "is_eos")) {
    return {head, true};
  }
  auto phrase = TickUntilEos(node);
  phrase.audio.insert(phrase.audio.begin(), head.begin(), head.end());
  return phrase;
}

} // namespace

TEST_CASE("TtsStreamingAudioOutput takes a backend registered by the test binary", "[tts][nodes][runtime]") {
  auto node = CreateNodeWithDataReader("tts_streaming_audio_output", kFake, CreateDataReader("data/"));
  NodePush(node, "text", std::string("abc"));
  auto chunks = NodeGetBatch<std::vector<float>>(node, "audio_chunk");
  REQUIRE(chunks.size() == 4); // a chunk per character and an empty one at the end of the phrase
  REQUIRE(chunks.back().empty());
  REQUIRE(FlattenAudio(chunks) == Expected("abc"));
  REQUIRE(NodeGet<bool>(node, "is_eos"));

  NodePush(node, "text", std::string());
  auto idle = NodeGetBatch<std::vector<float>>(node, "audio_chunk");
  REQUIRE(FlattenAudio(idle).empty());
  REQUIRE_FALSE(NodeGet<bool>(node, "is_eos"));
}

TEST_CASE("TtsStreamingAudioOutput fails to start when the backend does not initialize", "[tts][nodes][runtime]") {
  REQUIRE_THROWS_WITH(
    CreateNodeWithDataReader(
      "tts_streaming_audio_output",
      "{backend_type: fake, tts_interpreter_params: {fail_initialize: true}}",
      CreateDataReader("data/")
    ),
    Catch::Contains("Failed to initialize TTS interpreter")
  );
}

TEST_CASE("TtsStreamingAudioOutput sync cancel mid-phrase ends it with one eos", "[tts][nodes][runtime]") {
  auto node = CreateNodeWithDataReader("tts_streaming_audio_output", kFake, CreateDataReader("data/"), {"cancel"});
  NodePush(node, "cancel", false, true, 0);
  // A tick with the text and several synthesis iterations: the beginning of the phrase.
  const std::string text(1000, 'a');
  NodePush(node, "text", text, true, 1);
  NodePush(node, "cancel", true, true, 0);
  NodePush(node, "text", std::string());
  for (int tick = 0; tick < 3; ++tick) {
    NodePush(node, "cancel", false, true, 0);
    NodePush(node, "text", std::string());
  }
  const auto cancelled = Drain(node);
  REQUIRE(cancelled.eos == 1);
  REQUIRE_FALSE(cancelled.audio.empty());
  REQUIRE(cancelled.audio.size() < Expected(text).size());
  REQUIRE(cancelled.audio == Expected(text.substr(0, cancelled.audio.size() / 2)));

  NodePush(node, "cancel", false, true, 0);
  NodePush(node, "text", std::string("xy"));
  const auto next = Drain(node);
  REQUIRE(next.eos == 1);
  REQUIRE(next.audio == Expected("xy"));
}

TEST_CASE("TtsStreamingAudioOutput sync cancel with a text starts that text next", "[tts][nodes][runtime]") {
  auto node = CreateNodeWithDataReader("tts_streaming_audio_output", kFake, CreateDataReader("data/"), {"cancel"});
  NodePush(node, "cancel", false, true, 0);
  const std::string text(1000, 'a');
  NodePush(node, "text", text, true, 1);
  // The tick that cancels the phrase brings the next one.
  NodePush(node, "cancel", true, true, 0);
  NodePush(node, "text", std::string("xy"));
  const auto cancelled = Drain(node);
  REQUIRE(cancelled.eos == 1);
  REQUIRE(cancelled.audio.size() < Expected(text).size());
  REQUIRE(cancelled.audio == Expected(text.substr(0, cancelled.audio.size() / 2)));

  NodePush(node, "cancel", false, true, 0);
  NodePush(node, "text", std::string());
  const auto next = Drain(node);
  REQUIRE(next.eos == 1);
  REQUIRE(next.audio == Expected("xy"));
}

TEST_CASE("TtsStreamingAudioOutput async cancel with a text starts that text next", "[tts][nodes][runtime]") {
  auto environments = TtsPool();
  auto node = CreateNodeWithDataReader(
    "tts_streaming_audio_output", kAsyncFake, CreateDataReader("data/"), {"cancel"}, {}, {}, &environments
  );
  NodePush(node, "cancel", false, true, 0);
  const std::string text(1000, 'a');
  NodePush(node, "text", text);
  // The tick that cancels the phrase brings the next one.
  NodePush(node, "cancel", true, true, 0);
  NodePush(node, "text", std::string("xy"));
  const auto cancelled = Drain(node);
  REQUIRE(cancelled.eos == 1);
  REQUIRE(cancelled.audio == Expected(text.substr(0, cancelled.audio.size() / 2)));

  const auto next = TickUntilEos(node, {"cancel"});
  REQUIRE(next.finished);
  REQUIRE(next.audio == Expected("xy"));
}

TEST_CASE("TtsStreamingAudioOutput sync failed synthesis ends the phrase and keeps working", "[tts][nodes][runtime]") {
  auto node = CreateNodeWithDataReader("tts_streaming_audio_output", kFake, CreateDataReader("data/"));
  NodePush(node, "text", std::string("broken"));
  const auto failed_start = Drain(node);
  REQUIRE(failed_start.eos == 1);
  REQUIRE(failed_start.audio.empty());

  NodePush(node, "text", std::string("ab!cd"));
  const auto failed_chunk = Drain(node);
  REQUIRE(failed_chunk.eos == 1);
  REQUIRE(failed_chunk.audio == Expected("ab"));

  NodePush(node, "text", std::string("ok"));
  const auto next = Drain(node);
  REQUIRE(next.eos == 1);
  REQUIRE(next.audio == Expected("ok"));
}

TEST_CASE("TtsStreamingAudioOutput async gives the sync audio with one eos per phrase", "[tts][nodes][runtime]") {
  auto environments = TtsPool();
  auto node = CreateNodeWithDataReader(
    "tts_streaming_audio_output", kAsyncFake, CreateDataReader("data/"), {}, {}, {}, &environments
  );
  for (const std::string text : {"abc", "hello"}) {
    const auto phrase = AsyncPhrase(node, text);
    REQUIRE(phrase.finished);
    REQUIRE(phrase.audio == Expected(text));
    NodePush(node, "text", std::string());
    NodeGetBatch<std::vector<float>>(node, "audio_chunk");
    REQUIRE_FALSE(NodeGet<bool>(node, "is_eos"));
  }
}

TEST_CASE("TtsStreamingAudioOutput async failed synthesis ends the phrase and keeps working", "[tts][nodes][runtime]") {
  auto environments = TtsPool();
  auto node = CreateNodeWithDataReader(
    "tts_streaming_audio_output", kAsyncFake, CreateDataReader("data/"), {}, {}, {}, &environments
  );
  const auto failed = AsyncPhrase(node, "broken");
  REQUIRE(failed.finished);
  REQUIRE(failed.audio.empty());

  const auto failed_chunk = AsyncPhrase(node, "ab!cd");
  REQUIRE(failed_chunk.finished);
  REQUIRE(failed_chunk.audio == Expected("ab"));

  const auto next = AsyncPhrase(node, "ok");
  REQUIRE(next.finished);
  REQUIRE(next.audio == Expected("ok"));
}

TEST_CASE("TtsStreamingAudioOutput passes the voice to the backend", "[tts][nodes][runtime]") {
  auto node = CreateNodeWithDataReader("tts_streaming_audio_output", kFake, CreateDataReader("data/"), {"voice"});
  const auto phrase = [&](const std::string& voice) {
    NodePush(node, "voice", voice, true, 0);
    NodePush(node, "text", std::string("ab"));
    auto chunks = NodeGetBatch<std::vector<float>>(node, "audio_chunk");
    REQUIRE(NodeGet<bool>(node, "is_eos"));
    return FlattenAudio(chunks);
  };
  REQUIRE(phrase("inverted") == Expected("ab", -1.0F));
  REQUIRE(phrase("unknown") == Expected("ab"));
  REQUIRE(phrase("") == Expected("ab"));
}
