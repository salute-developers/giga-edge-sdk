#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/features/nodes/features/node.h"

#include <catch.hpp>

#include <string>

using namespace giga_edge;

namespace {

constexpr auto kGigaamFeaturesConfig = R"(
  features_type: asr
  sampling_freq: 16000
  frame_size_ms: 20
  frame_stride_ms: 10
  n_fft: 320
  num_filterbank_chans: 64
  low_freq_filterbank: 0
  high_freq_filterbank: 8000
  mel_scale: htk
  window_type: hanning
  is_window_periodic: true
  center: false
  use_mfsc: true
  use_power: true
  log_floor: 1.0e-9
  preem_coef: 0.0
  dither_val: 0.0
  use_energy: false
  raw_energy: false
  zero_mean_frame: false
  normalize_gain: false
  normalize_features_across_axis_0: false
  wav_scaling: false
)";

} // namespace

TEST_CASE("Features node, n_fft shorter than the frame is rejected", "[features][nodes]") {
  REQUIRE_THROWS(CreateNode(
    "features",
    R"(
      features_type: asr
      sampling_freq: 16000
      frame_size_ms: 20
      frame_stride_ms: 10
      n_fft: 256
      num_filterbank_chans: 64
      low_freq_filterbank: 0
      high_freq_filterbank: 8000
      window_type: hanning
      preem_coef: 0.0
      dither_val: 0.0
      use_power: true
      use_energy: false
      raw_energy: false
      zero_mean_frame: false
      normalize_gain: false
      normalize_features_across_axis_0: false
      wav_scaling: false
    )"
  ));
}

TEST_CASE("Features node rejects non-finite logarithm floors", "[features][nodes]") {
  for (const auto floor : {".nan", ".inf", "-1"}) {
    auto config = std::string(kGigaamFeaturesConfig);
    config.replace(
      config.find("log_floor: 1.0e-9"), std::string("log_floor: 1.0e-9").size(), std::string("log_floor: ") + floor
    );
    REQUIRE_THROWS(CreateNode("features", config));
  }
}
