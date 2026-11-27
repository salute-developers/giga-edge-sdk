// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#include "pre_emphasis.h"

#include <cstddef>
#include <stdexcept>

namespace asr {

template <typename T>
PreEmphasis<T>::PreEmphasis(T alpha, int64_t N)
    : preem_coef_(alpha), window_length_(N) {
  if (window_length_ <= 1) {
    throw std::invalid_argument("PreEmphasis: windowLength must be > 1");
  }
  if (preem_coef_ < 0.0 || preem_coef_ >= 1.0) {
    throw std::invalid_argument("PreEmphasis: alpha must be in [0, 1)");
  }
}

template <typename T>
std::vector<T> PreEmphasis<T>::Apply(const std::vector<T>& input) const {
  auto output(input);
  ApplyInPlace(output);
  return output;
}

template <typename T>
void PreEmphasis<T>::ApplyInPlace(std::vector<T>& input) const {
  if (input.size() % window_length_ != 0) {
    throw std::invalid_argument("PreEmphasis: input.size() not divisible by windowLength");
  }
  size_t nframes = input.size() / window_length_;
  for (size_t n = nframes; n > 0; --n) {
    size_t e = n * window_length_ - 1;  // end of current frame
    size_t s = (n - 1) * window_length_;  // start of current frame
    for (size_t i = e; i > s; --i) {
      input[i] -= (preem_coef_ * input[i - 1]);
    }
    input[s] *= (1 - preem_coef_);
  }
}

template class PreEmphasis<float>;
template class PreEmphasis<double>;
}  // namespace asr
