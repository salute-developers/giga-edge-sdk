#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/tts/nodes/tts_audio_accumulator/node.h"

#include <catch.hpp>

using namespace giga_edge;

namespace {

YAML::Node StreamConfig(int min_lead_ms) {
  YAML::Node config;
  config["stream"] = true;
  config["min_lead_ms"] = min_lead_ms;
  config["sample_rate"] = 1000;
  return config;
}

} // namespace

TEST_CASE("Tts audio accumulator emits whole phrase on eos", "[nodes][tts]") {
  auto node = CreateNode("tts_audio_accumulator", YAML::Node{});

  NodePush(node, "audio_chunk", std::vector<float>{1.0, 2.0}, false);
  NodePush(node, "audio_chunk", std::vector<float>{3.0, 4.0}, false);
  NodePush(node, "audio_chunk", std::vector<float>{5.0}, true);
  NodePushSingle(node, "is_eos", true);

  const auto audio = NodeGet<SynthesizedAudio>(node, "audio");
  REQUIRE(audio.samples == std::vector<float>{1.0, 2.0, 3.0, 4.0, 5.0});
  REQUIRE(audio.sample_rate == 16000);
  REQUIRE(audio.is_last);
}

TEST_CASE("Tts audio accumulator keeps silent while phrase is not finished", "[nodes][tts]") {
  auto node = CreateNode("tts_audio_accumulator", YAML::Node{});

  NodePush(node, "audio_chunk", std::vector<float>{1.0, 2.0}, true);
  NodePushSingle(node, "is_eos", false);

  const auto silent = NodeGet<SynthesizedAudio>(node, "audio");
  REQUIRE(silent.samples.empty());
  REQUIRE_FALSE(silent.is_last);

  NodePush(node, "audio_chunk", std::vector<float>{3.0}, true);
  NodePushSingle(node, "is_eos", true);

  REQUIRE(NodeGet<SynthesizedAudio>(node, "audio").samples == std::vector<float>{1.0, 2.0, 3.0});
}

TEST_CASE("Tts audio accumulator starts a new phrase after eos", "[nodes][tts]") {
  auto node = CreateNode("tts_audio_accumulator", YAML::Node{});

  NodePush(node, "audio_chunk", std::vector<float>{1.0}, true);
  NodePushSingle(node, "is_eos", true);
  REQUIRE(NodeGet<SynthesizedAudio>(node, "audio").samples == std::vector<float>{1.0});

  NodePush(node, "audio_chunk", std::vector<float>{2.0, 3.0}, true);
  NodePushSingle(node, "is_eos", true);
  REQUIRE(NodeGet<SynthesizedAudio>(node, "audio").samples == std::vector<float>{2.0, 3.0});
}

TEST_CASE("Tts audio accumulator streams after the lead and marks the last chunk", "[nodes][tts]") {
  auto node = CreateNode("tts_audio_accumulator", StreamConfig(3));

  NodePush(node, "audio_chunk", std::vector<float>{1.0, 2.0}, true);
  NodePushSingle(node, "is_eos", false);
  REQUIRE(NodeGet<SynthesizedAudio>(node, "audio").samples.empty());

  NodePush(node, "audio_chunk", std::vector<float>{3.0}, true);
  NodePushSingle(node, "is_eos", false);
  const auto lead = NodeGet<SynthesizedAudio>(node, "audio");
  REQUIRE(lead.samples == std::vector<float>{1.0, 2.0, 3.0});
  REQUIRE(lead.sample_rate == 1000);
  REQUIRE_FALSE(lead.is_last);

  NodePush(node, "audio_chunk", std::vector<float>{4.0}, true);
  NodePushSingle(node, "is_eos", false);
  REQUIRE(NodeGet<SynthesizedAudio>(node, "audio").samples == std::vector<float>{4.0});

  NodePush(node, "audio_chunk", std::vector<float>{5.0}, true);
  NodePushSingle(node, "is_eos", true);
  const auto tail = NodeGet<SynthesizedAudio>(node, "audio");
  REQUIRE(tail.samples == std::vector<float>{5.0});
  REQUIRE(tail.is_last);

  NodePush(node, "audio_chunk", std::vector<float>{6.0}, true);
  NodePushSingle(node, "is_eos", false);
  REQUIRE(NodeGet<SynthesizedAudio>(node, "audio").samples.empty());
}

TEST_CASE("Tts audio accumulator flushes a phrase shorter than the lead", "[nodes][tts]") {
  auto node = CreateNode("tts_audio_accumulator", StreamConfig(100));

  NodePush(node, "audio_chunk", std::vector<float>{1.0}, true);
  NodePushSingle(node, "is_eos", true);

  const auto audio = NodeGet<SynthesizedAudio>(node, "audio");
  REQUIRE(audio.samples == std::vector<float>{1.0});
  REQUIRE(audio.is_last);
}
