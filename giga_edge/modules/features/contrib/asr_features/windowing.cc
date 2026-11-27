// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#include "windowing.h"

#include <cmath>
#include <cstddef>
#include <numeric>
#include <stdexcept>

namespace asr {
template <typename T>
Windowing<T>::Windowing(int64_t n, WindowType window_type, bool is_periodic)
    : window_length_(n), window_type_(window_type), coefs_(n), window_periodic_(is_periodic) {
  if (window_length_ <= 1) {
    throw std::invalid_argument("Windowing: window_length_ must be > 1");
  }
  if (window_periodic_) {
    ++window_length_;
    coefs_.push_back(0.0);
  }
  std::iota(coefs_.begin(), coefs_.end(), 0.0);
  switch (window_type) {
    case WindowType::HAMMING:
      for (auto& c : coefs_) {
        c = 0.54 - 0.46 * std::cos(2 * M_PI * c / (window_length_ - 1));
      }
      break;
    case WindowType::HANNING:
      for (auto& c : coefs_) {
        c = 0.5 * (1.0 - std::cos(2 * M_PI * c / (window_length_ - 1)));
      }
      break;
    default:
      throw std::invalid_argument("Windowing: unsupported window type");
  }
}

template <typename T>
std::vector<T> Windowing<T>::Apply(const std::vector<T>& input) const {
  auto output(input);
  ApplyInPlace(output);
  return output;
}

template <typename T>
void Windowing<T>::ApplyInPlace(std::vector<T>& input) const {
  if ((!window_periodic_ && input.size() % (window_length_) != 0) ||
      (window_periodic_ && input.size() % (window_length_ - 1) != 0)) {
    throw std::invalid_argument("Windowing: input size is not divisible by windowLength");
  }
  int64_t n = 0;
  for (auto& in : input) {
    in *= coefs_[n++];
    if ((!window_periodic_ && n == window_length_) || (window_periodic_ && n == window_length_ - 1)) {
      n = 0;
    }
  }
}

template class Windowing<float>;
template class Windowing<double>;
}  // namespace asr
