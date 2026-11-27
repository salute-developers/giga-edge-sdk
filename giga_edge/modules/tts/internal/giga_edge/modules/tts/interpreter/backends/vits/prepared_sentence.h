#pragma once

#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace giga_edge::vits {

struct TextRange {
  size_t begin = 0;
  size_t size = 0; // bytes
};

// A word in both spellings: what BERT reads and what the acoustic model reads.
struct PreparedWord {
  TextRange plain;
  TextRange marked;
};

struct PreparedSentence {
  std::string plain;               // normalized lower-case text with punctuation: the input of BERT
  std::string marked;              // the alphabet of the acoustic model: upper-case stressed vowels, '^' pauses
  std::vector<PreparedWord> words; // in order; everything outside them (spaces, marks, pauses) is not a word
};

// Throws std::invalid_argument unless the words lie in order inside both strings, on code point boundaries.
inline void CheckWords(const PreparedSentence& sentence) {
  size_t plain_end = 0;
  size_t marked_end = 0;
  const auto check = [](const std::string& text, const TextRange& range, size_t& end, size_t word) {
    if (
      range.begin < end || range.begin > text.size() || range.size > text.size() - range.begin
      || !IsUtf8Boundary(text, range.begin) || !IsUtf8Boundary(text, range.begin + range.size)
    ) {
      throw std::invalid_argument("vits: word " + std::to_string(word) + " is out of order or out of its text");
    }
    end = range.begin + range.size;
  };
  for (size_t i = 0; i < sentence.words.size(); ++i) {
    check(sentence.plain, sentence.words[i].plain, plain_end, i);
    check(sentence.marked, sentence.words[i].marked, marked_end, i);
  }
}

} // namespace giga_edge::vits
