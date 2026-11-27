// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#include "dither.h"

#include <ctime>

namespace asr {

static const int kStaticDithiringSize = 16000;


template <typename T>
Dither<T>::Dither(T dither_val)
    : static_dithering_(kStaticDithiringSize) {
  std::mt19937 rng((dither_val > 0.0) ? 123456 : time(nullptr));
  std::uniform_real_distribution<T> distribution(-1.0, 1.0);
  for (size_t i = 0; i < kStaticDithiringSize ; ++i) {
    static_dithering_[i] = dither_val * distribution(rng);
  }
}

template <typename T>
std::vector<T> Dither<T>::Apply(const std::vector<T>& input, int feat_start) {
  auto output(input);
  ApplyInPlace(output, feat_start);
  return output;
}

template <typename T>
void Dither<T>::ApplyInPlace(std::vector<T>& input, int feat_start) {
  for (size_t i = 0, dither_ind = feat_start ; i < input.size() ; ++i, ++dither_ind) {
    input[i] += static_dithering_[dither_ind % kStaticDithiringSize];
  }
}

template class Dither<float>;
template class Dither<double>;
}  // namespace asr
