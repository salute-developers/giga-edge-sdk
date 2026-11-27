#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/audio/nodes/deinterleave/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Deinterleave node", "[nodes][audio]") {
  auto node = CreateNode("deinterleave", "num_channels: 3");

  NodePush(
    node,
    "interleaved_audio",
    std::vector<float>{
      {1.0, 11.0, 21.0, 2.0, 12.0, 22.0, 3.0, 13.0, 23.0},
    }
  );

  REQUIRE(
    NodeGet<std::vector<std::vector<float>>>(node, "audio")
    == std::vector<std::vector<float>>{
      {1.0, 2.0, 3.0},
      {11.0, 12.0, 13.0},
      {21.0, 22.0, 23.0},
    }
  );
}

TEST_CASE("Deinterleave, empty input", "[nodes][audio]") {
  auto node = CreateNode("deinterleave", "num_channels: 3");

  NodePush(node, "interleaved_audio", std::vector<float>{});
  REQUIRE(NodeGet<std::vector<std::vector<float>>>(node, "audio") == std::vector<std::vector<float>>{{}, {}, {}});
}
