#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/tts/nodes/tts_playback_timer/node.h"

#include <catch.hpp>

#include <string>
#include <utility>
#include <vector>

using namespace giga_edge;

namespace {

// One tick with the audio of the given length; at 1000 Hz a sample is a millisecond.
bool Tick(NodeContext& node, size_t ms, bool is_last = false) {
  NodePushSingle(
    node, "audio", SynthesizedAudio{.samples = std::vector<float>(ms), .sample_rate = 1000, .is_last = is_last}
  );
  return NodeGet<bool>(node, "played");
}

} // namespace

TEST_CASE("tts_playback_timer: a phrase ends when its audio has run out in real time", "[nodes][tts]") {
  auto node = CreateNode("tts_playback_timer", "{tick_ms: 50}");

  // The synthesis is faster than the playback: 220 ms of audio come in two ticks and are played in five.
  REQUIRE_FALSE(Tick(node, 120));
  REQUIRE_FALSE(Tick(node, 100, true));
  REQUIRE_FALSE(Tick(node, 0));
  REQUIRE_FALSE(Tick(node, 0));
  REQUIRE(Tick(node, 0));
  REQUIRE_FALSE(Tick(node, 0));

  SECTION("the next phrase is counted anew") {
    REQUIRE_FALSE(Tick(node, 60, true));
    REQUIRE(Tick(node, 0));
  }
}

TEST_CASE("tts_playback_timer: audio that runs out before the last chunk does not end the phrase", "[nodes][tts]") {
  auto node = CreateNode("tts_playback_timer", "{tick_ms: 50}");

  REQUIRE_FALSE(Tick(node, 40));
  REQUIRE_FALSE(Tick(node, 0));
  REQUIRE(Tick(node, 30, true));
}

TEST_CASE("tts_playback_timer: a phrase without audio ends at once, after the tail", "[nodes][tts]") {
  auto now = CreateNode("tts_playback_timer", "{tick_ms: 50}");
  REQUIRE(Tick(now, 0, true));

  auto later = CreateNode("tts_playback_timer", "{tick_ms: 50, tail_ms: 100}");
  REQUIRE_FALSE(Tick(later, 0, true));
  REQUIRE(Tick(later, 0));
}

TEST_CASE("tts_playback_timer: the tick length is required", "[nodes][tts]") {
  REQUIRE_THROWS(CreateNode("tts_playback_timer", "{}"));
  REQUIRE_THROWS(CreateNode("tts_playback_timer", "{tick_ms: 50, tail_ms: -1}"));
}

namespace {

// The node with its optional output playing.
NodeContext WithPlaying(const std::string& params) {
  return CreateNode("tts_playback_timer", params, {}, {}, {}, {}, nullptr, {"playing"});
}

// The ticks with playing true; tick i gives audio_ms[i] of audio at 1000 Hz, is_last after the last element.
std::vector<int> Playing(const std::string& params, const std::vector<int>& audio_ms) {
  auto node = WithPlaying(params);
  std::vector<int> playing;
  for (size_t tick = 0; tick < audio_ms.size(); ++tick) {
    NodePushSingle(
      node,
      "audio",
      SynthesizedAudio{
        .samples = std::vector<float>(static_cast<size_t>(audio_ms[tick])),
        .sample_rate = 1000,
        .is_last = tick + 1 == audio_ms.size()
      }
    );
    NodeGet<bool>(node, "played");
    if (NodeGet<bool>(node, "playing")) {
      playing.push_back(static_cast<int>(tick));
    }
  }
  return playing;
}

} // namespace

TEST_CASE("tts_playback_timer: playing lasts from the tick of the first audio for its length", "[nodes][tts]") {
  CHECK(Playing("{tick_ms: 50}", {100, 0, 0, 0}) == std::vector<int>{0, 1});
  CHECK(Playing("{tick_ms: 50}", {101, 0, 0, 0, 0}) == std::vector<int>{0, 1, 2});
  CHECK(Playing("{tick_ms: 50}", {10, 0, 0}) == std::vector<int>{0});
}

