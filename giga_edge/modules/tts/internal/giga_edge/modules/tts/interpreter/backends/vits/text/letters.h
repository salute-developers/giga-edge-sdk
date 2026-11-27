#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace giga_edge::vits {

// A word of the stress stage is made of the lower-case letters а-я and ё only: two bytes each in UTF-8, so letter i
// is bytes 2i and 2i+1.
inline constexpr size_t kLetterBytes = 2;

inline bool IsRussianLower(char32_t ch) {
  return (ch >= U'а' && ch <= U'я') || ch == U'ё';
}

inline bool IsVowel(char32_t ch) {
  return std::u32string_view(U"аеёиоуыэюя").find(ch) != std::u32string_view::npos;
}

inline size_t LetterCount(std::string_view word) {
  return word.size() / kLetterBytes;
}

inline char32_t LetterAt(std::string_view word, size_t letter) {
  const auto lead = static_cast<unsigned char>(word[letter * kLetterBytes]);
  const auto next = static_cast<unsigned char>(word[letter * kLetterBytes + 1]);
  return static_cast<char32_t>(((lead & 0x1F) << 6) | (next & 0x3F));
}

// Whether the two bytes at `pos` are one symbol of UTF-8: only then LetterAt reads a letter there.
inline bool IsTwoByteSymbol(std::string_view text, size_t pos) {
  return pos + 1 < text.size() && (static_cast<unsigned char>(text[pos]) & 0xE0) == 0xC0
    && (static_cast<unsigned char>(text[pos + 1]) & 0xC0) == 0x80;
}

inline bool IsRussianWord(std::string_view word) {
  if (word.empty() || word.size() % kLetterBytes != 0) {
    return false;
  }
  for (size_t i = 0; i < LetterCount(word); ++i) {
    if (!IsTwoByteSymbol(word, i * kLetterBytes) || !IsRussianLower(LetterAt(word, i))) {
      return false;
    }
  }
  return true;
}

inline std::string WithoutMarks(std::string_view stressed) {
  std::string text;
  text.reserve(stressed.size());
  for (const char symbol : stressed) {
    if (symbol != '+') {
      text += symbol;
    }
  }
  return text;
}

// A word with its stress marks: Russian letters, every '+' before a vowel ("зам+ок").
inline bool IsStressedWord(std::string_view stressed) {
  for (size_t i = 0; i < stressed.size(); ++i) {
    if (stressed[i] == '+' && (stressed.size() - i <= kLetterBytes || !IsVowel(LetterAt(stressed.substr(i + 1), 0)))) {
      return false;
    }
  }
  return IsRussianWord(WithoutMarks(stressed));
}

inline bool HasYo(std::string_view text) {
  return text.find("ё") != std::string_view::npos;
}

// "ё" -> "е": words are compared without the difference.
inline std::string FoldYo(std::string_view text) {
  std::string result(text);
  for (auto pos = result.find("ё"); pos != std::string::npos; pos = result.find("ё", pos)) {
    result.replace(pos, kLetterBytes, "е");
  }
  return result;
}

} // namespace giga_edge::vits
