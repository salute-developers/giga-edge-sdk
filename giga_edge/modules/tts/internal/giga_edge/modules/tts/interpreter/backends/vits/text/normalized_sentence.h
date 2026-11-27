#pragma once

#include <string>

namespace giga_edge::vits {

// One sentence of normalized text. Alphabet: lower-case Russian letters, single spaces, the marks .,;:?!-()" and
// '+' before a stressed vowel where the stress is known. The marks .,;:?!) follow their word without a space, '('
// precedes its word, a dash is " - ". Ends with '.', '?' or '!', maybe followed by closing quotes.
struct NormalizedSentence {
  std::string text;
  int pause_after_ms = 0; // silence after the sentence
};

} // namespace giga_edge::vits
