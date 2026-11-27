// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#pragma once

#include "feature_params.h"
#include "power_spectrum.h"
#include "speech_utils.h"
#include "tri_filterbank.h"

#include <vector>

namespace asr {

// Computes MFSC features for a speech signal.

template <typename T>
class Mfsc : public PowerSpectrum<T> {
 public:
  explicit Mfsc(const FeatureParams& params);

  virtual ~Mfsc() {}

  // input - input speech signal (T)
  // Returns - MFSC feature (Col Major : FEAT X FRAMESZ)
  std::vector<T> Apply(const std::vector<T>& input, int feat_start = 0) override;

  int64_t OutputSize(int64_t input_sz) override;

 protected:
  // Helper function which takes input as signal after dividing the signal into
  // frames. Main purpose of this function is to reuse it in MFCC code
  std::vector<T> MfscImpl(std::vector<T>& frames);
  void ValidateMfscParams() const;

 private:
  TriFilterbank<T> tri_flt_bank_;
};
}  // namespace asr
