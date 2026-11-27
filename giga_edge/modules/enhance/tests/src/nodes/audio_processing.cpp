#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/enhance/nodes/audio_processing/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("Enhance audio processing node", "[nodes][audio]") {
  auto node = CreateNode("enhance_audio_processing", YAML::Load("{\
sampling_freq: 16000,\
lc_enabled: true,\
lc_target_level_db: -23,\
lc_max_gain_db: 40,\
lc_bypass_time_sec: 3,\
lc_use_vad: true,\
lc_post_processing: true,\
rc_enabled: true,\
rc_target_level_db: -3,\
rc_max_gain_db: 10,\
rc_attack_time: 1e-5,\
rc_release_time: 300,\
rc_hold_time: 5,\
ng_enabled: true,\
ng_threshold_db: -15.0,\
ng_attack_time: 5e-2,\
ng_release_time: 2e-2,\
ng_hold_time: 5e-2,\
lim_enabled: true,\
lim_threshold_db: -3.0,\
lim_knee_width: 0.0,\
lim_attack_time: 0.0,\
lim_release_time: 0.2,\
drc_enabled: true,\
drc_threshold_db: -15.0,\
drc_ratio: 5,\
drc_knee_width: 0.0,\
drc_attack_time: 1e-2,\
drc_release_time: 2e-1,\
agc_enabled: true,\
agc_target_power_level: 1.0,\
agc_max_power_gain: 20.0,\
agc_time_rise: 1e-2,\
agc_time_fall: 1e-2,\
}"));

  const size_t size = 200;
  std::vector<float> vct(size);
  for (size_t i = 0; i < size; ++i) {
    vct[i] = 0.5;
  }

  NodePush(node, "audio", vct, false);

  REQUIRE(NodeGet<std::vector<float>>(node, "audio").size() == size);
}
