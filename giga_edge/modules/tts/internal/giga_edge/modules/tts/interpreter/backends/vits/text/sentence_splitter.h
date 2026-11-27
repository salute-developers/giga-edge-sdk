#pragma once

#include "giga_edge/modules/tts/interpreter/backends/vits/text/normalized_sentence.h"

#include <string>
#include <vector>

namespace giga_edge::vits {

inline constexpr int kMinSentenceChars = 20;
inline constexpr int kMaxSentenceChars = 400;
inline constexpr int kMaxPauseMs = 10000;

struct SentenceParams {
  // A longer sentence is cut at :, or a dash, then at a space, then at the limit. The length is measured as in the
  // training data of the acoustic model: each word or mark is its letters and 2 (stress marks not
  // counted), a space before it 1 but before , : " ); the end mark counts, the dot of a cut piece does not, and a text
  // over the limit by its end mark only, without a clause mark, is not cut. In [kMinSentenceChars, kMaxSentenceChars].
  int max_sentence_chars = 180;
  // In [0, kMaxPauseMs].
  int sentence_pause_ms = 500; // after a sentence: .?! and ;
  int clause_pause_ms = 150;   // after a piece cut at :, or a dash
  int word_pause_ms = 20;      // after a piece cut at a space or at the limit
};

// Throws std::invalid_argument on a value out of its range.
void CheckSentenceParams(const SentenceParams& params);

enum class PieceKind {
  WORD,   // words, one or more: a group that is cut only if it is longer than a sentence
  MARK,   // a mark that does not separate clauses: '-' inside a word, '(', '"'
  CLAUSE, // , : and a dash between words
  END,    // the end of a sentence: '.', '?' or '!'; closing quotes right after it go with it
};

struct Piece {
  std::string text;
  PieceKind kind = PieceKind::WORD;
  bool space_before = false;
};

// Sentences of the pieces, see SentenceParams. Each ends with '.', '?' or '!', maybe followed by closing quotes: a
// piece cut off ends with '.', the last piece of a question keeps its '?'. Only the alphabet of NormalizedSentence
// leaves: anything else in the pieces becomes a space. A dash or a comma at the start of a piece is dropped, pieces
// without letters are left out; the last sentence has no pause.
std::vector<NormalizedSentence> SplitSentences(const std::vector<Piece>& pieces, const SentenceParams& params);

} // namespace giga_edge::vits
