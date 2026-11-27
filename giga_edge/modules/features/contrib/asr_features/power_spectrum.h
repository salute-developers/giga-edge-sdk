// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#pragma once

#include "dither.h"
#include "feature_params.h"
#include "pre_emphasis.h"
#include "windowing.h"
#include "fft_calculator.h"

#include <mutex> // NOLINT
#include <vector>

namespace asr {

// Computes Power Spectrum features for a speech signal.
template <typename T>
class PowerSpectrum {
 public:
  explicit PowerSpectrum(const FeatureParams& params);
  virtual ~PowerSpectrum() = default;

  // input - input speech signal (T)
  // Returns - Power spectrum (Col Major : FEAT X FRAMESZ)
  virtual std::vector<T> Apply(const std::vector<T>& input, int feat_start = 0);
  virtual int64_t OutputSize(int64_t input_size);
  FeatureParams GetFeatureParams() const;

 protected:
  FeatureParams feat_params_;
  std::vector<T> PowSpectrumImpl(std::vector<T>& frames);

  void ValidatePowSpecParams() const;

  Dither<T> dither_;

  // The following classes are defined in the order they are applied
  PreEmphasis<T> pre_emphasis_;
  Windowing<T> windowing_;
  std::mutex fft_mutex_;
  FftCalculator<T> fft_calculator_;
};

}  // namespace asr
