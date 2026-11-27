// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#pragma once

#include "feature_params.h"

#include <vector>

namespace asr {

// Convert the speech signal into frames
template <typename T>
std::vector<T> FrameSignal(const std::vector<T>& input, const FeatureParams& params);

// row major;  matA - m x k , matB - k x n
template <typename T>
std::vector<T>
CblasGemm(const std::vector<T>& mat_a, const std::vector<T>& mat_b, int n, int k);

template <typename T>
void NormalizeAcrossAxis0Inplace(std::vector<T>* in, const std::vector<int>& shape);

template <typename T>
void NormalizeGainInplace(std::vector<T>* source);

template <typename T>
bool IsConstantWav(const std::vector<T>& source);

template <typename T>
std::vector<T> Padding(const std::vector<T>& input, const int pad_size, const PaddingType& pad_type);
}  // namespace asr
