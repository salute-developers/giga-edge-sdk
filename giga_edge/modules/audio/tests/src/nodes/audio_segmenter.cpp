#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/audio/nodes/audio_segmenter/node.h"

#include <catch.hpp>

using namespace giga_edge;
namespace {
constexpr auto config = R"(
  sample_rate: 1000
  min_duration_ms: 200
  max_duration_ms: 300
  silence_duration_ms: 40
  silence_threshold_db: -20
)";
NodeContext Segmenter() {
  return CreateNode("audio_segmenter", config, {}, {"need_reset", "push"});
}
std::vector<std::vector<float>> Push(
  NodeContext& node, const std::vector<float>& audio, bool reset = false, bool flush = false
) {
  NodePush(node, "need_reset", reset);
  NodePush(node, "push", flush);
  NodePush(node, "audio", audio);
  auto result = NodeGetBatch<std::vector<float>>(node, "audio_batch");
  const auto resets = NodeGetBatch<bool>(node, "need_reset_batch");
  REQUIRE(resets.size() == result.size());
  for (size_t i = 0; i < result.size(); ++i) {
    REQUIRE(resets[i] == !result[i].empty());
  }
  return result;
}
} // namespace
TEST_CASE("Audio segmenter preserves every sample and chooses a quiet boundary", "[audio][nodes]") {
  auto node = Segmenter();
  std::vector<float> audio(510, 0.5f);
  std::fill(audio.begin() + 220, audio.begin() + 260, 0.f);
  auto chunks = Push(node, audio, true, true);
  REQUIRE(chunks.size() == 2);
  REQUIRE(chunks[0].size() == 240);
  std::vector<float> restored;
  for (const auto& chunk : chunks) {
    restored.insert(restored.end(), chunk.begin(), chunk.end());
  }
  REQUIRE(restored == audio);
}
TEST_CASE("Audio segmenter boundaries do not depend on input packet sizes", "[audio][nodes]") {
  std::vector<float> audio(1013, 0.5f);
  std::fill(audio.begin() + 220, audio.begin() + 260, 0.f);
  auto full = Segmenter();
  const auto expected = Push(full, audio, true, true);
  for (size_t step : {1u, 17u, 300u, 700u}) {
    auto node = Segmenter();
    std::vector<std::vector<float>> chunks;
    for (size_t offset = 0; offset < audio.size(); offset += step) {
      const size_t end = std::min(audio.size(), offset + step);
      auto batch = Push(node, std::vector<float>(audio.begin() + offset, audio.begin() + end), offset == 0);
      for (auto& chunk : batch) {
        if (!chunk.empty()) {
          chunks.push_back(std::move(chunk));
        }
      }
    }
    for (auto& chunk : Push(node, {}, false, true)) {
      if (!chunk.empty()) {
        chunks.push_back(std::move(chunk));
      }
    }
    REQUIRE(chunks == expected);
  }
}
TEST_CASE("Audio segmenter falls back to a fixed bound and resets unfinished audio", "[audio][nodes]") {
  auto node = Segmenter();
  REQUIRE(Push(node, std::vector<float>(100, 9.f), true)[0].empty());
  const std::vector<float> current(650, .5f);
  auto chunks = Push(node, current, true, true);
  REQUIRE(chunks.size() == 3);
  REQUIRE(chunks[0] == std::vector<float>(300, .5f));
  REQUIRE(chunks[1] == std::vector<float>(300, .5f));
  REQUIRE(chunks[2] == std::vector<float>(50, .5f));
  REQUIRE(Push(node, {}, false, true)[0].empty());
}
TEST_CASE("Audio segmenter rejects invalid configuration", "[audio][nodes]") {
  REQUIRE_THROWS(CreateNode("audio_segmenter", "sample_rate: 0"));
  REQUIRE_THROWS(CreateNode("audio_segmenter", "sample_rate: 99"));
  REQUIRE_NOTHROW(
    CreateNode("audio_segmenter", std::string(config) + "  sample_rate: 22050\n", {}, {"need_reset", "push"})
  );
  REQUIRE_THROWS(CreateNode("audio_segmenter", "min_duration_ms: 30000"));
  REQUIRE_THROWS(CreateNode("audio_segmenter", "silence_duration_ms: 0"));
  REQUIRE_THROWS(CreateNode("audio_segmenter", "silence_threshold_db: 0"));
}

TEST_CASE("Audio segmenter handles exact maximum identically before and during flush", "[audio][nodes]") {
  std::vector<float> audio(300, .5f);
  std::fill(audio.begin() + 220, audio.begin() + 260, 0.f);
  auto immediate = Segmenter();
  const auto expected = Push(immediate, audio, true, true);
  auto streamed = Segmenter();
  auto actual = Push(streamed, audio, true);
  auto tail = Push(streamed, {}, false, true);
  actual.insert(actual.end(), tail.begin(), tail.end());
  REQUIRE(actual == expected);
}
