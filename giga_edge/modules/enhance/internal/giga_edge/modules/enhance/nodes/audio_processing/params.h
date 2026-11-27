#pragma once
#include <stdint.h>

namespace giga_edge {
struct AudioProcessingParams {
  // frequency (Hz) of speech signal recording
  uint32_t sampling_freq = 16000;

  // Loudness Control
  bool lc_enabled = false;
  float lc_target_level_db = -23;
  float lc_max_gain_db = 40;
  float lc_bypass_time_sec = 3;
  bool lc_use_vad = false;         // rnnoise VAD: more accurate speech detection, higher CPU
  bool lc_post_processing = false; // compressor/limiter chain after loudness normalization

  // Range Control
  bool rc_enabled = false;
  float rc_target_level_db = -3;
  float rc_max_gain_db = 10;
  float rc_attack_time = 1e-5;
  float rc_release_time = 300;
  float rc_hold_time = 5;

  // Noise Gate
  bool ng_enabled = false;
  float ng_threshold_db = -15.0;
  float ng_attack_time = 5e-2;
  float ng_release_time = 2e-2;
  float ng_hold_time = 5e-2;

  // Limiter
  bool lim_enabled = false;
  float lim_threshold_db = -3.0;
  float lim_knee_width = 0.0;
  float lim_attack_time = 0.0;
  float lim_release_time = 0.2;

  // Dynamic Range Compressor
  bool drc_enabled = false;
  float drc_threshold_db = -15.0;
  uint32_t drc_ratio = 5;
  float drc_knee_width = 0.0;
  float drc_attack_time = 1e-2;
  float drc_release_time = 2e-1;

  // Automatic Gain Control
  bool agc_enabled = false;
  float agc_target_power_level = 1.0;
  float agc_max_power_gain = 20.0;
  float agc_time_rise = 1e-2;
  float agc_time_fall = 1e-2;
};
} // namespace giga_edge
