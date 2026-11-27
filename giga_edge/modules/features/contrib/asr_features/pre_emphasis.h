// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#pragma once

#include <cstdint>
#include <vector>

namespace asr {

// Pre-emphasise the signal by applying the first order difference equation
//    s'(n) = s(n) - k * s(n-1)  where k in [0, 1)
template <typename T>
class PreEmphasis {
 public:
  PreEmphasis(T alpha, int64_t N);

  std::vector<T> Apply(const std::vector<T>& input) const;

  void ApplyInPlace(std::vector<T>& input) const;

 private:
  T preem_coef_;
  int64_t window_length_;
};
}  // namespace asr
