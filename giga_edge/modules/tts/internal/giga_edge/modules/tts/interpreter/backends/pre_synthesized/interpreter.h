#pragma once

#include "giga_edge/lib/audio/audio.h"
#include "giga_edge/modules/tts/interpreter/backends/pre_synthesized/params.h"
#include "giga_edge/modules/tts/interpreter/interpreter.h"

#include <json/reader.h>
#include <json/value.h>

#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace giga_edge {

class PreSynthesizedTtsInterpreter : public ITtsInterpreter {
public:
  PreSynthesizedTtsInterpreter();

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
  bool SetVoice(const std::string& voice) override;

private:
  void ClearQueue();

  PreSynthesizedTtsInterpreterParams params_;
  IDataReaderPtr data_reader_;
  // Voice (empty: the configured one) -> text -> file.
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> text_to_filename_;
  std::string voice_;
  std::queue<std::vector<float>> audio_queue_;
  bool is_ready_ = false;
  bool synthesis_finished_ = true;
};

} // namespace giga_edge

REGISTER_TTS_BACKEND(pre_synthesized, PreSynthesizedTtsInterpreterParams, PreSynthesizedTtsInterpreter)
