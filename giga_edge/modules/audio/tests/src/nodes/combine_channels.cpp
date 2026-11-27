#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/audio/nodes/combine_channels/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Combine channels node", "[nodes][audio]") {
  auto node = CreateNode("combine_channels", YAML::Node{}, {}, {}, {"channel_0", "channel_1", "channel_2"});

  NodePush(
    node,
    "channel_0",
    std::vector<float>{
      {1.0, 2.0, 3.0, 4.0, 5.0},
    }
  );
  NodePush(
    node,
    "channel_1",
    std::vector<float>{
      {11.0, 12.0, 13.0, 14.0, 15.0},
    }
  );
  NodePush(
    node,
    "channel_2",
    std::vector<float>{
      {21.0, 22.0, 23.0, 24.0, 25.0},
    }
  );

  REQUIRE(
    NodeGet<std::vector<std::vector<float>>>(node, "audio")
    == std::vector<std::vector<float>>{
      {1.0, 2.0, 3.0, 4.0, 5.0},
      {11.0, 12.0, 13.0, 14.0, 15.0},
      {21.0, 22.0, 23.0, 24.0, 25.0},
    }
  );
}

TEST_CASE("Combine channels node, empty input", "[nodes][audio]") {
  auto node = CreateNode("combine_channels", YAML::Node{}, {}, {}, {"channel_0", "channel_1", "channel_2"});

  NodePush(node, "channel_0", std::vector<float>{});
  NodePush(node, "channel_1", std::vector<float>{});
  NodePush(node, "channel_2", std::vector<float>{});
  REQUIRE(NodeGet<std::vector<std::vector<float>>>(node, "audio") == std::vector<std::vector<float>>{{}, {}, {}});
}
