// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#include "speech_utils.h"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <cassert>
#include <algorithm>

#include <Eigen/Core>
namespace asr {

template<typename T>
void Matmul(const T *mat_a, const T *mat_b, T *mat_c, int m, int k, int n) {
  // Tell Eigen to use one thread (i.e. CPU core) otherwise it occupies all cpu cores
  [[maybe_unused]] static bool set_threads = [](){ Eigen::setNbThreads(1); return true; }();
  using CMatrixXTRowMajor = const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
  using MatrixXTRowMajor = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
  const Eigen::Map<CMatrixXTRowMajor> mat_a_eigen(mat_a, m, k);
  const Eigen::Map<CMatrixXTRowMajor> mat_b_eigen(mat_b, k, n);
  Eigen::Map<MatrixXTRowMajor> mat_c_eigen(mat_c, m, n);
  mat_c_eigen = mat_a_eigen * mat_b_eigen;
}
}  // namespace asr

namespace asr {

template <typename T>
std::vector<T> FrameSignal(
    const std::vector<T>& input,
    const FeatureParams& params) {
  size_t frame_size = params.NumFrameSizeSamples();
  size_t frame_stride = params.NumFrameStrideSamples();
  std::vector<T> padded;
  if (params.center) {
    padded = Padding(input, params.Nfft(), params.padding_type);
  }
  const auto& input_pad = params.center ? padded : input;
  size_t numframes = params.NumFrames(input_pad.size());
  size_t shift = params.Shift();

  std::vector<T> frames(numframes * frame_size);
  for (size_t f = 0; f < numframes; ++f) {
    for (size_t i = 0; i < frame_size; ++i) {
      frames[f * frame_size + i] = input_pad[f * frame_stride + i + shift];
    }
  }
  return frames;
}

template <typename T>
std::vector<T> Padding(const std::vector<T>& input,
                       const int pad_size,
                       const PaddingType& pad_type) {
  auto input_size = static_cast<int>(input.size());
  if ((input_size == 0) || (pad_size <= 0)) {
    return input;
  }
  std::vector<T> padded(input_size + pad_size, static_cast<T>(0));
  int one_side_pad = pad_size >> 1;
  std::copy(input.begin(), input.end(), padded.data() + one_side_pad);
  if (pad_type == PaddingType::CONSTANT) {
    return padded;
  }
  if (pad_type != PaddingType::REFLECT) {
    throw std::invalid_argument("pad_type: invalid argument");
  }
  int input_it = 1;
  int pad_it = one_side_pad - 1;
  while (pad_it >= 0) {
    padded[pad_it--] = input[input_it++];
    input_it %= input_size;
  }
  input_it = std::max(input_size - 2, 0);
  pad_it = input_size + one_side_pad;
  while (pad_it < static_cast<int>(padded.size())) {
    padded[pad_it++] = input[input_it--];
    if (input_it < 0) {
      input_it = input_size - 1;
    }
  }
  return padded;
}

template <>
std::vector<float> CblasGemm(
    const std::vector<float>& mat_a,
    const std::vector<float>& mat_b,
    int n,
    int k) {
  if (n <= 0 || k <= 0 || mat_a.empty() || (mat_a.size() % k != 0) ||
      (mat_b.size() != (static_cast<size_t>(n) * static_cast<size_t>(k)))) {
    throw std::invalid_argument("cblasGemm: invalid arguments");
  }

  int m = static_cast<int>(mat_a.size()) / k;

  std::vector<float> mat_c(m * n);

  Matmul<float>(mat_a.data(), mat_b.data(), mat_c.data(), m, k, n);
  return mat_c;
}

template <>
std::vector<double> CblasGemm(
    const std::vector<double>& mat_a,
    const std::vector<double>& mat_b,
    int n,
    int k) {
  if (n <= 0 || k <= 0 || mat_a.empty() || (mat_a.size() % k != 0) ||
      (mat_b.size() != static_cast<size_t>(n) * static_cast<size_t>(k))) {
    throw std::invalid_argument("cblasGemm: invalid arguments");
  }

  int m = static_cast<int>(mat_a.size()) / k;

  std::vector<double> mat_c(m * n);
  Matmul<double>(mat_a.data(), mat_b.data(), mat_c.data(), m, k, n);
  return mat_c;
}

template <typename T>
void NormalizeAcrossAxis0Inplace(std::vector<T>* vec,
                                 const std::vector<int>& shape) {
  std::vector<T>& out = *vec;
  if (vec->empty()) {
    return;
  }
  assert(shape.size() == 2);

  size_t time = shape[0];
  size_t hidden = shape[1];

  std::vector<T> means(hidden);
  std::vector<T> stddevs(hidden);

  for (size_t i = 0; i < time; ++i) {
    for (size_t j = 0; j < hidden; ++j) {
      means[j] += out[i * hidden + j];
    }
  }
  for (size_t i = 0; i < time; ++i) {
    for (size_t j = 0; j < hidden; ++j) {
      auto val = std::abs(out[i * hidden + j] - means[j] / time);
      stddevs[j] += val * val;
    }
  }

  for (size_t j = 0; j < hidden; ++j) {
    means[j] /= time;
    stddevs[j] = std::sqrt(stddevs[j] / time);
  }
  for (size_t i = 0; i < time; ++i) {
    for (size_t j = 0; j < hidden; ++j) {
      out[i * hidden + j] = (out[i * hidden + j] - means[j]) / stddevs[j];
    }
  }
}

template<typename T>
static bool AbsCompare(T a, T b) {
  return (std::abs(a) < std::abs(b));
}

template <typename T>
void NormalizeGainInplace(std::vector<T>* source) {
  if (source->empty()) {
    return;
  }
  auto& source_ref = *source;
  float gain = std::abs(*std::max_element(source_ref.begin(), source_ref.end(), AbsCompare<T>));
  if (gain == 0) {
    return;
  }
  for (size_t i = 0; i < source_ref.size(); ++i) {
    source_ref[i] /= gain;
  }
}

template <typename T>
bool IsConstantWav(const std::vector<T>& source) {
  if (source.empty()) {
    return true;
  }
  for (size_t i = 1; i < source.size(); ++i) {
    if (source[i] != source.front()) {
      return false;
    }
  }
  return true;
}

template void NormalizeGainInplace(std::vector<float>* source);
template void NormalizeGainInplace(std::vector<double>* source);
template void NormalizeAcrossAxis0Inplace(std::vector<float>* in, const std::vector<int>& shape);
template void NormalizeAcrossAxis0Inplace(std::vector<double>* in, const std::vector<int>& shape);
template std::vector<float> FrameSignal(const std::vector<float>&, const FeatureParams&);
template std::vector<double> FrameSignal(const std::vector<double>&, const FeatureParams&);
template bool IsConstantWav(const std::vector<float>&);
template bool IsConstantWav(const std::vector<double>&);
}  // namespace asr
