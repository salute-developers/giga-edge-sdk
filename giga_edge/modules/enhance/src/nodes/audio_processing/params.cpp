#include "giga_edge/modules/enhance/nodes/audio_processing/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum.h"

namespace giga_edge {

template<>
AudioProcessingParams ParseConfig(const YAML::Node& config) {
  AudioProcessingParams p;

  p.sampling_freq = config["sampling_freq"].as<uint32_t>(p.sampling_freq);

  // Loudness Control
  p.lc_enabled = config["lc_enabled"].as<bool>(p.lc_enabled);
  p.lc_target_level_db = config["lc_target_level_db"].as<float>(p.lc_target_level_db);
  p.lc_max_gain_db = config["lc_max_gain_db"].as<float>(p.lc_max_gain_db);
  p.lc_bypass_time_sec = config["lc_bypass_time_sec"].as<float>(p.lc_bypass_time_sec);
  p.lc_use_vad = config["lc_use_vad"].as<bool>(p.lc_use_vad);
  p.lc_post_processing = config["lc_post_processing"].as<bool>(p.lc_post_processing);

  // Range Control
  p.rc_enabled = config["rc_enabled"].as<bool>(p.rc_enabled);
  p.rc_target_level_db = config["rc_target_level_db"].as<float>(p.rc_target_level_db);
  p.rc_max_gain_db = config["rc_max_gain_db"].as<float>(p.rc_max_gain_db);
  p.rc_attack_time = config["rc_attack_time"].as<float>(p.rc_attack_time);
  p.rc_release_time = config["rc_release_time"].as<float>(p.rc_release_time);
  p.rc_hold_time = config["rc_hold_time"].as<float>(p.rc_hold_time);

  // Noise Gate
  p.ng_enabled = config["ng_enabled"].as<bool>(p.ng_enabled);
  p.ng_threshold_db = config["ng_threshold_db"].as<float>(p.ng_threshold_db);
  p.ng_attack_time = config["ng_attack_time"].as<float>(p.ng_attack_time);
  p.ng_release_time = config["ng_release_time"].as<float>(p.ng_release_time);
  p.ng_hold_time = config["ng_hold_time"].as<float>(p.ng_hold_time);

  // Limiter
  p.lim_enabled = config["lim_enabled"].as<bool>(p.lim_enabled);
  p.lim_threshold_db = config["lim_threshold_db"].as<float>(p.lim_threshold_db);
  p.lim_knee_width = config["lim_knee_width"].as<float>(p.lim_knee_width);
  p.lim_attack_time = config["lim_attack_time"].as<float>(p.lim_attack_time);
  p.lim_release_time = config["lim_release_time"].as<float>(p.lim_release_time);

  // Dynamic Range Compressor
  p.drc_enabled = config["drc_enabled"].as<bool>(p.drc_enabled);
  p.drc_threshold_db = config["drc_threshold_db"].as<float>(p.drc_threshold_db);
  p.drc_ratio = config["drc_ratio"].as<uint32_t>(p.drc_ratio);
  p.drc_knee_width = config["drc_knee_width"].as<float>(p.drc_knee_width);
  p.drc_attack_time = config["drc_attack_time"].as<float>(p.drc_attack_time);
  p.drc_release_time = config["drc_release_time"].as<float>(p.drc_release_time);

  // Automatic Gain Control
  p.agc_enabled = config["agc_enabled"].as<bool>(p.agc_enabled);
  p.agc_target_power_level = config["agc_target_power_level"].as<float>(p.agc_target_power_level);
  p.agc_max_power_gain = config["agc_max_power_gain"].as<float>(p.agc_max_power_gain);
  p.agc_time_rise = config["agc_time_rise"].as<float>(p.agc_time_rise);
  p.agc_time_fall = config["agc_time_fall"].as<float>(p.agc_time_fall);

  return p;
}
} // namespace giga_edge