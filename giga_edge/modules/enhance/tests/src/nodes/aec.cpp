#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/enhance/nodes/echo_cancelation/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Enhance aec node", "[nodes][audio]") {
  auto node = CreateNode("enhance_echo_cancelation", YAML::Load("{\
frame_len: 1536,\
config: {\
  echo_cancellation: {\
    sample_rate: 48000,\
    filter_len: 4096,\
    chunk_len_ms: 16,\
    residual_echo_suppression_enabled: true,\
    delay_compensator_enabled: true,\
    type: speex }\
  }\
}"));

  const size_t interleaved_size = 1536 * 4;
  std::vector<float> vct(interleaved_size);
  for (size_t i = 0; i < interleaved_size; ++i) {
    vct[i] = 0.5;
  }

  NodePush(node, "interleaved_audio", vct, false);

  REQUIRE(NodeGet<std::vector<float>>(node, "audio").size() == interleaved_size / 2);
}
