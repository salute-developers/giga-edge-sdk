#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/acoustic_model.h"

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/enum/enum_fwd.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/prosody.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/directory_reader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace giga_edge::vits {
namespace {

constexpr char kPauseModel[] = "pause_predictor.onnx";

std::string Join(const std::string& directory, const std::string& name) {
  return directory.empty() ? name : directory + "/" + name;
}

template<typename T>
std::string Text(T value) {
  std::ostringstream text;
  text << value;
  return text.str();
}

// The params as they are, or an exception on a value out of its range.
AcousticParams Checked(const AcousticParams& params) {
  const auto check = [](bool condition, const std::string& message) {
    if (!condition) {
      throw std::invalid_argument("vits acoustic params: " + message);
    }
  };
  const auto range = [](float min, float max) {
    return " must be in [" + Text(min) + ", " + Text(max) + "]";
  };
  check(params.threads >= 1, "'threads' must be >= 1");
  check(
    params.speed >= AcousticParams::kMinSpeed && params.speed <= AcousticParams::kMaxSpeed,
    "'speed'" + range(AcousticParams::kMinSpeed, AcousticParams::kMaxSpeed)
  );
  check(
    !params.noise_scale || (std::isfinite(*params.noise_scale) && *params.noise_scale >= 0),
    "'noise_scale' must be >= 0"
  );
  check(
    !params.noise_scale_w || (std::isfinite(*params.noise_scale_w) && *params.noise_scale_w >= 0),
    "'noise_scale_w' must be >= 0"
  );
  check(
    params.gain_db >= AcousticParams::kMinGainDb && params.gain_db <= AcousticParams::kMaxGainDb,
    "'gain_db'" + range(AcousticParams::kMinGainDb, AcousticParams::kMaxGainDb)
  );
  return params;
}

InterpreterPtr Load(
  const AcousticParams& params,
  const std::string& name,
  const IDataReaderPtr& data_reader,
  const std::map<std::string, EnvironmentPtr>* environments
) {
  const auto path = Join(params.models_path, name);
  const auto models = std::make_shared<DirectoryReader>(data_reader, params.models_path);
  if (params.mmap_models) {
    // The ORT format is a flatbuffer with the identifier "ORTM" at byte 4.
    const auto bytes = EnsureNotNull(models->ReadPersistentData(name), path);
    const auto [data, size] = bytes->Get();
    if (size < 8 || std::memcmp(data + 4, "ORTM", 4) != 0) {
      throw std::invalid_argument("vits: 'mmap_models' takes models in the ORT format, '" + path + "' is not");
    }
  }
  InterpreterParams interpreter{};
  interpreter.type = InterpreterParams::InterpreterType::ONNX;
  interpreter.onnx_params.use_ort_format = params.mmap_models;
  interpreter.onnx_params.disable_copy_to_ram = params.mmap_models;
  interpreter.onnx_params.use_environment = params.onnx_environment;
  interpreter.onnx_params.intra_op_num_threads = params.threads;
  interpreter.onnx_params.optimization_level = params.optimization_level;
  return MakeInterpreter(interpreter, name, models, environments);
}

template<typename T>
T* Data(const TensorPtr& tensor) {
  if constexpr (std::is_same_v<T, float>) {
    return tensor->FloatData();
  } else {
    return tensor->Int64Data();
  }
}

// The state output of a streaming model to its state input for the next run: the same shape.
void PassState(Interpreter& model, const char* name) {
  const auto latents = model.Output("latents");
  const auto state = model.Input("prev_outputs");
  if (latents->GetShape() != state->GetShape()) {
    throw std::runtime_error(
      std::string("vits: the state of ") + name + " changes its shape: a dynamic axis other than the batch?"
    );
  }
  std::memcpy(state->Data(), latents->Data(), static_cast<size_t>(state->Size()));
}

using Signature = std::initializer_list<std::pair<const char*, Tensor::DataType>>;

// All the inputs of a model and the outputs the code reads, with their element types: a missing input or output, an
// input more (a state nobody passes) or another type throws.
void CheckSignature(const Interpreter& model, const char* name, Signature inputs, Signature outputs) {
  const auto check = [name](const TensorPtr& tensor, const char* tensor_name, Tensor::DataType type) {
    if (tensor->GetDataType() != type) {
      throw std::runtime_error(std::string("vits: '") + tensor_name + "' of " + name + " has another element type");
    }
  };
  for (const auto& [tensor, type] : inputs) {
    check(model.Input(tensor), tensor, type);
  }
  for (const auto& [tensor, type] : outputs) {
    check(model.Output(tensor), tensor, type);
  }
  for (const auto& input : model.InputNames()) {
    const auto known = std::find_if(inputs.begin(), inputs.end(), [&input](const auto& expected) {
      return input == expected.first;
    });
    if (known == inputs.end()) {
      throw std::runtime_error("vits: " + std::string(name) + " has an unknown input '" + input + "'");
    }
  }
}

} // namespace

AcousticModel::AcousticModel(
  const AcousticParams& params,
  const IDataReaderPtr& data_reader,
  const std::map<std::string, EnvironmentPtr>* environments
)
  : voice_(ReadVoiceConfig(data_reader, Join(Checked(params).models_path, "voice.json")))
  , bert_(
      Load(params, "bert.onnx", data_reader, environments),
      WordPiece(ReadText(data_reader, Join(params.models_path, "bert_vocab.txt")))
    )
  , text_qf0_(Load(params, "text_qf0.onnx", data_reader, environments))
  , full_text_(Load(params, "full_text.onnx", data_reader, environments))
  , length_regulator_(Load(params, "length_regulator.onnx", data_reader, environments))
  , flow_(Load(params, "flow.onnx", data_reader, environments))
  , decoder_(Load(params, "decoder.onnx", data_reader, environments))
  , letter_length_scale_(voice_.length_scale / params.speed)
  , other_length_scale_(1.0F / params.speed)
  , noise_scale_(params.noise_scale.value_or(voice_.noise_scale))
  , noise_scale_w_(params.noise_scale_w.value_or(voice_.noise_scale_w))
  , gain_(std::pow(10.0F, (voice_.gain_db - kReferenceLevelDb + params.gain_db) / 20.0F))
  , debug_(params.local_debug_enabled) {
  if (!std::isfinite(gain_) || !std::isfinite(letter_length_scale_)) {
    throw std::invalid_argument("vits: the gain or the length scale of the voice and the params is not finite");
  }
  if (voice_.use_question_rules) {
    const auto path = Join(params.models_path, "question_rules.json");
    question_rules_.emplace(ReadText(data_reader, path), path);
  }
  if (data_reader->Exists(Join(params.models_path, kPauseModel))) {
    pauses_ = Load(params, kPauseModel, data_reader, environments);
  }
  const auto inputs = decoder_->InputNames();
  decoder_takes_window_ = std::find(inputs.begin(), inputs.end(), "num_frames") != inputs.end();
  CheckModels();
  Reset();
}

void AcousticModel::CheckModels() {
  constexpr auto float32 = Tensor::DataType::FLOAT32;
  constexpr auto int64 = Tensor::DataType::INT64;
  CheckSignature(
    *text_qf0_,
    "text_qf0",
    {{"text_ids", int64}, {"text_mask", int64}, {"bert_emb", float32}, {"bert_mask", int64}, {"speaker_ids", int64}},
    {{"text_emb", float32}, {"qf0_logits", float32}}
  );
  CheckSignature(
    *full_text_,
    "full_text",
    {{"text_emb", float32},
     {"text_mask", int64},
     {"q_labels", int64},
     {"bert_emb", float32},
     {"bert_mask", int64},
     {"speaker_ids", int64},
     {"length_scale", float32},
     {"noise_scale_w", float32}},
    {{"m_p", float32}, {"logs_p", float32}, {"w_ceil", float32}, {"y_lengths", int64}}
  );
  CheckSignature(
    *length_regulator_,
    "length_regulator",
    {{"m_p", float32}, {"logs_p", float32}, {"w_ceil", float32}, {"attn_mask", int64}, {"noise_scale", float32}},
    {{"z_p", float32}, {"y_mask", int64}}
  );
  CheckSignature(
    *flow_,
    "flow",
    {{"x", float32},
     {"x_mask", int64},
     {"prev_outputs", float32},
     {"num_frames", int64},
     {"overlap", int64},
     {"speaker_ids", int64}},
    {{"out", float32}, {"latents", float32}}
  );
  if (pauses_) {
    CheckSignature(*pauses_, "pause_predictor", {{"bert_embeddings", float32}}, {{"pause_scores", float32}});
  }
  if (decoder_takes_window_) {
    CheckSignature(
      *decoder_,
      "decoder",
      {{"x", float32}, {"prev_outputs", float32}, {"num_frames", int64}, {"overlap", int64}, {"speaker_ids", int64}},
      {{"wav", float32}, {"latents", float32}}
    );
  } else {
    CheckSignature(
      *decoder_,
      "decoder",
      {{"x", float32}, {"prev_outputs", float32}, {"speaker_ids", int64}},
      {{"wav", float32}, {"latents", float32}}
    );
  }

  // One idle step of flow and decoder: a state that changes its shape (a dynamic axis other than the batch) fails here.
  const auto& streaming = voice_.streaming;
  const int channels = flow_->Input("x")->Dim(1);
  const int window = static_cast<int>(streaming.num_frames + streaming.flow_overlap);
  Fill<float>(*flow_, "x", {1, channels, window}, 0.0F);
  Fill<int64_t>(*flow_, "x_mask", {1, 1, window}, 0);
  Fill<int64_t>(*flow_, "num_frames", {1}, window);
  Fill<int64_t>(*flow_, "overlap", {1}, streaming.flow_overlap);
  Fill<int64_t>(*flow_, "speaker_ids", {1}, 0);
  flow_->Invoke();
  PassState(*flow_, "flow");
  Fill<float>(*decoder_, "x", {1, channels, static_cast<int>(streaming.num_frames)}, 0.0F);
  Fill<int64_t>(*decoder_, "speaker_ids", {1}, 0);
  if (decoder_takes_window_) {
    Fill<int64_t>(*decoder_, "num_frames", {1}, streaming.num_frames);
    Fill<int64_t>(*decoder_, "overlap", {1}, streaming.decoder_overlap);
  }
  decoder_->Invoke();
  PassState(*decoder_, "decoder");
}

void AcousticModel::Start(const PreparedSentence& sentence, int64_t speaker_id) {
  Reset();
  try {
    Prepare(sentence, speaker_id);
  } catch (...) {
    Fail();
    throw;
  }
  if (debug_ && Finished()) {
    PrintTimes();
  }
}

std::vector<float> AcousticModel::NextChunk() {
  std::vector<float> chunk;
  if (Finished()) {
    return chunk;
  }
  try {
    Step(chunk);
  } catch (...) {
    Fail();
    throw;
  }
  if (debug_ && Finished()) {
    PrintTimes();
  }
  return chunk;
}

bool AcousticModel::Finished() const {
  return audio_frames_left_ == 0;
}

void AcousticModel::Reset() {
  frames_ = 0;
  step_ = 0;
  audio_frames_left_ = 0;
  decoder_frames_count_ = 0;
  for (auto* model : {flow_.get(), decoder_.get()}) {
    const auto state = model->Input("prev_outputs");
    std::fill_n(state->FloatData(), state->NumElements(), 0.0F);
  }
}

void AcousticModel::Fail() {
  trusted_shapes_.clear();
  Reset();
}

int AcousticModel::SampleRate() const {
  return static_cast<int>(voice_.sample_rate);
}

TensorPtr AcousticModel::Input(Interpreter& model, const std::string& name, const std::vector<int>& shape) {
  auto tensor = model.Input(name);
  auto key = std::make_pair(static_cast<const Interpreter*>(&model), name);
  if (trusted_shapes_.count(key) == 0 || tensor->GetShape() != shape) {
    trusted_shapes_.erase(key);
    model.ResizeInput(name, shape.data(), static_cast<int>(shape.size()));
    model.ReallocateTensors();
    tensor = model.Input(name);
    trusted_shapes_.insert(std::move(key));
  }
  return tensor;
}

template<typename T>
void AcousticModel::Fill(Interpreter& model, const std::string& name, const std::vector<int>& shape, T value) {
  const auto tensor = Input(model, name, shape);
  std::fill_n(Data<T>(tensor), tensor->NumElements(), value);
}

// An output of one model to an input of another as it is.
void AcousticModel::Pass(Interpreter& from, const std::string& output, Interpreter& to, const std::string& input) {
  const auto source = from.Output(output);
  const auto target = Input(to, input, source->GetShape());
  if (source->GetDataType() != target->GetDataType() || source->Size() != target->Size()) {
    throw std::runtime_error("vits: output '" + output + "' does not fit input '" + input + "'");
  }
  std::memcpy(target->Data(), source->Data(), static_cast<size_t>(source->Size()));
}

AcousticModel::Clock::time_point AcousticModel::Now() const {
  return debug_ ? Clock::now() : Clock::time_point();
}

void AcousticModel::Lap(double& total, Clock::time_point& from) const {
  if (debug_) {
    const auto now = Clock::now();
    total += std::chrono::duration<double, std::milli>(now - from).count();
    from = now;
  }
}

void AcousticModel::PrintTimes() const {
  std::cerr << "[vits acoustic] ms: bert " << times_.bert << ", pauses " << times_.pauses << ", text_qf0 "
            << times_.text_qf0 << ", labels " << times_.labels << ", full_text " << times_.full_text
            << ", length_regulator " << times_.length_regulator << ", flow " << times_.flow << " in "
            << times_.flow_steps << " steps, decoder " << times_.decoder << " in " << times_.decoder_steps << " steps"
            << std::endl;
}

void AcousticModel::Prepare(const PreparedSentence& sentence, int64_t speaker_id) {
  times_ = {};
  // The models take a negative id from the end of their speaker table; one past the table fails in the session.
  if (speaker_id < 0) {
    throw std::invalid_argument("vits: a negative speaker id " + Text(speaker_id));
  }
  if (debug_) {
    std::cerr << "[vits acoustic] plain \"" << sentence.plain << "\", marked \"" << sentence.marked << "\""
              << std::endl;
  }
  auto time = Now();
  // Both texts are bounded before anything is allocated by them.
  if (sentence.plain.size() > kMaxPlainBytes) {
    throw std::invalid_argument("vits: a plain text of more than " + Text(kMaxPlainBytes) + " bytes");
  }
  auto symbols = Encode(sentence.marked);
  // Marks, pauses and spaces alone are not speech.
  if (!symbols.letters) {
    return;
  }
  const auto bert = bert_.Embed(sentence.plain);
  Lap(times_.bert, time);
  if (bert.tokens.empty()) {
    throw std::invalid_argument("vits: the plain text of a sentence has no tokens");
  }
  const auto paused = AddPauses(sentence, bert);
  Lap(times_.pauses, time);
  if (paused.marked.size() != sentence.marked.size()) {
    symbols = Encode(paused.marked);
    if (debug_) {
      std::cerr << "[vits acoustic] paused \"" << paused.marked << "\"" << std::endl;
    }
  }

  RunTextQf0(symbols, bert, speaker_id);
  Lap(times_.text_qf0, time);
  const auto logits = text_qf0_->Output("qf0_logits");
  const auto labels = MakeProsodyLabels(
    paused, bert.tokens, logits->FloatData(), logits->GetShape(), voice_, question_rules_ ? &*question_rules_ : nullptr
  );
  Lap(times_.labels, time);

  RunFullText(symbols, bert, labels, speaker_id);
  Lap(times_.full_text, time);
  int64_t spec_frames = 0;
  const auto audio_frames = CheckDurations(symbols, spec_frames);
  if (audio_frames == 0) {
    return;
  }

  RunLengthRegulator(spec_frames, static_cast<int>(symbols.ids.size()));
  Lap(times_.length_regulator, time);
  const auto num_frames = static_cast<size_t>(voice_.streaming.num_frames);
  frames_ = static_cast<size_t>(spec_frames);
  audio_frames_left_ = static_cast<size_t>(audio_frames);
  decoder_frames_.assign(channels_ * num_frames, 0.0F);
  Fill<int64_t>(*flow_, "speaker_ids", {1}, speaker_id);
  Fill<int64_t>(*decoder_, "speaker_ids", {1}, speaker_id);
  Input(*decoder_, "x", {1, static_cast<int>(channels_), static_cast<int>(num_frames)});
  if (decoder_takes_window_) {
    Fill<int64_t>(*decoder_, "num_frames", {1}, voice_.streaming.num_frames);
    Fill<int64_t>(*decoder_, "overlap", {1}, voice_.streaming.decoder_overlap);
  }
}

AcousticModel::Symbols AcousticModel::Encode(const std::string& marked) const {
  Symbols symbols;
  symbols.ids = EncodeText(marked, voice_, kMaxSymbols);
  for (size_t i = voice_.blank_intersperse ? 1 : 0; i < symbols.ids.size(); i += voice_.blank_intersperse ? 2 : 1) {
    ++symbols.count;
    symbols.letters = symbols.letters || IsLetter(voice_.alphabet[static_cast<size_t>(symbols.ids[i])]);
  }
  return symbols;
}

PreparedSentence AcousticModel::AddPauses(const PreparedSentence& sentence) {
  if (!pauses_ || sentence.words.size() < 2) {
    return sentence;
  }
  if (sentence.plain.size() > kMaxPlainBytes) {
    throw std::invalid_argument("vits: a plain text of more than " + Text(kMaxPlainBytes) + " bytes");
  }
  try {
    return AddPauses(sentence, bert_.Embed(sentence.plain));
  } catch (...) {
    Fail();
    throw;
  }
}

PreparedSentence AcousticModel::AddPauses(const PreparedSentence& sentence, const BertEmbeddings& bert) {
  // A pause needs a word after the word: one word gets none.
  if (!pauses_ || sentence.words.size() < 2) {
    return sentence;
  }
  CheckWords(sentence);
  const auto& tokens = bert.tokens;
  const int length = static_cast<int>(tokens.size());
  const auto input = Input(*pauses_, "bert_embeddings", {1, length, bert.size});
  if (static_cast<size_t>(input->NumElements()) != bert.values.size()) {
    throw std::runtime_error("vits: the pause model takes embeddings of another size");
  }
  std::copy(bert.values.begin(), bert.values.end(), input->FloatData());
  pauses_->Invoke();
  const auto scores = pauses_->Output("pause_scores");
  if (scores->GetShape() != std::vector<int>{1, length, 2}) {
    throw std::runtime_error("vits: pause_scores of the pause model are not [1, tokens, 2]");
  }
  const float* logits = scores->FloatData();

  PreparedSentence result;
  result.plain = sentence.plain;
  result.words = sentence.words;
  const auto& marked = sentence.marked;
  size_t copied = 0; // the bytes of `marked` in result.marked
  size_t token = 0;
  for (size_t i = 0; i < sentence.words.size(); ++i) {
    const auto& word = sentence.words[i];
    result.words[i].marked.begin += result.marked.size() - copied;
    // The last token that starts inside the word.
    const size_t plain_end = word.plain.begin + word.plain.size;
    std::optional<size_t> last;
    for (; token < tokens.size() && tokens[token].range.begin < plain_end; ++token) {
      if (tokens[token].range.begin >= word.plain.begin) {
        last = token;
      }
    }
    if (!last || !(1.0F / (1.0F + std::exp(logits[2 * *last] - logits[2 * *last + 1])) > voice_.pause_threshold)) {
      continue;
    }
    size_t next = word.marked.begin + word.marked.size;
    while (next < marked.size() && marked[next] == ' ') {
      ++next;
    }
    if (next == marked.size() || !IsLetter(DecodeUtf8(marked, next).value)) {
      continue;
    }
    const size_t end = word.marked.begin + word.marked.size;
    result.marked.append(marked, copied, end - copied);
    result.marked += '^';
    copied = end;
  }
  result.marked.append(marked, copied, std::string::npos);
  return result;
}

void AcousticModel::RunTextQf0(const Symbols& symbols, const BertEmbeddings& bert, int64_t speaker_id) {
  auto& model = *text_qf0_;
  const int length = static_cast<int>(symbols.ids.size());
  const int tokens = static_cast<int>(bert.tokens.size());
  std::copy(symbols.ids.begin(), symbols.ids.end(), Input(model, "text_ids", {1, length})->Int64Data());
  Fill<int64_t>(model, "text_mask", {1, length}, 1);
  std::copy(bert.values.begin(), bert.values.end(), Input(model, "bert_emb", {1, tokens, bert.size})->FloatData());
  Fill<int64_t>(model, "bert_mask", {1, tokens}, 1);
  Fill<int64_t>(model, "speaker_ids", {1}, speaker_id);
  model.Invoke();
}

void AcousticModel::RunFullText(
  const Symbols& symbols, const BertEmbeddings& bert, const std::vector<int64_t>& labels, int64_t speaker_id
) {
  auto& model = *full_text_;
  const int length = static_cast<int>(symbols.ids.size());
  const int tokens = static_cast<int>(bert.tokens.size());
  const int features = static_cast<int>(voice_.embedding_dims.size());
  Pass(*text_qf0_, "text_emb", model, "text_emb");
  Fill<int64_t>(model, "text_mask", {1, length}, 1);
  std::copy(labels.begin(), labels.end(), Input(model, "q_labels", {1, length, features})->Int64Data());
  std::copy(bert.values.begin(), bert.values.end(), Input(model, "bert_emb", {1, tokens, bert.size})->FloatData());
  Fill<int64_t>(model, "bert_mask", {1, tokens}, 1);
  Fill<int64_t>(model, "speaker_ids", {1}, speaker_id);
  // The speed scales every position: blanks and marks carry most of the duration.
  auto* length_scale = Input(model, "length_scale", {1, length})->FloatData();
  for (size_t i = 0; i < symbols.ids.size(); ++i) {
    const bool letter = IsLetter(voice_.alphabet[static_cast<size_t>(symbols.ids[i])]);
    length_scale[i] = letter ? letter_length_scale_ : other_length_scale_;
  }
  Fill<float>(model, "noise_scale_w", {1}, noise_scale_w_);
  model.Invoke();
}

int64_t AcousticModel::CheckDurations(const Symbols& symbols, int64_t& spec_frames) {
  auto& model = *full_text_;
  const auto durations = model.Output("w_ceil");
  const auto spectrogram = model.Output("y_lengths");
  if (
    durations->GetShape() != std::vector<int>{1, 1, static_cast<int>(symbols.ids.size())}
    || spectrogram->NumElements() != 1
  ) {
    throw std::runtime_error("vits: full_text gives w_ceil or y_lengths of an unexpected shape");
  }
  // The audio is the sum of the durations, at most the spectrogram; both bounded before anything is allocated by them.
  const auto max_frames = static_cast<double>(kMaxFramesPerSymbol) * static_cast<double>(symbols.count);
  const auto* duration = durations->FloatData();
  double total = 0;
  for (int i = 0; i < durations->NumElements(); ++i) {
    if (!std::isfinite(duration[i]) || duration[i] < 0) {
      throw std::runtime_error("vits: full_text gives a duration that is not a finite number >= 0");
    }
    total += duration[i];
  }
  spec_frames = spectrogram->Int64Data()[0];
  if (spec_frames < 0) {
    throw std::runtime_error("vits: full_text gives a negative number of frames");
  }
  if (total > max_frames || static_cast<double>(spec_frames) > max_frames) {
    throw std::runtime_error("vits: full_text gives more than " + Text(kMaxFramesPerSymbol) + " frames per symbol");
  }
  const auto positions = static_cast<int64_t>(symbols.ids.size());
  if (spec_frames > kMaxFramesByPositions / positions) {
    throw std::runtime_error(
      "vits: " + Text(spec_frames) + " frames by " + Text(positions) + " positions are more than "
      + Text(kMaxFramesByPositions) + ": too long a sentence for its speed"
    );
  }
  return std::min<int64_t>(std::llround(total), spec_frames);
}

void AcousticModel::RunLengthRegulator(int64_t spec_frames, int length) {
  auto& model = *length_regulator_;
  Pass(*full_text_, "m_p", model, "m_p");
  Pass(*full_text_, "logs_p", model, "logs_p");
  Pass(*full_text_, "w_ceil", model, "w_ceil");
  Fill<int64_t>(model, "attn_mask", {1, 1, static_cast<int>(spec_frames), length}, 1);
  Fill<float>(model, "noise_scale", {1}, noise_scale_);
  model.Invoke();
  const auto z_p = model.Output("z_p");
  const auto y_mask = model.Output("y_mask");
  const auto shape = z_p->GetShape();
  if (
    shape.size() != 3 || shape[0] != 1 || shape[1] < 1 || shape[2] != spec_frames || y_mask->NumElements() != shape[2]
  ) {
    throw std::runtime_error("vits: the length regulator gives z_p of an unexpected shape");
  }
  z_p_.assign(z_p->FloatData(), z_p->FloatData() + z_p->NumElements());
  y_mask_.assign(y_mask->Int64Data(), y_mask->Int64Data() + y_mask->NumElements());
  channels_ = static_cast<size_t>(shape[1]);
}

void AcousticModel::Step(std::vector<float>& chunk) {
  const auto& streaming = voice_.streaming;
  const auto num_frames = static_cast<size_t>(streaming.num_frames);
  const auto flow_overlap = static_cast<size_t>(streaming.flow_overlap);
  const auto decoder_step = num_frames - static_cast<size_t>(streaming.decoder_overlap);
  // The first flow step makes num_frames frames, the next ones a decoder step each; flow_overlap frames more are the
  // right context.
  const bool first = step_ == 0;
  const size_t take = first ? num_frames : decoder_step;
  const size_t begin = first ? 0 : num_frames + (step_ - 1) * decoder_step;
  const size_t window = take + flow_overlap;

  auto time = Now();
  if (begin < frames_) {
    auto& flow = *flow_;
    auto* x = Input(flow, "x", {1, static_cast<int>(channels_), static_cast<int>(window)})->FloatData();
    auto* mask = Input(flow, "x_mask", {1, 1, static_cast<int>(window)})->Int64Data();
    Fill<int64_t>(flow, "num_frames", {1}, static_cast<int64_t>(window));
    Fill<int64_t>(flow, "overlap", {1}, static_cast<int64_t>(flow_overlap));
    const auto valid = std::min(window, frames_ - begin);
    for (size_t channel = 0; channel < channels_; ++channel) {
      const auto* from = z_p_.data() + channel * frames_ + begin;
      std::copy(from, from + valid, x + channel * window);
      std::fill(x + channel * window + valid, x + (channel + 1) * window, 0.0F);
    }
    std::copy(
      y_mask_.begin() + static_cast<ptrdiff_t>(begin), y_mask_.begin() + static_cast<ptrdiff_t>(begin + valid), mask
    );
    std::fill(mask + valid, mask + window, 0);
    flow.Invoke();
    PassState(flow, "flow");
    const auto out = flow.Output("out");
    if (out->NumElements() != static_cast<int>(channels_ * window) || decoder_frames_count_ + take > num_frames) {
      throw std::runtime_error("vits: flow gives an output of an unexpected size");
    }
    const auto* frames = out->FloatData();
    for (size_t channel = 0; channel < channels_; ++channel) {
      std::copy(
        frames + channel * window,
        frames + channel * window + take,
        decoder_frames_.data() + channel * num_frames + decoder_frames_count_
      );
    }
    decoder_frames_count_ += take;
    Lap(times_.flow, time);
    ++times_.flow_steps;
  }

  auto& decoder = *decoder_;
  std::copy(decoder_frames_.begin(), decoder_frames_.end(), decoder.Input("x")->FloatData());
  decoder.Invoke();
  PassState(decoder, "decoder");
  const auto wav = decoder.Output("wav");
  const auto samples = static_cast<size_t>(wav->NumElements());
  if (samples == 0 || samples % num_frames != 0) {
    throw std::runtime_error("vits: the decoder gives audio of an unexpected size");
  }
  const auto ready = std::min(decoder_step, audio_frames_left_);
  chunk.resize(ready * (samples / num_frames));
  const auto* audio = wav->FloatData();
  std::transform(audio, audio + chunk.size(), chunk.begin(), [this](float sample) {
    const auto scaled = sample * gain_;
    return std::isfinite(scaled) ? std::clamp(scaled, -1.0F, 1.0F) : 0.0F;
  });
  audio_frames_left_ -= ready;
  Lap(times_.decoder, time);
  ++times_.decoder_steps;

  // The decoder frames after this step move to the front.
  const auto kept = decoder_frames_count_ > decoder_step ? decoder_frames_count_ - decoder_step : 0;
  for (size_t channel = 0; channel < channels_; ++channel) {
    auto* row = decoder_frames_.data() + channel * num_frames;
    std::copy(row + decoder_frames_count_ - kept, row + decoder_frames_count_, row);
    std::fill(row + kept, row + num_frames, 0.0F);
  }
  decoder_frames_count_ = kept;
  ++step_;
}

} // namespace giga_edge::vits

