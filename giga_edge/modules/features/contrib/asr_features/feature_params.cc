// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#include "feature_params.h"

namespace asr {

int64_t asr::FeatureParams::NumFrameSizeSamples() const {
  return static_cast<int>(round(1e-3 * frame_size_ms * sampling_freq));
}

int64_t asr::FeatureParams::NumFrameStrideSamples() const {
  return static_cast<int>(round(1e-3 * frame_stride_ms * sampling_freq));
}

int64_t asr::FeatureParams::Nfft() const {
  if (nfft > 0) {
    return nfft;
  }
  int64_t nsamples = NumFrameSizeSamples();
  return (nsamples > 0) ? (int64_t)1 << static_cast<std::size_t>(ceil(log2(nsamples))) : (int64_t)0;
}

int64_t asr::FeatureParams::FilterFreqResponseLen() const {
  return (Nfft() >> 1) + 1;
}

int64_t asr::FeatureParams::PowSpecFeatSz() const {
  return FilterFreqResponseLen();
}

int64_t asr::FeatureParams::MfscFeatSz() const {
  return (num_filterbank_chans + (use_energy ? 1 : 0));
}

int64_t asr::FeatureParams::NumFrames(int64_t in_size) const {
  auto frame_size = NumFrameSizeSamples();
  auto frame_stride = NumFrameStrideSamples();
  if (frame_stride <= 0 || in_size < frame_size) {
    return 0;
  }
  return 1 + floor(static_cast<float>(in_size - frame_size) / static_cast<float>(frame_stride));
}

int64_t asr::FeatureParams::Shift() const {
  return center ? (Nfft() - NumFrameSizeSamples()) >> 1 : 0;
}

} // namespace asr