#include "giga_edge/modules/tts/interpreter/backends/vits/text/homograph_model.h"

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/text_model.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace giga_edge::vits {

namespace {

constexpr int kClasses = 2;

} // namespace

HomographModel::HomographModel(
  const std::vector<TextModel>& models, const std::string& vocabulary_path, const IDataReaderPtr& data_reader
) {
  auto vocabulary = ToString(EnsureNotNull(data_reader->ReadTransientData(vocabulary_path), vocabulary_path));
  // [CLS], [SEP], a token of the reading and [SEP] at the least.
  positions_ = TakePositions(vocabulary, vocabulary_path, 4);
  try {
    tokenizer_.emplace(vocabulary, kMaxWordLength);
  } catch (const std::exception& error) {
    throw std::runtime_error("vits: homograph model vocabulary '" + vocabulary_path + "': " + error.what());
  }
  const auto id = [&](const char* token) {
    const auto found = tokenizer_->Find(token);
    if (!found) {
      throw std::runtime_error("vits: homograph model vocabulary '" + vocabulary_path + "' has no " + token);
    }
    return *found;
  };
  pad_ = id("[PAD]");
  cls_ = id("[CLS]");
  sep_ = id("[SEP]");
  open_ = id("<w>");
  close_ = id("</w>");
  for (const auto& model : models) {
    CheckTextModel(
      model,
      {{"input_ids", Tensor::DataType::INT64, 2}, {"attention_mask", Tensor::DataType::INT64, 2}},
      {{"logits", Tensor::DataType::FLOAT32, 2}}
    );
    models_.push_back(model);
  }
}

HomographModel::Tokens HomographModel::Encode(std::string_view text) const {
  Tokens ids;
  for (const auto& token : tokenizer_->Tokenize(text)) {
    ids.push_back(token.id);
  }
  return ids;
}

HomographModel::Choice HomographModel::Choose(
  const std::vector<Tokens>& sentence, size_t homograph, const std::vector<std::string>& readings
) {
  Tokens head{cls_};
  for (size_t i = 0; i < sentence.size(); ++i) {
    if (i == homograph) {
      head.push_back(open_);
    }
    head.insert(head.end(), sentence[i].begin(), sentence[i].end());
    if (i == homograph) {
      head.push_back(close_);
    }
  }
  head.push_back(sep_);
  std::vector<Tokens> rows;
  size_t length = 0;
  for (const auto& reading : readings) {
    auto row = head;
    const auto tokens = Encode(reading);
    row.insert(row.end(), tokens.begin(), tokens.end());
    row.push_back(sep_);
    row.resize(std::min(row.size(), positions_));
    length = std::max(length, row.size());
    rows.push_back(std::move(row));
  }

  const auto shape = TensorShape({rows.size(), length});
  std::vector<double> probabilities(rows.size(), 0.0);
  for (const auto& model : models_) {
    const auto& interpreter = model.session;
    interpreter->ResizeInput("input_ids", shape.data(), 2);
    interpreter->ResizeInput("attention_mask", shape.data(), 2);
    auto* ids = interpreter->Input("input_ids")->Int64Data();
    auto* mask = interpreter->Input("attention_mask")->Int64Data();
    for (const auto& row : rows) {
      ids = std::fill_n(std::copy(row.begin(), row.end(), ids), length - row.size(), pad_);
      mask = std::fill_n(std::fill_n(mask, row.size(), int64_t{1}), length - row.size(), int64_t{0});
    }
    RunTextModel(model);

    const auto output = interpreter->Output("logits");
    if (output->GetShape() != std::vector<int>{shape[0], kClasses}) {
      ThrowTextModelError(model, "the output 'logits' is not [readings, 2]");
    }
    const float* logits = output->FloatData();
    for (size_t i = 0; i < rows.size(); ++i) {
      const double margin = static_cast<double>(logits[i * kClasses + 1]) - logits[i * kClasses];
      if (!std::isfinite(margin)) {
        ThrowTextModelError(model, "the output 'logits' is not a number");
      }
      probabilities[i] += 1.0 / (1.0 + std::exp(-margin));
    }
  }
  const auto best = std::max_element(probabilities.begin(), probabilities.end()); // the first of the equal
  return {static_cast<size_t>(best - probabilities.begin()), *best / static_cast<double>(models_.size())};
}

} // namespace giga_edge::vits
