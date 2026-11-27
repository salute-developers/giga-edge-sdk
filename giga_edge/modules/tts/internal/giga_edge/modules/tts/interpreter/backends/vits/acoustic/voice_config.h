#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"

#include <json/value.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace giga_edge::vits {

struct FeatureLabel {
  size_t feature = 0;
  int64_t label = 0;
};

// Streaming windows of flow and decoder, in frames.
struct StreamingWindows {
  int64_t num_frames = 0;
  int64_t flow_overlap = 0;
  int64_t decoder_overlap = 0;
};

inline constexpr float kDefaultPauseThreshold = 0.7F;
inline constexpr int64_t kMaxSpeakers = 1 << 16;

// The constants of a voice: model_config.json of the model package as it is.
struct VoiceConfig {
  // The symbols of the acoustic input; the id of a symbol is its index, id 0 is the blank.
  std::u32string alphabet;
  std::vector<std::string> feature_names;
  // Per feature: labels 0..n-1 are values, label n is "no value" (not a letter).
  std::vector<int64_t> embedding_dims;
  std::vector<FeatureLabel> forced_labels;
  std::vector<FeatureLabel> question_labels;
  bool use_question_rules = false;
  // A pause after a word when the pause model gives it a probability above this; 0.7 when the voice does not say (the
  // value of the released voices).
  float pause_threshold = kDefaultPauseThreshold;
  // n_speakers: the rows of the table of speakers of the models, when the package says.
  std::optional<int64_t> speakers;
  bool blank_intersperse = true;
  float length_scale = 1.0F;
  float noise_scale = 0.0F;
  float noise_scale_w = 0.0F;
  float gain_db = 0.0F;
  int64_t sample_rate = 0;
  StreamingWindows streaming;
};

// A file of the data reader as text; throws naming the path when it is missing.
std::string ReadText(const IDataReaderPtr& data_reader, const std::string& path);

// Strict JSON: no duplicate keys, nothing after the value; throws std::runtime_error naming `name`.
Json::Value ParseJson(std::string_view text, const std::string& name);

VoiceConfig ParseVoiceConfig(std::string_view json, const std::string& name);
VoiceConfig ReadVoiceConfig(const IDataReaderPtr& data_reader, const std::string& path);

// The number of input positions for n symbols: with blank_intersperse the symbols stand between blanks.
inline size_t InputLength(size_t symbols, const VoiceConfig& voice) {
  return voice.blank_intersperse ? 2 * symbols + 1 : symbols;
}

// text_ids of the acoustic input; throws std::invalid_argument on a symbol outside the voice alphabet or on more than
// max_symbols symbols, before it holds more of them.
std::vector<int64_t> EncodeText(
  std::string_view marked, const VoiceConfig& voice, size_t max_symbols = std::numeric_limits<size_t>::max()
);

} // namespace giga_edge::vits
