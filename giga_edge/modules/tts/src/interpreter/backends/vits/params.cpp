#include "giga_edge/modules/tts/interpreter/backends/vits/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace giga_edge {

namespace {

// The value of a key that is there; a value of another type is an error, not the default.
template<typename T>
void Read(const YAML::Node& config, const char* key, T& value) {
  if (config[key]) {
    value = config[key].as<T>();
  }
}

} // namespace

template<>
VitsTtsInterpreterParams ParseConfig(const YAML::Node& config) {
  static const std::set<std::string> known_keys = {
    "models_path",
    "text_path",
    "voice",
    "voices",
    "onnx_environment",
    "threads",
    "optimization_level",
    "speed",
    "noise_scale",
    "noise_scale_w",
    "gain_db",
    "local_debug_enabled",
    "mmap_models",
    "max_text_chars",
    "verify_dictionaries",
    "max_sentence_chars",
    "sentence_pause_ms",
    "clause_pause_ms",
    "word_pause_ms",
    "homograph_strong_confidence",
    "homograph_mild_confidence"
  };
  for (const auto& entry : config) {
    const auto key = entry.first.as<std::string>();
    if (known_keys.count(key) == 0) {
      throw std::invalid_argument("vits: an unknown key '" + key + "' in the params");
    }
  }
  for (const char* key : {"models_path", "text_path", "voice", "voices"}) {
    if (!config[key]) {
      throw std::runtime_error(std::string("TTS interpreter params for 'vits' require '") + key + "'.");
    }
  }
  VitsTtsInterpreterParams result;
  result.acoustic = ParseConfig<vits::AcousticParams>(config);
  Read(config, "text_path", result.text_path);
  Read(config, "voice", result.voice);
  Read(config, "voices", result.voices);
  Read(config, "max_text_chars", result.max_text_chars);
  Read(config, "verify_dictionaries", result.verify_dictionaries);
  if (result.voices.count(result.voice) == 0) {
    throw std::invalid_argument("vits: no voice '" + result.voice + "' among 'voices'");
  }
  for (const auto& [name, speaker] : result.voices) {
    if (speaker < 0) {
      throw std::invalid_argument("vits: the speaker of voice '" + name + "' is negative");
    }
  }
  if (result.max_text_chars < 1 || result.max_text_chars > VitsTtsInterpreterParams::kMaxTextChars) {
    throw std::invalid_argument("vits: 'max_text_chars' is out of range");
  }
  auto& sentences = result.sentences;
  Read(config, "max_sentence_chars", sentences.max_sentence_chars);
  Read(config, "sentence_pause_ms", sentences.sentence_pause_ms);
  Read(config, "clause_pause_ms", sentences.clause_pause_ms);
  Read(config, "word_pause_ms", sentences.word_pause_ms);
  vits::CheckSentenceParams(sentences);
  auto& stress = result.stress;
  for (const auto& [key, value] :
       {std::pair{"homograph_strong_confidence", &stress.homograph_strong_confidence},
        std::pair{"homograph_mild_confidence", &stress.homograph_mild_confidence}}) {
    Read(config, key, *value);
    if (!(*value >= 0.0 && *value <= 1.0)) {
      throw std::invalid_argument(std::string("vits: '") + key + "' must be in [0, 1]");
    }
  }
  return result;
}

} // namespace giga_edge
