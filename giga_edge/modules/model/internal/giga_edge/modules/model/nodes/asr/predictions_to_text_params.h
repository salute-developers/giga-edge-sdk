#pragma once

#include <string>

namespace giga_edge {

enum class TokenizerType {
  PLAIN,
  SENTENCEPIECE,
};

struct PredictionsToTextNodeParams {
  std::string dict_path;
  std::string eos_token;
  std::string blank_token;
  bool ignore_duplicates;
  TokenizerType tokenizer_type = TokenizerType::PLAIN;
  // A capital letter at the start of the text and after . ? ! (for models that write lowercase only).
  bool capitalize_sentences = false;
  // Word forms of proper nouns, one lowercase form per line: such words are capitalized. The last word of a chunk
  // is held back until the next one starts or until push, to decide on the whole word.
  std::string proper_nouns_path;
};

} // namespace giga_edge