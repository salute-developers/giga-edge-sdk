#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/audio/nodes/resampler/node.h"

#include <catch.hpp>

#include <iostream>
#include <vector>

using namespace giga_edge;

TEST_CASE("Resampler upsample test output size", "[runtime][nodes]") {
  auto node = CreateNode(
    "resampler",
    R"(
      from_freq: 16000
      to_freq: 24000
    )"
  );

  const size_t size = 800;
  std::vector<float> audio(size);
  for (size_t i = 0; i < size; ++i) {
    audio[i] = 0.1;
  }

  NodePush(node, "audio", audio);
  REQUIRE(NodeGet<std::vector<float>>(node, "audio").size() == 1200);
}

TEST_CASE("Resampler downsample test output size", "[runtime][nodes]") {
  auto node = CreateNode(
    "resampler",
    R"(
      from_freq: 16000
      to_freq: 8000
    )"
  );

  const size_t size = 800;
  std::vector<float> audio(size);
  for (size_t i = 0; i < size; ++i) {
    audio[i] = 0.1;
  }

  NodePush(node, "audio", audio);
  REQUIRE(NodeGet<std::vector<float>>(node, "audio").size() == 400);
}

TEST_CASE("Resampler handles chunks not multiple of decimation", "[runtime][nodes]") {
  // 999 samples in odd-sized chunks. 16k->24k (3/2) consumes 998, keeps 1 -> 1497 out; 48k->16k (1/3) -> 333 out
  auto [from, to, expected] = GENERATE(table<uint32_t, uint32_t, size_t>({{16000, 24000, 1497}, {48000, 16000, 333}}));
  auto node = CreateNode("resampler", "{from_freq: " + std::to_string(from) + ", to_freq: " + std::to_string(to) + "}");

  size_t out = 0;
  for (size_t size : {1, 333, 7, 658}) {
    NodePush(node, "audio", std::vector<float>(size, 0.1f));
    out += NodeGet<std::vector<float>>(node, "audio").size();
  }
  REQUIRE(out == expected);
}
