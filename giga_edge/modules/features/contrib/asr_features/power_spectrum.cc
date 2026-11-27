// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#include "power_spectrum.h"

#include "speech_utils.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <unordered_map>
#include <stdexcept>


namespace asr {

template <typename T>
PowerSpectrum<T>::PowerSpectrum(const FeatureParams& params)
    : feat_params_(params)
    , dither_(params.dither_val)
    , pre_emphasis_(params.preem_coef, params.NumFrameSizeSamples())
    , windowing_(params.NumFrameSizeSamples(), params.window_type, params.is_window_periodic)
    , fft_calculator_(params.Nfft()) {
  ValidatePowSpecParams();
}

template <typename T>
std::vector<T> PowerSpectrum<T>::Apply(const std::vector<T>& input, int /*feat_start*/) {
  auto frames = FrameSignal(input, feat_params_);
  if (frames.empty()) {
    return {};
  }
  return PowSpectrumImpl(frames);
}

template <typename T>
std::vector<T> PowerSpectrum<T>::PowSpectrumImpl(std::vector<T>& frames) {
  size_t n_samples = static_cast<size_t>(feat_params_.NumFrameSizeSamples());
  size_t n_frames = frames.size() / n_samples;
  size_t k = feat_params_.FilterFreqResponseLen();

  if (feat_params_.zero_mean_frame) {
    for (size_t f = 0; f < n_frames; ++f) {
      auto begin = frames.data() + f * n_samples;
      T mean = std::accumulate(begin, begin + n_samples, 0.0);
      mean /= n_samples;
      std::transform(
          begin, begin + n_samples, begin, [mean](T x) { return x - mean; });
    }
  }
  if (feat_params_.preem_coef > 0) {
    pre_emphasis_.ApplyInPlace(frames);
  }
  windowing_.ApplyInPlace(frames);
  std::vector<T> dft(k * n_frames);
  for (size_t f = 0; f < n_frames; ++f) {
    auto begin = frames.data() + f * n_samples;
    {
      std::lock_guard<std::mutex> lock(fft_mutex_);
      fft_calculator_.Calculate(n_samples, k, f, begin, dft);
    }
  }
  return dft;
}

template <typename T>
FeatureParams PowerSpectrum<T>::GetFeatureParams() const {
  return feat_params_;
}

template <typename T>
int64_t PowerSpectrum<T>::OutputSize(int64_t input_size) {
  return feat_params_.PowSpecFeatSz() * feat_params_.NumFrames(input_size);
}

template <typename T>
void PowerSpectrum<T>::ValidatePowSpecParams() const {
  if (feat_params_.sampling_freq <= 0) {
    throw std::invalid_argument("PowerSpectrumBase: sampling_freq is negative");
  } else if (feat_params_.frame_size_ms <= 0) {
    throw std::invalid_argument("PowerSpectrumBase: frame_size_ms is negative");
  } else if (feat_params_.frame_stride_ms <= 0) {
    throw std::invalid_argument("PowerSpectrumBase: frame_stride_ms is negative");
  } else if (feat_params_.NumFrameSizeSamples() <= 0) {
    throw std::invalid_argument("PowerSpectrumBase: frame_size_ms is too low");
  } else if (feat_params_.NumFrameStrideSamples() <= 0) {
    throw std::invalid_argument("PowerSpectrumBase: frame_stride_ms is too low");
  } else if (feat_params_.nfft < 0 || (feat_params_.nfft > 0 && feat_params_.nfft < feat_params_.NumFrameSizeSamples())) {
    throw std::invalid_argument("PowerSpectrumBase: nfft must be 0 or not less than the frame size");
  }
}

template class PowerSpectrum<float>;
template class PowerSpectrum<double>;
}  // namespace asr
