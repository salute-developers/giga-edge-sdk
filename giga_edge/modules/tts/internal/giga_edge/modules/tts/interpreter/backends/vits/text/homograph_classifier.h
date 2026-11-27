#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/wordpiece.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/text_model.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace giga_edge::vits {

// A homograph model of the second kind: the word is marked in its text and one logit chooses between the two
// readings of the word.
// The text is all that came to the stage, as one line. A word is the longest run of Russian letters of any case and
// '+'; the model reads the words of its readings. The context of a word is the text on its left and the text on its
// right, each cleaned (Clean in the .cpp) and cut to the 150 symbols next to the word.
// A row of the model: [CLS] left [HOMO] word [/HOMO] right [SEP]. The rows of all such words of the text go as one
// batch padded with [PAD]. The model has no mask and sees the padding, so the answer of a word depends on the other
// words of the text: the batch is always the whole one. Inputs, int64: input_ids [rows, tokens], homo_start_ids
// [rows] and homo_end_ids [rows], the places of the markers in a row. Output logits, float [rows, 1]: above zero is
// the second reading. The probability of the answer is 1 / (1 + exp(-|logit|)).
// The files:
//   readings    word<TAB>the reading of a negative answer<TAB>the reading of a positive one; a reading is the word
//               with '+' before its stressed vowel and may spell 'ё' for 'е' of the word
//   vocabulary  a WordPiece one (acoustic/wordpiece.h, case kept, a word of more than 100 symbols is [UNK]) with
//               [PAD], [UNK], [CLS], [SEP], [HOMO], [/HOMO] and the line "[positions]<TAB>N" (text_model.h)
class HomographClassifier {
public:
  // Throws std::runtime_error naming the file on a model with other inputs or a bad line of a file.
  HomographClassifier(
    const TextModel& model,
    const std::string& vocabulary_path,
    const std::string& readings_path,
    const IDataReaderPtr& data_reader
  );

  // The word (lower case) is in the readings.
  bool Knows(std::string_view word) const;

  struct Answer {
    size_t begin = 0; // the bytes of the word in the text
    size_t end = 0;
    std::string_view reading; // lives as long as the model
    double probability = 0.0;
  };
  // The readings of all the words of the text the model knows, in the order of the text.
  std::vector<Answer> Read(std::string_view text);

private:
  void Append(std::vector<int64_t>& row, std::string_view text) const;

  TextModel model_;
  std::optional<WordPiece> tokenizer_;
  std::map<std::string, std::array<std::string, 2>, std::less<>> readings_;
  int64_t pad_ = 0;
  int64_t cls_ = 0;
  int64_t sep_ = 0;
  int64_t open_ = 0;
  int64_t close_ = 0;
};

} // namespace giga_edge::vits
