// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#pragma once

#include <random>
#include <vector>

namespace asr {

// Dither the signal by adding small amount of random noise to the signal
//    s'(n) = s(n) + q * RND()  where RND() is uniformly distributed in [-1, 1)
//      and `q` is the dithering constant
// Similar to HTK, positive value of `q` causes the same noise signal to be
// added everytime and with negative value of `q`, noise is random and the same
// file may produce slightly different results in different trials

template <typename T>
class Dither {
 public:
  explicit Dither(T dither_val);

  std::vector<T> Apply(const std::vector<T>& input, int feat_start);

  void ApplyInPlace(std::vector<T>& input, int feat_start);

 private:
  std::vector<T> static_dithering_;
};
}  // namespace asr