namespace giga_edge {

template<>
vits::AcousticParams ParseConfig(const YAML::Node& config) {
  vits::AcousticParams params;
  if (config["models_path"]) {
    params.models_path = config["models_path"].as<std::string>();
  }
  if (config["onnx_environment"]) {
    params.onnx_environment = config["onnx_environment"].as<std::string>();
  }
  if (config["threads"]) {
    params.threads = config["threads"].as<int>();
  }
  if (config["speed"]) {
    params.speed = config["speed"].as<float>();
  }
  if (config["noise_scale"]) {
    params.noise_scale = config["noise_scale"].as<float>();
  }
  if (config["noise_scale_w"]) {
    params.noise_scale_w = config["noise_scale_w"].as<float>();
  }
  if (config["gain_db"]) {
    params.gain_db = config["gain_db"].as<float>();
  }
  if (config["optimization_level"]) {
    params.optimization_level = EnumFromString<OnnxOptimizationLevel>(config["optimization_level"].as<std::string>());
  }
  if (config["local_debug_enabled"]) {
    params.local_debug_enabled = config["local_debug_enabled"].as<bool>();
  }
  if (config["mmap_models"]) {
    params.mmap_models = config["mmap_models"].as<bool>();
  }
  return vits::Checked(params);
}

} // namespace giga_edge
