#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/audio/nodes/audio_iterator/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Audio iterator node", "[nodes][audio]") {
  auto node = CreateNode(
    "audio_iterator",
    R"(
      chunk_size: 2
    )"
  );

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
    NodeGet<std::vector<std::vector<float>>>(node, "audio_chunks")
    == std::vector<std::vector<float>>{
      {1.0, 2.0},
      {11.0, 12.0},
      {21.0, 22.0},
    }
  );

  REQUIRE(
    NodeGet<std::vector<std::vector<float>>>(node, "audio_chunks")
    == std::vector<std::vector<float>>{
      {3.0, 4.0},
      {13.0, 14.0},
      {23.0, 24.0},
    }
  );

  NodePush(
    node,
    "audio",
    std::vector<std::vector<float>>{
      {6.0},
      {16.0},
      {26.0},
    }
  );

  REQUIRE(
    NodeGet<std::vector<std::vector<float>>>(node, "audio_chunks")
    == std::vector<std::vector<float>>{
      {5.0, 6.0},
      {15.0, 16.0},
      {25.0, 26.0},
    }
  );
}

TEST_CASE("Audio iterator node, reset", "[nodes][audio]") {
  auto node = CreateNode(
    "audio_iterator",
    R"(
      chunk_size: 3
    )"
  );

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
    NodeGet<std::vector<std::vector<float>>>(node, "audio_chunks")
    == std::vector<std::vector<float>>{
      {1.0, 2.0, 3.0},
      {11.0, 12.0, 13.0},
      {21.0, 22.0, 23.0},
    }
  );

  NodePushEvent(node, {.event = ResetEvent{}});

  NodePush(
    node,
    "audio",
    std::vector<std::vector<float>>{
      {6.0, 7.0, 8.0, 9.0, 10.0},
      {16.0, 17.0, 18.0, 19.0, 20.0},
      {26.0, 27.0, 28.0, 29.0, 30.0},
    }
  );

  REQUIRE(
    NodeGet<std::vector<std::vector<float>>>(node, "audio_chunks")
    == std::vector<std::vector<float>>{
      {6.0, 7.0, 8.0},
      {16.0, 17.0, 18.0},
      {26.0, 27.0, 28.0},
    }
  );
}

TEST_CASE("Audio iterator node, step_size", "[nodes][audio]") {
  auto node = CreateNode(
    "audio_iterator",
    R"(
      chunk_size: 3
      step_size: 2
    )"
  );

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
    NodeGet<std::vector<std::vector<float>>>(node, "audio_chunks")
    == std::vector<std::vector<float>>{
      {1.0, 2.0, 3.0},
      {11.0, 12.0, 13.0},
      {21.0, 22.0, 23.0},
    }
  );

  REQUIRE(
    NodeGet<std::vector<std::vector<float>>>(node, "audio_chunks")
    == std::vector<std::vector<float>>{
      {3.0, 4.0, 5.0},
      {13.0, 14.0, 15.0},
      {23.0, 24.0, 25.0},
    }
  );

  NodePush(
    node,
    "audio",
    std::vector<std::vector<float>>{
      {6.0, 7.0},
      {16.0, 17.0},
      {26.0, 27.0},
    }
  );

  REQUIRE(
    NodeGet<std::vector<std::vector<float>>>(node, "audio_chunks")
    == std::vector<std::vector<float>>{
      {5.0, 6.0, 7.0},
      {15.0, 16.0, 17.0},
      {25.0, 26.0, 27.0},
    }
  );
}

TEST_CASE("Audio iterator node, no channels input", "[nodes][audio]") {
  auto node = CreateNode(
    "audio_iterator",
    R"(
      chunk_size: 2
    )"
  );

  NodePush(
    node,
    "audio",
    std::vector<std::vector<float>>{
      {1.0, 2.0},
      {11.0, 12.0},
    }
  );

  REQUIRE(
    NodeGet<std::vector<std::vector<float>>>(node, "audio_chunks")
    == std::vector<std::vector<float>>{
      {1.0, 2.0},
      {11.0, 12.0},
    }
  );

  NodePush(node, "audio", std::vector<std::vector<float>>{});

  REQUIRE(NodeGet<std::vector<std::vector<float>>>(node, "audio_chunks").empty());

  NodePush(
    node,
    "audio",
    std::vector<std::vector<float>>{
      {3.0, 4.0},
      {13.0, 14.0},
    }
  );

  REQUIRE(
    NodeGet<std::vector<std::vector<float>>>(node, "audio_chunks")
    == std::vector<std::vector<float>>{
      {3.0, 4.0},
      {13.0, 14.0},
    }
  );
}

TEST_CASE("Audio iterator node, empty input", "[nodes][audio]") {
  auto node = CreateNode(
    "audio_iterator",
    R"(
      chunk_size: 3
    )"
  );

  NodePush(node, "audio", std::vector<std::vector<float>>{{}, {}, {}}, true);

  REQUIRE(
    NodeGet<std::vector<std::vector<float>>>(node, "audio_chunks") == std::vector<std::vector<float>>{{}, {}, {}}
  );
}
