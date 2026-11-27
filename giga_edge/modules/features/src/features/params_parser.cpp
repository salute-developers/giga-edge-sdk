#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum.h"
#include "giga_edge/lib/enum/enum_fwd.h"
#include "giga_edge/modules/features/params.h"

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(asr::WindowType, "hamming", "hanning")
GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(asr::PaddingType, "constant", "reflect")
GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(asr::FrequencyScale, "slaney", "htk", "linear", "log10")

namespace giga_edge {

template<>
SpotterFeatureParams ParseConfig(const YAML::Node& config) {
  SpotterFeatureParams result;
  result.sampling_freq = config["sampling_freq"].as<int>();
  result.frame_size_samples = config["frame_size_samples"].as<int>();
  result.frame_stride_samples = config["frame_stride_samples"].as<int>();
  result.num_freq = config["num_freq"].as<int>();
  result.num_mels = config["num_mels"].as<int>();
  result.ref_db = config["ref_db"].as<int>();
  result.min_db = config["min_db"].as<int>();
  result.rolling_norm = config["rolling_norm"].as<bool>();
  return result;
}

template<>
asr::FeatureParams ParseConfig(const YAML::Node& config) {
  std::string prefix = config["asr_window_type"].IsDefined() ? "asr_" : "";

  asr::FeatureParams result;
  result.sampling_freq = config[prefix + "sampling_freq"].as<int64_t>();
  result.frame_size_ms = config[prefix + "frame_size_ms"].as<int64_t>();
  result.frame_stride_ms = config[prefix + "frame_stride_ms"].as<int64_t>();
  result.num_filterbank_chans = config[prefix + "num_filterbank_chans"].as<int64_t>();
  result.low_freq_filterbank = config[prefix + "low_freq_filterbank"].as<int64_t>();
  result.high_freq_filterbank = config[prefix + "high_freq_filterbank"].as<int64_t>();
  std::string window_type = config[prefix + "window_type"].as<std::string>();
  result.window_type = EnumFromString<asr::WindowType>(window_type);
  std::string padding_type = config[prefix + "padding_type"].as<std::string>("constant");
  result.padding_type = EnumFromString<asr::PaddingType>(padding_type);
  result.is_window_periodic = config[prefix + "is_window_periodic"].as<bool>(false);
  result.use_mfsc = config[prefix + "use_mfsc"].as<bool>(true);
  result.center = config[prefix + "center"].as<bool>(false);
  result.preem_coef = config[prefix + "preem_coef"].as<float>();
  result.dither_val = config[prefix + "dither_val"].as<float>();
  result.use_power = config[prefix + "use_power"].as<bool>();
  result.use_energy = config[prefix + "use_energy"].as<bool>();
  result.raw_energy = config[prefix + "raw_energy"].as<bool>();
  result.zero_mean_frame = config[prefix + "zero_mean_frame"].as<bool>();
  result.normalize_gain = config[prefix + "normalize_gain"].as<bool>();
  result.normalize_features_across_axis_0 = config[prefix + "normalize_features_across_axis_0"].as<bool>();
  result.wav_scaling = config[prefix + "wav_scaling"].as<bool>();
  result.nfft = config[prefix + "n_fft"].as<int64_t>(0);
  result.freq_scale = EnumFromString<asr::FrequencyScale>(config[prefix + "mel_scale"].as<std::string>("slaney"));
  result.log_floor = config[prefix + "log_floor"].as<float>(0.0f);
  return result;
}

template<>
FeatureParams ParseConfig(const YAML::Node& config) {
  FeatureParams result;
  std::string features_type = config["features_type"].as<std::string>();
  if (features_type == "spotter") {
    result.params = ParseConfig<SpotterFeatureParams>(config);
  } else if (features_type == "asr") {
    result.params = ParseConfig<asr::FeatureParams>(config);
  } else {
    throw std::runtime_error("Unsupported features type: " + features_type);
  }

  result.InferValues();
  return result;
}

template<>
FeatureIteratorParams ParseConfig(const YAML::Node& config) {
  FeatureIteratorParams result;

  result.num_mels = config["num_mels"].as<int>();
  result.step_size = config["step_size"].as<int>();
  result.context_width = config["context_width"].as<int>();
  return result;
}

template<>
FeatureIteratorWithResetParams ParseConfig(const YAML::Node& config) {
  FeatureIteratorWithResetParams result{ParseConfig<FeatureIteratorParams>(config)};
  result.flush_tail = config["flush_tail"].as<bool>(false);
  result.flush_tail_count = config["flush_tail_count"].as<int>(1);
  if (result.flush_tail_count < 1) {
    throw std::runtime_error("feature_iterator_with_reset: 'flush_tail_count' must be at least 1");
  }
  return result;
}

} // namespace giga_edge