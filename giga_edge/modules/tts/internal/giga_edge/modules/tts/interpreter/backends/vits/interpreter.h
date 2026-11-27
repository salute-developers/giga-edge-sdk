#pragma once

#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/acoustic_model.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/params.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/english.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/normalized_sentence.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/normalizer.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/stress.h"
#include "giga_edge/modules/tts/interpreter/interpreter.h"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace giga_edge {

// Speech on the models of a voice. StartSynthesis normalizes the text and cuts it into sentences; a GetAudioChunk
// then does one unit of work: a step of the sentence in synthesis, a piece of the silence before the next sentence,
// or the start of that sentence (stress, BERT, durations) with its first step. So a cancel waits for one unit, and
// the memory does not grow with the text. A sentence with nothing to say gives no sound and no silence.
class VitsTtsInterpreter : public ITtsInterpreter {
public:
  bool Initialize(
    const TtsInterpreterParams& params,
    const IDataReaderPtr& data_reader,
    const std::map<std::string, EnvironmentPtr>* environments = nullptr
  ) override;
  bool StartSynthesis(const std::string& text) override;
  std::vector<float> GetAudioChunk() override;
  bool IsFinished() const override;
  void StopSynthesis() override;
  bool IsReady() const override;
  // The voice of the phrases started after the call.
  bool SetVoice(const std::string& voice) override;

private:
  std::vector<float> Silence();

  VitsTtsInterpreterParams params_;
  std::unique_ptr<vits::EnglishReader> english_;
  std::optional<vits::Stresser> stresser_; // before the normalizer: its words are the words of the normalizer
  std::optional<vits::TextNormalizer> normalizer_;
  std::optional<vits::AcousticModel> acoustic_;
  std::u32string alphabet_;
  int64_t voice_speaker_ = 0; // of the voice set last
  // The phrase in synthesis.
  int64_t speaker_ = 0;
  std::deque<vits::NormalizedSentence> sentences_;
  size_t silence_samples_ = 0;     // left to give before first_chunk_
  std::vector<float> first_chunk_; // the first sound of the sentence started in the acoustic model
  bool has_first_chunk_ = false;
  int pause_ms_ = 0; // after the sentence spoken last; 0 before the first one
};

} // namespace giga_edge

REGISTER_TTS_BACKEND(vits, VitsTtsInterpreterParams, VitsTtsInterpreter)
