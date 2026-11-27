#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace giga_edge::vits {

struct CodePoint {
  char32_t value = 0;
  size_t size = 0; // bytes
};

// The code point at byte `pos` of UTF-8 text; throws std::invalid_argument on a malformed sequence or `pos` at or past
// the end.
inline CodePoint DecodeUtf8(std::string_view text, size_t pos) {
  if (pos >= text.size()) {
    throw std::invalid_argument("vits: no UTF-8 code point at byte " + std::to_string(pos));
  }
  const auto byte = [text](size_t i) {
    return static_cast<unsigned char>(text[i]);
  };
  const auto lead = byte(pos);
  if (lead < 0x80) {
    return {lead, 1};
  }
  size_t size = 0;
  char32_t value = 0;
  if ((lead & 0xE0) == 0xC0) {
    size = 2;
    value = lead & 0x1F;
  } else if ((lead & 0xF0) == 0xE0) {
    size = 3;
    value = lead & 0x0F;
  } else if ((lead & 0xF8) == 0xF0) {
    size = 4;
    value = lead & 0x07;
  }
  const auto fail = [pos]() {
    throw std::invalid_argument("vits: invalid UTF-8 at byte " + std::to_string(pos));
  };
  if (size == 0 || size > text.size() - pos) {
    fail();
  }
  for (size_t i = 1; i < size; ++i) {
    if ((byte(pos + i) & 0xC0) != 0x80) {
      fail();
    }
    value = (value << 6) | (byte(pos + i) & 0x3F);
  }
  constexpr char32_t min_value[] = {0, 0, 0x80, 0x800, 0x10000};
  if (value < min_value[size] || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
    fail();
  }
  return {value, size};
}

inline bool IsUtf8Boundary(std::string_view text, size_t pos) {
  return pos == text.size() || (pos < text.size() && (static_cast<unsigned char>(text[pos]) & 0xC0) != 0x80);
}

// Russian and English letters.
inline bool IsLetter(char32_t ch) {
  return (ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z') || (ch >= U'А' && ch <= U'я') || ch == U'ё'
    || ch == U'Ё';
}

} // namespace giga_edge::vits
