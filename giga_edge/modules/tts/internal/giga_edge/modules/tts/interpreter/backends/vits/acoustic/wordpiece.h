#pragma once

#include "giga_edge/modules/tts/interpreter/backends/vits/prepared_sentence.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace giga_edge::vits {

struct WordPieceToken {
  int64_t id = 0;
  TextRange range; // the bytes of the text the token covers, without "##"
  bool unknown = false;
};

// BERT WordPiece of lower-case text: whitespace separates words, ASCII punctuation is a word of its own, a word splits
// greedily into the longest vocabulary pieces with "##" on continuations. A word that does not split to the end or is
// longer than the limit of code points (kMaxWordLength, as in the tokenizer the models were trained with, unless the
// model has its own) is one [UNK]. No [CLS]/[SEP].
class WordPiece {
public:
  static constexpr size_t kMaxWordLength = 150;

  // vocab.txt: a token per line; the id of a token is its number among the non-empty lines.
  explicit WordPiece(std::string_view vocab, size_t max_word_length = kMaxWordLength);

  std::vector<WordPieceToken> Tokenize(std::string_view text) const;

  // The id of a token of the vocabulary, a service one too ("[CLS]").
  std::optional<int64_t> Find(std::string_view token) const;

private:
  void AddWord(std::string_view text, size_t begin, size_t end, std::vector<WordPieceToken>& tokens) const;

  std::unordered_map<std::string, int64_t> ids_;
  int64_t unknown_id_ = 0;
  size_t max_word_length_ = kMaxWordLength;
};

} // namespace giga_edge::vits
