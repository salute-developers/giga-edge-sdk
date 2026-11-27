#include "giga_edge/modules/tts/interpreter/backends/pre_synthesized/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

#include <stdexcept>

namespace giga_edge {

template<>
PreSynthesizedTtsInterpreterParams ParseConfig(const YAML::Node& config) {
  PreSynthesizedTtsInterpreterParams result;
  if (!config["metadata_path"] || !config["audio_files_dir"]) {
    throw std::runtime_error(
      "TTS interpreter params for 'pre_synthesized' requires 'metadata_path' and 'audio_files_dir'."
    );
  }
  result.metadata_path = config["metadata_path"].as<std::string>();
  result.audio_files_dir = config["audio_files_dir"].as<std::string>();
  return result;
}

} // namespace giga_edge
