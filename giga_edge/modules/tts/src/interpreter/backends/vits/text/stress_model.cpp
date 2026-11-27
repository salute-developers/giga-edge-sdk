#include "giga_edge/modules/tts/interpreter/backends/vits/text/stress_model.h"

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/letters.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/table.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/text_model.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>

namespace giga_edge::vits {

namespace {

constexpr int kClasses = 3;
constexpr int kStressClass = 1;

} // namespace

StressModel::StressModel(
  const TextModel& model, const std::string& vocabulary_path, const IDataReaderPtr& data_reader
) {
  auto vocabulary = ToString(EnsureNotNull(data_reader->ReadTransientData(vocabulary_path), vocabulary_path));
  // [bos], a letter and [eos] at the least.
  max_letters_ = TakePositions(vocabulary, vocabulary_path, 3) - 2;
  std::optional<int64_t> unknown;
  std::optional<int64_t> bos;
  std::optional<int64_t> eos;
  std::array<std::optional<int64_t>, kLetters> letters;
  int64_t id = 0;
  for (auto line : Split(vocabulary, '\n')) {
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (line == "[unk]") {
      unknown = id;
    } else if (line == "[bos]") {
      bos = id;
    } else if (line == "[eos]") {
      eos = id;
    } else if (IsRussianWord(line) && LetterCount(line) == 1) {
      const auto letter = LetterAt(line, 0);
      letters[letter == U'ё' ? kLetters - 1 : letter - U'а'] = id;
    }
    ++id;
  }
  if (!unknown || !bos || !eos) {
    throw std::runtime_error("vits: stress model vocabulary '" + vocabulary_path + "' has no [unk], [bos] or [eos]");
  }
  bos_ = *bos;
  eos_ = *eos;
  for (size_t i = 0; i < kLetters; ++i) {
    letter_ids_[i] = letters[i].value_or(*unknown);
  }

  constexpr auto int64 = Tensor::DataType::INT64;
  CheckTextModel(
    model,
    {{"input_ids", int64, 2}, {"attention_mask", int64, 2}, {"token_type_ids", int64, 2}},
    {{"logits", Tensor::DataType::FLOAT32, 3}}
  );
  model_ = model;
}

size_t StressModel::StressedLetter(std::string_view word) {
  const auto letters = LetterCount(word);
  if (letters > max_letters_) {
    throw std::invalid_argument("vits: stress model: the word is longer than the model takes");
  }
  const auto shape = TensorShape({1, letters + 2});
  for (const char* name : {"input_ids", "attention_mask", "token_type_ids"}) {
    model_.session->ResizeInput(name, shape.data(), 2);
  }
  auto* ids = model_.session->Input("input_ids")->Int64Data();
  ids[0] = bos_;
  for (size_t i = 0; i < letters; ++i) {
    const auto letter = LetterAt(word, i);
    ids[i + 1] = letter_ids_[letter == U'ё' ? kLetters - 1 : letter - U'а'];
  }
  ids[letters + 1] = eos_;
  std::fill_n(model_.session->Input("attention_mask")->Int64Data(), letters + 2, int64_t{1});
  std::fill_n(model_.session->Input("token_type_ids")->Int64Data(), letters + 2, int64_t{0});
  RunTextModel(model_);

  const auto output = model_.session->Output("logits");
  if (output->GetShape() != std::vector<int>{1, shape[1], kClasses}) {
    ThrowTextModelError(model_, "the output 'logits' is not [1, letters + 2, 3]");
  }
  const float* logits = output->FloatData();
  std::optional<size_t> best;
  double best_margin = 0.0;
  for (size_t i = 0; i < letters; ++i) {
    if (!IsVowel(LetterAt(word, i))) {
      continue;
    }
    const float* row = logits + (i + 1) * kClasses;
    // The log-odds of the stress against the other two classes: grows with its probability and does not round to
    // the same value for two sure vowels.
    const double none = row[0];
    const double secondary = row[2];
    const double margin = row[kStressClass]
      - (std::max(none, secondary) + std::log1p(std::exp(-std::abs(none - secondary))));
    if (!std::isfinite(margin)) {
      ThrowTextModelError(model_, "the output 'logits' is not a number");
    }
    if (!best || margin >= best_margin) {
      best = i;
      best_margin = margin;
    }
  }
  if (!best) {
    throw std::invalid_argument("vits: stress model: a word without vowels");
  }
  return *best;
}

} // namespace giga_edge::vits
