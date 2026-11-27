// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#pragma once

#include <cstdint>
#include <vector>

#include "feature_params.h"

namespace asr {

template <typename T>
class TriFilterbank {
 public:
  TriFilterbank(
      int64_t numfilters,
      int64_t filterlen,
      int64_t samplingfreq,
      int64_t lowfreq = 0,
      int64_t highfreq = -1,
      FrequencyScale freqscale = FrequencyScale::MEL);

  std::vector<T> Apply(const std::vector<T>& input) const;

  // Returns triangular filterbank matrix
  std::vector<T> Filterbank() const;

 private:
  int64_t num_filters_;  // Number of filterbank channels
  int64_t filter_len_;  // length of each filterbank channel
  int64_t sampling_freq_;  // sampling frequency (Hz)
  int64_t low_freq_;  // lower cutoff frequency (Hz)
  int64_t high_freq_;  // higher cutoff frequency (Hz)
  FrequencyScale freq_scale_;  // frequency warp type Ex. FrequencyScale::MEL
  std::vector<T> H_;  // (numFilters_ x filterLen_) triangular filterbank matrix

  T HertzToWarpedScale(T hz, FrequencyScale freqscale) const;
  T WarpedToHertzScale(T wrp, FrequencyScale freqscale) const;
};
}  // namespace asr
