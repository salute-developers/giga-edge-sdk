#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/modules/model/interpreter/interpreter.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/text_model.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace giga_edge::vits {

// The stress of a word that no dictionary knows: a small network over the letters.
// Inputs, int64 [1, letters + 2]: input_ids ([bos], an id per letter, [eos]), attention_mask (ones),
// token_type_ids (zeros). Output logits [1, letters + 2, 3]: no stress, stress, secondary stress.
// The vocabulary is a symbol per line, the id is the number of the line; it must have [unk], [bos] and [eos].
// One more line, "[positions]<TAB>N" (text_model.h), is not a symbol and takes no id: the model has N positions, so
// a word of more than N - 2 letters does not fit it.
class StressModel {
public:
  // `environment`: a shared ONNX environment of `environments`; empty: an own session of one thread.
  StressModel(const TextModel& model, const std::string& vocabulary_path, const IDataReaderPtr& data_reader);

  // The letter of the stressed vowel of a word of the letters а-я and ё that has a vowel: the vowel with the
  // highest log-odds of the stress class, logit - ln(exp(the other two logits)); the last of the equal ones.
  size_t StressedLetter(std::string_view word);

  // Longer words do not fit the positions of the model.
  size_t MaxLetters() const {
    return max_letters_;
  }

private:
  static constexpr size_t kLetters = 33; // а..я, ё

  TextModel model_;
  std::array<int64_t, kLetters> letter_ids_{};
  int64_t bos_ = 0;
  int64_t eos_ = 0;
  size_t max_letters_ = 0;
};

} // namespace giga_edge::vits
