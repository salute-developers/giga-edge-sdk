#pragma once

#include "giga_edge/modules/model/interpreter/interpreter.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/wordpiece.h"

#include <string_view>
#include <vector>

namespace giga_edge::vits {

struct BertEmbeddings {
  std::vector<WordPieceToken> tokens;
  std::vector<float> values; // [tokens, size] row by row
  int size = 0;
};

// The BERT layer: WordPiece tokens of the plain text and their embeddings (model inputs token_ids and attention_mask,
// output encodings). The model takes at most kWindow tokens at once, a longer text goes window by window.
class Bert {
public:
  static constexpr size_t kWindow = 511;

  Bert(InterpreterPtr model, WordPiece wordpiece);

  BertEmbeddings Embed(std::string_view plain);

private:
  InterpreterPtr model_;
  WordPiece wordpiece_;
};

} // namespace giga_edge::vits
