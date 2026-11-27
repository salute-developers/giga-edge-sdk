#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/audio/nodes/extract_channel/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Extract channel node", "[nodes][audio]") {
  auto node = CreateNode("extract_channel", "channel: 1");

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
    NodeGet<std::vector<float>>(node, "channel")
    == std::vector<float>{
      {11.0, 12.0, 13.0, 14.0, 15.0},
    }
  );
}

TEST_CASE("Extract channel, empty input", "[nodes][audio]") {
  auto node = CreateNode("extract_channel", "channel: 1");

  NodePush(node, "audio", std::vector<std::vector<float>>{{}, {}, {}});
  REQUIRE(NodeGet<std::vector<float>>(node, "channel").empty());
}
