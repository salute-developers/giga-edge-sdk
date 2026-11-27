#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/audio/nodes/extract_channels/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Extract channels node", "[nodes][audio]") {
  auto node = CreateNode("extract_channels", "channels: [0, 2]");

  NodePush(
    node,
    "audio",
    std::vector<std::vector<float>>{
      {1.0, 2.0, 3.0, 4.0, 5.0},
      {11.0, 12.0, 13.0, 14.0, 15.0},
      {21.0, 22.0, 23.0, 24.0, 25.0},
    }
  );

  REQUIRE(
    NodeGet<std::vector<std::vector<float>>>(node, "audio")
    == std::vector<std::vector<float>>{
      {1.0, 2.0, 3.0, 4.0, 5.0},
      {21.0, 22.0, 23.0, 24.0, 25.0},
    }
  );
}

TEST_CASE("Extract channels, empty input", "[nodes][audio]") {
  auto node = CreateNode("extract_channels", "channels: [0, 2]");

  NodePush(node, "audio", std::vector<std::vector<float>>{{}, {}, {}});
  REQUIRE(NodeGet<std::vector<std::vector<float>>>(node, "audio") == std::vector<std::vector<float>>{{}, {}});
}
