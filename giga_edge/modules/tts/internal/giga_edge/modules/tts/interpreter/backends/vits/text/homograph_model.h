#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/modules/model/interpreter/interpreter.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/wordpiece.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/text_model.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace giga_edge::vits {

// The reading of a homograph by the whole sentence: a small BERT classifier of pairs "sentence, reading".
// A row of the input is [CLS], the tokens of the sentence with <w> and </w> around the homograph, [SEP], the tokens
// of a reading ("зам", "+", "ок"), [SEP]; it is cut to the positions of the model. All the readings of a word go as
// one batch padded with [PAD]. Inputs, int64 [readings, tokens]: input_ids, attention_mask. Output logits, float
// [readings, 2]; the probability of a reading is 1 / (1 + exp(-(logit 1 - logit 0))): the readings are not normed
// against each other. With several models of one vocabulary it is the mean of theirs. The answer is the most
// probable reading, the first of the equal.
// The vocabulary is a WordPiece one (acoustic/wordpiece.h, case kept, a word of more than 100 symbols is [UNK]) with
// [PAD], [UNK], [CLS], [SEP], <w>, </w> and the line "[positions]<TAB>N" (text_model.h).
class HomographModel {
public:
  using Tokens = std::vector<int64_t>;

  // `models`: opened, at least one.
  HomographModel(
    const std::vector<TextModel>& models, const std::string& vocabulary_path, const IDataReaderPtr& data_reader
  );

  Tokens Encode(std::string_view text) const;

  // `sentence`: the tokens of every word and mark of the sentence in order; `homograph`: the index of the word in
  // it; `readings`: the word with a '+' before the stressed vowel, at least one.
  struct Choice {
    size_t reading = 0;
    double probability = 0.0;
  };
  Choice Choose(const std::vector<Tokens>& sentence, size_t homograph, const std::vector<std::string>& readings);

private:
  static constexpr size_t kMaxWordLength = 100;

  std::vector<TextModel> models_;
  std::optional<WordPiece> tokenizer_;
  size_t positions_ = 0;
  int64_t pad_ = 0;
  int64_t cls_ = 0;
  int64_t sep_ = 0;
  int64_t open_ = 0;
  int64_t close_ = 0;
};

} // namespace giga_edge::vits
