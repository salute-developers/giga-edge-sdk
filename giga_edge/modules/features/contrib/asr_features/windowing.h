// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#pragma once

#include "feature_params.h"

#include <cstdint>
#include <vector>

namespace asr {

// Applies a given window on input
//    s'(n) = w(n) * s(n) where w(n) are the window coefficients
template <typename T>
class Windowing {
 public:
  Windowing(int64_t n, WindowType window, bool is_periodic = false);

  std::vector<T> Apply(const std::vector<T>& input) const;

  void ApplyInPlace(std::vector<T>& input) const;

 private:
  int64_t window_length_;
  WindowType window_type_;
  std::vector<T> coefs_;
  bool window_periodic_;
};
}  // namespace asr
