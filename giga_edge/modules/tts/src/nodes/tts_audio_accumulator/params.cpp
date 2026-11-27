#include "giga_edge/modules/tts/nodes/tts_audio_accumulator/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
TtsAudioAccumulatorParams ParseConfig(const YAML::Node& config) {
  TtsAudioAccumulatorParams result;
  if (!config) {
    return result;
  }
  result.stream = config["stream"].as<bool>(result.stream);
  result.min_lead_ms = config["min_lead_ms"].as<int>(result.min_lead_ms);
  result.sample_rate = config["sample_rate"].as<int>(result.sample_rate);
  return result;
}

} // namespace giga_edge
