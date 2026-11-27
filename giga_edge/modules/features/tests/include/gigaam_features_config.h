#pragma once

inline constexpr auto kGigaamFeaturesConfig = R"(
  features_type: asr
  sampling_freq: 16000
  frame_size_ms: 20
  frame_stride_ms: 10
  n_fft: 320
  num_filterbank_chans: 64
  low_freq_filterbank: 0
  high_freq_filterbank: 8000
  mel_scale: htk
  window_type: hanning
  is_window_periodic: true
  center: false
  use_mfsc: true
  use_power: true
  log_floor: 1.0e-9
  preem_coef: 0.0
  dither_val: 0.0
  use_energy: false
  raw_energy: false
  zero_mean_frame: false
  normalize_gain: false
  normalize_features_across_axis_0: false
  wav_scaling: false
)";
