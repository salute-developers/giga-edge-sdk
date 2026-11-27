#pragma once

#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/acoustic_model.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/sentence_splitter.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/stress.h"

#include <cstdint>
#include <map>
#include <string>

namespace giga_edge {

// The keys of tts_interpreter_params: the fields below by their names and the fields of `acoustic` and `sentences`
// next to them (models_path, speed, max_sentence_chars, ...); of `stress`, homograph_strong_confidence and
// homograph_mild_confidence.
struct VitsTtsInterpreterParams {
  static constexpr int kMaxTextChars = 1'000'000;

  vits::AcousticParams acoustic;
  vits::SentenceParams sentences;
  vits::StressParams stress;
  std::string text_path;                 // the language data of the text preparation in the data reader
  std::string voice;                     // the voice of a phrase until SetVoice names another
  std::map<std::string, int64_t> voices; // a voice name -> the speaker of the acoustic models
  int max_text_chars = 100'000;          // a longer text is cut; in [1, kMaxTextChars] code points
  bool verify_dictionaries = false;      // check the checksums of the stress dictionaries at load
};

// Throws on a missing required key and on a value out of its range.
template<>
VitsTtsInterpreterParams ParseConfig(const YAML::Node& config);

} // namespace giga_edge
