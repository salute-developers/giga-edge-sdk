// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#include "mfsc.h"
#include "speech_utils.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <numeric>
#include <limits>
#include <utility>
#include <stdexcept>


namespace asr {

template <typename T>
Mfsc<T>::Mfsc(const FeatureParams& params)
    : PowerSpectrum<T>(params),
      tri_flt_bank_(
          params.num_filterbank_chans,
          params.FilterFreqResponseLen(),
          params.sampling_freq,
          params.low_freq_filterbank,
          params.high_freq_filterbank,
          params.freq_scale) {
  ValidateMfscParams();
}

template <typename T>
std::vector<T> Mfsc<T>::Apply(const std::vector<T>& input, int feat_start) {
  auto frames = input;
  if (this->feat_params_.dither_val > 0.0) {
    frames = this->dither_.Apply(frames, feat_start);
  }
  frames = FrameSignal(frames, this->feat_params_);
  if (frames.empty()) {
    return {};
  }

  int64_t n_samples = this->feat_params_.NumFrameSizeSamples();
  size_t n_frames = frames.size() / n_samples;

  std::vector<T> energy(n_frames);
  if (this->feat_params_.use_energy && this->feat_params_.raw_energy) {
    for (size_t f = 0; f < n_frames; ++f) {
      auto begin = frames.data() + f * n_samples;
      energy[f] = std::log(std::max(
          std::inner_product(
              begin, begin + n_samples, begin, static_cast<T>(0.0)),
          std::numeric_limits<T>::min()));
    }
  }
  auto mfsc_feat = MfscImpl(frames);
  auto num_feat = this->feat_params_.num_filterbank_chans;
  if (this->feat_params_.use_energy) {
    if (!this->feat_params_.raw_energy) {
      for (size_t f = 0; f < n_frames; ++f) {
        auto begin = frames.data() + f * n_samples;
        energy[f] = std::log(std::max(
            std::inner_product(
                begin, begin + n_samples, begin, static_cast<T>(0.0)),
            std::numeric_limits<T>::min()));
      }
    }
    std::vector<T> new_mfsc_feat(mfsc_feat.size() + n_frames);
    for (size_t f = 0; f < n_frames; ++f) {
      size_t start = f * num_feat;
      new_mfsc_feat[start + f] = energy[f];
      std::copy(
          mfsc_feat.data() + start,
          mfsc_feat.data() + start + num_feat,
          new_mfsc_feat.data() + start + f + 1);
    }
    std::swap(mfsc_feat, new_mfsc_feat);
    ++num_feat;
  }
  return mfsc_feat;
}

template <typename T>
std::vector<T> Mfsc<T>::MfscImpl(std::vector<T>& frames) {
  auto powspectrum = this->PowSpectrumImpl(frames);
  if (this->feat_params_.use_power) {
    std::transform(
        powspectrum.begin(), powspectrum.end(), powspectrum.begin(), [](T x) {
          return x * x;
        });
  }
  auto triflt = tri_flt_bank_.Apply(powspectrum);
  const T log_floor = this->feat_params_.log_floor;
  std::transform(triflt.begin(), triflt.end(), triflt.begin(), [log_floor](T x) {
    return std::log(log_floor > 0 ? std::max(x, log_floor) : x);
  });
  return triflt;
}

template <typename T>
int64_t Mfsc<T>::OutputSize(int64_t input_size) {
  return this->feat_params_.MfscFeatSz() * this->feat_params_.NumFrames(input_size);
}

template <typename T>
void Mfsc<T>::ValidateMfscParams() const {
  this->ValidatePowSpecParams();
  if (this->feat_params_.num_filterbank_chans <= 0) {
    throw std::invalid_argument("Mfsc: num_filterbank_chans must be positive");
  }
  if (!std::isfinite(this->feat_params_.log_floor) || this->feat_params_.log_floor < 0) {
    throw std::invalid_argument("Mfsc: log_floor must be finite and non-negative");
  }
}

template class Mfsc<float>;
template class Mfsc<double>;
}  // namespace asr
