#pragma once

#include "spotter_features_params.h"

#include "asr_features/feature_params.h"

#include <variant>

namespace giga_edge {

struct FeatureParams {
  std::variant<SpotterFeatureParams, asr::FeatureParams> params;

  int num_mels;
  int frame_stride_samples;

  void InferValues();
};

struct FeatureIteratorParams {
  int32_t step_size;
  int32_t context_width;
  int32_t num_mels;
};

struct FeatureIteratorWithResetParams : FeatureIteratorParams {
  bool flush_tail = false;
  int32_t flush_tail_count = 1;
};

} // namespace giga_edge