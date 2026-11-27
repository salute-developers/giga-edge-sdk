#include "giga_edge/lib/node/system_events.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/tts_output_stub/node.h"

#include <catch.hpp>

#include <string>
#include <vector>

using namespace giga_edge;

TEST_CASE("TTS stub ends a phrase at once and the one dropped by a reset", "[runtime][nodes]") {
  auto node = CreateNode("tts_output_stub", "");
  const auto tick = [&node](const std::string& text) {
    NodePush(node, "text", text);
    for (const auto& chunk : NodeGetBatch<std::vector<float>>(node, "audio_chunk")) {
      REQUIRE(chunk.empty());
    }
    return NodeGet<bool>(node, "is_eos");
  };
  REQUIRE_FALSE(tick(""));
  REQUIRE(tick("привет"));
  REQUIRE_FALSE(tick(""));
  NodePushEvent(node, Event{.event = ResetEvent{}});
  REQUIRE(tick(""));
  REQUIRE_FALSE(tick(""));
  NodePushEvent(node, Event{.event = ResetEvent{}});
  REQUIRE(tick("привет"));
  REQUIRE_FALSE(tick(""));
}
