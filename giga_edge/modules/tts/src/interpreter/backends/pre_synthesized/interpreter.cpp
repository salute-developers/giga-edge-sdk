#include "giga_edge/modules/tts/interpreter/backends/pre_synthesized/interpreter.h"

#include "giga_edge/lib/common/str_utils.h"
#include "giga_edge/lib/data_reader/helpers.h"

#include <algorithm>
#include <cctype>

namespace giga_edge {

PreSynthesizedTtsInterpreter::PreSynthesizedTtsInterpreter() = default;

bool PreSynthesizedTtsInterpreter::Initialize(
  const TtsInterpreterParams& params,
  const IDataReaderPtr& data_reader,
  const std::map<std::string, EnvironmentPtr>* environments
) {
  (void)environments;
  params_ = std::any_cast<const PreSynthesizedTtsInterpreterParams&>(params);
  data_reader_ = data_reader;

  if (params_.metadata_path.empty() || params_.audio_files_dir.empty()) {
    throw std::runtime_error("PreSynthesizedTtsIterpreter: metadata_path and audio_files_dir must be provided.");
    return false;
  }

  auto metadata_ptr = data_reader->ReadTransientData(params_.metadata_path);
  auto [metadata_data, metadata_size] = EnsureNotNull(metadata_ptr, params_.metadata_path)->Get();
  std::string metadata_content(metadata_data, metadata_size);

  Json::Value root;
  Json::Reader reader;
  if (!reader.parse(metadata_content, root)) {
    throw std::runtime_error(
      "PreSynthesizedTtsIterpreter: Failed to parse JSON metadata from " + params_.metadata_path
    );
    return false;
  }

  if (!root.isArray()) {
    throw std::runtime_error("PreSynthesizedTtsIterpreter: Root of metadata JSON is not an array.");
    return false;
  }

  for (const auto& item : root) {
    std::string text = item["text"].asString();
    std::string file = item["file"].asString();
    if (!text.empty() && !file.empty()) {
      text_to_filename_[item["voice"].asString()][NormalizeString(text)] = file;
    }
  }

  is_ready_ = true;
  return true;
}

bool PreSynthesizedTtsInterpreter::StartSynthesis(const std::string& text) {
  if (!is_ready_) {
    return false;
  }

  StopSynthesis();
  synthesis_finished_ = false;

  const auto& phrases = text_to_filename_[voice_];
  auto it = phrases.find(NormalizeString(text));
  if (it != phrases.end()) {
    std::string wav_path = params_.audio_files_dir + "/" + it->second;
    auto wav_span = data_reader_->ReadTransientData(wav_path);
    if (wav_span) {
      auto [data, size] = wav_span->Get();

      std::vector<uint8_t> wav_bytes(data, data + size);
      std::vector<float> audio_data = LoadWav(wav_bytes);
      if (!audio_data.empty()) {
        audio_queue_.push(std::move(audio_data));
      }
    } else {
      throw std::runtime_error("Failed to red WAV file: " + wav_path);
    }
  }

  synthesis_finished_ = true;
  return true;
}

std::vector<float> PreSynthesizedTtsInterpreter::GetAudioChunk() {
  std::vector<float> result;
  while (!audio_queue_.empty()) {
    std::vector<float> chunk = std::move(audio_queue_.front());
    audio_queue_.pop();
    result.insert(result.end(), std::make_move_iterator(chunk.begin()), std::make_move_iterator(chunk.end()));
  }
  return result;
}

bool PreSynthesizedTtsInterpreter::IsFinished() const {
  return synthesis_finished_ && audio_queue_.empty();
}

void PreSynthesizedTtsInterpreter::StopSynthesis() {
  ClearQueue();
  synthesis_finished_ = true;
}

bool PreSynthesizedTtsInterpreter::IsReady() const {
  return is_ready_;
}

bool PreSynthesizedTtsInterpreter::SetVoice(const std::string& voice) {
  const bool known = voice.empty() || text_to_filename_.count(voice) > 0;
  voice_ = known ? voice : std::string();
  return known;
}

void PreSynthesizedTtsInterpreter::ClearQueue() {
  std::queue<std::vector<float>> empty;
  std::swap(audio_queue_, empty);
}

} // namespace giga_edge
