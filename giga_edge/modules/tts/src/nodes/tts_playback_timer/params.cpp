#include "giga_edge/modules/tts/nodes/tts_playback_timer/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

#include <stdexcept>

namespace giga_edge {

template<>
TtsPlaybackTimerParams ParseConfig(const YAML::Node& config) {
  TtsPlaybackTimerParams result;
  result.tick_ms = config["tick_ms"].as<int>(result.tick_ms);
  result.tail_ms = config["tail_ms"].as<int>(result.tail_ms);
  result.playing_tail_ms = config["playing_tail_ms"].as<int>(result.playing_tail_ms);
  if (result.tick_ms <= 0 || result.tail_ms < 0 || result.playing_tail_ms < 0) {
    throw std::runtime_error(
      "tts_playback_timer: tick_ms must be positive, tail_ms and playing_tail_ms must not be negative"
    );
  }
  return result;
}

} // namespace giga_edge
