#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/audio/nodes/audio_gather/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Audio gather node", "[nodes][audio]") {
  auto node = CreateNode("audio_gather", YAML::Node{});

  NodePush(
    node,
    "audio",
    std::vector<std::vector<float>>{
      {1.0, 2.0},
      {11.0, 12.0},
      {21.0, 22.0},
    },
    false
  );

  NodePush(
    node,
    "audio",
    std::vector<std::vector<float>>{
      {3.0, 4.0},
      {13.0, 14.0},
      {23.0, 24.0},
    },
    false
  );

  NodePush(
    node,
    "audio",
    std::vector<std::vector<float>>{
      {5.0, 6.0},
      {15.0, 16.0},
      {25.0, 26.0},
    },
    true
  );

  REQUIRE(
    NodeGet<std::vector<std::vector<float>>>(node, "audio")
    == std::vector<std::vector<float>>{
      {1.0, 2.0, 3.0, 4.0, 5.0, 6.0},
      {11.0, 12.0, 13.0, 14.0, 15.0, 16.0},
      {21.0, 22.0, 23.0, 24.0, 25.0, 26.0},
    }
  );
}

TEST_CASE("Audio gather node, empty input", "[nodes][audio]") {
  auto node = CreateNode("audio_gather", YAML::Node{});

  NodePush(node, "audio", std::vector<std::vector<float>>{{}, {}, {}}, true);

  REQUIRE(NodeGet<std::vector<std::vector<float>>>(node, "audio") == std::vector<std::vector<float>>{{}, {}, {}});
}
