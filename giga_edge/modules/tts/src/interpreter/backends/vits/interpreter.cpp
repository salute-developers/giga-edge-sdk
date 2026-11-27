#include "giga_edge/modules/tts/interpreter/backends/vits/interpreter.h"

#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/voice_config.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/latin.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/morph_dict.h"

#include <algorithm>
#include <any>
#include <chrono>
#include <cmath>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace giga_edge {

namespace {

constexpr int kSilenceChunkMs = 200;
constexpr std::u32string_view kRequiredSymbols = U" .,?!^абвгдеёжзийклмнопрстуфхцчшщъыьэюяАЕЁИОУЫЭЮЯ";

// The longest sentence the acoustic model takes at speed 1 within its budget of frames by positions; a slower
// speech has more frames, and the budget grows as the square of the length.
constexpr int kMaxCharsAtSpeedOne = 400;

// The first `chars` code points of the text; bytes that start no code point count with the one before them, so the
// result is at most 4 * chars bytes of any text.
std::string_view Head(std::string_view text, size_t chars) {
  text = text.substr(0, 4 * chars);
  size_t count = 0;
  for (size_t pos = 0; pos < text.size(); ++pos) {
    const bool starts_code_point = (static_cast<unsigned char>(text[pos]) & 0xC0U) != 0x80U;
    if (starts_code_point && count++ == chars) {
      return text.substr(0, pos);
    }
  }
  return text;
}

} // namespace

bool VitsTtsInterpreter::Initialize(
  const TtsInterpreterParams& params,
  const IDataReaderPtr& data_reader,
  const std::map<std::string, EnvironmentPtr>* environments
) {
  // Not ready until everything is loaded again.
  StopSynthesis();
  acoustic_.reset();
  normalizer_.reset();
  stresser_.reset();
  english_.reset();

  params_ = std::any_cast<const VitsTtsInterpreterParams&>(params);
  // The params may come around ParseConfig: what sets the sentence limit below is checked before it is used; NaN is out
  // of the range too.
  vits::CheckSentenceParams(params_.sentences);
  const auto speed = params_.acoustic.speed;
  if (!(speed >= vits::AcousticParams::kMinSpeed && speed <= vits::AcousticParams::kMaxSpeed)) {
    throw std::invalid_argument("vits: 'speed' must be in [0.25, 4]");
  }
  const auto voice = params_.voices.find(params_.voice);
  if (voice == params_.voices.end()) {
    throw std::invalid_argument("vits: no voice '" + params_.voice + "' among 'voices'");
  }
  voice_speaker_ = voice->second;

  const auto voice_config = vits::ReadVoiceConfig(data_reader, params_.acoustic.models_path + "/voice.json");
  for (const auto& [name, speaker] : params_.voices) {
    if (speaker < 0 || (voice_config.speakers && speaker >= *voice_config.speakers)) {
      throw std::invalid_argument(
        "vits: the speaker of voice '" + name + "' is out of the table of speakers of the voice"
      );
    }
  }
  alphabet_ = voice_config.alphabet;
  for (const char32_t symbol : kRequiredSymbols) {
    if (alphabet_.find(symbol) == std::u32string::npos) {
      throw std::runtime_error("vits: the alphabet of the voice lacks a symbol of Russian text");
    }
  }

  auto stress_params = params_.stress;
  stress_params.environment = params_.acoustic.onnx_environment;
  stress_params.verify_dictionaries = params_.verify_dictionaries;
  stresser_.emplace(stress_params, params_.text_path, data_reader, environments);

  vits::LatinWordReading read_word;
  english_ = vits::EnglishReader::Open(*data_reader, params_.text_path + "/english.dict", params_.verify_dictionaries);
  if (english_) {
    read_word = [english = english_.get()](std::string_view word) {
      auto reading = english->Read(word);
      return reading.empty() ? vits::Transliterate(word) : reading;
    };
  }
  // The words the stress stage knows are the words of the language for the normalizer (a word in capitals is read as a
  // word, not letter by letter); the normalizer asks in lower case, as the stage takes its words.
  auto is_word = [stresser = &*stresser_](std::string_view word) {
    return stresser->KnowsWord(word);
  };
  auto word_info = vits::LoadMorphWordInfo(*data_reader, params_.text_path, params_.verify_dictionaries);
  // A slower speech has more frames: the letters scale by length_scale / speed, blanks and marks by 1 / speed.
  auto sentences = params_.sentences;
  const auto pace = std::min(speed / std::max(voice_config.length_scale, 1.0F), 1.0F);
  sentences.max_sentence_chars = std::clamp(
    static_cast<int>(kMaxCharsAtSpeedOne * std::sqrt(pace)), vits::kMinSentenceChars, sentences.max_sentence_chars
  );
  normalizer_.emplace(
    *data_reader, params_.text_path, sentences, std::move(read_word), std::move(is_word), std::move(word_info)
  );

  acoustic_.emplace(params_.acoustic, data_reader, environments);
  return true;
}

