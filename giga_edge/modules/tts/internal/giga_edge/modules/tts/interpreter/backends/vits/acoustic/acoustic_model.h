#pragma once

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/modules/model/interpreter/interpreter.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/bert.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/question_rules.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/voice_config.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/prepared_sentence.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace giga_edge::vits {

// models_path in the data reader holds voice.json (model_config.json of the voice), question_rules.json (when the voice
// uses them), bert.onnx, bert_vocab.txt, text_qf0.onnx, full_text.onnx, length_regulator.onnx, flow.onnx and
// decoder.onnx, and may hold pause_predictor.onnx (the pause model: without it the pauses are those of the text). The
// model files are ONNX or ORT format under these names: ONNX Runtime tells them by their bytes.
struct AcousticParams {
  static constexpr float kMinSpeed = 0.25F;
  static constexpr float kMaxSpeed = 4.0F;
  static constexpr float kMinGainDb = -60.0F;
  static constexpr float kMaxGainDb = 40.0F;

  std::string models_path;
  std::string onnx_environment; // a shared ONNX environment by name; empty: threads of each model
  int threads = 1;
  // The highest level all the models of the voice load at: the decoder fails to load at extended and all.
  OnnxOptimizationLevel optimization_level = OnnxOptimizationLevel::BASIC;
  float speed = 1.0F; // in [kMinSpeed, kMaxSpeed]; 2 speaks twice as fast: length_scale of every position is divided
  std::optional<float> noise_scale;   // the voice config by default
  std::optional<float> noise_scale_w; // the voice config by default
  float gain_db = 0.0F;               // in [kMinGainDb, kMaxGainDb], on top of the voice gain
  bool local_debug_enabled = false;   // plain, marked and the stage times of every sentence to stderr
  // ORT-format models used in place from the files mapped by the data reader: no copy of the weights, no arena, no
  // prepacking. Every model file must be in the ORT format.
  bool mmap_models = false;
};

// Sentence to sound: BERT, text_qf0, prosody labels, full_text and the length regulator in Start, then a flow and
// decoder step per NextChunk with the states of both passed from step to step. One call at a time.
class AcousticModel {
public:
  // The reference level of the voice gain_db in this family of models: the output is scaled by gain_db - 20 dB.
  static constexpr float kReferenceLevelDb = 20.0F;
  // A sentence of the backend is at most 400 code points; pauses at most double it. The plain text is bounded by the
  // bytes of as many code points.
  static constexpr size_t kMaxSymbols = 800;
  static constexpr size_t kMaxPlainBytes = 4 * kMaxSymbols;
  // Durations above it are a broken model, not speech: the real voice gives at most 20 frames per symbol (short
  // sentences, speed 1), so 80 at speed 0.25, and a quarter more.
  static constexpr int64_t kMaxFramesPerSymbol = 100;
  // Frames x positions bound the memory of Start: the real voice grows by 260 MB at 3 million, 412 MB at 8.1 million
  // (800 symbols at speed 1), 1.1 GB at 28 million (800 symbols at speed 0.25). A slow sentence must be shorter.
  static constexpr int64_t kMaxFramesByPositions = int64_t{1} << 23;

  AcousticModel(
    const AcousticParams& params,
    const IDataReaderPtr& data_reader,
    const std::map<std::string, EnvironmentPtr>* environments = nullptr
  );
  // A copy would share the sessions and their states; the owner keeps the object in place.
  AcousticModel(const AcousticModel&) = delete;
  AcousticModel& operator=(const AcousticModel&) = delete;
  AcousticModel(AcousticModel&&) = delete;
  AcousticModel& operator=(AcousticModel&&) = delete;

  // Throws on a broken sentence, a negative speaker or a failed model (a speaker past the table of the voice fails in its
  // session); the model is reset then.
  void Start(const PreparedSentence& sentence, int64_t speaker_id);
  // Mono samples in [-1, 1] at SampleRate(); empty when finished. Throws on a failed model; the model is reset then.
  std::vector<float> NextChunk();
  bool Finished() const;
  void Reset();
  int SampleRate() const;

  // The sentence with the pauses of the pause model, as Start sounds it: '^' right after a word whose last BERT token
  // the model gives a pause (the softmax of pause_scores above pause_threshold of the voice) when the next symbol after
  // the spaces is a letter (not the last word, no mark or pause there yet). Without the model the sentence as it is.
  // Throws on a broken sentence or a failed model; the model is reset then.
  PreparedSentence AddPauses(const PreparedSentence& sentence);

private:
  using Clock = std::chrono::steady_clock;

  // Milliseconds of the stages of a sentence, counted under local_debug_enabled.
  struct StageTimes {
    double bert = 0;
    double pauses = 0;
    double text_qf0 = 0;
    double labels = 0;
    double full_text = 0;
    double length_regulator = 0;
    double flow = 0;
    double decoder = 0;
    size_t flow_steps = 0;
    size_t decoder_steps = 0;
  };

  // The text of the sentence: symbol ids and which of them are letters.
  struct Symbols {
    std::vector<int64_t> ids;
    size_t count = 0;
    bool letters = false;
  };

  void CheckModels();
  Symbols Encode(const std::string& marked) const;
  void Prepare(const PreparedSentence& sentence, int64_t speaker_id);
  PreparedSentence AddPauses(const PreparedSentence& sentence, const BertEmbeddings& bert);
  void RunTextQf0(const Symbols& symbols, const BertEmbeddings& bert, int64_t speaker_id);
  void RunFullText(
    const Symbols& symbols, const BertEmbeddings& bert, const std::vector<int64_t>& labels, int64_t speaker_id
  );
  // The audio frames of the sentence, 0 when it is silent.
  int64_t CheckDurations(const Symbols& symbols, int64_t& spec_frames);
  void RunLengthRegulator(int64_t spec_frames, int length);
  void Step(std::vector<float>& chunk);
  // The input resized to `shape`, unconditionally for an input not used since the last failure.
  TensorPtr Input(Interpreter& model, const std::string& name, const std::vector<int>& shape);
  template<typename T>
  void Fill(Interpreter& model, const std::string& name, const std::vector<int>& shape, T value);
  void Pass(Interpreter& from, const std::string& output, Interpreter& to, const std::string& input);
  void Fail();
  Clock::time_point Now() const;
  // Adds the time since `from` to `total` and moves `from` to now.
  void Lap(double& total, Clock::time_point& from) const;
  void PrintTimes() const;

  VoiceConfig voice_;
  std::optional<QuestionRules> question_rules_;
  Bert bert_;
  InterpreterPtr text_qf0_;
  InterpreterPtr full_text_;
  InterpreterPtr length_regulator_;
  InterpreterPtr flow_;
  InterpreterPtr decoder_;
  InterpreterPtr pauses_; // null without the pause model
  bool decoder_takes_window_ = false;
  float letter_length_scale_ = 1.0F;
  float other_length_scale_ = 1.0F;
  float noise_scale_ = 0.0F;
  float noise_scale_w_ = 0.0F;
  float gain_ = 1.0F;
  bool debug_ = false;
  StageTimes times_;
  // Inputs whose shape is known to match their tensor: a failure inside a resize of the model module may leave a
  // tensor of the old shape over a freed buffer, so after a failure every input is resized before its use.
  std::set<std::pair<const Interpreter*, std::string>> trusted_shapes_;

  // The sentence in synthesis: z_p [channels, frames], the frames for the decoder [channels, num_frames].
  std::vector<float> z_p_;
  std::vector<int64_t> y_mask_;
  std::vector<float> decoder_frames_;
  size_t channels_ = 0;
  size_t frames_ = 0;
  size_t decoder_frames_count_ = 0;
  size_t step_ = 0;
  size_t audio_frames_left_ = 0;
};

} // namespace giga_edge::vits

namespace giga_edge {

// Throws std::invalid_argument on a value out of its range.
template<>
vits::AcousticParams ParseConfig(const YAML::Node& config);

} // namespace giga_edge
