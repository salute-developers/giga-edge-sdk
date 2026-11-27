// Copyright (c) Facebook, Inc. and its affiliates. All rights reserved.
// Licensed under the BSD license found in the LICENSE file in this directory.
// Derived from wav2letter++ (https://github.com/flashlight/wav2letter, src/libraries/feature), modified.

#pragma once

#include <cmath>
#include <cstdint>

namespace asr {

enum class WindowType {
  HAMMING = 0,
  HANNING = 1,
};
enum class FrequencyScale {
  MEL = 0,
  MELHTK = 1,
  LINEAR = 2,
  LOG10 = 3,
};

enum class PaddingType {
  CONSTANT = 0,
  REFLECT = 1,
};

struct FeatureParams {
  // frequency (Hz) of speech signal recording
  int64_t sampling_freq = 16000;

  // frame size in milliseconds
  int64_t frame_size_ms = 25;

  // frame step size in milliseconds
  int64_t frame_stride_ms = 10;

  // number of filterbank channels
  // Kaldi recommends using 23 for 16KHz and 15 for 8KHz sampled data
  int64_t num_filterbank_chans = 23;

  // lower cutoff frequency (HZ) for the filterbank
  int64_t low_freq_filterbank = 0.0;

  // upper cutoff frequency (HZ) for the filterbank
  int64_t high_freq_filterbank = -1.0;

  // analysis window function handle for framing (hamming by default)
  WindowType window_type = WindowType::HAMMING;

  // padding type: constant or reflect
  PaddingType padding_type = PaddingType::CONSTANT;

  // flag that indicates whether this is a periodic window
  bool is_window_periodic = false;

  // flag that indicates whether use mfsc or not - default true
  bool use_mfsc = true;

  // flag that indicates whether window should be in center or nor
  bool center = false;

  // preemphasis filtering coefficient (0.7 default)
  float preem_coef = 0.97;

  // dithering constant (0.0 default ==> no dithering)
  float dither_val = 0.0;

  // use power instead of magnitude for filterbank energies
  bool use_power = true;

  // If true, append log energy term as a feature to MFSC
  // For MFCC, C0 is replaced with energy term
  bool use_energy = true;

  // If true, use energy before PreEmphasis and Windowing
  bool raw_energy = true;

  // If true, remove DC offset from the signal frames
  bool zero_mean_frame = true;

  // Whether or not to normalize gain of the input sound;
  bool normalize_gain = true;

  // If true normalize features across axis0
  bool normalize_features_across_axis_0 = false;

  // Wave scale constant (32767 - default)
  bool wav_scaling = false;

  int64_t nfft = 0;

  FrequencyScale freq_scale = FrequencyScale::MEL;

  float log_floor = 0.0;

  // frame size (no of samples)
  // the last frame is discarded, if less than the frame size
  int64_t NumFrameSizeSamples() const;

  int64_t NumFrameStrideSamples() const;

  int64_t Nfft() const;

  int64_t FilterFreqResponseLen() const;

  int64_t PowSpecFeatSz() const;

  int64_t MfscFeatSz() const;

  int64_t NumFrames(int64_t in_size) const;

  int64_t Shift() const;
};

}  // namespace asr