bool VitsTtsInterpreter::StartSynthesis(const std::string& text) {
  if (!IsReady()) {
    return false;
  }
  StopSynthesis();
  speaker_ = voice_speaker_;
  const auto head = Head(text, static_cast<size_t>(params_.max_text_chars));
  if (head.size() < text.size()) {
    std::cerr << "[WARNING] vits: the text is cut to " << params_.max_text_chars << " symbols" << std::endl;
  }
  const auto start = std::chrono::steady_clock::now();
  auto sentences = normalizer_->Normalize(head);
  if (params_.acoustic.local_debug_enabled) {
    const std::chrono::duration<double, std::milli> spent = std::chrono::steady_clock::now() - start;
    std::cerr << "[vits] " << sentences.size() << " sentences, normalized in " << spent.count() << " ms" << std::endl;
  }
  sentences_.assign(std::make_move_iterator(sentences.begin()), std::make_move_iterator(sentences.end()));
  return true;
}

std::vector<float> VitsTtsInterpreter::GetAudioChunk() {
  if (!IsReady()) {
    return {};
  }
  if (silence_samples_ > 0) {
    return Silence();
  }
  if (has_first_chunk_) {
    has_first_chunk_ = false;
    return std::move(first_chunk_);
  }
  if (!acoustic_->Finished()) {
    return acoustic_->NextChunk();
  }
  if (sentences_.empty()) {
    return {};
  }
  const auto sentence = std::move(sentences_.front());
  sentences_.pop_front();
  std::string dropped;
  const auto items = stresser_->Stress(sentence.text);
  if (params_.acoustic.local_debug_enabled) {
    // The text of the text stage: the words with their stresses and 'ё', before the letters of the acoustic model.
    std::string stressed;
    for (const auto& item : items) {
      stressed += item.is_word && !item.stressed.empty() ? item.stressed : item.written;
    }
    std::cerr << "[vits] stressed \"" << stressed << "\"" << std::endl;
  }
  const auto prepared = stresser_->Prepare(items, alphabet_, &dropped);
  if (params_.acoustic.local_debug_enabled && !dropped.empty()) {
    std::cerr << "[vits] dropped '" << dropped << "' of '" << sentence.text << "'" << std::endl;
  }
  acoustic_->Start(prepared, speaker_);
  if (acoustic_->Finished()) {
    return {};
  }
  // The first sound of the sentence comes before the pause is given: a sentence that fails gets no pause before it.
  auto chunk = acoustic_->NextChunk();
  const auto rate = static_cast<int64_t>(acoustic_->SampleRate());
  silence_samples_ = static_cast<size_t>(pause_ms_ * rate / 1000);
  pause_ms_ = sentence.pause_after_ms;
  if (silence_samples_ == 0) {
    return chunk;
  }
  first_chunk_ = std::move(chunk);
  has_first_chunk_ = true;
  return Silence();
}

std::vector<float> VitsTtsInterpreter::Silence() {
  const auto chunk = static_cast<size_t>(kSilenceChunkMs * static_cast<int64_t>(acoustic_->SampleRate()) / 1000);
  const auto samples = std::min(silence_samples_, chunk);
  silence_samples_ -= samples;
  return std::vector<float>(samples, 0.0F);
}

bool VitsTtsInterpreter::IsFinished() const {
  // The silence is given while the first sound of a sentence waits, so it needs no check of its own.
  return !IsReady() || (!has_first_chunk_ && acoustic_->Finished() && sentences_.empty());
}

void VitsTtsInterpreter::StopSynthesis() {
  sentences_.clear();
  silence_samples_ = 0;
  pause_ms_ = 0;
  has_first_chunk_ = false;
  first_chunk_.clear();
  if (acoustic_) {
    acoustic_->Reset();
  }
}

bool VitsTtsInterpreter::IsReady() const {
  return acoustic_.has_value();
}

bool VitsTtsInterpreter::SetVoice(const std::string& voice) {
  if (!IsReady()) {
    return false;
  }
  const auto found = params_.voices.find(voice.empty() ? params_.voice : voice);
  const bool known = found != params_.voices.end();
  voice_speaker_ = known ? found->second : params_.voices.at(params_.voice);
  return known;
}

} // namespace giga_edge