TEST_CASE("tts_playback_timer: playing counts the length by the sample rate", "[nodes][tts]") {
  auto node = WithPlaying("{tick_ms: 50}");
  const auto tick = [&node](size_t samples, int sample_rate) {
    NodePushSingle(node, "audio", SynthesizedAudio{.samples = std::vector<float>(samples), .sample_rate = sample_rate});
    NodeGet<bool>(node, "played");
    return NodeGet<bool>(node, "playing");
  };
  CHECK(tick(2400, 24000));
  CHECK(tick(0, 24000));
  CHECK_FALSE(tick(0, 24000));
  // Audio without a sample rate has no length.
  CHECK_FALSE(tick(100, 0));
  CHECK_FALSE(tick(0, 24000));
}

TEST_CASE("tts_playback_timer: playing_tail_ms extends playing after the end of the audio", "[nodes][tts]") {
  CHECK(Playing("{tick_ms: 50, playing_tail_ms: 100}", {100, 0, 0, 0, 0, 0, 0}) == std::vector<int>{0, 1, 2, 3});
  CHECK(Playing("{tick_ms: 50, playing_tail_ms: 120}", {100, 0, 0, 0, 0, 0, 0}) == std::vector<int>{0, 1, 2, 3, 4});
}

TEST_CASE("tts_playback_timer: the chunks of a stream play one after another without a gap", "[nodes][tts]") {
  CHECK(Playing("{tick_ms: 50}", {200, 40, 40, 40, 0, 0, 0, 0, 0}) == std::vector<int>{0, 1, 2, 3, 4, 5, 6});
}

TEST_CASE("tts_playback_timer: audio after the end of the previous one plays at once", "[nodes][tts]") {
  CHECK(Playing("{tick_ms: 50}", {100, 0, 0, 0, 0, 100, 0, 0, 0}) == std::vector<int>{0, 1, 5, 6});
  // Audio in the tail of the previous one: the tail does not delay it, and it gets a tail of its own.
  CHECK(
    Playing("{tick_ms: 50, playing_tail_ms: 100}", {100, 0, 0, 0, 50, 0, 0, 0, 0, 0})
    == std::vector<int>{0, 1, 2, 3, 4, 5, 6}
  );
}

TEST_CASE("tts_playback_timer: phrases without audio are never playing", "[nodes][tts]") {
  auto node = WithPlaying("{tick_ms: 50, tail_ms: 200, playing_tail_ms: 700}");
  for (int tick = 0; tick < 30; ++tick) {
    NodePushSingle(node, "audio", SynthesizedAudio{.sample_rate = 24000, .is_last = tick % 5 == 0});
    NodeGet<bool>(node, "played");
    CHECK_FALSE(NodeGet<bool>(node, "playing"));
  }
}

TEST_CASE("tts_playback_timer: playing_tail_ms does not change played", "[nodes][tts]") {
  auto node = WithPlaying("{tick_ms: 50, playing_tail_ms: 700}");
  std::vector<bool> played;
  for (const auto& [ms, is_last] : std::vector<std::pair<size_t, bool>>{
         {120, false}, {100, true}, {0, false}, {0, false}, {0, false}, {0, false}
       }) {
    played.push_back(Tick(node, ms, is_last));
    NodeGet<bool>(node, "playing");
  }
  CHECK(played == std::vector<bool>{false, false, false, false, true, false});
}

TEST_CASE("tts_playback_timer: a reset ends playing", "[nodes][tts]") {
  auto node = WithPlaying("{tick_ms: 50, playing_tail_ms: 100}");
  const auto playing = [&node](size_t ms) {
    Tick(node, ms);
    return NodeGet<bool>(node, "playing");
  };
  CHECK(playing(1000));
  CHECK(playing(0));
  node.node->Reset();
  CHECK_FALSE(playing(0));
  CHECK_FALSE(playing(0));
}

TEST_CASE("tts_playback_timer: playing_tail_ms must not be negative", "[nodes][tts]") {
  REQUIRE_THROWS(CreateNode("tts_playback_timer", "{tick_ms: 50, playing_tail_ms: -1}"));
}
